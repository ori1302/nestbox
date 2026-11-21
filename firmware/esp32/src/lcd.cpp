#include "lcd.h"
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <soc/gpio_reg.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "board.h"
#include "screen/slices.h"

namespace lcd {

namespace {
// ST7789 commands (Sitronix ST7789V2 datasheet, section 9).
constexpr uint8_t SLPIN = 0x10, DISPOFF = 0x28;     // ST7789: sleep in, display off
constexpr uint8_t SWRESET = 0x01, SLPOUT = 0x11, NORON = 0x13, INVON = 0x21, DISPON = 0x29, CASET = 0x2A,
                  RASET = 0x2B, RAMWR = 0x2C, MADCTL = 0x36, COLMOD = 0x3A, RAMCTRL = 0xB0, PORCTRL = 0xB2,
                  GCTRL = 0xB7, VCOMS = 0xBB, LCMCTRL = 0xC0, VDVVRHEN = 0xC2, VRHS = 0xC3, VDVS = 0xC4,
                  FRCTRL2 = 0xC6, PWCTRL1 = 0xD0, PVGAMCTRL = 0xE0, NVGAMCTRL = 0xE1;
constexpr uint8_t MADCTL_LANDSCAPE = 0x60, MADCTL_LANDSCAPE_FLIPPED = 0xA0;   // MX|MV, MY|MV

constexpr int ROWS = 24;                               // rows per strip: 13-15 KB
constexpr int CHUNKS = H / ROWS;
constexpr int BUFFERS = 2;                             // one strip is filled while the other goes out
constexpr size_t CHUNK_PX = size_t(W) * ROWS;
static_assert(H % ROWS == 0, "whole strips");
// Only what changed goes to the panel: each row is checked in SLICES slices (engine/screen/slices.h) against a
// fingerprint of what the panel shows, and a strip sends one rectangle around its changed slices. Every STRIP_EVERY
// frames one strip goes whole, in turn down the screen, so a fingerprint that missed a change (about one in four
// billion) shows for 5 s at most.
using nestbox::SLICES;
constexpr int SLICE_W = W / SLICES;
static_assert(W % SLICES == 0 && SLICE_W % 2 == 0, "slices of whole pixel pairs");
constexpr int STRIP_EVERY = 15;
// A strip's rectangle: CASET and its 4 bytes, RASET and its 4 bytes, RAMWR, the pixels.
constexpr int STRIP_TRANSACTIONS = 6;
// Waveshare's demos run the panel at 40 MHz; these pins go through the GPIO matrix, so faster isn't safe.
// A frame takes 280*240*16 bits / 40 MHz = 27 ms on the bus (320x240: 31 ms).
constexpr int SPI_HZ = 40 * 1000 * 1000;
constexpr uint32_t SLEEP_SETTLE_MS = 120;              // ST7789: wait this long after sleep in or out
constexpr int BACKLIGHT_PWM_HZ = 20000;                // above hearing: no whine from the backlight
constexpr int BACKLIGHT_BITS = 8;
constexpr uint32_t BACKLIGHT_MAX = (1u << BACKLIGHT_BITS) - 1;
constexpr int BYTES_PER_PIXEL = 2;
constexpr int BITS_PER_PIXEL = 16;
constexpr uint32_t RESET_PULSE_MS = 10, RESET_WAIT_MS = 120;   // Waveshare's driver
// The sender: a task on the other core puts each frame on the bus while this core draws the next.
constexpr uint32_t SENDER_STACK = 3072;
constexpr UBaseType_t SENDER_PRIORITY = 3;
constexpr BaseType_t SENDER_CORE = 0;
constexpr int SHORT_DATA_BYTES = 4;                    // parameters this short travel in the transaction itself

// Marks a transaction for set_dc(): D/C high sends data, low a command.
void* const DATA = reinterpret_cast<void*>(1);
void* const COMMAND = nullptr;

// Lets swap_copy() read and write the 16-bit frame two pixels at a time without breaking strict aliasing.
typedef uint32_t __attribute__((may_alias)) PixelPair;

spi_device_handle_t dev = nullptr;
// Two strips of the frame, bytes swapped, in internal DMA memory: a whole frame there (134 KB) would leave the
// network too little internal RAM, so frames go out a strip at a time, each strip swapped while the last is sent.
uint16_t* bufs[BUFFERS] = {};
spi_transaction_t trans[BUFFERS][STRIP_TRANSACTIONS];
int queued[BUFFERS] = {};                              // a buffer's transactions not yet collected
int fifo[BUFFERS * STRIP_TRANSACTIONS];                // which buffer each queued transaction belongs to, in order
int fifo_head = 0, fifo_count = 0;
// What the panel shows, a fingerprint per slice (H * SLICES, in PSRAM).
uint32_t* shown = nullptr;
bool send_all = true;                                  // the next frame goes whole (first frame, after a flip)
uint32_t frames_sent = 0;
std::atomic<uint32_t> stat_frames{0};
std::atomic<uint64_t> stat_pixels{0};

// Runs in the SPI interrupt, maybe while flash is busy: plain register writes only. GPIO32 and up are set through
// the second output register (the LCD-2's D/C is GPIO42).
void IRAM_ATTR set_dc(spi_transaction_t* t) {
    constexpr bool HIGH_BANK = board::LCD_DC >= 32;
    constexpr uint32_t BIT = 1u << (board::LCD_DC % 32);
    if (HIGH_BANK) REG_WRITE(t->user ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, BIT);
    else REG_WRITE(t->user ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, BIT);
}

void send(uint8_t cmd, const uint8_t* data = nullptr, size_t n = 0) {
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    t.user = COMMAND;
    spi_device_polling_transmit(dev, &t);
    if (!n) return;
    spi_transaction_t d{};
    d.length = n * 8;
    d.user = DATA;
    if (n <= SHORT_DATA_BYTES) {
        d.flags = SPI_TRANS_USE_TXDATA;
        memcpy(d.tx_data, data, n);
    } else {
        d.tx_buffer = data;
    }
    spi_device_polling_transmit(dev, &d);
}

struct Cmd {
    uint8_t cmd, len;
    uint8_t data[14];
    uint16_t wait_ms;
};
#ifdef NESTBOX_BOARD_LCD2
// Waveshare's ST7789 setup for their 2-inch 240x320 panel (power, porch, gamma), with inversion on for true colours.
const Cmd INIT[] = {
    {SLPOUT, 0, {}, 120},
    {COLMOD, 1, {0x55}, 0},                            // 16-bit RGB565
    {RAMCTRL, 2, {0x00, 0xF0}, 0},                     // MSB first
    {PORCTRL, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0},
    {GCTRL, 1, {0x35}, 0},
    {VCOMS, 1, {0x1F}, 0},
    {LCMCTRL, 1, {0x2C}, 0},
    {VDVVRHEN, 1, {0x01}, 0},
    {VRHS, 1, {0x12}, 0},
    {VDVS, 1, {0x20}, 0},
    {FRCTRL2, 1, {0x0F}, 0},                           // 60 Hz refresh
    {PWCTRL1, 2, {0xA4, 0xA1}, 0},
    {PVGAMCTRL, 14, {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D}, 0},
    {NVGAMCTRL, 14, {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31}, 0},
    {INVON, 0, {}, 0},
    {NORON, 0, {}, 10},
    {DISPON, 0, {}, 0},
};
#else
// Waveshare's ST7789V2 setup for this panel (their Arduino_GFX build: power, porch, gamma), plus inversion
// on, which IPS panels need for true colours (both of their demos turn it on).
const Cmd INIT[] = {
    {SLPOUT, 0, {}, 120},
    {COLMOD, 1, {0x55}, 0},                            // 16-bit RGB565
    {RAMCTRL, 2, {0x00, 0xF0}, 0},                     // MSB first
    {PORCTRL, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0},
    {GCTRL, 1, {0x35}, 0},
    {VCOMS, 1, {0x19}, 0},
    {LCMCTRL, 1, {0x2C}, 0},
    {VDVVRHEN, 1, {0x01}, 0},
    {VRHS, 1, {0x12}, 0},
    {VDVS, 1, {0x20}, 0},
    {FRCTRL2, 1, {0x0F}, 0},                           // 60 Hz refresh
    {PWCTRL1, 2, {0xA4, 0xA1}, 0},
    {PVGAMCTRL, 14, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}, 0},
    {NVGAMCTRL, 14, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}, 0},
    {INVON, 0, {}, 0},
    {NORON, 0, {}, 10},
    {DISPON, 0, {}, 0},
};
#endif

