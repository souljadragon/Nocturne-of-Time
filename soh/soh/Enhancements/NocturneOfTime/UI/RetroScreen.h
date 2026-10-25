#pragma once

// Nocturne of Time: the Megami Tensei screen. While the crawler is active the 3D world renders into a window
// (a viewport) framed by black, with the HUD laid out around it. Exploring and battles share one screen: a top
// bar, the view window, a message window and a bottom row (party cards, or commands and the party table).

namespace Retro {
class Painter;
}

struct RetroRect {
    int x, y, w, h;
};

// The 3D view window in 320x240 screen coordinates, the same for exploring and battles
constexpr RetroRect kView{ 8, 26, 304, 122 };
constexpr RetroRect kFieldView = kView;
constexpr RetroRect kBattleView = kView;
// Rows under the view
constexpr int kMessageY = kView.y + kView.h + 6; // message window, 28 tall
constexpr int kBottomY = kMessageY + 32;           // bottom row, to 4px above the screen's foot

// Black over the whole screen (to both widescreen edges) except `view`, and a white rule around it.
void RetroScreen_DrawFrame(Retro::Painter& p, const RetroRect& view);
