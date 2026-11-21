// The simulator's Bonjour registration (_nestbox._tcp), as the device announces itself over mDNS.
#pragma once
#include <dns_sd.h>
#include <string>
#include "host/protocol.h"

namespace nestbox {

class Bonjour {
public:
    Bonjour() = default;
    Bonjour(const Bonjour&) = delete;
    Bonjour& operator=(const Bonjour&) = delete;
    ~Bonjour() { withdraw(); }

    // Registers the service, replacing an earlier registration; false if mDNSResponder refused it.
    bool announce(const std::string& instance, int port, const TxtRecord& txt);
    // Withdraws it, so finders stop listing a simulator that has quit.
    void withdraw();

private:
    DNSServiceRef ref_ = nullptr;
};

// This Mac's host name as gethostname() gives it ("localhost" if that fails).
std::string local_hostname();
// The same without a trailing ".local".
std::string short_hostname();

}  // namespace nestbox
