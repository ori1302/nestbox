#include "body/shelf.h"
#include <algorithm>
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
    if (w == "next") {
        index = (current_ + 1) % eggs_.size();
        return true;
    }
    if (w == "previous" || w == "prev") {
        index = (current_ + eggs_.size() - 1) % eggs_.size();
        return true;
    }
    for (size_t i = 0; i < eggs_.size(); i++)
        if (ascii_lower(ids_[i]) == w || ascii_lower(eggs_[i]->name) == w) {
            index = i;
            return true;
        }
    return false;
}

void Shelf::put_out(size_t index, Pet& pet, const Canvas& screen, uint32_t now) {
    if (index >= eggs_.size()) return;
    old_ = screen;
    fade_start_ = now;
    fading_ = true;
    current_ = index;
    pet.set_egg(eggs_[current_].get());
    if (on_change) on_change(ids_[current_]);
}

size_t Shelf::put(Egg&& egg, const std::string& id, Pet& pet, const Canvas& screen, uint32_t now) {
    const auto at = std::find(ids_.begin(), ids_.end(), id);
    if (at == ids_.end()) {
        add(std::move(egg), id);
        if (eggs_.size() == 1) put_out(0, pet, screen, now);
        return eggs_.size() - 1;
    }
    const size_t i = size_t(at - ids_.begin());
    std::unique_ptr<Egg> old = std::move(eggs_[i]);    // freed once the pet no longer points at it
    eggs_[i] = std::make_unique<Egg>(std::move(egg));
    if (i == current_) put_out(i, pet, screen, now);
    return i;
}

bool Shelf::remove(size_t index, Pet& pet, const Canvas& screen, uint32_t now) {
    if (index >= eggs_.size() || eggs_.size() < 2) return false;
    if (index == current_) put_out((index + 1) % eggs_.size(), pet, screen, now);
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
