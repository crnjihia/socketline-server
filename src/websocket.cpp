#include "socketline/websocket.hpp"
#include <openssl/sha.h>
#include <cstring>
#include <arpa/inet.h>

namespace socketline {

static const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        size_t rem = len - i;
        uint32_t oct_a = data[i++];
        uint32_t oct_b = (rem > 1) ? data[i++] : 0;
        uint32_t oct_c = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (oct_a << 16) | (oct_b << 8) | oct_c;

        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(rem > 1 ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(rem > 2 ? kBase64Alphabet[triple & 0x3F] : '=');
    }

    return out;
}

std::string WebSocket::compute_accept_key(std::string_view client_key) {
    std::string combined;
    combined.reserve(client_key.size() + kRfc6455Guid.size());
    combined.append(client_key);
    combined.append(kRfc6455Guid);

    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), digest);

    return base64_encode(digest, SHA_DIGEST_LENGTH);
}

bool WebSocket::parse_frame(const uint8_t* data, size_t len, WsFrame& out) {
    if (len < 2) return false;

    size_t pos = 0;
    uint8_t b0 = data[pos++];
    out.fin = (b0 & 0x80) != 0;
    out.opcode = b0 & 0x0F;

    uint8_t b1 = data[pos++];
    out.masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if (payload_len == 126) {
        if (len < pos + 2) return false;
        payload_len = (static_cast<uint64_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        if (len < pos + 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | data[pos + i];
        }
        pos += 8;
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (out.masked) {
        if (len < pos + 4) return false;
        std::memcpy(mask_key, &data[pos], 4);
        pos += 4;
    }

    if (len < pos + payload_len) {
        return false;
    }

    out.payload.resize(static_cast<size_t>(payload_len));
    for (uint64_t i = 0; i < payload_len; ++i) {
        uint8_t byte = data[pos + i];
        if (out.masked) {
            byte ^= mask_key[i % 4];
        }
        out.payload[i] = byte;
    }

    out.bytes_consumed = pos + static_cast<size_t>(payload_len);

    // Parse close code if this is a Close frame
    if (out.opcode == static_cast<uint8_t>(WsOpcode::Close)) {
        if (out.payload.size() >= 2) {
            out.close_code = (static_cast<uint16_t>(out.payload[0]) << 8) | out.payload[1];
            if (out.payload.size() > 2) {
                out.close_reason.assign(reinterpret_cast<const char*>(&out.payload[2]),
                                        out.payload.size() - 2);
            }
        } else {
            out.close_code = 1005; // No status code present
        }
    }

    return true;
}

bool WebSocket::parse_frame(const std::vector<uint8_t>& data, WsFrame& out) {
    return parse_frame(data.data(), data.size(), out);
}

std::vector<uint8_t> WebSocket::build_frame(const uint8_t* payload, size_t len,
                                            uint8_t opcode, bool fin, bool mask) {
    std::vector<uint8_t> frame;
    frame.reserve(len + 14);

    uint8_t b0 = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
    frame.push_back(b0);

    uint8_t mask_bit = mask ? 0x80 : 0x00;
    if (len <= 125) {
        frame.push_back(mask_bit | static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(mask_bit | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    if (mask) {
        uint8_t mask_key[4] = {0x12, 0x34, 0x56, 0x78};
        frame.insert(frame.end(), mask_key, mask_key + 4);
        for (size_t i = 0; i < len; ++i) {
            frame.push_back(payload[i] ^ mask_key[i % 4]);
        }
    } else {
        if (payload && len > 0) {
            frame.insert(frame.end(), payload, payload + len);
        }
    }

    return frame;
}

std::vector<uint8_t> WebSocket::build_frame(const std::vector<uint8_t>& payload,
                                            uint8_t opcode, bool fin, bool mask) {
    return build_frame(payload.data(), payload.size(), opcode, fin, mask);
}

std::vector<uint8_t> WebSocket::build_text_frame(std::string_view text) {
    return build_frame(reinterpret_cast<const uint8_t*>(text.data()), text.size(),
                       static_cast<uint8_t>(WsOpcode::Text), true, false);
}

std::vector<uint8_t> WebSocket::build_close_frame(uint16_t code, std::string_view reason) {
    std::vector<uint8_t> payload;
    payload.reserve(2 + reason.size());
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());
    return build_frame(payload.data(), payload.size(),
                       static_cast<uint8_t>(WsOpcode::Close), true, false);
}

std::vector<uint8_t> WebSocket::build_pong_frame(const std::vector<uint8_t>& ping_payload) {
    return build_frame(ping_payload.data(), ping_payload.size(),
                       static_cast<uint8_t>(WsOpcode::Pong), true, false);
}

void WebSocket::add_fragment(const WsFrame& frame) {
    if (!assembling_) {
        assembled_payload_.clear();
        assembled_opcode_ = frame.opcode;
        assembling_ = true;
    }
    assembled_payload_.insert(assembled_payload_.end(),
                              frame.payload.begin(), frame.payload.end());
    if (frame.fin) {
        message_ready_ = true;
        assembling_ = false;
    }
}

void WebSocket::reset_message() {
    assembled_payload_.clear();
    assembled_opcode_ = 0;
    assembling_ = false;
    message_ready_ = false;
}

} // namespace socketline
