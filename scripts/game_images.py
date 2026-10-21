# Makes runtime\src\game_images_en.inc (English for the game's pictures with Japanese text in them: mission title
# cards, labels, menu prompts...) from runtime\text\game_images_en.tsv and your ROM.
#
# The game's named pictures (an 8-letter name in a 16-byte header, then 4- or 8-bit colour indices; README
# "Game text") all come in through one loader, func_8009930C; runtime\src\game_images.cpp patches a picture it has a
# patch for right after it's loaded. A patch is a rectangle of colour indices (run-length coded): the area of the
# picture holding the Japanese, drawn again with the English. The English is drawn here with a TrueType font, coloured
# from the picture's own palette: its background index, the fill colour of the original letters row by row (so
# gradients carry over), their outline colour, and in-between colours for smooth edges, each pixel the nearest colour
# the picture already uses. Nothing of the original picture is copied into a patch (the area is drawn from scratch),
# but the indices are picked by looking at it, so this needs the ROM: python scripts\game_images.py --rom <rom.z64>.
#
# game_images_en.tsv: name, English, options; a picture can have several rows (each redraws its own rect, in order). "\n" separates lines; a line can start with [0.6] to make it that size
# relative to the others. Options (space-separated, all optional):
#   rect=x0,y0,x1,y1  the area to redraw (default: the whole picture); the rest is kept (frames, English parts)
#   box=x0,y0,x1,y1   where the English goes (default: around the original letters in the area)
#   align=l|c|r       line alignment (default c)   valign=t|m|b (default m)
#   font=serif|sans   (default serif: Liberation Serif Bold; sans: Liberation Sans Bold)
#   outline=N         outline width in pixels (default: 1 if the original letters have an outline, else 0)
#   fill=I            the letters' colour index (default: from the original, row by row)
#   bg=I              the background index (default: the most common index on the area's edge)
#   squeeze=F         how narrow letters may get before the text is made smaller (default 0.5)
#   size=N            capital height in pixels (default: as large as fits); lines that don't fit are squeezed
#
# Usage: python scripts\game_images.py --rom <rom.z64> [--preview <folder>] [--only NAME...]
import argparse, os, struct, sys
from collections import Counter

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import yay0  # noqa: E402

TSV = os.path.join(ROOT, "runtime", "text", "game_images_en.tsv")
INC = os.path.join(ROOT, "runtime", "src", "game_images_en.inc")
IMAGE_ROM, IMAGE_TABLE = 0x1D9000, 0x800D6AD0
PALETTE_ROM, PALETTE_TABLE = 0x60CB80, 0x800DD820
PAIRS = 0x800E2080
FONTS = {
    "serif": ["/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf", r"C:\Windows\Fonts\LiberationSerif-Bold.ttf"],
    "sans": ["/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", r"C:\Windows\Fonts\LiberationSans-Bold.ttf"],
}
SS = 4  # supersampling
MIN_SQUEEZE = 0.5  # narrowest a line may be squeezed before it's made smaller


def boot_rom(vram):
    return vram - 0x80096400 + 0x1000


def load_asset(rom, base, table, index):
    offset, packed, size = struct.unpack_from(">III", rom, boot_rom(table) + index * 12)
    at = base + offset
    return yay0.decompress(rom, at) if packed else rom[at:at + size]


