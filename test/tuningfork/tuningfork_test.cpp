/*
 * Copyright 2019 The Android Open Source Project
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
#include "tf_test_utils.h"

#include "proto/protobuf_util.h"
#include "core/tuningfork_internal.h"
#include "core/memory_telemetry.h"
#include "http_backend/http_backend.h"

#include "full/tuningfork.pb.h"
#include "full/dev_tuningfork.pb.h"

#include "gtest/gtest.h"

#include <vector>
#include <mutex>

#ifndef LOG_TAG
#define LOG_TAG "TFTest"
#endif

#include "Log.h"

using namespace tuningfork;
using namespace test;

namespace tuningfork_test {

using ::com::google::tuningfork::FidelityParams;
using ::com::google::tuningfork::Annotation;
typedef std::string TuningForkLogEvent;

static const std::string kCacheDir = "/data/local/tmp/tuningfork_test";
const Duration s_test_wait_time = std::chrono::seconds(1);

class TestBackend : public IBackend {
public:
    TestBackend(std::shared_ptr<std::condition_variable> cv_,
                std::shared_ptr<std::mutex> mutex_,
                std::shared_ptr<IBackend> dl_backend_ = std::shared_ptr<IBackend>())
                 : cv(cv_), mutex(mutex_), dl_backend(dl_backend_) {}

    TuningFork_ErrorCode UploadTelemetry(const TuningForkLogEvent &evt_ser) override {
        ALOGI("Process");
        {
            std::lock_guard<std::mutex> lock(*mutex);
            result = evt_ser;
        }
        cv->notify_all();
        return TUNINGFORK_ERROR_OK;
    }

    TuningFork_ErrorCode GenerateTuningParameters(Request& request,
                                  const ProtobufSerialization* training_mode_params,
                                  ProtobufSerialization &fidelity_params,
                                  std::string& experiment_id) override {
        if (dl_backend) {
            return dl_backend->GenerateTuningParameters(request, training_mode_params, fidelity_params,
                                                 experiment_id);
        } else {
            return TUNINGFORK_ERROR_OK;
        }
    }

    TuningFork_ErrorCode UploadDebugInfo(Request& request) override {
        return TUNINGFORK_ERROR_OK;
    }

    void Clear() { result = ""; }

    void SetDownloadBackend(const std::shared_ptr<IBackend>& dl) {
        dl_backend = dl;
    }

    TuningForkLogEvent result;
    std::shared_ptr<std::condition_variable> cv;
    std::shared_ptr<std::mutex> mutex;
    std::shared_ptr<IBackend> dl_backend;
};

class TestDownloadBackend : public IBackend {
public:
    int n_times_called_ = 0;
    int wait_count_ = 0;
    const ProtobufSerialization* download_params_;
    const ProtobufSerialization* expected_training_params_;
    TestDownloadBackend(int wait_count = 0,
                     const ProtobufSerialization* download_params = nullptr,
                     const ProtobufSerialization* expected_training_params = nullptr) :
            wait_count_(wait_count),
            download_params_(download_params),
            expected_training_params_(expected_training_params) {}

    TuningFork_ErrorCode GenerateTuningParameters(Request& request,
                                  const ProtobufSerialization* training_mode_params,
                                  ProtobufSerialization &fidelity_params,
                                  std::string& experiment_id) override {
        n_times_called_++;
        if (expected_training_params_ != nullptr) {
            EXPECT_NE(training_mode_params, nullptr);
            EXPECT_EQ(*expected_training_params_, *training_mode_params);
        } else {
            EXPECT_EQ(training_mode_params, nullptr);
        }
        if (n_times_called_ > wait_count_) {
            if (download_params_!=nullptr) {
                fidelity_params = *download_params_;
                return TUNINGFORK_ERROR_OK;
            }
            else {
                return TUNINGFORK_ERROR_NO_FIDELITY_PARAMS;
            }
        }
        else {
            return TUNINGFORK_ERROR_GENERATE_TUNING_PARAMETERS_ERROR;
        }
    }

    TuningFork_ErrorCode UploadTelemetry(const TuningForkLogEvent &evt_ser) override {
        return TUNINGFORK_ERROR_OK;
    }

    TuningFork_ErrorCode UploadDebugInfo(Request& request) override {
        return TUNINGFORK_ERROR_OK;
    }

};

// Increment time with a known tick size
class TestTimeProvider : public ITimeProvider {
public:
    TestTimeProvider(Duration tick_size_ = std::chrono::milliseconds(20),
                     SystemDuration system_tick_size_ = std::chrono::milliseconds(20))
        : tick_size(tick_size_), system_tick_size(system_tick_size_) {}

    TimePoint t;
    SystemTimePoint st;
    Duration tick_size;
    SystemDuration system_tick_size;

    TimePoint Now() override {
        t += tick_size;
        return t;
    }
    SystemTimePoint SystemNow() override {
        st += system_tick_size;
        return st;
    }
    void Reset() {
        t = TimePoint();
        st = SystemTimePoint();
    }
};

class TestMemInfoProvider : public DefaultMemInfoProvider {
    uint64_t result_;
  public:
    TestMemInfoProvider(bool enabled) : result_(0) {
        SetEnabled(enabled);
        SetDeviceMemoryBytes(8000000000);
    }
    uint64_t GetNativeHeapAllocatedSize() override {
        // Crank up the memory with each call
        result_ += 100000000L;
        return result_;
    }
};

class TuningForkTest {
  public:
    std::shared_ptr<std::condition_variable> cv_ = std::make_shared<std::condition_variable>();
    std::shared_ptr<std::mutex> rmutex_ = std::make_shared<std::mutex>();
    TestBackend test_backend_;
    TestTimeProvider time_provider_;
    TestMemInfoProvider meminfo_provider_;
    ExtraUploadInfo extra_upload_info_;
    TuningFork_ErrorCode init_return_value_;

    TuningForkTest(const Settings& settings, Duration tick_size = std::chrono::milliseconds(20),
                   const std::shared_ptr<TestDownloadBackend>& download_backend
                   = std::make_shared<TestDownloadBackend>(),
                  bool enable_meminfo = false)
            : test_backend_(cv_, rmutex_, download_backend),
              time_provider_(tick_size), meminfo_provider_(enable_meminfo), extra_upload_info_({}) {
        init_return_value_ = tuningfork::Init(settings, &extra_upload_info_, &test_backend_,
                                              &time_provider_,
                                              &meminfo_provider_);
        EXPECT_EQ(init_return_value_, TUNINGFORK_ERROR_OK) << "Bad Init";
    }

    ~TuningForkTest() {
        if (init_return_value_ == TUNINGFORK_ERROR_OK) {
            tuningfork::Destroy();
            tuningfork::KillDownloadThreads();
        }
    }

    TuningForkLogEvent Result() const {
        return test_backend_.result;
    }

};

const TuningFork_CProtobufSerialization* TrainingModeParams() {
    static ProtobufSerialization pb = {1,2,3,4,5};
    static TuningFork_CProtobufSerialization cpb = {};
    cpb.bytes = pb.data();
    cpb.size = pb.size();
    return &cpb;
}

Settings TestSettings(Settings::AggregationStrategy::Submission method, int n_ticks, int n_keys,
                      std::vector<uint32_t> annotation_size,
                      const std::vector<Settings::Histogram>& hists = {}) {
    Settings s {};
    s.aggregation_strategy.method = method;
    s.aggregation_strategy.intervalms_or_count = n_ticks;
    s.aggregation_strategy.max_instrumentation_keys = n_keys;
    s.aggregation_strategy.annotation_enum_size = annotation_size;
    s.histograms = hists;
    s.c_settings.training_fidelity_params = TrainingModeParams();
    s.Check(kCacheDir);
    s.initial_request_timeout_ms = 5;
    s.ultimate_request_timeout_ms = 50;
    return s;
}

TuningForkLogEvent TestEndToEnd() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    auto settings = TestSettings(Settings::AggregationStrategy::Submission::TICK_BASED, NTICKS - 1,
                                 1, {});
    TuningForkTest test(settings);
    std::unique_lock<std::mutex> lock(*test.rmutex_);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_RAW_FRAME_TIME);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(test.cv_->wait_for(lock, s_test_wait_time)==std::cv_status::no_timeout)
        << "Timeout";

    return test.Result();
}

TuningForkLogEvent TestEndToEndWithAnnotation() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    // {3} is the number of values in the Level enum in tuningfork_extensions.proto
    auto settings = TestSettings(Settings::AggregationStrategy::Submission::TICK_BASED, NTICKS - 1,
                                 2, {3});
    TuningForkTest test(settings, std::chrono::milliseconds(10));
    Annotation ann;
    ann.set_level(com::google::tuningfork::LEVEL_1);
    tuningfork::SetCurrentAnnotation(Serialize(ann));
    std::unique_lock<std::mutex> lock(*test.rmutex_);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_PACED_FRAME_TIME);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(test.cv_->wait_for(lock, s_test_wait_time)==std::cv_status::no_timeout)
        << "Timeout";

    return test.Result();
}

TuningForkLogEvent TestEndToEndTimeBased() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    auto settings = TestSettings(Settings::AggregationStrategy::Submission::TIME_BASED, 10100,
                                 1, {}, {{TFTICK_RAW_FRAME_TIME, 50,150,10}});
    TuningForkTest test(settings, std::chrono::milliseconds(100));
    std::unique_lock<std::mutex> lock(*test.rmutex_);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_RAW_FRAME_TIME);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(test.cv_->wait_for(lock, s_test_wait_time)==std::cv_status::no_timeout)
        << "Timeout";

    return test.Result();
}

TuningForkLogEvent TestEndToEndWithMemory() {
    const int NTICKS = 101; // note the first tick doesn't add anything to the histogram
    auto settings = TestSettings(Settings::AggregationStrategy::Submission::TICK_BASED, NTICKS - 1,
                                 1, {});
    TuningForkTest test(settings,  std::chrono::milliseconds(20),
                        std::make_shared<TestDownloadBackend>(), /*enable_meminfo*/ true);
    std::unique_lock<std::mutex> lock(*test.rmutex_);
    for (int i = 0; i < NTICKS; ++i)
        tuningfork::FrameTick(TFTICK_RAW_FRAME_TIME);
    // Wait for the upload thread to complete writing the string
    EXPECT_TRUE(test.cv_->wait_for(lock, s_test_wait_time)==std::cv_status::no_timeout)
        << "Timeout";

    return test.Result();
}

