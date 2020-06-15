/*
 * Copyright 2018 The Android Open Source Project
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

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include "tuningfork_internal.h"

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "Trace.h"
#include "annotation_util.h"
#include "crash_handler.h"
#include "histogram.h"
#include "http_backend/http_backend.h"
#include "prong.h"
#include "tuningfork_swappy.h"
#include "tuningfork_utils.h"
#include "uploadthread.h"

/* Annotations come into tuning fork as a serialized protobuf. The protobuf can
 * only have enums in it. We form an integer annotation id from the annotation
 * interpreted as a mixed-radix number. E.g. say we have the following in the
 * proto: enum A { A_1 = 1, A_2 = 2, A_3 = 3}; enum B { B_1 = 1, B_2 = 2}; enum
 * C { C_1 = 1}; message Annotation { optional A a = 1; optional B b = 2;
 * optional C c = 3}; Then a serialization of 'b : B_1' might be: 0x16 0x01
 * https://developers.google.com/protocol-buffers/docs/encoding
 * Note the shift of 3 bits for the key.
 *
 * Assume we have 2 possible instrumentation keys: NUM_IKEY = 2
 *
 * The annotation id then is (0 + 1*4 + 0)*NUM_IKEY = 8, where the factor of 4
 * comes from the radix associated with 'a', i.e. 3 values for enum A + option
 * missing
 *
 * A compound id is formed from the annotation id and the instrument key:
 * compound_id = annotation_id + instrument_key;
 *
 * So for instrument key 1, the compound_id with the above annotation is 9
 *
 * This compound_id is used to look up a histogram in the ProngCache.
 *
 * annotation_radix_mult_ stores the multiplied radixes, so for the above, it
 * is: [4 4*3 4*3*2] = [4 12 24] and the maximum number of annotations is 24
 *
 * */

namespace tuningfork {

typedef uint64_t AnnotationId;

class ChronoTimeProvider : public ITimeProvider {
   public:
    virtual TimePoint Now() { return std::chrono::steady_clock::now(); }
    virtual SystemTimePoint SystemNow() {
        return std::chrono::system_clock::now();
    }
};

class TuningForkImpl : public IdProvider {
   private:
    CrashHandler crash_handler_;
    Settings settings_;
    std::unique_ptr<ProngCache> prong_caches_[2];
    ProngCache *current_prong_cache_;
    TimePoint last_submit_time_;
    std::unique_ptr<gamesdk::Trace> trace_;
    std::vector<TimePoint> live_traces_;
    IBackend *backend_;
    UploadThread upload_thread_;
    SerializedAnnotation current_annotation_;
    std::vector<uint32_t> annotation_radix_mult_;
    AnnotationId current_annotation_id_;
    ITimeProvider *time_provider_;
    IMemInfoProvider *meminfo_provider_;
    std::vector<InstrumentationKey> ikeys_;
    std::atomic<int> next_ikey_;
    TimePoint loading_start_;
    std::unique_ptr<ProtobufSerialization> training_mode_params_;

   public:
    TuningForkImpl(const Settings &settings, IBackend *backend,
                   ITimeProvider *time_provider,
                   IMemInfoProvider *memory_provider);

    void InitHistogramSettings();

    void InitAnnotationRadixes();

    void InitTrainingModeParams();

    // Returns true if the fidelity params were retrieved
    TuningFork_ErrorCode GetFidelityParameters(
        const ProtobufSerialization &defaultParams,
        ProtobufSerialization &fidelityParams, uint32_t timeout_ms);

    // Returns the set annotation id or -1 if it could not be set
    uint64_t SetCurrentAnnotation(const ProtobufSerialization &annotation);

    TuningFork_ErrorCode FrameTick(InstrumentationKey id);

    TuningFork_ErrorCode FrameDeltaTimeNanos(InstrumentationKey id,
                                             Duration dt);

    // Fills handle with that to be used by EndTrace
    TuningFork_ErrorCode StartTrace(InstrumentationKey key,
                                    TraceHandle &handle);

    TuningFork_ErrorCode EndTrace(TraceHandle);

    void SetUploadCallback(TuningFork_UploadCallback cbk);

    TuningFork_ErrorCode Flush();

    TuningFork_ErrorCode Flush(TimePoint t, bool upload);

    const Settings &GetSettings() const { return settings_; }

    TuningFork_ErrorCode SetFidelityParameters(
        const ProtobufSerialization &params);

