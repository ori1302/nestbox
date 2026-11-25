#include "net.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_mac.h>
#include <mdns.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "console_reply.h"
#include "board.h"
#include "door/api.h"
#include "door/setup.h"
#include "host/protocol.h"
#include "log.h"
#include "util/text.h"

namespace net {

namespace {
constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t RETRY_MS = 20000;         // on top of the Wi-Fi driver's own reconnects
constexpr uint32_t MDNS_SETTLE_MS = 2000;    // after claiming a name, before reading back the one mDNS holds
constexpr int RECV_TIMEOUT_RETRIES = 3;
constexpr size_t MAX_HEADER = 256;
constexpr size_t MAX_HOST = 63;              // one DNS label
constexpr size_t MAX_INSTANCE = 63;          // a DNS-SD instance name
constexpr const char* MDNS_SUFFIX = ".local";

Handler handler;
std::string network_ssid, network_pass;
std::string wanted_host, held_host;          // the name asked for, and the one mDNS holds
httpd_handle_t server = nullptr;
bool mdns_up = false;
std::string pet_name;
uint32_t tried_ms = 0;
uint32_t settle_ms = 0;
bool held = false;                           // setup is using the radio
// Failed joins since the station last had an address, and the last one's reason (the Wi-Fi event task writes them).
std::atomic<uint32_t> failed_joins{0};
std::atomic<uint8_t> failed_reason{0};

std::string status_line(int status) {
    const char* reason = nestbox::reason_phrase(status);
    return reason ? std::to_string(status) + " " + reason : "500 Internal Server Error";
}

const char* method_name(int m) {
    switch (m) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_PATCH: return "PATCH";
        case HTTP_OPTIONS: return "OPTIONS";
        default: return "OTHER";
    }
}

std::string header(httpd_req_t* req, const char* name) {
    std::string v;
    const size_t n = httpd_req_get_hdr_value_len(req, name);
    if (n && n < MAX_HEADER) {
        v.resize(n + 1);
        if (httpd_req_get_hdr_value_str(req, name, &v[0], n + 1) == ESP_OK) v.resize(n);
        else v.clear();
    }
    return v;
}

bool read_body(httpd_req_t* req, std::string& body) {
    body.assign(req->content_len, '\0');
    for (size_t got = 0, timeouts = 0; got < body.size();) {
        const int n = httpd_req_recv(req, &body[got], body.size() - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < RECV_TIMEOUT_RETRIES) continue;
        if (n <= 0) return false;
        got += size_t(n);
    }
    return true;
}

// The client's IP address, as text; "" if it can't be read.
std::string peer_of(httpd_req_t* req) {
    sockaddr_in6 a{};
    socklen_t len = sizeof a;
    if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&a), &len) != 0) return "";
    char name[INET6_ADDRSTRLEN] = "";
    if (a.sin6_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&a)->sin_addr, name, sizeof name);
    } else {
        inet_ntop(AF_INET6, &a.sin6_addr, name, sizeof name);
    }
    return name;
}

// Whether the request came in on the access point's own address, so from a phone on the setup network.
bool on_setup_network(httpd_req_t* req) {
    sockaddr_in6 a{};
    socklen_t len = sizeof a;
    if (getsockname(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&a), &len) != 0) return false;
    char name[INET6_ADDRSTRLEN] = "";
    if (a.sin6_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&a)->sin_addr, name, sizeof name);
    } else if (IN6_IS_ADDR_V4MAPPED(&a.sin6_addr)) {
        inet_ntop(AF_INET, &a.sin6_addr.s6_addr[12], name, sizeof name);
    }
    return std::strcmp(name, nestbox::Setup::AP_IP) == 0;
}

// Whether PSRAM can take an egg of `len` bytes. It is read whole (one block), copied once while it is checked (a
// second block, kept as the pet), and PSRAM_SPARE must stay free beside them. Tried with real allocations: after
// pets have come and gone, the room is often there only in separate blocks.
bool egg_fits(size_t len) {
    void* body = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    void* copy = body ? heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : nullptr;
    const bool ok = copy && heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= board::PSRAM_SPARE;
    heap_caps_free(copy);
    heap_caps_free(body);
    return ok;
}

