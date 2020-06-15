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

#include "histogram.h"
#include "jni/jni_helper.h"
#include "tuningfork_internal.h"
#include "tuningfork_utils.h"

#define LOG_TAG "TuningFork"
#include <android/asset_manager_jni.h>

#include <algorithm>
#include <sstream>

#include "Log.h"
#include "file_cache.h"
#include "nano/descriptor.pb.h"
#include "nano/tuningfork.pb.h"
#include "pb_decode.h"
#include "proto/protobuf_nano_util.h"
using PBSettings = com_google_tuningfork_Settings;

namespace tuningfork {

static FileCache sFileCache;

constexpr char kPerformanceParametersBaseUri[] =
    "https://performanceparameters.googleapis.com/v1/";

// Forward declaration
static bool GetEnumSizesFromDescriptors(std::vector<uint32_t>& enum_sizes);

// Use the default persister if the one passed in is null
static void CheckPersister(const TuningFork_Cache*& persister,
                           std::string save_dir) {
    if (persister == nullptr) {
        if (save_dir.empty()) {
            // If the save_dir is empty, try the app's cache dir or tmp storage.
            if (jni::IsValid()) {
                save_dir = file_utils::GetAppCacheDir();
            }
            if (save_dir.empty()) save_dir = "/data/local/tmp";
            save_dir += "/tuningfork";
        }
        ALOGI("Using local file cache at %s", save_dir.c_str());
        sFileCache.SetDir(save_dir);
        persister = sFileCache.GetCCache();
    }
}

void Settings::Check(const std::string& save_dir) {
    CheckPersister(c_settings.persistent_cache, save_dir);
    if (base_uri.empty()) base_uri = kPerformanceParametersBaseUri;
    if (base_uri.back() != '/') base_uri += '/';
    if (aggregation_strategy.intervalms_or_count == 0) {
        aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TIME_BASED;
#ifndef NDEBUG
        // For debug builds, upload every 10 seconds
        aggregation_strategy.intervalms_or_count = 10000;
#else
        // For non-debug builds, upload every 2 hours
        aggregation_strategy.intervalms_or_count = 7200000;
#endif
    }
    if (initial_request_timeout_ms == 0) initial_request_timeout_ms = 1000;
    if (ultimate_request_timeout_ms == 0) ultimate_request_timeout_ms = 100000;
}

static bool decodeAnnotationEnumSizes(pb_istream_t* stream,
                                      const pb_field_t* field, void** arg) {
    Settings* settings = static_cast<Settings*>(*arg);
    uint64_t a;
    if (pb_decode_varint(stream, &a)) {
        settings->aggregation_strategy.annotation_enum_size.push_back(
            (uint32_t)a);
        return true;
    } else {
        return false;
    }
}
static bool decodeHistograms(pb_istream_t* stream, const pb_field_t* field,
                             void** arg) {
    Settings* settings = static_cast<Settings*>(*arg);
    com_google_tuningfork_Settings_Histogram hist;
    if (pb_decode(stream, com_google_tuningfork_Settings_Histogram_fields,
                  &hist)) {
        settings->histograms.push_back({hist.instrument_key, hist.bucket_min,
                                        hist.bucket_max, hist.n_buckets});
        return true;
    } else {
        return false;
    }
}
static bool decode_string(pb_istream_t* stream, const pb_field_t* field,
                          void** arg) {
    std::string* out_str = static_cast<std::string*>(*arg);
    out_str->resize(stream->bytes_left);
    char* d = const_cast<char*>(
        out_str->data());  // C++17 would remove the need for cast
    while (stream->bytes_left) {
        uint64_t x;
        if (!pb_decode_varint(stream, &x)) return false;
        *d++ = (char)x;
    }
    return true;
}

static TuningFork_ErrorCode DeserializeSettings(
    const ProtobufSerialization& settings_ser, Settings* settings) {
    PBSettings pbsettings = com_google_tuningfork_Settings_init_zero;
    pbsettings.aggregation_strategy.annotation_enum_size.funcs.decode =
        decodeAnnotationEnumSizes;
    pbsettings.aggregation_strategy.annotation_enum_size.arg = settings;
    pbsettings.histograms.funcs.decode = decodeHistograms;
    pbsettings.histograms.arg = settings;
    pbsettings.base_uri.funcs.decode = decode_string;
    pbsettings.base_uri.arg = (void*)&settings->base_uri;
    pbsettings.api_key.funcs.decode = decode_string;
    pbsettings.api_key.arg = (void*)&settings->api_key;
    pbsettings.default_fidelity_parameters_filename.funcs.decode =
        decode_string;
    pbsettings.default_fidelity_parameters_filename.arg =
        (void*)&settings->default_fidelity_parameters_filename;
    ByteStream str{const_cast<uint8_t*>(settings_ser.data()),
                   settings_ser.size(), 0};
    pb_istream_t stream = {ByteStream::Read, &str, settings_ser.size()};
    if (!pb_decode(&stream, com_google_tuningfork_Settings_fields, &pbsettings))
        return TUNINGFORK_ERROR_BAD_SETTINGS;
    if (pbsettings.aggregation_strategy.method ==
        com_google_tuningfork_Settings_AggregationStrategy_Submission_TICK_BASED)
        settings->aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TICK_BASED;
    else
        settings->aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TIME_BASED;
    settings->aggregation_strategy.intervalms_or_count =
        pbsettings.aggregation_strategy.intervalms_or_count;
    settings->aggregation_strategy.max_instrumentation_keys =
        pbsettings.aggregation_strategy.max_instrumentation_keys;
    settings->initial_request_timeout_ms =
        pbsettings.initial_request_timeout_ms;
    settings->ultimate_request_timeout_ms =
        pbsettings.ultimate_request_timeout_ms;
    // Convert from 1-based to 0 based indices (-1 = not present)
    settings->loading_annotation_index =
        pbsettings.loading_annotation_index - 1;
    settings->level_annotation_index = pbsettings.level_annotation_index - 1;
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode Settings::FindInApk(Settings* settings) {
    if (settings) {
        ProtobufSerialization settings_ser;
        if (apk_utils::GetAssetAsSerialization(
                "tuningfork/tuningfork_settings.bin", settings_ser)) {
            ALOGI("Got settings from tuningfork/tuningfork_settings.bin");
            TuningFork_ErrorCode err =
                DeserializeSettings(settings_ser, settings);
            if (err != TUNINGFORK_ERROR_OK) return err;
            if (settings->aggregation_strategy.annotation_enum_size.size() ==
                0) {
                // If enum sizes are missing, use the descriptor in
                // dev_tuningfork.descriptor
                if (!GetEnumSizesFromDescriptors(
                        settings->aggregation_strategy.annotation_enum_size)) {
                    return TUNINGFORK_ERROR_NO_SETTINGS_ANNOTATION_ENUM_SIZES;
                }
            }
            return TUNINGFORK_ERROR_OK;
        } else {
            return TUNINGFORK_ERROR_NO_SETTINGS;
        }
    } else {
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
}

// Default histogram, used e.g. for TF Scaled or when a histogram is missing in
// the settings.
/*static*/ Settings::Histogram Settings::DefaultHistogram(
    InstrumentationKey ikey) {
    Settings::Histogram default_histogram;
    if (ikey == TFTICK_RAW_FRAME_TIME || ikey == TFTICK_PACED_FRAME_TIME) {
        default_histogram.bucket_min = 6.54f;
        default_histogram.bucket_max = 60.0f;
        default_histogram.n_buckets = HistogramBase::kDefaultNumBuckets;
    } else {
        default_histogram.bucket_min = 0.0f;
        default_histogram.bucket_max = 20.0f;
        default_histogram.n_buckets = HistogramBase::kDefaultNumBuckets;
    }
    if (ikey >= TFTICK_RAW_FRAME_TIME) {
        // Fill in the well-known keys
        default_histogram.instrument_key = ikey;
    } else {
        default_histogram.instrument_key = -1;
    }
    return default_histogram;
}

// Structures and functions needed for the decoding of a
// dev_tuningfork.descriptor. Used to calculate the annotation_enum_size vector
// from the proto definition.
namespace {

struct EnumField {
    int number;
    std::string type_name;
};

struct MessageType {
    std::string name;
    std::vector<EnumField> fields;
};

struct EnumType {
    std::string name;
    std::vector<std::pair<std::string, int>> value;
};

struct File {
    std::string package;
    std::vector<MessageType> message_type;
    std::vector<EnumType> enum_type;
};

static bool DecodeField(pb_istream_t* stream, const pb_field_t* field,
                        void** arg) {
    MessageType* message = static_cast<MessageType*>(*arg);
    google_protobuf_FieldDescriptorProto f =
        google_protobuf_FieldDescriptorProto_init_default;
    EnumField ef;
    f.type_name.funcs.decode = decode_string;
    f.type_name.arg = &ef.type_name;
    if (!pb_decode(stream, google_protobuf_FieldDescriptorProto_fields, &f))
        return false;
    if (f.type == google_protobuf_FieldDescriptorProto_Type_TYPE_ENUM) {
        ef.number = f.number;
        message->fields.push_back(ef);
    }
    return true;
}

static bool DecodeMessageType(pb_istream_t* stream, const pb_field_t* field,
                              void** arg) {
    File* d = static_cast<File*>(*arg);
    google_protobuf_DescriptorProto desc =
        google_protobuf_DescriptorProto_init_default;
    MessageType t;
    desc.name.funcs.decode = decode_string;
    desc.name.arg = &t.name;
    desc.field.funcs.decode = DecodeField;
    desc.field.arg = &t;
    if (!pb_decode(stream, google_protobuf_DescriptorProto_fields, &desc))
        return false;
    d->message_type.push_back(t);
    return true;
}

static bool DecodeValue(pb_istream_t* stream, const pb_field_t* field,
                        void** arg) {
    EnumType* e = static_cast<EnumType*>(*arg);
    google_protobuf_EnumValueDescriptorProto desc =
        google_protobuf_EnumValueDescriptorProto_init_default;
    std::string name;
    desc.name.funcs.decode = decode_string;
    desc.name.arg = &name;
    if (!pb_decode(stream, google_protobuf_EnumValueDescriptorProto_fields,
                   &desc))
        return false;
    if (desc.has_number) e->value.push_back({name, desc.number});
    return true;
}

static bool DecodeEnumType(pb_istream_t* stream, const pb_field_t* field,
                           void** arg) {
    File* d = static_cast<File*>(*arg);
    google_protobuf_EnumDescriptorProto desc =
        google_protobuf_EnumDescriptorProto_init_default;
    EnumType e;
    desc.name.funcs.decode = decode_string;
    desc.name.arg = &e.name;
    desc.value.funcs.decode = DecodeValue;
    desc.value.arg = &e;
    if (!pb_decode(stream, google_protobuf_EnumDescriptorProto_fields, &desc))
        return false;
    d->enum_type.push_back(e);
    return true;
}

static bool DecodeFile(pb_istream_t* stream, const pb_field_t* field,
                       void** arg) {
    File* d = static_cast<File*>(*arg);
    google_protobuf_FileDescriptorProto fdesc =
        google_protobuf_FileDescriptorProto_init_default;
    fdesc.message_type.funcs.decode = DecodeMessageType;
    fdesc.message_type.arg = d;
    fdesc.enum_type.funcs.decode = DecodeEnumType;
    fdesc.enum_type.arg = d;
    fdesc.package.funcs.decode = decode_string;
    fdesc.package.arg = &d->package;
    return pb_decode(stream, google_protobuf_FileDescriptorProto_fields,
                     &fdesc);
}

}  // anonymous namespace

// Parse the dev_tuningfork.descriptor file in order to find enum sizes.
// Returns true is successful, false if not.
static bool GetEnumSizesFromDescriptors(std::vector<uint32_t>& enum_sizes) {
    ProtobufSerialization descriptor_ser;
    if (!apk_utils::GetAssetAsSerialization(
            "tuningfork/dev_tuningfork.descriptor", descriptor_ser))
        return false;
    ByteStream str{const_cast<uint8_t*>(descriptor_ser.data()),
                   descriptor_ser.size(), 0};
    pb_istream_t stream = {ByteStream::Read, &str, descriptor_ser.size()};
    google_protobuf_FileDescriptorSet descriptor =
        google_protobuf_FileDescriptorSet_init_default;
    File f;
    descriptor.file.funcs.decode = DecodeFile;
    descriptor.file.arg = &f;
    if (!pb_decode(&stream, google_protobuf_FileDescriptorSet_fields,
                   &descriptor))
        return false;
    ALOGV("Searching for Annotation in TuningFork descriptor");
    for (auto& m : f.message_type) {
        if (m.name == "Annotation") {
            std::sort(m.fields.begin(), m.fields.end(),
                      [](const EnumField& a, const EnumField& b) {
                          return a.number < b.number;
                      });
            for (auto const& e : m.fields) {
                std::string n = e.type_name;
                // Strip off this package name
                std::string this_package = "." + f.package;
                if (n.find_first_of(this_package) == 0)
                    n = n.substr(this_package.size() + 1);
                for (auto const& enums_in_desc : f.enum_type) {
                    ALOGV("Looking for: %s, enum name: %s", n.c_str(),
                          enums_in_desc.name.c_str());
                    if (enums_in_desc.name == n) {
                        int max_value = 0;
                        for (auto const& v : enums_in_desc.value) {
                            if (v.second > max_value) max_value = v.second;
                        }
                        enum_sizes.push_back(max_value + 1);
                    }
                }
            }
        }
    }
    if (enum_sizes.size() == 0) return false;
    std::stringstream ostr;
    ostr << '[';
    for (auto const& e : enum_sizes) {
        ostr << e << ',';
    }
    ostr << ']';
    ALOGI("Found annotation enum sizes in descriptor: %s", ostr.str().c_str());
    return true;
}

}  // namespace tuningfork