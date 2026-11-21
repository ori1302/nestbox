#include "log.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
constexpr size_t MAX_LINE = 512;
constexpr size_t MAX_PENDING = 8192;         // with nobody reading, later lines are dropped

std::mutex mu;
std::string pending;
}  // namespace

void note(const char* fmt, ...) {
    char line[MAX_LINE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(mu);
    if (pending.size() < MAX_PENDING) pending += line;
}

std::string drain_log() {
    std::string out;
    std::lock_guard<std::mutex> lock(mu);
    out.swap(pending);
    return out;
}
