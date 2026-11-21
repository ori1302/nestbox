// Finding pets on the local network: Bonjour service _nestbox._tcp, with TXT records id, host and pet.
#pragma once
#include <string>
#include <vector>

namespace companion {

struct NetworkPet {
    std::string name;    // Bonjour service name
    std::string url;     // http://host:port, from the resolved service
    std::string id;      // TXT id: stable across renames and new addresses (empty if the record has none)
    std::string host;    // TXT host: the pet's own .local name
    std::string pet;     // TXT pet: the character it shows

    // The Mac simulator, rather than a device.
    bool simulator() const;
};

// Every pet that resolves within the browse window, devices before simulators. With `only_name`, just that one.
std::vector<NetworkPet> pets_on_network(int browse_ms, const std::string& only_name = "");

}  // namespace companion
