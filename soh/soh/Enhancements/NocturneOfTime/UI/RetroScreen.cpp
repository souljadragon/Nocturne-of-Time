#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include "RetroScreen.h"
#include "RetroUI.h"
#include "../DungeonCrawler.h"
#include "../Battle/BattleScene.h"

#include <memory>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

namespace {

bool sViewportChanged = false;

// Navi floats in front of the first-person camera (and her glow fills the view): she isn't drawn while the
// crawler is active. Her draw function is put back afterwards, if she is still the same actor.
Actor* sHiddenNavi = nullptr;
ActorFunc sNaviDraw = nullptr;

void HideNavi(Player* player) {
    Actor* navi = player != nullptr ? player->naviActor : nullptr;
    if (navi == sHiddenNavi) {
        return;
    }
    if (sHiddenNavi != nullptr) {
        sHiddenNavi->draw = sNaviDraw; // a different Navi now: let the old one go
    }
    sHiddenNavi = navi;
    sNaviDraw = navi != nullptr ? navi->draw : nullptr;
    if (navi != nullptr) {
        navi->draw = nullptr;
    }
}

void ShowNavi(Player* player) {
    if (sHiddenNavi != nullptr && player != nullptr && player->naviActor == sHiddenNavi &&
        sHiddenNavi->draw == nullptr) {
        sHiddenNavi->draw = sNaviDraw;
    }
    sHiddenNavi = nullptr;
    sNaviDraw = nullptr;
}

void SetViewport(PlayState* play, const RetroRect& r) {
    // The renderer stretches viewport coordinates across the whole (wide) window, while the HUD stays in the
    // 4:3 middle: convert the window's x so the 3D lands exactly inside its frame. In 4:3 this is a no-op.
    const int left = Retro::Painter::ScreenLeft(); // <= 0
    const float k = 160.0f / (160.0f - (float)left);
    Viewport vp;
    vp.leftX = (s32)(160.0f + (r.x - 160.0f) * k + 0.5f);
    vp.rightX = (s32)(160.0f + (r.x + r.w - 160.0f) * k + 0.5f);
    vp.topY = r.y;
    vp.bottomY = r.y + r.h;
    View_SetViewport(&play->view, &vp);
}

void RegisterRetroScreen() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;

    // Just before the world is drawn: point the main camera's viewport at the window for this screen
    COND_HOOK(OnPlayDrawBegin, enabled, []() {
        PlayState* play = gPlayState;
        if (play == nullptr) {
            return;
        }
        Player* player = GET_PLAYER(play);
        if (DungeonCrawler_IsActive() && play->pauseCtx.state == 0) {
            SetViewport(play, kView);
            sViewportChanged = true;
            HideNavi(player);
            // No cinematic black bars: they would clip the top and bottom of the view window
            Letterbox_SetSizeTarget(0);
            ShrinkWindow_SetCurrentVal(0);
        } else {
            if (sViewportChanged) {
                SetViewport(play, { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT });
                sViewportChanged = false;
            }
            ShowNavi(player);
        }
    });

    // A new scene brings a new Navi and resets the view
    COND_HOOK(OnSceneInit, enabled, [](int16_t) {
        sHiddenNavi = nullptr;
        sNaviDraw = nullptr;
        sViewportChanged = false;
    });

    if (!enabled && gPlayState != nullptr && sViewportChanged) {
        SetViewport(gPlayState, { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT });
        sViewportChanged = false;
    }
}

} // namespace

void RetroScreen_DrawFrame(Retro::Painter& p, const RetroRect& v) {
    using namespace Retro;
    const int left = Painter::ScreenLeft(), right = Painter::ScreenRight();
    p.Rect(left, 0, right - left, v.y - 2, Black);                                    // above
    p.Rect(left, v.y + v.h + 2, right - left, SCREEN_HEIGHT - (v.y + v.h + 2), Black); // below
    p.Rect(left, v.y - 2, v.x - 2 - left, v.h + 4, Black);                             // left
    p.Rect(v.x + v.w + 2, v.y - 2, right - (v.x + v.w + 2), v.h + 4, Black);           // right
    // white rule just outside the view
    p.Rect(v.x - 2, v.y - 2, v.w + 4, 2, White);
    p.Rect(v.x - 2, v.y + v.h, v.w + 4, 2, White);
    p.Rect(v.x - 2, v.y, 2, v.h, White);
    p.Rect(v.x + v.w, v.y, 2, v.h, White);
}

static RegisterShipInitFunc initFunc(RegisterRetroScreen, { CVAR_CRAWLER_NAME });
