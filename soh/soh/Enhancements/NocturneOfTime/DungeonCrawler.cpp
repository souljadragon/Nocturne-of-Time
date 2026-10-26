// Nocturne of Time: first-person, grid-based dungeon-crawler movement.
//
// Inside enabled dungeons Link moves one tile per press and turns in 90-degree steps, and the main camera
// sits at his eyes. Vanilla control takes over whenever Link is busy (doors, cutscenes, talking, ladders,
// swimming, aiming an item...) and the crawler re-snaps when he is free again.
//
// Controls (stick or D-pad):
//   up / down       step forward / back one tile
//   left / right    turn 90 degrees
//   Z + left/right  strafe one tile
//   right stick     look up / down (springs back)
//   A, B, items     unchanged (open doors, talk, swing sword...)

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "DungeonCrawler.h"
#include "Automap.h"
#include "Battle/BattleScene.h"
#include "Loot/EquipMenu.h"
#include "Boss/BossGate.h"

#include <math.h>

extern "C" {
#include "global.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

namespace {

// ---- Tuning ---------------------------------------------------------------------------------------------
constexpr f32 kTileSize = 80.0f;      // world units per grid step
constexpr s16 kStepFrames = 6;        // game frames (20 per second) to walk one tile
constexpr s16 kTurnFrames = 5;        // game frames to turn 90 degrees
constexpr s16 kBumpFrames = 6;        // game frames for the "hit a wall" nudge
constexpr f32 kBumpDistance = 6.0f;   // how far the view nudges into a wall
constexpr s8 kStickThreshold = 45;    // stick deflection that counts as a press
constexpr f32 kWallClearance = 26.0f; // keep Link this far from walls at the end of a step
constexpr f32 kMaxStepUp = 30.0f;     // tallest ledge a step can climb
constexpr f32 kEyeHeightAdult = 58.0f;
constexpr f32 kEyeHeightChild = 40.0f;
constexpr f32 kFov = 70.0f;
constexpr s16 kMaxPitch = 0x1C00; // about 40 degrees
constexpr f32 kPi = 3.14159265f;
constexpr f32 kMinApproach = 8.0f;     // shortest partial step worth taking toward an obstacle
constexpr f32 kChestFrontDist = 30.0f; // where Link stands in front of a chest (it opens from 0-50 units, centred)
constexpr f32 kDropAssist = 60.0f;     // drops deeper than this are walked off by the game (ladder tops, jumps)
constexpr s16 kAssistFrames = 15;      // longest the game drives Link during an assist

// Flags that mean Link is doing something the crawler must not interfere with.
constexpr u32 kBusyFlags1 = PLAYER_STATE1_LOADING | PLAYER_STATE1_SWINGING_BOTTLE | PLAYER_STATE1_HOOKSHOT_FALLING |
                            PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD |
                            PLAYER_STATE1_READY_TO_FIRE | PLAYER_STATE1_GETTING_ITEM |
                            PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_HANGING_OFF_LEDGE |
                            PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_FIRST_PERSON |
                            PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_ON_HORSE |
                            PLAYER_STATE1_USING_BOOMERANG | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_ITEM_CS |
                            PLAYER_STATE1_IN_CUTSCENE;
constexpr u32 kBusyFlags2 = PLAYER_STATE2_CRAWLING;
// While airborne the crawler keeps the view but won't start new moves.
constexpr u32 kAirborneFlags1 = PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL;

// Assist: for a few frames the game itself walks Link forward, so vanilla behaviour that needs real
// movement can start (grabbing vines/ladders, climbing down from a ladder top, walking off a ledge).
enum class Action { None, Step, Turn, Bump, Assist };

struct CrawlerState {
    bool active = false; // the crawler owns Link and the camera this frame
    s8 facing = 0;       // quarter turns; yaw = facing * 0x4000
    s16 curYaw = 0;
    s16 pitch = 0;
    Action action = Action::None;
    s16 timer = 0;
    Vec3f gridOrigin = { 0.0f, 0.0f, 0.0f };
    Vec3f from = { 0.0f, 0.0f, 0.0f };
    Vec3f to = { 0.0f, 0.0f, 0.0f };
    s16 stepYawFrom = 0; // a step can also swing the view (e.g. to face a chest)
    s16 stepYawTo = 0;
    s16 yawFrom = 0;
    s16 yawTo = 0;
    Vec3f bumpDir = { 0.0f, 0.0f, 0.0f };
    f32 bumpAmount = 0.0f;
    bool needCentre = false; // centre Link in the corridor once he's on the ground (see CentreInCorridor)
};

CrawlerState sCrawler;

s16 FacingToYaw(s8 facing) {
    return (s16)((facing & 3) * 0x4000);
}

s8 YawToFacing(s16 yaw) {
    // Round to the nearest quarter turn
    return (s8)((((u16)yaw + 0x2000) >> 14) & 3);
}

Vec3f YawToDir(s16 yaw) {
    return { Math_SinS(yaw), 0.0f, Math_CosS(yaw) };
}

f32 SmoothStep(f32 t) {
    return t * t * (3.0f - 2.0f * t);
}

bool SceneAllowed(PlayState* play) {
    // The vertical slice starts in the Deku Tree (and Queen Gohma's chamber below it); more dungeons get
    // added as they are remixed.
    return play->sceneNum == SCENE_DEKU_TREE || play->sceneNum == SCENE_DEKU_TREE_BOSS;
}

bool PlayerBusy(Player* player) {
    return (player->stateFlags1 & kBusyFlags1) || (player->stateFlags2 & kBusyFlags2);
}

// Snap a world XZ position to the nearest tile centre of the current grid.
Vec3f SnapToGrid(const Vec3f& pos) {
    Vec3f out = pos;
    out.x = sCrawler.gridOrigin.x + roundf((pos.x - sCrawler.gridOrigin.x) / kTileSize) * kTileSize;
    out.z = sCrawler.gridOrigin.z + roundf((pos.z - sCrawler.gridOrigin.z) / kTileSize) * kTileSize;
    return out;
}

struct WallHit {
    bool hit = false;
    f32 dist = 0.0f; // along the step direction
    CollisionPoly* poly = NULL;
    s32 bgId = BGCHECK_SCENE;
};

// Nearest wall (scene or actor collision) along dir within length, tested at knee and chest height.
WallHit ProbeWalls(PlayState* play, const Vec3f& from, const Vec3f& dir, f32 length) {
    static const f32 kProbeHeights[] = { 20.0f, 45.0f };
    WallHit best;
    best.dist = length;

    for (f32 h : kProbeHeights) {
        Vec3f a = { from.x, from.y + h, from.z };
        Vec3f b = { from.x + dir.x * length, from.y + h, from.z + dir.z * length };
        Vec3f hitPos;
        CollisionPoly* poly;
        s32 bgId;
        if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hitPos, &poly, true, false, false, true, &bgId)) {
            f32 d = (hitPos.x - a.x) * dir.x + (hitPos.z - a.z) * dir.z;
            if (!best.hit || d < best.dist) {
                best.hit = true;
                best.dist = d;
                best.poly = poly;
                best.bgId = bgId;
            }
        }
    }
    return best;
}

