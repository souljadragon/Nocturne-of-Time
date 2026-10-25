// Nocturne of Time: the dungeon HUD in the Famicom Megami Tensei style. While the crawler is active it
// replaces OoT's interface (hearts, buttons, minimap): the moon window and automap in the upper right, party
// cards with portraits along the bottom, and a message window when the moon changes. Dialogue boxes are drawn
// separately by the game and are unaffected.

#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "RetroUI.h"
#include "../Automap.h"
#include "../DungeonCrawler.h"
#include "../Party.h"
#include "../Battle/BattleScene.h"
#include "../Loot/EquipMenu.h"
#include "../Moon/MoonHud.h"
#include "../Moon/Moon.h"

#include <memory>
#include <string>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
int16_t OTRGetRectDimensionFromLeftEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

using namespace Nocturne;

namespace {

constexpr int kStartLevel = 5; // matches the battle scene, if the HUD shows before any battle

std::string Upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
    }
    return s;
}

} // namespace

// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.
static void DrawFieldHud(PlayState* play) {
    using namespace Retro;
    Party_EnsureCreated(kStartLevel);
    const int left = OTRGetRectDimensionFromLeftEdge(10.0f);
    const int right = OTRGetRectDimensionFromRightEdge((f32)(SCREEN_WIDTH - 10));

    // The automap draws itself (corner window, or the large map when L is held down)
    Automap_Draw(play);

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);

    // Moon window, top right
    const int moonSize = 20;
    MoonHud_DrawBox(p, right - MoonHud_BoxWidth(moonSize), 8, moonSize);

    // A word about the moon when it changes, top left
    if (const char* note = MoonHud_NoticeText()) {
        std::string text = note;
        int w = 28 + (int)Length(text) * kCharW;
        p.Window(left, 8, w, 18, (Moon_Phase() == MoonPhase::Full) ? Violet : White);
        p.Text(left + 8, 13, "▶", Green);
        p.Text(left + 20, 13, text, (Moon_Phase() == MoonPhase::Full) ? Violet : White);
    }

    // Party cards along the bottom: portrait, name and condition, HP, MP
    const std::vector<PartyMember>& members = Party_Members();
    const int cardW = 124, cardH = 38, cardY = SCREEN_HEIGHT - 10 - cardH;
    for (size_t i = 0; i < members.size() && i < 3; i++) {
        Combatant c = Party_Effective(members[i]);
        int x = left + (int)i * (cardW + 4);
        p.Window(x, cardY, cardW, cardH);
        if (!p.Portrait(c.name, x + 5, cardY + 5, 28)) {
            p.Rect(x + 5, cardY + 5, 28, 28, GreenDark);
        }
        int tx = x + 38;
        p.Text(tx, cardY + 5, Fit(Upper(c.name), 6), White);
        const char* cond = c.ailment == Ailment::Poison ? "PSN" : (c.ailment == Ailment::Burn ? "BRN" : "");
        if (*cond) {
            p.TextRight(x + cardW - 5, cardY + 5, cond, c.ailment == Ailment::Poison ? Violet : Orange);
        }
        char line[24];
        snprintf(line, sizeof(line), "HP%4d", c.hp);
        p.Text(tx, cardY + 15, line, c.hp * 4 <= c.maxHp ? Red : White);
        snprintf(line, sizeof(line), "MP%4d", c.mp);
        p.Text(tx, cardY + 25, line, White);
        p.TextRight(x + cardW - 5, cardY + 25, "LV" + std::to_string(c.level), Green);
    }

    MoonHud_Tick();
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

void RegisterFieldHud() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;
    COND_VB_SHOULD(VB_DRAW_INTERFACE, enabled, {
        PlayState* play = va_arg(args, PlayState*);
        // Battles and the party menu draw their own screens; the pause menu keeps OoT's HUD
        if (DungeonCrawler_IsActive() && !Battle_IsActive() && !EquipMenu_IsOpen() && play->pauseCtx.state == 0) {
            DrawFieldHud(play);
            *should = false;
        }
    });
}

} // namespace

static RegisterShipInitFunc initFunc(RegisterFieldHud, { CVAR_CRAWLER_NAME });
