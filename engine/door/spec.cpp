#include "door/spec.h"
#include "body/looks.h"
#include "mind/steps.h"

namespace nestbox {

namespace {
const char* facing_name(Facing f) {
    switch (f) {
        case Facing::Up: return "away";
        case Facing::UpRight: return "up-right";
        case Facing::Right: return "right";
        case Facing::Desk: return "its screen";
        case Facing::Down: break;
    }
    return "you";
}

void write_play(JsonWriter& w, const Play& p) {
    w.begin_object().field("anim", p.anim).field("facing", facing_name(p.facing));
    if (p.speed != 1) w.key("speed").num_short(p.speed);
    if (p.loop == PlayLoop::Strike) w.field("loop", "strike");
    if (p.prop == Prop::Desk) w.field("desk", true);
    if (p.shake) w.field("shake", true);
    if (p.lap) w.field("laps", true);
    w.end_object();
}

void write_look(JsonWriter& w, const LookInfo& s) {
    const bool reaction = s.kind == LookKind::Reaction;
    w.begin_object().field("look", s.key).field("kind", reaction ? "reaction" : "mood");
    w.field("sign", sign_key(s.bubble));
    if (reaction) w.field("length_ms", static_cast<long long>(s.length_ms));
    w.field("means", s.means).key("plays").begin_array();
    if (s.look == Look::Wander) {   // wandering walks between pauses; the pauses play the table's entries
        Play walk;
        walk.anim = "Walk";
        write_play(w, walk);
    }
    for (const Play& p : s.plays) write_play(w, p);
    w.end_array().end_object();
}

void write_spec(JsonWriter& w) {
    w.key("signs").begin_array();
    for (Bubble b : signs()) w.str(sign_key(b));
    w.end_array();
    w.key("required").begin_array().str("Idle").str("Walk").end_array();
    w.key("words").begin_array();                // the behaviours a step's "do" may name
    for (const Behaviour& b : behaviours()) w.str(b.word);
    w.end_array();
    w.key("looks").begin_array(JsonWriter::Items::OnePerLine);
    for (auto& s : looks())
        if (s.look != Look::Act) write_look(w, s);
    w.end_array();
}
}  // namespace

std::string spec_json() {
    JsonWriter w;
    w.begin_object();
    write_spec(w);
    return w.end_object().text() + "\n";
}

}  // namespace nestbox
