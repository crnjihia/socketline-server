#include <benchmark/benchmark.h>
#include "socketline/http_parser.hpp"

static void BM_HttpParse(benchmark::State& state) {
    std::string_view request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    socketline::HttpRequest req;
    socketline::HttpParser parser;
    for (auto _ : state) {
        benchmark::DoNotOptimize(parser.parse(request, req));
    }
}
BENCHMARK(BM_HttpParse);
BENCHMARK_MAIN();
