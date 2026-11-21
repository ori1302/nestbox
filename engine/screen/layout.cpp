#include "screen/layout.h"
#include <algorithm>
#include <cctype>

namespace nestbox {

namespace {
struct Wrapped {
    int size = 2;
    std::vector<std::string> lines;
    bool fits = true;
};

// Cuts a word that is too wide for a line: after a "-" or before a "." that starts a name (".local") when it
// can, else at the last letter that fits.
void split_word(std::string w, int width, int size, std::vector<std::string>& out, bool& fits) {
    while (w.size() > 1 && text_px(w, size) > width) {
        size_t fit = 1;                                   // how many letters fit
        while (fit < w.size() && text_px(w.substr(0, fit + 1), size) <= width) fit++;
        size_t cut = 0;
        for (size_t i = fit; i > 0 && !cut; i--)
            if (w[i - 1] == '-' ||
                (i < w.size() && w[i] == '.' && i + 1 < w.size() && std::isalnum(static_cast<unsigned char>(w[i + 1]))))
                cut = i;
        if (!cut) { cut = fit; fits = false; }
        out.push_back(w.substr(0, cut));
        w = w.substr(cut);
    }
    if (!w.empty()) out.push_back(w);
}

Wrapped wrap(const std::string& text, int width, int size) {
    Wrapped r;
    r.size = size;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {                       // paragraphs end at \n, words at spaces
        size_t nl = std::min(text.find('\n', at), text.size());
        std::string para = text.substr(at, nl - at);
        line.clear();
        size_t i = 0;
        while (i < para.size()) {
            while (i < para.size() && para[i] == ' ') i++;
            size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            if (j == i) break;
            std::string word = para.substr(i, j - i);
            i = j;
            if (!line.empty() && text_px(line + " " + word, size) <= width) { line += " " + word; continue; }
            if (!line.empty()) r.lines.push_back(line);
            line.clear();
            std::vector<std::string> parts;
            split_word(word, width, size, parts, r.fits);
            for (size_t k = 0; k + 1 < parts.size(); k++) r.lines.push_back(parts[k]);
            if (!parts.empty()) line = parts.back();
        }
        if (!line.empty()) r.lines.push_back(line);
        at = nl + 1;
    }
    return r;
}

int height_of(const Wrapped& w) { return text_block(int(w.lines.size()), w.size); }

Wrapped best_fit(const Box& area, const std::string& text, int max_size, int min_size) {
    Wrapped w;
    for (int s = max_size; s >= min_size; s--) {
        w = wrap(text, area.w, s);
        if (w.fits && height_of(w) <= area.h) return w;
    }
    w.fits = false;                                    // too much even at the smallest size: cut it
    while (!w.lines.empty() && height_of(w) > area.h) w.lines.pop_back();
    return w;
}

// Centred text with several lines: the narrowest width that keeps the same lines, so they come out even
// ("Point your phone's / camera at me", never "Point your phone's camera at / me").
Wrapped balanced(const Wrapped& w, const std::string& text, int width) {
    if (w.lines.size() < 2 || !w.fits) return w;
    Wrapped best = w;
    for (int narrower = width - 4; narrower > 8; narrower -= 4) {
        Wrapped t = wrap(text, narrower, w.size);
        if (t.lines.size() != w.lines.size() || !t.fits) break;
        best = t;
    }
    return best;
}
}  // namespace

std::vector<std::string> wrap_lines(const std::string& text, int width, int size, bool* fits, bool even) {
    Wrapped w = wrap(text, width, size);
    if (even) w = balanced(w, text, width);
    if (fits) *fits = w.fits;
    return w.lines;
}

bool text_fits(const Box& area, const std::string& text, int max_size, int min_size) {
    return best_fit(area, text, max_size, min_size).fits;
}

int text_height(const Box& area, const std::string& text, int max_size, int min_size) {
    return height_of(best_fit(area, text, max_size, min_size));
}

Box draw_text(Canvas& c, Layout* l, const Box& area, const std::string& text, uint16_t color, int max_size,
              int min_size, Align align) {
    Wrapped w = best_fit(area, text, max_size, min_size);
    if (align == Align::Center) w = balanced(w, text, area.w);
    Box ink{area.x, area.y, 0, height_of(w)};
    int left = area.x + area.w, right = area.x;
    const int pitch = font_size(w.size).pitch;
    for (size_t i = 0; i < w.lines.size(); i++) {
        int lw = text_px(w.lines[i], w.size);
        int x = align == Align::Center ? area.x + (area.w - lw) / 2 : area.x;
        c.text(w.lines[i], x, area.y + int(i) * pitch, w.size, color);
        left = std::min(left, x);
        right = std::max(right, x + lw);
    }
    if (!w.lines.empty()) { ink.x = left; ink.w = right - left; }
    if (l && ink.h > 0) l->add("text: " + text, ink, w.fits);
    return ink;
}

Box draw_qr(Canvas& c, Layout* l, const Qr& q, int x, int y, int module, int quiet) {
    Box b{x, y, qr_box(q, module, quiet), qr_box(q, module, quiet)};
    c.rect(b.x, b.y, b.w, b.h, rgb(255, 255, 255));
    for (int my = 0; my < q.size; my++)
        for (int mx = 0; mx < q.size; mx++)
            if (q.at(mx, my)) c.rect(x + (quiet + mx) * module, y + (quiet + my) * module, module, module, 0);
    if (l) l->add("qr", b);
    return b;
}

}  // namespace nestbox
