// The device's own page, at its address: its pets, its screen and how to reach it. The page is served whole; its
// script reads and changes everything through the HTTP API (GET /v1, /v1/pets; POST /v1; PUT and DELETE /v1/pets).
#pragma once
#include <string>

namespace nestbox {

// `name`: the device's name (given in setup, else its pet's), not escaped; `picture`: the pet out, as SVG;
// `name_svg`: the name in the screen's font (door/pet_svg.h), or "" to set it in the page's own type.
std::string device_page(const std::string& name, const std::string& picture, const std::string& name_svg);

std::string html_escape(const std::string& s);

}  // namespace nestbox
