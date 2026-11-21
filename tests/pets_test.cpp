// Adding and taking off pets over the API (PUT and DELETE /v1/pets/NAME): the key or the pet's button, the checks
// on the egg and the name, one change at a time, the platform's part (writing the file), replacing the pet that is
// out, and what GET /v1/pets says meanwhile.
// Usage: pets_test NIBBLE_EGG GANDER_EGG
#include <algorithm>
#include <cstdio>
#include <string>
#include "check.h"
#include "door/api.h"
#include "host/eggs.h"
#include "screen/display.h"
#include "util/json.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t S = 1000;
constexpr const char* HOST = "nestbox-test.local";

std::string text_of(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// A device with Nibble on its shelf and out, paired.
struct Door {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen{280, 240};
    Device device{brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test"};
    std::string key;

    explicit Door(const std::vector<uint8_t>& nibble) {
        Egg egg;
        std::string error;
        if (!load_egg(nibble, egg, error)) test::fail("Nibble loads: " + error);
        shelf.add(std::move(egg), "nibble");
        device.random = [n = 0u]() mutable { return n += 2654435761u; };
        put_out_first(device, *display_named("lcd-1.69"), "", 0);
        call("POST", "/v1/pair", R"({"name":"Mac"})", S);
        input(R"({"press":"single"})", 2 * S);
        Fields f;
        parse_json(call("POST", "/v1/pair", R"({"name":"Mac"})", 3 * S).body, f);
        key = "Bearer " + f.str("key");
    }
    Response call(const std::string& method, const std::string& path, const std::string& body, uint32_t now,
                  const std::string& auth = "") {
        return handle_request(device, method, path, body, now, HOST, "", false, auth, "10.0.0.5");
    }
    Response input(const std::string& body, uint32_t now) {
        return handle_request(device, "POST", "/v1/input", body, now, "", "", true);
    }
    Fields pets(uint32_t now) {
        Fields f;
        if (!parse_json(call("GET", "/v1/pets", "", now).body, f)) test::fail("GET /v1/pets is JSON");
        return f;
    }
    // The platform's part: writes the file (here: nowhere), then hands back. Returns the bytes it would write.
    size_t store(uint32_t now, const std::string& error = "") {
        const Device::PetChange* c = pet_change_to_store(device, now);
        if (!c) return 0;
        const size_t bytes = c->kind == Device::PetChange::Kind::Add ? c->egg.bytes.size() : 1;
        finish_pet_change(device, error, now);
        return bytes;
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void limits() {
    check(body_limit("PUT", "/v1/pets/fox") == MAX_EGG, "an egg may be up to MAX_EGG");
    check(body_limit("POST", "/v1/pets/fox") == MAX_BODY && body_limit("PUT", "/v1") == MAX_BODY &&
              body_limit("POST", "/v1/tell") == MAX_BODY,
          "every other request stays at MAX_BODY");
}

void added_with_the_button(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    Response r = d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S);
    check(r.status == 202 && contains(r.body, "waiting for the button"), "without the key it waits for the button");
    check(contains(d.call("GET", "/v1", "", 10 * S).body, "Add Gander? Press the button"), "the pet asks");
    check(d.store(11 * S) == 0, "nothing is written before the button");
    Fields f = d.pets(11 * S);
    check(f.str("change.pet") == "gander" && f.str("change.waiting") == "the button", "GET /v1/pets: waiting");
    check(d.call("PUT", "/v1/pets/fox", text_of(gander), 12 * S).status == 409, "one change at a time");
    check(d.call("POST", "/v1/pair", R"({"name":"Other"})", 12 * S).status == 409, "and no pairing meanwhile");
    Fields in;
    parse_json(d.input(R"({"press":"single"})", 13 * S).body, in);
    check(in.str("input") == "adding gander", "the button: go ahead");
    check(d.pets(13 * S).str("change.waiting") == "saving", "then it is being saved");
    check(d.store(14 * S) == gander.size(), "the platform writes the egg as sent");
    f = d.pets(14 * S);
    check(contains(f.str("pets"), "\"gander\"") && f.str("last_change") == "added gander" && !f.has("change"),
          "Gander is on the shelf");
    check(d.shelf.egg(d.shelf.current()).name == "Nibble", "the pet out stays out");
    check(d.call("POST", "/v1", R"({"pet":"gander"})", 15 * S, d.key).status == 200, "and Gander can come out");
}

void added_with_the_key(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    Response r = d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, d.key);
    check(r.status == 202 && contains(r.body, "saving"), "with the key it goes ahead");
    check(!contains(d.call("GET", "/v1", "", 10 * S).body, "Press the button"), "and the pet asks nothing");
    check(d.store(10 * S) == gander.size() && d.shelf.has("gander"), "saved and on the shelf");
}

void refused(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    check(d.call("PUT", "/v1/pets/Gander", text_of(gander), 10 * S, d.key).status == 400, "a name in capitals");
    check(d.call("PUT", "/v1/pets/../x", text_of(gander), 10 * S, d.key).status == 400, "a path");
    check(d.call("PUT", "/v1/pets/next", text_of(gander), 10 * S, d.key).status == 400, "a word the API uses");
    check(d.call("PUT", "/v1/pets/gander", "", 10 * S, d.key).status == 400, "no body");
    Response r = d.call("PUT", "/v1/pets/gander", "NEGG not really an egg", 10 * S, d.key);
    check(r.status == 400 && contains(r.body, "not a pet's egg"), "a body that isn't an egg");
    std::string cut = text_of(gander);
    cut.resize(cut.size() / 2);
    check(d.call("PUT", "/v1/pets/gander", cut, 10 * S, d.key).status == 400, "half an egg");
    check(d.device.pet_change.kind == Device::PetChange::Kind::None, "none of them starts a change");
    check(d.call("POST", "/v1/pets/gander", "{}", 10 * S, d.key).status == 405, "POST is not how");
}

void the_button_not_pressed(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S);
    Fields f = d.pets(10 * S + 61 * S);
    check(!f.has("change") && contains(f.str("last_change"), "the button wasn't pressed"),
          "a minute later it gives up");
    check(!contains(d.call("GET", "/v1", "", 72 * S).body, "Press the button"), "and stops asking");
    check(d.call("PUT", "/v1/pets/gander", text_of(gander), 73 * S, d.key).status == 202, "then another may start");
}

void storing_fails(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, d.key);
    d.store(10 * S, "no room left (12 KB free)");
    Fields f = d.pets(11 * S);
    check(!d.shelf.has("gander") && f.str("last_change") == "gander: no room left (12 KB free)",
          "a file that could not be written: not on the shelf, and GET /v1/pets says why");
}

