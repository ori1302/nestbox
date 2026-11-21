// The pet's network side: Wi-Fi (station), mDNS (<host>.local, _nestbox._tcp) and the HTTP server on port 80, all on
// core 0, away from the frame loop.
//
// Every pet has its own name, nestbox-<last 4 hex digits of its id> unless renamed, so several can share a network.
// If the name is taken anyway, mDNS moves to the next free one ("kitchen" -> "kitchen-2") and host() reports that.
// Finders pair by the permanent id in the TXT record, not by name.
//
// Not thread-safe: called from setup() and then only from the service task. The HTTP server calls the Handler on
// its own task.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "door/http.h"

namespace net {

// host, origin, auth: the request's Host, Origin and Authorization headers ("" if none); peer: the client's address;
// setup_network: it came in on the setup access point's address.
using Handler = std::function<nestbox::Response(const std::string& method, const std::string& path,
                                                const std::string& body, const std::string& host,
                                                const std::string& origin, const std::string& auth,
                                                const std::string& peer, bool setup_network)>;

std::string device_id();                      // 6 hex digits from the factory MAC, fixed for life
std::string default_host();                   // nestbox-<last 4 hex digits of the id>
bool valid_host(const std::string& name);     // lower-case letters, digits, hyphens; 1-63, no hyphen at either end

// Starts Wi-Fi with these credentials (none: stays offline) and the HTTP server, as <host>.local.
void begin(const std::string& host, const std::string& ssid, const std::string& pass, Handler h);
// New credentials (the caller saves them); an empty ssid forgets the network.
void join(const std::string& ssid, const std::string& pass);
// The network the station is already on (setup just joined it): keep it, without reconnecting.
void adopt(const std::string& ssid, const std::string& pass);
// Stops (true) or resumes rejoining the saved network, while setup uses the radio.
void hold(bool hold, uint32_t now_ms);
// A new name (the caller saves it). mDNS takes it at once; the router learns it on the next connect.
void rename(const std::string& host);
// Call about every half second: retries Wi-Fi, starts mDNS once there is an address, and follows a name mDNS had
// to change.
void tick(uint32_t now_ms);
bool online();
std::string host();                           // the name in use, without ".local"
std::string ip();                             // the station's address, "0.0.0.0" while offline
std::string status();                         // one line for the console
// Publishes the pet's display name (TXT pet=, and the instance "<pet> (<host>)").
void announce(const std::string& pet);

}  // namespace net
