#pragma once

// Nocturne of Time: the Hollowed Husk as three hand-made grid floors (design/hollowed-husk-layout.md).
//
// While the mod is on, the Deku Tree scene no longer shows its own rooms or actors: the floors in HuskFloors are
// built far away from the vanilla geometry (one floor at a time) by a world actor that draws the walls,
// floor, ceiling and props and gives them collision, so the crawler, battles and the automap work on them as on
// any level. This module also runs everything on the tiles: chests, Deku Sticks and fire, webs, torches,
// engravings, brambles, rot, pits, tripwires, egg sacs, the membrane gate, Sheikah Stones, the ways up and down.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Should the scene's own rooms be drawn? (z_room.c asks; false in the Husk)
int Husk_HidesRooms(void);
#ifdef __cplusplus
}

#include <nlohmann/json.hpp>
#include <string>
#include "z64math.h"

struct PlayState;
struct Player;

// The grid floors are in use (the mod is on and this is the Deku Tree)
bool Husk_Active(PlayState* play);
int Husk_Floor();
// Every frame the crawler runs here. True while Link is being placed on a floor (the crawler leaves him alone).
bool Husk_Frame(PlayState* play, Player* player);
// A step from `from` in crawler direction `facing` (0 +Z south, 1 +X east, 2 -Z north, 3 -X west). False if the
// grid blocks it; else `to` is the next tile's centre.
bool Husk_CanStep(PlayState* play, const Vec3f& from, int facing, Vec3f& to);
// A step ended on a tile. True if something happened there that replaces the encounter roll.
bool Husk_OnStepEnd(PlayState* play, Player* player);
// A pressed in the field. True if it did something here (the press is then used up).
bool Husk_OnA(PlayState* play, Player* player, int facing);

// The field message window's text (nullptr when none) and its colour (0xRRGGBB00)
const char* Husk_Message();
uint32_t Husk_MessageColor();
// Steps left on a burning Deku Stick (0: none)
int Husk_StickSteps();

// The membrane gate: open once the three egg sacs are destroyed
bool Husk_GateOpen();
bool Husk_GatePosition(float& x, float& y, float& z);

// Battles tell the Husk how they ended (egg sacs, the elite), and how the boss fight went
void Husk_OnBattleEnd(bool victory);
void Husk_OnBossResult(bool won);

void Husk_Reset();
nlohmann::json Husk_Save();
void Husk_Load(const nlohmann::json& data);
#endif
