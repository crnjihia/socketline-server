cmake_minimum_required(VERSION 3.24)
project(socketline-server LANGUAGES CXX)

option(SOCKETLINE_USE_IO_URING "Enable io_uring backend" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
add_compile_options(-Wall -Wextra -Werror -pedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
endif()

# Find dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

set(SOCKETLINE_SOURCES
    src/event_loop.cpp
    src/epoll_backend.cpp
    src/io_uring_backend.cpp
    src/thread_pool.cpp
    src/http_parser.cpp
    src/websocket.cpp
    src/connection.cpp
    src/daraja_relay.cpp
)

set(SOCKETLINE_HEADERS
    include/socketline/event_loop.hpp
    include/socketline/epoll_backend.hpp
    include/socketline/io_uring_backend.hpp
    include/socketline/thread_pool.hpp
    include/socketline/http_parser.hpp
    include/socketline/websocket.hpp
    include/socketline/connection.hpp
    include/socketline/ring_buffer.hpp
    include/socketline/daraja_relay.hpp
)

add_library(socketline STATIC ${SOCKETLINE_SOURCES} ${SOCKETLINE_HEADERS})

target_include_directories(socketline PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(socketline PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(SOCKETLINE_USE_IO_URING)
    find_library(URING_LIB uring REQUIRED)
    target_link_libraries(socketline PUBLIC ${URING_LIB})
    target_compile_definitions(socketline PUBLIC SOCKETLINE_USE_IO_URING=1)
endif()

# Executable
add_executable(socketline_server src/main.cpp)
target_link_libraries(socketline_server PRIVATE socketline)

# Testing with GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows/MSVC runtime compatibility if needed
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(socketline_tests
    tests/test_http_parser.cpp
    tests/test_websocket_frame.cpp
    tests/test_ring_buffer.cpp
)

target_link_libraries(socketline_tests PRIVATE socketline gtest_main)
include(GoogleTest)
gtest_discover_tests(socketline_tests)

# Benchmarks
option(SOCKETLINE_BUILD_BENCHMARKS "Build benchmarks" OFF)
if(SOCKETLINE_BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()
