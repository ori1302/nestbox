#include "relay.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <mbedtls/platform.h>
#include <string>
#include "app.h"
#include "door/http.h"
#include "host/relay.h"
#include "log.h"

namespace {
constexpr uint32_t STACK = 8192;
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t IO_TIMEOUT_MS = 5000;
constexpr int POLL_MS = 200;
// The pet takes at most 1 KB a tell (door/api.h), so a frame much bigger than that is read through and dropped
// rather than kept: the relay passes up to 16 KB from anyone who has the URL.
constexpr int MAX_FRAME = 4096;

// TLS keeps a 16 KB buffer each way per connection; in internal RAM that leaves Wi-Fi and the tasks' stacks too
// little (measured on the LCD-2: 83 KB free fell to 18 KB at worst), so mbedTLS allocates from PSRAM.
void* tls_calloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

ws_transport_opcodes_t final(ws_transport_opcodes_t op) { return ws_transport_opcodes_t(op | WS_TRANSPORT_OPCODES_FIN); }

class Relay {
public:
    explicit Relay(App& app) : app_(app) {
        esp_transport_handle_t ssl = esp_transport_ssl_init();
        esp_transport_ssl_crt_bundle_attach(ssl, esp_crt_bundle_attach);
        ws_ = esp_transport_ws_init(ssl);
    }

    void run() {
        for (;;) {
            const uint32_t now = millis();
            follow_settings(now);
            if (link_.should_connect(now) && WiFi.status() == WL_CONNECTED) connect(now);
            else vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

private:
    App& app_;
    nestbox::RelayLink link_;
    nestbox::RelaySettings keys_;
    const char* shown_ = "";
    esp_transport_handle_t ws_ = nullptr;   // over TLS; made once and reused for every connection
    bool stack_noted_ = false, configured_ = false;

    void follow_settings(uint32_t now) {
        const bool changed = app_.with_lock([&](PetCore& c) {
            if (!c.dev.relay_changed && configured_) return false;
            configured_ = true;
            c.dev.relay_changed = false;
            keys_ = c.dev.settings.relay;
            return true;
        });
        if (changed) link_.configure(keys_, now);
        publish();
    }

    // The link's state for GET /v1, and a line in the log when it changes.
    void publish() {
        const char* word = link_.state_word();
        if (word == shown_) return;
        shown_ = word;
        app_.with_lock([&](PetCore& c) { c.dev.relay_state = word; });
        note("relay: %s\n", word);
    }

    bool settings_changed() {
        return app_.with_lock([](PetCore& c) { return c.dev.relay_changed; });
    }

    void connect(uint32_t now) {
        const std::string headers = nestbox::relay_headers(keys_);
        esp_transport_ws_config_t config = {};
        config.ws_path = nestbox::RELAY_CONNECT_PATH;
        config.headers = headers.c_str();
        config.propagate_control_frames = true;   // to read the close code: 4000 means another connection took over
        esp_transport_ws_set_config(ws_, &config);

        link_.connecting(now);
        publish();
        if (esp_transport_connect(ws_, nestbox::RELAY_HOST, 443, CONNECT_TIMEOUT_MS) < 0) {
            // The transport keeps the last upgrade's status even when this attempt failed before getting one, so a
            // 401 here may be an older refusal; the link then only waits an hour before trying again.
            const int status = esp_transport_ws_get_upgrade_request_status(ws_);
            link_.closed(millis(), status > 0 ? status : 0);
        } else {
            link_.connected(millis());
            publish();
            link_.closed(millis(), serve());
        }
        publish();
        esp_transport_close(ws_);
    }

    // Sends one whole frame. The transport masks a frame in place (its const char* notwithstanding: transport_ws.c
    // casts the const away), so it is always given a copy of its own: a constant in flash would crash the chip.
    int send(ws_transport_opcodes_t opcode, const char* data, size_t n) {
        std::string frame(data, n);
        return esp_transport_ws_send_raw(ws_, final(opcode), frame.data(), int(frame.size()), IO_TIMEOUT_MS);
    }

    // Reads frames until the connection ends; returns its close code (0 for a network failure or a stop).
    int serve() {
        std::string frame;
        char buf[512];
        for (;;) {
            const uint32_t now = millis();
            if (settings_changed() || WiFi.status() != WL_CONNECTED || link_.silent(now)) return 0;
            if (link_.should_ping(now) &&
                send(WS_TRANSPORT_OPCODES_TEXT, nestbox::RELAY_PING, strlen(nestbox::RELAY_PING)) < 0)
                return 0;
            const int ready = esp_transport_poll_read(ws_, POLL_MS);
            if (ready < 0) return 0;
            if (ready == 0) continue;
            const int n = esp_transport_read(ws_, buf, sizeof buf, IO_TIMEOUT_MS);
            if (n < 0) return 0;
            if (n == 0) continue;   // no frame came after all; the opcode and length would be the last frame's
            link_.heard(millis());
            const ws_transport_opcodes_t op = esp_transport_ws_get_read_opcode(ws_);
            const int length = esp_transport_ws_get_read_payload_len(ws_);
            if (op == WS_TRANSPORT_OPCODES_CLOSE) return n >= 2 ? (uint8_t(buf[0]) << 8) | uint8_t(buf[1]) : 0;
            if (op == WS_TRANSPORT_OPCODES_PING) {
                send(WS_TRANSPORT_OPCODES_PONG, buf, size_t(n));
                continue;
            }
            const bool keep = op == WS_TRANSPORT_OPCODES_TEXT && length <= MAX_FRAME;
            if (keep) frame.assign(buf, size_t(n));
            for (int got = n; got < length;) {   // the rest of a long frame
                const int more = esp_transport_read(ws_, buf, sizeof buf, IO_TIMEOUT_MS);
                if (more <= 0) return 0;
                got += more;
                if (keep) frame.append(buf, size_t(more));
            }
            if (!keep) {
                if (op == WS_TRANSPORT_OPCODES_TEXT) note("relay: webhook dropped: %d bytes, too long\n", length);
                continue;
            }
            if (frame != "pong") deliver(frame);
        }
    }

    // The secret is checked and the frame parsed outside the lock; only the tell takes it.
    void deliver(const std::string& frame) {
        std::string secret, key;
        app_.with_lock([&](PetCore& c) {
            secret = c.dev.settings.relay.secret;
            key = c.dev.settings.key;
        });
        std::string why;
        const std::string body = nestbox::relay_body(frame, secret, why);
        if (body.empty()) {
            note("relay: webhook dropped: %s\n", why.c_str());
            return;
        }
        const std::string auth = key.empty() ? "" : "Bearer " + key;
        const int status = app_.request("POST", nestbox::TELL_PATH, body, false, "", "", auth, "relay").status;
        note("relay: webhook -> %d\n", status);
        if (!stack_noted_) {
            stack_noted_ = true;
            note("relay: %u bytes of stack never used\n", unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        }
    }
};

void relay_task(void* arg) {
    Relay relay(*static_cast<App*>(arg));
    relay.run();
}
}  // namespace

void follow_relay(App* app) {
    static bool started = false;
    if (started || !app->with_lock([](PetCore& c) { return !c.dev.settings.relay.mailbox.empty(); })) return;
    started = true;
    mbedtls_platform_set_calloc_free(tls_calloc, heap_caps_free);
    if (xTaskCreatePinnedToCore(relay_task, "relay", STACK, app, 1, nullptr, 0) != pdPASS)
        note("relay: cannot start the task (%u bytes of stack)\n", unsigned(STACK));
}
