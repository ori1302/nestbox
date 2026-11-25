// Adding and taking off pets over the API (PUT and DELETE /v1/pets/NAME): the key or the pet's button, the checks
// on the egg and the name, one change at a time, the platform's part (writing the file), replacing the pet that is
// out, and what GET /v1/pets says meanwhile. Also a shelf that keeps only the pet out whole (the board's).
// Usage: pets_test NIBBLE_EGG GANDER_EGG
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
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

// Draws frames until `until`; whether the screen shows anything.
bool draws(Door& d, uint32_t from, uint32_t until) {
    for (uint32_t t = from; t < until; t += 33) {
        d.brain.decide(t);
        d.brain.show(d.pet, t);
        d.pet.update(t);
        d.pet.render(d.screen, t);
    }
    return std::any_of(d.screen.px.begin(), d.screen.px.end(), [](uint16_t px) { return px != 0; });
}

// A shelf with `load` keeps the pets that are not out slim, describes them exactly as whole ones, and loads a pet
// when it comes out; one that can't be loaded leaves the pet out as it is.
void kept_slim(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door whole(nibble);
    whole.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, whole.key);
    whole.store(10 * S);
    const std::string gander_said = whole.call("GET", "/v1/pets/gander", "", 11 * S).body;
    const std::string guide_said = whole.call("GET", "/agents", "", 11 * S).body;
    whole.call("POST", "/v1", R"({"pet":"gander"})", 12 * S, whole.key);
    const std::string nibble_said = whole.call("GET", "/v1/pets/nibble", "", 16 * S).body;

    Door d(nibble);
    std::map<std::string, std::vector<uint8_t>> files{{"nibble", nibble}, {"gander", gander}};
    int loads = 0, allocations = 0;
    d.shelf.egg_memory = [&](const std::function<void()>& step) {
        allocations++;
        step();
    };
    d.shelf.load = [&](const std::string& id, Egg& out) {
        loads++;
        std::string error;
        const auto f = files.find(id);
        return f != files.end() && load_egg(f->second, out, error);
    };
    d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, d.key);
    d.store(10 * S);
    check(d.shelf.egg(1).slim && d.shelf.egg(1).bytes.size() < gander.size() / 10 && !d.shelf.egg(0).slim,
          "a pet added while another is out is kept slim");
    check(idle_reach(d.shelf.egg(1)) > 0 && idle_reach(d.shelf.egg(1)) == idle_reach(whole.shelf.egg(1)),
          "it keeps how wide its Idle is (the places it describes depend on it)");
    check(d.call("GET", "/v1/pets/gander", "", 11 * S).body == gander_said,
          "a slim pet is described exactly as a whole one");
    check(d.call("GET", "/agents", "", 11 * S).body == guide_said, "and the guide says the same");
    const std::string listed = d.call("GET", "/v1/pets", "", 11 * S).body;
    check(contains(listed, "\"bytes\": " + std::to_string(gander.size())) &&
              contains(listed, "\"bytes\": " + std::to_string(nibble.size())) && !contains(listed, "\"storage\""),
          "GET /v1/pets gives each pet's file size, a slim one's too, and no storage when the platform has none");
    d.device.health.flash_total = 7 << 20;
    d.device.health.flash_used = 1 << 20;
    Fields f;
    parse_json(d.call("GET", "/v1/pets", "", 11 * S).body, f);
    check(f.num("storage.total") == double(7 << 20) && f.num("storage.used") == double(1 << 20),
          "and the pets' flash when it has it");
    const Response pic = d.call("GET", "/v1/pets/gander/picture", "", 11 * S);
    check(pic.status == 200 && pic.type == "image/png" && pic.body.rfind("\x89PNG", 0) == 0 &&
              pic.body == whole.call("GET", "/v1/pets/gander/picture", "", 11 * S).body,
          "a slim pet's picture is the same PNG as a whole one's");
    check(d.call("GET", "/v1/pets/fox/picture", "", 11 * S).status == 404, "and a pet that isn't there has none");

    Response r = d.call("POST", "/v1", R"({"pet":"gander"})", 12 * S, d.key);
    check(r.status == 200 && loads == 1 && !d.shelf.egg(1).slim && d.shelf.egg(0).slim &&
              d.pet.egg() == &d.shelf.egg(1),
          "bringing it out loads it, and the pet that was out goes slim");
    check(allocations >= 3, "adding, loading and slimming run where the platform keeps egg memory");
    check(draws(d, 12 * S, 16 * S), "the loaded pet draws");
    check(d.call("GET", "/v1/pets/nibble", "", 16 * S).body == nibble_said, "the one gone slim is described the same");

    files.erase("nibble");
    r = d.call("POST", "/v1", R"({"pet":"nibble"})", 17 * S, d.key);
    check(contains(r.body, "nibble can't be loaded; gander stays out") && d.pet.egg() == &d.shelf.egg(1) &&
              d.shelf.egg(0).slim,
          "a pet that can't be loaded: the pet out stays");
    check(draws(d, 17 * S, 19 * S), "and keeps drawing");

    files["nibble"] = nibble;
    d.call("DELETE", "/v1/pets/gander", "", 20 * S, d.key);
    d.store(20 * S);
    check(d.shelf.size() == 1 && d.shelf.id(0) == "nibble" && !d.shelf.egg(0).slim && d.pet.egg() == &d.shelf.egg(0),
          "taking off the pet out loads the next");
    check(draws(d, 20 * S, 24 * S), "which draws");

    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen{280, 240};
    Device device{brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test"};
    shelf.load = [&](const std::string& id, Egg& out) {
        std::string error;
        return id == "gander" && load_egg(gander, out, error);
    };
    for (const auto& [id, bytes] : {std::pair{"nibble", &nibble}, std::pair{"gander", &gander}}) {
        Egg egg;
        std::string error;
        load_egg(*bytes, egg, error);
        shelf.add(std::move(egg), id);
    }
    put_out_first(device, *display_named("lcd-1.69"), "nibble", 0);
    check(shelf.current() == 1 && pet.egg() == &shelf.egg(1) && !shelf.egg(1).slim,
          "at start-up, a saved pet that can't be loaded gives way to the next");
}

