cmake_minimum_required(VERSION 3.21)

project(canon_lbp810_bridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE LIBCAPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcapt/libcapt/*.cpp"
)
list(FILTER LIBCAPT_SOURCES EXCLUDE REGEX "/Decoder/")

add_library(libcapt STATIC ${LIBCAPT_SOURCES})
target_include_directories(libcapt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcapt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcapt/libcapt"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/Config.hpp"
    "#pragma once\n#define HAVE_STOP_TOKEN 1\n"
)

add_executable(lbp810-bridge
    src/main.cpp
    src/BridgeServer.cpp
    src/PbmRaster.cpp
    src/WindowsService.cpp
    src/WindowsUsbPrinter.cpp
    third_party/captppd/captbackend/Core/CaptPrinter.cpp
    third_party/captppd/captbackend/Core/Log.cpp
    third_party/captppd/captbackend/Core/StateReporter.cpp
)

target_include_directories(lbp810-bridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/captppd/captbackend"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_definitions(lbp810-bridge PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(lbp810-bridge PRIVATE
    libcapt
    setupapi
    ws2_32
)

if(MINGW)
    target_compile_options(libcapt PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lbp810-bridge PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(lbp810-bridge PRIVATE -static)
endif()

set_target_properties(lbp810-bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)
