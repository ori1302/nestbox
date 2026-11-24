// The pet's HTTP API, shared by the firmware and the simulator: platforms move bytes on a socket and pass each
// request to handle_request().
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include "body/pet.h"
#include "body/shelf.h"
#include "door/http.h"
#include "door/rest.h"
#include "mind/brain.h"

#ifndef NESTBOX_VERSION
#error "NESTBOX_VERSION is set by the build (CMakeLists.txt, or firmware/esp32/scripts/version.py)"
#endif

namespace nestbox {

class Setup;

// What the device keeps across restarts.
// Webhooks through the relay (host/relay.h): the keys the Mac app hands over, and whether to use them. The mailbox
// key alone lets the device connect; the user's access key for the relay stays on the Mac.
struct RelaySettings {
    std::string mailbox;         // this device's mailbox key; the webhook URL is derived from it
    std::string secret;          // every webhook must carry it in X-NestBox-Secret
    bool on = true;              // false: keys kept, socket closed, webhooks turned away
};

struct Settings {
    std::string key;             // the secret for keyed routes; "" until the first pairing
    bool locked = false;         // Lock: every /v1 route but pairing needs the key
    int text = 2;                // text size: 1 small, 2 medium, 3 large
    ScreenSettings screen;       // brightness and when the screen rests
    RelaySettings relay;
};

// What the platform knows of the board's health, for GET /v1 and the device's page; unknown values stay unset.
struct Health {
    std::optional<float> temperature_c;   // the chip's own sensor
    std::optional<int> wifi_dbm;          // the home network's signal
};

struct Device {
    Device(Brain& b, Pet& p, Shelf& s, Canvas& c, std::string name, std::string url = "http://nestbox.local",
           std::string device_id = "", Setup* wifi = nullptr)
        : brain(b), pet(p), shelf(s), screen(c), device_name(std::move(name)), base_url(std::move(url)),
          id(std::move(device_id)), setup(wifi) {}

    Brain& brain;
    Pet& pet;
    Shelf& shelf;                // pets on the device; switching cross-fades from `screen`
    Canvas& screen;
    std::string device_name;     // e.g. "nestbox-simulator"
    std::string base_url;        // how others reach it (shown in the help and guide)
    std::string id;              // permanent id, announced over Bonjour; a paired app finds its device by it
    Setup* setup = nullptr;      // Wi-Fi setup, if the platform has one
    std::string firmware = NESTBOX_VERSION;   // set by the build
    Settings settings;
    std::function<void(const Settings&)> save;     // keeps the settings; may be empty
    std::function<uint32_t()> random;              // random bits for a new key; without it the device can't pair

    // A pairing waiting for the button: who asked, from where, until when, and whether the user pressed it.
    struct Pairing {
        std::string name, peer;
        uint32_t until = 0;
        bool confirmed = false;
    } pairing;
    // A pet being added or taken off (PUT and DELETE /v1/pets/NAME). Without the key it waits for the pet's button.
    // Once it may go ahead, the platform writes the egg's file (or deletes it), then calls finish_pet_change().
    struct PetChange {
        enum class Kind { None, Add, Remove };
        Kind kind = Kind::None;
        std::string id;              // the pet's id, its file name without .egg
        std::string name;            // as the pet calls itself
        Egg egg;                     // Add: the new pet, checked and ready to show
        uint32_t until = 0;          // waiting for the button until then
        bool go = false;             // the key came with it, or the button was pressed
    } pet_change;
    std::string pet_change_done; // how the last change ended, for GET /v1/pets ("" none yet)
    uint32_t active_at = 0;      // the last activity (see door/rest.h); the screen rests counting from it
    ScreenState screen_state = ScreenState::Awake;   // set each frame by step_frame()
    Shift shift;                 // where the platform shows the picture (burn-in care), set with screen_state
    Health health;               // the platform keeps it current
    std::string relay_state;     // the relay link's word (host/relay.h), kept by the platform; "" if it has none
    bool relay_changed = false;  // the relay settings changed: the platform reconfigures its link and clears this
    uint32_t restart_at = 0;     // a restart was asked for: the platform restarts at this time (0: none)
    uint32_t rate_second = 0;    // the rate limit's current second, and the requests in it
    int rate_count = 0;
};

// `host` and `origin` are the request's headers ("" if unknown): requests for another host name are refused (DNS
// rebinding), as are writes from another site's page. `local` marks requests from the device itself (its button
// and sensors, the USB console, or the simulator's own Mac): only they may send device input, and they need no key.
// `auth` is the Authorization header, `peer` the client's address ("" if unknown). `setup_network` is false for a
// request that did not come over the device's own setup access point: setup's pages, which need no key, are not
// served to it. The simulator has one network, so it leaves it true.
Response handle_request(Device& d, const std::string& method, const std::string& path, const std::string& body,
                        uint32_t now_ms, const std::string& host = "", const std::string& origin = "",
                        bool local = false, const std::string& auth = "", const std::string& peer = "",
                        bool setup_network = true);

// For the platform, after requests and frames: the change to a pet's file to carry out now (write `egg.bytes` to
// `<id>.egg`, or delete it), or nullptr. A change left waiting for the button too long is dropped here.
const Device::PetChange* pet_change_to_store(Device& d, uint32_t now_ms);
// The platform wrote (or deleted) the file; `error` is "" or why it couldn't. Puts the pet on the shelf (or takes
// it off) and ends the change.
void finish_pet_change(Device& d, const std::string& error, uint32_t now_ms);

// Something happened that someone would want to see: the screen wakes and its rest starts over.
void wake(Device& d, uint32_t now_ms);
// The screen's state now, from the time since the last activity, whether anyone is at the computer and the night
// hours; also kept in d.screen_state. Called once per frame.
ScreenState update_rest(Device& d, uint32_t now_ms);
// The backlight the platform should show now, in percent (0 when the screen is off).
int backlight_now(const Device& d);

// Request bodies over this many bytes are refused...
constexpr size_t MAX_BODY = 1024;
// ...but for a pet's egg sent to PUT /v1/pets/NAME, this many.
constexpr size_t MAX_EGG = 2 * 1024 * 1024;
// The most body bytes a platform should read for this request: MAX_EGG for an egg, else MAX_BODY.
size_t body_limit(const std::string& method, const std::string& path);
// Requests from the network per second; more are refused until the next second.
constexpr int MAX_REQUESTS_PER_S = 20;

}  // namespace nestbox
