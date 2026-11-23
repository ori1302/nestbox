// The behaviour engine. It keeps the notes senders tell it (POST /v1/tell), hears the user's activity (POST /v1/user)
// and the device's own input, and each frame decides what the pet shows: the body goes to the highest-ranked note
// that has something for it (or to the pet's own life), and so do the bubble, the caption, the sign and the effect;
// lines and gauges show several notes at once. Everything a note holds ends by itself.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "body/pet.h"
#include "mind/free_time.h"
#include "mind/local_clock.h"
#include "mind/note.h"
#include "mind/steps.h"
#include "mind/user.h"
#include "util/json.h"
#include "util/text.h"

namespace nestbox {

class Brain {
public:
    static constexpr uint32_t SECOND_MS = 1000, MINUTE_MS = 60 * SECOND_MS, HOUR_MS = 60 * MINUTE_MS;
    static constexpr uint32_t DAY_MS = 24 * HOUR_MS;             // the longest anything is kept
    // The states' own times. A "for" the sender gives is real time and replaces them.
    static constexpr uint32_t WORKING_MS = 15 * MINUTE_MS, ASKING_MS = HOUR_MS, FAILING_MS = HOUR_MS;
    static constexpr uint32_t ON_MS = 15 * MINUTE_MS, QUIET_MS = HOUR_MS, MOMENT_MS = 8 * SECOND_MS;
    static constexpr uint32_t DEVICE_QUIET_MS = 8 * HOUR_MS;     // the long press
    static constexpr uint32_t QUIET_OFF_MS = 4000;              // "quiet off" in the status bar
    static constexpr uint32_t WAIT_MAX_MS = 12 * HOUR_MS;        // what waits for the user gives up after this
    static constexpr uint32_t MOMENT_WAIT_MS = MINUTE_MS;        // a moment that cannot show yet gives up after this
    // A question waves for CALL_MS, then waits calmly and waves again for RECALL_MS every RECALL_EVERY_MS.
    static constexpr uint32_t CALL_MS = MINUTE_MS, RECALL_EVERY_MS = 10 * MINUTE_MS, RECALL_MS = 5 * SECOND_MS;
    static constexpr size_t MAX_NOTES = 24, MAX_PER_SENDER = 8;
    static constexpr size_t MAX_GAUGES = 3;                      // on screen at once
    // A "do" on a lasting note comes first for TURN_MS; then the note goes back to its state's place.
    // Fair share: a sender showing a look it chose (a moment, a question, a "do" in its turn) while another sender
    // wants the body uses its share, SHARE_MS, which comes back over SHARE_BACK_MS. With none left it gives way, and
    // takes the body again only once SHARE_AGAIN_MS of it is back.
    static constexpr uint32_t TURN_MS = 8 * SECOND_MS, SHARE_MS = 8 * SECOND_MS, SHARE_BACK_MS = 22 * SECOND_MS;
    // An `on` note's "do" takes turns with the pet's own life: ON_DO_MS of it, then ON_REST_MS of the pet's own, from
    // when its body or state last changed.
    static constexpr uint32_t ON_DO_MS = 30 * SECOND_MS, ON_REST_MS = 90 * SECOND_MS;
    static constexpr uint32_t SHARE_AGAIN_MS = 2 * SECOND_MS;

    // What one tell did, for its reply.
    struct Told {
        bool used = false;               // anything in the request could be used
        std::string from, id, state;
        std::vector<std::string> showing;   // the note's parts on screen now, by their tell keys
        long ends_in = 0;                // seconds until it ends by itself; 0: not kept
        std::string why;                 // when not everything it has is showing
        int ended = -1;                  // after "over": how many notes ended
        std::vector<std::string> warnings;
    };
    Told tell(const Fields& f, uint32_t now_ms);
    // A note from the device itself (pairing asks the user); not reachable from the network.
    void tell_device(const Note& n, uint32_t now_ms);
    void end_device();

    // A report from a source of activity; returns its warnings.
    std::vector<std::string> user(const Fields& f, uint32_t now_ms) { return user_.report(f, now_ms); }
    // The device's own input. tap: a single press or a tap on the case; dismiss: a double press; quiet_press: a long
    // press; shake: the motion sensor; computer: the USB host slept or woke. Each returns what it did ("" for a
    // double press with nothing to dismiss: then the device brings out the next pet).
    std::string tap(uint32_t now_ms);
    std::string dismiss(uint32_t now_ms);
    std::string quiet_press(uint32_t now_ms);
    std::string shake(uint32_t now_ms);
    std::string computer(bool asleep, uint32_t now_ms);

