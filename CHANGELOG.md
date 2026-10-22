# Changelog

What changed in each download. Newest first. Version numbers start with 0.1.0; earlier downloads were named by
date (`adam-<date>-<commit>.zip`).

## Unreleased

### New

- **The game's own text in English**: the mission-intro captions, the briefings (target, EVA, operation) and the
  cutscene lines are now drawn in English, in the game's own style, where the game drew Japanese. On/off in
  Settings -> General -> English Game Text (on by default). 214 lines, in `runtime\text\game_text_en.tsv`.
- **The pictures with Japanese text in English too**: the mission title cards, the controls screen before each
  mission, the file and reset prompts, briefing and results labels, the MAGI vote panels, mission-failed screens,
  and the Angel and weapon names (116 pictures), redrawn in each picture's own colours. Same switch. The staff
  credits are still Japanese.
- **60 FPS** was tried (the game running every VI) and taken out again: much of the game counts frames rather than
  time (attacks, walking), so it ran 2-3x too fast.
- **Smooth 60 FPS (experimental)**, Settings -> General, off by default: the picture at the screen's refresh rate
  (RT64 draws in-between pictures) while the game keeps its own speed. RT64 now sees each game frame whole (the game
  draws one in several graphics tasks; before, its in-between pictures lost the 3D).
  Fixed a crash with it on (in RT64, when the frame rate dropped and more in-between pictures were needed: seen in
  the second Angel's fight).
- **Fixed: the game closing after the first Angel is beaten** (as its core explodes), with "Failed to find function at
  0x800416F0" in the log. 16 functions in the overlays were listed as starting a few zero bytes too early (where
  splat had marked them as data), so a call to their real start found nothing. Their entries in `symbols.toml` now
  start at the code.
- **adam_log.txt**: everything adam prints is now saved next to adam.exe (overwritten each start), for reports.
- **Fixed: 3D flickering in fights and 3D cutscenes**: with Settings -> Graphics -> Framerate on Display or Manual,
  the renderer made up in-between frames, and those had no 3D at all (the Evas and the city showed in one frame of
  three). The game now always runs at its own framerate, whatever that setting says.
- **No black frame in widescreen**: with a wide aspect ratio, mission battles fill the whole screen instead of keeping
  the game's 16-pixel black frame (which ended up as black bars on all four sides).
- **Steady 3D** (Settings -> General, on by default): fixes 3D scenes that flicker (the Evas, Angels and 3D cutscenes
  flashing on and off rapidly). The screen could show a new frame before its 3D part had been drawn; now the last
  complete frame stays up until the new one is done. Only the picture waits, not the game: the first versions of this
  fix (three downloads) held the game itself, which cut the sound and froze a cutscene on one PC.
- **English subtitles** for the voice clips: while a character speaks, an English line shows at the bottom of the
  screen. On/off in Settings -> General (on by default). 50 of the game's 307 voice clips have a line so far (the
  spoken lines; shouts and grunts don't get one); some are marked as unsure in `runtime\subtitles\voice_lines.tsv`
  and need checking by ear. A `subtitles_en.txt` in the `data` folder replaces the built-in lines, so they can be
  corrected without a new build.
- **HD texture packs**: RT64 texture packs (`.rtz`) go into `data\mods` (or are dropped on the window) and are
  switched on and off in the new Settings -> Texture Packs tab. Settings -> General -> Save Textures saves every texture
  the game draws to `data\texture_dump`, and `scripts\texture_pack.py` turns upscaled copies of them into a pack.
- **1080p resolution**: Settings -> Graphics -> Resolution has a new 1080p choice that always renders at 1080p,
  whatever the window's size (Auto picks a whole multiple of 240 lines from the window instead). Downsampling works
  with it too.

## 0.1.1 (2026-10-13)

Same game as 0.1.0; only the version number changed.

### Changed

- The launcher shows the download's version number (0.1.1), so the two always match.

## 0.1.0 (2026-10-13)

Changes since the previous download (`adam-2026-10-12-9f7f668.zip`).

### New

- **Rewind**: hold Backspace or click the left stick during a mission to run the game backwards, up to 10 seconds.
  Let go and the game continues from there. While the game's own pause screen is open, rewind jumps straight back
  into the action. On/off in Settings -> General (on by default). Only in missions: intro, menus and cutscenes
  ignore it.
- **Bezels**: artwork from the game in the black bars beside the 4:3 picture. New Settings -> Bezels tab with a
  preview of every usable picture from your ROM (93), or Off. Adjust brightness, black & white, and position and
  size (press A / Enter on the control, then move with the d-pad, left stick or arrow keys; resize with the right
  stick or + / -). The pictures are read from your ROM; the download contains none of them.
- **Launcher background**: the launcher has a new "Background" entry with the same picker and adjustments as the
  bezels (or Off for the plain look).
- **Reset Game**: button in Settings -> General (asks first). Restarts the game; saves are kept.
- **Fast-forward and rewind can be remapped** in Settings -> Controls (rows after the D-pad).
- The download's name and `HOW_TO_PLAY.txt` now show a version number.

### Changed

- **Fast-forward is a fixed 2x** with an on/off option in Settings -> General. The 2x / 4x / 8x choice is gone:
  4x and 8x flickered a lot.
- **Widescreen only in gameplay**: if you choose a wide aspect ratio in Settings -> Graphics, it is now used only in
  mission battles. Intro, menus, cutscenes and briefings stay 4:3 (they are drawn for 4:3 and showed empty or
  stretched edges). Your saved setting doesn't change.
- Company names were removed from the included texts.

### Fixed

- Fast-forward: a half-drawn frame could be shown (HUD missing or a black screen flickering in missions).
- Fast-forward at 8x could freeze the game while held.
- Rumble no longer keeps running while a menu is open, the game is paused or you rewind.

### Known issues

- Higher render resolutions ("Auto" in a large window) show thin lines across some scaled backgrounds. The default
  (original 2x) doesn't.
- Widescreen in battles hasn't been checked for objects disappearing at the new screen edges.
