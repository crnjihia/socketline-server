#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "socketline/http_parser.hpp"
#include "socketline/websocket.hpp"

namespace socketline {

class ThreadPool;
class DarajaRelay;

class Connection {
public:
    Connection(int fd, ThreadPool* pool, DarajaRelay* relay);
    virtual ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void on_event(uint32_t events);
    bool is_closed() const { return fd_ == -1; }
    int fd() const { return fd_; }

    virtual void deliver_event(std::string_view event);
    virtual void send_ws_message(std::string_view message);
    void close_connection();

private:
    int fd_{-1};
    ThreadPool* pool_{nullptr};
    DarajaRelay* relay_{nullptr};

    std::string read_buf_;
    bool is_websocket_{false};
    std::string subscribed_short_code_;

    WebSocket ws_parser_;
    HttpParser http_parser_;

    void handle_http();
    void handle_websocket_data();
    void serve_static(std::string_view path);
    void do_websocket_upgrade(std::string_view sec_key);
    void send_response(std::string_view status, std::string_view headers, std::string_view body);
    void send_raw(const void* data, size_t len);
};

} // namespace socketline
