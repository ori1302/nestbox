#include "relay_link.h"
#include <Network/Network.h>
#include <cstdlib>
#include <cstring>
#include "log.h"

namespace nestbox {

namespace {
constexpr int64_t TICK_NS = 250 * NSEC_PER_MSEC;

// The relay's /connect as a WebSocket URL: http becomes ws and https wss.
std::string connect_url() {
    const char* env = std::getenv("NESTBOX_RELAY");
    std::string base = env && *env ? env : std::string("https://") + RELAY_HOST;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.rfind("https://", 0) == 0) base = "wss://" + base.substr(8);
    else if (base.rfind("http://", 0) == 0) base = "ws://" + base.substr(7);
    return base + RELAY_CONNECT_PATH;
}
}  // namespace

// Everything runs on one serial queue: the tick, and the connection's callbacks. The Network framework reports a
// refused upgrade (a key the relay doesn't know) only as an aborted connection, without its HTTP status, so here that
// is retried with the link's backoff rather than ending as "refused" as on the board.
struct SimRelay::Impl : std::enable_shared_from_this<SimRelay::Impl> {
    explicit Impl(Hooks h) : hooks(std::move(h)) {}
    ~Impl() {
        if (timer) dispatch_release(timer);
        dispatch_release(queue);
    }
    Hooks hooks;
    dispatch_queue_t queue = dispatch_queue_create("pet.nestbox.relay", DISPATCH_QUEUE_SERIAL);
    dispatch_source_t timer = nullptr;
    RelayLink link;
    RelaySettings keys;
    nw_connection_t conn = nullptr;
    const char* shown = "";
    std::string url = connect_url();

    void tick() {
        const uint32_t now = hooks.now();
        if (const std::optional<RelaySettings> k = hooks.changed()) {
            keys = *k;
            drop(0);
            link.configure(keys, now);
        }
        if (conn) {
            if (link.silent(now)) drop(0);
            else if (link.should_ping(now)) send(RELAY_PING);
        } else if (link.should_connect(now)) {
            open(now);
        }
        publish();
    }

    void publish() {
        const char* word = link.state_word();
        if (word == shown) return;
        shown = word;
        hooks.state(word);
        note("relay: %s\n", word);
    }

    void open(uint32_t now) {
        const bool tls = url.rfind("wss://", 0) == 0;
        nw_endpoint_t endpoint = nw_endpoint_create_url(url.c_str());
        nw_parameters_t params = nw_parameters_create_secure_tcp(
            tls ? NW_PARAMETERS_DEFAULT_CONFIGURATION : NW_PARAMETERS_DISABLE_PROTOCOL,
            NW_PARAMETERS_DEFAULT_CONFIGURATION);
        nw_protocol_options_t ws = nw_ws_create_options(nw_ws_version_13);
        nw_ws_options_add_additional_header(ws, "X-NestBox-Mailbox", keys.mailbox.c_str());
        nw_protocol_stack_t stack = nw_parameters_copy_default_protocol_stack(params);
        nw_protocol_stack_prepend_application_protocol(stack, ws);
        nw_connection_t c = nw_connection_create(endpoint, params);
        nw_release(stack);
        nw_release(ws);
        nw_release(params);
        nw_release(endpoint);
        conn = c;
        link.connecting(now);
        nw_connection_set_queue(c, queue);
        std::shared_ptr<Impl> self = shared_from_this();   // the blocks keep it alive until the connection is gone
        nw_connection_set_state_changed_handler(c, ^(nw_connection_state_t state, nw_error_t) {
            if (c != self->conn) return;
            if (state == nw_connection_state_ready) {
                self->link.connected(self->hooks.now());
                self->publish();
                self->receive(c);
            } else if (state == nw_connection_state_failed || state == nw_connection_state_waiting) {
                self->drop(0);
                self->publish();
            }
        });
        nw_connection_start(c);
    }

    void receive(nw_connection_t c) {
        std::shared_ptr<Impl> self = shared_from_this();
        nw_connection_receive_message(c, ^(dispatch_data_t content, nw_content_context_t context, bool, nw_error_t error) {
            if (c != self->conn) return;
            nw_protocol_definition_t def = nw_protocol_copy_ws_definition();
            nw_protocol_metadata_t meta = context ? nw_content_context_copy_protocol_metadata(context, def) : nullptr;
            nw_release(def);
            const nw_ws_opcode_t op = meta ? nw_ws_metadata_get_opcode(meta) : nw_ws_opcode_invalid;
            const int close_code = op == nw_ws_opcode_close ? int(nw_ws_metadata_get_close_code(meta)) : 0;
            if (meta) nw_release(meta);
            if (error || op == nw_ws_opcode_close) {
                self->drop(close_code);
                self->publish();
                return;
            }
            self->link.heard(self->hooks.now());
            if (op == nw_ws_opcode_text && content) {
                __block std::string frame;
                dispatch_data_apply(content, ^bool(dispatch_data_t, size_t, const void* bytes, size_t size) {
                    frame.append(static_cast<const char*>(bytes), size);
                    return true;
                });
                if (frame != "pong") {
                    const Delivered d = self->hooks.deliver(frame);
                    if (!d.why.empty()) note("relay: webhook dropped: %s\n", d.why.c_str());
                    else note("relay: webhook -> %d\n", d.status);
                }
            }
            self->receive(c);
        });
    }

    void send(const char* text) {
        nw_protocol_metadata_t meta = nw_ws_create_metadata(nw_ws_opcode_text);
        nw_content_context_t context = nw_content_context_create("ping");
        nw_content_context_set_metadata_for_protocol(context, meta);
        dispatch_data_t data = dispatch_data_create(text, strlen(text), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        nw_connection_send(conn, data, context, true, ^(nw_error_t) {});
        dispatch_release(data);
        nw_release(context);
        nw_release(meta);
    }

    // Ends the connection, if any, and tells the link why (`code`: a close code or HTTP status, or 0).
    void drop(int code) {
        if (!conn) return;
        nw_connection_t c = conn;
        conn = nullptr;
        nw_connection_cancel(c);
        nw_release(c);
        link.closed(hooks.now(), code);
    }
};

SimRelay::SimRelay(Hooks hooks) : impl_(std::make_shared<Impl>(std::move(hooks))) {
    Impl* impl = impl_.get();
    impl->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, impl->queue);
    dispatch_source_set_timer(impl->timer, dispatch_time(DISPATCH_TIME_NOW, 0), TICK_NS, TICK_NS / 4);
    dispatch_source_set_event_handler(impl->timer, ^{ impl->tick(); });
    dispatch_resume(impl->timer);
}

SimRelay::~SimRelay() {
    Impl* impl = impl_.get();
    dispatch_sync(impl->queue, ^{
        dispatch_source_cancel(impl->timer);
        impl->drop(0);
        impl->hooks = {};   // a callback still to come finds nothing to call into the device with
    });
}

}  // namespace nestbox
