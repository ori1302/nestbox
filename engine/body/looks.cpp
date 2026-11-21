#include "body/looks.h"

namespace nestbox {

namespace {
Play play(const char* anim, float speed = 1) {
    Play p;
    p.anim = anim;
    p.speed = speed;
    return p;
}

// Laps round the open floor, the animation's rows following the way it runs; then the look's next play.
Play laps(const char* anim) {
    Play p = play(anim);
    p.lap = true;
    return p;
}

Play at_desk(const char* anim, float speed) {
    Play p = play(anim, speed);
    p.facing = Facing::Desk;
    p.prop = Prop::Desk;
    return p;
}

// Strikes on a loop: only the frames between a strike's rush and return.
std::vector<Play> strikes(std::initializer_list<const char*> anims, Facing facing) {
    std::vector<Play> out;
    for (const char* a : anims) {
        Play p = play(a);
        p.loop = PlayLoop::Strike;
        p.facing = facing;
        out.push_back(p);
    }
    return out;
}

std::vector<Play> work_fast() {
    std::vector<Play> plays = {play("WorkFast")};
    for (const Play& p : strikes({"Shock", "Emit", "SpAttack", "MultiStrike", "MultiScratch", "QuickStrike", "Strike",
                                  "Attack"}, Facing::Down))
        plays.push_back(p);
    for (auto& p : plays) p.prop = Prop::Desk;
    return plays;
}

std::vector<Play> fighting() {
    std::vector<Play> plays = {play("Fighting")};
    for (const Play& p : strikes({"MultiStrike", "QuickStrike", "Kick", "Strike", "Attack"}, Facing::Right))
        plays.push_back(p);
    return plays;
}

// A pet's own animation for the look (the first name on each list) leads every list and keeps its author's timing
// (after laps, for the two that can run them). The fallbacks were made for quick moments, so most play slower here
// (speed < 1).
std::vector<LookInfo> build_table() {
    Play work = at_desk("Work", 1);
    work.typing = true;
    const Play desk = at_desk("Idle", 1);
    Play hurt = play("Hurt");
    hurt.shake = true;
    Play look_up = at_desk("LookUp", 0.12f);
    Play charge = play("Charge", 0.5f);
    charge.prop = Prop::Desk;
    const Play sit = at_desk("Sit", 0.2f);
    const Play watch = at_desk("Idle", 1);
    Play own_watch = play("Watch");          // its own pose and facing; the laptop stays on, an agent is using it
    own_watch.prop = Prop::Desk;
    const LookKind mood = LookKind::Mood, reaction = LookKind::Reaction;
    const LookInfo rows[] = {
        {Look::Wander, "wander", "WANDER", mood, 0, Bubble::None,
         "nothing going on: it strolls around", {play("Idle")}},
        {Look::Idle, "idle", "IDLE", mood, 0, Bubble::None, "you are around: it stands about", {play("Idle")}},
        {Look::Type, "type", "TYPE", mood, 0, Bubble::None,
         "you are typing: it works at its desk", {work, desk}},
        {Look::TypeFast, "type_fast", "TYPE FAST", mood, 0, Bubble::None,
         "you type fast: it works hard, facing you", work_fast()},
        {Look::Happy, "happy", "HAPPY", reaction, 3200, Bubble::Heart, "something finished well",
         {play("Pose", 0.8f), play("Hop", 0.8f)}},
        {Look::Alert, "alert", "ALERT", mood, 0, Bubble::Bang, "someone needs the user",
         {play("Alert"), play("Hop")}},
        {Look::Sad, "sad", "SAD", reaction, 3200, Bubble::Cross,
         "something failed: a red cross, unlike done's heart",
         {play("Cringe"), hurt}},
        {Look::Dizzy, "dizzy", "DIZZY", reaction, 2400, Bubble::None, "it was shaken", {play("Rotate")}},
        {Look::Wake, "wake", "WAKE", reaction, 2400, Bubble::None, "it wakes up",
         {play("DeepBreath"), play("Wake"), play("Hop")}},
        {Look::Sleep, "sleep", "SLEEP", mood, 0, Bubble::Zzz, "nobody around: it sleeps", {play("Sleep")}},
        {Look::Watch, "watch", "WATCH", mood, 0, Bubble::Dots,
         "an agent works: it sits back and watches its laptop",
         {own_watch, sit, look_up, watch}},
        {Look::Busy, "busy", "BUSY", mood, 0, Bubble::Dots, "something runs: a build, a deploy, tests",
         {play("Busy"), charge}},
        {Look::Party, "party", "PARTY", mood, 0, Bubble::None, "a party",
         {laps("Zoomies"), play("Party"), play("Dance", 0.7f), play("Twirl", 0.6f), play("Hop", 0.8f)}},
        {Look::Dance, "dance", "DANCE", mood, 0, Bubble::Note, "music is playing",
         {play("Dancing"), play("Dance", 0.6f), play("Twirl", 0.45f), play("Nod", 0.6f), play("Hop", 0.5f)}},
        {Look::Fight, "fight", "FIGHT", mood, 0, Bubble::None, "a play-fight", fighting()},
        {Look::Eat, "eat", "EAT", mood, 0, Bubble::None, "lunch or a break",
         {play("Eating"), play("Eat", 0.6f), play("Charge", 0.35f)}},
        {Look::Sit, "sit", "SIT", mood, 0, Bubble::None, "a meeting or focus: it keeps still",
         {play("Quiet"), play("Sit", 0.5f), play("Laying", 0.5f), play("Idle", 0.4f)}},
        {Look::Slump, "slump", "SLUMP", mood, 0, Bubble::None, "something stays broken",
         {play("Down"), play("Pain", 0.5f), play("Hurt", 0.35f)}},
        {Look::Celebrate, "celebrate", "CELEBRATE", reaction, 4200, Bubble::Heart,
         "something shipped: bigger than done",
         {laps("Zoomies"), play("Celebrate"), play("Dance", 0.75f), play("Twirl", 0.6f), play("Pose", 0.7f),
          play("Hop", 0.9f)}},
        {Look::Nod, "nod", "NOD", reaction, 1600, Bubble::None, "a small nod: it got the message",
         {play("Noted"), play("Nod", 0.6f), play("Idle")}},
        {Look::Surprised, "surprised", "SURPRISED", reaction, 2600, Bubble::Shock, "a message, the doorbell, a timer",
         {play("Surprised"), play("LostBalance", 0.8f), play("Hop", 0.8f)}},
        {Look::Petted, "petted", "PETTED", reaction, 2600, Bubble::Heart, "a pat on the head",
         {play("Petted"), play("Appeal", 0.6f), play("TailWhip", 0.6f), play("Nod", 0.6f), play("Hop", 0.6f)}},
        {Look::Grumpy, "grumpy", "GRUMPY", reaction, 2600, Bubble::Angry, "rejected, flaky, annoying",
         {play("Grumpy"), play("RearUp", 0.7f), play("Charge", 0.5f)}},
        {Look::Confused, "confused", "CONFUSED", reaction, 2600, Bubble::Question, "a conflict, something odd",
         {play("Confused"), play("Idle")}},
        {Look::Yawn, "yawn", "YAWN", reaction, 2200, Bubble::None, "getting sleepy",
         {play("Yawn"), play("DeepBreath", 0.7f), play("Idle", 0.5f)}},
        {Look::Oops, "oops", "OOPS", reaction, 2600, Bubble::None, "a warning: lighter than an error",
         {play("Oops"), play("Trip", 0.75f), play("Cringe", 0.7f), play("Hurt", 0.6f)}},
        {Look::Act, "do", "DO", reaction, 3000, Bubble::None,
         "a step sent with do: its own animation, a walk, laps, a throw", {play("Idle")}},
    };
    std::vector<LookInfo> table(static_cast<size_t>(Look::Count));     // indexed by Look
    for (const LookInfo& row : rows) table[size_t(row.look)] = row;
    return table;
}
}  // namespace

const std::vector<LookInfo>& looks() {
    static const std::vector<LookInfo> table = build_table();
    return table;
}

const LookInfo& info(Look s) { return looks()[size_t(s)]; }

bool look_named(const std::string& key, Look& out) {
    for (auto& i : looks())
        if (key == i.key && i.look != Look::Act) { out = i.look; return true; }
    return false;
}

const char* sign_key(Bubble b) {
    switch (b) {
        case Bubble::Heart: return "heart";
        case Bubble::Bang: return "!";
        case Bubble::Question: return "?";
        case Bubble::Zzz: return "zzz";
        case Bubble::Note: return "note";
        case Bubble::Cross: return "cross";
        case Bubble::Angry: return "angry";
        case Bubble::Shock: return "shock";
        case Bubble::Dots: return "...";
        case Bubble::Star: return "star";
        case Bubble::Check: return "check";
        case Bubble::None: break;
    }
    return "";
}

const std::vector<Bubble>& signs() {
    static const std::vector<Bubble> all = {Bubble::Bang,  Bubble::Question, Bubble::Dots,  Bubble::Zzz,
                                            Bubble::Heart, Bubble::Star,     Bubble::Check, Bubble::Cross,
                                            Bubble::Note,  Bubble::Angry,    Bubble::Shock};
    return all;
}

bool sign_named(const std::string& key, Bubble& out) {
    if (key == "dots") {
        out = Bubble::Dots;
        return true;
    }
    for (Bubble b : signs())
        if (key == sign_key(b)) {
            out = b;
            return true;
        }
    return false;
}

const char* effect_key(Effect e) {
    switch (e) {
        case Effect::Notes: return "notes";
        case Effect::Sparkles: return "sparkles";
        case Effect::Confetti: return "confetti";
        case Effect::None: break;
    }
    return "";
}

const std::vector<Effect>& effects() {
    static const std::vector<Effect> all = {Effect::Notes, Effect::Sparkles, Effect::Confetti};
    return all;
}

bool effect_named(const std::string& key, Effect& out) {
    for (Effect e : effects())
        if (key == effect_key(e)) {
            out = e;
            return true;
        }
    return false;
}

}  // namespace nestbox
