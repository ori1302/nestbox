// The egg reader: the words and pictures an egg carries, the time-of-day fallbacks, and that broken files are
// refused with a reason instead of crashing.
// Usage: egg_test EGG [AMBIENT_EGG]   (AMBIENT_EGG: tests/ambient_pet.py's pet, for the moving parts)
#include <algorithm>
#include <cstdio>
#include "body/pet.h"
#include "check.h"
#include "screen/canvas.h"

using namespace nestbox;
using test::check;

namespace {

uint32_t description_length(const std::vector<uint8_t>& b) {
    return b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24);
}

std::string description(const std::vector<uint8_t>& b) {
    return std::string(b.begin() + 8, b.begin() + 8 + description_length(b));
}

void refused(std::vector<uint8_t> bytes, const std::string& what) {
    Egg egg;
    std::string why;
    check(!load_egg(std::move(bytes), egg, why) && !why.empty(), what);
}

// The egg with one string in its description replaced by another of the same length, so every offset stays valid.
std::vector<uint8_t> edited(const std::vector<uint8_t>& bytes, const std::string& from, const std::string& to) {
    const size_t at = description(bytes).find(from);
    if (at == std::string::npos || to.size() != from.size()) {
        test::fail("the test egg has no " + from);
        return {};
    }
    std::vector<uint8_t> b = bytes;
    std::copy(to.begin(), to.end(), b.begin() + 8 + at);
    return b;
}

void words_and_scene(const Egg& e) {
    check(!e.name.empty() && !e.theme.empty() && !e.personality.empty() && !e.style.empty(),
          "it has a name, theme, personality and style");
    check(e.color.size() == 7 && e.color[0] == '#', "its colour is #rrggbb");
    check(e.find("Idle") && e.find("Walk") && e.anims.size() > 10, "Idle, Walk and its other animations");
    check(e.has_scene && e.scene.day && e.scene.keep[2] > e.scene.keep[0], "a scene with a part to keep in view");
}

void daypart_fallbacks(const Scene& s) {
    float dim;
    const auto* night = s.part[int(Daypart::Night)];
    check(s.picture(Daypart::Morning, dim) == s.day && dim == 0, "morning without its own picture: the main one");
    check(s.picture(Daypart::Night, dim) == night && dim == 0, "night: its night picture");
    check(s.picture(Daypart::Evening, dim) == night && dim == 0, "evening without its own: the night picture");
    Scene bare;
    bare.day = s.day;
    check(bare.picture(Daypart::Night, dim) == s.day && dim == 1, "no night picture: the main one, dimmed");
    check(bare.picture(Daypart::Evening, dim) == s.day && dim > 0 && dim < 1,
          "no evening or night picture: a little dimmed");
    Scene full = bare;
    full.part[int(Daypart::Evening)] = s.day + 1;
    check(full.picture(Daypart::Evening, dim) == s.day + 1 && dim == 0, "its own evening picture wins");
}

void broken_files(const std::vector<uint8_t>& bytes) {
    refused({}, "an empty file is refused with a reason");
    refused({'P', 'N', 'G', ' ', 0, 0, 0, 0}, "another kind of file is refused with a reason");
    refused(std::vector<uint8_t>(bytes.begin(), bytes.begin() + bytes.size() / 2),
            "half an egg is refused with a reason");
    std::vector<uint8_t> broken = bytes;
    broken[8] = 'x';  // the description must open with '{'
    refused(broken, "a broken description is refused with a reason");

    // Coded frames: the first animation's first frame (its offset after the box table) with a colour past the
    // palette, then with its offset past the end of the file.
    Egg e;
    std::string why;
    if (!check(load_egg(bytes, e, why) && e.anims[0].boxes[0].code, "its frames are run-coded"))
        return;
    const Anim& a = e.anims[0];
    const std::string d = description(bytes);
    const size_t key = d.find("\"at\":", d.find("\"anims\"")) + 5;
    const size_t block = size_t(std::stoul(d.substr(key, d.find_first_of(",}", key) - key)));
    const size_t first = size_t(a.boxes[0].code - e.bytes.data());
    std::vector<uint8_t> wrong = bytes;
    wrong[first] = 0xC0;                 // one pixel as it is: colour 255, which the palette doesn't have
    wrong[first + 1] = 0xFF;
    refused(wrong, "a coded frame with a colour past the palette is refused");
    wrong = bytes;
    const size_t at = block + a.boxes.size() * 8;
    const uint32_t past = uint32_t(bytes.size());
    for (int i = 0; i < 4; i++) wrong[at + i] = uint8_t(past >> (8 * i));
    refused(wrong, "a coded frame whose offset runs past the file is refused");
}

