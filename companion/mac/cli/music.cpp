#include "music.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <vector>

namespace companion {
namespace {

// Each player's announcement and the name of its song-length field. Spotify also gives the position in seconds;
// Apple Music does not, so its songs have no progress.
struct Source {
    CFStringRef notification;
    CFStringRef length_ms;
};

const Source SOURCES[] = {
    {CFSTR("com.spotify.client.PlaybackStateChanged"), CFSTR("Duration")},
    {CFSTR("com.apple.Music.playerInfo"), CFSTR("Total Time")},
};

std::string text(CFDictionaryRef info, CFStringRef key) {
    const void* v = info ? CFDictionaryGetValue(info, key) : nullptr;
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return "";
    CFStringRef s = CFStringRef(v);
    std::vector<char> buffer(CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1);
    if (!CFStringGetCString(s, buffer.data(), buffer.size(), kCFStringEncodingUTF8)) return "";
    return buffer.data();
}

std::optional<double> number(CFDictionaryRef info, CFStringRef key) {
    const void* v = info ? CFDictionaryGetValue(info, key) : nullptr;
    if (!v || CFGetTypeID(v) != CFNumberGetTypeID()) return std::nullopt;
    double d = 0;
    CFNumberGetValue(CFNumberRef(v), kCFNumberDoubleType, &d);
    return d;
}

void on_announcement(CFNotificationCenterRef, void* observer, CFNotificationName name, const void*,
                     CFDictionaryRef info) {
    for (int player = 0; player < int(std::size(SOURCES)); player++) {
        const Source& s = SOURCES[player];
        if (!name || CFStringCompare(name, s.notification, 0) != kCFCompareEqualTo) continue;
        static_cast<MusicWatch*>(observer)->heard(player, text(info, CFSTR("Player State")) == "Playing",
                                                  text(info, CFSTR("Name")), text(info, CFSTR("Artist")),
                                                  number(info, s.length_ms).value_or(0) / 1000,
                                                  number(info, CFSTR("Playback Position")));
    }
}

}  // namespace

MusicWatch::MusicWatch() {
    CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    for (const Source& s : SOURCES)
        CFNotificationCenterAddObserver(center, this, on_announcement, s.notification, nullptr,
                                        CFNotificationSuspensionBehaviorDeliverImmediately);
}

void MusicWatch::listen(std::chrono::milliseconds wait) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, std::chrono::duration<double>(wait).count(), false);
}

void MusicWatch::heard(int player, bool playing, const std::string& song, const std::string& artist,
                       double length_s, std::optional<double> position_s) {
    Player& p = players_[player];
    p.playing = playing && !song.empty();
    p.song = song;
    p.artist = artist;
    p.length_s = length_s;
    p.position_s = position_s;
    p.at = Clock::now();
    changes_++;
}

std::optional<NowPlaying> MusicWatch::now() const {
    const Player* latest = nullptr;
    for (const Player& p : players_)
        if (p.playing && (!latest || p.at > latest->at)) latest = &p;
    if (!latest) return std::nullopt;
    NowPlaying np;
    np.song = latest->song;
    np.artist = latest->artist;
    if (latest->length_s > 0 && latest->position_s) {
        const double played = std::chrono::duration<double>(Clock::now() - latest->at).count();
        const double at = std::min(*latest->position_s + played, latest->length_s);
        np.percent = int(100 * at / latest->length_s);
        np.seconds_left = int(latest->length_s - at);
    }
    return np;
}

uint64_t MusicWatch::changes() const {
    return changes_;
}

}  // namespace companion
