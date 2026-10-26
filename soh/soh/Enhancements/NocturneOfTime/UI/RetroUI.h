#pragma once

// Nocturne of Time: the Famicom Megami Tensei look for every menu - our own 8x8 pixel font, black windows with
// white rules, segmented stat bars, and pixel-sharp images (portraits, moon sprites). Draws into the overlay
// display list; replaces the debug font (GfxPrint).

#include <cstdint>
#include <string>
#include <vector>

namespace Retro {

struct Color {
    uint8_t r, g, b, a;
};

// Famicom palette, as in the mock-ups
constexpr Color White{ 252, 252, 252, 255 };
constexpr Color Black{ 0, 0, 0, 255 };
constexpr Color GreenLight{ 184, 248, 184, 255 }; // stat fill
constexpr Color Green{ 88, 216, 84, 255 };        // bullets, labels
constexpr Color GreenDark{ 0, 168, 0, 255 };      // empty bar
constexpr Color Yellow{ 248, 184, 0, 255 };       // cursor, selection
constexpr Color Blue{ 60, 188, 252, 255 };        // uncommon items, press turn
constexpr Color Violet{ 168, 120, 248, 255 };     // moon, poison, rare items
constexpr Color Red{ 248, 56, 0, 255 };           // weak, danger
constexpr Color Orange{ 252, 152, 56, 255 };      // legendary, burn
constexpr Color Grey{ 124, 124, 124, 255 };       // disabled, hints
constexpr Color Cream{ 252, 224, 168, 255 };

inline Color WithAlpha(Color c, uint8_t a) {
    return { c.r, c.g, c.b, a };
}
// 0xRRGGBB00 (battle event colours) to a Color; 0 gives `fallback`.
Color FromPacked(uint32_t rgb0, Color fallback);

constexpr int kCharW = 8;  // every glyph is 8 wide
constexpr int kLineH = 10; // comfortable line spacing

// Number of glyphs in a UTF-8 string (the symbols ◆ ▶ ◀ ▼ ✻ × ○ ■ count as one).
size_t Length(const std::string& utf8);
// Word wrap to `chars` glyphs per line.
std::vector<std::string> Wrap(const std::string& text, size_t chars);
// Cut to `chars` glyphs, ending in ".." when shortened.
std::string Fit(const std::string& text, size_t chars);

// Draws into a display list pointer (the caller's OVERLAY_DISP, passed as Gfx**).
class Painter {
  public:
    explicit Painter(void* gfxPtrPtr);

    void Rect(int x, int y, int w, int h, Color c);
    // The whole screen, out to both widescreen edges
    void FillScreen(Color c);
    static int ScreenLeft();  // x of the screen's left edge (negative in widescreen)
    static int ScreenRight(); // x of the right edge (past 320 in widescreen)
    // Black window with a 2px rule, Famicom style
    void Window(int x, int y, int w, int h, Color rule = White, uint8_t fillAlpha = 235);
    void Text(int x, int y, const std::string& s, Color c);
    void TextRight(int right, int y, const std::string& s, Color c);
    void TextCenter(int centerX, int y, const std::string& s, Color c);
    // Big text: every glyph drawn `scale` times its size (title cards)
    void TextScaled(int x, int y, const std::string& s, Color c, int scale);
    void TextCenterScaled(int centerX, int y, const std::string& s, Color c, int scale);
    // Segmented bar: `segs` blocks, the first `lit` in `on`, the rest in `off`
    void Bar(int x, int y, int segs, int lit, int segW, int segH, int gap, Color on, Color off);
    // An RGBA32 texture drawn pixel-sharp at w x h
    void Image(const uint8_t* rgba32, int texW, int texH, int x, int y, int w, int h, uint8_t alpha = 255);
    // The Famicom moon sprite for a phase (0 = New .. 7 = Full)
    void Moon(int phase, int x, int y, int size);
    // A party member's Famicom-style portrait (48px source) at size x size, false if there is none
    bool Portrait(const std::string& name, int x, int y, int size);

  private:
    enum class Mode { None, Fill, Font, Image };
    void UseFill();
    void UseFont();
    void UseImage(const uint8_t* tex, int texW, int texH);
    void* mGfx; // Gfx**
    Mode mMode = Mode::None;
    const uint8_t* mImage = nullptr;
};

} // namespace Retro