// A shelf that loads later (the board): a pet asked for is coming while the platform reads its egg, the pet out
// keeps moving and says who is coming, then the new one comes out; a read that fails leaves the pet out.
void loaded_later(const std::vector<uint8_t>& nibble, const std::vector<uint8_t>& gander) {
    Door d(nibble);
    std::map<std::string, std::vector<uint8_t>> files{{"nibble", nibble}, {"gander", gander}};
    d.shelf.load = [&](const std::string& id, Egg& out) {
        std::string error;
        const auto f = files.find(id);
        return f != files.end() && load_egg(f->second, out, error);
    };
    d.shelf.load_later = true;
    d.call("PUT", "/v1/pets/gander", text_of(gander), 10 * S, d.key);
    d.store(10 * S);
    const Egg* nibble_out = d.pet.egg();

    Response r = d.call("POST", "/v1", R"({"pet":"gander"})", 11 * S, d.key);
    check(r.status == 200 && !contains(r.body, "only pet") && d.shelf.coming() == "gander" &&
              d.pet.egg() == nibble_out && d.shelf.egg(1).slim,
          "asked for, a slim pet is coming and the pet out stays meanwhile");
    check(contains(d.call("GET", "/v1", "", 11 * S).body, "Gander is coming"), "and says who is coming");
    check(draws(d, 11 * S, 12 * S) && d.pet.egg() == nibble_out, "it keeps drawing while the egg is read");
    check(pet_to_load(d.device) == "gander", "the platform is told which egg to read");
    Egg egg;
    std::string error;
    load_egg(gander, egg, error);
    finish_pet_load(d.device, "gander", &egg, 12 * S);
    check(d.shelf.coming().empty() && d.pet.egg() == &d.shelf.egg(1) && !d.shelf.egg(1).slim && d.shelf.egg(0).slim,
          "once read, it comes out and the one before goes slim");
    check(!contains(d.call("GET", "/v1", "", 12 * S).body, "is coming"), "and the note is gone");
    check(draws(d, 12 * S, 15 * S), "the new pet draws");

    d.call("POST", "/v1", R"({"pet":"nibble"})", 16 * S, d.key);
    d.call("POST", "/v1", R"({"pet":"gander"})", 16 * S, d.key);
    check(d.shelf.coming().empty() && !contains(d.call("GET", "/v1", "", 16 * S).body, "is coming"),
          "asking for the pet out again stops the one coming");
    Egg late;
    load_egg(nibble, late, error);
    finish_pet_load(d.device, "nibble", &late, 17 * S);
    check(d.pet.egg() == &d.shelf.egg(1) && d.shelf.egg(0).slim, "an egg read for a pet no longer coming is dropped");

    d.call("POST", "/v1", R"({"pet":"next"})", 18 * S, d.key);
    check(d.shelf.coming() == "nibble", "next: the other pet is coming");
    finish_pet_load(d.device, "nibble", nullptr, 19 * S);
    check(d.shelf.coming().empty() && d.pet.egg() == &d.shelf.egg(1) &&
              contains(d.pets(19 * S).str("last_change"), "nibble: it can't be loaded; gander stays out"),
          "an egg that can't be read: the pet out stays, and GET /v1/pets says so");
    check(draws(d, 19 * S, 21 * S), "and keeps drawing");
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
    kept_slim(nibble, gander);
    loaded_later(nibble, gander);
    return test::summary();
}
