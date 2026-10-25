#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "MoonHud.h"
#include "Moon.h"
#include "MoonDialTextures.h"

#include <memory>
#include <string>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
int16_t OTRGetRectDimensionFromRightEdge(float v);
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

using namespace Nocturne;

namespace {

constexpr int kNoticeFrames = 60; // the dial pulses this long after a change (20 frames = 1 second)
int sNotice = 0;

void RegisterMoonHud() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;
    // The moon belongs to the play session: a loaded save starts at New Moon (saving comes later)
    COND_HOOK(OnLoadGame, enabled, [](int32_t) {
        Moon_Reset();
        sNotice = 0;
    });
}

} // namespace

void MoonHud_OnStep() {
    if (Moon_OnStep()) {
        sNotice = kNoticeFrames;
        Sfx_PlaySfxCentered(Moon_Phase() == MoonPhase::Full ? NA_SE_SY_CORRECT_CHIME : NA_SE_SY_MESSAGE_PASS);
    }
}

// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.
// The dial: the phase's moon, its name and "MOON" on a rounded panel (pre-rendered by art/moon/make_dial.py at
// twice the size it is drawn, so it stays crisp). After a phase change its outline pulses violet.
static void DrawDial(PlayState* play, int x, int y, int w, int h) {
    const uint8_t* tex = kMoonDials[(int)Moon_Phase() % 8];
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);

    if (sNotice > 0) {
        // Pulse: a violet glow behind the panel that fades out
        u8 alpha = (u8)((sNotice > 20 ? 1.0f : sNotice / 20.0f) * (110 + 70 * Math_SinS((s16)(sNotice * 0x1800))));
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetOtherMode(OVERLAY_DISP++,
                        G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 160, 110, 255, alpha);
        gDPFillRectangle(OVERLAY_DISP++, x - 2, y - 2, x + w + 2, y + h + 2);
    }

    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetOtherMode(OVERLAY_DISP++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    // A tile load (not a block load): the dial has more texels than a block load can address
    gDPLoadTextureTile(OVERLAY_DISP++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, kMoonDialW, kMoonDialH, 0, 0, kMoonDialW - 1,
                       kMoonDialH - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                       G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    int dsdx = (kMoonDialW << 10) / w, dtdy = (kMoonDialH << 10) / h; // texels per pixel, s5.10
    gSPWideTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0, dsdx,
                            dtdy);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}

void MoonHud_DrawField(PlayState* play) {
    // Below the C buttons in the upper-right corner, anchored to the right edge so it stays in the corner in
    // widescreen
    const int w = kMoonDialW / 2, h = kMoonDialH / 2, y = 66;
    const int right = OTRGetRectDimensionFromRightEdge((f32)(SCREEN_WIDTH - 10));
    DrawDial(play, right - w, y, w, h);
    if (sNotice > 0) {
        sNotice--;
    }
}

void MoonHud_DrawBattle(PlayState* play) {
    // In the battle screen's top bar, a little smaller
    const int w = 76, h = 24;
    DrawDial(play, SCREEN_WIDTH - 10 - w, 3, w, h);
}

int MoonHud_BattleDialWidth() {
    return 76;
}

static RegisterShipInitFunc initFunc(RegisterMoonHud, { CVAR_CRAWLER_NAME });