// Floor height under `to`, probing from just above the tallest climbable ledge. BGCHECK_Y_MIN = void.
f32 FloorAt(PlayState* play, const Vec3f& from, const Vec3f& to) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe = { to.x, from.y + kMaxStepUp + 5.0f, to.z };
    return BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
}

f32 DistXZ(const Vec3f& a, const Vec3f& b) {
    f32 dx = b.x - a.x;
    f32 dz = b.z - a.z;
    return sqrtf(dx * dx + dz * dz);
}

void StartTurn(s16 yawTo) {
    sCrawler.action = Action::Turn;
    sCrawler.timer = 0;
    sCrawler.yawFrom = sCrawler.curYaw;
    sCrawler.yawTo = yawTo;
}

void StartBump(Player* player, const Vec3f& dir) {
    sCrawler.action = Action::Bump;
    sCrawler.timer = 0;
    sCrawler.bumpDir = dir;
    Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
}

void BeginStep(const Vec3f& from, const Vec3f& to, s16 endYaw) {
    sCrawler.action = Action::Step;
    sCrawler.timer = 0;
    sCrawler.from = from;
    sCrawler.to = to;
    sCrawler.stepYawFrom = sCrawler.curYaw;
    sCrawler.stepYawTo = endYaw;
}

void StartAssist() {
    sCrawler.action = Action::Assist;
    sCrawler.timer = 0;
}