// Every method and path goes to the shared API.
esp_err_t on_request(httpd_req_t* req) {
    nestbox::Response r;
    const std::string method = method_name(req->method);
    const size_t limit = nestbox::body_limit(method, req->uri);
    const bool egg = limit > nestbox::MAX_BODY;
    if (req->content_len > limit) {
        r = nestbox::too_large_reply(limit);
    } else if (egg && !egg_fits(req->content_len)) {
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        nestbox::set_error(r, 507, "not enough memory for this pet: two blocks of " +
                                       std::to_string(req->content_len / 1024) + " KB needed, the largest free is " +
                                       std::to_string(largest / 1024) + " KB; take another pet off first");
    } else {
        std::string body;
        if (!read_body(req, body)) return ESP_FAIL;
        if (egg) heap_caps_malloc_extmem_enable(1);   // its many small tables go to PSRAM too
        r = handler(method, req->uri, body, header(req, "Host"), header(req, "Origin"), header(req, "Authorization"),
                    peer_of(req), on_setup_network(req));
        if (egg) heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    }
    const std::string status = status_line(r.status);   // must outlive httpd_resp_send
    httpd_resp_set_status(req, status.c_str());
    httpd_resp_set_type(req, r.type.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!r.location.empty()) httpd_resp_set_hdr(req, "Location", r.location.c_str());
    return httpd_resp_send(req, r.body.data(), r.body.size());
}

void start_server() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = HTTP_PORT;
    cfg.core_id = 0;
    cfg.stack_size = 12 * 1024;              // the API builds its replies in std::strings
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;             // a companion that never closes its socket can't lock others out
    cfg.max_open_sockets = 10;               // phones open several at once while checking for a captive portal
    if (httpd_start(&server, &cfg) != ESP_OK) {
        note("http: cannot start the server\n");
        return;
    }
    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_PATCH, HTTP_OPTIONS};
    for (httpd_method_t m : methods) {
        httpd_uri_t u{};
        u.uri = "/*";
        u.method = m;
        u.handler = on_request;
        httpd_register_uri_handler(server, &u);
    }
}

void connect(uint32_t now) {
    tried_ms = now;
    WiFi.disconnect();
    if (network_ssid.empty()) return;
    // An IPv6 address too, so mDNS answers a computer's IPv6 question for the name: unanswered, a Mac waits 5 s on it
    // before every request by name.
    WiFi.enableIPv6();
    WiFi.begin(network_ssid.c_str(), network_pass.empty() ? nullptr : network_pass.c_str());
}

void publish() {
    if (!mdns_up) return;
    const std::string instance = pet_name + " (" + held_host + ")";
    mdns_service_instance_name_set(nestbox::SERVICE_TYPE, nestbox::SERVICE_PROTOCOL,
                                   nestbox::utf8_cut(instance, MAX_INSTANCE).c_str());
    for (const auto& [key, value] : nestbox::bonjour_txt(pet_name, device_id(), held_host + MDNS_SUFFIX))
        mdns_service_txt_item_set(nestbox::SERVICE_TYPE, nestbox::SERVICE_PROTOCOL, key.c_str(), value.c_str());
}

void start_mdns() {
    if (mdns_init() != ESP_OK || mdns_hostname_set(wanted_host.c_str()) != ESP_OK) {
        note("mdns: cannot start\n");
        return;
    }
    held_host = wanted_host;
    settle_ms = millis();
    mdns_service_add(nullptr, nestbox::SERVICE_TYPE, nestbox::SERVICE_PROTOCOL, HTTP_PORT, nullptr, 0);
    mdns_up = true;
    publish();
}