bool CheckStrings(const std::string& name, const std::string& result,
                  const std::string& expected) {
    bool comp = CompareIgnoringWhitespace(result, expected);
    EXPECT_TRUE( comp ) << "\nResult:\n" << result << "\n!=\nExpected:" << expected;
    return comp;
}

static std::string ReplaceReturns(std::string in) {
    std::replace( in.begin(), in.end(), '\n', ' ');
    return in;
}

static const std::string session_context = R"TF(
{
  "device": {
    "brand": "",
    "build_version": "",
    "cpu_core_freqs_hz": [],
    "device": "",
    "fingerprint": "",
    "gles_version": {
      "major": 0,
      "minor": 0
    },
    "model": "",
    "product": "",
    "total_memory_bytes": 0
  },
  "game_sdk_info": {
    "session_id": "",
    "version": "1.0"
  },
  "time_period": {
    "end_time": "1970-01-01T00:00:02.020000Z",
    "start_time": "1970-01-01T00:00:00.020000Z"
  }
}
)TF";

TEST(TuningForkTest, EndToEnd) {
    auto result = TestEndToEnd();
    TuningForkLogEvent expected = R"TF(
{
  "name": "applications//apks/0",
  "session_context":)TF" + session_context + R"TF(,
  "telemetry": [{
    "context": {
      "annotations": "",
      "duration": "2s",
      "tuning_parameters": {
        "experiment_id": "",
        "serialized_fidelity_parameters": ""
      }
    },
    "report": {
      "rendering": {
        "render_time_histogram": [{
         "counts": [
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
         "instrument_id": 64000
        }]
      }
    }
  }]
}
)TF";
    CheckStrings("Base", result, expected);
}