void replacing_the_pet_out(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    const Egg* before = d.pet.egg();
    d.call("PUT", "/v1/pets/nibble", text_of(gander), 10 * S, d.key);
    check(contains(d.call("GET", "/v1", "", 10 * S).body, "nibble"), "still Nibble until it is saved");
    d.store(10 * S);
    check(d.shelf.size() == 1 && d.pet.egg() != before && d.pet.egg() == &d.shelf.egg(0) &&
              d.shelf.egg(0).name == "Gander",
          "replaced in place, and the pet out is the new egg");
    for (uint32_t t = 10 * S; t < 20 * S; t += 33) {   // frames after the swap draw from the new egg only
        d.brain.decide(t);
        d.brain.show(d.pet, t);
        d.pet.update(t);
        d.pet.render(d.screen, t);
    }
    check(d.pet.egg() == &d.shelf.egg(0) &&
              std::any_of(d.screen.px.begin(), d.screen.px.end(), [](uint16_t px) { return px != 0; }),
          "frames after replacing the pet out draw the new egg");
}

void taking_off(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    check(d.call("DELETE", "/v1/pets/nibble", "", 10 * S, d.key).status == 409, "the last pet stays");
    check(d.call("DELETE", "/v1/pets/fox", "", 10 * S, d.key).status == 404, "an unknown pet: 404");
    d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, d.key);
    d.store(10 * S);
    Response r = d.call("DELETE", "/v1/pets/nibble", "", 20 * S);
    check(r.status == 202 && contains(d.call("GET", "/v1", "", 20 * S).body, "Take Nibble off? Press the button"),
          "without the key, taking off asks too");
    d.input(R"({"press":"single"})", 21 * S);
    check(d.store(22 * S) == 1, "the platform deletes the file");
    check(d.shelf.size() == 1 && d.shelf.id(0) == "gander" && d.pet.egg() == &d.shelf.egg(0),
          "taking off the pet out brings out the next first");
    check(d.pets(22 * S).str("last_change") == "took off nibble", "GET /v1/pets says so");
    for (uint32_t t = 22 * S; t < 30 * S; t += 33) {
        d.brain.decide(t);
        d.brain.show(d.pet, t);
        d.pet.update(t);
        d.pet.render(d.screen, t);
    }
    check(d.pet.egg() == &d.shelf.egg(0) &&
              std::any_of(d.screen.px.begin(), d.screen.px.end(), [](uint16_t px) { return px != 0; }),
          "frames after taking off the pet out draw the one out now");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: pets_test NIBBLE_EGG GANDER_EGG\n");
        return 2;
    }
    const std::vector<uint8_t> nibble = test::read_file(argv[1]), gander = test::read_file(argv[2]);
    limits();
    added_with_the_button(nibble, gander);
    added_with_the_key(nibble, gander);
    refused(nibble, gander);
    the_button_not_pressed(nibble, gander);
    storing_fails(nibble, gander);
    replacing_the_pet_out(nibble, gander);
    taking_off(nibble, gander);
    return test::summary();
}
