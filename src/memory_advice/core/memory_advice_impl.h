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

#pragma once

#include <memory>
#include <mutex>

#include "device_profiler.h"
#include "metrics_provider.h"

namespace memory_advice {

using namespace json11;

class MemoryAdviceImpl {
   private:
    std::unique_ptr<MetricsProvider> metrics_provider_;
    std::unique_ptr<DeviceProfiler> device_profiler_;
    Json::object advisor_parameters_;
    Json::object baseline_;
    Json::object device_profile_;
    std::mutex advice_mutex_;

    MemoryAdvice_ErrorCode initialization_error_code_ = MEMORYADVICE_ERROR_OK;

    MemoryAdvice_ErrorCode ProcessAdvisorParameters(const char* parameters);
    /** @brief Given a list of fields, extracts metrics by calling the matching
     * metrics functions and gathers them in a single Json object. */
    Json::object GenerateMetricsFromFields(Json::object fields);
    /**
     * Calls the provided metrics function, and extracts a subset of the metrics
     * using the fields parameter. fields can either be a single boolean
     * evaluating to true, which implies all the metrics provided by the metrics
     * function should be extracted, or fields can be a Json object whose keys
     * are metrics that need to be extracted from the metrics provided by the
     * metrics function.
     *
     * @param metrics_function The metrics function to call, which provides a
     * json object of metrics.
     * @param fields the list of fields to extract
     * @return A subset of the Json object returned by the metrics function. The
     * returned object also includes how long it took to gather the metrics.
     */
    Json::object ExtractValues(
        MetricsProvider::MetricsFunction metrics_function, Json fields);
    double MillisecondsSinceEpoch();
    /** @brief Find a value in a JSON object, even when it is nested in
     * sub-dictionaries in the object. */
    Json GetValue(Json::object object, std::string key);

   public:
    MemoryAdviceImpl(const char* params);
    /** @brief Creates an advice object by reading variable metrics and
     * comparing them to baseline values and values provided by device profiler.
     */
    Json::object GetAdvice();
    /** @brief Evaluates information from the current metrics and returns a
     * memory state.
     */
    MemoryAdvice_MemoryState GetMemoryState();
    /** @brief Evaluates information from the current metrics and returns an
     * estimate for how much more memory is available, in bytes.
     */
    int64_t GetAvailableMemory();
    /** @brief Reads the variable part of the advisor_parameters_ and reports
     * metrics for those fields. */
    Json::object GenerateVariableMetrics();
    /** @brief Reads the constant part of the advisor_parameters_ and reports
     * metrics for those fields. */
    Json::object GenerateConstantMetrics();

    MemoryAdvice_ErrorCode InitializationErrorCode() const {
        return initialization_error_code_;
    }
};

}  // namespace memory_advice