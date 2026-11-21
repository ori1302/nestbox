#include "bonjour.h"
#include <arpa/inet.h>
#include <unistd.h>
#include "util/text.h"

namespace nestbox {

namespace {
const std::string MDNS_SUFFIX = ".local";

// TXT record wire format: each "key=value" preceded by its length in one byte.
std::string txt_bytes(const TxtRecord& txt) {
    std::string out;
    for (const auto& [key, value] : txt) {
        const std::string item = key + "=" + value;
        out += char(item.size());
        out += item;
    }
    return out;
}
}  // namespace

bool Bonjour::announce(const std::string& instance, int port, const TxtRecord& txt) {
    withdraw();
    const std::string record = txt_bytes(txt);
    const std::string type = std::string(SERVICE_TYPE) + "." + SERVICE_PROTOCOL;
    const DNSServiceErrorType e =
        DNSServiceRegister(&ref_, 0, 0, instance.c_str(), type.c_str(), nullptr, nullptr, htons(uint16_t(port)),
                           uint16_t(record.size()), record.data(), nullptr, nullptr);
    return e == kDNSServiceErr_NoError;
}

void Bonjour::withdraw() {
    if (ref_) DNSServiceRefDeallocate(ref_);
    ref_ = nullptr;
}

std::string local_hostname() {
    char host[256] = "localhost";
    gethostname(host, sizeof host);
    return host;
}

std::string short_hostname() {
    std::string h = local_hostname();
    if (h.size() > MDNS_SUFFIX.size() && ends_with(h, MDNS_SUFFIX)) h.resize(h.size() - MDNS_SUFFIX.size());
    return h;
}

}  // namespace nestbox
