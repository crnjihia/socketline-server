#include "socketline/daraja_relay.hpp"
#include "socketline/connection.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <cctype>

namespace socketline {

DarajaRelay::DarajaRelay(std::string secret_key)
    : secret_key_(std::move(secret_key)) {}

void DarajaRelay::set_secret(std::string key) {
    std::lock_guard<std::mutex> lock(mutex_);
    secret_key_ = std::move(key);
}

bool DarajaRelay::verify_hmac(std::string_view payload, std::string_view expected_signature) const {
    std::string key_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_copy = secret_key_;
    }

    if (expected_signature.empty() || key_copy.empty()) {
        return false;
    }

    if (expected_signature.rfind("sha256=", 0) == 0) {
        expected_signature.remove_prefix(7);
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    size_t digest_len = 0;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr,
                   key_copy.data(), key_copy.size(),
                   reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
                   digest, sizeof(digest), &digest_len)) {
        return false;
    }
#else
    unsigned int u_len = 0;
    if (!HMAC(EVP_sha256(), key_copy.data(), static_cast<int>(key_copy.size()),
              reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
              digest, &u_len)) {
        return false;
    }
    digest_len = u_len;
#endif

    static const char hex_chars[] = "0123456789abcdef";
    std::string computed_hex;
    computed_hex.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        computed_hex.push_back(hex_chars[(digest[i] >> 4) & 0x0F]);
        computed_hex.push_back(hex_chars[digest[i] & 0x0F]);
    }

    if (computed_hex.size() != expected_signature.size()) {
        return false;
    }

    bool match = true;
    for (size_t i = 0; i < computed_hex.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(computed_hex[i])) !=
            std::tolower(static_cast<unsigned char>(expected_signature[i]))) {
            match = false;
        }
    }
    return match;
}

DarajaCallback DarajaRelay::parse_callback(std::string_view json_body) {
    DarajaCallback cb;
    cb.raw_payload = std::string(json_body);

    auto extract_string = [&](std::string_view key) -> std::string {
        std::string search = "\"" + std::string(key) + "\":";
        size_t pos = json_body.find(search);
        if (pos == std::string_view::npos) return {};

        pos += search.size();
        while (pos < json_body.size() && (json_body[pos] == ' ' || json_body[pos] == '\t')) {
            pos++;
        }
        if (pos >= json_body.size()) return {};

        if (json_body[pos] == '"') {
            pos++;
            size_t end_quote = json_body.find('"', pos);
            if (end_quote != std::string_view::npos) {
                return std::string(json_body.substr(pos, end_quote - pos));
            }
        } else {
            // Unquoted number (e.g. "BusinessShortCode": 174379)
            size_t end_num = pos;
            while (end_num < json_body.size() &&
                   (std::isdigit(static_cast<unsigned char>(json_body[end_num])))) {
                end_num++;
            }
            if (end_num > pos) {
                return std::string(json_body.substr(pos, end_num - pos));
            }
        }
        return {};
    };

    cb.business_short_code = extract_string("BusinessShortCode");
    if (cb.business_short_code.empty()) {
        cb.business_short_code = extract_string("ShortCode");
    }
    if (cb.business_short_code.empty()) {
        cb.business_short_code = extract_string("merchant");
    }

    cb.valid = !cb.business_short_code.empty();
    return cb;
}

size_t DarajaRelay::process_callback(const std::string& short_code, const std::string& payload) {
    std::shared_ptr<RingBuffer<std::string>> buf;
    std::vector<Connection*> target_conns;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = merchant_buffers_[short_code];
        if (!entry) {
            entry = std::make_shared<RingBuffer<std::string>>(kRingCapacity);
        }
        buf = entry;

        auto sub_it = subscribers_.find(short_code);
        if (sub_it != subscribers_.end()) {
            target_conns = sub_it->second;
        }
    }

    if (buf) {
        buf->push(payload);
    }

    for (Connection* conn : target_conns) {
        if (conn && !conn->is_closed()) {
            conn->deliver_event(payload);
        }
    }

    return target_conns.size();
}

void DarajaRelay::subscribe(const std::string& short_code, Connection* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& list = subscribers_[short_code];
    if (std::find(list.begin(), list.end(), conn) == list.end()) {
        list.push_back(conn);
    }
}

void DarajaRelay::unsubscribe(const std::string& short_code, Connection* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(short_code);
    if (it != subscribers_.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), conn), list.end());
        if (list.empty()) {
            subscribers_.erase(it);
        }
    }
}

void DarajaRelay::remove_connection(Connection* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = subscribers_.begin(); it != subscribers_.end();) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), conn), list.end());
        if (list.empty()) {
            it = subscribers_.erase(it);
        } else {
            ++it;
        }
    }
}

void DarajaRelay::replay_events(const std::string& short_code, Connection* conn) {
    if (!conn) return;
    std::shared_ptr<RingBuffer<std::string>> buf;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = merchant_buffers_.find(short_code);
        if (it != merchant_buffers_.end()) {
            buf = it->second;
        }
    }

    if (buf) {
        auto events = buf->snapshot();
        for (const auto& ev : events) {
            conn->deliver_event(ev);
        }
    }
}

std::shared_ptr<RingBuffer<std::string>> DarajaRelay::get_buffer(const std::string& short_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = merchant_buffers_.find(short_code);
    if (it != merchant_buffers_.end()) {
        return it->second;
    }
    auto new_buf = std::make_shared<RingBuffer<std::string>>(kRingCapacity);
    merchant_buffers_[short_code] = new_buf;
    return new_buf;
}

} // namespace socketline
