// Webhooks through the relay, for the simulator: the same link as the board's (engine/host/relay.h), with the
// WebSocket and TLS from Apple's Network framework. NESTBOX_RELAY (e.g. http://127.0.0.1:8787, a local relay for
// tests) replaces https://hooks.nestbox.pet.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "door/api.h"
#include "host/relay.h"

namespace nestbox {

class SimRelay {
public:
    // What the link needs from the device, each called under the device's lock by the device.
    struct Hooks {
        std::function<uint32_t()> now;
        std::function<std::optional<RelaySettings>()> changed;   // the settings, once after each change
        std::function<void(const char*)> state;                  // the link's word for GET /v1
        std::function<Delivered(const std::string&)> deliver;    // a frame from the relay
    };
    explicit SimRelay(Hooks hooks);
    ~SimRelay();
    SimRelay(const SimRelay&) = delete;
    SimRelay& operator=(const SimRelay&) = delete;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;   // shared with the connection's callbacks, which may outlive this
};

}  // namespace nestbox
