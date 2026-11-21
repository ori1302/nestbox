// What every device puts on the wire besides the API: status lines for every status the engine answers with, the
// shelf order of egg files, the Bonjour TXT record and the input the device's own hardware sends.
// Usage: host_test
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "check.h"
#include "host/eggs.h"
#include "host/protocol.h"
#include "util/json.h"

using namespace nestbox;
using test::check;

namespace {

void reason_phrases() {
    const std::pair<int, const char*> sent[] = {
        {200, "OK"},        {202, "Accepted"},           {302, "Found"},    {400, "Bad Request"},
        {403, "Forbidden"}, {404, "Not Found"},          {405, "Method Not Allowed"},
        {409, "Conflict"},  {413, "Payload Too Large"},  {507, "Insufficient Storage"},
        {401, "Unauthorized"}, {429, "Too Many Requests"},
    };
    for (auto [status, phrase] : sent)
        check(reason_phrase(status) && std::strcmp(reason_phrase(status), phrase) == 0,
              std::to_string(status) + " is \"" + phrase + "\"");
    check(std::strcmp(reason_phrase(418), "Client Error") == 0 && std::strcmp(reason_phrase(502), "Server Error") == 0,
          "a status the API never sends still gets its class's phrase");
    check(too_large_reply(1024).status == 413 && too_large_reply(1024).body.find("1024") != std::string::npos,
          "an oversized body gets 413, with the limit");
}

void egg_files() {
    check(is_egg_file("nibble.egg") && !is_egg_file("nibble.png") && !is_egg_file("egg"), "only .egg files are eggs");
    check(egg_id("nibble.egg") == "nibble" && egg_id("notes.txt") == "notes.txt", "an egg's id is its file name");
    std::vector<std::string> files = {"zebra.egg", "ant.egg", "nibble.egg", "fox.egg"};
    std::sort(files.begin(), files.end(), egg_file_before);
    check(files == std::vector<std::string>{"nibble.egg", "ant.egg", "fox.egg", "zebra.egg"},
          "the default pet comes first, then the rest by name");
}

void bonjour_record() {
    const TxtRecord txt = bonjour_txt("Goose", "a1b2", "nestbox-a1b2.local");
    const TxtRecord want = {{"pet", "Goose"}, {"api", "1"}, {"id", "a1b2"}, {"host", "nestbox-a1b2.local"}};
    check(txt == want, "the TXT record names the pet, the API version, the id and the host");
}

void hardware_input() {
    Fields f;
    check(parse_json(button_input("double"), f) && f.str("press") == "double" && f.values.size() == 1,
          "a button press is {\"press\": ...}");
    f = Fields();
    check(parse_json(sensor_input("shake"), f) && f.str("gesture") == "shake" && f.values.size() == 1,
          "a gesture is {\"gesture\": ...}");
    f = Fields();
    check(parse_json(computer_input(true), f) && f.str("computer") == "asleep" && f.values.size() == 1,
          "the USB host sleeping is {\"computer\": \"asleep\"}");
    f = Fields();
    check(parse_json(computer_input(false), f) && f.str("computer") == "awake", "... and waking is \"awake\"");
}

}  // namespace

int main() {
    reason_phrases();
    egg_files();
    bonjour_record();
    hardware_input();
    return test::summary();
}
