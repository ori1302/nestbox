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

    // Reads a pet's egg again by its id. A platform with room for few whole eggs sets it: the shelf then keeps only
    // the pet that is out whole and the rest slim (slim_egg), and loads a pet when it comes out.
    std::function<bool(const std::string& id, Egg& out)> load;
    // With load_later, a slim pet asked for (bring_out) is read on the platform's own time, outside whatever it holds
    // while a request runs (the board: its lock), so the pet out keeps moving meanwhile: it is `coming` until the
    // platform hands its egg to `arrive`.
    bool load_later = false;

    // Runs a step that allocates an egg's memory (loading one, slimming one); a platform can choose where that memory
    // comes from (the board: PSRAM, keeping internal RAM for Wi-Fi). Unset, the step just runs.
    std::function<void(const std::function<void()>& step)> egg_memory;

    // id: a short lower-case handle, e.g. "nibble" (the egg's file name). Eggs keep their address while they are on
    // the shelf (Pet holds a pointer to the one that is out). With `load`, it is kept slim until it comes out.
    void add(Egg&& egg, const std::string& id) {
        in_egg_memory([&] {
            eggs_.push_back(std::make_unique<Egg>(std::move(egg)));
            if (load) slim_egg(*eggs_.back());
        });
        ids_.push_back(id);
    }
    // Adds the pet, or puts it in place of the one with the same id; a replaced pet that was out comes out again as
    // the new one. Returns its index.
    size_t put(Egg&& egg, const std::string& id, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // Takes a pet off the shelf. Not the last one; if it is out, the next comes out first. False if it can't (the
    // next one can't be loaded either).
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
    // Brings this pet out; the picture on `screen` now fades into the new pet over FADE_MS. False, and the pet out
    // stays, if its egg can't be loaded.
    bool put_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // Brings this pet out as asked for (by a person, a sender): now, or with load_later and a slim pet, once its egg
    // arrives. False if it can't (its egg can't be loaded now). Asking for the pet out stops one coming.
    bool bring_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // The id of the pet coming out, waiting for its egg; empty when none.
    const std::string& coming() const { return coming_; }
    // The coming pet's egg, read: it comes out. False, and the egg is dropped, if that pet is no longer coming (another
    // was asked for, or it came off the shelf) or the egg is no use.
    bool arrive(const std::string& id, Egg&& egg, Pet& pet, const Canvas& screen, uint32_t now_ms);
    // Its egg couldn't be read: it stays where it is.
    void give_up(const std::string& id) {
        if (id == coming_) coming_.clear();
    }

    // After rendering a frame: blends in the old picture while a switch is fading.
    void fade(Canvas& screen, uint32_t now_ms) const;

    std::function<void(const std::string& id)> on_change;   // a pet was put out
    std::function<void()> rescan;                            // look for new eggs (asked for an unknown pet)

private:
    void in_egg_memory(const std::function<void()>& step) {
        if (egg_memory) egg_memory(step);
        else step();
    }

    std::deque<std::unique_ptr<Egg>> eggs_;
    std::vector<std::string> ids_;
    size_t current_ = 0;
    std::string coming_;
    Canvas old_{0, 0};                   // the picture being faded out, as big as the screen
    uint32_t fade_start_ = 0;
    bool fading_ = false;
};

}  // namespace nestbox
