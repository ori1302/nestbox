// ~/.nestbox/device: the pet the companion last found. Line 1 is its URL (hooks read it), then "id=<TXT id>" when
// it is the paired pet, then "key=<key>" once paired with the pet's button.
#pragma once
#include <string>

namespace companion {

// Writes the file, keeping the key it holds for the same id.
void remember_pet(const std::string& url, const std::string& id);
// Writes the file with a key from pairing.
void remember_key(const std::string& url, const std::string& id, const std::string& key);

// $NESTBOX_URL, else the remembered URL, else http://nestbox.local.
std::string remembered_url();

// The paired pet's TXT id, or "".
std::string paired_id();
// The key from pairing, or "".
std::string paired_key();

}  // namespace companion
