#include "socketline/http_parser.hpp"
#include <cctype>
#include <charconv>

namespace socketline {

static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view HttpRequest::get_header(std::string_view name) const {
    for (const auto& h : headers) {
        if (iequals(h.name, name)) {
            return h.value;
        }
    }
    return {};
}

ParseResult HttpParser::parse(std::string_view data, HttpRequest& out_req) {
    out_req = HttpRequest{};

    // Request must end headers with \r\n\r\n
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        return ParseResult::Incomplete;
    }

    // 1. Parse Request Line
    size_t line_end = data.find("\r\n");
    if (line_end == std::string_view::npos || line_end > header_end) {
        return ParseResult::Error;
    }

    std::string_view req_line = data.substr(0, line_end);
    size_t sp1 = req_line.find(' ');
    if (sp1 == std::string_view::npos) return ParseResult::Error;

    size_t sp2 = req_line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return ParseResult::Error;

    out_req.method = req_line.substr(0, sp1);
    out_req.target = req_line.substr(sp1 + 1, sp2 - (sp1 + 1));
    out_req.version = req_line.substr(sp2 + 1);

    if (out_req.method.empty() || out_req.target.empty() || out_req.version.empty()) {
        return ParseResult::Error;
    }

    // 2. Parse Headers
    size_t pos = line_end + 2; // skip first \r\n
    while (pos < header_end) {
        size_t next_line = data.find("\r\n", pos);
        if (next_line == std::string_view::npos || next_line > header_end) {
            next_line = header_end;
        }

        std::string_view line = data.substr(pos, next_line - pos);
        if (!line.empty()) {
            size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                return ParseResult::Error;
            }

            std::string_view name = line.substr(0, colon);
            std::string_view val = line.substr(colon + 1);

            // Trim leading/trailing whitespace from value
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
                val.remove_prefix(1);
            }
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) {
                val.remove_suffix(1);
            }

            out_req.headers.push_back(HttpHeader{name, val});
        }
        pos = next_line + 2;
    }

    // 3. Process Header Directives
    std::string_view cl = out_req.get_header("Content-Length");
    if (!cl.empty()) {
        size_t val = 0;
        auto res = std::from_chars(cl.data(), cl.data() + cl.size(), val);
        if (res.ec == std::errc()) {
            out_req.content_length = val;
        }
    }

    std::string_view te = out_req.get_header("Transfer-Encoding");
    if (!te.empty() && te.find("chunked") != std::string_view::npos) {
        out_req.is_chunked = true;
    }

    std::string_view expect = out_req.get_header("Expect");
    if (!expect.empty() && iequals(expect, "100-continue")) {
        out_req.has_expect_100_continue = true;
    }

    // 4. Body Processing
    size_t body_start = header_end + 4;
    std::string_view body_data = data.substr(body_start);

    if (out_req.is_chunked) {
        std::string_view decoded;
        size_t consumed = 0;
        ParseResult cr = parse_chunked_body(body_data, decoded, consumed);
        if (cr != ParseResult::Success) {
            return cr;
        }
        out_req.body = decoded;
        out_req.total_parsed_bytes = body_start + consumed;
        return ParseResult::Success;
    }

    if (out_req.content_length > 0) {
        if (body_data.size() < out_req.content_length) {
            return ParseResult::Incomplete;
        }
        out_req.body = body_data.substr(0, out_req.content_length);
        out_req.total_parsed_bytes = body_start + out_req.content_length;
        return ParseResult::Success;
    }

    out_req.body = {};
    out_req.total_parsed_bytes = body_start;
    return ParseResult::Success;
}

ParseResult HttpParser::parse_chunked_body(std::string_view chunked_data,
                                          std::string_view& decoded_body,
                                          size_t& bytes_consumed) {
    size_t pos = 0;
    // For zero-copy single-chunk or verifying completion
    size_t first_chunk_start = 0;
    size_t total_payload_len = 0;
    bool has_chunks = false;

    while (pos < chunked_data.size()) {
        size_t line_end = chunked_data.find("\r\n", pos);
        if (line_end == std::string_view::npos) {
            return ParseResult::Incomplete;
        }

        std::string_view hex_str = chunked_data.substr(pos, line_end - pos);
        size_t chunk_len = 0;
        auto res = std::from_chars(hex_str.data(), hex_str.data() + hex_str.size(), chunk_len, 16);
        if (res.ec != std::errc()) {
            return ParseResult::Error;
        }

        size_t chunk_data_start = line_end + 2;
        if (chunk_len == 0) {
            // Last chunk (0\r\n\r\n)
            size_t trailer_end = chunked_data.find("\r\n", chunk_data_start);
            if (trailer_end == std::string_view::npos) {
                return ParseResult::Incomplete;
            }
            bytes_consumed = trailer_end + 2;
            if (has_chunks && total_payload_len > 0) {
                decoded_body = chunked_data.substr(first_chunk_start, total_payload_len);
            } else {
                decoded_body = {};
            }
            return ParseResult::Success;
        }

        if (chunked_data.size() < chunk_data_start + chunk_len + 2) {
            return ParseResult::Incomplete;
        }

        if (!has_chunks) {
            first_chunk_start = chunk_data_start;
            has_chunks = true;
        }
        total_payload_len += chunk_len;

        // Skip chunk data + \r\n
        pos = chunk_data_start + chunk_len + 2;
    }

    return ParseResult::Incomplete;
}

} // namespace socketline
