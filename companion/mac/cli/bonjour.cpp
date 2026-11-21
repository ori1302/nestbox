#include "bonjour.h"

#include <arpa/inet.h>
#include <dns_sd.h>
#include <poll.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>

#include "host/protocol.h"

namespace companion {
namespace {

const std::string SERVICE = std::string(nestbox::SERVICE_TYPE) + "." + nestbox::SERVICE_PROTOCOL;
constexpr int RESOLVE_TIMEOUT_MS = 1500;    // stale browse entries do not resolve

struct BrowseResult {
    std::string name, domain;
    uint32_t interface_index = 0;
};

// Dispatches the service's replies until `ms` have passed or `done` is set.
void process_replies(DNSServiceRef ref, int ms, const std::atomic<bool>* done = nullptr) {
    using Clock = std::chrono::steady_clock;
    int fd = DNSServiceRefSockFD(ref);
    auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end && !(done && *done)) {
        pollfd p{fd, POLLIN, 0};
        int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count());
        if (poll(&p, 1, std::max(1, left)) == 1) DNSServiceProcessResult(ref);
    }
}

std::vector<BrowseResult> browse(int ms) {
    std::vector<BrowseResult> found;
    DNSServiceRef ref;
    auto on_reply = [](DNSServiceRef, DNSServiceFlags flags, uint32_t interface_index, DNSServiceErrorType err,
                       const char* name, const char*, const char* domain, void* context) {
        auto* list = static_cast<std::vector<BrowseResult>*>(context);
        if (err != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd)) return;
        for (auto& f : *list)
            if (f.name == name) return;    // the same service on another interface
        list->push_back({name, domain, interface_index});
    };
    if (DNSServiceBrowse(&ref, 0, 0, SERVICE.c_str(), nullptr, on_reply, &found) != kDNSServiceErr_NoError)
        return found;
    process_replies(ref, ms);
    DNSServiceRefDeallocate(ref);
    return found;
}

std::string txt_value(uint16_t len, const unsigned char* record, const char* key) {
    uint8_t n = 0;
    const void* v = TXTRecordGetValuePtr(len, record, key, &n);
    return v ? std::string(static_cast<const char*>(v), n) : "";
}

// The pet behind a browse result; its url stays empty if it does not resolve in time.
NetworkPet resolve(const BrowseResult& found) {
    struct Context {
        NetworkPet pet;
        std::atomic<bool> done{false};
    } context;
    context.pet.name = found.name;
    DNSServiceRef ref;
    auto on_reply = [](DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*,
                       const char* host, uint16_t port, uint16_t txt_len, const unsigned char* txt, void* c) {
        auto* ctx = static_cast<Context*>(c);
        if (err == kDNSServiceErr_NoError) {
            std::string h = host;
            if (!h.empty() && h.back() == '.') h.pop_back();
            ctx->pet.url = "http://" + h + ":" + std::to_string(ntohs(port));
            ctx->pet.id = txt_value(txt_len, txt, "id");
            ctx->pet.host = txt_value(txt_len, txt, "host");
            ctx->pet.pet = txt_value(txt_len, txt, "pet");
        }
        ctx->done = true;
    };
    if (DNSServiceResolve(&ref, 0, found.interface_index, found.name.c_str(), SERVICE.c_str(), found.domain.c_str(),
                          on_reply, &context) != kDNSServiceErr_NoError)
        return context.pet;
    process_replies(ref, RESOLVE_TIMEOUT_MS, &context.done);
    DNSServiceRefDeallocate(ref);
    return context.pet;
}

}  // namespace

bool NetworkPet::simulator() const {
    return name.find("simulator") != std::string::npos || id.rfind("sim-", 0) == 0;
}

std::vector<NetworkPet> pets_on_network(int browse_ms, const std::string& only_name) {
    std::vector<NetworkPet> pets;
    for (auto& found : browse(browse_ms)) {
        if (!only_name.empty() && found.name != only_name) continue;
        NetworkPet p = resolve(found);
        if (!p.url.empty()) pets.push_back(p);
    }
    std::stable_sort(pets.begin(), pets.end(),
                     [](const NetworkPet& a, const NetworkPet& b) { return !a.simulator() && b.simulator(); });
    return pets;
}

}  // namespace companion
