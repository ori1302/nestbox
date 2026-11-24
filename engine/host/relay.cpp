#include "host/relay.h"
#include <algorithm>
#include "door/http.h"
#include "util/json.h"

namespace nestbox {

namespace {
constexpr int CLOSE_REPLACED = 4000;    // the relay: a newer connection took this mailbox
constexpr int CLOSE_REMOVED = 4001;     // the relay: the mailbox was removed
constexpr int CLOSE_ACCESS_ENDED = 4003;   // the relay: the access key was disabled
constexpr int HTTP_UNAUTHORIZED = 401;

bool configured(const RelaySettings& r) { return r.on && !r.mailbox.empty() && !r.secret.empty(); }
bool due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }
}  // namespace

std::string relay_headers(const RelaySettings& r) {
    return "X-NestBox-Mailbox: " + r.mailbox + "\r\n";
}

void RelayLink::configure(const RelaySettings& r, uint32_t now_ms) {
    state_ = configured(r) ? State::Waiting : State::Off;
    retry_at_ = now_ms;
    retry_ms_ = FIRST_RETRY_MS;
}

bool RelayLink::should_connect(uint32_t now_ms) const {
    return (state_ == State::Waiting || state_ == State::Refused) && due(now_ms, retry_at_);
}

void RelayLink::connecting(uint32_t) { state_ = State::Connecting; }

void RelayLink::connected(uint32_t now_ms) {
    state_ = State::Connected;
    retry_ms_ = FIRST_RETRY_MS;
    pinged_at_ = heard_at_ = now_ms;
}

void RelayLink::closed(uint32_t now_ms, int code) {
    if (state_ == State::Off) return;
    if (code == HTTP_UNAUTHORIZED || code == CLOSE_REMOVED || code == CLOSE_ACCESS_ENDED) {
        state_ = State::Refused;
        retry_at_ = now_ms + REFUSED_RETRY_MS;
        return;
    }
    if (code == CLOSE_REPLACED) {
        state_ = State::Replaced;
        return;
    }
    state_ = State::Waiting;
    retry_at_ = now_ms + retry_ms_;
    retry_ms_ = std::min(retry_ms_ * 2, LAST_RETRY_MS);
}

void RelayLink::heard(uint32_t now_ms) { heard_at_ = now_ms; }

bool RelayLink::should_ping(uint32_t now_ms) {
    if (state_ != State::Connected || now_ms - pinged_at_ < PING_MS) return false;
    pinged_at_ = now_ms;
    return true;
}

bool RelayLink::silent(uint32_t now_ms) const { return state_ == State::Connected && now_ms - heard_at_ >= SILENT_MS; }

const char* RelayLink::state_word() const {
    switch (state_) {
        case State::Off: return "off";
        case State::Waiting: return "waiting";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Refused: return "refused";
        case State::Replaced: return "replaced";
    }
    return "off";
}

bool same_secret(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string relay_body(const std::string& frame, const std::string& secret, std::string& why) {
    Fields f;
    if (!parse_json(frame, f) || !f.has("body")) {
        why = "not a relay frame";
        return "";
    }
    if (secret.empty() || !f.has("headers.x-nestbox-secret") || !same_secret(f.str("headers.x-nestbox-secret"), secret)) {
        why = "wrong secret";
        return "";
    }
    std::string body = f.str("body");
    Fields tell;
    if (!parse_json(body, tell)) {
        why = "body not a JSON object";
        return "";
    }
    why.clear();
    return body;
}

Delivered relay_deliver(Device& d, const std::string& frame, uint32_t now_ms) {
    Delivered out;
    const std::string body = relay_body(frame, d.settings.relay.secret, out.why);
    if (body.empty()) return out;
    const std::string auth = d.settings.key.empty() ? "" : "Bearer " + d.settings.key;
    out.status = handle_request(d, "POST", TELL_PATH, body, now_ms, "", "", false, auth, "relay").status;
    return out;
}

}  // namespace nestbox
