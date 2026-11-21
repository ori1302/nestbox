// The pets a device holds, which one is out, and switching between them with a short cross-fade.
// The platform loads the eggs (files on the Mac, flash on the ESP32) and remembers the choice.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "body/pet.h"
#include "egg/egg.h"
#include "screen/canvas.h"

namespace nestbox {

class Shelf {
public:
    static constexpr uint32_t FADE_MS = 600;

    // id: a short lower-case handle, e.g. "nibble" (the egg's file name). Eggs keep their address while they are on
    // the shelf (Pet holds a pointer to the one that is out).
    void add(Egg&& egg, const std::string& id) {
        eggs_.push_back(std::make_unique<Egg>(std::move(egg)));
        ids_.push_back(id);
    }
    // Adds the pet, or puts it in place of the one with the same id; a replaced pet that was out comes out again as
    // the new one. Returns its index.
    size_t put(Egg&& egg, const std::string& id, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // Takes a pet off the shelf. Not the last one; if it is out, the next comes out first. False if it can't.
    bool remove(size_t index, Pet& pet, const Canvas& screen, uint32_t now_ms);
    bool has(const std::string& id) const;
    // The pet with exactly this id; none if there is none.
    std::optional<size_t> index_of(const std::string& id) const;
    size_t size() const { return eggs_.size(); }
    const std::string& id(size_t i) const { return ids_[i]; }
    const Egg& egg(size_t i) const { return *eggs_[i]; }
    size_t current() const { return current_; }
    // The pet that is out; the shelf must not be empty.
    const Egg& current_egg() const { return *eggs_[current_]; }
    // The pet that is out, or nullptr on an empty shelf.
    const Egg* pet_out() const { return eggs_.empty() ? nullptr : eggs_[current_].get(); }

    // A pet by id or name (any case), or "next" / "previous". False if there is no such pet.
    bool find(const std::string& which, size_t& index) const;
    // Brings this pet out; the picture on `screen` now fades into the new pet over FADE_MS.
    void put_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // After rendering a frame: blends in the old picture while a switch is fading.
    void fade(Canvas& screen, uint32_t now_ms) const;

    std::function<void(const std::string& id)> on_change;   // a pet was put out
    std::function<void()> rescan;                            // look for new eggs (asked for an unknown pet)

private:
    std::deque<std::unique_ptr<Egg>> eggs_;
    std::vector<std::string> ids_;
    size_t current_ = 0;
    Canvas old_{0, 0};                   // the picture being faded out, as big as the screen
    uint32_t fade_start_ = 0;
    bool fading_ = false;
};

}  // namespace nestbox
