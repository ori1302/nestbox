// The setup guide for coding agents (GET /agents and /llms.txt) and the pet descriptions behind GET /v1/pets.
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include "body/shelf.h"
#include "screen/display.h"
#include "util/json.h"

namespace nestbox {

// Markdown: how to tell the pet at `base_url` what is going on, then a section per pet on the device, shown on `d`.
// `locked`: every request needs the key.
std::string guide(const std::string& base_url, const Shelf& pets, const Display& d, bool locked);
// The pets on the device and which is out (GET /v1/pets); `more` writes fields of its own after the list.
std::string pets_json(const Shelf& pets, const std::function<void(JsonWriter&)>& more = {});
// A point as [x, y], to a tenth.
void write_point(JsonWriter& w, Point p);
// One pet: its words, scene, the behaviours it acts out itself, its animations, its throw, its places, the part of
// its scene the screen shows, its floor (GET /v1/pets/NAME). Points are percent of the scene.
std::string pet_json(const Shelf& pets, size_t i, const Display& d);

}  // namespace nestbox
