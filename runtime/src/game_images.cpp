// English for the game's pictures with Japanese text in them: mission title cards, briefing labels, menu prompts,
// results-screen labels, names in the movie viewer.
//
// The game's named pictures (a 16-byte header: u16 width, u16 height, u16 colours (256 = 8 bits per pixel, else 4), u16 0,
// an 8-letter name; then the colour indices, two pixels a byte for 4 bits, high nibble first) are all loaded by
// func_8009930C(dst, ROM base, asset table, index), which unpacks one asset to dst and returns its size. The live build
// (live.cpp) puts load_asset below in its place: it calls the game's loader, and if what arrived is a picture with a
// patch in game_images_en.inc, writes the patch's colour indices over the picture's rectangle. Patches are made by
// scripts\game_images.py from runtime\text\game_images_en.tsv: the English drawn into the picture's own colours (README
// "Game text"); no part of the original picture is in them.
//
// Settings -> General -> English Game Text switches this off too (pictures loaded after that stay Japanese).
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp.h"

namespace adam {
    bool get_game_text_enabled(); // config.cpp
}

namespace adam::game_text {
    struct Wrapped {
        uint32_t vram;
        recomp_func_t* wrapper;
        recomp_func_t** original;
    };
}

namespace {
    struct PatchData {
        const char* name;
        int width, height, bits;
        int x0, y0, x1, y1;
        const char* rle; // pairs of hex bytes: count - 1, colour index
    };

    const PatchData patch_data[] = {
#include "game_images_en.inc"
    };

    std::atomic<bool> enabled{ true };
    recomp_func_t* original_load = nullptr; // func_8009930C

    int hex_digit(char c) {
        return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    }

    const std::unordered_map<std::string, std::vector<const PatchData*>>& patches() {
        static const std::unordered_map<std::string, std::vector<const PatchData*>> map = [] {
            std::unordered_map<std::string, std::vector<const PatchData*>> m;
            for (const PatchData& p : patch_data) {
                m[p.name].push_back(&p);
            }
            return m;
        }();
        return map;
    }

    void write_pixel(uint8_t* rdram, gpr pixels, int width, int bits, int x, int y, uint8_t value) {
        if (bits == 8) {
            MEM_B(y * width + x, pixels) = (int8_t)value;
            return;
        }
        int offset = (y * width + x) / 2;
        uint8_t old = (uint8_t)MEM_BU(offset, pixels);
        uint8_t nibble = value & 0x0F;
        MEM_B(offset, pixels) = (int8_t)((x & 1) == 0 ? (old & 0x0F) | (nibble << 4) : (old & 0xF0) | nibble);
    }

    void apply(uint8_t* rdram, gpr pixels, const PatchData& p) {
        const char* r = p.rle;
        int w = p.x1 - p.x0, h = p.y1 - p.y0, at = 0, total = w * h;
        while (r[0] && r[1] && r[2] && r[3] && at < total) {
            int count = hex_digit(r[0]) * 16 + hex_digit(r[1]) + 1;
            uint8_t value = (uint8_t)(hex_digit(r[2]) * 16 + hex_digit(r[3]));
            r += 4;
            for (int i = 0; i < count && at < total; i++, at++) {
                write_pixel(rdram, pixels, p.width, p.bits, p.x0 + at % w, p.y0 + at / w, value);
            }
        }
    }

    // func_8009930C(dst, ROM base, table, index) -> unpacked size
    void load_asset(uint8_t* rdram, recomp_context* ctx) {
        gpr dst = ctx->r4;
        original_load(rdram, ctx);
        if (!enabled.load(std::memory_order_relaxed)) {
            return;
        }
        uint32_t size = (uint32_t)ctx->r2;
        uint32_t d = (uint32_t)dst;
        if (size < 16 || d < 0x80000000u || d + size > 0x80800000u) {
            return;
        }
        char name[9] = {};
        for (int i = 0; i < 8; i++) {
            name[i] = (char)MEM_BU(8 + i, dst);
        }
        auto it = patches().find(name);
        if (it == patches().end()) {
            return;
        }
        int width = MEM_HU(0, dst), height = MEM_HU(2, dst), colours = MEM_HU(4, dst);
        int bits = colours == 256 ? 8 : 4;
        uint32_t needed = 16u + (uint32_t)(bits == 8 ? width * height : width * height / 2);
        if (size < needed) {
            return;
        }
        for (const PatchData* p : it->second) {
            if (p->width == width && p->height == height && p->bits == bits) {
                apply(rdram, dst + 16, *p);
            }
        }
    }
}

namespace adam::game_images {
    const std::vector<adam::game_text::Wrapped>& wrapped() {
        static const std::vector<adam::game_text::Wrapped> list = {
            { 0x8009930C, load_asset, &original_load },
        };
        return list;
    }

    // Every tick on the main thread (main.cpp update_gfx).
    void update() {
        enabled.store(adam::get_game_text_enabled(), std::memory_order_relaxed);
    }
}
