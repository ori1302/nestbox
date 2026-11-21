#include "shot.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "host/frame.h"
#include "host/palette.h"
#include "image.h"
#include "keys.h"

namespace nestbox {

namespace {
constexpr const char* DAYPART_ENV = "NESTBOX_SHOT_DAYPART";   // morning, afternoon, evening or night; else day

// A look's key ("watch"), or a digit key 0-9 as in the window; false for anything else.
bool parse_look(const std::string& key, Look& out) {
    if (key.size() == 1 && std::isdigit(static_cast<unsigned char>(key[0]))) {
        out = DIGIT_KEY_LOOKS[key[0] - '0'];
        return true;
    }
    return look_named(key, out);
}

void apply_daypart_env(Pet& pet) {
    const char* part = std::getenv(DAYPART_ENV);
    if (!part) return;
    for (Daypart p : {Daypart::Morning, Daypart::Afternoon, Daypart::Evening, Daypart::Night})
        if (std::string(part) == daypart_key(p)) pet.daypart(p);
}
}  // namespace

bool parse_shot(const std::vector<std::string>& args, ShotRequest& r) {
    if (args.size() < 5) return false;
    r.out = args[1];
    r.pet = std::atoi(args[2].c_str());
    r.look = args[3];
    r.ms = uint32_t(std::atol(args[4].c_str()));
    if (args.size() > 5) r.text = args[5];
    if (args.size() > 6) r.size = std::atoi(args[6].c_str());
    if (args.size() > 7) r.at = std::atoi(args[7].c_str());
    return true;
}

int render_shot(const Shelf& shelf, const Display& display, const ShotRequest& r) {
    const Egg& egg = shelf.egg(size_t(r.pet) % shelf.size());
    Canvas canvas(display.w, display.h);
    Pet pet;
    pet.set_display(display);
    pet.set_egg(&egg);
    pet.say(r.text, HOST_ACCENT, r.size, SpeechAt(r.at));
    Look look;
    if (!parse_look(r.look, look)) {
        std::fprintf(stderr, "unknown look %s; `docgen spec` lists them\n", r.look.c_str());
        return 2;
    }
    pet.want(look, 0);
    apply_daypart_env(pet);
    for (uint32_t t = 0; t <= r.ms; t += 1000 / FPS) pet.update(t);
    pet.render(canvas, r.ms);
    if (!write_ppm(canvas, r.out)) return 1;
    std::printf("%s  %s  %s  %s\n", egg.name.c_str(), look_label(pet.look()), pet.phase_label(),
                pet.playing().c_str());
    return 0;
}

}  // namespace nestbox
