#include "RetroUI.h"
#include "RetroFontTexture.h"
#include "RetroArtTextures.h"

#include <memory>

extern "C" {
#include "global.h"
}

namespace Retro {

namespace {

constexpr int kSymbolBase = 96; // atlas cell of the first symbol (after ASCII 32-127)

// Atlas cell for a code point
int Cell(uint32_t cp) {
    if (cp >= 32 && cp < 127) {
        return (int)cp - 32;
    }
    switch (cp) {
        case 0x25C6: // ◆
            return kSymbolBase + 0;
        case 0x25B6: // ▶
        case 0x25BA:
            return kSymbolBase + 1;
        case 0x25C0: // ◀
        case 0x25C4:
            return kSymbolBase + 2;
        case 0x25BC: // ▼
            return kSymbolBase + 3;
        case 0x273B: // ✻
            return kSymbolBase + 4;
        case 0x00D7: // ×
            return kSymbolBase + 5;
        case 0x25CB: // ○
            return kSymbolBase + 6;
        case 0x25A0: // ■
            return kSymbolBase + 7;
        case 0x2019: // ’
            return '\'' - 32;
        case 0x2013: // – —
        case 0x2014:
            return '-' - 32;
        default:
            return '?' - 32;
    }
}

// Decode the next UTF-8 code point at s[i], advancing i
uint32_t Next(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
        i += 1;
        return c;
    }
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    i += 1;
    for (int k = 0; k < extra && i < s.size(); k++, i++) {
        cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    }
    return cp;
}

} // namespace

Color FromPacked(uint32_t rgb0, Color fallback) {
    if (rgb0 == 0) {
        return fallback;
    }
    return { (uint8_t)(rgb0 >> 24), (uint8_t)(rgb0 >> 16), (uint8_t)(rgb0 >> 8), 255 };
}

size_t Length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        Next(s, i);
        n++;
    }
    return n;
}

std::vector<std::string> Wrap(const std::string& text, size_t chars) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) {
            return;
        }
        if (!line.empty() && Length(line) + 1 + Length(word) > chars) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') {
            flushWord();
        } else if (c == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string Fit(const std::string& text, size_t chars) {
    if (Length(text) <= chars) {
        return text;
    }
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < text.size() && n + 2 < chars;) {
        size_t start = i;
        Next(text, i);
        out += text.substr(start, i - start);
        n++;
    }
    return out + "..";
}

Painter::Painter(void* gfxPtrPtr) : mGfx(gfxPtrPtr) {
}

#define GFX (*(Gfx**)mGfx)

void Painter::UseFill() {
    if (mMode == Mode::Fill) {
        return;
    }
    mMode = Mode::Fill;
    gDPPipeSync(GFX++);
    gSPClearGeometryMode(GFX++, G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                    G_TEXTURE_GEN_LINEAR | G_SHADING_SMOOTH | G_LOD);
    gDPSetOtherMode(GFX++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_1PRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_CLD_SURF | G_RM_CLD_SURF2);
    gDPSetCombineMode(GFX++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
}

static void TextureMode(Gfx*& g) {
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                  G_TEXTURE_GEN_LINEAR | G_SHADING_SMOOTH | G_LOD);
    // point sampling keeps every pixel of the font and sprites crisp
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
}

void Painter::UseFont() {
    if (mMode == Mode::Font) {
        return;
    }
    mMode = Mode::Font;
    TextureMode(GFX);
    // A tile load (not a block load), as for the moon dial: the atlas is bigger than one block load addresses
    gDPLoadTextureTile(GFX++, gRetroFontAtlas, G_IM_FMT_RGBA, G_IM_SIZ_32b, kRetroFontAtlasW, kRetroFontAtlasH, 0, 0,
                       kRetroFontAtlasW - 1, kRetroFontAtlasH - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                       G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
}

void Painter::UseImage(const uint8_t* tex, int texW, int texH) {
    if (mMode == Mode::Image && mImage == tex) {
        return;
    }
    mMode = Mode::Image;
    mImage = tex;
    TextureMode(GFX);
    gDPLoadTextureTile(GFX++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0, 0, texW - 1, texH - 1, 0,
                       G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                       G_TX_NOLOD);
}

void Painter::Rect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    UseFill();
    gDPPipeSync(GFX++);
    gDPSetPrimColor(GFX++, 0, 0, c.r, c.g, c.b, c.a);
    gDPFillRectangle(GFX++, x, y, x + w, y + h);
}

void Painter::Window(int x, int y, int w, int h, Color rule, uint8_t fillAlpha) {
    Rect(x, y, w, h, WithAlpha(Black, fillAlpha));
    Rect(x, y, w, 2, rule);
    Rect(x, y + h - 2, w, 2, rule);
    Rect(x, y, 2, h, rule);
    Rect(x + w - 2, y, 2, h, rule);
}

void Painter::Text(int x, int y, const std::string& s, Color c) {
    if (s.empty()) {
        return;
    }
    UseFont();
    gDPPipeSync(GFX++);
    gDPSetPrimColor(GFX++, 0, 0, c.r, c.g, c.b, c.a);
    int cx = x;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = Next(s, i);
        if (cp != ' ' && cx >= 0 && y >= 0) {
            int cell = Cell(cp);
            int s0 = (cell % 16) * 8, t0 = (cell / 16) * 8;
            gSPTextureRectangle(GFX++, cx << 2, y << 2, (cx + 8) << 2, (y + 8) << 2, G_TX_RENDERTILE, s0 << 5,
                                t0 << 5, 1 << 10, 1 << 10);
        }
        cx += kCharW;
    }
}

void Painter::TextRight(int right, int y, const std::string& s, Color c) {
    Text(right - (int)Length(s) * kCharW, y, s, c);
}

void Painter::TextCenter(int centerX, int y, const std::string& s, Color c) {
    Text(centerX - (int)Length(s) * kCharW / 2, y, s, c);
}

void Painter::Bar(int x, int y, int segs, int lit, int segW, int segH, int gap, Color on, Color off) {
    for (int i = 0; i < segs; i++) {
        Rect(x + i * (segW + gap), y, segW, segH, i < lit ? on : off);
    }
}

void Painter::Image(const uint8_t* rgba32, int texW, int texH, int x, int y, int w, int h, uint8_t alpha) {
    if (rgba32 == nullptr || w <= 0 || h <= 0) {
        return;
    }
    UseImage(rgba32, texW, texH);
    gDPPipeSync(GFX++);
    gDPSetPrimColor(GFX++, 0, 0, 255, 255, 255, alpha);
    int dsdx = (texW << 10) / w, dtdy = (texH << 10) / h;
    gSPWideTextureRectangle(GFX++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
}

void Painter::Moon(int phase, int x, int y, int size) {
    Image(kRetroMoons[((phase % 8) + 8) % 8], 32, 32, x, y, size, size);
}

bool Painter::Portrait(const std::string& name, int x, int y, int size) {
    int src = size >= 48 ? 64 : 32;
    const uint8_t* tex = RetroPortrait(name.c_str(), src);
    if (tex == nullptr) {
        return false;
    }
    Image(tex, src, src, x, y, size, size);
    return true;
}

#undef GFX

} // namespace Retro
