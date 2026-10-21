# Makes runtime\src\game_text_en.inc (the English for the game's own on-screen text) from runtime\text\game_text_en.tsv.
#
# The game draws its captions, mission briefings and cutscene lines from strings of 16-bit character codes in its
# overlays, through one text renderer (runtime\src\game_text.cpp has how). game_text.cpp swaps a string it knows for its
# English line and draws that with its own font: the .inc made here holds the English lines, keyed by a hash of the
# game's string (FNV-1a 64 over its big-endian codes, without the terminating 0; nothing from the ROM itself), and the
# font, drawn here from a TrueType font as 8-bit coverage (the game's outline and shadow are added when a line is drawn).
#
# game_text_en.tsv: hash, ROM offset of the string (for finding it again), the Japanese (for translators; not used), the
# English. "\n" is a line break; leading spaces and an empty first line ("\n...") keep the game's layout. The game's box
# for a string is as wide as its Japanese was (so it stays where the game puts it); an English line wider than that is
# squeezed to fit, so keep lines about as wide as the Japanese (--check lists the ones squeezed hard).
#
# Usage: python scripts\game_text.py [--font <ttf>] [--check] [--preview <folder>]
#   --font     the font (default Liberation Serif Bold; SIL Open Font License, see README "Credits")
#   --check    list lines squeezed by more than 15%
#   --preview  write a PNG per line (English box over the Japanese box's size) to look at
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TSV = os.path.join(ROOT, "runtime", "text", "game_text_en.tsv")
INC = os.path.join(ROOT, "runtime", "src", "game_text_en.inc")
DEFAULT_FONTS = [
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
    r"C:\Windows\Fonts\LiberationSerif-Bold.ttf",
]

CELL = 24          # the game's glyphs are 24x24
SIZE = 19          # font size in pixels
BASELINE = 17      # baseline row in the cell
SS = 4             # supersampling
CHARS = [chr(c) for c in range(32, 127)] + ["\u00b2", "\u2026", "\u2014"]  # ASCII, superscript 2, ellipsis, em dash
SPACING = 1        # pixels between letters (the outline needs room)


def load_font(path):
    from PIL import ImageFont
    return ImageFont.truetype(path, SIZE * SS)


