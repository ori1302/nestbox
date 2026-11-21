#include "settings.h"
#include <Preferences.h>

namespace settings {

namespace {
constexpr const char* NAMESPACE = "nestbox";
Preferences prefs;
}  // namespace

void begin() { prefs.begin(NAMESPACE, false); }
std::string get(const char* key) { return prefs.getString(key, "").c_str(); }
void put(const char* key, const std::string& value) { prefs.putString(key, value.c_str()); }
bool get_flag(const char* key, bool fallback) { return prefs.getBool(key, fallback); }
void put_flag(const char* key, bool value) { prefs.putBool(key, value); }
int get_int(const char* key, int fallback) { return prefs.getInt(key, fallback); }
void put_int(const char* key, int value) { prefs.putInt(key, value); }
void remove(const char* key) { prefs.remove(key); }

}  // namespace settings
