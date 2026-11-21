#include "host/frame.h"
#include <cstdio>
#include "door/setup.h"
#include "screen/cards.h"
#include "screen/hud.h"

namespace nestbox {

Look step_frame(Device& d, uint32_t now, const Caption& caption) {
    if (d.setup) d.setup->update(now);
    const Look s = d.brain.decide(now);
    d.brain.show(d.pet, now);
    const std::string wifi = d.setup ? d.setup->caption(now) : "";
    if (!d.brain.speaking(now)) {
        if (!caption.text.empty()) d.pet.say(caption.text, caption.color, Pet::SPEECH_SIZE, SpeechAt::Bottom);
        else if (!wifi.empty()) d.pet.say(wifi, cards::WARN, Pet::SPEECH_SIZE, SpeechAt::Bottom);
    }
    d.pet.want(s, now);
    d.pet.update(now);
    const bool setup_shown = d.setup && d.setup->covers(now);
    if (setup_shown) wake(d, now);               // a setup screen is there to be read
    if (update_rest(d, now) == ScreenState::Off) return s;   // the pet lives on; nothing is drawn
    if (!setup_shown) {                          // under a setup screen the pet would not show
        d.pet.render(d.screen, now);
        d.shelf.fade(d.screen, now);
    }
    if (d.setup) d.setup->render(d.screen, d.shelf.pet_out(), now);
    return s;
}

std::string LookLog::line(Look s, uint32_t now, const std::string& why) {
    if (s == last_) return "";
    last_ = s;
    char head[64];
    std::snprintf(head, sizeof head, "%8.1fs  %-12s ", now / 1000.0, look_key(s));
    return head + why + "\n";
}

}  // namespace nestbox
