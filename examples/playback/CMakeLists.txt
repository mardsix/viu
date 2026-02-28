set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")

cmake_minimum_required(VERSION 4.0)

project(viumock)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_MODULE_STD ON)

set(
    CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -gdwarf-4"
)

set(PROXY_LIB_NAME ${PROJECT_NAME}-record)
set(PLAYBACK_LIB_NAME ${PROJECT_NAME}-playback)

add_library(${PROXY_LIB_NAME} SHARED)
add_library(${PLAYBACK_LIB_NAME} SHARED)

option(ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(
            CMAKE_CXX_CLANG_TIDY
            ${CLANG_TIDY_EXE};
            -allow-no-checks;
            -fix;
            -system-headers=false;
        )
    else()
        message(WARNING "clang-tidy not found")
    endif()
else()
    message(STATUS "Clang-tidy disabled")
endif()

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE}
            -i
            -style=file
            ${PROJECT_SOURCE_DIR}/*.cpp
        COMMENT "Formatting source files"
    )
endif()

add_compile_options(-Wall -Wpedantic)

target_sources(${PROXY_LIB_NAME}
    PRIVATE
    recording_proxy.cpp
)

target_sources(${PLAYBACK_LIB_NAME}
    PRIVATE
    playback_device.cpp
)

include_directories(
    "${CMAKE_SOURCE_DIR}/../../out/install/libusb/include/libusb-1.0"
    "${CMAKE_SOURCE_DIR}/../../out/install/viu/include"
)
