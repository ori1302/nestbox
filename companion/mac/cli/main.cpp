// nestbox: the Mac companion for the NestBox desk pet.
//
//   nestbox [--device URL] [--id ID] [--name NAME] [--print] [--machine] [--no-keys] [--no-idle] [--no-lock]
//       Reports the Mac's activity to the pet (see activity.h). It finds the pet by Bonjour: --id picks the
//       paired pet by its TXT id at its current address (default: the id in ~/.nestbox/device), --name by Bonjour
//       instance name; --device uses one fixed URL instead. --print echoes each body with the pet's reply;
//       --machine adds tagged JSON lines for the Mac app.
//   nestbox pair [--device URL] [--machine]   gets the pet's key; confirm with its button (see commands.h)
//   nestbox find [--machine] [NAME]   lists pets on the network
#include <csignal>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "activity.h"
#include "commands.h"

namespace {

constexpr const char* USAGE =
    "usage: nestbox [--device URL] [--id ID] [--name NAME] [--print] [--machine] [--no-keys] [--no-idle] "
    "[--no-lock] | pair [--device URL] [--machine] | find [--machine] [NAME]\n";

bool parse_options(const std::vector<std::string>& args, companion::CompanionOptions& o) {
    const std::map<std::string, bool companion::CompanionOptions::*> switches = {
        {"--print", &companion::CompanionOptions::print}, {"--machine", &companion::CompanionOptions::machine}};
    const std::map<std::string, bool companion::CompanionOptions::*> signals_off = {
        {"--no-keys", &companion::CompanionOptions::keys}, {"--no-idle", &companion::CompanionOptions::idle},
        {"--no-lock", &companion::CompanionOptions::lock}};
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        const bool has_value = i + 1 < args.size();
        if (a == "--device" && has_value) {
            o.device = args[++i];
        } else if (a == "--name" && has_value) {
            o.name = args[++i];
        } else if (a == "--id" && has_value) {
            o.id = args[++i];
            o.id_given = true;
        } else if (switches.count(a)) {
            o.*switches.at(a) = true;
        } else if (signals_off.count(a)) {
            o.*signals_off.at(a) = false;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);    // a closed connection shows up as a send error instead
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "pair") {
        std::string device;
        bool machine = false;
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--device" && i + 1 < args.size()) device = args[++i];
            else if (args[i] == "--machine") machine = true;
            else {
                std::fputs(USAGE, stderr);
                return 2;
            }
        }
        return companion::run_pair(device, machine);
    }
    if (!args.empty() && args[0] == "find") {
        const bool machine = args.size() > 1 && args[1] == "--machine";
        const size_t name_at = machine ? 2 : 1;
        return companion::run_find(args.size() > name_at ? args[name_at] : "", machine);
    }
    companion::CompanionOptions options;
    if (!parse_options(args, options)) {
        std::fputs(USAGE, stderr);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    companion::run_companion(options);
}