    TuningFork_ErrorCode EnableMemoryRecording(bool enable);

   private:
    Prong *TickNanos(uint64_t compound_id, TimePoint t);

    Prong *TraceNanos(uint64_t compound_id, Duration dt);

    TuningFork_ErrorCode CheckForSubmit(TimePoint t, Prong *prong);

    bool ShouldSubmit(TimePoint t, Prong *prong);

    AnnotationId DecodeAnnotationSerialization(const SerializedAnnotation &ser,
                                               bool *loading) const override;

    uint32_t GetInstrumentationKey(uint64_t compoundId) {
        return compoundId %
               settings_.aggregation_strategy.max_instrumentation_keys;
    }

    TuningFork_ErrorCode MakeCompoundId(InstrumentationKey k, AnnotationId a,
                                        uint64_t &id) override {
        int key_index;
        auto err = GetOrCreateInstrumentKeyIndex(k, key_index);
        if (err != TUNINGFORK_ERROR_OK) return err;
        id = key_index + a;
        return TUNINGFORK_ERROR_OK;
    }

    SerializedAnnotation SerializeAnnotationId(uint64_t);

    bool keyIsValid(InstrumentationKey key) const;

    TuningFork_ErrorCode GetOrCreateInstrumentKeyIndex(InstrumentationKey key,
                                                       int &index);

    bool LoadingNextScene() const { return loading_start_ != TimePoint::min(); }

    bool IsLoadingAnnotationId(uint64_t id) const;

    void SwapProngCaches();

    bool Debugging() const;
};

static std::unique_ptr<TuningForkImpl> s_impl;
static std::unique_ptr<ChronoTimeProvider> s_chrono_time_provider =
    std::make_unique<ChronoTimeProvider>();
static HttpBackend s_backend;
static std::unique_ptr<SwappyTraceWrapper> s_swappy_tracer;
static std::unique_ptr<DefaultMemInfoProvider> s_meminfo_provider =
    std::make_unique<DefaultMemInfoProvider>();

TuningFork_ErrorCode Init(const Settings &settings,
                          const RequestInfo *request_info, IBackend *backend,
                          ITimeProvider *time_provider,
                          IMemInfoProvider *meminfo_provider) {
    if (s_impl.get() != nullptr) return TUNINGFORK_ERROR_ALREADY_INITIALIZED;

    if (request_info != nullptr) {
        RequestInfo::CachedValue() = *request_info;
    } else {
        RequestInfo::CachedValue() = RequestInfo::ForThisGameAndDevice();
    }

    if (backend == nullptr) {
        TuningFork_ErrorCode err = s_backend.Init(settings);
        if (err == TUNINGFORK_ERROR_OK) {
            ALOGI("TuningFork.GoogleEndpoint: OK");
            backend = &s_backend;
        } else {
            ALOGE("TuningFork.GoogleEndpoint: FAILED");
            return err;
        }
    }

    if (time_provider == nullptr) {
        time_provider = s_chrono_time_provider.get();
    }

    if (meminfo_provider == nullptr) {
        meminfo_provider = s_meminfo_provider.get();
        meminfo_provider->SetDeviceMemoryBytes(
            RequestInfo::CachedValue().total_memory_bytes);
    }

    s_impl = std::make_unique<TuningForkImpl>(settings, backend, time_provider,
                                              meminfo_provider);

    // Set up the Swappy tracer after TuningFork is initialized
    if (settings.c_settings.swappy_tracer_fn != nullptr) {
        s_swappy_tracer = std::unique_ptr<SwappyTraceWrapper>(
            new SwappyTraceWrapper(settings));
    }
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode GetFidelityParameters(
    const ProtobufSerialization &defaultParams, ProtobufSerialization &params,
    uint32_t timeout_ms) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->GetFidelityParameters(defaultParams, params, timeout_ms);
    }
}

TuningFork_ErrorCode FrameTick(InstrumentationKey id) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->FrameTick(id);
    }
}

TuningFork_ErrorCode FrameDeltaTimeNanos(InstrumentationKey id, Duration dt) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->FrameDeltaTimeNanos(id, dt);
    }
}

TuningFork_ErrorCode StartTrace(InstrumentationKey key, TraceHandle &handle) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->StartTrace(key, handle);
    }
}

TuningFork_ErrorCode EndTrace(TraceHandle h) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->EndTrace(h);
    }
}