void StartStep(PlayState* play, Player* player, s8 dirFacing) {
    bool forward = ((dirFacing - sCrawler.facing) & 3) == 0;
    s16 gridYaw = FacingToYaw(sCrawler.facing);
    Vec3f dir = YawToDir(FacingToYaw(dirFacing));
    Vec3f from = player->actor.world.pos;
    Vec3f to = SnapToGrid({ from.x + dir.x * kTileSize, from.y, from.z + dir.z * kTileSize });

    WallHit wall = ProbeWalls(play, from, dir, DistXZ(from, to) + kWallClearance);
    if (wall.hit) {
        u32 wallFlags = SurfaceType_GetWallFlags(&play->colCtx, wall.poly, wall.bgId);

        // Vines and ladders: let the game walk Link into them so he grabs on
        if (forward && (wallFlags & (WALL_FLAG_CLIMBABLE | WALL_FLAG_LADDER))) {
            StartAssist();
            return;
        }

        // Chests only open from just in front of them and centred, so line Link up with the front
        if (forward && wall.bgId != BGCHECK_SCENE) {
            DynaPolyActor* dyna = DynaPoly_GetActor(&play->colCtx, wall.bgId);
            if (dyna != NULL && dyna->actor.id == ACTOR_EN_BOX) {
                Actor* chest = &dyna->actor;
                s16 rot = chest->shape.rot.y;
                Vec3f front = { chest->world.pos.x - Math_SinS(rot) * kChestFrontDist, from.y,
                                chest->world.pos.z - Math_CosS(rot) * kChestFrontDist };
                BeginStep(from, front, Math_Vec3f_Yaw(&front, &chest->world.pos));
                return;
            }
        }

        // Anything else: walk up to it rather than stopping a whole tile short
        f32 approach = wall.dist - kWallClearance;
        if (approach < kMinApproach) {
            StartBump(player, dir);
            return;
        }
        to = { from.x + dir.x * approach, from.y, from.z + dir.z * approach };
    }

    f32 floorY = FloorAt(play, from, to);
    if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY > from.y + kMaxStepUp) {
        // Void, or a ledge too tall to step onto
        StartBump(player, dir);
        return;
    }
    if (forward && floorY < from.y - kDropAssist) {
        // A real drop: let the game walk Link off so ladder tops and ledge jumps behave normally
        StartAssist();
        return;
    }

    BeginStep(from, to, gridYaw);
}

