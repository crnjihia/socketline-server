#pragma once

#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include "socketline/ring_buffer.hpp"

namespace socketline {

class Connection;

struct DarajaCallback {
    std::string business_short_code;
    std::string raw_payload;
    bool valid{false};
};

class DarajaRelay {
public:
    explicit DarajaRelay(std::string secret_key = "daraja_secret_passkey");
    ~DarajaRelay() = default;

    DarajaRelay(const DarajaRelay&) = delete;
    DarajaRelay& operator=(const DarajaRelay&) = delete;

    void set_secret(std::string key);
    const std::string& secret() const { return secret_key_; }

    /**
     * Verifies HMAC-SHA256 signature over payload.
     * Expects hex-encoded signature (e.g. "sha256=..." or raw hex).
     */
    bool verify_hmac(std::string_view payload, std::string_view expected_signature) const;

    /**
     * Parses BusinessShortCode and details from JSON callback body.
     */
    static DarajaCallback parse_callback(std::string_view json_body);

    /**
     * Dispatches callback: stores in per-merchant ring buffer and fans out to subscribers.
     * Returns number of WebSocket clients notified.
     */
    size_t process_callback(const std::string& short_code, const std::string& payload);

    /**
     * Subscription management for WebSocket clients.
     */
    void subscribe(const std::string& short_code, Connection* conn);
    void unsubscribe(const std::string& short_code, Connection* conn);
    void remove_connection(Connection* conn);

    /**
     * Replays the last up-to-1000 events for a merchant to a connection.
     */
    void replay_events(const std::string& short_code, Connection* conn);

    /**
     * Accessor for testing ring buffer state.
     */
    std::shared_ptr<RingBuffer<std::string>> get_buffer(const std::string& short_code);

private:
    std::string secret_key_;
    static constexpr size_t kRingCapacity = 1000;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<RingBuffer<std::string>>> merchant_buffers_;
    std::unordered_map<std::string, std::vector<Connection*>> subscribers_;
};

} // namespace socketline
