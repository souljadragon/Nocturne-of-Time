# Nocturne of Time — Music and Sound

Audio files for the mod. The decisions and specs live in the Claude project docs (`ost/style.md`, `ost/tracklist.md`, `ost/tracks/*.md`, `sfx/style.md`); this folder holds the files themselves.

**Naming:** every track the user is still working on is marked **(beta)** until it's confirmed as final. The plain `<Title> (beta).wav` file in each folder is the game file; variants carry a tag in square brackets (`[preview]`, `[master 48k 24-bit]`, …). Folder names stay simple and stable so code can point at them.

All game files are 32 kHz 16-bit stereo WAVs, mastered to -14 LUFS with true peak under -1 dBTP. Loop points are in samples and are also written into each WAV's `smpl` chunk. The intro before the loop start plays once.

Why 32 kHz: an open Ship of Harkinian issue (#5897) reports streamed custom music above 32 kHz playing too slowly for some players. This fork is based on SoH 9.3.1, which already includes the fix that makes streamed music respect its loop end.

## Hollowed Husk - Dungeon Theme (beta) — `hollowed_husk_dungeon/`
Exploration music for the first dungeon, **Hollowed Husk** (the Deku Tree, renamed). Composed by the user in FL Studio; mixed in the OST chat (mix v1, Oct 28, 2026). 110 BPM, D minor.

| File | Use |
|---|---|
| `Hollowed Husk - Dungeon Theme (beta).wav` | **The file to put in the game.** Intro 0–4.364 s (2 bars), loop 4.364–109.091 s = samples 139,637–3,490,910 |
| `Hollowed Husk - Dungeon Theme (beta) [master 48k 24-bit].wav` | The mix at 48 kHz 24-bit (loop 209,455–5,236,364) |
| `Hollowed Husk - Dungeon Theme (beta) [preview].mp3` | Intro + loop twice + fade |
| `placeholder/` | The OST chat's placeholder v1 (game loop, preview, per-part MIDI) |

## Hollowed Husk - Battle A (beta) — `hollowed_husk_battle_a/`
Random-battle theme for Hollowed Husk. Composed by the user in FL Studio; mixed in the OST chat (mix v1, Oct 28, 2026). 160 BPM, D minor. (Earlier called "Hollow Husk - Random Encounter" / "Hollowed Husk - Random Encounter".)

| File | Use |
|---|---|
| `Hollowed Husk - Battle A (beta).wav` | **The file to put in the game.** Intro 0–6 s, loop 6.000–66.000 s = samples 192,000–2,112,000 |
| `Hollowed Husk - Battle A (beta) [44.1 kHz].wav` | The same at 44.1 kHz (loop 264,600–2,910,600) |
| `Hollowed Husk - Battle A (beta) [master 48k 24-bit].wav` | The mix at 48 kHz 24-bit (loop 288,000–3,168,000) |
| `Hollowed Husk - Battle A (beta) [preview].mp3` | Intro + loop twice + fade |
| `placeholder/` | The OST chat's v2 placeholder: per-part MIDI and a preview |

## Take Your Time - Kokiri Haven Shop (beta) — `kokiri_haven_shop/`
Shop theme for **Kokiri Haven**, the hub (Kokiri Forest, renamed). Not built yet in the game. Only the OST chat's placeholder exists so far; the user's beta goes in this folder as `Take Your Time - Kokiri Haven Shop (beta).wav`. 120 BPM, F major, swung 8ths.

| File | Use |
|---|---|
| `placeholder/Take Your Time - Kokiri Haven Shop (placeholder v1).wav` | 32 kHz 16-bit. Intro 0–4 s (2 bars), loop 4.000–68.000 s = samples 128,000–2,176,000. -14 LUFS |
| `placeholder/… [preview].mp3` | Intro + loop twice + fade |
| `placeholder/… [parts].mid` | One track per part, section markers. Straight timing: set swing to ~55% on 8ths |

## Getting it into the game (for the build chat)
- **Dungeon music:** "Hollowed Husk - Dungeon Theme (beta)" should replace the Deku Tree's background music while exploring.
- **Battle music:** battles queue music in `soh/soh/Enhancements/NocturneOfTime/Battle/BattleScene.cpp` with `Audio_QueueSeqCmd` (currently `NA_BGM_BOSS` and `NA_BGM_BOSS_CLEAR` for Queen Gohma). Random Hollowed Husk battles should start "Hollowed Husk - Battle A (beta)" when the battle begins, then return to the dungeon theme afterwards.
- The file names contain spaces and brackets. If the audio pack tool or loader dislikes them, copy the files to simple names when packing rather than renaming them here.
- `docs/CUSTOM_MUSIC.md` only covers `.seq` custom sequences; streamed audio packs (real WAV/OGG files) are newer. Check how this SoH version loads them before choosing a pack layout.

## Tools (`tools/`)
- `tools/placeholder/`: the scripts that wrote and rendered the Battle A placeholder (`compose.py` = notes, `render.py` = synth and mix, `write_midi.py` = MIDI). Run from that folder: `python3 render.py`, `python3 write_midi.py` (need numpy and scipy; output goes to `out/`).
- `tools/dungeon_placeholder/`: the Dungeon Theme placeholder (`compose_d.py` notes, `render_d.py` synth, `mixdown.py` mix and master, `write_midi_d.py`). They import helpers from `tools/placeholder/` and `tools/mix/`; the `sys.path` lines point at the OST chat's work folders, so adjust them to run locally.
- `tools/shop_placeholder/`: the Kokiri Haven Shop placeholder (`compose_s.py` notes, `render_s.py` synth, `mixdown_s.py` mix and master, `write_midi_s.py`). Same import setup as the dungeon placeholder; it also needs the reference track in `ref/ref.mp3` for the tonal match.
- `tools/mix/`: the Battle A stem mix. `session.py` runs the per-stem chains, `master.py` masters and exports. Needs numpy, scipy and ffmpeg.
- `tools/dungeon_mix/session_d.py`: the Dungeon Theme stem mix and master, in one script. It uses `mix.py` and `common.py` from `tools/mix/`. Stems go in `stems/` named after their part (`piano.wav`, `organ.wav`, …), plus the reference as `stems/ref.mp3`.

Stems aren't stored here; the user keeps them in the FL Studio projects.
