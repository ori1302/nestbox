#include "commands.h"

#include <cstdio>
#include <string>
#include <unistd.h>

#include "activity.h"
#include "bonjour.h"
#include "door/http.h"
#include "http.h"
#include "pairing_store.h"
#include "sensors.h"
#include "util/json.h"
#include "util/text.h"

namespace companion {
namespace {

constexpr int SEND_TIMEOUT_MS = 1500;
constexpr int FIND_BROWSE_MS = 2000;
constexpr int PAIR_POLL_S = 2, PAIR_GIVE_UP_S = 62;

void print_field(const char* key, std::string value) {
    nestbox::replace_all(value, " ", "%20");
    if (!value.empty()) std::printf("  %s=%s", key, value.c_str());
}

void print_machine_pet(const NetworkPet& p, bool paired) {
    nestbox::JsonWriter json;
    json.begin_object().field("event", "pet").field("name", p.name).field("url", p.url).field("id", p.id);
    json.field("host", p.host).field("pet", p.pet).field("simulator", p.simulator()).field("paired", paired);
    std::printf("%s%s\n", MACHINE_TAG, json.end_object().text().c_str());
}

}  // namespace

// Pairing: the pet asks the user, and a press of its button hands this Mac the key. Asks again every couple of
// seconds until the button is pressed or the pet gives up after a minute.
int run_pair(const std::string& device, bool machine) {
    const std::string target = device.empty() ? remembered_url() : device;
    Url url;
    if (!parse_url(target, url)) {
        std::fprintf(stderr, "nestbox pair: not a pet's address: %s\n", target.c_str());
        return 2;
    }
    const std::string name = short_host();
    const std::string body = nestbox::JsonWriter().begin_object().field("name", name).end_object().text();
    auto event = [&](const char* what, const std::string& detail = "") {
        if (!machine) return;
        nestbox::JsonWriter json;
        json.begin_object().field("event", what);
        if (!detail.empty()) json.field("detail", detail);
        std::printf("%s%s\n", MACHINE_TAG, json.end_object().text().c_str());
        std::fflush(stdout);
    };
    bool told = false;
    for (int attempt = 0; attempt * PAIR_POLL_S < PAIR_GIVE_UP_S; attempt++) {
        std::string reply;
        const int status = post(url, nestbox::PAIR_PATH, body, SEND_TIMEOUT_MS, &reply);
        nestbox::Fields f;
        nestbox::parse_json(reply, f);
        if (status == 200 && f.str("pairing") == "done" && !f.str("key").empty()) {
            remember_key(target, f.str("id"), f.str("key"));
            std::printf("paired: the key is kept in ~/.nestbox/device\n");
            event("pair_done");
            return 0;
        }
        if (status != 200) {
            const std::string why = status ? f.str("error") : "the pet did not answer";
            std::fprintf(stderr, "nestbox pair: %s\n", why.c_str());
            event("pair_failed", why);
            return 1;
        }
        if (!told) {
            std::printf("press the pet's button to pair (the pet asks \"Pair with %s?\")\n", name.c_str());
            event("pair_waiting");
            told = true;
        }
        sleep(PAIR_POLL_S);
    }
    std::fprintf(stderr, "nestbox pair: the button was not pressed in time\n");
    event("pair_failed", "the button was not pressed in time");
    return 1;
}

int run_find(const std::string& only_name, bool machine) {
    auto pets = pets_on_network(FIND_BROWSE_MS, only_name);
    const std::string paired = paired_id();
    for (auto& p : pets) {
        if (machine) {
            print_machine_pet(p, !paired.empty() && p.id == paired);
            continue;
        }
        std::printf("%-20s", p.name.c_str());
        print_field("id", p.id);
        print_field("host", p.host);
        print_field("pet", p.pet);
        if (p.simulator()) print_field("kind", "simulator");
        if (!paired.empty() && p.id == paired) print_field("paired", "yes");
        std::printf("  %s\n", p.url.c_str());
    }
    if (pets.empty() && !machine) std::printf("no pets found on the network (_nestbox._tcp)\n");
    return pets.empty() ? 1 : 0;
}

}  // namespace companion
