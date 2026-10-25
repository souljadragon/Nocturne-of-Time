#pragma once

// Nocturne of Time: the moon icon in the upper-right corner, and a note when the phase changes.

struct PlayState;

// Count a dungeon step toward the next phase (called by the crawler on each completed step).
void MoonHud_OnStep();
// Field HUD: moon below the C buttons, plus the phase name for a few seconds after it changes.
void MoonHud_DrawField(PlayState* play);
// Smaller moon for the battle screen's top bar.
void MoonHud_DrawBattle(PlayState* play);
