#pragma once

#include <stdint.h>

// Nocturne's own music (ost/, packed into soh.o2r by ost/tools/pack_game_audio.py) in place of
// the vanilla tracks it stands in for. Every sequence the game starts is passed through
// NocturneMusic_Override first; anything Nocturne doesn't replace comes back unchanged.
#ifdef __cplusplus
extern "C" {
#endif

uint16_t NocturneMusic_Override(uint16_t seqId);

#ifdef __cplusplus
}

struct PlayState;

// A random battle starts / ends: swap to the area's battle theme and back
void NocturneMusic_StartBattle(PlayState* play);
void NocturneMusic_EndBattle(PlayState* play);
#endif
