#include "http_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include "door/api.h"
#include "host/protocol.h"

namespace nestbox {

namespace {
constexpr int BACKLOG = 8;
constexpr int ACCEPT_POLL_MS = 200;          // how soon stop() is noticed
constexpr int SOCKET_TIMEOUT_S = 2;
constexpr size_t MAX_KEPT = 4;               // open connections kept for their next request; the oldest goes first
constexpr int64_t KEEP_IDLE_MS = 30000;      // a kept connection with nothing to say for this long is closed
constexpr size_t MAX_HEAD = 8192;
constexpr size_t RECV_CHUNK = 2048;
constexpr const char* HEAD_END = "\r\n\r\n";
constexpr const char* CRLF = "\r\n";
constexpr const char* CONTENT_LENGTH = "content-length:";

int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// True if `line` is the header `name` (given with its colon, any case).
bool is_header(const std::string& line, const char* name) {
    const size_t n = std::strlen(name);
    return line.size() > n && strncasecmp(line.c_str(), name, n) == 0;
}

// The value of header `name` on `line` without leading spaces, into `value`; false if not that header or empty.
bool header_value(const std::string& line, const char* name, std::string& value) {
    if (!is_header(line, name)) return false;
    const size_t at = line.find_first_not_of(' ', std::strlen(name));
    if (at == std::string::npos) return false;
    value = line.substr(at);
    return true;
}

// The peer is this Mac: a loopback address, or the Mac's own address at both ends of the connection. The listener is
// IPv6 with IPv4 mapped in, so every peer arrives as AF_INET6.
bool from_this_mac(int fd, const sockaddr_storage& peer) {
    if (peer.ss_family != AF_INET6) return false;
    sockaddr_in6 me{};
    socklen_t me_len = sizeof me;
    getsockname(fd, reinterpret_cast<sockaddr*>(&me), &me_len);
    const in6_addr& a = reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_addr;
    return IN6_IS_ADDR_LOOPBACK(&a) || (IN6_IS_ADDR_V4MAPPED(&a) && a.s6_addr[12] == 127) ||
           std::memcmp(&a, &me.sin6_addr, sizeof a) == 0;
}

std::string status_line(int status) {
    const char* reason = reason_phrase(status);
    return "HTTP/1.1 " + std::to_string(status) + " " + (reason ? reason : "Error");
}

bool send_all(int fd, const std::string& out) {
    for (size_t sent = 0; sent < out.size();) {
        const ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

bool reply(int fd, const Response& r, bool keep) {
    return send_all(fd, status_line(r.status) + "\r\nContent-Type: " + r.type + "\r\nContent-Length: " +
                            std::to_string(r.body.size()) +
                            (r.location.empty() ? "" : "\r\nLocation: " + r.location) +
                            "\r\nCache-Control: no-store\r\nConnection: " + (keep ? "keep-alive" : "close") +
                            "\r\n\r\n" + r.body) &&
           keep;
}

Response bad_request() {
    Response r;
    set_error(r, 400, "malformed request");
    return r;
}
}  // namespace

bool HttpServer::start(int port, Handler handler, std::string& error) {
    handler_ = std::move(handler);
    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ < 0) {
        error = std::strerror(errno);
        return false;
    }
    int one = 1, zero = 0;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);   // IPv4 too, as ::ffff:a.b.c.d
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_any;
    a.sin6_port = htons(uint16_t(port));
    if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || listen(fd_, BACKLOG) < 0) {
        error = std::string("port ") + std::to_string(port) + ": " + std::strerror(errno);
        close(fd_);
        fd_ = -1;
        return false;
    }
    socklen_t len = sizeof a;
    getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin6_port);
    running_ = true;
    thread_ = std::thread([this] { serve(); });
    return true;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    for (const Client& c : clients_) close(c.fd);
    clients_.clear();
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}