void UpdateAction(Player* player) {
    sCrawler.bumpAmount = 0.0f;

    switch (sCrawler.action) {
        case Action::None:
            break;

        case Action::Step: {
            sCrawler.timer++;
            f32 t = SmoothStep(CLAMP_MAX((f32)sCrawler.timer / kStepFrames, 1.0f));
            player->actor.world.pos.x = sCrawler.from.x + (sCrawler.to.x - sCrawler.from.x) * t;
            player->actor.world.pos.z = sCrawler.from.z + (sCrawler.to.z - sCrawler.from.z) * t;
            s16 yawDiff = (s16)(sCrawler.stepYawTo - sCrawler.stepYawFrom);
            sCrawler.curYaw = (s16)(sCrawler.stepYawFrom + (s16)(yawDiff * t));
            if (sCrawler.timer >= kStepFrames) {
                sCrawler.curYaw = sCrawler.stepYawTo;
                Player_PlaySfx(&player->actor, NA_SE_PL_WALK_GROUND + player->floorSfxOffset);
                sCrawler.action = Action::None;
                // Onto the boss gate: its prompt opens instead of an encounter roll
                if (gPlayState != NULL && !BossGate_OnStep(gPlayState, player)) {
                    Battle_OnStep(gPlayState, player); // may start a battle
                }
            }
            break;
        }

        case Action::Assist: {
            // The game is moving Link; follow his facing, and hand back once he's airborne or time is up.
            // (If he starts climbing he becomes "busy" and the crawler steps aside on its own.)
            sCrawler.timer++;
            sCrawler.curYaw = player->actor.shape.rot.y;
            bool airborne = !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
            if (airborne || sCrawler.timer >= kAssistFrames) {
                StartTurn(FacingToYaw(sCrawler.facing));
            }
            break;
        }

        case Action::Turn: {
            sCrawler.timer++;
            f32 t = SmoothStep(CLAMP_MAX((f32)sCrawler.timer / kTurnFrames, 1.0f));
            s16 diff = (s16)(sCrawler.yawTo - sCrawler.yawFrom);
            sCrawler.curYaw = (s16)(sCrawler.yawFrom + (s16)(diff * t));
            if (sCrawler.timer >= kTurnFrames) {
                sCrawler.curYaw = sCrawler.yawTo;
                sCrawler.action = Action::None;
            }
            break;
        }

        case Action::Bump: {
            sCrawler.timer++;
            // Out and back: a sine bump over kBumpFrames
            f32 t = (f32)sCrawler.timer / kBumpFrames;
            sCrawler.bumpAmount = sinf(t * kPi) * kBumpDistance;
            if (sCrawler.timer >= kBumpFrames) {
                sCrawler.bumpAmount = 0.0f;
                sCrawler.action = Action::None;
            }
            break;
        }
    }
}

void Activate(Player* player) {
    sCrawler.active = true;
    sCrawler.action = Action::None;
    sCrawler.pitch = 0;
    // Steps land on the automap's fixed world tiles (centres at multiples of the tile size)
    sCrawler.gridOrigin = { roundf(player->actor.world.pos.x / kTileSize) * kTileSize, 0.0f,
                            roundf(player->actor.world.pos.z / kTileSize) * kTileSize };
    sCrawler.facing = YawToFacing(player->actor.shape.rot.y);
    sCrawler.curYaw = player->actor.shape.rot.y;
    // Gohma's lair isn't laid out on the 80-unit grid: line the grid up with its corridors instead
    sCrawler.needCentre = gPlayState != NULL && gPlayState->sceneNum == SCENE_DEKU_TREE_BOSS;
    // Ease from wherever Link was facing onto the grid
    StartTurn(FacingToYaw(sCrawler.facing));
    player->actor.shape.shadowDraw = NULL;
}

// Between two walls (a corridor), move Link to the middle and run the grid through that line, so steps stay
// centred. Only along an axis with walls close on both sides; open floor keeps the usual grid.
void CentreInCorridor(PlayState* play, Player* player) {
    const f32 kReach = 200.0f;
    Vec3f pos = player->actor.world.pos;
    for (int axis = 0; axis < 2; axis++) {
        Vec3f plus = axis == 0 ? Vec3f{ 1.0f, 0.0f, 0.0f } : Vec3f{ 0.0f, 0.0f, 1.0f };
        Vec3f minus = { -plus.x, 0.0f, -plus.z };
        WallHit a = ProbeWalls(play, pos, plus, kReach);
        WallHit b = ProbeWalls(play, pos, minus, kReach);
        if (!a.hit || !b.hit) {
            continue;
        }
        f32 shift = (a.dist - b.dist) * 0.5f;
        if (axis == 0) {
            pos.x += shift;
            sCrawler.gridOrigin.x = pos.x;
        } else {
            pos.z += shift;
            sCrawler.gridOrigin.z = pos.z;
        }
    }
    player->actor.world.pos.x = pos.x;
    player->actor.world.pos.z = pos.z;
}

