// What every device puts on the wire besides the API itself: HTTP status lines, the Bonjour record companions find
// it by, and the input its own hardware sends to the API.
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "door/http.h"

namespace nestbox {

// Bonjour service type and protocol, announced as "_nestbox._tcp".
constexpr const char* SERVICE_TYPE = "_nestbox";
constexpr const char* SERVICE_PROTOCOL = "_tcp";

// The reason phrase for an HTTP status ("Not Found"); a generic one for its class for any other status.
const char* reason_phrase(int status);

// The reply to a request whose body is over its limit (body_limit() in door/api.h).
Response too_large_reply(size_t limit);

using TxtRecord = std::vector<std::pair<std::string, std::string>>;
// The Bonjour TXT record: pet (display name), api (the API version, "1"), id (permanent; a paired app finds its device
// by it) and host (the mDNS name, "nestbox-3f2a.local").
TxtRecord bonjour_txt(const std::string& pet, const std::string& id, const std::string& mdns_host);

// POST /v1/input bodies for the device's own hardware.
std::string button_input(const std::string& press);     // "single", "double" or "long"
std::string sensor_input(const std::string& gesture);   // "tap" or "shake"
std::string computer_input(bool asleep);                // the USB host suspended or resumed the bus

}  // namespace nestbox