// RGB565 as the CPU stores it (little-endian) to the panel's order (high byte first), two pixels at a time, and a
// fingerprint of them.
uint32_t swap_copy(uint16_t* dst, const uint16_t* src, size_t n) {
    auto* d = reinterpret_cast<PixelPair*>(dst);
    auto* s = reinterpret_cast<const PixelPair*>(src);
    uint32_t h = nestbox::SLICE_SEED;
    for (size_t i = 0; i < n / 2; i++) {
        const uint32_t v = s[i];
        d[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
        h = nestbox::slice_step(h, v);
    }
    return h;
}

// A command, or up to 4 bytes of its parameters, as a queued transaction.
spi_transaction_t short_trans(const uint8_t* bytes, int n, void* dc) {
    spi_transaction_t t{};
    t.length = size_t(n) * 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, bytes, size_t(n));
    t.user = dc;
    return t;
}

// The two bytes of a start and an end address, high byte first.
void address(uint8_t out[4], int from, int to) {
    out[0] = uint8_t(from >> 8);
    out[1] = uint8_t(from & 0xFF);
    out[2] = uint8_t(to >> 8);
    out[3] = uint8_t(to & 0xFF);
}

void reset_panel() {
    if (board::LCD_RST >= 0) {                         // the LCD-2's panel has no reset pin: the command does it
        digitalWrite(board::LCD_RST, HIGH);
        delay(RESET_PULSE_MS);
        digitalWrite(board::LCD_RST, LOW);
        delay(RESET_WAIT_MS);
        digitalWrite(board::LCD_RST, HIGH);
        delay(RESET_WAIT_MS);
    }
    send(SWRESET);
    delay(RESET_WAIT_MS);
}