void Deactivate(Player* player) {
    if (!sCrawler.active) {
        return;
    }
    sCrawler.active = false;
    sCrawler.action = Action::None;
    sCrawler.bumpAmount = 0.0f;
    Battle_Abort();
    EquipMenu_Close();
    if (player != NULL) {
        player->actor.shape.shadowDraw = ActorShadow_DrawFeet;
    }
}

void ClearMovementInput(Input* input) {
    const u16 kEatenButtons = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_Z | BTN_L | BTN_R;
    for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
        pad->stick_x = 0;
        pad->stick_y = 0;
        pad->button &= ~kEatenButtons;
    }
}

// Returns false to skip Link's own update this frame (during battles).
bool OnFilterInput(Player* player, Input* input) {
    PlayState* play = gPlayState;
    if (play == NULL || !SceneAllowed(play) || PlayerBusy(player)) {
        Deactivate(player);
        return true;
    }
    if (!sCrawler.active) {
        Activate(player);
    }

    BossGate_Tick(play, player);

    // R opens the party / equipment menu (not during a battle)
    if (!Battle_IsActive() && !EquipMenu_IsOpen() && !BossGate_PromptOpen() && (input->press.button & BTN_R) &&
        sCrawler.action == Action::None) {
        EquipMenu_Open();
        input->press.button &= ~BTN_R;
    }

    if (Battle_IsActive() || EquipMenu_IsOpen() || BossGate_PromptOpen()) {
        // The battle, menu or gate prompt reads the controller; Link himself does nothing but finish turning to
        // face the enemies
        if (Battle_IsActive()) {
            Battle_Update(play, player, input);
        } else if (EquipMenu_IsOpen()) {
            EquipMenu_Update(input);
        } else {
            BossGate_Update(play, input);
        }
        ClearMovementInput(input);
        for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
            pad->button &= ~(BTN_A | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT | BTN_CUP | BTN_R);
        }
        if (sCrawler.action == Action::Turn) {
            UpdateAction(player);
        }
        player->linearVelocity = 0.0f;
        player->actor.speedXZ = 0.0f;
        player->yaw = sCrawler.curYaw;
        player->actor.world.rot.y = sCrawler.curYaw;
        player->actor.shape.rot.y = sCrawler.curYaw;
        return false; // Link stays frozen: no idle animation, fidgets or their sounds
    }

    const OSContPad& cur = input->cur;
    bool up = cur.stick_y > kStickThreshold || (cur.button & BTN_DUP);
    bool down = cur.stick_y < -kStickThreshold || (cur.button & BTN_DDOWN);
    bool left = cur.stick_x < -kStickThreshold || (cur.button & BTN_DLEFT);
    bool right = cur.stick_x > kStickThreshold || (cur.button & BTN_DRIGHT);
    bool strafe = cur.button & BTN_Z;

    if (input->press.button & BTN_L) {
        Automap_ToggleLarge();
    }
    Automap_Update(play, player);
    Battle_FieldTick(play, player); // boss rooms start their fight

    // Look up/down with the right stick; springs back to level when released
    s16 targetPitch = (s16)CLAMP((s32)cur.right_stick_y * kMaxPitch / 80, -kMaxPitch, kMaxPitch);
    sCrawler.pitch += (s16)((targetPitch - sCrawler.pitch) / 3);

    ClearMovementInput(input);

    bool airborne = (player->stateFlags1 & kAirborneFlags1) || !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
    if (sCrawler.needCentre && !airborne && sCrawler.action == Action::None) {
        sCrawler.needCentre = false;
        CentreInCorridor(play, player);
    }

    // Holding a direction keeps walking/turning, one tile or quarter turn at a time
    if (sCrawler.action == Action::None && !airborne) {
        // Facing + 1 is a quarter turn to the left (yaw grows counter-clockwise seen from above)
        if (up) {
            StartStep(play, player, sCrawler.facing);
        } else if (down) {
            StartStep(play, player, sCrawler.facing + 2);
        } else if (left && strafe) {
            StartStep(play, player, sCrawler.facing + 1);
        } else if (right && strafe) {
            StartStep(play, player, sCrawler.facing + 3);
        } else if (left) {
            sCrawler.facing = (sCrawler.facing + 1) & 3;
            StartTurn(FacingToYaw(sCrawler.facing));
        } else if (right) {
            sCrawler.facing = (sCrawler.facing + 3) & 3;
            StartTurn(FacingToYaw(sCrawler.facing));
        }
    }

    if (sCrawler.action == Action::Assist) {
        // Hold the stick straight ahead (the camera faces Link's way, so "up" is forward) and let the game
        // move Link: this is what makes him grab vines and ladders or climb down from a ladder top.
        for (OSContPad* pad : { &input->cur, &input->rel }) {
            pad->stick_x = 0;
            pad->stick_y = 80;
        }
        UpdateAction(player);
        return true;
    }

    UpdateAction(player);

    // Link stands still as far as vanilla physics is concerned; the crawler owns his XZ position and facing.
    player->linearVelocity = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->yaw = sCrawler.curYaw;
    player->actor.world.rot.y = sCrawler.curYaw;
    player->actor.shape.rot.y = sCrawler.curYaw;
    return true;
}

