#include "socketline/connection.hpp"
#include "socketline/daraja_relay.hpp"
#include "socketline/thread_pool.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>

namespace socketline {

Connection::Connection(int fd, ThreadPool* pool, DarajaRelay* relay)
    : fd_(fd), pool_(pool), relay_(relay) {}

Connection::~Connection() {
    close_connection();
}

void Connection::close_connection() {
    if (fd_ != -1) {
        if (relay_) {
            relay_->remove_connection(this);
        }
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

void Connection::on_event(uint32_t events) {
    if (fd_ == -1) return;

    if (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        close_connection();
        return;
    }

    if (events & EPOLLIN) {
        char buf[8192];
        while (true) {
            ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n > 0) {
                read_buf_.append(buf, static_cast<size_t>(n));
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Drained all available data
                    break;
                }
                close_connection();
                return;
            } else {
                // EOF / Peer disconnected
                close_connection();
                return;
            }
        }

        if (is_websocket_) {
            handle_websocket_data();
        } else {
            handle_http();
        }
    }
}

void Connection::handle_http() {
    HttpRequest req;
    ParseResult res = http_parser_.parse(read_buf_, req);

    if (res == ParseResult::Incomplete) {
        // Need more data from client
        if (req.has_expect_100_continue && read_buf_.find("\r\n\r\n") != std::string::npos) {
            // Acknowledge Expect: 100-continue so client sends the body
            const char continue_resp[] = "HTTP/1.1 100 Continue\r\n\r\n";
            send_raw(continue_resp, sizeof(continue_resp) - 1);
        }
        return;
    }

    if (res == ParseResult::Error) {
        send_response("400 Bad Request", "Connection: close\r\n", "Bad Request\n");
        close_connection();
        return;
    }

    // Check for WebSocket upgrade
    std::string_view upgrade = req.get_header("Upgrade");
    if (!upgrade.empty() && (upgrade == "websocket" || upgrade == "WebSocket")) {
        std::string_view sec_key = req.get_header("Sec-WebSocket-Key");
        if (sec_key.empty()) {
            send_response("400 Bad Request", "Connection: close\r\n", "Missing Sec-WebSocket-Key\n");
            close_connection();
            return;
        }
        do_websocket_upgrade(sec_key);
        // Remove parsed HTTP request bytes from buffer
        read_buf_.erase(0, req.total_parsed_bytes);
        if (!read_buf_.empty()) {
            handle_websocket_data();
        }
        return;
    }

    // Handle POST /callback (Daraja notification relay)
    if (req.method == "POST" && req.target == "/callback") {
        std::string_view sig = req.get_header("X-Hub-Signature");
        if (sig.empty()) sig = req.get_header("X-Signature");
        if (sig.empty()) sig = req.get_header("X-Safaricom-Signature");

        if (!relay_ || !relay_->verify_hmac(req.body, sig)) {
            send_response("403 Forbidden", "Content-Type: text/plain\r\nConnection: close\r\n", "Invalid HMAC Signature\n");
            close_connection();
            return;
        }

        auto cb = DarajaRelay::parse_callback(req.body);
        if (!cb.valid) {
            send_response("400 Bad Request", "Content-Type: text/plain\r\n", "Missing BusinessShortCode in callback\n");
        } else {
            relay_->process_callback(cb.business_short_code, std::string(req.body));
            send_response("200 OK", "Content-Type: application/json\r\n", "{\"status\":\"relayed\"}\n");
        }

        read_buf_.erase(0, req.total_parsed_bytes);
        return;
    }

    // Handle GET static files
    if (req.method == "GET") {
        std::string_view target = req.target;
        if (target == "/" || target.empty()) {
            target = "/index.html";
        }

        // Prevent directory traversal attacks
        if (target.find("..") != std::string_view::npos) {
            send_response("403 Forbidden", "Connection: close\r\n", "Forbidden\n");
            close_connection();
            return;
        }

        serve_static(target);
        read_buf_.erase(0, req.total_parsed_bytes);
        return;
    }

    send_response("405 Method Not Allowed", "Allow: GET, POST\r\nConnection: close\r\n", "Method Not Allowed\n");
    close_connection();
}

void Connection::serve_static(std::string_view path) {
    std::string full_path = "public";
    if (path.front() != '/') full_path.push_back('/');
    full_path.append(path);

    int file_fd = ::open(full_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        send_response("404 Not Found", "Content-Type: text/plain\r\n", "File Not Found\n");
        return;
    }

    struct stat st{};
    if (::fstat(file_fd, &st) < 0) {
        ::close(file_fd);
        send_response("500 Internal Server Error", "", "Internal Error\n");
        return;
    }

    off_t filesize = st.st_size;

    std::string content_type = "text/plain";
    if (full_path.ends_with(".html")) content_type = "text/html";
    else if (full_path.ends_with(".css")) content_type = "text/css";
    else if (full_path.ends_with(".js")) content_type = "application/javascript";
    else if (full_path.ends_with(".json")) content_type = "application/json";

    std::ostringstream hdr;
    hdr << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << filesize << "\r\n"
        << "Connection: keep-alive\r\n"
        << "\r\n";

    std::string hdr_str = hdr.str();
    send_raw(hdr_str.data(), hdr_str.size());

    // Zero-copy sendfile(2) loop
    off_t offset = 0;
    while (offset < filesize) {
        size_t count = static_cast<size_t>(filesize - offset);
        ssize_t sent = ::sendfile(fd_, file_fd, &offset, count);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // In production, would register EPOLLOUT to drain; wait briefly
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (sent == 0) {
            break;
        }
    }

    ::close(file_fd);
}

void Connection::do_websocket_upgrade(std::string_view sec_key) {
    std::string accept_key = WebSocket::compute_accept_key(sec_key);

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
         << "\r\n";

    std::string resp_str = resp.str();
    send_raw(resp_str.data(), resp_str.size());
    is_websocket_ = true;
}

void Connection::handle_websocket_data() {
    while (read_buf_.size() >= 2) {
        WsFrame frame;
        const auto* data_ptr = reinterpret_cast<const uint8_t*>(read_buf_.data());
        if (!WebSocket::parse_frame(data_ptr, read_buf_.size(), frame)) {
            // Incomplete frame, wait for more data
            break;
        }

        read_buf_.erase(0, frame.bytes_consumed);

        // Control frame: Ping -> Pong
        if (frame.opcode == static_cast<uint8_t>(WsOpcode::Ping)) {
            auto pong = WebSocket::build_pong_frame(frame.payload);
            send_raw(pong.data(), pong.size());
            continue;
        }

        // Control frame: Close
        if (frame.opcode == static_cast<uint8_t>(WsOpcode::Close)) {
            auto close_reply = WebSocket::build_close_frame(1000, "Normal Closure");
            send_raw(close_reply.data(), close_reply.size());
            close_connection();
            return;
        }

        // Frame reassembly (handles fragmented frames)
        ws_parser_.add_fragment(frame);
        if (!ws_parser_.has_complete_message()) {
            continue;
        }

        const auto& full_payload = ws_parser_.complete_message();
        std::string text(full_payload.begin(), full_payload.end());
        ws_parser_.reset_message();

        // Process Daraja subscription commands
        if (text.rfind("SUBSCRIBE:", 0) == 0) {
            std::string code = text.substr(10);
            while (!code.empty() && (code.back() == '\r' || code.back() == '\n' || code.back() == ' ')) {
                code.pop_back();
            }
            if (!subscribed_short_code_.empty() && relay_) {
                relay_->unsubscribe(subscribed_short_code_, this);
            }
            subscribed_short_code_ = code;
            if (relay_) {
                relay_->subscribe(subscribed_short_code_, this);
            }
            send_ws_message("SUBSCRIBED:" + code);
        } else if (text.rfind("UNSUBSCRIBE", 0) == 0) {
            if (!subscribed_short_code_.empty() && relay_) {
                relay_->unsubscribe(subscribed_short_code_, this);
                send_ws_message("UNSUBSCRIBED:" + subscribed_short_code_);
                subscribed_short_code_.clear();
            }
        } else if (text.rfind("REPLAY:", 0) == 0) {
            std::string code = text.substr(7);
            while (!code.empty() && (code.back() == '\r' || code.back() == '\n' || code.back() == ' ')) {
                code.pop_back();
            }
            if (relay_) {
                relay_->replay_events(code, this);
            }
        } else {
            send_ws_message("ECHO:" + text);
        }
    }
}

void Connection::deliver_event(std::string_view event) {
    send_ws_message(event);
}

void Connection::send_ws_message(std::string_view message) {
    auto frame = WebSocket::build_text_frame(message);
    send_raw(frame.data(), frame.size());
}

void Connection::send_response(std::string_view status, std::string_view headers, std::string_view body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << "\r\n";
    if (headers.find("Content-Length:") == std::string_view::npos) {
        resp << "Content-Length: " << body.size() << "\r\n";
    }
    resp << headers << "\r\n" << body;
    std::string s = resp.str();
    send_raw(s.data(), s.size());
}

void Connection::send_raw(const void* data, size_t len) {
    if (fd_ == -1) return;
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t w = ::write(fd_, ptr, remaining);
        if (w > 0) {
            ptr += w;
            remaining -= static_cast<size_t>(w);
        } else if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            close_connection();
            break;
        } else {
            close_connection();
            break;
        }
    }
}

} // namespace socketline