    // The look the pet should show now, once per frame; why() says why in a sentence.
    Look decide(uint32_t now_ms);
    const std::string& why() const { return why_; }
    // Pushes everything else the pet shows: the playing step, words, the caption, the sign, the effect, the lines,
    // the gauges, the typing pace and the time of day.
    void show(Pet& pet, uint32_t now_ms);

    // Reading, for GET /v1 and the replies.
    struct NoteView {
        std::string from, id, state, line, color;
        std::vector<std::string> showing;
        long ends_in = 0;
        std::string why;
    };
    std::vector<NoteView> notes(uint32_t now_ms);
    bool quiet(uint32_t now_ms) const;
    std::string user_word(uint32_t now_ms) const { return user_.word(now_ms); }
    // The words in the bubble and the first status line on screen now.
    std::string saying(uint32_t now_ms);
    std::string captioning(uint32_t now_ms);
    std::string first_line(uint32_t now_ms);
    // What the body does, in a word ("watch", "throw", "honk"), from the look decide() gave.
    std::string doing(Look l) const;
    // Local minutes after midnight by the clock the pet follows; none until it knows.
    std::optional<int> minute_of_day(uint32_t now_ms) const;
    Daypart daypart(uint32_t now_ms) const;

    // The device's own clock: local seconds since 1970 as of now_ms.
    void clock(int64_t local_seconds, uint32_t now_ms) { clock_.set(local_seconds, now_ms); }
    // Forces the time of day to `minute` (minutes after midnight) for an hour; none lets the clock decide again.
    void force_time(std::optional<int> minute, uint32_t now_ms);
    // Text size, 1-3: bubbles and gauges.
    void text_size(int size) { text_size_ = size < 1 ? 1 : size > 3 ? 3 : size; }
    int text_size() const { return text_size_; }

    // Debugging (the simulator's keys): pin a look until automatic(), play a reaction now, show a time of day.
    void hold(Look l) { held_ = true; held_look_ = l; }
    void automatic() {
        held_ = false;
        pastime_held_ = false;
    }
    bool held() const { return held_; }
    // A reaction even while quiet: the user's own hand, and debugging.
    void show_reaction(Look l, uint32_t now_ms, uint32_t for_ms = 0);
    // The pet's next pastime in turn, now, whatever its hours, played through like a held look, ahead of typing and
    // the notes (the simulator's P key: the key press itself counts as typing).
    void next_pastime(uint32_t now_ms);
    // One of the pet's own pastimes by name, now, the same way (the simulator's recordings); false if it has none
    // of that name.
    bool play_pastime(const std::string& name, uint32_t now_ms);
    // The pet out was replaced or taken off the shelf: forget what came from its egg (its pastime) until the next
    // frame shows the pet that is out now.
    void forget_pet() {
        egg_ = nullptr;
        game_.clear();
        picked_ = NO_PASTIME;
    }
    void hold_daypart(std::optional<Daypart> part) { held_daypart_ = part; }
    // Steps sent with "do" still to play, on every note whose steps have not finished.
    size_t steps_left() const;
    bool speaking(uint32_t now_ms) { return !saying(now_ms).empty(); }
    float kps(uint32_t now_ms) const { return user_.kps(now_ms); }

private:
    struct Kept {
        Note note;
        uint32_t first = 0, told = 0;    // when its name first appeared (line order), when it was last told
        uint32_t counted_ms = 0;         // time counted toward a lifetime that waits for the user
        uint32_t counted_to = 0;         // counted up to this moment
        uint32_t shown_from = 0;         // a moment began showing
        uint32_t turn_from = 0;          // its own "do" began its turn: when its body or state last changed
        bool seen = false, dismissed = false;
        StepChain chain;
        int rank = 0;
    };
    // What shows now: for each part, the note that has it.
    struct Pick {
        std::string body;                // the note that has the body ("" for none)
        bool draining = false;           // it plays a look its sender chose while another sender wants the body
        Look body_look = Look::Idle;     // its look, Look::Act for steps
        std::string bubble, caption, sign, effect;
        Bubble sign_value = Bubble::None;
        Effect effect_value = Effect::None;
        std::vector<std::pair<std::string, std::string>> lines;   // note name, text
        std::vector<std::string> gauges;
    };

