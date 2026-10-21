// English for the game's own on-screen text: the mission-intro captions, the briefings and the cutscene lines.
//
// How the game draws text: a string is 16-bit character codes (code - 16 = a glyph of the 538-glyph font at ROM
// 0x1BB280, 1 = line break, 0 = end), kept in the overlays' data. Every such string goes through a few functions in the
// boot code, which draw it into a 4-bit picture (24x24 glyphs, nibble 0 = clear, 1 = outline, 2..15 = grey to white)
// that the caller then shows as a sprite with its own palette:
//   func_800A7354(str, xsp)          the picture's width: longest line in characters x 24 (+ spacing), to 16 pixels
//   func_800A73F4(str, ysp)          its height: lines x 24 (+ spacing)
//   func_800A74A8(str, xsp)          the longest line's width in pixels (proportional glyph widths)
//   func_800A7660(str, xsp, ysp, shade, align)  allocates the picture (func_800C998C) and draws the string into it
//                                    (func_800A77D8 -> func_800A7AEC per glyph); align 3 centres the lines, 4 right-aligns
// The live build (live.cpp) puts the wrappers below in place of those four. A wrapper hashes the string; for one in
// game_text_en.inc (scripts\game_text.py, from runtime\text\game_text_en.tsv) it answers with the English line, drawn
// here with our own font in the game's style; any other string goes to the game's function. The picture keeps the
// Japanese one's width, so the game still puts it where it put the Japanese: an English line wider than that is
// squeezed to fit (the .tsv keeps them about as wide). It's as tall as the taller of the two.
//
// Settings -> General -> English Game Text switches it off (the Japanese again, for text drawn after that).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp.h"

namespace adam {
    bool get_game_text_enabled(); // config.cpp
}

namespace {
    struct GlyphData {
        uint32_t code;     // Unicode
        int width;         // columns of coverage
        int advance4;      // pen advance in 1/4 pixels
        int lead;          // first column's offset from the pen (negative: reaches left)
        const char* hex;   // 24 rows x width coverage bytes, as hex
    };

    struct LineData {
        uint64_t hash;
        const char* text;
    };

    const GlyphData glyph_data[] = {
#define GAME_TEXT_GLYPHS
#include "game_text_en.inc"
#undef GAME_TEXT_GLYPHS
    };

    const LineData line_data[] = {
#define GAME_TEXT_LINES
#include "game_text_en.inc"
#undef GAME_TEXT_LINES
    };

    constexpr int CELL = 24;
    constexpr uint32_t ALLOC_VRAM = 0x800C998C; // the game's allocator (size) -> pointer
    constexpr int MAX_CODES = 1024;

    struct Glyph {
        int width = 0;
        int advance4 = 0;
        int lead = 0;
        std::vector<uint8_t> coverage; // CELL rows x width
    };

    std::atomic<bool> enabled{ true };

    recomp_func_t* original_box_width = nullptr;   // func_800A7354
    recomp_func_t* original_box_height = nullptr;  // func_800A73F4
    recomp_func_t* original_line_width = nullptr;  // func_800A74A8
    recomp_func_t* original_draw = nullptr;        // func_800A7660

    int hex_digit(char c) {
        return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    }

    const std::unordered_map<uint32_t, Glyph>& glyphs() {
        static const std::unordered_map<uint32_t, Glyph> map = [] {
            std::unordered_map<uint32_t, Glyph> m;
            for (const GlyphData& g : glyph_data) {
                Glyph glyph{ g.width, g.advance4, g.lead, std::vector<uint8_t>((size_t)CELL * g.width) };
                for (size_t i = 0; i < glyph.coverage.size(); i++) {
                    glyph.coverage[i] = (uint8_t)(hex_digit(g.hex[i * 2]) * 16 + hex_digit(g.hex[i * 2 + 1]));
                }
                m.emplace(g.code, std::move(glyph));
            }
            return m;
        }();
        return map;
    }

    const std::unordered_map<uint64_t, const char*>& lines() {
        static const std::unordered_map<uint64_t, const char*> map = [] {
            std::unordered_map<uint64_t, const char*> m;
            for (const LineData& l : line_data) {
                m.emplace(l.hash, l.text);
            }
            return m;
        }();
        return map;
    }

    // The string at `addr` (the game's codes, without the 0) and its shape as the game measures it.
    struct GameString {
        uint64_t hash = 0xcbf29ce484222325ull;
        int lines = 1;
        int longest = 0; // characters in the longest line
    };

    bool read_string(uint8_t* rdram, gpr addr, GameString* out) {
        int32_t a = (int32_t)addr;
        if ((uint32_t)a < 0x80000000u || (uint32_t)a >= 0x80800000u || (a & 1) != 0) {
            return false;
        }
        int current = 0;
        for (int i = 0; i < MAX_CODES; i++) {
            uint16_t code = (uint16_t)MEM_HU(i * 2, addr);
            if (code == 0) {
                out->longest = std::max(out->longest, current);
                return true;
            }
            for (uint8_t b : { (uint8_t)(code >> 8), (uint8_t)code }) {
                out->hash ^= b;
                out->hash *= 0x100000001b3ull;
            }
            if (code == 1) {
                out->longest = std::max(out->longest, current);
                current = 0;
                out->lines++;
            }
            else {
                current++;
            }
        }
        return false;
    }

