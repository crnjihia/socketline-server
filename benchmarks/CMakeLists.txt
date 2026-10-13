# Benchmark CMake configuration

include(FetchContent)
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
FetchContent_MakeAvailable(benchmark)

add_executable(socketline_bench benchmark_http.cpp)

target_link_libraries(socketline_bench PRIVATE benchmark::benchmark socketline)

# Enable testing of benchmarks if desired
add_test(NAME benchmark_http COMMAND socketline_bench)
