#!/usr/bin/env python3
"""Pack the Nocturne OST into soh.o2r as streamed custom sequences.

For each track this writes, under soh/assets/custom/ (the packer copies that folder
into soh.o2r as-is, so soh/assets/custom/custom/music/X is archived as custom/music/X):

  custom/samples/nocturne/<Name>L.wav, <Name>R.wav   mono 16-bit halves, cut at the loop end
  custom/samples/nocturne/<Name>L, <Name>R           Sample XML (S16, wav, loop forever)
  custom/fonts/Nocturne<Name>                        SoundFont XML: instrument 0 = L, 1 = R
  custom/music/Nocturne<Name>                        streamed stereo Sequence XML

The engine's synth is mono, and a streamed stereo sequence plays instrument 0 hard left
and instrument 1 hard right, which is why each track is split in two. The sample loop
does the looping, so the intro plays once and the loop body repeats with no gap.

Filenames avoid "_" because the audio editor splits names on it.

Usage: python3 ost/tools/pack_game_audio.py   (from the repo root, rerun when a track changes)
"""
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "soh", "assets", "custom")

# Name -> source WAV (32 kHz 16-bit stereo with a smpl loop)
TRACKS = {
    "HollowedHuskDungeon": "ost/hollowed_husk_dungeon/Hollowed Husk - Dungeon Theme (beta).wav",
    "HollowedHuskBattleA": "ost/hollowed_husk_battle_a/Hollowed Husk - Battle A (beta).wav",
    "KokiriHavenShop": "ost/kokiri_haven_shop/Take Your Time - Kokiri Haven Shop (beta).wav",
}

# --- CRC64 exactly as libultraship's StrHash64 CRC64(): table-driven, init ~0, no final xor
POLY = 0x42F0E1EBA9EA3693
MASK = (1 << 64) - 1
TABLE = []
for i in range(256):
    c = i << 56
    for _ in range(8):
        c = ((c << 1) ^ POLY) if c & (1 << 63) else (c << 1)
        c &= MASK
    TABLE.append(c)


def crc64(s):
    crc = MASK
    for b in s.encode():
        crc = TABLE[((crc >> 56) ^ b) & 0xFF] ^ ((crc << 8) & MASK)
    return crc


def read_smpl_loop(path):
    data = open(path, "rb").read()
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"smpl":
            body = data[pos + 8:pos + 8 + size]
            if struct.unpack("<I", body[28:32])[0] >= 1:
                start, end = struct.unpack("<II", body[36 + 8:36 + 16])
                return start, end + 1  # smpl end is inclusive, the engine's is exclusive
        pos += 8 + size + (size & 1)
    sys.exit(f"{path}: no smpl loop")


def write_mono(path, rate, frames):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(frames)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)


def pack(name, src):
    src = os.path.join(ROOT, src)
    start, end = read_smpl_loop(src)
    with wave.open(src, "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2, f"{src}: need 16-bit stereo"
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    frames = min(end, len(raw) // 4)
    assert start < frames, f"{src}: bad loop"
    # split the interleaved stereo into two mono halves, nothing past the loop end is ever played
    import array
    pcm = array.array("h")
    pcm.frombytes(raw[:frames * 4])
    if sys.byteorder != "little":
        pcm.byteswap()
    halves = {"L": pcm[0::2], "R": pcm[1::2]}

    sdir = "custom/samples/nocturne"
    os.makedirs(os.path.join(OUT, sdir), exist_ok=True)
    for side, half in halves.items():
        if sys.byteorder != "little":
            half.byteswap()
        wav_rel = f"{sdir}/{name}{side}.wav"
        write_mono(os.path.join(OUT, wav_rel), rate, half.tobytes())
        write(os.path.join(OUT, f"{sdir}/{name}{side}"),
              f'<Sample Version="0" Codec="S16" Medium="Ram" bit26="0" Size="{frames * 2}" '
              f'Path="{wav_rel}" CustomFormat="wav">\n'
              f'    <ADPCMLoop Start="{start}" End="{frames}" Count="4294967295"/>\n'
              f'</Sample>\n')

    font_rel = f"custom/fonts/Nocturne{name}"
    instruments = ""
    for side in "LR":
        instruments += (
            '        <Instrument IsValid="true" Loaded="0" NormalRangeLo="0" NormalRangeHi="127" ReleaseRate="208">\n'
            '            <Envelopes>\n'
            '                <Envelope Delay="1" Arg="32700"/>\n'
            '                <Envelope Delay="-1" Arg="0"/>\n'
            '            </Envelopes>\n'
            f'            <NormalNotesSound Tuning="1.0" SampleRef="{sdir}/{name}{side}"/>\n'
            '        </Instrument>\n')
    write(os.path.join(OUT, font_rel),
          '<SoundFont Version="0" Num="0" Medium="Ram" CachePolicy="Either" Data1="0" Data2="0" Data3="0">\n'
          '    <Instruments>\n' + instruments + '    </Instruments>\n'
          '</SoundFont>\n')

    h = crc64(font_rel)
    fonts = "".join(f'        <Font FontIdx="{(h >> (8 * b)) & 0xFF}"/>\n' for b in range(8))
    # Looped streamed sequences hold the note for 0x7FFF ticks (~11 h); the sample loop repeats the body.
    write(os.path.join(OUT, f"custom/music/Nocturne{name}"),
          '<Sequence Version="0" Medium="Ram" CachePolicy="Either" Size="0" Index="0" '
          'Streamed="true" Length="40000" Looped="true" Stereo="true">\n'
          '    <Fonts>\n' + fonts + '    </Fonts>\n'
          '</Sequence>\n')
    print(f"{name}: {rate} Hz, {frames} frames, loop {start}-{frames}, font crc {h:016x}")


if __name__ == "__main__":
    for n, s in TRACKS.items():
        pack(n, s)
