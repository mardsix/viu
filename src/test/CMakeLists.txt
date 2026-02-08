set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")

cmake_minimum_required(VERSION 4.0)

project(viu)

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_MODULE_STD ON)

set(TEST_EXECUTABLE_NAME ${PROJECT_NAME}-gtest)
add_executable(${TEST_EXECUTABLE_NAME})

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
        message(WARNING "clang-tidy not found, but ENABLE_CLANG_TIDY=ON")
    endif()
else()
    message(STATUS "Clang-tidy disabled")
endif()

option(ENABLE_COVERAGE "Enable coverage instrumentation for unit tests" OFF)

if (ENABLE_COVERAGE)
    message(STATUS "Coverage instrumentation enabled for tests")

    target_compile_options(${TEST_EXECUTABLE_NAME}
        PRIVATE
            -fprofile-arcs
            -ftest-coverage
            -fprofile-instr-generate
            -fcoverage-mapping
    )

    target_link_options(${TEST_EXECUTABLE_NAME}
        PRIVATE
            -fprofile-arcs
            -fprofile-instr-generate
    )
endif()

add_compile_options(-Wall -Wpedantic)
add_compile_options(-Wno-zero-length-array -Wno-c99-extensions)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/9706f75b8f91c52a3840cf5d878a7f37ea10ef00.zip
)

FetchContent_MakeAvailable(googletest)

foreach(tgt
    gtest gtest_main
    gmock gmock_main
)
    if(TARGET ${tgt})
        set_target_properties(${tgt} PROPERTIES CXX_CLANG_TIDY "")
    endif()
endforeach()

target_sources(${TEST_EXECUTABLE_NAME}
    PRIVATE
    FILE_SET CXX_MODULES
    BASE_DIRS
    ${VIU_TOP_SOURCE_DIR}
    FILES
    ${VIU_TOP_SOURCE_DIR}/src/assert.cppm
    ${VIU_TOP_SOURCE_DIR}/src/boost.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/usb_descriptors.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/types.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/structs.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/traits.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/packer.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/tree.cppm
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/descriptor_classes.cppm
    ${VIU_TOP_SOURCE_DIR}/src/format.cppm
    ${VIU_TOP_SOURCE_DIR}/src/io.cppm
    ${VIU_TOP_SOURCE_DIR}/src/json/json.cppm
    ${VIU_TOP_SOURCE_DIR}/src/tickable.cppm
    ${VIU_TOP_SOURCE_DIR}/src/transfer.cppm
    ${VIU_TOP_SOURCE_DIR}/src/types.cppm
    ${VIU_TOP_SOURCE_DIR}/src/usb.cppm
    ${VIU_TOP_SOURCE_DIR}/src/usb_basic.cppm
    ${VIU_TOP_SOURCE_DIR}/src/usb_device_proxy.cppm
    ${VIU_TOP_SOURCE_DIR}/src/usb_mock.cppm
    ${VIU_TOP_SOURCE_DIR}/src/usbip_socket.cppm
    ${VIU_TOP_SOURCE_DIR}/src/vector.cppm
    ${VIU_TOP_SOURCE_DIR}/src/vhci.cppm)

target_sources(${TEST_EXECUTABLE_NAME}
    PRIVATE
    ${VIU_TOP_SOURCE_DIR}/src/assert_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/descriptors/usb_descriptors_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/json/json_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/transfer_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usb_basic_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usb_device_proxy_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usb_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usbip_socket_impl.cpp
    ${VIU_TOP_SOURCE_DIR}/src/vhci_impl.cpp

    ${VIU_TOP_SOURCE_DIR}/src/format_test.cpp
    ${VIU_TOP_SOURCE_DIR}/src/types_test.cpp
    ${VIU_TOP_SOURCE_DIR}/src/vector_test.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usb_descriptors_test.cpp
    ${VIU_TOP_SOURCE_DIR}/src/usb_mock_test.cpp

    main.cpp
)

set(BOOST_ROOT ${CMAKE_SOURCE_DIR}/out/install/boost)
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/lib)
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost 1.88.0 REQUIRED)
find_package(Boost COMPONENTS json REQUIRED)
find_package(Boost COMPONENTS thread REQUIRED)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/out/install/libusb/include/libusb-1.0
)

target_link_libraries(
    ${TEST_EXECUTABLE_NAME}
    Boost::json
    Boost::thread
    GTest::gtest_main
    GTest::gmock_main
    udev
    systemd
    ${CMAKE_SOURCE_DIR}/out/install/libusb/lib/libusb-1.0.a
)

include(GoogleTest)
gtest_discover_tests(${TEST_EXECUTABLE_NAME})

#enable_sanitizers(${TEST_EXECUTABLE_NAME} tsan)
