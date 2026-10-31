#include "BossGate.h"
#include "../UI/RetroUI.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include <math.h>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
}

namespace {

constexpr f32 kTileSize = 80.0f; // the crawler's grid
constexpr s16 kWebFloor = 0;     // BgYdanSp params after init: WEB_FLOOR

int sWins = 0;
bool sCleared = false;

// Where the gate is: the lobby's floor web, found when the dungeon loads. Remembered in the save in case the
// web gets broken (jumping onto it from high up, as in the vanilla dungeon).
bool sHaveSaved = false;
Vec3f sSavedPos = { 0.0f, 0.0f, 0.0f };
bool sFound = false; // this visit
Vec3f sGatePos = { 0.0f, 0.0f, 0.0f };
int sSearchFrames = 0;
bool sHaveEntryPos = false; // last resort: where Link arrived
Vec3f sEntryPos = { 0.0f, 0.0f, 0.0f };

enum class Prompt { None, Sealed, Ask, Cleared };
bool sRematch = false; // set by the cleared gate's warp, until the next return from the chamber
Prompt sPrompt = Prompt::None;
bool sStillOnGate = false; // after a prompt, stepping off and back on asks again

Vec3f SnapToTile(const Vec3f& p) {
    return { roundf(p.x / kTileSize) * kTileSize, p.y, roundf(p.z / kTileSize) * kTileSize };
}

void FindGate(PlayState* play, Player* player) {
    if (sFound) {
        return;
    }
    if (!sHaveEntryPos) {
        sHaveEntryPos = true;
        sEntryPos = SnapToTile(player->actor.world.pos);
    }
    // The highest floor web in the dungeon: the one on the lobby's ground floor. (A web that hasn't run its
    // init yet still has its spawn params, with the type in the top four bits.)
    Actor* best = NULL;
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
            if (a->id != ACTOR_BG_YDAN_SP) {
                continue;
            }
            s16 type = a->init != NULL ? ((a->params >> 12) & 0xF) : a->params;
            if (type == kWebFloor && (best == NULL || a->world.pos.y > best->world.pos.y)) {
                best = a;
            }
        }
    }
    if (best != NULL) {
        sFound = true;
        sGatePos = SnapToTile(best->world.pos);
        sSavedPos = sGatePos;
        sHaveSaved = true;
        return;
    }
    // No web (broken, or the lobby isn't loaded yet): give it a moment, then use what we know
    if (++sSearchFrames > 30) {
        sFound = true;
        sGatePos = sHaveSaved ? sSavedPos : sEntryPos;
    }
}

bool OnGate(const Vec3f& pos) {
    return sFound && fabsf(pos.x - sGatePos.x) < 24.0f && fabsf(pos.z - sGatePos.z) < 24.0f &&
           fabsf(pos.y - sGatePos.y) < 60.0f;
}

void Warp(PlayState* play, s16 entrance) {
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
}

void RegisterBossGate() {
    // Each visit looks for the web again
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        sFound = false;
        sSearchFrames = 0;
        sHaveEntryPos = false;
        sPrompt = Prompt::None;
        sStillOnGate = false;
    });
}

} // namespace

int BossGate_Wins() {
    return sWins;
}

bool BossGate_IsOpen() {
    return sWins >= kBossGateWins;
}

bool BossGate_IsCleared() {
    return sCleared;
}

void BossGate_Reset() {
    sWins = 0;
    sCleared = false;
    sHaveSaved = false;
    sFound = false;
    sPrompt = Prompt::None;
}

nlohmann::json BossGate_Save() {
    nlohmann::json j = { { "wins", sWins }, { "cleared", sCleared } };
    if (sHaveSaved) {
        j["web"] = { sSavedPos.x, sSavedPos.y, sSavedPos.z };
    }
    return j;
}

void BossGate_Load(const nlohmann::json& j) {
    BossGate_Reset();
    if (!j.is_object()) {
        return;
    }
    if (j.contains("wins") && j["wins"].is_number_integer()) {
        sWins = std::max(0, j["wins"].get<int>());
    }
    if (j.contains("cleared") && j["cleared"].is_boolean()) {
        sCleared = j["cleared"].get<bool>();
    }
    if (j.contains("web") && j["web"].is_array() && j["web"].size() == 3 && j["web"][0].is_number()) {
        sSavedPos = { j["web"][0].get<float>(), j["web"][1].get<float>(), j["web"][2].get<float>() };
        sHaveSaved = true;
    }
}

std::string BossGate_OnVictory(PlayState* play) {
    if (play == NULL || play->sceneNum != SCENE_DEKU_TREE || sCleared) {
        return "";
    }
    if (sWins >= kBossGateWins) {
        return "";
    }
    sWins++;
    if (sWins == kBossGateWins) {
        return "The great web glows! The way down is open.";
    }
    return "The great web stirs... (" + std::to_string(sWins) + "/" + std::to_string(kBossGateWins) + ")";
}

void BossGate_SetCleared() {
    sCleared = true;
}