void HttpServer::serve() {
    while (running_) {
        std::vector<pollfd> p{{fd_, POLLIN, 0}};
        for (const Client& c : clients_) p.push_back({c.fd, POLLIN, 0});
        const int ready = poll(p.data(), nfds_t(p.size()), ACCEPT_POLL_MS);
        const int64_t now = steady_ms();
        std::vector<Client> kept;
        for (size_t i = 0; i < clients_.size(); i++) {
            Client& c = clients_[i];
            const bool readable = ready > 0 && (p[i + 1].revents & (POLLIN | POLLHUP | POLLERR));
            if (readable ? handle(c) : now - c.idle_since < KEEP_IDLE_MS) {
                if (readable) c.idle_since = steady_ms();
                kept.push_back(std::move(c));
            } else {
                close(c.fd);
            }
        }
        clients_ = std::move(kept);
        if (ready <= 0 || !(p[0].revents & POLLIN)) continue;
        sockaddr_storage peer{};
        socklen_t peer_len = sizeof peer;
        Client c;
        c.fd = accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (c.fd < 0) continue;
        c.loopback = from_this_mac(c.fd, peer);
        char name[INET6_ADDRSTRLEN] = "";
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_addr, name, sizeof name);
        c.peer = name;
        int one = 1;
        setsockopt(c.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        timeval tv{SOCKET_TIMEOUT_S, 0};
        setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (!handle(c)) {
            close(c.fd);
            continue;
        }
        c.idle_since = steady_ms();
        if (clients_.size() >= MAX_KEPT) {
            auto oldest = std::min_element(clients_.begin(), clients_.end(), [](const Client& a, const Client& b) {
                return a.idle_since < b.idle_since;
            });
            close(oldest->fd);
            clients_.erase(oldest);
        }
        clients_.push_back(std::move(c));
    }
}

bool HttpServer::handle(Client& c) {
    const int fd = c.fd;
    std::string in = std::move(c.in);
    c.in.clear();
    char buf[RECV_CHUNK];
    size_t head_end = in.find(HEAD_END);
    while (head_end == std::string::npos && in.size() < MAX_HEAD) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        in.append(buf, size_t(n));
        head_end = in.find(HEAD_END);
    }
    if (head_end == std::string::npos) return reply(fd, bad_request(), false);
    const std::string head = in.substr(0, head_end);
    std::string body = in.substr(head_end + std::strlen(HEAD_END));
    const size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return reply(fd, bad_request(), false);
    const size_t line_end = head.find(CRLF);
    const std::string version = head.substr(sp2 + 1, line_end == std::string::npos ? line_end : line_end - sp2 - 1);

    HttpRequest q;
    q.method = head.substr(0, sp1);
    q.path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    q.loopback = c.loopback;
    q.peer = c.peer;
    size_t length = 0;
    // HTTP/1.1 keeps the connection unless the client says close; HTTP/1.0 only if it says keep-alive.
    bool keep = version == "HTTP/1.1";
    for (size_t at = head.find(CRLF); at != std::string::npos; at = head.find(CRLF, at + 2)) {
        const std::string line = head.substr(at + 2, head.find(CRLF, at + 2) - at - 2);
        if (is_header(line, CONTENT_LENGTH)) length = size_t(std::atol(line.c_str() + std::strlen(CONTENT_LENGTH)));
        header_value(line, "host:", q.host);
        header_value(line, "origin:", q.origin);
        header_value(line, "authorization:", q.auth);
        std::string connection;
        if (header_value(line, "connection:", connection)) {
            if (strcasecmp(connection.c_str(), "close") == 0) keep = false;
            if (strcasecmp(connection.c_str(), "keep-alive") == 0) keep = true;
        }
    }

    const size_t limit = body_limit(q.method, q.path);
    if (length > limit) return reply(fd, too_large_reply(limit), false);
    while (body.size() < length) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        body.append(buf, size_t(n));
    }
    c.in = body.substr(length);
    body.resize(length);
    q.body = std::move(body);
    return reply(fd, handler_(q), keep);
}

}  // namespace nestbox
