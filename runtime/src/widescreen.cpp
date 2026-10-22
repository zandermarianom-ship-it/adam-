// Widescreen without the game's black frame.
//
// The game keeps a 16-pixel black frame around its picture: the RDP scissor it sets for each frame (func_8009C06C,
// from the globals ulx 0x80149FB0, uly 0x80149FA4, lrx 0x8016D040, lry 0x8014A2C8: 16, 16, 304, 224 at 320x240; set
// by func_8009BF40 and others) is 16 pixels in from every edge, while its viewport covers the whole screen. With a wide
// aspect ratio (Expand) RT64 widens the picture but keeps that inset, so the extra width ended in black bars on all
// four sides. While the picture is widened (main.cpp's check_aspect_ratio: wide aspect ratios are used only in mission
// battles), the wrapper below has func_8009C06C emit a scissor covering the whole screen instead; the globals are put
// back right after, so the game sees its own values. Live build only (live.cpp puts the wrapper in the tables).
#include <atomic>
#include <cstdint>
#include <vector>

#include "recomp.h"

namespace adam::game_text {
    struct Wrapped {
        uint32_t vram;
        recomp_func_t* wrapper;
        recomp_func_t** original;
    };
}

namespace {
    constexpr uint32_t ULX = 0x80149FB0, ULY = 0x80149FA4, LRX = 0x8016D040, LRY = 0x8014A2C8;

    std::atomic<bool> full_screen{ false };
    recomp_func_t* original_scissor = nullptr; // func_8009C06C

    gpr addr(uint32_t a) {
        return (gpr)(int32_t)a;
    }

    void emit_scissor(uint8_t* rdram, recomp_context* ctx) {
        if (!full_screen.load(std::memory_order_relaxed)) {
            original_scissor(rdram, ctx);
            return;
        }
        int32_t ulx = MEM_W(0, addr(ULX)), uly = MEM_W(0, addr(ULY)), lrx = MEM_W(0, addr(LRX)), lry = MEM_W(0, addr(LRY));
        // Only the usual centred frame (the same inset on both sides) is opened up; anything else is the game's own.
        bool framed = ulx > 0 && uly > 0 && ulx <= 32 && uly <= 32;
        if (framed) {
            MEM_W(0, addr(ULX)) = 0;
            MEM_W(0, addr(ULY)) = 0;
            MEM_W(0, addr(LRX)) = lrx + ulx;
            MEM_W(0, addr(LRY)) = lry + uly;
        }
        original_scissor(rdram, ctx);
        if (framed) {
            MEM_W(0, addr(ULX)) = ulx;
            MEM_W(0, addr(ULY)) = uly;
            MEM_W(0, addr(LRX)) = lrx;
            MEM_W(0, addr(LRY)) = lry;
        }
    }
}

namespace adam::widescreen {
    const std::vector<adam::game_text::Wrapped>& wrapped() {
        static const std::vector<adam::game_text::Wrapped> list = {
            { 0x8009C06C, emit_scissor, &original_scissor },
        };
        return list;
    }

    // main.cpp: true while a wide aspect ratio is in use.
    void set_full_screen(bool on) {
        full_screen.store(on, std::memory_order_relaxed);
    }
}