void bad_descriptions(const std::vector<uint8_t>& bytes) {
    const std::string marks = "\"rush\":1,\"hit\":2,\"return\":3";
    refused(edited(bytes, marks, "\"rush\":9,\"hit\":2,\"return\":3"), "attack markers past the frames are refused");
    refused(edited(bytes, marks, "\"rush\":3,\"hit\":2,\"return\":1"), "a rush after its return is refused");

    const std::string d = description(bytes);
    {  // the palette cut to two colours, padded to the original field's width
        const size_t a = d.find("\"count\":"), z = d.find_first_of(",}", a);
        const std::string from = d.substr(a, z - a);
        std::string to = "\"count\":2";
        to.insert(8, std::string(from.size() > to.size() ? from.size() - to.size() : 0, ' '));
        refused(edited(bytes, from, to), "pixels using colours past the palette are refused");
    }
    {  // the first animation's offset made a string
        const size_t a = d.find("\"at\":", d.find("\"anims\"")), z = d.find_first_of(",}", a);
        const std::string from = d.substr(a, z - a);
        std::string to = "\"at\":\"nan\"";
        to.resize(std::max(to.size(), from.size()), ' ');
        refused(edited(bytes, from, to), "an offset that is not a number is refused");
    }
    const std::string attack = "\"w\":48,\"h\":60,\"rows\":8,\"frames\":5";
    refused(edited(bytes, attack, "\"w\":12,\"h\":60,\"rows\":8,\"frames\":5"),
            "cropped frames outside their animation's size are refused");
    refused(edited(bytes, attack, "\"w\":48,\"h\":60,\"rows\":8,\"frames\":0"),
            "an animation with no frames is refused");

    Egg z;
    std::string why;
    const bool ok = load_egg(edited(bytes, "\"durations\":[6,4,8,6,10]", "\"durations\":[0,4,8,6,10]"), z, why);
    const Anim* a = ok ? z.find("Attack") : nullptr;
    check(a && a->durations[0] == 1, "a frame of no time gets one tick");
}

// A pet's own walking pace: read, clamped to 20-120 px/s, and 0 (the engine's own) when missing or not above 0.
void pace(const std::vector<uint8_t>& bytes, const Egg& plain) {
    check(plain.pace == 0, "an egg without a pace walks at the engine's own");
    const std::string credit = "\"credit\":\"NestBox\"";
    auto paced = [&](const std::string& to) {
        Egg z;
        std::string why;
        return load_egg(edited(bytes, credit, to), z, why) ? z.pace : -1.0f;
    };
    check(paced("\"pace\":40,\"c\":\"xy\"") == 40, "a pace of 40 px/s is read");
    check(paced("\"pace\":5,\"cr\":\"xy\"") == 20, "a pace below 20 is walked at 20");
    check(paced("\"pace\":500,\"c\":\"x\"") == 120, "a pace above 120 is walked at 120");
    check(paced("\"pace\":-4,\"c\":\"xy\"") == 0, "a pace below zero is the engine's own");
    check(paced("\"pace\":\"a\",\"c\":\"x\"") == 0, "a pace that is not a number is the engine's own");
}

// The corner a scene's gauges go in: top right unless it names another; a name that is not a corner is refused.
void gauges(const std::vector<uint8_t>& bytes, const Egg& plain) {
    check(plain.scene.gauges == Corner::TopRight, "a scene's gauges go top right unless it says otherwise");
    const std::string keep = "\"keep\":[120,100,292,238]";
    Egg z;
    std::string why;
    check(load_egg(edited(bytes, keep, "\"gauges\":\"bottom-left\"  "), z, why) && z.scene.gauges == Corner::BottomLeft,
          "a scene's own corner for its gauges is read");
    refused(edited(bytes, keep, "\"gauges\":\"middle\"       "),
            "gauges in something that is not a corner are refused");
}

// A throw kit's release point names a pixel of a one-row throw's frame; the test pet (Nibble) throws in 8 directions,
// so one is refused, as is one outside the frame.
void release(const std::vector<uint8_t>& bytes) {
    const std::string fast = "\"typing_fast\":false";
    check(description(bytes).find(fast) != std::string::npos, "the test pet has a throw kit");
    refused(edited(bytes, fast, "\"release\":[20,10]  "), "a release point in an 8-row throw is refused");
    refused(edited(bytes, fast, "\"release\":[-1,10]  "), "a release point outside the frame is refused");
}