TuningFork_ErrorCode SetCurrentAnnotation(const ProtobufSerialization &ann) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        if (s_impl->SetCurrentAnnotation(ann) == -1) {
            return TUNINGFORK_ERROR_INVALID_ANNOTATION;
        } else {
            return TUNINGFORK_ERROR_OK;
        }
    }
}

TuningFork_ErrorCode SetUploadCallback(TuningFork_UploadCallback cbk) {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        s_impl->SetUploadCallback(cbk);
        return TUNINGFORK_ERROR_OK;
    }
}

TuningFork_ErrorCode Flush() {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        return s_impl->Flush();
    }
}

TuningFork_ErrorCode Destroy() {
    if (!s_impl) {
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    } else {
        s_backend.KillThreads();
        s_impl.reset();
        return TUNINGFORK_ERROR_OK;
    }
}

const Settings *GetSettings() {
    if (!s_impl)
        return nullptr;
    else
        return &s_impl->GetSettings();
}

TuningFork_ErrorCode SetFidelityParameters(
    const ProtobufSerialization &params) {
    if (!s_impl)
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    else
        return s_impl->SetFidelityParameters(params);
}

TuningFork_ErrorCode EnableMemoryRecording(bool enable) {
    if (!s_impl)
        return TUNINGFORK_ERROR_TUNINGFORK_NOT_INITIALIZED;
    else
        return s_impl->EnableMemoryRecording(enable);
}

TuningForkImpl::TuningForkImpl(const Settings &settings, IBackend *backend,
                               ITimeProvider *time_provider,
                               IMemInfoProvider *meminfo_provider)
    : settings_(settings),
      trace_(gamesdk::Trace::create()),
      backend_(backend),
      upload_thread_(backend),
      current_annotation_id_(0),
      time_provider_(time_provider),
      meminfo_provider_(meminfo_provider),
      ikeys_(settings.aggregation_strategy.max_instrumentation_keys),
      next_ikey_(0),
      loading_start_(TimePoint::min()) {
    ALOGI(
        "TuningFork Settings:\n  method: %d\n  interval: %d\n  n_ikeys: %d\n  "
        "n_annotations: %zu"
        "\n  n_histograms: %zu\n  base_uri: %s\n  api_key: %s\n  fp filename: "
        "%s\n  itimeout: %d"
        "\n  utimeout: %d",
        settings.aggregation_strategy.method,
        settings.aggregation_strategy.intervalms_or_count,
        settings.aggregation_strategy.max_instrumentation_keys,
        settings.aggregation_strategy.annotation_enum_size.size(),
        settings.histograms.size(), settings.base_uri.c_str(),
        settings.api_key.c_str(),
        settings.default_fidelity_parameters_filename.c_str(),
        settings.initial_request_timeout_ms,
        settings.ultimate_request_timeout_ms);

    if (time_provider_ == nullptr) {
        time_provider_ = s_chrono_time_provider.get();
    }

    last_submit_time_ = time_provider_->Now();

    InitHistogramSettings();
    InitAnnotationRadixes();
    InitTrainingModeParams();

    size_t max_num_prongs_ = 0;
    int max_ikeys = settings.aggregation_strategy.max_instrumentation_keys;

    if (annotation_radix_mult_.size() == 0 || max_ikeys == 0)
        ALOGE(
            "Neither max_annotations nor max_instrumentation_keys can be zero");
    else
        max_num_prongs_ = max_ikeys * annotation_radix_mult_.back();
    auto serializeId = [this](uint64_t id) {
        return SerializeAnnotationId(id);
    };
    auto isLoading = [this](uint64_t id) { return IsLoadingAnnotationId(id); };
    prong_caches_[0] = std::make_unique<ProngCache>(
        max_num_prongs_, max_ikeys, settings_.histograms, serializeId,
        isLoading, meminfo_provider);
    prong_caches_[1] = std::make_unique<ProngCache>(
        max_num_prongs_, max_ikeys, settings_.histograms, serializeId,
        isLoading, meminfo_provider);
    current_prong_cache_ = prong_caches_[0].get();
    live_traces_.resize(max_num_prongs_);
    for (auto &t : live_traces_) t = TimePoint::min();
    auto crash_callback = [this]() -> bool {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        TuningFork_ErrorCode ret = this->Flush();
        ALOGI("Crash flush result : %d", ret);
        return true;
    };

    crash_handler_.Init(crash_callback);

    // Check if there are any files waiting to be uploaded
    // + merge any histograms that are persisted.
    upload_thread_.InitialChecks(*current_prong_cache_, *this,
                                 settings_.c_settings.persistent_cache);

    ALOGI("TuningFork initialized");
}

