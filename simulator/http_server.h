// A small blocking HTTP/1.1 server for the simulator: one request at a time, as the device's esp_http_server task
// serves them, and like it keeping connections open for the next request unless the client asks to close.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "door/http.h"

namespace nestbox {

struct HttpRequest {
    std::string method, path, body;
    std::string host, origin;    // those headers; "" if not sent
    std::string auth;            // the Authorization header
    std::string peer;            // the client's address
    bool loopback = false;       // sent from this Mac itself
};

class HttpServer {
public:
    using Handler = std::function<Response(const HttpRequest&)>;

    ~HttpServer() { stop(); }
    // Listens on every interface, IPv4 and IPv6, on `port` (0: any free one) and serves from a thread of its own.
    bool start(int port, Handler handler, std::string& error);
    int port() const { return port_; }
    void stop();

private:
    struct Client {
        int fd = -1;
        bool loopback = false;
        std::string peer;
        std::string in;          // bytes read past the last request
        int64_t idle_since = 0;  // ms on a steady clock
    };
    void serve();
    bool handle(Client& c);      // serves one request; false: close the connection

    Handler handler_;
    int fd_ = -1, port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::vector<Client> clients_;    // open connections waiting for their next request
};

}  // namespace nestbox
