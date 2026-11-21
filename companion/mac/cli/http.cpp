#include "http.h"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

namespace companion {
namespace {

constexpr char SCHEME[] = "http://";
constexpr int DEFAULT_PORT = 80;
constexpr size_t STATUS_CODE_OFFSET = 9;    // "HTTP/1.1 200 OK"

// A socket connected to `addr` within `timeout_ms`, left blocking with send/receive timeouts; -1 on failure.
int connect_with_timeout(const addrinfo& addr, int timeout_ms) {
    int fd = socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    connect(fd, addr.ai_addr, addr.ai_addrlen);
    pollfd p{fd, POLLOUT, 0};
    int err = 0;
    socklen_t len = sizeof err;
    if (poll(&p, 1, timeout_ms) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, 0);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return fd;
}

bool has_header(const std::string& head, const std::string& name, std::string* value = nullptr) {
    for (size_t at = head.find("\r\n"); at != std::string::npos; at = head.find("\r\n", at + 2)) {
        const std::string line = head.substr(at + 2, head.find("\r\n", at + 2) - at - 2);
        if (line.size() <= name.size() || strncasecmp(line.c_str(), name.c_str(), name.size()) != 0) continue;
        if (value) {
            const size_t start = line.find_first_not_of(' ', name.size());
            *value = start == std::string::npos ? "" : line.substr(start);
        }
        return true;
    }
    return false;
}

struct Exchange {
    int status = 0;
    bool keep = false;       // the pet keeps the connection open
    bool heard = false;      // any byte of a response arrived
};

// Sends the whole request and reads one response: to its Content-Length, or until the server closes.
Exchange exchange(int fd, const std::string& request, std::string* reply) {
    Exchange e;
    if (send(fd, request.data(), request.size(), 0) != ssize_t(request.size())) return e;
    std::string response;
    char buf[1024];
    size_t head_end = std::string::npos, length = std::string::npos;
    for (ssize_t n; (n = recv(fd, buf, sizeof buf, 0)) > 0;) {
        response.append(buf, size_t(n));
        e.heard = true;
        if (head_end == std::string::npos && (head_end = response.find("\r\n\r\n")) != std::string::npos) {
            std::string value;
            const std::string head = response.substr(0, head_end);
            if (has_header(head, "content-length:", &value)) length = size_t(std::atol(value.c_str()));
            e.keep = length != std::string::npos && !(has_header(head, "connection:", &value) &&
                                                      strcasecmp(value.c_str(), "close") == 0);
        }
        if (head_end != std::string::npos && length != std::string::npos && response.size() >= head_end + 4 + length)
            break;
    }
    const bool whole = head_end != std::string::npos &&
                       (length == std::string::npos || response.size() >= head_end + 4 + length);
    if (!whole) e.keep = false;
    if (response.rfind("HTTP/1.", 0) == 0 && response.size() > STATUS_CODE_OFFSET + 3)
        e.status = std::atoi(response.c_str() + STATUS_CODE_OFFSET);
    if (reply) *reply = head_end == std::string::npos ? "" : response.substr(head_end + 4, length);
    return e;
}

// True if the pet has closed `fd` or sent something nobody asked for: it can't carry the next request.
bool stale(int fd) {
    pollfd p{fd, POLLIN, 0};
    return poll(&p, 1, 0) != 0;
}

}  // namespace

bool parse_url(const std::string& text, Url& out) {
    std::string s = text;
    if (s.rfind(SCHEME, 0) == 0) s = s.substr(sizeof SCHEME - 1);
    s = s.substr(0, s.find('/'));
    size_t colon = s.rfind(':');
    out.host = colon == std::string::npos ? s : s.substr(0, colon);
    out.port = colon == std::string::npos ? DEFAULT_PORT : std::atoi(s.c_str() + colon + 1);
    return !out.host.empty() && out.port > 0;
}

void Connection::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

int Connection::post(const Url& url, const std::string& path, const std::string& body, int timeout_ms,
                     std::string* reply, const std::string& key) {
    if (fd_ >= 0 && (url.host != host_ || url.port != port_ || stale(fd_))) close();
    const std::string auth = key.empty() ? "" : "\r\nAuthorization: Bearer " + key;
    const std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + url.host + auth +
                                "\r\nContent-Type: application/json\r\nContent-Length: " +
                                std::to_string(body.size()) + (keep_ ? "" : "\r\nConnection: close") + "\r\n\r\n" +
                                body;
    if (fd_ >= 0) {
        const Exchange e = exchange(fd_, request, reply);
        if (e.status && e.keep && keep_) return e.status;
        close();
        if (e.status || e.heard) return e.status;   // only a request the pet never answered is sent again
    }
    addrinfo hints{}, *addrs = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(url.host.c_str(), std::to_string(url.port).c_str(), &hints, &addrs) != 0 || !addrs) return 0;
    int status = 0;
    for (addrinfo* a = addrs; a && !status; a = a->ai_next) {
        const int fd = connect_with_timeout(*a, timeout_ms);
        if (fd < 0) continue;
        const Exchange e = exchange(fd, request, reply);
        status = e.status;
        if (status && e.keep && keep_) {
            fd_ = fd;
            host_ = url.host;
            port_ = url.port;
        } else {
            ::close(fd);
        }
    }
    freeaddrinfo(addrs);
    return status;
}

int post(const Url& url, const std::string& path, const std::string& body, int timeout_ms, std::string* reply,
         const std::string& key) {
    Connection once(false);
    return once.post(url, path, body, timeout_ms, reply, key);
}

}  // namespace companion