// Return the set annotation id or -1 if it could not be set
uint64_t TuningForkImpl::SetCurrentAnnotation(
    const ProtobufSerialization &annotation) {
    current_annotation_ = annotation;
    bool loading;
    auto id = DecodeAnnotationSerialization(annotation, &loading);
    if (id == annotation_util::kAnnotationError) {
        ALOGW("Error setting annotation of size %zu", annotation.size());
        current_annotation_id_ = 0;
        return annotation_util::kAnnotationError;
    } else {
        // Check if we have started or stopped loading
        if (LoadingNextScene()) {
            if (!loading) {
                // We've stopped loading: record the time
                auto dt = time_provider_->Now() - loading_start_;
                TraceNanos(current_annotation_id_, dt);
                int cnt =
                    (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                        dt)
                        .count();
                bool isLoading = IsLoadingAnnotationId(current_annotation_id_);
                ALOGI("Scene loading %" PRIu64 " (%s) took %d ms",
                      current_annotation_id_, isLoading ? "true" : "false",
                      cnt);
                current_annotation_id_ = id;
                loading_start_ = TimePoint::min();
            }
        } else {
            if (loading) {
                // We've just switched to a loading screen
                loading_start_ = time_provider_->Now();
            }
        }
        ALOGV("Set annotation id to %" PRIu64, id);
        current_annotation_id_ = id;
        return current_annotation_id_;
    }
}

AnnotationId TuningForkImpl::DecodeAnnotationSerialization(
    const SerializedAnnotation &ser, bool *loading) const {
    auto id = annotation_util::DecodeAnnotationSerialization(
        ser, annotation_radix_mult_, settings_.loading_annotation_index,
        settings_.level_annotation_index, loading);
    // Shift over to leave room for the instrument id
    return id * settings_.aggregation_strategy.max_instrumentation_keys;
}

SerializedAnnotation TuningForkImpl::SerializeAnnotationId(AnnotationId id) {
    SerializedAnnotation ann;
    AnnotationId a =
        id / settings_.aggregation_strategy.max_instrumentation_keys;
    annotation_util::SerializeAnnotationId(a, ann, annotation_radix_mult_);
    return ann;
}

bool TuningForkImpl::IsLoadingAnnotationId(AnnotationId id) const {
    if (settings_.loading_annotation_index == -1) return false;
    AnnotationId a =
        id / settings_.aggregation_strategy.max_instrumentation_keys;
    int value;
    if (annotation_util::Value(a, settings_.loading_annotation_index,
                               annotation_radix_mult_,
                               value) == annotation_util::NO_ERROR) {
        return value > 1;
    } else {
        return false;
    }
}

