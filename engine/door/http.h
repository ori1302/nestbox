// HTTP pieces shared by the API and the setup routes.
#pragma once
#include <string>

namespace nestbox {

// The machine API's paths. The help (GET /), the guide for agents and the setup pages are unversioned.
constexpr const char* API_PATH = "/v1";                // GET: what the pet is doing; POST: the device itself
constexpr const char* TELL_PATH = "/v1/tell";
constexpr const char* USER_PATH = "/v1/user";
constexpr const char* PETS_PATH = "/v1/pets";
constexpr const char* WORDS_PATH = "/v1/words";
constexpr const char* PAIR_PATH = "/v1/pair";
constexpr const char* INPUT_PATH = "/v1/input";        // the device's own button and sensors; local only
constexpr const char* DEBUG_PATH = "/v1/debug";        // the body's details, for tests and recordings

struct Response {
    int status = 200;
    std::string type = "application/json";
    std::string body;
    std::string location;        // Location header, for redirects (captive portal)
};

// Sets `status` and the body {"ok": false, "error": message}; other fields are left as they are.
void set_error(Response& r, int status, const std::string& message);

std::string path_only(const std::string& path_q);
// The raw (not URL-decoded) value of `key` in the query string; "" if absent.
std::string query_value(const std::string& path_q, const std::string& key);

// A Host or Origin reduced to a lower-case name without scheme, port, path or trailing dot
// ("http://Pet.local.:8080/" -> "pet.local"). An IPv6 literal keeps its brackets.
std::string host_name(std::string host);
// host_name() without ".local" ("http://Pet.local:8080/" -> "pet").
std::string bare_host(const std::string& host);
// True for "name.local".
bool is_mdns_name(const std::string& host);
// True for a host_name() that is an IP address or "localhost": names, not literals, are what DNS rebinding needs.
bool is_literal_host(const std::string& name);

}  // namespace nestbox