void BossGate_Tick(PlayState* play, Player* player) {
    if (play->sceneNum != SCENE_DEKU_TREE) {
        return;
    }
    FindGate(play, player);
    if (!OnGate(player->actor.world.pos)) {
        sStillOnGate = false;
    }
    if (!sFound || (play->gameplayFrames % 3) != 0) {
        return;
    }
    // Sparkles rising from the gate: dim violet while sealed, bright blue when open, green once cleared
    Color_RGBA8 prim, env;
    if (sCleared) {
        prim = { 200, 255, 200, 255 };
        env = { 0, 200, 0, 0 };
    } else if (BossGate_IsOpen()) {
        prim = { 220, 240, 255, 255 };
        env = { 60, 120, 255, 0 };
    } else {
        if ((play->gameplayFrames % 9) != 0) {
            return; // fewer while sealed
        }
        prim = { 200, 160, 255, 255 };
        env = { 110, 40, 160, 0 };
    }
    Vec3f pos = { sGatePos.x + Rand_CenteredFloat(70.0f), sGatePos.y + 4.0f, sGatePos.z + Rand_CenteredFloat(70.0f) };
    Vec3f vel = { 0.0f, 1.2f + Rand_ZeroOne() * 1.2f, 0.0f };
    Vec3f accel = { 0.0f, 0.02f, 0.0f };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 1200, 30);
}

bool BossGate_OnStep(PlayState* play, Player* player) {
    if (play->sceneNum != SCENE_DEKU_TREE || !OnGate(player->actor.world.pos) || sStillOnGate ||
        play->transitionTrigger != TRANS_TRIGGER_OFF) {
        return false;
    }
    sStillOnGate = true;
    sPrompt = sCleared ? Prompt::Cleared : (BossGate_IsOpen() ? Prompt::Ask : Prompt::Sealed);
    Sfx_PlaySfxCentered(sPrompt == Prompt::Ask ? NA_SE_SY_FSEL_DECIDE_L : NA_SE_SY_MESSAGE_PASS);
    return true;
}

bool BossGate_PromptOpen() {
    return sPrompt != Prompt::None;
}

void BossGate_Update(PlayState* play, void* inputPtr) {
    Input* input = (Input*)inputPtr;
    u16 press = input->press.button;
    if (sPrompt == Prompt::Ask || sPrompt == Prompt::Cleared) {
        if (press & BTN_A) {
            sRematch = sPrompt == Prompt::Cleared;
            sPrompt = Prompt::None;
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            BossGate_WarpToBoss(play);
        } else if (press & BTN_B) {
            sPrompt = Prompt::None;
            Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
        }
    } else if (press & (BTN_A | BTN_B)) {
        sPrompt = Prompt::None;
        Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
    }
}

static unsigned sBlink = 0;

bool BossGate_DrawPrompt(Retro::Painter& p, int x, int y, int w) {
    using namespace Retro;
    if (sPrompt == Prompt::None) {
        return false;
    }
    std::string line1, line2, keys;
    Color c = White;
    switch (sPrompt) {
        case Prompt::Ask:
            line1 = "The great web glows. Descend to";
            line2 = "the Heart of the Tree?";
            keys = "A:YES B:NO";
            c = Blue;
            break;
        case Prompt::Sealed: {
            int left = kBossGateWins - sWins;
            line1 = "A strange force seals the web.";
            line2 = "Win " + std::to_string(left) + " more battle" + (left == 1 ? "." : "s.");
            keys = ""; // just waits for A: the blinking marker
            c = Violet;
            break;
        }
        case Prompt::Cleared:
        default:
            line1 = "The Heart of the Tree is at peace.";
            line2 = "Face Queen Gohma again?";
            keys = "A:YES B:NO";
            c = Green;
            break;
    }
    p.Text(x, y, line1, c);
    p.Text(x, y + kLineH, line2, c);
    if (!keys.empty()) {
        p.TextRight(x + w, y + kLineH, keys, Yellow);
    } else if ((sBlink++ / 4) % 2 == 0) {
        p.TextRight(x + w, y + kLineH, "▼", Yellow); // press A
    }
    return true;
}

bool BossGate_Position(PlayState* play, float& x, float& y, float& z) {
    if (play->sceneNum != SCENE_DEKU_TREE || !sFound) {
        return false;
    }
    x = sGatePos.x;
    y = sGatePos.y;
    z = sGatePos.z;
    return true;
}

bool BossGate_InBossRoom(PlayState* play) {
    return play != NULL && play->sceneNum == SCENE_DEKU_TREE_BOSS;
}

void BossGate_WarpToBoss(PlayState* play) {
    Warp(play, ENTR_DEKU_TREE_BOSS_ENTRANCE);
}

bool BossGate_IsRematch() {
    return sRematch;
}

void BossGate_ReturnToDungeon(PlayState* play) {
    sRematch = false;
    Warp(play, ENTR_DEKU_TREE_ENTRANCE);
}

static RegisterShipInitFunc initFunc(RegisterBossGate, {});
