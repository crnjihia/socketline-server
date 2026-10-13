-- wrk_http.lua: wrk benchmark script for HTTP/1.1 pipelined static requests
wrk.method = "GET"
wrk.path = "/index.html"
wrk.headers["User-Agent"] = "wrk-benchmark/4.1.0"
wrk.headers["Accept"] = "*/*"
wrk.headers["Connection"] = "keep-alive"
