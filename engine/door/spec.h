// The pet format's contract as JSON: the signs, the required animations, and the looks with their kinds and what
// each plays (docgen spec).
#pragma once
#include <string>
#include "util/json.h"

namespace nestbox {

// The contract as a JSON object, one look per line.
std::string spec_json();

}  // namespace nestbox
