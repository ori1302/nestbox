#include "body/shelf.h"
#include <algorithm>
#include <utility>
#include "util/text.h"

namespace nestbox {

bool Shelf::has(const std::string& id) const { return index_of(id).has_value(); }

std::optional<size_t> Shelf::index_of(const std::string& id) const {
    const auto at = std::find(ids_.begin(), ids_.end(), id);
    return at == ids_.end() ? std::nullopt : std::optional<size_t>(size_t(at - ids_.begin()));
}

bool Shelf::find(const std::string& which, size_t& index) const {
    if (eggs_.empty()) return false;
    const std::string w = ascii_lower(which);
    const size_t from = coming_.empty() ? current_ : index_of(coming_).value_or(current_);   // "next" twice: two on
    if (w == "next") {
        index = (from + 1) % eggs_.size();
        return true;
    }
    if (w == "previous" || w == "prev") {
        index = (from + eggs_.size() - 1) % eggs_.size();
        return true;
    }
    for (size_t i = 0; i < eggs_.size(); i++)
        if (ascii_lower(ids_[i]) == w || ascii_lower(eggs_[i]->name) == w) {
            index = i;
            return true;
        }
    return false;
}

bool Shelf::put_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now) {
    if (index >= eggs_.size()) return false;
    if (eggs_[index]->slim) {
        bool loaded = false;
        in_egg_memory([&] {
            auto whole = std::make_unique<Egg>();
            if (!load || !load(ids_[index], *whole) || whole->slim) return;
            eggs_[index] = std::move(whole);
            loaded = true;
        });
        if (!loaded) return false;
    }
    const size_t before = current_;
    old_ = screen;
    fade_start_ = now;
    fading_ = true;
    current_ = index;
    pet.set_egg(eggs_[current_].get());
    if (load && before != current_ && before < eggs_.size()) in_egg_memory([&] { slim_egg(*eggs_[before]); });
    if (on_change) on_change(ids_[current_]);
    return true;
}

bool Shelf::bring_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now) {
    if (index >= eggs_.size()) return false;
    coming_.clear();
    if (index == current_) return true;
    if (load_later && eggs_[index]->slim) {
        coming_ = ids_[index];
        return true;
    }
    return put_out(index, pet, screen, now);
}

bool Shelf::arrive(const std::string& id, Egg&& egg, Pet& pet, const Canvas& screen, uint32_t now) {
    if (id.empty() || id != coming_) return false;
    coming_.clear();
    const std::optional<size_t> i = index_of(id);
    if (!i || egg.slim) return false;
    if (eggs_[*i]->slim) in_egg_memory([&] { eggs_[*i] = std::make_unique<Egg>(std::move(egg)); });
    return put_out(*i, pet, screen, now);
}

size_t Shelf::put(Egg&& egg, const std::string& id, Pet& pet, const Canvas& screen, uint32_t now) {
    const auto at = std::find(ids_.begin(), ids_.end(), id);
    const bool added = at == ids_.end();
    const size_t i = added ? eggs_.size() : size_t(at - ids_.begin());
    std::unique_ptr<Egg> old;                          // a replaced egg, freed once the pet no longer points at it
    in_egg_memory([&] {
        auto put = std::make_unique<Egg>(std::move(egg));
        if (added) eggs_.push_back(std::move(put));
        else old = std::exchange(eggs_[i], std::move(put));
    });
    if (added) ids_.push_back(id);
    if (eggs_.size() == 1 || (!added && i == current_)) put_out(i, pet, screen, now);
    else if (load) in_egg_memory([&] { slim_egg(*eggs_[i]); });
    return i;
}

bool Shelf::remove(size_t index, Pet& pet, const Canvas& screen, uint32_t now) {
    if (index >= eggs_.size() || eggs_.size() < 2) return false;
    if (ids_[index] == coming_) coming_.clear();
    if (index == current_ && !put_out((index + 1) % eggs_.size(), pet, screen, now)) return false;
    eggs_.erase(eggs_.begin() + std::ptrdiff_t(index));
    ids_.erase(ids_.begin() + std::ptrdiff_t(index));
    if (current_ > index) current_--;
    return true;
}

void Shelf::fade(Canvas& screen, uint32_t now) const {
    if (!fading_ || now - fade_start_ >= FADE_MS || old_.px.size() != screen.px.size()) return;
    const int k = int(32 * (now - fade_start_) / FADE_MS);
    for (size_t i = 0; i < screen.px.size(); i++) screen.px[i] = blend(old_.px[i], screen.px[i], k);
    const Egg& now_out = *eggs_[current_];
    if (!now_out.screen_lut.empty()) screen.recolour(now_out.screen_lut);   // the blend keeps to its few colours
}

}  // namespace nestbox
