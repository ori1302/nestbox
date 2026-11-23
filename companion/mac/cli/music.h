// What a music player on this Mac is playing, from the announcements Spotify and Apple Music post to every app
// when a song starts, pauses, stops or changes. Listening needs no permission and asks the players nothing, so
// until the first announcement (a play, pause or skip after the helper starts) nothing is known.
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace companion {

struct NowPlaying {
    std::string song, artist;      // artist may be empty
    std::optional<int> percent;    // how far into the song, 0 to 100; none when the player does not say
    int seconds_left = 0;          // with `percent`: until the song ends
};

class MusicWatch {
public:
    MusicWatch();    // starts listening; announcements arrive while listen() runs, on the main thread only
    MusicWatch(const MusicWatch&) = delete;
    MusicWatch& operator=(const MusicWatch&) = delete;

    // Waits up to `wait`, taking in any announcement that comes. Call it from the main thread.
    void listen(std::chrono::milliseconds wait);

    // The song playing now, if any; with several players playing, the one that started last.
    std::optional<NowPlaying> now() const;
    // Counts the announcements that changed what plays, so a caller sees a change without comparing songs.
    uint64_t changes() const;

    // One player's announcement, taken in by listen().
    void heard(int player, bool playing, const std::string& song, const std::string& artist, double length_s,
               std::optional<double> position_s);

private:
    using Clock = std::chrono::steady_clock;
    struct Player {
        bool playing = false;
        std::string song, artist;
        double length_s = 0;
        std::optional<double> position_s;    // at `at`, as the player said it
        Clock::time_point at;
    };
    static constexpr int PLAYERS = 2;

    Player players_[PLAYERS];
    uint64_t changes_ = 0;
};

}  // namespace companion
