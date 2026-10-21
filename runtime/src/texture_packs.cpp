// HD texture packs and texture dumping (for making them).
//
// Packs: RT64 replaces the game's textures with the ones in a texture pack: a .rtz file (a zip holding rt64.json, the
// list of replaced textures by their RT64 hash, and the pictures as .png or .dds). Like other N64: Recompiled games, a pack
// is a mod: it goes into data\mods (or is dropped on the window / installed from Settings -> Texture Packs) and can be
// switched on and off there. register_content_types() tells the mod system that a .rtz is a texture pack (no mod.json
// needed) and that a mod with an rt64.json is one; enabling / disabling it goes to RecompFrontend's renderer, which
// loads the enabled packs into RT64 (rt64_render_context.cpp check_texture_pack_actions). A folder can be a pack too, but
// the mod system then wants a mod.json in it (scripts\texture_pack.py makes the .rtz instead).
//
// Dumping: Settings -> General -> Save Textures makes RT64 write every texture the game draws to data\texture_dump as
// <hash>.v<version>.png (plus .tmem / .rice files RT64's own tools use), once per texture, while it's on. Those PNGs are
// what an upscaler works on; scripts\texture_pack.py turns the upscaled ones into a .rtz. The switch goes to RT64 through
// recompui::renderer::set_texture_dump_directory (runtime\patches\recompfrontend-texture-dump.patch), the same thing RT64's
// developer inspector's "Start dumping textures" button does.
#include <filesystem>
#include <vector>

#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "recompui/renderer.h"

namespace adam {
    bool get_texture_dump_enabled(); // config.cpp
}

namespace {
    void on_pack_enabled(recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
        recompui::renderer::enable_texture_pack(context, mod);
    }

    void on_pack_disabled(recomp::mods::ModContext&, const recomp::mods::ModHandle& mod) {
        recompui::renderer::disable_texture_pack(mod);
    }

    void on_packs_reordered(recomp::mods::ModContext&) {
        recompui::renderer::trigger_texture_pack_update();
    }

    int dump_state = -1; // main thread only: -1 not sent yet, else the last value sent
}

namespace adam::texture_packs {
    // Before recomp::start (which scans data\mods).
    void register_content_types() {
        recomp::mods::ModContentType texture_pack{
            .content_filename = "rt64.json",
            .allow_runtime_toggle = true,
            .on_enabled = on_pack_enabled,
            .on_disabled = on_pack_disabled,
            .on_reordered = on_packs_reordered,
        };
        recomp::mods::ModContentTypeId id = recomp::mods::register_mod_content_type(texture_pack);
        recomp::mods::register_mod_container_type("rtz", std::vector{ id }, false);
    }

    // Every tick on the main thread (main.cpp update_gfx): follows the Save Textures switch.
    void update() {
        int want = adam::get_texture_dump_enabled() ? 1 : 0;
        if (want == dump_state) {
            return;
        }
        // Nothing to stop before it was ever started.
        if (dump_state == -1 && want == 0) {
            dump_state = 0;
            return;
        }
        dump_state = want;
        recompui::renderer::set_texture_dump_directory(want ? recomp::get_config_path() / "texture_dump" : std::filesystem::path{});
        printf("[textures] dumping %s\n", want ? "on (data\\texture_dump)" : "off");
    }
}
