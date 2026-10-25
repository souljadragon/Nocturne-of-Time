#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "MoonHud.h"
#include "Moon.h"
#include "MoonTextures.h"

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

constexpr int kNoticeFrames = 70; // phase name stays up this long after a change (20 frames = 1 second)
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
static void DrawMoonIcon(PlayState* play, int x, int y, int size) {
    const uint8_t* tex = kMoonTextures[(int)Moon_Phase() % 8];
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetOtherMode(OVERLAY_DISP++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, kMoonTexSize, kMoonTexSize, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    int step = (kMoonTexSize << 10) / size; // texels per pixel, s5.10
    gSPWideTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + size) << 2, (y + size) << 2, G_TX_RENDERTILE, 0, 0,
                            step, step);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawNotice(PlayState* play, int rightX, int y) {
    if (sNotice <= 0) {
        return;
    }
    sNotice--;
    std::string name = MoonPhaseName(Moon_Phase());
    u8 alpha = (u8)(sNotice > 20 ? 255 : sNotice * 12);
    OPEN_DISPS(play->state.gfxCtx);
    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, OVERLAY_DISP);
    bool full = Moon_Phase() == MoonPhase::Full;
    GfxPrint_SetColor(&printer, full ? 210 : 230, full ? 150 : 230, 255, alpha);
    GfxPrint_SetPosPx(&printer, rightX - (int)name.size() * 8, y);
    GfxPrint_Printf(&printer, "%s", name.c_str());
    OVERLAY_DISP = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    CLOSE_DISPS(play->state.gfxCtx);
}

void MoonHud_DrawField(PlayState* play) {
    // Below the C buttons in the upper-right corner
    // (anchored to the right edge like the C buttons, so it stays in the corner in widescreen)
    const int size = 32, y = 66;
    const int right = OTRGetRectDimensionFromRightEdge((f32)(SCREEN_WIDTH - 12));
    DrawMoonIcon(play, right - size, y, size);
    DrawNotice(play, right, y + size + 2);
}

void MoonHud_DrawBattle(PlayState* play) {
    DrawMoonIcon(play, SCREEN_WIDTH - 10 - 24, 4, 24);
}

static RegisterShipInitFunc initFunc(RegisterMoonHud, { CVAR_CRAWLER_NAME });
