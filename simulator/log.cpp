#include "log.h"
#include <cstdarg>
#include <cstdio>

void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::fflush(stdout);
}

namespace {
bool machine_lines = false;
}  // namespace

void set_machine_lines(bool on) { machine_lines = on; }

void machine_line(const std::string& json) {
    if (machine_lines) note("%s%s\n", MACHINE_TAG, json.c_str());
}