    std::vector<uint32_t> utf8_decode(const char* s) {
        std::vector<uint32_t> out;
        const unsigned char* p = (const unsigned char*)s;
        while (*p) {
            uint32_t c = *p++;
            if (c >= 0xF0) {
                c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
                p += 3;
            }
            else if (c >= 0xE0) {
                c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
                p += 2;
            }
            else if (c >= 0xC0) {
                c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
                p += 1;
            }
            out.push_back(c);
        }
        return out;
    }

    // An English string laid out: its lines (code points) and the picture's size.
    struct Layout {
        std::vector<std::vector<uint32_t>> lines;
        int width = 0;   // the picture (the Japanese one's width)
        int height = 0;
        int longest = 0; // the longest English line in pixels
    };

    const Glyph* glyph_for(uint32_t c) {
        const auto& map = glyphs();
        auto it = map.find(c);
        if (it == map.end()) {
            it = map.find('?');
        }
        return it == map.end() ? nullptr : &it->second;
    }

    int line_pixels(const std::vector<uint32_t>& line) {
        int advance4 = 0;
        for (uint32_t c : line) {
            if (const Glyph* g = glyph_for(c)) {
                advance4 += g->advance4;
            }
        }
        return (advance4 + 3) / 4;
    }

    // Returns false for a string without an English line (or with English text off).
    bool layout_for(uint8_t* rdram, gpr str, int xsp, int ysp, Layout* out) {
        if (!enabled.load(std::memory_order_relaxed)) {
            return false;
        }
        GameString game;
        if (!read_string(rdram, str, &game)) {
            return false;
        }
        auto it = lines().find(game.hash);
        if (it == lines().end()) {
            return false;
        }
        out->lines.clear();
        out->lines.emplace_back();
        for (uint32_t c : utf8_decode(it->second)) {
            if (c == '\n') {
                out->lines.emplace_back();
            }
            else {
                out->lines.back().push_back(c);
            }
        }
        out->longest = 0;
        for (const auto& line : out->lines) {
            out->longest = std::max(out->longest, line_pixels(line));
        }
        // As func_800A7354 / func_800A73F4 measure the Japanese.
        out->width = (game.longest * CELL + xsp * (game.longest - 1) + 15) & ~15;
        out->width = std::max(out->width, 16);
        int rows = std::max(game.lines, (int)out->lines.size());
        out->height = rows * CELL + ysp * (rows - 1);
        return true;
    }

    // One line's coverage (CELL rows x the returned width, 0..255) at full size.
    std::vector<uint8_t> draw_line(const std::vector<uint32_t>& line, int* width) {
        int w = line_pixels(line) + 2;
        std::vector<uint8_t> out((size_t)CELL * w, 0);
        int pen4 = 0;
        for (uint32_t c : line) {
            const Glyph* g = glyph_for(c);
            if (g == nullptr) {
                continue;
            }
            int x0 = pen4 / 4 + g->lead;
            for (int y = 0; y < CELL; y++) {
                for (int gx = 0; gx < g->width; gx++) {
                    int x = x0 + gx;
                    if (x < 0 || x >= w) {
                        continue;
                    }
                    uint8_t v = g->coverage[(size_t)y * g->width + gx];
                    uint8_t& d = out[(size_t)y * w + x];
                    d = std::max(d, v);
                }
            }
            pen4 += g->advance4;
        }
        *width = w;
        return out;
    }

    // Narrows a line to `to` columns (each new column averages the old ones it covers).
    std::vector<uint8_t> squeeze(const std::vector<uint8_t>& line, int from, int to) {
        std::vector<uint8_t> out((size_t)CELL * to, 0);
        double k = (double)from / to;
        for (int c = 0; c < to; c++) {
            double a = c * k, b = (c + 1) * k;
            for (int y = 0; y < CELL; y++) {
                double acc = 0;
                for (int i = (int)a; i < b && i < from; i++) {
                    double part = std::min(b, (double)i + 1) - std::max(a, (double)i);
                    acc += line[(size_t)y * from + i] * part;
                }
                out[(size_t)y * to + c] = (uint8_t)std::lround(acc / k);
            }
        }
        return out;
    }

