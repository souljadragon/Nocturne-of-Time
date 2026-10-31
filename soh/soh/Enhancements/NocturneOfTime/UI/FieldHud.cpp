// Nocturne of Time: the dungeon HUD in the Famicom Megami Tensei style. While the crawler is active it
// replaces OoT's interface (hearts, buttons, minimap) with the shared Megami Tensei screen (see RetroScreen):
// place and moon on top, the 3D view window with the automap in its corner, the message window and party cards.
// Dialogue boxes are drawn separately by the game and are unaffected.

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
#include "../Boss/BossGate.h"

#include <algorithm>
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
            return "HOLLOWED HUSK";
        case SCENE_DEKU_TREE_BOSS:
            return "GOHMA'S LAIR";
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
// The same screen as battles (RetroScreen): a top bar (place, facing, encounter gem, moon), the 3D view window
// with the automap in its corner, the message window, and party cards along the bottom.
static void DrawFieldHud(PlayState* play) {
    using namespace Retro;
    Party_EnsureCreated(kStartLevel);
    const RetroRect view = kView;

    // The big map (L) is a screen of its own: black, the map in the middle, a title and a key
    if (Automap_IsLarge()) {
        OPEN_DISPS(play->state.gfxCtx);
        {
            Painter p(&OVERLAY_DISP);
            p.FillScreen(Black);
            p.Text(8, 4, std::string(SceneTitle(play)) + " MAP", White);
            p.Text(176, 4, "FACING", Green);
            p.Text(176 + 7 * kCharW, 4, FacingName(DungeonCrawler_GetFacing()), Yellow);
        }
        CLOSE_DISPS(play->state.gfxCtx);
        Automap_Draw(play, 0, 0, 0);
        OPEN_DISPS(play->state.gfxCtx);
        {
            Painter p(&OVERLAY_DISP);
            p.Rect(8, 229, 7, 7, Yellow);
            p.Text(18, 229, "YOU", White);
            p.Rect(56, 229, 7, 7, GreenDark);
            p.Text(66, 229, "FLOOR", White);
            p.Rect(120, 232, 7, 2, Blue);
            p.Text(130, 229, "DOOR", White);
            p.Rect(176, 229, 7, 7, BossGate_IsCleared() ? Green : (BossGate_IsOpen() ? Blue : Violet));
            p.Text(186, 229, "GATE", White);
            p.TextRight(SCREEN_WIDTH - 8, 229, "L:CLOSE", Grey);
            MoonHud_Tick();
        }
        CLOSE_DISPS(play->state.gfxCtx);
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    {
        Painter p(&OVERLAY_DISP);
        RetroScreen_DrawFrame(p, view);

        // Top bar
        const int barY = 8;
        // Long place names push FACING along; boss chambers have no encounters, so no danger meter there
        const char* title = SceneTitle(play);
        int facingX = std::max(96, 8 + (int)Length(title) * kCharW + kCharW);
        p.Text(8, barY, title, White);
        p.Text(facingX, barY, "FACING", Green);
        p.Text(facingX + 7 * kCharW, barY, FacingName(DungeonCrawler_GetFacing()), Yellow);
        if (play->sceneNum != SCENE_DEKU_TREE_BOSS) {
            static const char* kDanger[3] = { "CALM", "ALERT", "DANGER" };
            static const Color kDangerColor[3] = { Blue, Yellow, Red };
            int level = Battle_DangerLevel();
            // after FACING and its letter, clear of the moon box even with DANGER
            int dangerX = std::max(176, facingX + 8 * kCharW + 4);
            p.Text(dangerX, barY, "◆", kDangerColor[level]);
            p.Text(dangerX + 12, barY, kDanger[level], kDangerColor[level]);
        }
        const int moonW = MoonHud_BoxWidth(16);
        MoonHud_DrawBox(p, SCREEN_WIDTH - 6 - moonW, 2, 16);
    }
    CLOSE_DISPS(play->state.gfxCtx);

    // Automap in the view's top-right corner (or the large map over everything when toggled)
    const int mapSize = 62;
    Automap_Draw(play, view.x + view.w - mapSize - 2, view.y + 2, mapSize);

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);

    // Message window: the Full Moon announcement, otherwise empty like a Famicom message window
    p.Window(6, kMessageY, SCREEN_WIDTH - 12, 28);
    p.Text(14, kMessageY + 6, "▶", Green);
    // The boss gate's prompt takes the window while it's up
    if (!BossGate_DrawPrompt(p, 26, kMessageY + 6, SCREEN_WIDTH - 14 - 26)) {
        if (const char* note = MoonHud_NoticeText()) {
            p.Text(26, kMessageY + 6, note, Violet);
        }
    }

    // Party cards: portrait, then name and condition, level, HP and MP
    const std::vector<PartyMember>& members = Party_Members();
    const int cardY = kBottomY, cardH = SCREEN_HEIGHT - 4 - cardY;
    const int count = (int)std::min<size_t>(members.size(), 3);
    const int cardW = count > 0 ? (SCREEN_WIDTH - 12 - (count - 1) * 4) / count : 0;
    for (int i = 0; i < count; i++) {
        Combatant c = Party_Effective(members[i]);
        int x = 6 + i * (cardW + 4);
        p.Window(x, cardY, cardW, cardH);
        int portrait = cardH - 12;
        if (!p.Portrait(c.name, x + 6, cardY + 6, portrait)) {
            p.Rect(x + 6, cardY + 6, portrait, portrait, GreenDark);
        }
        int tx = x + 12 + portrait;
        p.Text(tx, cardY + 6, Fit(Upper(c.name), 8), White);
        const char* cond = c.ailment == Ailment::Poison ? "PSN" : (c.ailment == Ailment::Burn ? "BRN" : "GOOD");
        p.TextRight(x + cardW - 6, cardY + 6, cond,
                    c.ailment == Ailment::Poison ? Violet : (c.ailment == Ailment::Burn ? Orange : Green));
        // Four lines, 10px apart: name + condition, LV, HP, MP
        char line[24];
        snprintf(line, sizeof(line), "LV %d", c.level);
        p.Text(tx, cardY + 16, line, Green);
        snprintf(line, sizeof(line), "HP %d/%d", c.hp, c.maxHp);
        p.Text(tx, cardY + 26, line, c.hp * 4 <= c.maxHp ? Red : White);
        snprintf(line, sizeof(line), "MP %d/%d", c.mp, c.maxMp);
        p.Text(tx, cardY + 36, line, White);
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
