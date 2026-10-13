#pragma once

#include <string_view>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace socketline {

enum class ParseResult {
    Success,
    Incomplete,
    Error
};

struct HttpHeader {
    std::string_view name;
    std::string_view value;
};

struct HttpRequest {
    std::string_view method;
    std::string_view target;
    std::string_view version;
    std::vector<HttpHeader> headers;
    std::string_view body;

    bool has_expect_100_continue{false};
    bool is_chunked{false};
    size_t content_length{0};
    size_t total_parsed_bytes{0};

    std::string_view get_header(std::string_view name) const;
};

class HttpParser {
public:
    HttpParser() = default;

    /**
     * Parse raw buffer into HttpRequest using string_view slices.
     * Zero memory allocations on the hot path.
     */
    ParseResult parse(std::string_view data, HttpRequest& out_req);

    /**
     * Helper to decode chunked transfer encoding in-place or into string_views.
     */
    static ParseResult parse_chunked_body(std::string_view chunked_data,
                                          std::string_view& decoded_body,
                                          size_t& bytes_consumed);
};

} // namespace socketline
