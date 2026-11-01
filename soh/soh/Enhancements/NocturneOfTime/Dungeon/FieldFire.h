#pragma once

// Nocturne of Time: Deku Stick fire in the real dungeons, crawler style. Face a lit torch and press A to light a
// stick (a Nocturne consumable; it burns for 12 steps), then A at a web to burn it or at a cold torch to light it.
// Works on the game's own torches (ObjSyokudai) and webs (BgYdanSp), so their puzzles and switch flags behave
// as in OoT. Vanilla Deku Sticks that Link picks up or buys become Nocturne ones.

struct PlayState;
struct Player;

// A in the field, facing `yaw`. True if it did something (the press is used up).
bool FieldFire_OnA(PlayState* play, Player* player, short yaw);
// A step ended: the stick burns down
void FieldFire_OnStep();
// Every crawler frame (keeps vanilla sticks flowing into the Nocturne count; clears old messages)
void FieldFire_Tick(PlayState* play);
int FieldFire_StickSteps();
// The field message window's text (nullptr when none) and its colour (0xRRGGBB00)
const char* FieldFire_Message();
unsigned int FieldFire_MessageColor();
