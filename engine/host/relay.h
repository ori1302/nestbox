// Webhooks through the NestBox relay (hooks.nestbox.pet): the device keeps one WebSocket open to its mailbox there,
// and each webhook posted to the mailbox's URL arrives over it as a frame, {"headers": {...}, "body": "..."}. A
// frame whose X-NestBox-Secret matches the device's secret is told to the pet as a tell; anything else is dropped.
//
// The platform owns the socket; RelayLink says when to connect, ping and wait, and relay_body() and relay_deliver()
// handle each frame. Connect: GET <relay>/connect as a WebSocket, with relay_headers(). Ping: the text frame "ping" (the relay
// answers "pong" without waking).
#pragma once
#include <cstdint>
#include <string>
#include "door/api.h"

namespace nestbox {

constexpr const char* RELAY_HOST = "hooks.nestbox.pet";
constexpr const char* RELAY_CONNECT_PATH = "/connect";
constexpr const char* RELAY_PING = "ping";

// The upgrade request's extra headers, each ending in CRLF.
std::string relay_headers(const RelaySettings& r);

class RelayLink {
public:
    static constexpr uint32_t PING_MS = 30 * 1000;      // a ping this often keeps the connection and proves it alive
    static constexpr uint32_t SILENT_MS = 75 * 1000;    // nothing heard, not even a pong, for this long: reconnect
    static constexpr uint32_t FIRST_RETRY_MS = 2 * 1000;
    static constexpr uint32_t LAST_RETRY_MS = 5 * 60 * 1000;
    static constexpr uint32_t REFUSED_RETRY_MS = 60 * 60 * 1000;   // the relay refused the mailbox: try again hourly

    enum class State { Off, Waiting, Connecting, Connected, Refused, Replaced };

    // The settings changed (or the device started): start over, connecting at once if there is anything to do.
    void configure(const RelaySettings& r, uint32_t now_ms);
    // Whether the platform should open the socket now (it is online, and the backoff has passed).
    bool should_connect(uint32_t now_ms) const;
    void connecting(uint32_t now_ms);
    void connected(uint32_t now_ms);
    // The socket closed, or the connection failed. `code` is the WebSocket close code, or the HTTP status of a
    // refused upgrade, or 0 for a network failure. A refused mailbox (401, or the relay removing it) waits an hour
    // before trying again. A newer connection to the same mailbox (close code 4000) stops it until the settings
    // change or the device restarts: two devices on one mailbox never fight over it.
    void closed(uint32_t now_ms, int code);
    // A frame (or pong) arrived.
    void heard(uint32_t now_ms);
    // Whether to send a ping now; true at most once per PING_MS.
    bool should_ping(uint32_t now_ms);
    // Connected but silent too long: the platform closes the socket and calls closed(now, 0).
    bool silent(uint32_t now_ms) const;

    State state() const { return state_; }
    // For GET /v1: "off", "waiting", "connecting", "connected", "refused", "replaced".
    const char* state_word() const;

private:
    State state_ = State::Off;
    uint32_t retry_at_ = 0, retry_ms_ = FIRST_RETRY_MS, pinged_at_ = 0, heard_at_ = 0;
};

// A frame from the relay: when its secret is right and its body is a JSON object, the body; else "" and `why` says
// why ("wrong secret", "not a relay frame", "body not a JSON object"). Needs no lock: the platform passes a copy of
// the secret.
std::string relay_body(const std::string& frame, const std::string& secret, std::string& why);

// Handles one frame: its body, if relay_body() gives one, is told to the pet (POST /v1/tell, as a request from the
// network that carries the device's key) and the pet's status returned; otherwise status 0 and `why`.
struct Delivered {
    int status = 0;
    std::string why;
};
Delivered relay_deliver(Device& d, const std::string& frame, uint32_t now_ms);

// Compares in time that does not depend on where the strings differ.
bool same_secret(const std::string& a, const std::string& b);

}  // namespace nestbox
