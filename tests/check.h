// The C++ tests' shared harness: named checks, a summary line and exit code, and egg loading.
#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "egg/egg.h"

namespace nestbox::test {

struct Tally {
    int checks = 0;
    int failures = 0;
};

inline Tally& tally() {
    static Tally t;
    return t;
}

// Records one check and prints it if it failed. Returns `ok`.
inline bool check(bool ok, const std::string& what) {
    tally().checks++;
    if (!ok) {
        tally().failures++;
        std::printf("FAIL  %s\n", what.c_str());
    }
    return ok;
}

// Records a failure found outside a check() (a broken fixture, a bad test input).
inline void fail(const std::string& what) { check(false, what); }

// Prints the totals; returns the process exit code.
inline int summary() {
    const Tally& t = tally();
    if (t.failures)
        std::printf("FAILED: %d of %d checks\n", t.failures, t.checks);
    else
        std::printf("all passed: %d checks\n", t.checks);
    return t.failures ? 1 : 0;
}

inline std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A second egg from the same file's bytes (an Egg can't be copied: its frames point into its own bytes).
inline Egg reloaded(const Egg& egg) {
    Egg copy;
    std::string err;
    load_egg(egg.bytes, copy, err);
    copy.has_throw = egg.has_throw;      // a test may have changed its kit or target in memory
    copy.throw_kit = egg.throw_kit;
    copy.scene.target_x = egg.scene.target_x;
    copy.scene.target_y = egg.scene.target_y;
    copy.scene.gauges = egg.scene.gauges;
    return copy;
}

// Loads an egg file; prints the reason and returns false if it does not load.
inline bool load_egg_file(const std::string& path, Egg& egg) {
    std::string err;
    if (load_egg(read_file(path), egg, err)) return true;
    std::printf("%s: %s\n", path.c_str(), err.c_str());
    return false;
}

}  // namespace nestbox::test
