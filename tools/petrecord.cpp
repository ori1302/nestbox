// Records a pet as the device would show it, frame by frame, to check its looks and pastimes before they ship:
//
//   petrecord EGG list                         the looks and the pet's pastimes
//   petrecord EGG look LOOK [options]          hold a look (as the simulator's keys do)
//   petrecord EGG pastime NAME [options]       play one of its pastimes now, through to its end (as the P key does)
//
// Options: --out DIR      save every recorded frame there as fNNNN.ppm (else only the timeline)
//          --seconds S    how long a look is recorded (default 6); a pastime runs to its end (at most 3 minutes)
//          --every MS     record every this many ms (default 33; the device draws 30 frames a second)
//          --hour H       the time of day, 0-23 (default 14), which picks the scene's picture
//          --display ID   the screen (default lcd-1.69; also lcd-2, square-240, round-240, amoled-2.41)
//
// The timeline goes to stdout as CSV, one line a recorded frame: ms from the start, the scene part shown and ms
// since it began (a pastime's cued part), the feet and the top of the head (screen pixels), the facing row, the
// phase, the animation playing, its frame, and the saved file.
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "body/looks.h"
#include "body/pet.h"
#include "host/frame.h"
#include "mind/brain.h"
#include "screen/canvas.h"
#include "screen/display.h"

using namespace nestbox;

namespace {

constexpr uint32_t START_MS = 1000;
constexpr uint32_t MAX_PASTIME_MS = 180000;
constexpr uint32_t SETTLE_MS = 600;   // a moment standing before a look or pastime is asked for

int usage() {
    std::fprintf(stderr,
                 "usage: petrecord EGG list\n"
                 "       petrecord EGG look LOOK [--out DIR] [--seconds S] [--every MS] [--hour H] [--display ID]\n"
                 "       petrecord EGG pastime NAME [--out DIR] [--every MS] [--hour H] [--display ID]\n");
    return 2;
}

bool save_ppm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    std::vector<unsigned char> row(size_t(c.w) * 3);
    for (int y = 0; y < c.h; y++) {
        for (int x = 0; x < c.w; x++) {
            const uint16_t p = c.px[size_t(y) * c.w + x];
            row[size_t(x) * 3] = uint8_t((p >> 11) << 3);
            row[size_t(x) * 3 + 1] = uint8_t(((p >> 5) & 63) << 2);
            row[size_t(x) * 3 + 2] = uint8_t((p & 31) << 3);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    return std::fclose(f) == 0;
}

std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Egg egg;
    std::string error;
    if (bytes.empty() || !load_egg(std::move(bytes), egg, error)) {
        std::fprintf(stderr, "%s: %s\n", argv[1], error.empty() ? "cannot read it" : error.c_str());
        return 1;
    }
    const std::string what = argv[2];
    if (what == "list") {
        std::printf("%s\nlooks (what each plays: eggpack.py FOLDER --check):\n ", egg.name.c_str());
        for (int l = 0; l < int(Look::Act); l++) std::printf(" %s", look_key(Look(l)));
        std::printf("\npastimes:\n");
        for (const Pastime& p : egg.pastimes) std::printf("  %s\n", p.name.c_str());
        return 0;
    }
    if ((what != "look" && what != "pastime") || argc < 4) return usage();
    const std::string name = argv[3];
    std::string out, display_id = "lcd-1.69";
    double seconds = 6;
    int every = 1000 / FPS, hour = 14;
    for (int i = 4; i < argc; i++) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        const char* v = argv[++i];
        if (a == "--out") out = v;
        else if (a == "--seconds") seconds = std::atof(v);
        else if (a == "--every") every = std::max(1, std::atoi(v));
        else if (a == "--hour") hour = std::atoi(v) % 24;
        else if (a == "--display") display_id = v;
        else return usage();
    }
    const Display* display = display_named(display_id);
    if (!display) {
        std::fprintf(stderr, "no display %s\n", display_id.c_str());
        return 2;
    }
    Look look = Look::Idle;
    if (what == "look" && !look_named(name, look)) {
        std::fprintf(stderr, "no look %s; petrecord EGG list shows them\n", name.c_str());
        return 2;
    }
    if (what == "pastime") {   // only this one, so the P key starts it
        std::vector<Pastime> kept;
        for (Pastime& p : egg.pastimes)
            if (p.name == name) kept.push_back(std::move(p));
        if (kept.empty()) {
            std::fprintf(stderr, "%s has no pastime \"%s\"; petrecord EGG list shows them\n", egg.name.c_str(),
                         name.c_str());
            return 2;
        }
        egg.pastimes = std::move(kept);
        if (egg.throw_kit.plays) egg.has_throw = false;   // its game would come first
    }
    if (!out.empty()) mkdir(out.c_str(), 0755);

    Brain brain;
    brain.clock(int64_t(hour) * 3600, START_MS);
    Pet pet;
    pet.set_display(*display);
    pet.daypart(brain.daypart(START_MS));   // the hour's picture from the first frame, not a fade into it
    pet.set_egg(&egg);
    Canvas canvas(display->w, display->h);
    const uint32_t step = 1000 / FPS;
    const uint32_t begin = START_MS + SETTLE_MS;
    const uint32_t end = what == "look" ? begin + uint32_t(seconds * 1000) : begin + MAX_PASTIME_MS;
    const std::string life = "its own life: " + name;
    bool started = false;
    int saved = 0;
    std::printf("ms,part,part_ms,feet_x,feet_y,head_x,head_y,row,phase,anim,frame,file\n");
    for (uint32_t t = START_MS, next_shot = begin; t < end; t += step) {
        if (t >= begin && !started) {
            started = true;
            if (what == "look") brain.hold(look);
            else brain.next_pastime(t);
        }
        const Look s = brain.decide(t);
        brain.show(pet, t);
        pet.want(s, t);
        pet.update(t);
        if (what == "pastime" && started && brain.why() != life) break;   // played through
        if (t < next_shot) continue;
        next_shot += uint32_t(every);
        pet.render(canvas, t);
        std::string shot;
        if (!out.empty()) {
            char n[32];
            std::snprintf(n, sizeof n, "f%04d.ppm", saved++);
            shot = n;
            if (!save_ppm(canvas, out + "/" + shot)) {
                std::fprintf(stderr, "cannot write %s/%s\n", out.c_str(), n);
                return 1;
            }
        }
        const Point head = pet.drawn_head();
        const bool part = !pet.shown_part().empty();
        std::printf("%u,%s,%s,%.1f,%.1f,%.0f,%.0f,%d,%s,%s,%d,%s\n", t - begin, csv_field(pet.shown_part()).c_str(),
                    part ? std::to_string(t - pet.shown_since()).c_str() : "", pet.x(), pet.y(), head.x, head.y,
                    pet.row(), pet.phase_label(), csv_field(pet.playing()).c_str(), pet.drawn_frame(), shot.c_str());
    }
    return 0;
}
