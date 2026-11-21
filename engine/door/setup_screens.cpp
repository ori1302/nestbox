// What the setup shows on the pet's own screen in each phase, as cards (screen/cards.h).
#include <algorithm>
#include "door/setup.h"
#include "door/setup_parts.h"
#include "screen/cards.h"
#include "util/text.h"

namespace nestbox {

using cards::ACCENT;
using cards::BG;
using cards::Card;
using cards::DIM;
using cards::FG;
using cards::PetPicture;
using cards::Piece;
using cards::WARN;
using cards::YES;
using cards::pet_piece;
using cards::qr_card;
using cards::stack;
using cards::text_piece;

namespace {

// The "no Wi-Fi" corner mark of a pet set up to live offline: 7x6 cells at scale 2.
const char* const NO_WIFI[] = {"1111100", ".....11", "111.1.1", "...1..1", "1.1...1", ".1....."};
constexpr int NO_WIFI_W = 14, NO_WIFI_H = 12;
// When the phone's sign-in page didn't open by itself.
constexpr const char* SIGN_IN_HINT = "On your phone, tap \"Sign in to network\" or \"Use Without Internet\"";

// The Wi-Fi joining code phones understand: WIFI:T:WPA;S:<ssid>;P:<password>;; with \ ; , " : escaped.
std::string wifi_qr_text(const std::string& ssid, const std::string& password) {
    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':') o += '\\';
            o += c;
        }
        return o;
    };
    return "WIFI:T:WPA;S:" + esc(ssid) + ";P:" + esc(password) + ";;";
}

// The pet as the cards show it: its standing animation, facing the viewer.
PetPicture picture_of(const Egg* pet) {
    PetPicture p;
    const Anim* a = pet ? pet->standing_anim() : nullptr;
    if (!a) return p;
    p.boxes = a->boxes.data(); p.durations = a->durations.data(); p.frames = a->frames; p.fw = a->fw; p.fh = a->fh;
    p.scale = pet->scale; p.palette = pet->palette.data();
    return p;
}

// The access point's name and password beside (or under) its joining code.
void draw_join_card(Canvas& c, Layout* l, const Display& d, const std::string& ssid, const std::string& password) {
    Card k;
    k.title = "Point your phone's camera at me";
    k.short_title = "Scan me";
    k.qr = wifi_qr_text(ssid, password);
    const std::string pass = password.size() > 4 ? password.substr(0, 4) + " " + password.substr(4) : password;
    k.side = {{"Wi-Fi", DIM, 2, 4}, {ssid, FG, 3, 14, true}, {"Password", DIM, 2, 4}, {pass, ACCENT, 3, 0, true}};
    k.stacked = {{ssid, FG, 2, 4, true}, {pass, ACCENT, 2, 0, true}};
    qr_card(c, l, d, k);
}

// The pet's addresses on the home network, with a code that opens its welcome page.
void draw_all_set_card(Canvas& c, Layout* l, const Display& d, const std::string& url, const std::string& mdns,
                       const std::string& ip, const std::string& net) {
    Card k;
    k.title = "All set!";
    k.title_color = YES;
    k.title_scale = 3;
    k.title_gap = 10;
    k.qr = url;
    k.side = {{"Scan to open my page", FG, 3, 12}};
    if (!mdns.empty()) k.side.push_back({mdns, ACCENT, 2, 8});
    k.side.push_back({ip, ACCENT, 2, 0, true});
    k.below = {{"On " + net, DIM, 2, 0, false, true}};
    k.stacked = {{"Scan to open my page", FG, 2, 4}};
    if (!mdns.empty()) k.stacked.push_back({mdns, ACCENT, 2, 2, false, true});
    k.stacked.push_back({ip, ACCENT, 2, 4, true});
    k.stacked.push_back({"On " + net, DIM, 2, 0, false, true});
    qr_card(c, l, d, k);
}

}  // namespace

