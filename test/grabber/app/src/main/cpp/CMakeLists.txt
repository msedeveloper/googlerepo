# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)
project(grabber)

# Build the grabber native library.
add_library(${PROJECT_NAME} SHARED
  grabber.cpp)

target_link_libraries(${PROJECT_NAME} log)
