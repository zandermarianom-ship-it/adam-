// 60 FPS (Settings -> General, experimental, off by default).
//
// The game runs one frame every few VIs: func_80099124(vis) sets how many (0x80149F90 = vis, 0x8016D168 = 60 / vis;
// 2 = 30 fps in most scenes, 3 = 20 fps in fights), and the frame loop (func_800A8B34, via func_800B68A0) waits until
// that many VIs have passed. The game logic advances by that same number everywhere (hundreds of reads of 0x80149F90 in
// the mission and cutscene code: movement, timers and animation are scaled by it), so with 1 the game runs every VI
// at the same speed: 60 frames per second. The wrapper below passes 1 to func_80099124 while the setting is on and
// remembers what the game asked for, so switching it off puts the game's own value back. Parts of the game that don't
// scale by it would run too fast; hence experimental. Live build only (live.cpp puts the wrapper in the tables).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "recomp.h"

namespace adam {
    bool get_60fps_enabled(); // config.cpp
}

namespace adam::game_text {
    struct Wrapped {
        uint32_t vram;
        recomp_func_t* wrapper;
        recomp_func_t** original;
    };
}

namespace {
    constexpr uint32_t VIS_PER_FRAME = 0x80149F90, FRAMES_PER_SECOND = 0x8016D168;

    std::atomic<bool> enabled{ false };
    std::atomic<int32_t> requested{ 0 };      // the game's own last value (0: not set yet)
    std::atomic<uint8_t*> game_rdram{ nullptr };
    recomp_func_t* original_set_rate = nullptr; // func_80099124

    gpr addr(uint32_t a) {
        return (gpr)(int32_t)a;
    }

    // func_80099124(vis)
    void set_rate(uint8_t* rdram, recomp_context* ctx) {
        game_rdram.store(rdram, std::memory_order_relaxed);
        int32_t vis = (int32_t)ctx->r4;
        requested.store(vis, std::memory_order_relaxed);
        if (enabled.load(std::memory_order_relaxed) && vis > 1) {
            ctx->r4 = 1;
        }
        original_set_rate(rdram, ctx);
    }

    void apply(int32_t vis) {
        uint8_t* rdram = game_rdram.load(std::memory_order_relaxed);
        if (rdram == nullptr || vis <= 0) {
            return;
        }
        MEM_W(0, addr(VIS_PER_FRAME)) = vis;
        MEM_W(0, addr(FRAMES_PER_SECOND)) = 60 / vis;
    }
}

namespace adam::framerate {
    const std::vector<adam::game_text::Wrapped>& wrapped() {
        static const std::vector<adam::game_text::Wrapped> list = {
            { 0x80099124, set_rate, &original_set_rate },
        };
        return list;
    }

    // Every tick on the main thread (main.cpp update_gfx): follows the 60 FPS switch, also mid-scene.
    void update() {
        bool want = adam::get_60fps_enabled();
        if (want == enabled.load(std::memory_order_relaxed)) {
            return;
        }
        enabled.store(want, std::memory_order_relaxed);
        int32_t vis = requested.load(std::memory_order_relaxed);
        apply(want && vis > 1 ? 1 : vis);
        printf("[framerate] 60 fps %s\n", want ? "on" : "off");
    }
}
