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

#include "Suite.hpp"

#include <map>
#include <memory>


namespace ancer::internal {
    inline std::mutex _operations_lock;
    inline std::map<int, std::unique_ptr<BaseOperation>> _operations;
}

template <typename Func>
void ancer::internal::ForEachOperation(Func&& func) {
    std::lock_guard<std::mutex> lock(_operations_lock);
    for ( auto& op : _operations ) {
        if ( !op.second->IsStopped()) {
            func(*op.second);
        }
    }
}