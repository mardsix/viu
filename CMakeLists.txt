set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")

cmake_minimum_required(VERSION 4.0)
project(viu)

set(VIU_TOP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_MODULE_STD ON)

set(
    CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -gdwarf-4 -DVIU_ASSERT_ABORT"
)

set(DAEMON_NAME ${PROJECT_NAME}d)
set(LIB_NAME ${PROJECT_NAME})

set(APP_MAJOR_VERSION 0)
set(APP_MINOR_VERSION 2)
set(APP_PATCH_VERSION 0)
set(APP_PRE_RELEASE_VERSION "alpha")

set(LIB_MAJOR_VERSION 0)
set(LIB_MINOR_VERSION 2)
set(LIB_PATCH_VERSION 0)
set(LIB_PRE_RELEASE_VERSION "alpha")

find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_SHA "unknown")
endif()

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT GIT_STATUS STREQUAL "")
        set(GIT_DIRTY ".dirty")
    endif()
endif()

set(GIT_COMMIT ${GIT_SHA}${GIT_DIRTY})

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.cppm.in
    ${CMAKE_BINARY_DIR}/generated/version.cppm
    @ONLY
)

set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/.git/HEAD
    ${CMAKE_SOURCE_DIR}/.git/index
)

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
            ${PROJECT_SOURCE_DIR}/src/*.cpp
            ${PROJECT_SOURCE_DIR}/src/*.cppm
            ${PROJECT_SOURCE_DIR}/src/*/*.cpp
            ${PROJECT_SOURCE_DIR}/src/*/*.cppm
        COMMENT "Formatting source files"
    )
endif()

function(enable_sanitizers target type)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Sanitizers require Clang")
    endif()

    set(flags -fno-omit-frame-pointer)

    if (type STREQUAL "asan")
        list(APPEND flags -fsanitize=address)
    elseif (type STREQUAL "msan")
        list(APPEND flags -fsanitize=memory -fsanitize-memory-track-origins=2)
    elseif (type STREQUAL "tsan")
        list(APPEND flags -fsanitize=thread)
    elseif (type STREQUAL "ubsan")
        list(APPEND flags -fsanitize=undefined)
    else()
        message(FATAL_ERROR "Invalid sanitizer: ${type}")
    endif()

    target_compile_options(${target} PRIVATE ${flags})
    target_link_options(${target} PRIVATE ${flags})
endfunction()

add_compile_options(-Wall -Wpedantic)
add_compile_options(-Wno-zero-length-array -Wno-c99-extensions)
add_compile_options(-O3 -DBOOST_STACKTRACE_USE_ADDR2LINE)

add_executable(${DAEMON_NAME})
add_library(${LIB_NAME} SHARED)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_23)

set(VIU_MODULES
    src/assert.cppm
    src/boost.cppm
    src/descriptors/usb_descriptors.cppm
    src/descriptors/types.cppm
    src/descriptors/structs.cppm
    src/descriptors/traits.cppm
    src/descriptors/packer.cppm
    src/descriptors/tree.cppm
    src/descriptors/descriptor_classes.cppm
    src/error.cppm
    src/format.cppm
    src/json/json.cppm
    src/plugin/interfaces.cppm
    src/plugin/catalog.cppm
    src/plugin/catalog_loader.cppm
    src/io.cppm
    src/tickable.cppm
    src/transfer.cppm
    src/types.cppm
    src/usb.cppm
    src/usb_basic.cppm
    src/usb_device_proxy.cppm
    src/usb_mock.cppm
    src/usbip_socket.cppm
    src/vector.cppm
    src/vhci.cppm

    ${CMAKE_BINARY_DIR}/generated/version.cppm
)

target_sources(${LIB_NAME}
    PRIVATE
    src/assert_impl.cpp
    src/descriptors/usb_descriptors_impl.cpp
    src/json/json_impl.cpp
    src/transfer_impl.cpp
    src/usb_basic_impl.cpp
    src/usb_device_proxy_impl.cpp
    src/usb_impl.cpp
    src/usbip_socket_impl.cpp
    src/vhci_impl.cpp

    PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${PROJECT_SOURCE_DIR}
    FILES
    ${VIU_MODULES}
)

target_sources(${DAEMON_NAME} PRIVATE
    main.cpp
    src/cli/cli_impl.cpp
    src/client/client_impl.cpp
    src/daemon/daemon_impl.cpp

    PRIVATE
    FILE_SET CXX_MODULES
    BASE_DIRS ${PROJECT_SOURCE_DIR}
    FILES
    src/cli/cli.cppm
    src/client/client.cppm
    src/daemon/daemon.cppm
)

target_include_directories(${DAEMON_NAME}
    PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)

add_dependencies(${DAEMON_NAME} ${LIB_NAME})

include(GNUInstallDirs)

target_include_directories(${LIB_NAME}
    PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

install(FILES ${VIU_MODULES}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS ${LIB_NAME}
    EXPORT ${LIB_NAME}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ${LIB_NAME}Targets
    FILE ${LIB_NAME}Targets.cmake
    NAMESPACE ${LIB_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LIB_NAME}
)

include(CMakePackageConfigHelpers)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LIB_NAME}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LIB_NAME}
)

if(COV)
    target_compile_options(${DAEMON_NAME}
        PRIVATE
        -fprofile-arcs -ftest-coverage -fprofile-instr-generate -fcoverage-mapping
    )

    target_link_options(${DAEMON_NAME}
        PRIVATE
        -fprofile-arcs -fprofile-instr-generate
    )
endif(COV)

set(BOOST_ROOT ${CMAKE_SOURCE_DIR}/out/install/boost)
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/lib)
#set(Boost_USE_STATIC_LIBS ON)

find_package(Boost 1.88.0 REQUIRED)
find_package(Boost REQUIRED COMPONENTS json)
find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Boost REQUIRED COMPONENTS serialization)
find_package(Boost REQUIRED COMPONENTS thread)

include_directories(
    include
    SYSTEM PRIVATE ${Boost_INCLUDE_DIRS}
    SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/libusb/libusb
)

target_link_libraries(
    ${DAEMON_NAME}
    PRIVATE ${LIB_NAME}
    PRIVATE Boost::json
    PRIVATE Boost::program_options
    PRIVATE Boost::serialization
)

target_link_libraries(
    ${LIB_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/out/install/libusb/lib/libusb-1.0.a
    PRIVATE Boost::thread
    udev
    systemd
)

add_subdirectory(src/test)

#enable_sanitizers(${DAEMON_NAME} tsan)
