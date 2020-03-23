/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *
 * ScheduleAffinityOperation
 *
 * This test aims to determine reliability of thread pinning. E.g., if a thread
 * is pinned to a specific core, will the os respect that assignment?
 *
 * Basic operation is to spawn N threads per CPU, pin them to that CPU, have
 * them run some busy work and periodically report what CPU they're executing
 * on.
 *
 * Inputs:
 *
 * configuration:
 *   thread_count: [int] count of threads to spawn per cpu
 *   report_frequency: [Duration] Period of time between reporting cpu thread
 *     is running on
 *
 * Outputs:
 *
 * datum:
 *   message:[string] The action being reported, one of:
 *     "spawning_started": start of test, about to start work threads
 *     "spawning_batch": starting a batch of work threads for a cpu
 *     "setting_affinity": about to set affinity of work thread X
 *     "did_set_affinity": finished setting affinity for work thread X
 *     "work_started": work starting for a worker thread
 *     "work_running": work running (this is sent periodically
 *       while test executes)
 *     "work_finished": work finished for worker thread
 *   expected_cpu:[int]: if >= 0 cpu this worker was expected to run on
 *   thread_affinity_set: [bool] if true, expected_cpu should be checked against
 *     the datum payload's cpu id for veracity
 *
 */

#include <condition_variable>
#include <mutex>
#include <sched.h>
#include <thread>

#include <cpu-features.h>

#include <ancer/BaseOperation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/util/Basics.hpp>
#include <ancer/util/Log.hpp>
#include <ancer/util/Json.hpp>
#include <ancer/util/Time.hpp>

using namespace ancer;

//==============================================================================

namespace {
constexpr Log::Tag TAG{"schedule_affinity"};
}

//==============================================================================

namespace {
struct configuration {
  // How many threads should we run at one time?
  int thread_count = 1;

  // How often should threads report in?
  Milliseconds report_frequency = 100ms;
};

JSON_READER(configuration) {
  JSON_OPTVAR(thread_count);
  JSON_OPTVAR(report_frequency);
}

//==============================================================================

struct datum {
  const char* message;
  int expected_cpu;
  bool thread_affinity_set = false;
};

void WriteDatum(report_writers::Struct w, const datum& d) {
    ADD_DATUM_MEMBER(w, d, message);
    ADD_DATUM_MEMBER(w, d, expected_cpu);
    ADD_DATUM_MEMBER(w, d, thread_affinity_set);
}
} // anonymous namespace

//==============================================================================

class ScheduleAffinityOperation : public BaseOperation {
  void Start() override {
    BaseOperation::Start();

    _thread = std::thread{[this] {
      // NOTE: This will probably change since it was never explicitly
      // assigned, but it doesn't hurt to note what the spawning thread
      // is doing.
      const auto original_cpu = sched_getcpu();
      Report(datum{"spawning_started", original_cpu});
      const auto c = GetConfiguration<configuration>();
      const auto time_to_run = GetDuration();

      std::vector<std::thread> threads;
      threads.reserve(c.thread_count);

      const int cpu_count = android_getCpuCount();
      for (int i = 0; i < cpu_count;) {
        if (IsStopped()) break;

        Report(datum{"spawning_batch", original_cpu});

        const auto range_start = i;
        const auto range_end = range_start + c.thread_count;
        for (; i < range_end; ++i) {
          // When we're running the last CPUs, cycle back to the
          // beginning if we need to grab a few extra to keep
          // things consistent.
          const auto cpu = i % cpu_count;
          // We're looping on ourselves, which means we've requested
          // more CPUs than we actually have. Just stop the loop and
          // we should be running everything.
          if (i >= cpu_count && cpu >= range_start) break;

          threads.emplace_back(
              [this, cpu = cpu, &c, time_to_run] {
                Report(datum{"setting_affinity", cpu, false});
                bool success = ancer::SetThreadAffinity(cpu);
                Report(datum{"did_set_affinity", cpu, success});

                Report(datum{"work_started", cpu, success});
                StressCpu(time_to_run, c.report_frequency, cpu, success);
                Report(datum{"work_finished", cpu, success});
              });
        }

        // Wait for this batch to finish
        for (auto& thread : threads) {
          thread.join();
        }
        threads.clear();
      }

      Report(datum{"spawning_finished", original_cpu});
    }};
  }

  void Wait() override {
    _thread.join();
  }

  std::thread _thread;

//==============================================================================

  // Workload copied from CalculatePiOperation
  void StressCpu(
      const Duration duration,
      const Duration report_freq,
      const int cpu,
      bool did_set_thread_affinity) {

    const auto start = SteadyClock::now();
    const auto end = start + duration;
    auto next_report = start + report_freq;

    long d = 3;
    long i = 0;
    double accumulator = 1.0;
    while (true) {
      if (IsStopped()) break;

      const auto now = SteadyClock::now();
      if (now >= end) break;
      if (now >= next_report) {
        next_report = now + report_freq;
        Report(datum{"work_running", cpu, did_set_thread_affinity});
      }

      const double factor = 1.0 / static_cast<double>(d);
      d += 2;
      accumulator += (i++ % 2) ? factor : -factor;
    }

    // Make sure we don't optimize away the above work.
    ForceCompute(accumulator);
  }
};

EXPORT_ANCER_OPERATION(ScheduleAffinityOperation)
