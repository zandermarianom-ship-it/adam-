# Writes the game's sound effect / voice clips as WAV files, to listen to them while checking the subtitles
# (runtime\subtitles\voice_lines.tsv). Reads your ROM (rom\game.z64); the WAVs are ROM data, don't share them.
#
# The game's two effect banks (subtitles.cpp) play from two libmus sample banks: "N64 PtrTablesV2" files followed by
# their "N64 WaveTables" data. Sample bank A (effects, ROM 0xEC08E0) and B (voices, ROM 0x11D89E0). A PtrTables file:
# name[16], flags, wbk name[12], then count, three offsets, the third = the list of each sample's record (offsets in the
# file). A record is an ALWaveTable (base offset in the wave data, length, type/flags, loop, book) followed by its
# ADPCM book (order, predictor count, order * predictors * 8 coefficients). Clips are VADPCM, played at 22050 Hz.
# Usage: python scripts\extract_voices.py [output folder] [bank A|B]   (default dumps\voices, both banks)
import os, struct, sys, wave

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # project folder (scripts\..)
BANKS = {"A": 0xEC08E0, "B": 0x11D89E0}
RATE = 22050


def decode_vadpcm(data, book, order, npred):
    """N64 VADPCM: 9-byte frames (scale/predictor byte + 16 4-bit samples), two halves of 8 per frame."""
    out, last = [], [0] * 8
    for f in range(len(data) // 9):
        frame = data[f * 9:f * 9 + 9]
        scale = 1 << (frame[0] >> 4)
        coefs = book[(frame[0] & 15) % npred]
        ins = []
        for b in frame[1:]:
            for n in (b >> 4, b & 15):
                ins.append((n - 16 if n >= 8 else n) * scale)
        for half in range(2):
            ix = ins[half * 8:half * 8 + 8]
            res = []
            for j in range(8):
                acc = ix[j] << 11
                for o in range(order):
                    acc += coefs[o][j] * last[8 - order + o]
                for k in range(j):
                    acc += coefs[order - 1][j - k - 1] * ix[k]
                res.append(max(-32768, min(32767, acc >> 11)))
            out += res
            last = res
    return out


def clips(rom, base):
    """Yields (number, pcm samples) for every sample of the bank whose PtrTables file starts at `base`."""
    count, _, _, records = struct.unpack_from(">4I", rom, base + 0x20)
    waves = rom.find(b"N64 WaveTables", base)
    for i in range(count):
        record = struct.unpack_from(">I", rom, base + records + 4 * i)[0]
        offset, length, _, _, book_at = struct.unpack_from(">5I", rom, base + record)
        order, npred = struct.unpack_from(">2I", rom, base + book_at)
        values = struct.unpack_from(">%dh" % (order * npred * 8), rom, base + book_at + 8)
        book = [[list(values[(p * order + o) * 8:(p * order + o) * 8 + 8]) for o in range(order)] for p in range(npred)]
        yield i, decode_vadpcm(rom[waves + offset:waves + offset + length], book, order, npred)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BASE, "dumps", "voices")
    only = sys.argv[2].upper() if len(sys.argv) > 2 else None
    rom = open(os.path.join(BASE, "rom", "game.z64"), "rb").read()
    os.makedirs(out_dir, exist_ok=True)
    for name, base in BANKS.items():
        if only and name != only:
            continue
        n = 0
        for number, pcm in clips(rom, base):
            with wave.open(os.path.join(out_dir, f"{name}_{number:03d}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
            n += 1
        print(f"bank {name}: {n} clips")
    print(f"-> {out_dir}")


if __name__ == "__main__":
    main()
