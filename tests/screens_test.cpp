// Every setup screen, on every screen profile, for each pet and the longest or oddest text it may show, checked for
// layout: nothing overlaps, everything stays 6 px inside the visible area (on a round screen, the circle), and no
// text is cut or broken mid-word. Each screen is also written as a PPM image for review.
// Usage: screens_test PETS_DIR OUT_DIR
#include <cstdio>
#include <filesystem>
#include <string>
#include "check.h"
#include "door/setup.h"

using namespace nestbox;
using test::check;
namespace fs = std::filesystem;

namespace {

constexpr int EDGE_MARGIN = 6;  // px from the visible edge
constexpr int GAP = 3;          // px between any two items

void save_ppm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        test::fail("cannot write " + path);
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    for (uint16_t p : c.px) {
        const uint32_t v = rgb888(p);
        const uint8_t px[3] = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
        std::fwrite(px, 1, 3, f);
    }
    std::fclose(f);
}

void check_layout(const std::string& name, const Layout& l, const Display& d) {
    for (size_t i = 0; i < l.items.size(); i++) {
        const auto& a = l.items[i];
        check(a.fits, name + ": cut or broken: " + a.what);
        check(d.visible(a.box, EDGE_MARGIN), name + ": off the screen: " + a.what + " at " + std::to_string(a.box.x) +
                                                 "," + std::to_string(a.box.y) + " " + std::to_string(a.box.w) + "x" +
                                                 std::to_string(a.box.h));
        const Box grown{a.box.x - GAP, a.box.y - GAP, a.box.w + 2 * GAP, a.box.h + 2 * GAP};
        for (size_t j = i + 1; j < l.items.size(); j++)
            check(!grown.overlaps(l.items[j].box), name + ": \"" + a.what + "\" touches \"" + l.items[j].what + "\"");
    }
}

std::vector<Egg> load_eggs(const fs::path& dir) {
    std::vector<Egg> eggs;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".egg") continue;
        Egg e;
        if (test::load_egg_file(entry.path().string(), e)) eggs.push_back(std::move(e));
    }
    return eggs;
}

struct Case {
    const char* tag;
    std::string ssid, ip, host;
    Join why;
};

const Case CASES[] = {
    {"plain", "Home", "192.168.1.37", "goose-7k3q", Join::WrongPassword},
    {"long", "TheVeryLongNetworkNameForTesting", "192.168.100.200", "nestbox-attic-7k3q", Join::NotFound},
    {"hebrew", "\xd7\x94\xd7\x91\xd7\x99\xd7\xaa \xd7\xa9\xd7\x9c\xd7\xa0\xd7\x95", "10.0.0.5", "", Join::NoAddress},
    {"weak", "Neighbours 2.4", "172.16.254.254", "desk-2f9a", Join::Failed},
    {"login", "Office", "192.168.1.2", "nestbox-3f2a", Join::NeedsLogin},
};

struct Screen {
    Setup::Phase phase;
    const char* name;
};

const Screen SCREENS[] = {
    {Setup::Phase::Hello, "hello"}, {Setup::Phase::ShowCode, "join"},     {Setup::Phase::Joined, "joined"},
    {Setup::Phase::Page, "page"},   {Setup::Phase::Checking, "checking"}, {Setup::Phase::Failed, "failed"},
    {Setup::Phase::AllSet, "allset"},
};

constexpr uint32_t FIRST_MS = 500;

// Joined and Checking change once their hint appears, so they are also shown after it.
bool has_later_view(Setup::Phase p) { return p == Setup::Phase::Joined || p == Setup::Phase::Checking; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: screens_test PETS_DIR OUT_DIR\n");
        return 2;
    }
    const std::vector<Egg> eggs = load_eggs(argv[1]);
    if (!check(!eggs.empty(), std::string("eggs in ") + argv[1])) return 1;
    fs::create_directories(argv[2]);
    int shots = 0;
    for (auto& d : displays())
        for (auto& egg : eggs)
            for (auto& k : CASES)
                for (auto& screen : SCREENS)
                    for (uint32_t at : {FIRST_MS, Setup::HINT_MS + FIRST_MS}) {
                        const bool later = at > FIRST_MS;
                        if (later && !has_later_view(screen.phase)) continue;
                        Setup s;
                        s.preview(screen.phase, egg.name, k.ssid, "X8MW4R9K", k.why, k.ip, k.host);
                        Canvas c(d.w, d.h);
                        Layout l;
                        s.set_display(d);
                        s.render(c, &egg, at, &l);
                        std::string name =
                            d.id + "-" + egg.name + "-" + k.tag + "-" + screen.name + (later ? "-later" : "");
                        for (auto& ch : name)
                            if (ch == ' ') ch = '_';
                        check_layout(name, l, d);
                        save_ppm(c, std::string(argv[2]) + "/" + name + ".ppm");
                        shots++;
                    }
    // Setup run again after the Wi-Fi stopped working, and BOOT held down: early and late in the start-over countdown,
    // then early and late in the reset-everything one.
    const uint32_t holds[] = {Setup::HOLD_SHOW_MS + 100, Setup::START_OVER_MS - 100, Setup::START_OVER_MS + 100,
                              Setup::RESET_ALL_MS - 100};
    const char* hold_tags[] = {"hold", "hold-late", "hold-reset", "hold-reset-late"};
    for (auto& d : displays())
        for (auto& egg : eggs)
            for (int view = 0; view < 5; view++) {
                Setup s;
                s.preview(view == 0 ? Setup::Phase::Hello : Setup::Phase::Off, egg.name, "Home", "X8MW4R9K",
                          Join::Failed, "", "", true);
                const uint32_t at = 30000;
                if (view > 0) s.preview_hold(holds[view - 1], at);
                Canvas c(d.w, d.h);
                Layout l;
                s.set_display(d);
                check(s.render(c, &egg, at, &l), "the screen is setup's");
                const char* tag = view == 0 ? "again-hello" : hold_tags[view - 1];
                std::string name = d.id + "-" + egg.name + "-" + tag;
                for (auto& ch : name)
                    if (ch == ' ') ch = '_';
                check_layout(name, l, d);
                save_ppm(c, std::string(argv[2]) + "/" + name + ".ppm");
                shots++;
            }
    std::printf("%d screens\n", shots);
    return test::summary();
}
