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

#ifndef marching_cubes_job_h
#define marching_cubes_job_h

#include <ancer/util/MarchingCubesThreadPool.hpp>

namespace marching_cubes::job {

class MarchingCubesJobQueue {
 public:
  using WorkFn = MarchingCubesThreadPool::WorkFn;
 public:
    MarchingCubesJobQueue(ancer::ThreadAffinity affinity,
      bool pinned, int max_thread_count,
      MarchingCubesThreadPool::SleepConfig sleep_config) {

    _thread_pool = std::make_unique<MarchingCubesThreadPool>(
        affinity, pinned, max_thread_count, sleep_config);

    _executing_jobs = &_jobs_0;
    _queueing_jobs = &_jobs_1;
  }
  MarchingCubesJobQueue() = delete;
  MarchingCubesJobQueue(const MarchingCubesJobQueue&) = delete;
    MarchingCubesJobQueue(MarchingCubesJobQueue&&) = delete;
  ~MarchingCubesJobQueue() = default;

  void RunAllReadiedJobs() {

    {
      // swap queueing and executing
      std::lock_guard guard(_lock);
      std::swap(_executing_jobs, _queueing_jobs);
    }

    _thread_pool->EnqueueAll(*_executing_jobs);
    _executing_jobs->clear();
    _thread_pool->Wait();
  }

  template<class F>
  void AddJob(F &&work) {
    std::lock_guard guard(_lock);
    _queueing_jobs->emplace_back(std::forward<F>(work));
  }

  size_t NumThreads() const { return _thread_pool->NumThreads(); }

 private:
  std::mutex _lock;
  std::unique_ptr<MarchingCubesThreadPool> _thread_pool;
  std::vector<WorkFn> _jobs_0;
  std::vector<WorkFn> _jobs_1;

  std::vector<WorkFn>* _executing_jobs;
  std::vector<WorkFn>* _queueing_jobs;
};

} // end namespace marching_cubes::job

#endif