#include "door/http.h"
#include "util/json.h"
#include "util/text.h"

namespace nestbox {

namespace {
constexpr char MDNS_SUFFIX[] = ".local";
constexpr size_t MDNS_SUFFIX_LEN = sizeof MDNS_SUFFIX - 1;
}  // namespace

void set_error(Response& r, int status, const std::string& message) {
    r.status = status;
    r.body = JsonWriter().begin_object().field("ok", false).field("error", message).end_object().text();
}

std::string path_only(const std::string& path_q) { return path_q.substr(0, path_q.find('?')); }

std::string query_value(const std::string& path_q, const std::string& key) {
    const std::string prefix = key + "=";
    for (size_t q = path_q.find('?'); q != std::string::npos;) {
        size_t start = q + 1, end = path_q.find('&', start);
        std::string pair = path_q.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (pair.compare(0, prefix.size(), prefix) == 0) return pair.substr(prefix.size());
        q = end;
    }
    return "";
}

bool is_mdns_name(const std::string& host) { return host.size() > MDNS_SUFFIX_LEN && ends_with(host, MDNS_SUFFIX); }

std::string host_name(std::string h) {
    size_t scheme = h.find("://");
    if (scheme != std::string::npos) h = h.substr(scheme + 3);
    h = ascii_lower(h.substr(0, h.find('/')));
    if (!h.empty() && h[0] == '[') return h.substr(0, h.find(']') + 1);
    h = h.substr(0, h.find(':'));
    if (!h.empty() && h.back() == '.') h.pop_back();
    return h;
}

std::string bare_host(const std::string& host) {
    std::string h = host_name(host);
    if (is_mdns_name(h)) h.resize(h.size() - MDNS_SUFFIX_LEN);
    return h;
}

bool is_literal_host(const std::string& name) {
    return name == "localhost" || (!name.empty() && name[0] == '[') ||
           (!name.empty() && name.find_first_not_of("0123456789.") == std::string::npos);
}

}  // namespace nestbox
