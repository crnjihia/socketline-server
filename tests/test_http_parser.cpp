#include <gtest/gtest.h>
#include "socketline/http_parser.hpp"

using namespace socketline;

TEST(HttpParserTest, ParsesSimpleGet) {
    HttpParser parser;
    HttpRequest req;
    std::string_view raw = "GET /index.html HTTP/1.1\r\n"
                           "Host: localhost:8080\r\n"
                           "User-Agent: wrk\r\n"
                           "Accept: */*\r\n\r\n";

    ParseResult res = parser.parse(raw, req);
    ASSERT_EQ(res, ParseResult::Success);
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.target, "/index.html");
    EXPECT_EQ(req.version, "HTTP/1.1");
    EXPECT_EQ(req.get_header("Host"), "localhost:8080");
    EXPECT_EQ(req.get_header("host"), "localhost:8080"); // Case-insensitive
    EXPECT_EQ(req.get_header("USER-AGENT"), "wrk");
    EXPECT_EQ(req.get_header("Accept"), "*/*");
    EXPECT_TRUE(req.body.empty());
    EXPECT_EQ(req.total_parsed_bytes, raw.size());
}

TEST(HttpParserTest, HandlesExpect100Continue) {
    HttpParser parser;
    HttpRequest req;
    std::string_view raw = "POST /upload HTTP/1.1\r\n"
                           "Host: api.safaricom.co.ke\r\n"
                           "Content-Length: 100\r\n"
                           "Expect: 100-continue\r\n\r\n";

    ParseResult res = parser.parse(raw, req);
    // Since content-length is 100 and no body is supplied, it is Incomplete for body
    ASSERT_EQ(res, ParseResult::Incomplete);
    EXPECT_TRUE(req.has_expect_100_continue);
    EXPECT_EQ(req.content_length, 100);
}

TEST(HttpParserTest, ParsesPostWithBody) {
    HttpParser parser;
    HttpRequest req;
    std::string body = "{\"BusinessShortCode\":\"174379\",\"amount\":1500}";
    std::string raw = "POST /callback HTTP/1.1\r\n"
                      "Host: localhost:8080\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "X-Hub-Signature: sha256=abcdef123456\r\n\r\n" + body;

    ParseResult res = parser.parse(raw, req);
    ASSERT_EQ(res, ParseResult::Success);
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.target, "/callback");
    EXPECT_EQ(req.body, body);
    EXPECT_EQ(req.get_header("X-Hub-Signature"), "sha256=abcdef123456");
}

TEST(HttpParserTest, ParsesChunkedTransferEncoding) {
    HttpParser parser;
    HttpRequest req;
    // Two chunks: 4 bytes ("wiki"), 6 bytes ("pedia\n"), then terminator 0\r\n\r\n
    std::string raw = "POST /stream HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Transfer-Encoding: chunked\r\n\r\n"
                      "4\r\nwiki\r\n"
                      "6\r\npedia\n\r\n"
                      "0\r\n\r\n";

    ParseResult res = parser.parse(raw, req);
    ASSERT_EQ(res, ParseResult::Success);
    EXPECT_TRUE(req.is_chunked);
}

TEST(HttpParserTest, ReturnsIncompleteForPartialHeaders) {
    HttpParser parser;
    HttpRequest req;
    std::string_view partial = "GET /test HTTP/1.1\r\nHost: example.com\r\n";

    ParseResult res = parser.parse(partial, req);
    EXPECT_EQ(res, ParseResult::Incomplete);
}

TEST(HttpParserTest, ReturnsErrorForMalformedRequest) {
    HttpParser parser;
    HttpRequest req;
    std::string_view malformed = "GARBAGE_WITHOUT_SPACES\r\n\r\n";

    ParseResult res = parser.parse(malformed, req);
    EXPECT_EQ(res, ParseResult::Error);
}
