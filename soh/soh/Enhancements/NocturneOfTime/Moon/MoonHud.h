#pragma once

// Nocturne of Time: the moon dial (moon icon, phase and "MOON") in the upper-right corner.

struct PlayState;

// Count a dungeon step toward the next phase (called by the crawler on each completed step).
void MoonHud_OnStep();
// Field HUD: the moon dial below the C buttons; it pulses for a few seconds after the phase changes.
void MoonHud_DrawField(PlayState* play);
// Smaller dial in the battle screen's top bar (right edge).
void MoonHud_DrawBattle(PlayState* play);
int MoonHud_BattleDialWidth();