def named_images(rom):
    """name -> (indices array, palette RGBA (N x 4), bits)."""
    out = {}
    index = 0
    while True:
        offset, packed, size = struct.unpack_from(">III", rom, boot_rom(IMAGE_TABLE) + index * 12)
        if size == 0 or size > 0x100000 or IMAGE_ROM + offset >= len(rom):
            break
        image_index, palette_index = struct.unpack_from(">HH", rom, boot_rom(PAIRS) + index * 4)
        try:
            data = load_asset(rom, IMAGE_ROM, IMAGE_TABLE, index)
        except Exception:
            index += 1
            continue
        index += 1
        if len(data) < 16:
            continue
        w, h, nc = struct.unpack_from(">HHH", data, 0)
        name = data[8:16]
        if not all(32 <= c < 127 for c in name) or w == 0 or h == 0:
            continue
        name = name.decode().rstrip("\0 ")
        if nc == 256 and len(data) >= 16 + w * h:
            pix = np.frombuffer(data[16:16 + w * h], np.uint8).reshape(h, w).copy()
            bits = 8
        elif len(data) >= 16 + w * h // 2:
            b = np.frombuffer(data[16:16 + w * h // 2], np.uint8)
            pix = np.empty(w * h, np.uint8)
            pix[0::2] = b >> 4
            pix[1::2] = b & 15
            pix = pix.reshape(h, w)
            bits = 4
        else:
            continue
        if image_index != index - 1:
            continue
        pal = load_asset(rom, PALETTE_ROM, PALETTE_TABLE, palette_index)
        c = np.frombuffer(pal[:len(pal) - len(pal) % 2], ">u2").astype(np.uint32)
        rgba = np.stack([((c >> 11) & 31) * 255 // 31, ((c >> 6) & 31) * 255 // 31, ((c >> 1) & 31) * 255 // 31,
                         (c & 1) * 255], -1).astype(np.float32)
        if len(rgba) <= pix.max():
            continue
        out.setdefault(name, (pix, rgba, bits))
    return out


def font_path(kind):
    for p in FONTS[kind]:
        if os.path.exists(p):
            return p
    sys.exit(f"no {kind} font (Liberation {kind.title()} Bold)")


def parse_options(text):
    opts = {}
    for part in text.split():
        k, _, v = part.partition("=")
        if k == "box" and v == "rect":
            opts[k] = v
        elif k in ("rect", "box"):
            opts[k] = tuple(int(x) for x in v.split(","))
        elif k in ("outline", "fill", "bg"):
            opts[k] = int(v)
        else:
            opts[k] = v
    return opts


def render_lines(lines, fontfile, box_w, box_h, align, valign, min_squeeze=0.5, fixed=None):
    """Coverage (box_h x box_w, 0..1) of the lines fitted into the box: as large as fits, squeezed sideways (down to 55%)
    before shrinking."""
    specs = []
    for line in lines:
        rel = 1.0
        if line.startswith("[") and "]" in line:
            rel = float(line[1:line.index("]")])
            line = line[line.index("]") + 1:]
        specs.append((line, rel))
    total_rel = sum(r for _, r in specs)
    gap = 0.15
    best = None
    # Pick the largest unit size (cap height drives the look) where every line fits with squeeze >= 0.55.
    last_desc = 0.24 * specs[-1][1] if any(c in "gjpqy,;" for c in specs[-1][0]) else 0
    sizes = [fixed] if fixed else np.arange(box_h / (total_rel + gap * (len(specs) - 1) + last_desc), 2, -0.5)
    for unit in sizes:
        ok, rendered = True, []
        for text, rel in specs:
            size = unit * rel
            font = ImageFont.truetype(fontfile, max(4, int(size * SS * 1.38)))  # cap height ~0.72 em for bold faces
            l, t, r, b = font.getbbox(text or " ", anchor="ls")
            asc, desc = font.getmetrics()
            w = (r - l) / SS
            squeeze = min(1.0, box_w / w) if w > 0 else 1.0
            if squeeze < min_squeeze and not fixed:
                ok = False
                break
            rendered.append((text, font, l, w, squeeze, size))
        if ok:
            best = rendered
            break
    if best is None:
        best = rendered
    line_h = [s for *_, s in best]
    desc = 0.24 * line_h[-1] if any(c in "gjpqy,;" for c in best[-1][0]) else 0
    total = sum(line_h) + gap * unit * (len(line_h) - 1) + desc
    y = {"t": 0, "m": (box_h - total) / 2, "b": box_h - total}[valign]
    cov = np.zeros((box_h, box_w), np.float32)
    for (text, font, l, w, squeeze, size), lh in zip(best, line_h):
        # draw at full width, then squeeze
        W = int((w + 4) * SS)
        im = Image.new("L", (W, int(box_h * SS) + SS * 8), 0)
        base_y = (y + lh) * SS  # baseline: the capitals fill the line's height
        ImageDraw.Draw(im).text((2 * SS - l, base_y), text, font=font, fill=255, anchor="ls")
        a = np.asarray(im, np.float32)[:box_h * SS]
        if a.shape[0] < box_h * SS:
            a = np.vstack([a, np.zeros((box_h * SS - a.shape[0], a.shape[1]), np.float32)])
        new_w = max(1, int(round((w + 4) * squeeze * SS)))
        a = np.asarray(Image.fromarray(a).resize((new_w, box_h * SS), Image.BILINEAR), np.float32)
        lw = new_w / SS
        x = {"l": 0, "c": (box_w - lw) / 2, "r": box_w - lw}[align]
        xs = int(round(x * SS))
        canvas = np.zeros((box_h * SS, box_w * SS + 8 * SS), np.float32)
        x0 = max(0, xs)
        canvas[:, x0:x0 + new_w - (x0 - xs)] = a[:, x0 - xs:][:, :canvas.shape[1] - x0]
        canvas = canvas[:, :box_w * SS]
        c = canvas.reshape(box_h, SS, box_w, SS).mean(axis=(1, 3)) / 255
        cov = np.maximum(cov, c)
        y += lh + gap * unit
    return cov


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def make_patch(name, pix, pal, english, opts):
    h, w = pix.shape
    x0, y0, x1, y1 = opts.get("rect", (0, 0, w, h))
    area = pix[y0:y1, x0:x1]
    edge = np.concatenate([area[0], area[-1], area[:, 0], area[:, -1]])
    bg = opts.get("bg", Counter(edge.tolist()).most_common(1)[0][0])
    if pal[bg][3]:
        # on a solid background, letters are what differs from it clearly (backgrounds can be dithered)
        lum = np.array([luminance(c) for c in pal])
        text = np.abs(lum[area] - lum[bg]) > 40
    else:
        text = area != bg
    # original letters: interior (not touching the background) and their edge
    pad = np.pad(text, 1)
    touching = text & ~(pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:])
    interior = text & ~touching
    ys, xs = np.nonzero(text)
    if len(ys) == 0:
        box = (0, 0, x1 - x0, y1 - y0)
    else:
        box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    if "box" in opts and opts["box"] == "rect":
        box = (0, 0, x1 - x0, y1 - y0)
    elif "box" in opts:
        bx0, by0, bx1, by1 = opts["box"]
        box = (bx0 - x0, by0 - y0, bx1 - x0, by1 - y0)
    # colours
    used = sorted(set(area.flatten().tolist()) | {bg})
    if "fill" in opts:
        rowfill = [opts["fill"]] * area.shape[0]
    else:
        src = interior if interior.any() else text
        counts = [int(src[y].sum()) for y in range(area.shape[0])]
        enough = max(3, 0.3 * float(np.median([c for c in counts if c] or [0])))
        rows = {}
        for y in range(area.shape[0]):
            if counts[y] >= enough:
                rows[y] = Counter(area[y][src[y]].tolist()).most_common(1)[0][0]
        if not rows:
            rows = {0: max(used, key=lambda i: abs(luminance(pal[i]) - luminance(pal[bg])))}
        if pal[bg][3]:
            # letters on a solid background: one colour, the strongest common one (row modes catch edge shades)
            share = Counter(area[src].tolist())
            total_px = sum(share.values())
            strong = [i for i, n in share.items() if n >= 0.1 * total_px]
            if not strong:
                strong = list(share)
            best_i = max(strong or list(share), key=lambda i: abs(luminance(pal[i]) - luminance(pal[bg])))
            rows = {0: best_i}
        keys = sorted(rows)
        rowfill = [rows[min(keys, key=lambda k: abs(k - y))] for y in range(area.shape[0])]
    edge_vals = Counter(area[touching].tolist()).most_common(3)
    outline_index = None
    if edge_vals:
        cand = edge_vals[0][0]
        fl = luminance(pal[Counter(rowfill).most_common(1)[0][0]])
        bl = luminance(pal[bg]) if pal[bg][3] else None
        cl = luminance(pal[cand])
        # an outline: darker than the fill (or lighter) by a lot, and not just an in-between edge shade
        if bl is None:
            if abs(cl - fl) > 60:
                outline_index = cand
        elif abs(cl - fl) > 60 and abs(cl - bl) > 40 and not (min(fl, bl) < cl < max(fl, bl)):
            outline_index = cand
    width = opts.get("outline", 1 if outline_index is not None else 0)
    if width and outline_index is None:
        outline_index = min(used, key=lambda i: luminance(pal[i]) if pal[i][3] else 999)
    # draw
    bx0, by0, bx1, by1 = box
    lines = english.split("\\n")
    fontfile = font_path(opts.get("font", "serif"))
    inner_w = max(4, bx1 - bx0 - 2 * width)
    inner_h = max(4, by1 - by0 - 2 * width)
    cov_box = render_lines(lines, fontfile, inner_w, inner_h, opts.get("align", "c"), opts.get("valign", "m"),
                           float(opts.get("squeeze", MIN_SQUEEZE)), float(opts["size"]) if "size" in opts else None)
    fill_cov = np.zeros(area.shape, np.float32)
    oy, ox = by0 + width, bx0 + width
    hh = min(inner_h, area.shape[0] - oy)
    ww = min(inner_w, area.shape[1] - ox)
    fill_cov[oy:oy + hh, ox:ox + ww] = cov_box[:hh, :ww]
    out_cov = np.zeros_like(fill_cov)
    if width:
        solid = fill_cov
        for dy in range(-width, width + 1):
            for dx in range(-width, width + 1):
                if dx * dx + dy * dy > width * width + width:
                    continue
                sh = np.roll(np.roll(solid, dy, 0), dx, 1)
                out_cov = np.maximum(out_cov, sh)
    # colour each pixel and map it to the nearest colour already in the picture
    cand = np.array(used)
    cpal = pal[cand]
    result = np.empty(area.shape, np.uint8)
    bgc = pal[bg]
    oc = pal[outline_index] if width else None
    for y in range(area.shape[0]):
        fc = pal[rowfill[y]]
        for x in range(area.shape[1]):
            f, o = fill_cov[y, x], out_cov[y, x]
            if width:
                base = bgc if bgc[3] else oc
                c = base + (oc - base) * min(1, o) if bgc[3] else (oc if o > 0.45 else bgc)
                if bgc[3] == 0 and o <= 0.45 and f <= 0.45:
                    result[y, x] = bg
                    continue
                c = c + (fc - c) * f
            else:
                if bgc[3] == 0:
                    if f <= 0.45:
                        result[y, x] = bg
                        continue
                    c = fc
                else:
                    c = bgc + (fc - bgc) * f
            opaque = cpal[:, 3] > 0
            d = ((cpal[:, :3] - c[:3]) ** 2).sum(axis=1) + np.where(opaque, 0, 1e9)
            result[y, x] = cand[int(np.argmin(d))]
    return (x0, y0, x1, y1), result


def rle(values):
    out = []
    i = 0
    flat = values.flatten().tolist()
    while i < len(flat):
        v = flat[i]
        n = 1
        while i + n < len(flat) and flat[i + n] == v and n < 256:
            n += 1
        out.append("%02x%02x" % (n - 1, v))
        i += n
    return "".join(out)


def read_tsv():
    rows = []
    with open(TSV, encoding="utf-8") as f:
        next(f)
        for n, line in enumerate(f, 2):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            while len(cols) < 3:
                cols.append("")
            rows.append((cols[0], cols[1], parse_options(cols[2])))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=os.path.join(ROOT, "rom", "game.z64"))
    ap.add_argument("--preview")
    ap.add_argument("--only", nargs="*")
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    images = named_images(rom)
    rows = read_tsv()
    entries = []
    previews = {}
    for name, english, opts in rows:
        if name not in images:
            sys.exit(f"{name}: no such picture")
        if args.only and name not in args.only:
            continue
        pix, pal, bits = images[name]
        current = previews.get(name, pix)  # a later row for the same picture draws over the earlier ones
        rect, patch = make_patch(name, current, pal, english, opts)
        entries.append((name, pix.shape, bits, rect, patch))
        new = current.copy()
        new[rect[1]:rect[3], rect[0]:rect[2]] = patch
        previews[name] = new
    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for name, new in previews.items():
            pix, pal, _ = images[name]
            def show(p):
                rgba = pal[p].astype(np.uint8)
                im = Image.fromarray(rgba, "RGBA")
                bgim = Image.new("RGBA", im.size, (255, 0, 255, 255))
                bgim.alpha_composite(im)
                return bgim.convert("RGB")
            a, b = show(pix), show(new)
            sc = 2
            o = Image.new("RGB", (a.width * sc, a.height * sc * 2 + 4), "white")
            o.paste(a.resize((a.width * sc, a.height * sc), Image.NEAREST), (0, 0))
            o.paste(b.resize((b.width * sc, b.height * sc), Image.NEAREST), (0, a.height * sc + 4))
            o.save(os.path.join(args.preview, name + ".png"))
    if args.only:
        print(f"previewed {len(entries)}")
        return
    with open(INC, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// Generated by scripts\\game_images.py from runtime\\text\\game_images_en.tsv: per picture, its name, size, bits\n")
        f.write("// per pixel, the redrawn rectangle and its colour indices (run-length: count-1, index). Don't edit.\n")
        for name, (h, w), bits, (x0, y0, x1, y1), patch in entries:
            data = rle(patch)
            chunks = '"\n    "'.join(data[i:i + 4000] for i in range(0, len(data), 4000))
            f.write('{ "%s", %d, %d, %d, %d, %d, %d, %d,\n    "%s" },\n' % (name, w, h, bits, x0, y0, x1, y1, chunks))
    print(f"-> {os.path.relpath(INC, ROOT)}: {len(entries)} pictures")


if __name__ == "__main__":
    main()
