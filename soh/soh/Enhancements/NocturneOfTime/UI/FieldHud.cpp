// Nocturne of Time: the dungeon HUD in the Famicom Megami Tensei style. While the crawler is active it
// replaces OoT's interface (hearts, buttons, minimap): the 3D view sits in a window (see RetroScreen) with the
// moon, automap and location beside it, a message bar under it and party cards along the bottom. Dialogue
// boxes are drawn separately by the game and are unaffected.

#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "RetroUI.h"
#include "RetroScreen.h"
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

const char* SceneTitle(PlayState* play) {
    switch (play->sceneNum) {
        case SCENE_DEKU_TREE:
            return "DEKU TREE";
        default:
            return "DUNGEON";
    }
}

const char* FacingName(signed char facing) {
    // facing 0 looks along +Z, which the automap draws pointing down (south)
    static const char* kNames[4] = { "S", "E", "N", "W" };
    return kNames[facing & 3];
}

// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.
// Layout (320x240): the 3D view top left, a column on the right (moon, map, where you are), a message bar,
// and party cards along the bottom - all on black, as in the Famicom games.
static void DrawFieldHud(PlayState* play) {
    using namespace Retro;
    Party_EnsureCreated(kStartLevel);
    const RetroRect view = kFieldView;
    const int colX = view.x + view.w + 6, colW = SCREEN_WIDTH - 6 - colX; // right column

    OPEN_DISPS(play->state.gfxCtx);
    {
        Painter p(&OVERLAY_DISP);
        RetroScreen_DrawFrame(p, view);
        MoonHud_DrawBox(p, colX, view.y - 2, 20, colW);
    }
    CLOSE_DISPS(play->state.gfxCtx);

    // Map, filling a square box under the moon (or the large map over everything when toggled)
    const int mapY = view.y + 26;
    Automap_Draw(play, colX, mapY, colW);

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);

    // Where you are: place, facing, how close the next encounter is
    {
        int y = mapY + colW + 4, h = view.y + view.h + 2 - y;
        p.Window(colX, y, colW, h);
        p.Text(colX + 6, y + 6, SceneTitle(play), White);
        p.Text(colX + 6, y + 18, "FACING", Green);
        p.Text(colX + 6 + 7 * kCharW, y + 18, FacingName(DungeonCrawler_GetFacing()), Yellow);
        static const char* kDanger[3] = { "CALM", "ALERT", "DANGER" };
        static const Color kDangerColor[3] = { Blue, Yellow, Red };
        int level = Battle_DangerLevel();
        p.Text(colX + 6, y + 30, "◆", kDangerColor[level]);
        p.Text(colX + 18, y + 30, kDanger[level], kDangerColor[level]);
    }

    // Message bar: the Full Moon announcement, otherwise empty like a Famicom message window
    {
        int y = view.y + view.h + 6;
        p.Window(6, y, SCREEN_WIDTH - 12, 18);
        p.Text(14, y + 5, "▶", Green);
        if (const char* note = MoonHud_NoticeText()) {
            p.Text(26, y + 5, note, Violet);
        }
    }

    // Party cards: portrait, name and condition, HP and MP, level
    const std::vector<PartyMember>& members = Party_Members();
    const int cardY = view.y + view.h + 28, cardH = SCREEN_HEIGHT - 4 - cardY;
    const int count = (int)std::min<size_t>(members.size(), 3);
    const int cardW = count > 0 ? (SCREEN_WIDTH - 12 - (count - 1) * 4) / count : 0;
    for (int i = 0; i < count; i++) {
        Combatant c = Party_Effective(members[i]);
        int x = 6 + i * (cardW + 4);
        p.Window(x, cardY, cardW, cardH);
        if (!p.Portrait(c.name, x + 5, cardY + 5, 28)) {
            p.Rect(x + 5, cardY + 5, 28, 28, GreenDark);
        }
        int tx = x + 38;
        p.Text(tx, cardY + 5, Fit(Upper(c.name), 8), White);
        const char* cond = c.ailment == Ailment::Poison ? "PSN" : (c.ailment == Ailment::Burn ? "BRN" : "");
        if (*cond) {
            p.TextRight(x + cardW - 5, cardY + 5, cond, c.ailment == Ailment::Poison ? Violet : Orange);
        }
        char line[24];
        snprintf(line, sizeof(line), "HP %d/%d", c.hp, c.maxHp);
        p.Text(tx, cardY + 15, line, c.hp * 4 <= c.maxHp ? Red : White);
        snprintf(line, sizeof(line), "MP %d/%d", c.mp, c.maxMp);
        p.Text(tx, cardY + 25, line, White);
        if (cardW >= 140) {
            p.TextRight(x + cardW - 5, cardY + 25, "LV" + std::to_string(c.level), Green);
        }
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