TEST(TuningForkTest, TestEndToEndWithAnnotation) {
    auto result = TestEndToEndWithAnnotation();
    TuningForkLogEvent expected = R"TF(
{
  "name": "applications//apks/0",
  "session_context":)TF" + session_context + R"TF(,
  "telemetry": [{
    "context": {
      "annotations": "CAE=",
      "duration": "1s",
      "tuning_parameters": {
        "experiment_id": "",
        "serialized_fidelity_parameters": ""
      }
    },
    "report": {
      "rendering": {
        "render_time_histogram": [{
         "counts": [
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
         "instrument_id": 64001
        }]
      }
    }
  }]
}
)TF";
    CheckStrings("Annotation", result, expected);
}

TEST(TuningForkTest, TestEndToEndTimeBased) {
    auto result = TestEndToEndTimeBased();
    TuningForkLogEvent expected = R"TF(
{
  "name": "applications//apks/0",
  "session_context":)TF" + session_context + R"TF(,
  "telemetry": [{
    "context": {
      "annotations": "",
      "duration": "10s",
      "tuning_parameters": {
        "experiment_id": "",
        "serialized_fidelity_parameters": ""
      }
    },
    "report": {
      "rendering": {
        "render_time_histogram": [{
         "counts": [
           0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0],
         "instrument_id": 64000
        }]
      }
    }
  }]
}
)TF";
    CheckStrings("TimeBased", result, expected);
}

