# Nocturne of Time — Music and Sound

Audio files for the mod. The decisions and specs live in the Claude project docs (`ost/style.md`, `ost/tracklist.md`, `ost/tracks/*.md`, `sfx/style.md`); this folder holds the files themselves.

## Hollow Husk - Random Encounter (`hollow_husk/`)
Random-battle theme for the first dungeon (the Deku Tree, to be renamed). Composed by the user in FL Studio from the OST chat's placeholder; mixed in the OST chat (mix v1, Oct 28, 2026).

| File | Use |
|---|---|
| `hollow_husk_game_loop_32k.wav` | **The file to put in the game.** 32 kHz 16-bit stereo. Intro 0–6 s plays once, loop 6.000–66.000 s = samples 192,000–2,112,000 (also in the WAV's `smpl` chunk). -14 LUFS, true peak under -1 dBTP |
| `hollow_husk_game_loop_44k.wav` | The same at 44.1 kHz (loop 264,600–2,910,600) |
| `hollow_husk_master_48k24.wav` | The mix at 48 kHz 24-bit, for archiving and further work (loop 288,000–3,168,000) |
| `hollow_husk_preview.mp3` | Intro + loop twice + fade, for listening |
| `placeholder/` | The OST chat's v2 placeholder: per-part MIDI and a preview |

Why 32 kHz: an open Ship of Harkinian issue (#5897) reports streamed custom music above 32 kHz playing too slowly for some players. This fork is based on SoH 9.3.1, which already includes the fix that makes streamed music respect its loop end.

## Hollowed Husk — dungeon theme (`hollowed_husk_dungeon/`)
Exploration music for the first dungeon, Hollowed Husk (the Deku Tree, renamed). **Placeholder v1** from the OST chat (Oct 28, 2026), a sketch for the user's FL Studio version. 110 BPM, D minor, swung 16ths.

| File | Use |
|---|---|
| `hollowed_husk_game_loop_32k.wav` | Game file, 32 kHz 16-bit. Intro 2 bars once, loop 4.364–109.091 s = samples 139,637–3,490,910 (in the `smpl` chunk). -14 LUFS |
| `hollowed_husk_preview.mp3` | Intro + loop twice + fade |
| `hollowed_husk_parts.mid` | One track per part, section markers; whisper syllables as lyric events. Straight timing: set swing to ~58% |

## Getting it into the game (for the build chat)
- Battles queue music in `soh/soh/Enhancements/NocturneOfTime/Battle/BattleScene.cpp` with `Audio_QueueSeqCmd` (currently `NA_BGM_BOSS` and `NA_BGM_BOSS_CLEAR` for Queen Gohma). Random Deku Tree battles should start Hollow Husk when the battle begins and return to the dungeon music (Hollowed Husk) afterwards.
- `docs/CUSTOM_MUSIC.md` only covers `.seq` custom sequences; streamed audio packs (real WAV/OGG files) are newer. Check how this SoH version loads them before choosing a pack layout.

## Tools (`tools/`)
- `tools/placeholder/`: the scripts that wrote and rendered the placeholder (`compose.py` = notes, `render.py` = synth and mix, `write_midi.py` = MIDI). Run from that folder: `python3 render.py`, `python3 write_midi.py` (need numpy and scipy; output goes to `out/`).
- `tools/dungeon_placeholder/`: the Hollowed Husk dungeon placeholder (`compose_d.py` notes, `render_d.py` synth, `mixdown.py` mix and master, `write_midi_d.py`). They import helpers from `tools/placeholder/` and `tools/mix/`; the `sys.path` lines point at the OST chat's work folders, so adjust them to run locally.
- `tools/mix/`: the stem mix. `session.py` runs the per-stem chains (stems go in `stems/` as `hollowed_husk_<part>.wav`, plus a reference track), `master.py` masters and exports. Needs numpy, scipy and ffmpeg.

Stems aren't stored here; the user keeps them in the FL Studio project.
