// Restoring the scene picture slice by slice (Canvas::held, screen/slices.h) draws exactly what restoring it whole
// does: two devices live through the same requests, one drawing each frame from scratch, the other as the firmware
// does, into two buffers in turn, each with the fingerprints its last frame left. Every frame of every pet on every
// screen must match pixel for pixel, through walks, typing, effects, captions, gauges and time-of-day fades.
// Usage: slices_test EGG...
#include <cstdio>
#include <string>
#include "check.h"
#include "door/api.h"
#include "host/eggs.h"
#include "host/frame.h"
#include "screen/display.h"
#include "screen/slices.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t FRAME_MS = 33;

struct Twin {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen;
    Device device;
    Twin(const Egg& egg, const Display& d)
        : screen(d.w, d.h), device(brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test") {
        shelf.add(test::reloaded(egg), "pet");
        device.random = [n = 0u]() mutable { return n += 2654435761u; };
        put_out_first(device, d, "", 0);
    }
    void tell(const std::string& body, uint32_t now) { handle_request(device, "POST", "/v1/tell", body, now, "", "", true); }
    void device_post(const std::string& body, uint32_t now) { handle_request(device, "POST", "/v1", body, now, "", "", true); }
};

// What the firmware's sender leaves with a buffer: its slice fingerprints.
std::vector<uint32_t> fingerprints(const Canvas& c) {
    std::vector<uint32_t> held(size_t(c.h) * SLICES);
    const int sw = c.w / SLICES;
    for (int y = 0; y < c.h; y++)
        for (int k = 0; k < SLICES; k++)
            held[size_t(y) * SLICES + k] = slice_hash(c.px.data() + size_t(y) * c.w + size_t(k) * sw, size_t(sw));
    return held;
}

}  // namespace

int main(int argc, char** argv) {
    const char* const STEPS[] = {
        R"({"from":"t","do":"stroll"})",
        R"({"from":"t","state":"working","do":"type","line":"a line that scrolls along the status bar for a while"})",
        R"({"from":"t","do":"dance","effect":"confetti","say":"Hello from the test!"})",
        R"({"from":"t","caption":"A caption along the bottom","gauge":40,"label":"half"})",
        R"({"from":"t","do":"throw"})",
        R"({"from":"t","do":"run","effect":"sparkles"})",
        R"({"from":"t","state":"over"})",
    };
    int frames = 0;
    for (int a = 1; a < argc; a++) {
        Egg egg;
        if (!test::load_egg_file(argv[a], egg)) continue;
        for (const Display& d : displays()) {
            Twin whole(egg, d), sliced(egg, d);
            Canvas other(d.w, d.h);                       // the second buffer, as the firmware's `sending`
            uint32_t now = 0;
            int step = 0, bad = 0;
            for (int f = 0; f < 1500 && !bad; f++, now += FRAME_MS) {
                if (f % 200 == 100) {
                    whole.tell(STEPS[step % 7], now);
                    sliced.tell(STEPS[step % 7], now);
                    step++;
                }
                if (f == 700 || f == 1100) {              // a time-of-day fade, then back
                    const std::string body = f == 700 ? R"({"time":"22:30"})" : R"({"time":"10:00"})";
                    whole.device_post(body, now);
                    sliced.device_post(body, now);
                }
                step_frame(whole.device, now);
                step_frame(sliced.device, now);
                if (whole.screen.px != sliced.screen.px) {
                    size_t i = 0;
                    while (whole.screen.px[i] == sliced.screen.px[i]) i++;
                    check(false, d.id + " " + egg.name + ": frame " + std::to_string(f) + " differs first at " +
                                     std::to_string(i % size_t(d.w)) + "," + std::to_string(i / size_t(d.w)));
                    bad++;
                }
                frames++;
                // Sent: the buffer goes to the panel and comes back with its fingerprints; the other one is drawn next.
                sliced.screen.held = fingerprints(sliced.screen);
                sliced.screen.px.swap(other.px);
                sliced.screen.held.swap(other.held);
            }
        }
    }
    check(frames > 0, "some frames were drawn");
    std::printf("%d frames compared\n", frames);
    return test::summary();
}
