// Webhooks through the relay (engine/host/relay.cpp and the "relay" setting in engine/door/api.cpp): setting and
// reporting the keys, when the link connects, retries and gives up, and which frames reach the pet.
// Usage: relay_test
#include "host/relay.h"
#include <string>
#include "check.h"
#include "util/json.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t S = 1000;
const std::string MAILBOX = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const std::string SECRET = "ccccccccccccccccccccccccccccccccccccccccccc";

struct Box {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen{280, 240};
    Device device{brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test"};
    Settings saved;

    Box() {
        device.random = [n = 0u]() mutable { return n += 2654435761u; };
        device.save = [this](const Settings& s) { saved = s; };
        device.relay_state = "off";
    }
    Response post(const std::string& path, const std::string& body, uint32_t now, const std::string& auth = "") {
        return handle_request(device, "POST", path, body, now, "nestbox-test.local", "", false, auth, "10.0.0.5");
    }
    std::string pair(uint32_t now) {
        post("/v1/pair", R"({"name":"Mac"})", now);
        handle_request(device, "POST", "/v1/input", R"({"press":"single"})", now + S, "", "", true);
        Fields f;
        parse_json(post("/v1/pair", R"({"name":"Mac"})", now + 2 * S).body, f);
        return "Bearer " + f.str("key");
    }
    std::string set_keys(uint32_t now) {
        const std::string key = pair(now);
        post("/v1", R"({"relay":{"mailbox":")" + MAILBOX + R"(","secret":")" + SECRET + R"("}})", now + 3 * S, key);
        return key;
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string frame(const std::string& secret, const std::string& body) {
    std::string escaped;
    for (char c : body) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }
    return R"({"headers":{"x-nestbox-secret":")" + secret + R"("},"body":")" + escaped + R"("})";
}

void setting_the_keys() {
    Box p;
    const std::string body = R"({"relay":{"mailbox":")" + MAILBOX + R"(","secret":")" + SECRET + R"("}})";
    check(p.post("/v1", body, S).status == 401, "the keys need the device's key");
    check(p.device.settings.relay.mailbox.empty(), "nothing kept without it");

    const std::string key = p.pair(2 * S);
    const Response r = p.post("/v1", body, 5 * S, key);
    check(r.status == 200 && contains(r.body, "\"warnings\": []"), "set with the key, without warnings: " + r.body);
    check(p.saved.relay.mailbox == MAILBOX && p.saved.relay.secret == SECRET && p.saved.relay.on,
          "saved, on");
    check(p.device.relay_changed, "the platform is told to reconfigure");

    const std::string now = handle_request(p.device, "GET", "/v1", "", 6 * S, "nestbox-test.local").body;
    check(contains(now, R"("relay": {"state": "off", "set": true, "on": true})"), "GET /v1 reports it: " + now);
    check(!contains(now, MAILBOX) && !contains(now, SECRET), "never the keys");

    p.post("/v1", R"({"relay":{"on":false}})", 7 * S, key);
    check(!p.saved.relay.on && p.saved.relay.mailbox == MAILBOX, "off keeps the keys");

    const Response bad = p.post("/v1", R"({"relay":{"secret":"short","colour":1,"access":"nbr_xxxxxxxxxxxxxxxx"}})", 8 * S, key);
    check(contains(bad.body, "relay.secret") && contains(bad.body, "relay.colour") && contains(bad.body, "relay.access"),
          "bad values warned, and an access key is not taken: " + bad.body);
    check(p.saved.relay.secret == SECRET, "a bad value is not kept");

    p.post("/v1", R"({"relay":"forget"})", 9 * S, key);
    check(p.saved.relay.mailbox.empty() && p.saved.relay.secret.empty() && p.saved.relay.on,
          "forget clears them");

    Box none;
    none.device.relay_state.clear();
    check(!contains(handle_request(none.device, "GET", "/v1", "", S, "").body, "\"relay\""),
          "a platform without a relay link reports none");
}

void the_link() {
    RelaySettings r{MAILBOX, SECRET, true};
    RelayLink l;
    check(!l.should_connect(0) && std::string(l.state_word()) == "off", "off before it is configured");
    l.configure(RelaySettings{}, 0);
    check(!l.should_connect(0), "no keys: off");
    l.configure(r, 10 * S);
    check(l.should_connect(10 * S), "keys: connects at once");
    l.connecting(10 * S);
    check(!l.should_connect(10 * S) && std::string(l.state_word()) == "connecting", "connecting");
    l.connected(11 * S);
    check(std::string(l.state_word()) == "connected" && !l.should_ping(20 * S), "connected; no ping yet");
    check(l.should_ping(41 * S) && !l.should_ping(42 * S), "a ping every 30 s");
    l.heard(41 * S);
    check(!l.silent(100 * S) && l.silent(116 * S), "silent after 75 s without a frame");

    l.closed(120 * S, 1006);
    check(std::string(l.state_word()) == "waiting" && !l.should_connect(121 * S) && l.should_connect(122 * S),
          "a dropped connection retries after 2 s");
    l.connecting(122 * S);
    l.closed(122 * S, 0);
    check(!l.should_connect(125 * S) && l.should_connect(126 * S), "then 4 s");
    for (int i = 0; i < 20; ++i) {
        l.connecting(0);
        l.closed(0, 0);
    }
    check(!l.should_connect(299 * S) && l.should_connect(300 * S), "at most 5 minutes apart");
    l.connecting(300 * S);
    l.connected(301 * S);
    l.closed(400 * S, 1006);
    check(l.should_connect(402 * S), "a good connection starts the backoff over");

    l.connecting(402 * S);
    l.closed(402 * S, 401);
    check(std::string(l.state_word()) == "refused" && !l.should_connect(4001 * S), "a refused mailbox waits");
    check(l.should_connect(4002 * S), "an hour later it tries again");
    l.configure(r, 1000 * S);
    l.connecting(1000 * S);
    l.connected(1000 * S);
    l.closed(1001 * S, 4000);
    check(std::string(l.state_word()) == "replaced" && !l.should_connect(2000 * S),
          "another connection to the same mailbox: stops instead of fighting");
    l.configure(r, 2000 * S);
    l.connecting(2000 * S);
    l.closed(2000 * S, 4001);
    check(std::string(l.state_word()) == "refused", "the mailbox removed: stops");
    r.on = false;
    l.configure(r, 3000 * S);
    check(std::string(l.state_word()) == "off" && !l.should_connect(3000 * S), "turned off: off");
}

void frames() {
    Box p;
    const std::string tell = R"({"from":"ci","state":"done","line":"main is green"})";
    check(relay_deliver(p.device, frame(SECRET, tell), S).why == "wrong secret", "no keys yet: dropped");
    p.set_keys(2 * S);

    Delivered d = relay_deliver(p.device, frame(SECRET, tell), 10 * S);
    check(d.status == 200 && d.why.empty(), "the right secret: told (" + d.why + ")");
    check(contains(handle_request(p.device, "GET", "/v1", "", 11 * S, "").body, "main is green"), "the pet shows it");

    check(relay_deliver(p.device, frame(SECRET.substr(1) + "x", tell), 12 * S).why == "wrong secret",
          "a wrong secret: dropped");
    check(relay_deliver(p.device, frame("", tell), 12 * S).why == "wrong secret", "no secret: dropped");
    check(relay_deliver(p.device, R"({"body":")" + tell + "\"}", 12 * S).why != "",
          "a frame without headers: dropped");
    check(relay_deliver(p.device, frame(SECRET, "[1,2]"), 12 * S).why == "body not a JSON object",
          "a body that isn't an object: dropped");
    check(relay_deliver(p.device, "pong", 12 * S).why == "not a relay frame", "a pong is not a webhook");
    check(relay_deliver(p.device, frame(SECRET, R"({"from":"device","state":"done"})"), 12 * S).status == 403,
          "a webhook can't speak as the device");
    check(relay_deliver(p.device, frame(SECRET, "{\"from\":\"x\",\"say\":\"" + std::string(1100, 'a') + "\"}"),
                        12 * S).status == 413,
          "the pet's own size limit holds");

    handle_request(p.device, "POST", "/v1", R"({"lock":true})", 13 * S, "", "", true);
    check(relay_deliver(p.device, frame(SECRET, tell), 14 * S).status == 200, "a locked pet still takes webhooks");

    check(same_secret("abc", "abc") && !same_secret("abc", "abd") && !same_secret("abc", "abcd"), "same_secret");
    check(relay_headers(RelaySettings{MAILBOX, SECRET, true}) == "X-NestBox-Mailbox: " + MAILBOX + "\r\n",
          "the upgrade's only header: the mailbox key");
    std::string why;
    check(relay_body(frame(SECRET, tell), SECRET, why) == tell && why.empty(), "relay_body: the body");
    check(relay_body(frame("x", tell), SECRET, why).empty() && why == "wrong secret", "relay_body: why not");
}

}  // namespace

int main() {
    setting_the_keys();
    the_link();
    frames();
    return test::summary();
}
