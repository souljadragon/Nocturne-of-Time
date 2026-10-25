#pragma once

// Nocturne of Time: runs battles inside the game. Encounters come from a danger meter that fills as the
// crawler takes steps; a battle freezes the dungeon, shows the enemies in front of the first-person camera and
// drives the Press Turn rules from BattleEngine with menus and messages.

struct PlayState;
struct Player;

bool Battle_IsActive();

// For saving and loading: forget per-save battle state (bestiary, encounter meter); the enemy kinds whose
// affinities Navi has analysed.
void Battle_ResetForLoad();
#ifdef __cplusplus
#include <set>
std::set<int>& Battle_Bestiary();
#endif

// The crawler finished a step: fill the danger meter and maybe start a battle.
void Battle_OnStep(PlayState* play, Player* player);

// Each frame of a battle, from the crawler's input hook. `input` is the game's Input*; the battle reads it and
// the crawler then clears it so Link doesn't act.
void Battle_Update(PlayState* play, Player* player, void* input);

// Battle menus, icons and messages (drawn where the minimap would be drawn, on top of the HUD).
void Battle_DrawUi(PlayState* play);

// The encounter danger gem beside the automap (blue, yellow, red).
// How close the next encounter is: 0 calm, 1 alert, 2 danger (the HUD's gem).
int Battle_DangerLevel();

// Abandon any battle without results (scene change, crawler switched off).
void Battle_Abort();
