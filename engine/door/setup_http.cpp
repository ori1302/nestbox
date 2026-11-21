// The setup's HTTP routes: the setup page and its API, the captive portal, and the handover page.
#include <algorithm>
#include <iterator>
#include "door/page.h"
#include "door/pet_svg.h"
#include "door/setup.h"
#include "door/setup_parts.h"
#include "util/deadline.h"
#include "util/json.h"
#include "util/text.h"

namespace nestbox {

namespace {

// A 1x1 transparent PNG. The handover page loads it to detect when the phone can reach the pet on the home network.
const unsigned char PING_PNG[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};

// Paths phones request to detect internet access. A redirect makes them open the setup page (iPhone) or offer
// "Sign in to network" (Android, Windows).
const char* const PROBES[] = {"/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
                              "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt"};

constexpr size_t SSID_MAX = 32, PET_NAME_MAX = 48;
constexpr size_t PASSPHRASE_MIN = 8, PASSPHRASE_MAX = 63, HEX_KEY_LEN = 64;   // WPA2: a passphrase, or the key in hex


// A host name a phone may use that isn't the pet: its internet check, or a typed address.
bool foreign(const std::string& host) {
    const std::string name = host_name(host);
    return !name.empty() && !is_literal_host(name) && !is_mdns_name(name);
}

bool valid_password(const std::string& p) {
    if (p.size() == HEX_KEY_LEN) return p.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    return p.size() >= PASSPHRASE_MIN && p.size() <= PASSPHRASE_MAX;
}

std::string ok_json() { return JsonWriter().begin_object().field("ok", true).end_object().text(); }

}  // namespace

bool Setup::handle(const std::string& method, const std::string& path_q, const std::string& body,
                   const std::string& host, uint32_t now, const Egg* pet, Response& r, bool setup_network) {
    const std::string path = path_only(path_q);
    if (path == "/ping.png") {
        r.type = "image/png";
        r.body.assign(reinterpret_cast<const char*>(PING_PNG), sizeof PING_PNG);
        return true;
    }
    if (path == "/welcome" && method == "GET") welcome_visit(path_q);   // then the API serves the page
    if (!ap_open_ || !setup_network) return false;
    if (redirect_probe(path, host, r)) return true;
    if (path == "/" && method == "GET" && active()) {
        page_seen_ = now;
        r.type = "text/html; charset=utf-8";
        r.body = setup_page();
        return true;
    }
    if (path.compare(0, 7, "/setup/") != 0) return false;
    page_seen_ = now;
    serve_setup_api(method, path, body, now, pet, r);
    return true;
}

// The handover lands here from the home network; the token from the screen's QR code closes "All set". The page itself
// is the device's own (door/page.h), served by the API.
void Setup::welcome_visit(const std::string& path_q) {
    if (phase_ == Phase::AllSet && !token_.empty() && query_value(path_q, "t") == token_) phase_ = Phase::Off;
}

bool Setup::redirect_probe(const std::string& path, const std::string& host, Response& r) const {
    const bool probe = std::any_of(std::begin(PROBES), std::end(PROBES), [&](const char* p) { return path == p; });
    if (!probe && !foreign(host)) return false;
    r.status = 302;
    r.location = std::string("http://") + AP_IP + "/";
    r.type = "text/html";
    r.body = std::string("<a href=\"http://") + AP_IP + "/\">Set up " + html_escape(pet_) + "</a>";
    return true;
}

void Setup::serve_setup_api(const std::string& method, const std::string& path, const std::string& body, uint32_t now,
                            const Egg* pet, Response& r) {
    if (method == "GET") {
        if (path == "/setup/status") { r.body = status_json(now); return; }
        if (path == "/setup/pet.svg") { r.type = "image/svg+xml"; r.body = pet_svg(pet); return; }
        if (path == "/setup/networks") { r.body = networks_json(); return; }
    }
    if (method != "POST") return set_error(r, 405, "use POST");
    Fields f;
    if (!parse_json(body, f)) return set_error(r, 400, "send JSON");
    if (path == "/setup/scan") {
        if (!scanning_) { radio_->scan(); scanning_ = true; }
        r.body = ok_json();
    } else if (path == "/setup/connect") {
        connect(f, now, r);
    } else if (path == "/setup/finish") {
        finish(f, now, r);
    } else if (path == "/setup/offline") {
        go_offline(f, now, r);
    } else {
        set_error(r, 404, "not found");
    }
}

std::string Setup::networks_json() const {
    JsonWriter w;
    w.begin_object().field("pet", pet_).field("scanning", scanning_).key("networks").begin_array();
    for (const WifiNet& n : nets_)
        w.begin_object().field("ssid", n.ssid).field("bars", signal_bars(n.rssi)).field("secure", n.secure)
            .field("enterprise", n.enterprise).end_object();
    return w.end_array().end_object().text();
}

void Setup::connect(const Fields& f, uint32_t now, Response& r) {
    if (!active() || phase_ == Phase::AllSet) return set_error(r, 409, "already set up");
    if (phase_ == Phase::Checking) return set_error(r, 409, "still checking " + want_ssid_);
    const std::string ssid = f.str("ssid"), pass = f.str("password");
    if (ssid.empty() || ssid.size() > SSID_MAX) return set_error(r, 400, "a network name has 1 to 32 characters");
    if (!pass.empty() && !valid_password(pass))
        return set_error(r, 400, "Wi-Fi passwords have 8 to 63 characters, or are 64 hex digits");
    clock_from(f);
    want_ssid_ = ssid;
    const WifiNet* n = seen(ssid);
    since_ = now;
    if (n && n->enterprise) {
        result_ = Join::NeedsLogin;
        phase_ = Phase::Failed;
    } else {
        want_pass_ = pass;
        radio_->join(ssid, pass, n ? n->channel : 0);
        result_ = Join::Trying;
        phase_ = Phase::Checking;
    }
    r.body = JsonWriter().begin_object().field("ok", true).field("checking", ssid).end_object().text();
}

void Setup::finish(const Fields& f, uint32_t now, Response& r) {
    if (!ok_at_) return set_error(r, 409, "not on a Wi-Fi yet");
    const std::string name = trim(f.str("name"));
    if (!name.empty() && name.size() <= PET_NAME_MAX) {   // kept even when it is the one suggested: the device's name
        pet_ = name;
        radio_->save(SAVE_NAME, name);
    }
    close_at_ = deadline(now, CLOSE_SOON_MS);              // the phone then falls back to the home network
    r.body = JsonWriter()
                 .begin_object()
                 .field("ok", true)
                 .field("url", welcome_url())
                 .field("ping", "http://" + ip_ + "/ping.png")
                 .end_object()
                 .text();
}

void Setup::go_offline(const Fields& f, uint32_t now, Response& r) {
    if (!active() || phase_ == Phase::AllSet) return set_error(r, 409, "already set up");
    clock_from(f);
    offline_ = true;
    radio_->forget();                                       // the old network, when setup runs again
    saved_.clear();
    radio_->save(SAVE_OFFLINE, "1");
    phase_ = Phase::Off;
    close_at_ = deadline(now, CLOSE_SOON_MS);
    r.body = ok_json();
}

}  // namespace nestbox
