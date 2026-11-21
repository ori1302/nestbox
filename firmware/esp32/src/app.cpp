#include "app.h"
#include <Arduino.h>
#include "host/protocol.h"
#include "log.h"
#include "wall_clock.h"

nestbox::Response App::request(const std::string& method, const std::string& path, const std::string& body,
                               bool local, const std::string& host, const std::string& origin,
                               const std::string& auth, const std::string& peer, bool setup_network) {
    return with_lock([&](PetCore& c) {
        if (!c.shelf.size()) {
            nestbox::Response r;
            nestbox::set_error(r, 503, "no pets on the device: flash the data image (pio run -t uploadfs)");
            return r;
        }
        return nestbox::handle_request(c.dev, method, path, body, millis(), host, origin, local, auth, peer,
                                       setup_network);
    });
}

void App::post_input(const std::string& json) {
    const nestbox::Response r = request("POST", nestbox::INPUT_PATH, json, true);
    note("%s -> %s\n", json.c_str(), r.body.c_str());
}

void App::update_clock() {
    with_lock([](PetCore& c) {
        int64_t local;
        if (wall_clock::local(local)) c.brain.clock(local, millis());
    });
}

bool App::has_pets() {
    return with_lock([](PetCore& c) { return c.shelf.size() > 0; });
}