bool Setup::render(Canvas& c, const Egg* pet, uint32_t now, Layout* l) const {
    const PetPicture pic = picture_of(pet);
    if (holding_ && now - hold_since_ >= hold_show_ms_) {
        c.fill(BG);
        Piece bar;
        bar.kind = Piece::Bar;
        bar.gap = 20;
        const uint32_t held = std::min(now - hold_since_, RESET_ALL_MS);
        if (held < START_OVER_MS) {
            const float left = 1 - float(held - std::min(held, hold_show_ms_)) / float(START_OVER_MS - hold_show_ms_);
            stack(c, l, *d_,
                  {text_piece("Keep holding to start over", FG, 3, 8, 100),
                   text_piece("I'll forget my Wi-Fi", DIM, 2, 18), bar, text_piece("Let go to cancel", DIM, 2, 0)},
                  pic, now, left);
        } else {
            // Past START_OVER_MS letting go starts over; holding on counts down to resetting everything.
            const float left = 1 - float(held - START_OVER_MS) / float(RESET_ALL_MS - START_OVER_MS);
            stack(c, l, *d_,
                  {text_piece("Let go to start over", FG, 3, 8, 100),
                   text_piece("Keep holding to reset everything", DIM, 2, 18), bar,
                   text_piece("My pets stay", DIM, 2, 0)},
                  pic, now, left);
        }
        mask_outside(c, *d_);
        return true;
    }
    if (!active()) {
        if (offline_) {
            const Box m = d_->safe(4);
            const Box mark{m.x + m.w - NO_WIFI_W, m.y + m.h - NO_WIFI_H, NO_WIFI_W, NO_WIFI_H};
            c.glyph(NO_WIFI, 6, mark.x, mark.y, 2, DIM, BG);
            if (l) l->add("offline mark", mark);
        }
        return false;
    }
    c.fill(BG);
    const Display& d = *d_;
    const std::string name = drawable(pet_) ? pet_ : "";
    const std::string net = screen_name(want_ssid_);

    switch (phase_) {
        case Phase::Hello: {
            const std::string hello = again_ ? "Let's find my Wi-Fi" : name.empty() ? "Hi!" : "Hi, I'm " + name + "!";
            stack(c, l, d, {text_piece(hello, FG, 3, 18), pet_piece(), text_piece("Looking around...", DIM, 2, 0)}, pic,
                  now, 0);
            break;
        }
        case Phase::ShowCode:
            draw_join_card(c, l, d, ap_ssid_, ap_pass_);
            break;
        case Phase::Joined:
            if (now - since_ < HINT_MS)
                stack(c, l, d,
                      {text_piece("Phone joined!", YES, 3, 8), text_piece("Look at your phone", FG, 3, 20),
                       pet_piece(0)},
                      pic, now, 0);
            else                                               // the page didn't open by itself
                stack(c, l, d,
                      {text_piece("Phone joined!", YES, 3, 16), text_piece(SIGN_IN_HINT, FG, 3, 16, 100),
                       text_piece("Or open", DIM, 2, 6), text_piece(AP_IP, ACCENT, 3, 0)},
                      pic, now, 0);
            break;
        case Phase::Page:
            stack(c, l, d,
                  {text_piece("Look at your phone", FG, 3, 8), text_piece("Pick your Wi-Fi there", DIM, 2, 20),
                   pet_piece(0)},
                  pic, now, 0);
            break;
        case Phase::Checking: {
            Piece bar;
            bar.kind = Piece::Bar;
            bar.gap = 20;
            stack(c, l, d, {text_piece("Checking", DIM, 2, 6), text_piece(net, FG, 3, 14), bar, pet_piece(0)}, pic, now,
                  float(std::min<uint32_t>(now - since_, CHECK_MS)) / CHECK_MS);
            break;
        }
        case Phase::Failed:
            stack(c, l, d,
                  {text_piece(capitalized(reason(Audience::Screen)), WARN, 3, 12, 100),
                   text_piece("Try again on your phone", FG, 2, 20), pet_piece(0)},
                  pic, now, 0);
            break;
        case Phase::AllSet:
            draw_all_set_card(c, l, d, welcome_url(), mdns_name(), ip_, net);
            break;
        case Phase::Off:
            break;
    }
    mask_outside(c, d);
    return true;
}

}  // namespace nestbox
