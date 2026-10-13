#pragma once

#include <string_view>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace socketline {

enum class WsOpcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

struct WsFrame {
    bool fin{true};
    uint8_t opcode{static_cast<uint8_t>(WsOpcode::Text)};
    bool masked{false};
    uint16_t close_code{0};
    std::string close_reason;
    std::vector<uint8_t> payload;
    size_t bytes_consumed{0};

    bool is_control() const {
        return (opcode & 0x8) != 0;
    }
};

class WebSocket {
public:
    static constexpr std::string_view kRfc6455Guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    /**
     * Computes the Sec-WebSocket-Accept value from client Sec-WebSocket-Key
     * using SHA-1 and Base64 per RFC 6455.
     */
    static std::string compute_accept_key(std::string_view client_key);

    /**
     * Parses a single WebSocket frame from a byte buffer.
     * Returns true if a full frame was parsed, false if more data is needed or malformed.
     */
    static bool parse_frame(const uint8_t* data, size_t len, WsFrame& out_frame);
    static bool parse_frame(const std::vector<uint8_t>& data, WsFrame& out_frame);

    /**
     * Builds an RFC 6455 WebSocket frame.
     * Server-to-client frames should not be masked (mask = false).
     */
    static std::vector<uint8_t> build_frame(const uint8_t* payload, size_t len,
                                            uint8_t opcode = static_cast<uint8_t>(WsOpcode::Text),
                                            bool fin = true,
                                            bool mask = false);

    static std::vector<uint8_t> build_frame(const std::vector<uint8_t>& payload,
                                            uint8_t opcode = static_cast<uint8_t>(WsOpcode::Text),
                                            bool fin = true,
                                            bool mask = false);

    static std::vector<uint8_t> build_text_frame(std::string_view text);
    static std::vector<uint8_t> build_close_frame(uint16_t code = 1000, std::string_view reason = "");
    static std::vector<uint8_t> build_pong_frame(const std::vector<uint8_t>& ping_payload);

    /**
     * Fragment assembler for handling multi-frame messages.
     */
    void add_fragment(const WsFrame& frame);
    bool has_complete_message() const { return message_ready_; }
    const std::vector<uint8_t>& complete_message() const { return assembled_payload_; }
    uint8_t message_opcode() const { return assembled_opcode_; }
    void reset_message();

private:
    std::vector<uint8_t> assembled_payload_;
    uint8_t assembled_opcode_{0};
    bool assembling_{false};
    bool message_ready_{false};
};

} // namespace socketline