// mDNS moves to the next free name when another device holds ours.
void follow_name_change(uint32_t now) {
    char in_use[MDNS_NAME_BUF_LEN] = {};
    if (now - settle_ms <= MDNS_SETTLE_MS || mdns_hostname_get(in_use) != ESP_OK || !in_use[0] || held_host == in_use)
        return;
    note("mdns: %s.local is taken; this pet is %s.local\n", held_host.c_str(), in_use);
    held_host = in_use;
    publish();
}
}  // namespace

std::string device_id() {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char id[7];
    snprintf(id, sizeof id, "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return id;
}

std::string default_host() { return "nestbox-" + device_id().substr(2); }

bool valid_host(const std::string& n) {
    if (n.empty() || n.size() > MAX_HOST || n.front() == '-' || n.back() == '-') return false;
    for (char c : n)
        if (!(islower(static_cast<unsigned char>(c)) || isdigit(static_cast<unsigned char>(c)) || c == '-'))
            return false;
    return true;
}

void begin(const std::string& host, const std::string& ssid, const std::string& pass, Handler h) {
    handler = std::move(h);
    wanted_host = held_host = host;
    network_ssid = ssid;
    network_pass = pass;
    WiFi.persistent(false);                  // credentials live in the firmware's own settings only
    WiFi.setHostname(wanted_host.c_str());     // the name the router learns over DHCP
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                    // modem sleep adds 100+ ms to every request
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        if (reason == WIFI_REASON_ASSOC_LEAVE) return;          // its own disconnect before a retry
        failed_reason = reason;
        failed_joins++;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { failed_joins = 0; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    start_server();
    if (!network_ssid.empty()) connect(millis());
}

void join(const std::string& ssid, const std::string& pass) {
    network_ssid = ssid;
    network_pass = pass;
    connect(millis());
}

void rename(const std::string& host) {
    wanted_host = host;
    WiFi.setHostname(wanted_host.c_str());
    if (!mdns_up) {
        held_host = wanted_host;
        return;
    }
    if (mdns_hostname_set(wanted_host.c_str()) != ESP_OK) {
        note("mdns: cannot take %s.local; keeping %s.local\n", wanted_host.c_str(), held_host.c_str());
        return;
    }
    held_host = wanted_host;
    settle_ms = millis();
    publish();
}

void adopt(const std::string& ssid, const std::string& pass) {
    network_ssid = ssid;
    network_pass = pass;
    tried_ms = millis();
    WiFi.setAutoReconnect(true);
}

void hold(bool hold, uint32_t now) {
    held = hold;
    if (hold) {
        WiFi.setAutoReconnect(false);
        if (!online()) WiFi.disconnect(false);   // the station only; setup's own network stays up
    } else {
        WiFi.setAutoReconnect(true);
        tried_ms = now - RETRY_MS;           // try again on the next tick, if still offline
    }
}

void tick(uint32_t now) {
    if (online()) {
        if (!mdns_up) start_mdns();          // from here the responder follows the link by itself
        if (mdns_up) follow_name_change(now);
        return;
    }
    if (!held && !network_ssid.empty() && now - tried_ms > RETRY_MS) connect(now);
}

bool online() { return WiFi.status() == WL_CONNECTED; }
std::string host() { return held_host; }
std::string ip() { return WiFi.localIP().toString().c_str(); }

std::string status() {
    if (network_ssid.empty()) return "no network set: wifi <ssid> <password>";
    if (!online()) {
        std::string line = console_reply::JOINING + network_ssid + "\"...";
        const uint32_t failed = failed_joins;
        if (failed)   // after the reply's prefix, which the Mac app reads
            line += " (" + std::to_string(failed) + (failed == 1 ? " try" : " tries") + " failed, the last: " +
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(uint8_t(failed_reason))) + ")";
        return line;
    }
    return console_reply::ONLINE + network_ssid + console_reply::ADDRESS + ip() + console_reply::HOST_URL + held_host +
           MDNS_SUFFIX + "), signal " + std::to_string(WiFi.RSSI()) + " dBm";
}

void announce(const std::string& pet) {
    pet_name = pet;
    publish();
}

}  // namespace net