static std::condition_variable fp_cv;
static std::mutex fp_mutex;
static int fp_n_callbacks_called = 0;
ProtobufSerialization default_fps = {1,2,3};
void FidelityParamsCallback(const TuningFork_CProtobufSerialization* s) {
    EXPECT_NE(s, nullptr) << "Null FPs in callback";
    auto in_fps = ToProtobufSerialization(*s);
    EXPECT_EQ(in_fps, default_fps) << "Wrong FPs in callback";
    fp_n_callbacks_called++;
    fp_cv.notify_all();
}

void TestFidelityParamDownloadThread(bool insights) {

    TuningFork_CProtobufSerialization c_default_fps;
    ToCProtobufSerialization(default_fps, c_default_fps);

    auto settings = TestSettings(Settings::AggregationStrategy::Submission::TICK_BASED, 100, 1, {});

    settings.api_key = "dummy_api_key";
    settings.c_settings.training_fidelity_params = nullptr;

    auto download_backend = insights?std::make_shared<TestDownloadBackend>(/*wait_count*/0, nullptr):
                                  std::make_shared<TestDownloadBackend>(/*wait_count*/3, &default_fps);

    TuningForkTest test(settings, std::chrono::milliseconds(20), download_backend);

    auto r = TuningFork_startFidelityParamDownloadThread(nullptr, FidelityParamsCallback);
    EXPECT_EQ(r, TUNINGFORK_ERROR_BAD_PARAMETER);
    std::unique_lock<std::mutex> lock(fp_mutex);
    r = TuningFork_startFidelityParamDownloadThread(&c_default_fps, FidelityParamsCallback);
    EXPECT_EQ(r, TUNINGFORK_ERROR_OK);

    fp_n_callbacks_called = 0;

    if (insights) {
        EXPECT_TRUE(fp_cv.wait_for(lock, s_test_wait_time)!=std::cv_status::timeout)
          << "Timeout";
        EXPECT_EQ(fp_n_callbacks_called, 1);
        EXPECT_EQ(download_backend->n_times_called_, 1);
    }
    else {
        // Wait for the download thread. We have one initial callback with the defaults and
        //  one with the ones from the loader.
        for(int i=0;i<2;++i) {
            EXPECT_TRUE(fp_cv.wait_for(lock, s_test_wait_time)!=std::cv_status::timeout)
              << "Timeout";
        }
        EXPECT_EQ(fp_n_callbacks_called, 2);
        EXPECT_EQ(download_backend->n_times_called_, 4);
    }

    TuningFork_CProtobufSerialization_free(&c_default_fps);
}

