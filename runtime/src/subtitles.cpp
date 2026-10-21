// English subtitles for the game's voice clips.
//
// The game plays every sound effect and voice clip through func_8009B830(number, volume, pan, restart, priority) ->
// func_800ADCFC -> MusStartEffect2 (0x800AE540). Numbers below the count at 0x80149F10 play from effect bank
// *0x800FF3B8 ("A", loaded from ROM 0x11D7500, samples in the sample bank at ROM 0xEC08E0), the rest (minus that count)
// from bank *0x800FF3BC ("B", ROM 0x1687B20, samples at ROM 0x11D89E0). Effect n of a bank plays sample n of its sample
// bank at its base pitch (each effect is "81 <sample> ... 30 <length> 80"), so a clip's length is its sample's.
// adam_on_sound is called at the start of func_8009B830 (static build: scripts\extra_hooks.txt; live build: a syscall
// patch in live.cpp), on the game thread.
//
// The lines are in subtitles_en.inc ({bank, effect number in its bank, clip length in ms, English text}), made by
// scripts\subtitles.py from runtime\subtitles\voice_lines.tsv: the clips were decoded from the ROM, transcribed
// (Whisper) and translated by hand. Only the text is in the exe, nothing from the ROM. A file data\subtitles_en.txt
// next to adam.exe, if there is one, replaces the built-in lines (same columns, tab-separated: bank, number, ms, text;
// lines starting with # are skipped), so a translation can be corrected without a new build. It's read once, at
// startup.
//
// Drawing: like the bezels (bezels.cpp), one UI context that takes no input, shown once when the game starts, holding a
// box at the bottom of the window. update() runs every tick on the main thread (main.cpp update_gfx) and shows the
// current line until the clip has played (game frames from main.cpp's adam_frame_count, ~60 per second, so pauses,
// menus and fast-forward keep the line in step with the voice). A new line replaces the one on screen.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "SDL.h"
#include "librecomp/game.hpp"
#include "recompui/recompui.h"
#include "ultramodern/ultramodern.hpp"
#include "elements/ui_container.h"
#include "elements/ui_label.h"

#include "recomp.h"

uint32_t adam_frame_count(); // main.cpp

namespace adam {
    bool get_subtitles_enabled(); // config.cpp
}

namespace {
    struct Line {
        char bank;           // 'A' or 'B'
        uint32_t number;     // effect number within its bank
        uint32_t ms;         // clip length
        std::string text;
    };

    const std::vector<Line> builtin_lines = {
#include "subtitles_en.inc"
    };

    // The table in use: builtin_lines or the override file. Swapped once on the main thread at startup
    // (load_lines), then only read; the game thread may still be using the built-in table then, which stays valid.
    std::atomic<const std::vector<Line>*> lines = &builtin_lines;
    std::vector<Line> file_lines;

    void load_lines() {
        std::filesystem::path path = recomp::get_config_path() / "subtitles_en.txt";
        std::ifstream file{ path };
        if (!file.good()) {
            return;
        }
        std::string row;
        while (std::getline(file, row)) {
            if (!row.empty() && row.back() == '\r') {
                row.pop_back();
            }
            if (row.empty() || row[0] == '#') {
                continue;
            }
            std::stringstream fields{ row };
            std::string bank, number, ms, text;
            if (!std::getline(fields, bank, '\t') || !std::getline(fields, number, '\t') || !std::getline(fields, ms, '\t') ||
                !std::getline(fields, text) || bank.size() != 1 || (bank[0] != 'A' && bank[0] != 'B')) {
                fprintf(stderr, "[subtitles] skipped a line of %s: %s\n", path.string().c_str(), row.c_str());
                continue;
            }
            file_lines.push_back(Line{ bank[0], uint32_t(std::strtoul(number.c_str(), nullptr, 10)),
                uint32_t(std::strtoul(ms.c_str(), nullptr, 10)), text });
        }
        printf("[subtitles] %zu lines from %s\n", file_lines.size(), path.string().c_str());
        lines.store(&file_lines);
    }

    const Line* find_line(char bank, uint32_t number) {
        for (const Line& line : *lines.load()) {
            if (line.bank == bank && line.number == number) {
                return &line;
            }
        }
        return nullptr;
    }

