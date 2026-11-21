// The helper's one-shot subcommands.
#pragma once
#include <string>
#include <vector>

namespace companion {

// `nestbox pair [--device URL] [--machine]`: asks the pet (the remembered one, or URL) for its key; the user confirms
// with the pet's button, and the key goes to ~/.nestbox/device. Returns 0 once paired, 1 if it was not, 2 for a bad
// address.
int run_pair(const std::string& device, bool machine);

// `nestbox find [--machine] [NAME]`: one line per pet on the network, URL last (scripts take the last word):
//   nestbox-1a2b         id=1a2b3c  host=nestbox-1a2b.local  pet=Nibble  paired=yes  http://nestbox-1a2b.local:80
// Spaces inside a value are written %20; kind=simulator marks the Mac simulator. With `machine`, each pet is a
// tagged JSON line instead (activity.h): {"event": "pet", "name", "url", "id", "host", "pet", "simulator",
// "paired"}. Returns 1 if none is found.
int run_find(const std::string& only_name, bool machine);

}  // namespace companion
