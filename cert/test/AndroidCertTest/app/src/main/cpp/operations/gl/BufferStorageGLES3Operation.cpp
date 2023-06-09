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

/**
 * Buffer Storage is an extension to OpenGL ES 3.1 specification. The original buffer data stores
 * are mutable. This means that, once allocated and already in use, they could be de-allocated or
 * resized. This extension enables the possibility that a buffer can't be de-allocated or resized.
 * However, like any other OpenGL ES extension, availability ain't guaranteed (the device is running
 * on an older version, etc.)
 * This functional test checks, both, that the feature is available and that when applied, it works
 * as expected.
 *
 * Input configuration: none.
 *
 * Output report: a buffer_storage object containing two data points:
 * - available: a boolean telling whether Buffer Storage is available and usable on the device.
 * - status: a scoring integer ranging from:
 *     - 0: available.
 *     - 1: not found in OpenGL ES extension list.
 *     - 2: not found in OpenGL ES shared library.
 *     - 3: the test failed when trying to allocate a mutable store.
 *     - 4: the test failed when trying to de-allocate the mutable store.
 *     - 5: the test failed when trying to allocate an immutable store.
 *     - 6: the test failed because it succeeded de-allocating the immutable
 *          store.
 */

#include <ancer/BaseGLES3Operation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/util/Json.hpp>

#include "./buffer_storage/GLESBufferObject.inl"

using namespace ancer;

//==================================================================================================

namespace {
constexpr Log::Tag TAG{"BufferStorageGLES3Operation"};
}

//==================================================================================================

namespace {

enum BufferStorageStatus : int;

struct buffer_storage_info {
  bool available;
  BufferStorageStatus status;
};

void WriteDatum(report_writers::Struct w, const buffer_storage_info &i) {
  ADD_DATUM_MEMBER(w, i, available);
  ADD_DATUM_MEMBER(w, i, status);
}

struct datum {
  buffer_storage_info buffer_storage;
};

void WriteDatum(report_writers::Struct w, const datum &d) {
  ADD_DATUM_MEMBER(w, d, buffer_storage);
}

/**
 * This enum models the success / failure statuses that this test reports.
 * AVAILABLE means "PASSED". All the others indicate the test step in which the failure occurred.
 */
enum BufferStorageStatus : int {
  /**
   * Feature is available and works (i.e., PASS)
   */
      Available,

  /**
   * Feature isn't available
   * It's missing in the OpenGL ES extensions list
   */
      MissingInExtensionsList,

  /**
   * Feature isn't available
   * It's missing in the OpenGL ES shared library
   */
      MissingInLibrary,

  /**
   * Feature is available but doesn't work
   * Test unexpectedly failed on a mutable store allocation
   */
      FailureAllocatingMutableStore,

  /**
   * Feature is available but doesn't work
   * Test unexpectedly failed on a mutable store deallocation / reallocation
   */
      FailureDeallocatingMutableStore,

  /**
   * Feature is available but doesn't work
   * Test unexpectedly failed on an immutable store allocation
   */
      FailureAllocatingImmutableStore,

  /**
   * Feature is available but doesn't work
   * Test unexpectedly succeeded on an immutable store deallocation / reallocation
   */
      UnexpectedSuccessDeallocatingImmutableStore
};
}

//==================================================================================================

/**
 * OpenGL ES extension Buffer Storage test. This operation first creates a buffer object. Then it
 * allocates mutable and immutable GPU storage to it in order to verify that buffer storage
 * allocation and deallocation work as the OpenGL ES 3.x specification mandates.
 */
class BufferStorageGLES3Operation : public BaseGLES3Operation {
 public:

  void OnGlContextReady(const GLContextConfig &ctx_config) override {
    TestBufferAllocation();
  }

 private:

  /**
   * Allocates a buffer and sequentially tries to allocate, deallocate different kind of storages
   * for it. Goal is to verify that once an immutable store got allocated, it can't be deallocated
   * until the entire buffer object is destroyed.
   */
  void TestBufferAllocation() {
    auto gl_buffer_object = GLESBufferObject<GL_ARRAY_BUFFER, sizeof(float_t)*10>();

    // 1st stop: is the feature listed in OpenGL ES extensions?
    if (not gl_buffer_object.IsBufferStorageEXT_InGLESExtensionList()) {
      Report(datum{{false, MissingInExtensionsList}});
      return;
    }

    // 2nd stop: is the storage function available in the driver library?
    if (not gl_buffer_object.IsBufferStorageEXT_InGLESSharedLibrary()) {
      Report(datum{{false, MissingInLibrary}});
      return;
    }

    // 3rd stop: it should be possible to allocate a mutable store for the buffer
    if (not gl_buffer_object.AllocateMutableStorage()) {
      Report(datum{{false, FailureAllocatingMutableStore}});
      return;
    }

    // 4th stop: it should be possible to deallocate/reallocate another mutable store for the buffer
    if (not gl_buffer_object.AllocateMutableStorage()) {
      Report(datum{{false, FailureDeallocatingMutableStore}});
      return;
    }

    // Up to here, all fine as we were allocating / deallocating mutable buffer storage.
    // As long as mutable, we can do that multiple times.
    // Now follows to confirm that immutable storage can be allocated only once per buffer.

    // 5th stop: it should be possible to deallocate the second mutable store, allocate an immutable
    // one.
    if (not gl_buffer_object.AllocateImmutableStorage()) {
      Report(datum{{false, FailureAllocatingImmutableStore}});
      return;
    }

    // 6th stop: it shouldn't be possible to deallocate the immutable store, allocating a brand new
    // mutable one.
    if (gl_buffer_object.AllocateMutableStorage()) {
      Report(datum{{false, UnexpectedSuccessDeallocatingImmutableStore}});
      return;
    }

    // 7th and last stop: it shouldn't be possible to deallocate the immutable store, allocating a
    // brand new immutable one.
    if (gl_buffer_object.AllocateImmutableStorage()) {
      Report(datum{{false, UnexpectedSuccessDeallocatingImmutableStore}});
      return;
    }

    Report(datum{{true, Available}});
  }
};

EXPORT_ANCER_OPERATION(BufferStorageGLES3Operation)
