cmake_minimum_required(VERSION 3.16)

set(PROJECT webrtc_aec3)
set(CMAKE_CXX_STANDARD 20)

project(${PROJECT} VERSION 0.1.0)

add_compile_options(-Wall -fPIC -Wno-deprecated -m64 -fexceptions)

if(APPLE)
    add_definitions(-DWEBRTC_MAC -DWEBRTC_POSIX)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DWEBRTC_LINUX -DWEBRTC_POSIX)
elseif(WIN32)
    add_definitions(-DWEBRTC_WIN)
endif()

# Collect all source files
file(GLOB_RECURSE AEC3_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.h
)

# Exclude AVX2/SSE2 files on non-x86 platforms
if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64"))
    list(FILTER AEC3_SRC EXCLUDE REGEX ".*_avx2\\.cc$")
    list(FILTER AEC3_SRC EXCLUDE REGEX ".*_sse2\\.cc$")
endif()

add_library(${PROJECT} SHARED ${AEC3_SRC})

target_include_directories(${PROJECT} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base/rtc_base
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base/system_wrappers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing
    ${CMAKE_CURRENT_SOURCE_DIR}/src/absl
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base/jsoncpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing/aec3/
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing/resampler
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_processing/utility
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rtc_base/experiments
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common_audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common_audio/third_party/ooura/fft_size_128
)

set_target_properties(${PROJECT} PROPERTIES LINKER_LANGUAGE CXX)