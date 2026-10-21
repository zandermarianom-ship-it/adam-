# Makes an HD texture pack (.rtz) from upscaled textures, for Settings -> Texture Packs (runtime\src\texture_packs.cpp).
#
# How a pack is made:
#   1. Settings -> General -> Save Textures on, play the parts you want covered, switch it off again. The game's textures
#      are now in data\texture_dump next to adam.exe, as <hash>.v<version>.png (RT64's names; the .tmem / .rice.* files
#      there are for RT64's own tools and aren't needed).
#   2. Upscale those PNGs with any tool (Upscayl, chaiNNer, ...) into another folder. The output names may get a suffix
#      (Upscayl adds "_upscayl_4x_<model>"): only the "<hash>.v<version>" part has to stay in the name. Subfolders are
#      fine, so the pictures can be sorted by hand.
#   3. python scripts\texture_pack.py <upscaled folder> --dump <...>\data\texture_dump --out "Evangelion HD.rtz"
#      --dump is optional: with it the script warns about pictures whose shape changed (RT64 stretches a replacement
#      over the original, so it has to keep the original's width:height) and counts the dumped textures still missing.
#   4. Copy the .rtz into data\mods (or drop it on adam's window / install it from Settings -> Texture Packs) and restart.
#
# The .rtz is a zip: rt64.json (RT64's replacement database: the configuration and, per texture, its RT64 hash and the
# file inside the zip) plus the pictures under textures\. Pictures stay PNG; RT64's texture_packer tool can turn a pack
# into one with DDS mipmaps, which loads faster, but isn't needed. Several packs can be on at once: the one higher in
# the Texture Packs list wins.
#
# Usage: python scripts\texture_pack.py <upscaled folder> [--dump <dump folder>] [--out <pack.rtz>] [--preload]
#   --preload  load every replacement when the pack is switched on (more memory, no pop-in); default: stream them in
#              as they're first drawn (RT64's default).
import argparse, json, os, re, struct, sys, zipfile

NAME = re.compile(r"([0-9a-fA-F]{16})\.v(\d+)")
PICTURES = (".png", ".dds")


def png_size(path):
    """Width and height from a PNG's IHDR, or None."""
    with open(path, "rb") as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def scan(folder):
    """{hash: (version, path)} for every picture whose name holds a "<hash>.v<version>"."""
    found, skipped = {}, []
    for root, _, files in os.walk(folder):
        for name in sorted(files):
            if not name.lower().endswith(PICTURES):
                continue
            m = NAME.search(name)
            path = os.path.join(root, name)
            if m is None:
                skipped.append(path)
                continue
            key = m.group(1).lower()
            if key in found:
                print(f"note: {key} twice, using {found[key][1]} (not {path})")
                continue
            found[key] = (int(m.group(2)), path)
    return found, skipped


def main():
    ap = argparse.ArgumentParser(description="Make an HD texture pack (.rtz) for adam from upscaled RT64 texture dumps.")
    ap.add_argument("folder", help="the upscaled pictures (names keep <hash>.v<version>)")
    ap.add_argument("--dump", help="the original dump (data\\texture_dump), to check shapes and coverage")
    ap.add_argument("--out", default="texture_pack.rtz", help="the pack to write (default texture_pack.rtz)")
    ap.add_argument("--preload", action="store_true", help="load all replacements up front instead of streaming them")
    args = ap.parse_args()

    found, skipped = scan(args.folder)
    for path in skipped:
        print(f"skipped (no <hash>.v<version> in its name): {path}")
    if not found:
        sys.exit("no pictures with <hash>.v<version> names found")
    versions = {v for v, _ in found.values()}
    if len(versions) != 1:
        sys.exit(f"pictures from different RT64 hash versions ({sorted(versions)}): dump them again with one adam build")
    version = versions.pop()

    if args.dump:
        originals, _ = scan(args.dump)
        bad = 0
        for key, (_, path) in found.items():
            if key not in originals:
                print(f"note: {key} isn't in the dump folder")
                continue
            a, b = png_size(originals[key][1]), png_size(path)
            if a and b and abs(a[0] / a[1] - b[0] / b[1]) > 0.02:
                print(f"warning: {os.path.basename(path)} is {b[0]}x{b[1]}, the original {a[0]}x{a[1]} (different shape)")
                bad += 1
        missing = len(set(originals) - set(found))
        print(f"{len(found)} of {len(originals)} dumped textures replaced ({missing} left as they are), {bad} shape warnings")

    database = {
        "configuration": {
            "configurationVersion": 3,
            "autoPath": "rt64",
            "defaultOperation": "preload" if args.preload else "stream",
            "defaultShift": "half",
            "hashVersion": version,
        },
        "textures": [],
        "operationFilters": [],
        "shiftFilters": [],
        "extraFiles": [],
    }
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_STORED) as z:  # PNG / DDS are compressed already
        for key in sorted(found):
            path = found[key][1]
            inside = f"textures/{key}{os.path.splitext(path)[1].lower()}"
            z.write(path, inside)
            database["textures"].append({"path": inside, "hashes": {"rt64": key, "rice": ""}})
        z.writestr("rt64.json", json.dumps(database, indent=1))
    print(f"-> {args.out}: {len(found)} textures (RT64 hash version {version})")


if __name__ == "__main__":
    main()
