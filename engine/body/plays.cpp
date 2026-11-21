#include "body/plays.h"

namespace nestbox {

bool has_throw(const Egg& e) {
    if (!e.has_throw) return false;
    const ThrowKit& kit = e.throw_kit;
    const Anim* anim = e.find(kit.anim);
    if (!anim || anim->hit < 0 || anim->hit >= anim->frames || !e.find(kit.item)) return false;
    if (!kit.catch_anim.empty() && !e.find(kit.catch_anim)) return false;
    return kit.flight == Flight::Toss || (e.has_scene && e.scene.target_x >= 0);
}

PlayChoice play_for(const Egg& egg, const LookInfo& look) {
    if (look.look == Look::TypeFast && has_throw(egg) && egg.throw_kit.typing_fast)
        return {true, Play{egg.throw_kit.anim.c_str()}, true};
    for (size_t i = 0; i < look.plays.size(); i++) {
        const Anim* a = egg.find(look.plays[i].anim);
        if (!a || (look.plays[i].lap && a->rows < FACING_ROWS)) continue;   // laps need every facing
        return {true, look.plays[i], i == 0 || !egg.note(look.plays[i].anim).empty()};
    }
    if (!egg.find("Idle")) return {};
    return {true, Play{"Idle"}, false};
}

}  // namespace nestbox
