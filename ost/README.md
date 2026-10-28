# Nocturne of Time — Music and Sound

Audio files for the mod. The decisions and specs live in the Claude project docs (`ost/style.md`, `ost/tracklist.md`, `ost/tracks/*.md`, `sfx/style.md`); this folder holds the files themselves.

All game files are 32 kHz 16-bit stereo WAVs, mastered to -14 LUFS with true peak under -1 dBTP. Loop points are in samples and are also written into each WAV's `smpl` chunk. The intro before the loop start plays once.

Why 32 kHz: an open Ship of Harkinian issue (#5897) reports streamed custom music above 32 kHz playing too slowly for some players. This fork is based on SoH 9.3.1, which already includes the fix that makes streamed music respect its loop end.

## Hollowed Husk — dungeon theme (`hollowed_husk_dungeon/`)
Exploration music for the first dungeon, **Hollowed Husk** (the Deku Tree, renamed). Composed by the user in FL Studio; mixed in the OST chat (mix v1, Oct 28, 2026). 110 BPM, D minor.

| File | Use |
|---|---|
| `hollowed_husk_game_loop_32k.wav` | **The file to put in the game.** Intro 0–4.364 s (2 bars), loop 4.364–109.091 s = samples 139,637–3,490,910 |
| `hollowed_husk_master_48k24.wav` | The mix at 48 kHz 24-bit (loop 209,455–5,236,364) |
| `hollowed_husk_preview.mp3` | Intro + loop twice + fade |
| `placeholder/` | The OST chat's placeholder v1 (game loop, preview, per-part MIDI) |

## Hollowed Husk - Random Encounter (`hollowed_husk_random_encounter/`)
Random-battle theme for Hollowed Husk. Composed by the user in FL Studio; mixed in the OST chat (mix v1, Oct 28, 2026). 160 BPM, D minor. (Earlier called "Hollow Husk - Random Encounter".)

| File | Use |
|---|---|
| `hollowed_husk_random_encounter_game_loop_32k.wav` | **The file to put in the game.** Intro 0–6 s, loop 6.000–66.000 s = samples 192,000–2,112,000 |
| `hollowed_husk_random_encounter_game_loop_44k.wav` | The same at 44.1 kHz (loop 264,600–2,910,600) |
| `hollowed_husk_random_encounter_master_48k24.wav` | The mix at 48 kHz 24-bit (loop 288,000–3,168,000) |
| `hollowed_husk_random_encounter_preview.mp3` | Intro + loop twice + fade |
| `placeholder/` | The OST chat's v2 placeholder: per-part MIDI and a preview |

## Getting it into the game (for the build chat)
- **Dungeon music:** Hollowed Husk should replace the Deku Tree's background music while exploring.
- **Battle music:** battles queue music in `soh/soh/Enhancements/NocturneOfTime/Battle/BattleScene.cpp` with `Audio_QueueSeqCmd` (currently `NA_BGM_BOSS` and `NA_BGM_BOSS_CLEAR` for Queen Gohma). Random Hollowed Husk battles should start "Hollowed Husk - Random Encounter" when the battle begins, then return to the dungeon theme afterwards.
- `docs/CUSTOM_MUSIC.md` only covers `.seq` custom sequences; streamed audio packs (real WAV/OGG files) are newer. Check how this SoH version loads them before choosing a pack layout.

## Tools (`tools/`)
- `tools/placeholder/`: the scripts that wrote and rendered the battle-theme placeholder (`compose.py` = notes, `render.py` = synth and mix, `write_midi.py` = MIDI). Run from that folder: `python3 render.py`, `python3 write_midi.py` (need numpy and scipy; output goes to `out/`).
- `tools/dungeon_placeholder/`: the dungeon-theme placeholder (`compose_d.py` notes, `render_d.py` synth, `mixdown.py` mix and master, `write_midi_d.py`). They import helpers from `tools/placeholder/` and `tools/mix/`; the `sys.path` lines point at the OST chat's work folders, so adjust them to run locally.
- `tools/mix/`: the battle-theme stem mix. `session.py` runs the per-stem chains, `master.py` masters and exports. Needs numpy, scipy and ffmpeg.
- `tools/dungeon_mix/session_d.py`: the dungeon-theme stem mix and master, in one script. It uses `mix.py` and `common.py` from `tools/mix/`. Stems go in `stems/` named after their part (`piano.wav`, `organ.wav`, …), plus the reference as `stems/ref.mp3`.

Stems aren't stored here; the user keeps them in the FL Studio projects.