// The test pet's moving parts (tests/ambient_pet.py): a waterfall by day and at night, lights and fireflies only at
// night; frames that follow their durations and loop, and none for a picture a part has no frames for; and "pop", a
// part shown once on cue.
void ambient(const Scene& s) {
    if (!check(s.ambient.size() == 4, "the scene has four moving parts")) return;
    check(s.ambient[3].name == "pop" && s.ambient[3].cue == Ambient::Cue::Once &&
              s.ambient[0].cue == Ambient::Cue::Always,
          "a part's name and cue");
    const auto* night = s.part[int(Daypart::Night)];
    const Ambient& fall = s.ambient[0];
    check(fall.w > 0 && fall.h > 0 && fall.frames > 1 && fall.durations.size() == size_t(fall.frames),
          "with its size and frames");
    check(s.frames_for(fall, s.day) == &fall.day && s.frames_for(fall, night) == &fall.part[int(Daypart::Night)] &&
              fall.day.size() == size_t(fall.frames), "the waterfall: frames for the main and the night picture");
    check(s.frames_for(s.ambient[1], s.day) == nullptr && s.frames_for(s.ambient[1], night) &&
              s.frames_for(s.ambient[2], s.day) == nullptr && s.frames_for(s.ambient[2], night),
          "the lights: frames only for the night picture");
    check(s.frames_for(fall, s.day + 1) == nullptr, "none for a picture it has no frames for");
    bool changes = true;
    for (const Changes& c : fall.day) changes = changes && c.count > 0 && c.count <= fall.w * fall.h;
    check(changes, "each waterfall frame changes some of its pixels");
    int total = 0;
    for (auto d : fall.durations) total += d;
    check(fall.frame_at(0) == 0 && fall.frame_at(fall.durations[0]) == 1 && fall.frame_at(uint64_t(total)) == 0 &&
              fall.frame_at(uint64_t(total) * 1000 + fall.durations[0] - 1) == 0,
          "frames follow their durations and loop");
}

// A cued part is drawn only while a step shows it, from the step's start, and holds its last frame once played.
void cued(const Egg& e) {
    const Ambient& pop = e.scene.ambient[3];
    Pet pet;
    pet.set_display(*display_named("lcd-1.69"));
    pet.set_egg(&e);
    Canvas c(pet.display().w, pet.display().h);
    const Stage& st = pet.stage();
    const int s = st.scene_scale();
    const int x = pop.x * s - st.scene_x(), y = pop.y * s - st.scene_y();   // its top-left pixel, on screen
    int filled = 0;
    auto red = [&](uint32_t t) {
        pet.update(t);
        pet.render(c, t);
        int n = 0;
        filled = 0;
        for (int dy = 0; dy < pop.h * s; dy++)
            for (int dx = 0; dx < pop.w * s; dx++) {
                const uint16_t p = c.px[size_t(y + dy) * c.w + x + dx];
                if (p == rgb(240, 80, 80)) n++;
                if (p == rgb(20, 30, 120)) filled++;
            }
        return n;
    };
    const int before = red(1000), filled_before = filled;
    pet.show_part("pop", 2000);
    const int first = red(2000), filled_first = filled;
    const int last = red(2000 + 400), later = red(9000);
    pet.show_part("", 0);
    const int after = red(9100);
    check(pop.fill && filled_before == 0 && filled_first + first == pop.w * pop.h * s * s && filled == 0,
          "its fill paints the rest of its box while it shows, and is gone after");
    check(before == 0 && after == 0, "a cued part is hidden until a step shows it, and after");
    check(first > 0 && last > first && later == last,
          "shown, it plays once from the step's start and holds its last frame");

    // "during": it also shows while its look plays, from when the look begins, and not on the way to it.
    check(pop.during == "type_fast", "a part's look is read from the egg");
    uint32_t t = 10000;
    pet.want(Look::TypeFast, t);
    bool hidden_on_the_way = true;
    for (; t < 40000; t += 20) {
        const int n = red(t);
        if (std::string(pet.phase_label()) == "acting") break;
        if (n > 0) hidden_on_the_way = false;
    }
    const uint32_t began = t;
    const int at_start = red(t), held = red(began + 2000);
    pet.want(Look::Idle, began + 2000);
    int gone = -1;
    for (t = began + 2000; t < began + 20000 && gone != 0; t += 20) gone = red(t);
    check(hidden_on_the_way && at_start > 0 && held >= at_start && gone == 0,
          "a part shown during a look plays while the look plays, and only then");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: egg_test EGG [AMBIENT_EGG]\n");
        return 2;
    }
    const std::vector<uint8_t> bytes = test::read_file(argv[1]);
    Egg e;
    std::string err;
    if (!check(load_egg(bytes, e, err), "it loads " + err)) return test::summary();
    words_and_scene(e);
    daypart_fallbacks(e.scene);
    if (argc > 2) {
        const std::vector<uint8_t> moving = test::read_file(argv[2]);
        Egg m;
        std::string why;
        if (check(load_egg(moving, m, why), "the test pet with moving parts loads " + why)) {
            ambient(m.scene);
            if (m.scene.ambient.size() == 4) cued(m);
        }
    }
    gauges(bytes, e);
    broken_files(bytes);
    bad_descriptions(bytes);
    pace(bytes, e);
    release(bytes);
    return test::summary();
}