    static std::string key_of(const std::string& name);
    void update(uint32_t now);
    bool ended(const Kept& k, uint32_t now) const;
    bool counts_presence(const Kept& k) const;
    uint32_t presence_length(const Kept& k) const;
    bool waits_for_parts(const Kept& k, uint32_t now) const;
    bool visible(const Kept& k, uint32_t now) const;
    bool calling(const Kept& k, uint32_t now) const;
    bool stale(const Kept& k) const;
    std::optional<Look> default_look(const Kept& k) const;
    bool has_body(const Kept& k, uint32_t now) const;
    bool on_rests(const Kept& k, uint32_t now) const;   // an `on` note's "do" between its rounds
    int body_rank(const Kept& k, uint32_t now) const;
    std::string sender_of(const Kept& k) const { return ascii_lower(k.note.from); }
    bool has_share(const std::string& sender) const;
    uint32_t share_used(const std::string& sender) const;
    void count_share(uint32_t now);
    std::vector<const Kept*> ranked(uint32_t now) const;
    Pick pick(uint32_t now) const;
    std::string line_text(const Kept& k, uint32_t now) const;
    uint16_t line_color(const Kept& k) const;
    long ends_in(const Kept& k, uint32_t now) const;
    std::string why_not(const Kept& k, const Pick& p, uint32_t now) const;
    std::vector<std::string> showing(const Kept& k, const Pick& p) const;
    void finish_steps(Kept& k, uint32_t now);
    int end_notes(const std::string& name);
    void trim(const std::string& keep, std::vector<std::string>& warnings);
    bool quiet_except(const std::string& sender, uint32_t now) const;
    bool hushed_for(const Kept& k, uint32_t now) const;   // quiet holds it back (3.7)
    Look own_life(uint32_t now);
    void pastime(Pet& pet, uint32_t now);
    int pick_pastime(uint32_t now);
    // What the reason calls pastime `pick` (or the game).
    std::string pastime_name(int pick) const;
    std::vector<Step> game_steps(const Pet& pet);
    Look steady(Look s, uint32_t now);
    Look with_transitions(Look s, uint32_t now, bool reacting);
    bool bedtime(uint32_t now) const;
    Kept* find(const std::string& name);

    User user_;
    LocalClock clock_;
    FreeTime free_time_;
    // A pastime in its free time, played like a sender's steps: one of the pet's own (its egg's "free_time"), or a
    // game with its toy if its throw kit plays: throws from a few spots round its floor.
    static constexpr int GAME_SPOTS = 3, GAME_THROWS_MIN = 2, GAME_THROWS_MAX = 3;
    static constexpr float GAME_SPOT_APART_PX = 40, GAME_TARGET_CLEAR_PX = 30;
    // Random spots tried for the game; after GAME_SPOT_STRICT_TRIES one too close to another is taken anyway.
    static constexpr int GAME_SPOT_TRIES = 200, GAME_SPOT_STRICT_TRIES = 150;
    static constexpr int GAME = -1;              // the throwing game among the pastimes
    StepChain game_;
    std::string game_name_ = "a pastime";
    static constexpr int NO_PASTIME = -2;
    int last_pastime_ = NO_PASTIME;              // the one played last, so the next is another when it can be
    int picked_ = NO_PASTIME;                    // the one picked to play next, named as soon as it is picked
    int stepped_ = NO_PASTIME;                   // the last one the P key played
    bool pastime_held_ = false;                  // ...and it is playing, held until it ends
    bool can_play_ = false;                      // the pet out has pastimes (seen when last shown)
    bool plays_game_ = false;                    // ...and a throw kit that plays
    const Egg* egg_ = nullptr;                   // the pet out's egg, as last shown
    bool playing_ = false;                       // its own life is a pastime now
    std::map<std::string, Kept> notes_;          // by lower-case name
    std::string body_owner_;                     // the note whose steps the body plays
    // Fair share: how much of its SHARE_MS each sender has used (none listed: none used), counted up to when; who
    // had the body since then, and whether that used its share.
    std::map<std::string, uint32_t> share_used_;
    uint32_t share_counted_to_ = 0;
    std::string share_holder_;
    bool share_draining_ = false;

    Look reaction_ = Look::Idle;
    uint32_t reaction_until_ = 0;
    Look pending_ = Look::Idle;                  // a reaction that arrived while asleep, played after waking
    uint32_t pending_ms_ = 0;
    bool held_ = false;
    Look held_look_ = Look::Idle;
    std::optional<Daypart> held_daypart_;
    int forced_minute_ = -1;
    uint32_t forced_at_ = 0, forced_until_ = 0;
    int text_size_ = 2;
    Look last_ = Look::Idle, last_body_ = Look::Idle;
    uint32_t last_since_ = 0;
    std::string why_ = "starting";
    bool paused_ = false;
};

}  // namespace nestbox
