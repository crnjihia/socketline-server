#include <gtest/gtest.h>
#include "socketline/websocket.hpp"

using namespace socketline;

TEST(WebSocketTest, Rfc6455HandshakeVector) {
    // Official test vector from RFC 6455 Section 1.3
    std::string_view key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string expected_accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

    std::string accept = WebSocket::compute_accept_key(key);
    EXPECT_EQ(accept, expected_accept);
}

TEST(WebSocketTest, ParseUnmaskedTextFrame) {
    std::string text = "Hello WebSocket";
    auto frame = WebSocket::build_text_frame(text);

    WsFrame parsed;
    ASSERT_TRUE(WebSocket::parse_frame(frame, parsed));
    EXPECT_TRUE(parsed.fin);
    EXPECT_EQ(parsed.opcode, static_cast<uint8_t>(WsOpcode::Text));
    EXPECT_FALSE(parsed.masked);
    std::string payload_str(parsed.payload.begin(), parsed.payload.end());
    EXPECT_EQ(payload_str, text);
    EXPECT_EQ(parsed.bytes_consumed, frame.size());
}

TEST(WebSocketTest, ParseMaskedClientFrame) {
    std::string text = "Masked Payload From Client";
    std::vector<uint8_t> payload(text.begin(), text.end());

    // Client-to-server frames MUST be masked
    auto frame = WebSocket::build_frame(payload, static_cast<uint8_t>(WsOpcode::Text), true, true);

    WsFrame parsed;
    ASSERT_TRUE(WebSocket::parse_frame(frame, parsed));
    EXPECT_TRUE(parsed.fin);
    EXPECT_TRUE(parsed.masked);
    std::string unmasked(parsed.payload.begin(), parsed.payload.end());
    EXPECT_EQ(unmasked, text);
}

TEST(WebSocketTest, ParseExtendedLength126) {
    // Payload between 126 and 65535 bytes triggers 16-bit length
    std::vector<uint8_t> large_payload(500, 'Z');
    auto frame = WebSocket::build_frame(large_payload, static_cast<uint8_t>(WsOpcode::Binary), true, false);

    WsFrame parsed;
    ASSERT_TRUE(WebSocket::parse_frame(frame, parsed));
    EXPECT_EQ(parsed.opcode, static_cast<uint8_t>(WsOpcode::Binary));
    EXPECT_EQ(parsed.payload.size(), 500);
    EXPECT_EQ(parsed.payload, large_payload);
}

TEST(WebSocketTest, ControlPingPongFrames) {
    std::vector<uint8_t> ping_data = {'h', 'e', 'a', 'r', 't', 'b', 'e', 'a', 't'};
    auto ping_frame = WebSocket::build_frame(ping_data, static_cast<uint8_t>(WsOpcode::Ping), true, false);

    WsFrame parsed_ping;
    ASSERT_TRUE(WebSocket::parse_frame(ping_frame, parsed_ping));
    EXPECT_TRUE(parsed_ping.is_control());
    EXPECT_EQ(parsed_ping.opcode, static_cast<uint8_t>(WsOpcode::Ping));
    EXPECT_EQ(parsed_ping.payload, ping_data);

    auto pong_frame = WebSocket::build_pong_frame(parsed_ping.payload);
    WsFrame parsed_pong;
    ASSERT_TRUE(WebSocket::parse_frame(pong_frame, parsed_pong));
    EXPECT_EQ(parsed_pong.opcode, static_cast<uint8_t>(WsOpcode::Pong));
    EXPECT_EQ(parsed_pong.payload, ping_data);
}

TEST(WebSocketTest, CloseFrameWithStatusAndReason) {
    uint16_t code = 1000;
    std::string reason = "Normal Closure";
    auto close_frame = WebSocket::build_close_frame(code, reason);

    WsFrame parsed;
    ASSERT_TRUE(WebSocket::parse_frame(close_frame, parsed));
    EXPECT_TRUE(parsed.is_control());
    EXPECT_EQ(parsed.opcode, static_cast<uint8_t>(WsOpcode::Close));
    EXPECT_EQ(parsed.close_code, 1000);
    EXPECT_EQ(parsed.close_reason, reason);
}

TEST(WebSocketTest, FrameFragmentationReassembly) {
    WebSocket ws;

    // Fragment 1: FIN=false, Opcode=Text
    std::vector<uint8_t> part1 = {'P', 'a', 'r', 't', '1', ' '};
    auto f1_bytes = WebSocket::build_frame(part1, static_cast<uint8_t>(WsOpcode::Text), false, false);
    WsFrame f1;
    ASSERT_TRUE(WebSocket::parse_frame(f1_bytes, f1));
    EXPECT_FALSE(f1.fin);
    ws.add_fragment(f1);
    EXPECT_FALSE(ws.has_complete_message());

    // Fragment 2: FIN=true, Opcode=Continuation (0x0)
    std::vector<uint8_t> part2 = {'P', 'a', 'r', 't', '2'};
    auto f2_bytes = WebSocket::build_frame(part2, static_cast<uint8_t>(WsOpcode::Continuation), true, false);
    WsFrame f2;
    ASSERT_TRUE(WebSocket::parse_frame(f2_bytes, f2));
    EXPECT_TRUE(f2.fin);
    ws.add_fragment(f2);
    EXPECT_TRUE(ws.has_complete_message());

    std::string full_msg(ws.complete_message().begin(), ws.complete_message().end());
    EXPECT_EQ(full_msg, "Part1 Part2");
    EXPECT_EQ(ws.message_opcode(), static_cast<uint8_t>(WsOpcode::Text));
}
