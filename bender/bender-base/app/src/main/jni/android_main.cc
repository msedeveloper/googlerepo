// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "timing.h"
#include "vulkan_main.h"
#include "user_interface.h"

#include <android/log.h>
#include <android_native_app_glue.h>

// Process the next main command.
void handle_cmd(android_app *app, int32_t cmd) {
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      StartVulkan(app);
      break;
    case APP_CMD_TERM_WINDOW:
      DestroyWindow();
      break;
    case APP_CMD_DESTROY:
      DeleteVulkan();
      break;
    default:
      __android_log_print(ANDROID_LOG_INFO, "Bender",
                          "event not handled: %d", cmd);
  }
}

void android_main(struct android_app *app) {
  // Set the callback to process system events
  app->onAppCmd = handle_cmd;
  app->onInputEvent = UserInterface::Handler;
  app->userData = (void *) new input::Data;

  // Used to poll the events in the main loop
  int events;
  android_poll_source *source;

  // Main loop
  do {
    timing::timer.Time("Main Loop", timing::EventType::MAIN_LOOP, [&events, &source, app](){
      if (ALooper_pollAll(IsVulkanReady() ? 1 : 0, nullptr,
                          &events, (void **) &source) >= 0) {
        if (source != NULL) source->process(app, source);
      }

      // render if vulkan is ready
      if (IsVulkanReady()) {
        VulkanDrawFrame((input::Data *)app->userData);
      }
    });
  } while (app->destroyRequested == 0);
}
