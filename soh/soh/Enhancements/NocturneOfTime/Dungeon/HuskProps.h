#pragma once

// Nocturne of Time: OoT's own models drawn as the Husk's props (its torches, the Deku Tree's webs, Gossip Stones
// as Sheikah Stones, Gohma's eggs as egg sacs, treasure chests). They are drawn straight from the game's assets
// by the Husk's world actor, so no actor or object bank is needed. C, so it can use the game's draw macros.

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Once a frame from the world actor's update (the chests' skeleton poses)
void HuskProps_Update(PlayState* play);
// A chest has just been opened at (x, y, z): play its lid animation there
void HuskProps_OpenChest(f32 x, f32 y, f32 z);
// The scene changed (skeletons must be set up again)
void HuskProps_Reset(void);

// Drawing, from the world actor's draw. Positions are the base of the prop, on the floor.
void HuskProps_Torch(PlayState* play, f32 x, f32 y, f32 z, f32 scale, s32 lit);
void HuskProps_WallWeb(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 width, f32 height);
void HuskProps_FloorWeb(PlayState* play, f32 x, f32 y, f32 z, f32 width);
void HuskProps_SheikahStone(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 scale);
void HuskProps_EggSac(PlayState* play, f32 x, f32 y, f32 z, f32 size);
void HuskProps_Chest(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 scale, s32 open);

#ifdef __cplusplus
}
#endif
