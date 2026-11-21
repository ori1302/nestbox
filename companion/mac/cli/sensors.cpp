#include "sensors.h"

#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>

namespace companion {

uint32_t key_presses() {
    return CGEventSourceCounterForEventType(kCGEventSourceStateHIDSystemState, kCGEventKeyDown);
}

double idle_seconds() {
    return CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGAnyInputEventType);
}

bool screen_locked() {
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session) return false;
    bool locked = false;
    if (CFTypeRef v = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked")))
        locked = CFGetTypeID(v) == CFBooleanGetTypeID() ? CFBooleanGetValue(CFBooleanRef(v)) : true;
    CFRelease(session);
    return locked;
}

std::string short_host() {
    char name[256] = "";
    gethostname(name, sizeof name - 1);
    std::string n = name;
    if (const size_t dot = n.find('.'); dot != std::string::npos) n.resize(dot);
    return n.empty() ? "mac" : n.substr(0, 24);
}

}  // namespace companion
