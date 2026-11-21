#include "host/eggs.h"
#include "util/text.h"

namespace nestbox {

bool is_egg_file(const std::string& file_name) { return ends_with(file_name, EGG_EXTENSION); }

std::string egg_id(const std::string& file_name) {
    return is_egg_file(file_name) ? file_name.substr(0, file_name.size() - std::string(EGG_EXTENSION).size())
                                  : file_name;
}

bool egg_file_before(const std::string& a, const std::string& b) {
    const bool a_default = egg_id(a) == DEFAULT_PET, b_default = egg_id(b) == DEFAULT_PET;
    return a_default != b_default ? a_default : a < b;
}

void put_out_first(Device& d, const Display& display, const std::string& saved_id, uint32_t now) {
    size_t first = 0;
    d.shelf.find(saved_id, first);
    d.pet.set_display(display);
    d.shelf.put_out(first, d.pet, d.screen, now);
}

}  // namespace nestbox