def glyph(font, ch):
    """(coverage rows (CELL lists of 0..255), width, advance in 1/4 pixels)."""
    from PIL import Image, ImageDraw
    import numpy as np
    pad = 2
    adv = font.getlength(ch) / SS
    left, _, right, _ = font.getbbox(ch, anchor="ls")
    lead = min(0, int(left // SS) - 1)  # a glyph reaching left of its origin
    width = max(1, int(-(-max(right / SS, adv) // 1)) - lead + 1)
    im = Image.new("L", ((width + pad) * SS, CELL * SS), 0)
    ImageDraw.Draw(im).text((-lead * SS, BASELINE * SS), ch, font=font, fill=255, anchor="ls")
    a = np.asarray(im, np.float32).reshape(CELL, SS, width + pad, SS).mean(axis=(1, 3))
    a = a[:, :width]
    cols = np.nonzero(a.max(axis=0) > 0)[0]
    width = int(cols[-1]) + 1 if len(cols) else 0
    rows = [[int(round(v)) for v in r[:width]] for r in a]
    return rows, width, int(round((adv + SPACING) * 4)), lead


def read_tsv():
    lines = []
    with open(TSV, encoding="utf-8") as f:
        next(f)
        for n, line in enumerate(f, 2):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 4:
                sys.exit(f"{TSV}:{n}: expected 4 tab-separated columns")
            h, rom, ja, en = cols
            lines.append((int(h, 16), rom, ja, en))
    return lines


def c_string(s):
    out = []
    for ch in s.encode("utf-8"):
        c = chr(ch)
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif 32 <= ch < 127:
            out.append(c)
        else:
            out.append("\\x%02x\"\"" % ch)
    return '"' + "".join(out) + '"'


def main():
    ap = argparse.ArgumentParser(description="Make runtime/src/game_text_en.inc from runtime/text/game_text_en.tsv.")
    ap.add_argument("--font")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview")
    args = ap.parse_args()
    path = args.font or next((p for p in DEFAULT_FONTS if os.path.exists(p)), None)
    if path is None:
        sys.exit("no font: pass --font <LiberationSerif-Bold.ttf>")
    font = load_font(path)
    glyphs = {ch: glyph(font, ch) for ch in CHARS}
    lines = read_tsv()

    for _, rom, _, en in lines:
        for ch in en.replace("\\n", ""):
            if ch not in glyphs:
                sys.exit(f"line {rom}: no glyph for {ch!r}")

    def width(text):
        return sum(glyphs[ch][2] for ch in text) / 4

    if args.check or args.preview:
        squeezed = 0
        for h, rom, ja, en in lines:
            jw = (max(len(l) for l in ja.split("\\n")) * CELL + 15) // 16 * 16
            ew = max(width(l) for l in en.split("\\n"))
            if ew > jw * 1.15:
                squeezed += 1
                if args.check:
                    print(f"{rom}: {ew:.0f} px for a {jw} px box ({jw / ew:.0%}): {en}")
        if args.check:
            print(f"{squeezed} of {len(lines)} lines squeezed by more than 15%")
        if args.preview:
            preview(args.preview, lines, glyphs)

    with open(INC, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// Generated by scripts\\game_text.py from runtime\\text\\game_text_en.tsv: the English font (8-bit coverage,\n")
        f.write(f"// {os.path.basename(path)} at {SIZE} px, baseline row {BASELINE}) and lines. Don't edit; edit the .tsv and rerun.\n")
        f.write("#ifdef GAME_TEXT_GLYPHS\n")
        for ch in CHARS:
            rows, w, adv, lead = glyphs[ch]
            data = "".join("%02x" % v for r in rows for v in r)
            f.write("{ 0x%04X, %d, %d, %d, \"%s\" },\n" % (ord(ch), w, adv, lead, data))
        f.write("#endif\n#ifdef GAME_TEXT_LINES\n")
        for h, rom, _, en in lines:
            f.write("{ 0x%016Xull, %s }, // %s\n" % (h, c_string(en.replace("\\n", "\n")), rom))
        f.write("#endif\n")
    print(f"-> {os.path.relpath(INC, ROOT)}: {len(CHARS)} glyphs, {len(lines)} lines")


def preview(folder, lines, glyphs):
    """Draws each line like game_text.cpp does (same outline and shadow) on blue, scaled up, to look at."""
    import numpy as np
    from PIL import Image
    os.makedirs(folder, exist_ok=True)
    for h, rom, ja, en in lines:
        jl = ja.split("\\n")
        el = en.split("\\n")
        n = max(len(jl), len(el))
        box_w = (max(len(l) for l in jl) * CELL + 15) // 16 * 16
        box_h = n * CELL
        cov = np.zeros((box_h, box_w), np.float32)
        for i, text in enumerate(el):
            line = line_coverage(text, glyphs)
            if line.shape[1] > box_w:
                line = squeeze(line, box_w)
            x0 = 0
            cov[i * CELL:(i + 1) * CELL, x0:x0 + line.shape[1]] = line
        nib = style(cov)
        pal = np.array([[30, 60, 120], [24, 24, 24]] + [[int(24 + (v - 1) / 14 * 231)] * 3 for v in range(2, 16)], np.uint8)
        img = pal[nib]
        Image.fromarray(img).resize((box_w * 3, box_h * 3), Image.NEAREST).save(os.path.join(folder, f"{rom}.png"))


def line_coverage(text, glyphs):
    """One line at full width: each glyph's coverage at its pen position (rounded down), overlaps keep the larger."""
    import numpy as np
    w = int(sum(glyphs[ch][2] for ch in text) / 4) + 2
    out = np.zeros((CELL, w + CELL), np.float32)
    pen = 0  # 1/4 pixels
    for ch in text:
        rows, gw, adv, lead = glyphs[ch]
        x = pen // 4 + lead
        if gw:
            g = np.array(rows, np.float32).reshape(CELL, gw) / 255
            lo = max(0, x)
            out[:, lo:x + gw] = np.maximum(out[:, lo:x + gw], g[:, lo - x:])
        pen += adv
    return out[:, :w]


def squeeze(line, w):
    """Shrinks a line's width to w columns (each new column averages the old ones it covers)."""
    import numpy as np
    h, old = line.shape
    out = np.zeros((h, w), np.float32)
    k = old / w
    for c in range(w):
        a, b = c * k, (c + 1) * k
        i = int(a)
        acc = np.zeros(h, np.float32)
        while i < b and i < old:
            part = min(b, i + 1) - max(a, i)
            acc += line[:, i] * part
            i += 1
        out[:, c] = acc / k
    return out


def style(cov):
    """Coverage 0..1 -> the game's nibbles: 0 clear, 1 outline / shadow, 2..15 fill (mirrors game_text.cpp)."""
    import numpy as np
    solid = cov > 0.08
    out = np.zeros(cov.shape, bool)
    h, w = cov.shape
    for dy, dx in [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1), (2, 2)]:
        src = solid[max(0, -dy):h - max(0, dy), max(0, -dx):w - max(0, dx)]
        out[max(0, dy):h - max(0, -dy), max(0, dx):w - max(0, -dx)] |= src
    nib = np.where(out, 1, 0)
    fill = np.maximum(np.rint(1 + cov * 14), 2).astype(int)
    return np.where(solid, fill, nib)


if __name__ == "__main__":
    main()
