#include "pairing_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace companion {
namespace {

constexpr const char* FALLBACK_URL = "http://nestbox.local";
constexpr const char* ID_PREFIX = "id=";
constexpr const char* KEY_PREFIX = "key=";

std::string state_file() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.nestbox/device";
}

std::string line_with(const std::string& prefix) {
    std::ifstream in(state_file());
    for (std::string line; std::getline(in, line);)
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    return "";
}

// Hooks read the file while the companion rewrites it: write a temporary file, then rename it into place. It holds
// the key, so only its owner may read it.
void write_file(const std::string& url, const std::string& id, const std::string& key) {
    const std::string file = state_file();
    mkdir(file.substr(0, file.rfind('/')).c_str(), 0755);
    const std::string temporary = file + ".tmp" + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    std::string text = url + "\n";
    if (!id.empty()) text += ID_PREFIX + id + "\n";
    if (!key.empty()) text += KEY_PREFIX + key + "\n";
    const bool written = write(fd, text.data(), text.size()) == ssize_t(text.size());
    if (close(fd) != 0 || !written) {
        std::remove(temporary.c_str());
        return;
    }
    if (std::rename(temporary.c_str(), file.c_str()) != 0) std::remove(temporary.c_str());
}

}  // namespace

void remember_pet(const std::string& url, const std::string& id) {
    const std::string kept = !id.empty() && id == paired_id() ? line_with(KEY_PREFIX) : "";
    write_file(url, id, kept);
}

void remember_key(const std::string& url, const std::string& id, const std::string& key) { write_file(url, id, key); }

std::string remembered_url() {
    if (const char* env = std::getenv("NESTBOX_URL")) return env;
    std::ifstream in(state_file());
    std::string url;
    std::getline(in, url);
    return url.empty() ? FALLBACK_URL : url;
}

std::string paired_id() { return line_with(ID_PREFIX); }

std::string paired_key() {
    if (const char* env = std::getenv("NESTBOX_KEY")) return env;
    return line_with(KEY_PREFIX);
}

}  // namespace companion
