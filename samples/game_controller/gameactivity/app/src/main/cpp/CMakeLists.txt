#
# Copyright 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.4.1)
project(paddleboat_demo)

find_package(png16 REQUIRED CONFIG)
find_package(game-activity REQUIRED CONFIG)
find_package(games-controller REQUIRED CONFIG)

# Export GameActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u GameActivity_onCreate")

# Set common compiler options
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Werror -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -Wextra")
add_definitions("-DIMGUI_IMPL_OPENGL_ES2")

# source files common to both the GameActivity and NativeActivity versions
set(COMMON_SRC_DIR "../../../../../common")

# common include directory for all samples
set(COMMON_INCLUDE_DIR "../../../../../../common/include")

# build Dear ImGui as a static lib
set(IMGUI_BASE_DIR "../../../../../third-party/imgui")

add_library(imgui STATIC
        ${IMGUI_BASE_DIR}/imgui_demo.cpp
        ${IMGUI_BASE_DIR}/imgui_draw.cpp
        ${IMGUI_BASE_DIR}/imgui_tables.cpp
        ${IMGUI_BASE_DIR}/imgui_widgets.cpp
        ${IMGUI_BASE_DIR}/imgui.cpp
        ${IMGUI_BASE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(imgui PRIVATE
        ${IMGUI_BASE_DIR})

# now build app's shared lib
add_library(${CMAKE_PROJECT_NAME} SHARED
        ${COMMON_SRC_DIR}/android_main.cpp
        ${COMMON_SRC_DIR}/controllerui_data.cpp
        ${COMMON_SRC_DIR}/controllerui_util.cpp
        ${COMMON_SRC_DIR}/demo_scene.cpp
        ${COMMON_SRC_DIR}/imgui_manager.cpp
        ${COMMON_SRC_DIR}/input_util.cpp
        jni_util.cpp
        native_engine.cpp
        ${COMMON_SRC_DIR}/scene.cpp
        ${COMMON_SRC_DIR}/scene_manager.cpp
        ${COMMON_SRC_DIR}/texture_asset_loader.cpp
        ${COMMON_SRC_DIR}/util.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${COMMON_SRC_DIR}
        ${COMMON_INCLUDE_DIR}
        ${IMGUI_BASE_DIR})

target_compile_options(${CMAKE_PROJECT_NAME}
        PRIVATE
        -std=c++17
        -Wall
        -Wextra-semi
        -Wshadow
        -Wshadow-field
        -O0
        "$<$<CONFIG:DEBUG>:-Werror>")

# add lib dependencies
target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        imgui
        game-activity::game-activity_static
        games-controller::paddleboat_static
        png16::png16-static
        atomic
        EGL
        GLESv2
        jnigraphics
        log
        z)
