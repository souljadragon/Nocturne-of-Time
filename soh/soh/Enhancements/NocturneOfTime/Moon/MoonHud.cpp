#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include "MoonHud.h"
#include "Moon.h"
#include "../UI/RetroUI.h"

#include <memory>
#include <string>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

using namespace Nocturne;

namespace {

constexpr int kNoticeFrames = 100; // pulse / Full Moon message length after a change (20 frames = 1 second)
int sNotice = 0;

const char* ShortName(MoonPhase p) {
    switch (p) {
        case MoonPhase::New:
            return "NEW";
        case MoonPhase::Half:
            return "HALF";
        case MoonPhase::Full:
            return "FULL";
        case MoonPhase::TwoEighths:
            return "2/8";
        case MoonPhase::ThreeEighths:
            return "3/8";
        case MoonPhase::FiveEighths:
            return "5/8";
        case MoonPhase::SixEighths:
            return "6/8";
        case MoonPhase::SevenEighths:
            return "7/8";
        default:
            return "";
    }
}

void RegisterMoonHud() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;
    // The moon is saved with the file (Save/SaveHooks); nothing to do here
    (void)enabled;
}

} // namespace

void MoonHud_ResetNotice() {
    sNotice = 0;
}

void MoonHud_OnStep() {
    if (Moon_OnStep()) {
        sNotice = kNoticeFrames;
        Sfx_PlaySfxCentered(Moon_Phase() == MoonPhase::Full ? NA_SE_SY_CORRECT_CHIME : NA_SE_SY_MESSAGE_PASS);
    }
}

void MoonHud_Tick() {
    if (sNotice > 0) {
        sNotice--;
    }
}

const char* MoonHud_NoticeText() {
    if (sNotice <= 0) {
        return nullptr;
    }
    // Only the Full Moon is announced; other changes just pulse the moon window
    return Moon_Phase() == MoonPhase::Full ? "The moon is full! Monsters stir." : nullptr;
}

int MoonHud_BoxWidth(int size) {
    return 4 + size + 4 + 4 * Retro::kCharW + 4; // rule, sprite, gap, up to four letters, rule
}

void MoonHud_DrawBox(Retro::Painter& p, int x, int y, int size, int width) {
    bool full = Moon_Phase() == MoonPhase::Full;
    Retro::Color rule = full ? Retro::Violet : Retro::White;
    if (sNotice > 0 && (sNotice / 6) % 2 == 0) {
        rule = Retro::Violet; // pulse after a change
    }
    int w = width > 0 ? width : MoonHud_BoxWidth(size), h = size + 4;
    p.Window(x, y, w, h, rule);
    p.Moon((int)Moon_Phase(), x + 3, y + 2, size);
    p.Text(x + 4 + size + 4, y + (h - 7) / 2, ShortName(Moon_Phase()), full ? Retro::Violet : Retro::White);
}

static RegisterShipInitFunc initFunc(RegisterMoonHud, { CVAR_CRAWLER_NAME });
