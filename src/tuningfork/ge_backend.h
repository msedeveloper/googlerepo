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

#pragma once

#include <sstream>
#include <jni.h>
#include <string>

#include "tuningfork_internal.h"

namespace tuningfork {

// Google Endpoint backend
class GEBackend : public Backend {
public:
    TFErrorCode Init(JNIEnv* env, jobject context);
    ~GEBackend() override;
    TFErrorCode Process(const std::string &json_event) override;

private:
    JavaVM* vm_;
    jobject uploader_;
    jmethodID schedule_method_;
};

} //namespace tuningfork {