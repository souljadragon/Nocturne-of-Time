#pragma once

// Nocturne of Time: the moon on the HUD - the Famicom moon sprite and its phase in a small window. The window's
// rule pulses violet for a few seconds after the phase changes, and stays violet at Full Moon.

namespace Retro {
class Painter;
}

// Count a dungeon step toward the next phase (called by the crawler on each completed step).
void MoonHud_OnStep();
// Advance the change pulse; call once per drawn frame.
void MoonHud_Tick();
// Width of the moon window for a sprite of `size` pixels.
int MoonHud_BoxWidth(int size);
// A message for a few seconds after the moon turns full, else nullptr.
const char* MoonHud_NoticeText();
// Draw the moon window with its top-left at (x, y). Height is size + 4.
void MoonHud_DrawBox(Retro::Painter& p, int x, int y, int size, int width = 0);
