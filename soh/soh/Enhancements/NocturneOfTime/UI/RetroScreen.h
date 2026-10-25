#pragma once

// Nocturne of Time: the Megami Tensei screen. While the crawler is active the 3D world renders into a window
// (a viewport) framed by black, with the HUD laid out around it - one layout for exploring, one for battles.

namespace Retro {
class Painter;
}

struct RetroRect {
    int x, y, w, h;
};

// 3D view windows in 320x240 screen coordinates
constexpr RetroRect kFieldView{ 8, 8, 206, 162 };
constexpr RetroRect kBattleView{ 8, 26, 304, 122 };

// Black over the whole screen (to both widescreen edges) except `view`, and a white rule around it.
void RetroScreen_DrawFrame(Retro::Painter& p, const RetroRect& view);