bool ready() { return bufs[0] != nullptr; }
}  // namespace

void backlight(int percent) {
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    ledcWrite(board::LCD_BL, uint32_t(percent) * BACKLIGHT_MAX / 100);
}

namespace {
// Collects the oldest queued transaction (they finish in order).
void collect() {
    spi_transaction_t* done;
    spi_device_get_trans_result(dev, &done, portMAX_DELAY);
    queued[fifo[fifo_head]]--;
    fifo_head = (fifo_head + 1) % (BUFFERS * STRIP_TRANSACTIONS);
    fifo_count--;
}

// Everything queued, finished.
void drain() {
    while (fifo_count > 0) collect();
}

void queue(int buffer, spi_transaction_t& t) {
    if (spi_device_queue_trans(dev, &t, portMAX_DELAY) != ESP_OK) {
        send_all = true;                               // the panel may now differ from the fingerprints
        return;
    }
    fifo[(fifo_head + fifo_count) % (BUFFERS * STRIP_TRANSACTIONS)] = buffer;
    fifo_count++;
    queued[buffer]++;
}

// One strip of the frame: swapped into `buffer` and checked slice by slice; the rectangle around what changed goes
// out, with its own window. With MV set, x (the long side) is the column address, from the gap (20 on the LCD-1.69).
void send_strip(int buffer, const uint16_t* px, uint32_t* held, int strip, bool all) {
    uint16_t* out = bufs[buffer];
    const int top = strip * ROWS;
    int first = -1, last = -1, left = SLICES, right = -1;
    for (int r = 0; r < ROWS; r++) {
        const int y = top + r;
        bool changed = false;
        for (int k = 0; k < SLICES; k++) {
            const size_t at = size_t(r) * W + size_t(k) * SLICE_W;
            const uint32_t h = swap_copy(out + at, px + size_t(y) * W + size_t(k) * SLICE_W, SLICE_W);
            const size_t i = size_t(y) * SLICES + k;
            if (held) held[i] = h;
            if (!all && h == shown[i]) continue;
            shown[i] = h;
            changed = true;
            left = std::min(left, k);
            right = std::max(right, k);
        }
        if (changed) {
            if (first < 0) first = r;
            last = r;
        }
    }
    if (first < 0) return;
    const int x0 = left * SLICE_W, w = (right - left + 1) * SLICE_W, rows = last - first + 1;
    if (w != W || first != 0)                          // pack the rectangle's rows together, in place
        for (int r = 0; r < rows; r++)
            memmove(out + size_t(r) * w, out + size_t(first + r) * W + x0, size_t(w) * BYTES_PER_PIXEL);
    uint8_t xs[4], ys[4];
    address(xs, board::LCD_GAP + x0, board::LCD_GAP + x0 + w - 1);
    address(ys, top + first, top + last);
    spi_transaction_t* t = trans[buffer];
    t[0] = short_trans(&CASET, 1, COMMAND);
    t[1] = short_trans(xs, 4, DATA);
    t[2] = short_trans(&RASET, 1, COMMAND);
    t[3] = short_trans(ys, 4, DATA);
    t[4] = short_trans(&RAMWR, 1, COMMAND);
    t[5] = spi_transaction_t{};
    t[5].length = size_t(w) * rows * BITS_PER_PIXEL;
    t[5].tx_buffer = out;
    t[5].user = DATA;
    for (int i = 0; i < STRIP_TRANSACTIONS; i++) queue(buffer, t[i]);
    stat_pixels += uint64_t(w) * rows;
}

// One frame onto the bus, a strip at a time, only what changed; returns when the last strip has gone.
void send_frame(const uint16_t* px, uint32_t* held) {
    const bool all = send_all;
    send_all = false;
    const uint32_t round = frames_sent++;
    const int whole = round % STRIP_EVERY ? -1 : int(round / STRIP_EVERY % CHUNKS);
    for (int i = 0; i < CHUNKS; i++) {
        const int b = i % BUFFERS;
        while (queued[b] > 0) collect();               // the strip about to be refilled has gone out
        send_strip(b, px, held, i, all || i == whole);
    }
    drain();
    stat_frames++;
}

TaskHandle_t sender = nullptr;
SemaphoreHandle_t idle = nullptr;                      // held while a frame is being sent
const uint16_t* next_frame = nullptr;
uint32_t* next_held = nullptr;

void sender_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        send_frame(next_frame, next_held);
        xSemaphoreGive(idle);
    }
}
}  // namespace