bool OnCameraUpdate(Camera* camera) {
    if (!sCrawler.active || camera->thisIdx != CAM_ID_MAIN || camera->player == NULL) {
        return true;
    }

    Player* player = camera->player;
    Vec3f fwd = YawToDir(sCrawler.curYaw);
    f32 eyeHeight = LINK_IS_ADULT ? kEyeHeightAdult : kEyeHeightChild;

    Vec3f eye = player->actor.world.pos;
    eye.y += eyeHeight;
    eye.x += sCrawler.bumpDir.x * sCrawler.bumpAmount;
    eye.z += sCrawler.bumpDir.z * sCrawler.bumpAmount;

    f32 cosPitch = Math_CosS(sCrawler.pitch);
    Vec3f at = {
        eye.x + fwd.x * cosPitch * 100.0f,
        eye.y + Math_SinS(sCrawler.pitch) * 100.0f,
        eye.z + fwd.z * cosPitch * 100.0f,
    };

    camera->eye = eye;
    camera->eyeNext = eye;
    camera->at = at;
    camera->fov = kFov;
    return false;
}

void RegisterDungeonCrawler() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;

    if (!enabled && gPlayState != NULL) {
        Deactivate(GET_PLAYER(gPlayState));
    }

    COND_VB_SHOULD(VB_PLAYER_FILTER_INPUT, enabled, {
        Player* player = va_arg(args, Player*);
        Input* input = va_arg(args, Input*);
        *should = OnFilterInput(player, input);
    });

    COND_VB_SHOULD(VB_CAMERA_RUN_SETTING_FUNC, enabled, {
        Camera* camera = va_arg(args, Camera*);
        *should = OnCameraUpdate(camera);
    });

    // Hide Link's body so it doesn't block the first-person view
    COND_VB_SHOULD(VB_PLAYER_OVERRIDE_LIMB_DRAW, enabled, {
        if (sCrawler.active) {
            va_arg(args, s32); // limbIndex
            Gfx** dList = va_arg(args, Gfx**);
            *dList = NULL;
        }
    });

    // A new scene means a new Link actor; start fresh
    COND_HOOK(OnSceneInit, enabled, [](int16_t) { sCrawler = CrawlerState(); });
}

} // namespace

bool DungeonCrawler_IsActive() {
    return sCrawler.active;
}

signed char DungeonCrawler_GetFacing() {
    return sCrawler.facing;
}

void DungeonCrawler_SetFacing(signed char facing) {
    sCrawler.facing = (s8)(facing & 3);
    sCrawler.action = Action::None;
    StartTurn(FacingToYaw(sCrawler.facing));
}

static RegisterShipInitFunc initFunc(RegisterDungeonCrawler, { CVAR_CRAWLER_NAME });

void DungeonCrawler_SetPitch(short pitch) {
    sCrawler.pitch = pitch;
}
