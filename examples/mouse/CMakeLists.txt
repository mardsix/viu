set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")

cmake_minimum_required(VERSION 4.0)

project(mm)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_MODULE_STD ON)

set(
    CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -gdwarf-4"
)

set(EXECUTABLE_NAME ${PROJECT_NAME})
add_library(${EXECUTABLE_NAME} SHARED)

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

add_compile_options(-Wall -Wpedantic)

target_sources(${EXECUTABLE_NAME}
    PRIVATE
    main.cpp
)

set(BOOST_ROOT ${CMAKE_SOURCE_DIR}/../../out/install/boost)
find_package(Boost REQUIRED COMPONENTS thread)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../../out/install/viu")
find_package(viu REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/../../out/install/libusb/include/libusb-1.0
)

target_link_libraries(
    ${EXECUTABLE_NAME}
    PRIVATE viu::viu
)