    // Draws the layout into the game's 4-bit picture at `dst` (width x height, two pixels a byte, high nibble first):
    // the glyphs' coverage, then the game's look: fill 2..15 by coverage, outline 1 around it and a shadow down-right.
    void draw(uint8_t* rdram, gpr dst, const Layout& layout, int ysp, int align) {
        int w = layout.width, h = layout.height;
        std::vector<uint8_t> cover((size_t)w * h, 0);
        for (size_t i = 0; i < layout.lines.size(); i++) {
            int lw = 0;
            std::vector<uint8_t> line = draw_line(layout.lines[i], &lw);
            if (lw > w) {
                line = squeeze(line, lw, w);
                lw = w;
            }
            int used = std::min(lw, line_pixels(layout.lines[i]));
            int x0 = align == 3 ? (w - used) / 2 : align == 4 ? w - used : 0;
            x0 = std::max(0, std::min(x0, w - lw));
            int y0 = (int)i * (CELL + ysp);
            for (int y = 0; y < CELL; y++) {
                if (y0 + y < 0 || y0 + y >= h) {
                    continue;
                }
                for (int x = 0; x < lw; x++) {
                    uint8_t& d = cover[(size_t)(y0 + y) * w + x0 + x];
                    d = std::max(d, line[(size_t)y * lw + x]);
                }
            }
        }
        constexpr uint8_t SOLID = 20; // coverage that counts as ink (0.08)
        static const int around[][2] = { {-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}, {2,2} };
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t c = cover[(size_t)y * w + x];
                int nibble = 0;
                if (c > SOLID) {
                    nibble = std::max(2, (int)std::lround(1 + c * 14 / 255.0));
                }
                else {
                    for (const auto& o : around) {
                        int sy = y - o[0], sx = x - o[1];
                        if (sy >= 0 && sy < h && sx >= 0 && sx < w && cover[(size_t)sy * w + sx] > SOLID) {
                            nibble = 1;
                            break;
                        }
                    }
                }
                gpr byte_addr = dst + (gpr)(int32_t)((y * w + x) / 2);
                uint8_t old = (uint8_t)MEM_BU(0, byte_addr);
                uint8_t value = (x & 1) == 0 ? (uint8_t)((old & 0x0F) | (nibble << 4)) : (uint8_t)((old & 0xF0) | nibble);
                MEM_B(0, byte_addr) = (int8_t)value;
            }
        }
    }

    int8_t s8(gpr v) {
        return (int8_t)(uint8_t)v;
    }

    // func_800A7354(str, xsp) -> picture width
    void box_width(uint8_t* rdram, recomp_context* ctx) {
        Layout layout;
        if (!layout_for(rdram, ctx->r4, s8(ctx->r5), 0, &layout)) {
            original_box_width(rdram, ctx);
            return;
        }
        ctx->r2 = (gpr)(int32_t)layout.width;
    }

    // func_800A73F4(str, ysp) -> picture height
    void box_height(uint8_t* rdram, recomp_context* ctx) {
        Layout layout;
        if (!layout_for(rdram, ctx->r4, 0, s8(ctx->r5), &layout)) {
            original_box_height(rdram, ctx);
            return;
        }
        ctx->r2 = (gpr)(int32_t)layout.height;
    }

    // func_800A74A8(str, xsp) -> longest line in pixels
    void line_width(uint8_t* rdram, recomp_context* ctx) {
        Layout layout;
        if (!layout_for(rdram, ctx->r4, s8(ctx->r5), 0, &layout)) {
            original_line_width(rdram, ctx);
            return;
        }
        ctx->r2 = (gpr)(int32_t)std::min(layout.longest, layout.width);
    }

    // func_800A7660(str, xsp, ysp, shade, align on the stack) -> the picture
    void draw_string(uint8_t* rdram, recomp_context* ctx) {
        Layout layout;
        int ysp = s8(ctx->r6);
        if (!layout_for(rdram, ctx->r4, s8(ctx->r5), ysp, &layout)) {
            original_draw(rdram, ctx);
            return;
        }
        int align = (uint8_t)MEM_BU(0x13, ctx->r29); // 5th argument, a byte: sp+0x10 (big-endian word, low byte)
        ctx->r4 = (gpr)(int32_t)((layout.width / 2) * layout.height);
        get_function((int32_t)ALLOC_VRAM)(rdram, ctx);
        gpr dst = ctx->r2;
        if ((uint32_t)dst < 0x80000000u || (uint32_t)dst >= 0x80800000u) {
            return; // out of memory: the game gets what the allocator gave
        }
        draw(rdram, dst, layout, ysp, align);
        ctx->r2 = dst;
    }
}

namespace adam::game_text {
    struct Wrapped {
        uint32_t vram;
        recomp_func_t* wrapper;
        recomp_func_t** original;
    };

    // The functions live.cpp replaces, with where it keeps the game's own.
    const std::vector<Wrapped>& wrapped() {
        static const std::vector<Wrapped> list = {
            { 0x800A7354, box_width, &original_box_width },
            { 0x800A73F4, box_height, &original_box_height },
            { 0x800A74A8, line_width, &original_line_width },
            { 0x800A7660, draw_string, &original_draw },
        };
        return list;
    }

    // Every tick on the main thread (main.cpp update_gfx): follows the English Game Text switch.
    void update() {
        enabled.store(adam::get_game_text_enabled(), std::memory_order_relaxed);
    }
}
