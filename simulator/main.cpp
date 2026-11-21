// NestBox simulator: the device's screen in a Mac window, serving the device's HTTP API on every interface and
// announcing it over Bonjour (_nestbox._tcp), so companions and agents talk to it as they would to the device.
//
//   nestbox-sim [--pets DIR] [--port 8080] [--name nestbox-simulator] [--headless] [--speed 1] [--setup]
//               [--display ID] [--machine]
//   nestbox-sim --displays                                            the screens it can be, as JSON
//   nestbox-sim --shot OUT.ppm PET_INDEX LOOK MS [TEXT [SIZE AT]]    one frame to an image, no window
//
// --machine adds tagged JSON lines for the Mac app (log.h).
//
// --setup starts unconfigured: the setup screens, and the phone's page at http://localhost:PORT/ (the networks are
// in sim_radio.h). Otherwise it starts on the pretend network Home. GET /screen.bmp returns the screen; POST
// /sim/router breaks or mends the pretend router.
//
// Mouse: click, double click and right click are the button's single, double and long press; drag moves the window.
// Keys: 0-9 hold a look or play a reaction (keys.h), W watch, A automatic again, S / Shift+S every look,
// D time of day, M music, P the next pastime, Tab / Shift+Tab next / previous pet, U setup again,
// R break the pretend router (new password, gone, working again), B held: the BOOT button,
// Z / Shift+Z window size, L label, Q or Esc quit.
#include <csignal>
#include <cstdio>
#include <string>
#include <vector>
#include "bonjour.h"
#include "util/json.h"
#include "files.h"
#include "http_server.h"
#include "log.h"
#include "options.h"
#include "shot.h"
#include "sim_device.h"
#include "window.h"

using namespace nestbox;
namespace fs = std::filesystem;

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

int run_shot(const Options& o, const Shelf& shelf) {
    ShotRequest r;
    if (!parse_shot(o.shot, r)) {
        std::fprintf(stderr, "usage: nestbox-sim --shot OUT.ppm PET_INDEX LOOK MS [TEXT [SIZE AT]]\n");
        return 2;
    }
    return render_shot(shelf, *o.display, r);
}

// The Mac app reads these instead of the log's wording.
void machine_failure(const char* reason, const std::string& message) {
    JsonWriter json;
    json.begin_object().field("event", "failed").field("reason", reason).field("message", message).end_object();
    machine_line(json.text());
}

void machine_listening(const std::string& url, int port) {
    JsonWriter json;
    json.begin_object().field("event", "listening").field("url", url).field("port", port).end_object();
    machine_line(json.text());
}

int run_device(const Options& o, Shelf& shelf, const std::vector<fs::path>& egg_dirs) {
    SimDevice device(o, shelf, egg_dirs);
    HttpServer server;
    std::string err;
    if (!server.start(o.port, [&](const HttpRequest& q) { return device.serve(q); }, err)) {
        std::fprintf(stderr, "cannot listen: %s\n", err.c_str());
        machine_failure("cannot_listen", err);
        return 1;
    }
    Bonjour bonjour;
    auto announce = [&](const std::string& pet) {
        return bonjour.announce(o.name, server.port(), bonjour_txt(pet, device.id(), short_hostname() + ".local"));
    };
    device.on_pet_change(announce);
    const bool announced = announce(device.pet_name());
    const std::string host = local_hostname();
    const std::string base_url = "http://" + host + ":" + std::to_string(server.port());
    device.set_base_url(base_url);
    note("%s listening on %s  (Bonjour %s.%s.%s: %s)\n", o.name.c_str(), base_url.c_str(), o.name.c_str(),
         SERVICE_TYPE, SERVICE_PROTOCOL, announced ? "announced" : "FAILED");
    machine_listening(base_url, server.port());
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);           // a write to a closed socket returns an error instead of a signal

    int code = 0;
    if (o.headless) {
        FramePacer pacer;
        while (!g_stop) {
            device.step();
            pacer.wait();
        }
    } else {
        SimWindow window(device);
        if (window.open()) window.run([] { return g_stop != 0; });
        else code = 1;
    }
    bonjour.withdraw();
    server.stop();
    return code;
}
}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (const int code = parse_options(argc, argv, o); code >= 0) return code;
    set_machine_lines(o.machine);
    const std::vector<fs::path> egg_dirs = {o.pets_dir, own_eggs_dir()};
    Shelf shelf;
    for (const auto& dir : egg_dirs) load_egg_folder(dir, shelf);
    if (!shelf.size()) {
        const std::string message = "no pet eggs (.egg) in " + egg_dirs[0].string() + " or " + egg_dirs[1].string();
        std::fprintf(stderr, "%s\n", message.c_str());
        machine_failure("no_eggs", message);
        return 1;
    }
    if (!o.shot.empty()) return run_shot(o, shelf);
    return run_device(o, shelf, egg_dirs);
}
