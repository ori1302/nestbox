// Constants and helpers shared by the Pet and Stage implementation files. Not part of the public API.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "egg/egg.h"
#include "screen/canvas.h"
#include "util/math.h"

namespace nestbox {

// Facing rows, as eggs store them, clockwise from the front.
constexpr int ROWS = FACING_ROWS;
constexpr int ROW_FRONT = 0, ROW_FRONT_RIGHT = 1, ROW_RIGHT = 2, ROW_BACK_RIGHT = 3, ROW_BACK = 4, ROW_FRONT_LEFT = 7;

constexpr int DASH_PX = 4;             // a dashing strike's extra slide, art pixels
constexpr float WALK_PX_S = 66;        // walking speed, screen pixels per second
constexpr uint32_t TURN_STEP_MS = 90;  // one of the eight facings per step
constexpr int CELL = 4;                // path-finding grid, screen pixels
constexpr int DESK_LEGS_END = -3;      // the plain desk's leg tips, art pixels below the pet's feet
constexpr int PLAIN_DESK_HALF = 17;    // the plain desk's half width, art pixels
constexpr float EYE_HEIGHT = 0.6f;     // the pet's eyes above its feet, as a fraction of its height
constexpr float JUMP_MS = 650;         // a jump into or out of the water, however far
constexpr int JUMP_HEIGHT = 12;        // the top of its arc above the straight way, art pixels

// The facing row for a movement or gaze along (vx, vy), screen axes (y down).
inline int row_toward(float vx, float vy) {
    static constexpr int SECTOR_TO_ROW[ROWS] = {2, 1, 0, 7, 6, 5, 4, 3};   // sector 0 = right, clockwise
    return SECTOR_TO_ROW[((int(std::lround(std::atan2(vy, vx) / (PI / 4))) % ROWS) + ROWS) % ROWS];
}

// One turning step from `row` toward `goal`, the shorter way round; a half turn steps to the lower row.
inline int turn_step(int row, int goal) {
    int d = (goal - row + ROWS) % ROWS;
    return d == 0 ? row : (row + (d < ROWS / 2 ? 1 : ROWS - 1)) % ROWS;
}

// The turning steps between two facings, the shorter way round.
inline int turns_apart(int row, int goal) { return std::min((goal - row + ROWS) % ROWS, (row - goal + ROWS) % ROWS); }

// Light on water: ripples and splashes, by day and at night.
constexpr uint16_t WATER_LIGHT = rgb(0xe4, 0xf3, 0xf6), WATER_LIGHT_NIGHT = rgb(0x8f, 0xa6, 0xc0);

// A flattened ring of dots round (x, y), radii rx and ry, on the grid of `u` screen pixels. `broken`: every other dot,
// as a ring breaks up while it fades.
inline void draw_ring(Canvas& c, float x, float y, float rx, float ry, int u, uint16_t colour, bool broken = false) {
    const int points = std::max(8, int(rx * 2));
    for (int i = 0; i < points; i++) {
        if (broken && i % 2) continue;
        const float a = float(FULL_TURN) * i / points;
        const int px = int(x + rx * std::cos(a)) / u * u, py = int(y + ry * std::sin(a)) / u * u;
        if (px >= 0 && px < c.w && py >= 0 && py < c.h) c.rect(px, py, u, u, colour);
    }
}

}  // namespace nestbox