TEST(TuningForkTest, TestFidelityParamDownloadThread) {
    TestFidelityParamDownloadThread(/*insights*/true);
    TestFidelityParamDownloadThread(/*insights*/false);
}

struct TestResponse {
    std::string request;
    int response_code;
    std::string response_body;
};

class TestRequest: public Request {
    std::vector<TestResponse> responses_;
    int next_response_ = 0;
  public:
    TestRequest(const Request& r, std::vector<TestResponse> responses)
            : Request(r), responses_(responses) {}
    TuningFork_ErrorCode Send(const std::string& rpc_name, const std::string& request,
              int& response_code, std::string& response_body) override {
        EXPECT_LT(next_response_, responses_.size()) << "Unexpected request";
        if (next_response_<responses_.size()) {
            auto& expected = responses_[next_response_];
            if (CheckStrings("DownloadRequest", request, expected.request)) {
                response_code = expected.response_code;
                response_body = expected.response_body;
                ++next_response_;
            }
        }
        return TUNINGFORK_ERROR_OK;
    }
};

static const std::string empty_tuning_parameters_request = R"({
  "device_spec": {
    "brand": "",
    "build_version": "",
    "cpu_core_freqs_hz": [],
    "device": "",
    "fingerprint": "",
    "gles_version": {
      "major": 0,
      "minor": 0
    },
    "model": "",
    "product": "",
    "total_memory_bytes": 0
  },
  "name": "applications//apks/0"
})";

TEST(TuningForkTest, TestFidelityParamDownloadRequest) {
    HttpBackend backend;
    ExtraUploadInfo info ({});
    Request inner_request(info, "https://test.google.com", "dummy_api_key",
                          std::chrono::milliseconds(1000));
    TestRequest request(inner_request, {{empty_tuning_parameters_request, 200, "out"}});
    ProtobufSerialization fps;
    std::string experiment_id;
    backend.GenerateTuningParameters(request, nullptr, fps, experiment_id);
}

TEST(TuningForkTest, EndToEndWithMemory) {
    auto result = TestEndToEndWithMemory();
    TuningForkLogEvent expected = R"TF(
{
  "name": "applications//apks/0",
  "session_context":)TF" + session_context + R"TF(,
  "telemetry": [{
    "context": {
      "annotations": "",
      "duration": "2s",
      "tuning_parameters": {
        "experiment_id": "",
        "serialized_fidelity_parameters": ""
      }
    },
    "report": {
      "rendering": {
        "render_time_histogram": [{
         "counts": [
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
         "instrument_id": 64000
        }]
      }
    }
  },{
    "context": {
      "annotations": "",
      "duration": "2s",
      "tuning_parameters": {
        "experiment_id": "",
        "serialized_fidelity_parameters": ""
      }
    },
    "report": {
      "memory": {
        "memory_histogram": [{
          "counts": [0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0,
                     1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1,
                     0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0,
                     1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1,
                     0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0,
                     0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0,
                     1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1,
                     0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 22],
         "histogram_config": {
          "bucket_max_bytes": "8000000000",
          "bucket_min_bytes": "0"
         },
         "period_ms": 16,
         "type": 1
        }]
      }
    }
  }]
}
)TF";
    CheckStrings("WithMemory", result, expected);
}

} // namespace tuningfork_test
