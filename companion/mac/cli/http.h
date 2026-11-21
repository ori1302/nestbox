// A minimal HTTP/1.1 client for talking to the pet, each request bounded by a timeout.
#pragma once
#include <string>

namespace companion {

struct Url {
    std::string host;
    int port = 80;
};

// Reads "http://host[:port][/path]". Returns false if there is no host or the port is not positive.
bool parse_url(const std::string& text, Url& out);

// A connection to the pet kept open between requests, for a stream of reports: no new connection (and no name
// lookup) each time. When the pet has closed it, the next request opens a new one.
class Connection {
public:
    Connection() = default;
    explicit Connection(bool keep) : keep_(keep) {}   // false: closed after each request
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { close(); }

    // POSTs a JSON body, with `key` as a bearer token if given. Returns the HTTP status, or 0 if the pet could not
    // be reached within `timeout_ms` (applied to the connect, and to each send and receive). `reply`, if given,
    // receives the response body.
    int post(const Url& url, const std::string& path, const std::string& body, int timeout_ms,
             std::string* reply = nullptr, const std::string& key = "");
    void close();

private:
    int fd_ = -1;
    std::string host_;
    int port_ = 0;
    bool keep_ = true;
};

// One POST on a connection of its own, closed after it.
int post(const Url& url, const std::string& path, const std::string& body, int timeout_ms,
         std::string* reply = nullptr, const std::string& key = "");

}  // namespace companion
