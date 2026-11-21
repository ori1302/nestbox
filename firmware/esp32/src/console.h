// The USB serial console: reads lines and runs the commands in `help`.
#pragma once
#include <string>
#include "app.h"

// Free internal RAM and PSRAM, one line on the console.
void print_memory();

class Console {
public:
    explicit Console(App& app);
    // Reads what has arrived and runs every complete line; call often from the service task.
    void poll();

private:
    // How a command takes its argument.
    enum class Arg {
        None,          // "perf"
        Optional,      // "tz" or "tz RULE"
        Required,      // "event JSON"
        Number,        // "bright 50" (also "bright50"); digits only
    };
    struct Command {
        const char* name;
        Arg arg;
        void (Console::*run)(const std::string& arg);
    };
    static const Command COMMANDS[];

    static bool matches(const Command& c, const std::string& line, std::string& arg);
    void run(const std::string& line);
    void help(const std::string&);
    void wifi(const std::string& arg);
    void forget(const std::string&);
    void time_zone(const std::string& arg);
    void name(const std::string& arg);
    void usb(const std::string&);
    void tell(const std::string& json);
    void now(const std::string&);
    void words(const std::string&);
    void key(const std::string& arg);
    void lock(const std::string& arg);
    void get_pets(const std::string&);
    void get_api(const std::string&);
    void rotate(const std::string&);
    void bright(const std::string& percent);
    void imu(const std::string&);
    void perf(const std::string&);
    void reboot(const std::string&);

    App& app_;
    std::string line_;
};