void wait_idle() {
    if (sender) {
        xSemaphoreTake(idle, portMAX_DELAY);
        xSemaphoreGive(idle);
    } else {
        drain();
    }
}

void set_flip(bool flip) {
    if (!ready()) return;
    wait_idle();
    const uint8_t madctl = flip ? MADCTL_LANDSCAPE_FLIPPED : MADCTL_LANDSCAPE;
    send(MADCTL, &madctl, 1);
    send_all = true;                                   // the panel's memory now reads the other way round
}

void sleep(bool asleep) {
    if (!ready()) return;
    wait_idle();
    if (asleep) {
        send(DISPOFF);
        send(SLPIN);
        delay(SLEEP_SETTLE_MS);
    } else {
        send(SLPOUT);
        delay(SLEEP_SETTLE_MS);                        // the panel takes commands again after this
        send(DISPON);
        send_all = true;
    }
}

bool begin(bool flip) {
    ledcAttach(board::LCD_BL, BACKLIGHT_PWM_HZ, BACKLIGHT_BITS);
    ledcWrite(board::LCD_BL, 0);                       // dark until the first frame is in
    pinMode(board::LCD_DC, OUTPUT);
    if (board::LCD_RST >= 0) pinMode(board::LCD_RST, OUTPUT);
    if (board::SD_CS >= 0) {                           // the SD card slot shares the bus: keep it deselected
        pinMode(board::SD_CS, OUTPUT);
        digitalWrite(board::SD_CS, HIGH);
    }

    spi_bus_config_t bus{};
    bus.mosi_io_num = board::LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = board::LCD_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = CHUNK_PX * BYTES_PER_PIXEL;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t cfg{};
    cfg.clock_speed_hz = SPI_HZ;
    cfg.mode = 0;
    cfg.spics_io_num = board::LCD_CS;
    cfg.queue_size = BUFFERS * STRIP_TRANSACTIONS;
    cfg.pre_cb = set_dc;
    if (spi_bus_add_device(SPI2_HOST, &cfg, &dev) != ESP_OK) return false;

    constexpr uint32_t DMA_RAM = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    uint16_t* strips[BUFFERS];
    for (uint16_t*& b : strips) {
        b = static_cast<uint16_t*>(heap_caps_malloc(CHUNK_PX * BYTES_PER_PIXEL, DMA_RAM));
        if (!b) return false;
    }

    shown = static_cast<uint32_t*>(heap_caps_calloc(size_t(H) * SLICES, sizeof(uint32_t), MALLOC_CAP_SPIRAM));
    if (!shown) return false;

    reset_panel();
    for (const Cmd& c : INIT) {
        send(c.cmd, c.data, c.len);
        if (c.wait_ms) delay(c.wait_ms);
    }
    for (int i = 0; i < BUFFERS; i++) bufs[i] = strips[i];
    set_flip(flip);
    idle = xSemaphoreCreateBinary();
    if (idle) {
        xSemaphoreGive(idle);
        if (xTaskCreatePinnedToCore(sender_task, "lcd", SENDER_STACK, nullptr, SENDER_PRIORITY, &sender,
                                    SENDER_CORE) != pdPASS)
            sender = nullptr;                          // then push() sends on the caller's own time
    }
    return true;
}

void push(const uint16_t* px, uint32_t* held) {
    if (!ready()) return;
    if (!sender) {
        wait_idle();
        send_frame(px, held);
        return;
    }
    xSemaphoreTake(idle, portMAX_DELAY);              // the frame before has gone
    next_frame = px;
    next_held = held;
    xTaskNotifyGive(sender);
}

void take_send_stats(uint32_t& frames, uint64_t& pixels) {
    frames = stat_frames.exchange(0);
    pixels = stat_pixels.exchange(0);
}

}  // namespace lcd