TuningFork_ErrorCode TuningForkImpl::GetFidelityParameters(
    const ProtobufSerialization &default_params,
    ProtobufSerialization &params_ser, uint32_t timeout_ms) {
    std::string experiment_id;
    if (settings_.EndpointUri().empty()) {
        ALOGW("The base URI in Tuning Fork TuningFork_Settings is invalid");
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
    if (settings_.api_key.empty()) {
        ALOGE("The API key in Tuning Fork TuningFork_Settings is invalid");
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
    Duration timeout =
        (timeout_ms <= 0)
            ? std::chrono::milliseconds(settings_.initial_request_timeout_ms)
            : std::chrono::milliseconds(timeout_ms);
    HttpRequest web_request(settings_.EndpointUri(), settings_.api_key,
                            timeout);
    auto result = backend_->GenerateTuningParameters(
        web_request, training_mode_params_.get(), params_ser, experiment_id);
    if (result == TUNINGFORK_ERROR_OK) {
        RequestInfo::CachedValue().current_fidelity_parameters = params_ser;
    } else if (training_mode_params_.get()) {
        RequestInfo::CachedValue().current_fidelity_parameters =
            *training_mode_params_;
    }
    RequestInfo::CachedValue().experiment_id = experiment_id;
    if (Debugging() && jni::IsValid()) {
        backend_->UploadDebugInfo(web_request);
    }
    return result;
}
TuningFork_ErrorCode TuningForkImpl::GetOrCreateInstrumentKeyIndex(
    InstrumentationKey key, int &index) {
    int nkeys = next_ikey_;
    for (int i = 0; i < nkeys; ++i) {
        if (ikeys_[i] == key) {
            index = i;
            return TUNINGFORK_ERROR_OK;
        }
    }
    // Another thread could have incremented next_ikey while we were checking,
    // but we
    //  mandate that different threads not use the same key, so we are OK adding
    //  our key, if we can.
    int next = next_ikey_++;
    if (next < ikeys_.size()) {
        ikeys_[next] = key;
        index = next;
        return TUNINGFORK_ERROR_OK;
    } else {
        next_ikey_--;
    }
    return TUNINGFORK_ERROR_INVALID_INSTRUMENT_KEY;
}
TuningFork_ErrorCode TuningForkImpl::StartTrace(InstrumentationKey key,
                                                TraceHandle &handle) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    auto err = MakeCompoundId(key, current_annotation_id_, handle);
    if (err != TUNINGFORK_ERROR_OK) return err;
    trace_->beginSection("TFTrace");
    live_traces_[handle] = time_provider_->Now();
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::EndTrace(TraceHandle h) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    if (h >= live_traces_.size()) return TUNINGFORK_ERROR_INVALID_TRACE_HANDLE;
    auto i = live_traces_[h];
    if (i != TimePoint::min()) {
        trace_->endSection();
        TraceNanos(h, time_provider_->Now() - i);
        live_traces_[h] = TimePoint::min();
        return TUNINGFORK_ERROR_OK;
    } else {
        return TUNINGFORK_ERROR_INVALID_TRACE_HANDLE;
    }
}

TuningFork_ErrorCode TuningForkImpl::FrameTick(InstrumentationKey key) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    uint64_t compound_id;
    auto err = MakeCompoundId(key, current_annotation_id_, compound_id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    trace_->beginSection("TFTick");
    current_prong_cache_->Ping(time_provider_->SystemNow());
    auto t = time_provider_->Now();
    auto p = TickNanos(compound_id, t);
    if (p) CheckForSubmit(t, p);
    trace_->endSection();
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::FrameDeltaTimeNanos(InstrumentationKey key,
                                                         Duration dt) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    uint64_t compound_id;
    auto err = MakeCompoundId(key, current_annotation_id_, compound_id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    auto p = TraceNanos(compound_id, dt);
    if (p) CheckForSubmit(time_provider_->Now(), p);
    return TUNINGFORK_ERROR_OK;
}

Prong *TuningForkImpl::TickNanos(uint64_t compound_id, TimePoint t) {
    // Find the appropriate histogram and add this time
    Prong *p = current_prong_cache_->Get(compound_id);
    if (p)
        p->Tick(t);
    else
        ALOGW("Bad id or limit of number of prongs reached");
    return p;
}

Prong *TuningForkImpl::TraceNanos(uint64_t compound_id, Duration dt) {
    // Find the appropriate histogram and add this time
    Prong *h = current_prong_cache_->Get(compound_id);
    if (h)
        h->Trace(dt);
    else
        ALOGW("Bad id or limit of number of prongs reached");
    return h;
}

void TuningForkImpl::SetUploadCallback(TuningFork_UploadCallback cbk) {
    upload_thread_.SetUploadCallback(cbk);
}

bool TuningForkImpl::ShouldSubmit(TimePoint t, Prong *prong) {
    auto method = settings_.aggregation_strategy.method;
    auto count = settings_.aggregation_strategy.intervalms_or_count;
    switch (settings_.aggregation_strategy.method) {
        case Settings::AggregationStrategy::Submission::TIME_BASED:
            return (t - last_submit_time_) >= std::chrono::milliseconds(count);
        case Settings::AggregationStrategy::Submission::TICK_BASED:
            if (prong) return prong->Count() >= count;
    }
    return false;
}

TuningFork_ErrorCode TuningForkImpl::CheckForSubmit(TimePoint t, Prong *prong) {
    TuningFork_ErrorCode ret_code = TUNINGFORK_ERROR_OK;
    if (ShouldSubmit(t, prong)) {
        ret_code = Flush(t, true);
    }
    return ret_code;
}

void TuningForkImpl::InitHistogramSettings() {
    auto max_keys = settings_.aggregation_strategy.max_instrumentation_keys;
    if (max_keys != settings_.histograms.size()) {
        InstrumentationKey default_keys[] = {TFTICK_RAW_FRAME_TIME,
                                             TFTICK_PACED_FRAME_TIME,
                                             TFTICK_CPU_TIME, TFTICK_GPU_TIME};
        // Add histograms that are missing
        auto key_present = [this](InstrumentationKey k) {
            for (auto &h : settings_.histograms) {
                if (k == h.instrument_key) return true;
            }
            return false;
        };
        std::vector<InstrumentationKey> to_add;
        for (auto &k : default_keys) {
            if (!key_present(k)) {
                if (settings_.histograms.size() < max_keys) {
                    ALOGI(
                        "Couldn't get histogram for key index %d. Using "
                        "default histogram",
                        k);
                    settings_.histograms.push_back(
                        Settings::DefaultHistogram(k));
                } else {
                    ALOGE(
                        "Can't fit default histograms: change "
                        "max_instrumentation_keys");
                }
            }
        }
    }
    for (uint32_t i = 0; i < max_keys; ++i) {
        if (i > settings_.histograms.size()) {
            ALOGW(
                "Couldn't get histogram for key index %d. Using default "
                "histogram",
                i);
            settings_.histograms.push_back(Settings::DefaultHistogram(-1));
        } else {
            int index;
            GetOrCreateInstrumentKeyIndex(
                settings_.histograms[i].instrument_key, index);
        }
    }
    // If there was an instrument key but no other settings, update the
    // histogram
    auto check_histogram = [](Settings::Histogram &h) {
        if (h.bucket_max == 0 || h.n_buckets == 0) {
            h = Settings::DefaultHistogram(h.instrument_key);
        }
    };
    for (auto &h : settings_.histograms) {
        check_histogram(h);
    }
    ALOGI("Settings::Histograms");
    for (uint32_t i = 0; i < settings_.histograms.size(); ++i) {
        auto &h = settings_.histograms[i];
        ALOGI("ikey: %d min: %f max: %f nbkts: %d", h.instrument_key,
              h.bucket_min, h.bucket_max, h.n_buckets);
    }
}

void TuningForkImpl::InitAnnotationRadixes() {
    annotation_util::SetUpAnnotationRadixes(
        annotation_radix_mult_,
        settings_.aggregation_strategy.annotation_enum_size);
}

TuningFork_ErrorCode TuningForkImpl::Flush() {
    auto t = std::chrono::steady_clock::now();
    return Flush(t, false);
}

void TuningForkImpl::SwapProngCaches() {
    if (current_prong_cache_ == prong_caches_[0].get()) {
        prong_caches_[1]->Clear();
        current_prong_cache_ = prong_caches_[1].get();
    } else {
        prong_caches_[0]->Clear();
        current_prong_cache_ = prong_caches_[0].get();
    }
}
TuningFork_ErrorCode TuningForkImpl::Flush(TimePoint t, bool upload) {
    ALOGV("Flush %d", upload);
    TuningFork_ErrorCode ret_code;
    current_prong_cache_->SetInstrumentKeys(ikeys_);
    if (upload_thread_.Submit(current_prong_cache_, upload)) {
        SwapProngCaches();
        ret_code = TUNINGFORK_ERROR_OK;
    } else {
        ret_code = TUNINGFORK_ERROR_PREVIOUS_UPLOAD_PENDING;
    }
    if (upload) last_submit_time_ = t;
    return ret_code;
}

void TuningForkImpl::InitTrainingModeParams() {
    auto cser = settings_.c_settings.training_fidelity_params;
    if (cser != nullptr)
        training_mode_params_ = std::make_unique<ProtobufSerialization>(
            ToProtobufSerialization(*cser));
}

TuningFork_ErrorCode TuningForkImpl::SetFidelityParameters(
    const ProtobufSerialization &params) {
    auto flush_result = Flush();
    if (flush_result != TUNINGFORK_ERROR_OK) {
        ALOGW("Warning, previous data could not be flushed.");
        SwapProngCaches();
    }
    RequestInfo::CachedValue().current_fidelity_parameters = params;
    // We clear the experiment id here.
    RequestInfo::CachedValue().experiment_id = "";
    return TUNINGFORK_ERROR_OK;
}

bool TuningForkImpl::Debugging() const {
#ifndef NDEBUG
    // Always return true if we are a debug build
    return true;
#else
    // Otherwise, check the APK and system settings
    if (jni::IsValid())
        return apk_utils::GetDebuggable();
    else
        return false;
#endif
}

TuningFork_ErrorCode TuningForkImpl::EnableMemoryRecording(bool enable) {
    if (meminfo_provider_ != nullptr) {
        meminfo_provider_->SetEnabled(enable);
    }
    return TUNINGFORK_ERROR_OK;
}

}  // namespace tuningfork