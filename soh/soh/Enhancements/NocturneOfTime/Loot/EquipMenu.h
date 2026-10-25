#pragma once

// Nocturne of Time: the party / equipment menu (R while exploring in first person). Freezes the dungeon while
// open; lists each member's gear in rarity colours with item details and stat comparisons.

struct PlayState;

bool EquipMenu_IsOpen();
void EquipMenu_Open();
void EquipMenu_Close();

// Each frame while open, from the crawler's input hook. `input` is the game's Input*.
void EquipMenu_Update(void* input);

// Drawn in place of the HUD while open.
void EquipMenu_Draw(PlayState* play);