    constexpr uint32_t FRAMES_PER_SECOND = 60;   // adam_frame_count: controller reads, one per VI
    constexpr uint32_t EXTRA_FRAMES = 30;        // the line stays half a second after the voice ends
    constexpr uint32_t MIN_FRAMES = 90;          // and at least 1.5 seconds in all

    std::atomic<bool> enabled = true;            // copied from the config on the main thread (update)
    std::mutex line_mutex;
    const Line* current = nullptr;               // under line_mutex
    uint32_t show_until = 0;                     // frame number, under line_mutex

    bool overlay_created = false;
    recompui::ContextId overlay = recompui::ContextId::null();
    recompui::Container* box = nullptr;
    recompui::Label* label = nullptr;
    const Line* shown = nullptr;                 // main thread only
    bool lines_loaded = false;                   // main thread only

    void create_overlay() {
        overlay = recompui::create_context();
        overlay.set_captures_input(false);
        overlay.set_captures_mouse(false);
        overlay.open();
        recompui::Element* root = overlay.get_root_element();
        root->set_position(recompui::Position::Absolute);
        root->set_top(0);
        root->set_left(0);
        root->set_width(100.0f, recompui::Unit::Percent);
        root->set_height(100.0f, recompui::Unit::Percent);

        // A full-width row near the bottom that centres the caption.
        box = overlay.create_element<recompui::Container>(root, recompui::FlexDirection::Row, recompui::JustifyContent::Center);
        box->set_position(recompui::Position::Absolute);
        box->set_left(8.0f, recompui::Unit::Percent);
        box->set_width(84.0f, recompui::Unit::Percent);
        box->set_bottom(6.0f, recompui::Unit::Percent);
        box->set_align_items(recompui::AlignItems::Center);
        box->set_display(recompui::Display::None);

        // Sizes in dp: RecompFrontend sets 1 dp = window height / 1080, so the caption scales with the window.
        label = overlay.create_element<recompui::Label>(box, "", recompui::LabelStyle::Normal);
        label->set_font_size(44.0f);
        label->set_line_height(56.0f);
        label->set_font_weight(600);
        label->set_color(recompui::Color{ 255, 255, 255, 255 });
        label->set_background_color(recompui::Color{ 0, 0, 0, 170 });
        label->set_padding_left(22.0f);
        label->set_padding_right(22.0f);
        label->set_padding_top(6.0f);
        label->set_padding_bottom(8.0f);
        label->set_border_radius(10.0f);
        label->set_text_align(recompui::TextAlign::Center);
        label->set_white_space(recompui::WhiteSpace::Normal);
        overlay.close();
        recompui::show_context(overlay, "");
    }
}

extern "C" void adam_on_sound(uint8_t* rdram, recomp_context* ctx) {
    uint32_t number = (uint32_t)ctx->r4;
    uint32_t bank_a_count = (uint32_t)MEM_W(0, (gpr)(int32_t)0x80149F10);
    char bank = number < bank_a_count ? 'A' : 'B';
    uint32_t in_bank = number < bank_a_count ? number : number - bank_a_count;
    if (!enabled.load()) {
        return;
    }
    const Line* line = find_line(bank, in_bank);
    if (line == nullptr) {
        return;
    }
    uint32_t frames = line->ms * FRAMES_PER_SECOND / 1000 + EXTRA_FRAMES;
    if (frames < MIN_FRAMES) {
        frames = MIN_FRAMES;
    }
    std::lock_guard lock{ line_mutex };
    current = line;
    show_until = adam_frame_count() + frames;
}

namespace adam::subtitles {
    // Every tick on the main thread (main.cpp update_gfx).
    void update() {
        if (!lines_loaded) {
            lines_loaded = true;
            load_lines();
        }
        if (!ultramodern::is_game_started()) {
            return;
        }
        enabled.store(adam::get_subtitles_enabled());
        if (!overlay_created) {
            overlay_created = true;
            create_overlay();
        }
        const Line* want = nullptr;
        {
            std::lock_guard lock{ line_mutex };
            if (current != nullptr && enabled.load() && int32_t(show_until - adam_frame_count()) > 0) {
                want = current;
            } else {
                current = nullptr;
            }
        }
        if (want == shown) {
            return;
        }
        shown = want;
        overlay.open();
        if (want != nullptr) {
            label->set_text(want->text);
            box->set_display(recompui::Display::Flex);
        } else {
            box->set_display(recompui::Display::None);
        }
        overlay.close();
    }
}
