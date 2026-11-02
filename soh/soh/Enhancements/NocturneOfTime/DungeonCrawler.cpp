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
//   forward at a gap   jumps it when there's floor 2-3 tiles on (Z + forward drops down instead)
//   forward at a drop  climbs down to the floor below (ladders, vines, ledges)
//   right stick     look up / down (springs back)
//   A, B, items     unchanged (open doors, talk, swing sword...)

#include "Dungeon/HuskDungeon.h"
#include "Dungeon/FieldFire.h"
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
constexpr s16 kHuskStepFrames = 9;    // ...one of the Husk's tiles (twice as long)
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
enum class Action { None, Step, Turn, Bump, Assist, Climb };

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
    bool needCentre = false; // line the grid up with the corridor once he's on the ground (AlignToCorridor)
    s16 gridYaw = 0;         // the grid's rotation: 0 everywhere except where the level isn't axis-aligned
    s8 lastRoom = -1;        // the room the grid was last lined up for
    Vec3f climbFrom = { 0.0f, 0.0f, 0.0f }; // a climb (or a drop): from here up/down, then onto `to`
    f32 climbRise = 0.0f;
    bool jumping = false;   // an auto-jump across a gap (an arc, not a climb)
    bool faceWall = false;  // after climbing: look straight on from the wall that was climbed
    s16 wallYaw = 0;        // ...which faces this way (Link's direction while climbing it)
};

CrawlerState sCrawler;

s16 FacingToYaw(s8 facing) {
    return (s16)(sCrawler.gridYaw + (facing & 3) * 0x4000);
}

s8 YawToFacing(s16 yaw) {
    // Round to the nearest quarter turn of the grid
    return (s8)((((u16)(s16)(yaw - sCrawler.gridYaw) + 0x2000) >> 14) & 3);
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
    // In the grid's own axes: f0 = facing 0 = (sin g, cos g), f1 = (cos g, -sin g)
    f32 s = Math_SinS(sCrawler.gridYaw), c = Math_CosS(sCrawler.gridYaw);
    f32 dx = pos.x - sCrawler.gridOrigin.x, dz = pos.z - sCrawler.gridOrigin.z;
    f32 a = roundf((dx * c - dz * s) / kTileSize) * kTileSize;
    f32 b = roundf((dx * s + dz * c) / kTileSize) * kTileSize;
    Vec3f out = pos;
    out.x = sCrawler.gridOrigin.x + c * a + s * b;
    out.z = sCrawler.gridOrigin.z - s * a + c * b;
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

constexpr s16 kClimbFrames = 18;

// Climbing as a grid move: the top of the vines or ladder in front, onto the floor just past its edge. Returns
// false if no top is found (then the wall is just a wall).
bool FindClimbTop(PlayState* play, const Vec3f& from, const Vec3f& dir, f32 wallDist, Vec3f& top) {
    for (f32 h = 40.0f; h < 1200.0f; h += 20.0f) {
        Vec3f a = { from.x, from.y + h, from.z };
        Vec3f b = { from.x + dir.x * (wallDist + 30.0f), from.y + h, from.z + dir.z * (wallDist + 30.0f) };
        Vec3f hit;
        CollisionPoly* poly;
        s32 bgId;
        if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
            continue; // still wall here
        }
        // Clear above the wall: the ledge's floor, a little past the edge
        Vec3f probe = { from.x + dir.x * (wallDist + 45.0f), from.y + h + 40.0f, from.z + dir.z * (wallDist + 45.0f) };
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY < from.y + 30.0f) {
            return false;
        }
        top = { probe.x, floorY, probe.z };
        return true;
    }
    return false;
}

void StartClimb(Player* player, const Vec3f& to) {
    sCrawler.jumping = false;
    sCrawler.faceWall = false;
    sCrawler.action = Action::Climb;
    sCrawler.timer = 0;
    sCrawler.climbFrom = player->actor.world.pos;
    sCrawler.to = to;
    sCrawler.climbRise = to.y - player->actor.world.pos.y;
    Player_PlaySfx(&player->actor, sCrawler.climbRise > 0 ? NA_SE_PL_CLIMB_CLIFF : NA_SE_PL_LAND);
}

// The Deku Tree's rooms as OoT built them: most run along the world axes, a few at 45 degrees. The grid turns to
// match the room Link is in (and he's centred in its passages), so steps run straight down its corridors.
s16 RoomGridYaw(PlayState* play, s8 room, bool& known) {
    known = false;
    if (play->sceneNum == SCENE_DEKU_TREE) {
        static const s8 k45[] = { 1, 2, 8 };
        known = true;
        for (s8 r : k45) {
            if (r == room) {
                return 0x2000;
            }
        }
        return 0;
    }
    return 0;
}

void StartJump(Player* player, const Vec3f& to) {
    StartClimb(player, to);
    sCrawler.jumping = true;
    Player_PlaySfx(&player->actor, NA_SE_VO_LI_AUTO_JUMP);
}

// Is there water at (x, z) above floor height y?
bool WaterAt(PlayState* play, f32 x, f32 z, f32 y) {
    f32 surface;
    WaterBox* box;
    return WaterBox_GetSurfaceImpl(play, &play->colCtx, x, z, &surface, &box) && surface > y + 5.0f;
}

// A gap ahead: the first floor within reach on the far side at about Link's height (the tile or two after the
// one in front). Its centre, or false if there isn't one.
bool FindJumpLanding(PlayState* play, const Vec3f& from, const Vec3f& dir, Vec3f& landing) {
    for (int tiles = 2; tiles <= 3; tiles++) {
        Vec3f to = { from.x + dir.x * kTileSize * tiles, from.y, from.z + dir.z * kTileSize * tiles };
        f32 y = FloorAt(play, from, to);
        if (y <= BGCHECK_Y_MIN + 1.0f || y > from.y + kMaxStepUp || y < from.y - 40.0f) {
            continue;
        }
        // Nothing in the way at chest height
        Vec3f a = { from.x, from.y + 45.0f, from.z }, b = { to.x, y + 45.0f, to.z }, hit;
        CollisionPoly* poly;
        s32 bgId;
        if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
            return false;
        }
        landing = { to.x, y, to.z };
        return true;
    }
    return false;
}

void StartStep(PlayState* play, Player* player, s8 dirFacing, bool dropDown = false) {
    bool forward = ((dirFacing - sCrawler.facing) & 3) == 0;
    s16 gridYaw = FacingToYaw(sCrawler.facing);
    Vec3f dir = YawToDir(FacingToYaw(dirFacing));
    Vec3f from = player->actor.world.pos;

    // The Husk's grid floors decide steps themselves (walls, webs, the gate, ledges)
    if (Husk_Active(play)) {
        Vec3f next;
        if (!Husk_CanStep(play, from, dirFacing & 3, next)) {
            StartBump(player, dir);
        } else {
            BeginStep(from, next, gridYaw);
        }
        return;
    }
    Vec3f to = SnapToGrid({ from.x + dir.x * kTileSize, from.y, from.z + dir.z * kTileSize });

    WallHit wall = ProbeWalls(play, from, dir, DistXZ(from, to) + kWallClearance);
    if (wall.hit) {
        u32 wallFlags = SurfaceType_GetWallFlags(&play->colCtx, wall.poly, wall.bgId);

        // Vines and ladders: up to the top in one move (as a crawler climbs stairs); the game's own climbing if no
        // top can be found
        if (forward && (wallFlags & (WALL_FLAG_CLIMBABLE | WALL_FLAG_LADDER))) {
            Vec3f top;
            if (FindClimbTop(play, from, dir, wall.dist, top)) {
                StartClimb(player, top);
                if (wall.poly != NULL) {
                    // Afterwards, look straight on from the wall (a ladder on a slanted wall of the round lobby)
                    sCrawler.faceWall = true;
                    sCrawler.wallYaw = Math_Atan2S(-COLPOLY_GET_NORMAL(wall.poly->normal.z),
                                                   -COLPOLY_GET_NORMAL(wall.poly->normal.x));
                }
            } else {
                StartAssist();
            }
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
    bool gap = floorY <= BGCHECK_Y_MIN + 1.0f || floorY < from.y - kDropAssist;
    // A gap with floor on the far side: jump it (Z + forward drops down instead)
    if (forward && gap && !dropDown) {
        Vec3f landing;
        if (FindJumpLanding(play, from, dir, landing)) {
            StartJump(player, landing);
            return;
        }
    }
    if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY > from.y + kMaxStepUp) {
        // Void, or a ledge too tall to step onto
        StartBump(player, dir);
        return;
    }
    if (forward && floorY < from.y - kDropAssist) {
        // A real drop (ladders and vines down included): Link climbs down to the floor below in one move. Onto a
        // web the game's own fall is needed (a long fall breaks it), and into water the game's own swimming.
        CollisionPoly* poly;
        s32 bgId;
        Vec3f probe = { to.x, from.y + 5.0f, to.z };
        f32 below = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        DynaPolyActor* onto = bgId != BGCHECK_SCENE ? DynaPoly_GetActor(&play->colCtx, bgId) : NULL;
        if ((onto != NULL && onto->actor.id == ACTOR_BG_YDAN_SP) || WaterAt(play, to.x, to.z, below)) {
            StartAssist();
        } else {
            StartClimb(player, { to.x, below, to.z });
        }
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
            s16 stepFrames = gPlayState != NULL && Husk_Active(gPlayState) ? kHuskStepFrames : kStepFrames;
            f32 t = SmoothStep(CLAMP_MAX((f32)sCrawler.timer / stepFrames, 1.0f));
            player->actor.world.pos.x = sCrawler.from.x + (sCrawler.to.x - sCrawler.from.x) * t;
            player->actor.world.pos.z = sCrawler.from.z + (sCrawler.to.z - sCrawler.from.z) * t;
            s16 yawDiff = (s16)(sCrawler.stepYawTo - sCrawler.stepYawFrom);
            sCrawler.curYaw = (s16)(sCrawler.stepYawFrom + (s16)(yawDiff * t));
            if (sCrawler.timer >= stepFrames) {
                FieldFire_OnStep();
                sCrawler.curYaw = sCrawler.stepYawTo;
                Player_PlaySfx(&player->actor, NA_SE_PL_WALK_GROUND + player->floorSfxOffset);
                sCrawler.action = Action::None;
                // Onto the boss gate: its prompt opens instead of an encounter roll; in the Husk, its tiles
                // (chests, pits, tripwires...) do the same
                if (gPlayState != NULL && Husk_Active(gPlayState)) {
                    if (!Husk_OnStepEnd(gPlayState, player)) {
                        Battle_OnStep(gPlayState, player);
                    }
                } else if (gPlayState != NULL && !BossGate_OnStep(gPlayState, player)) {
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
                sCrawler.needCentre = true; // wherever he lands, line the grid up there
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

        case Action::Climb: {
            // Up (or down) the wall first, then over the edge
            sCrawler.timer++;
            f32 t = CLAMP_MAX((f32)sCrawler.timer / kClimbFrames, 1.0f);
            f32 rise = SmoothStep(CLAMP_MAX(t / 0.7f, 1.0f));
            f32 over = SmoothStep(CLAMP((t - 0.5f) / 0.5f, 0.0f, 1.0f));
            if (sCrawler.climbRise < 0.0f) { // going down: over the edge first, then down
                over = SmoothStep(CLAMP_MAX(t / 0.4f, 1.0f));
                rise = SmoothStep(CLAMP((t - 0.3f) / 0.7f, 0.0f, 1.0f));
            }
            f32 arc = 0.0f;
            if (sCrawler.jumping) { // a leap: straight across, with a hop
                over = t;
                rise = t;
                arc = sinf(t * kPi) * 40.0f;
            }
            player->actor.world.pos.x = sCrawler.climbFrom.x + (sCrawler.to.x - sCrawler.climbFrom.x) * over;
            player->actor.world.pos.z = sCrawler.climbFrom.z + (sCrawler.to.z - sCrawler.climbFrom.z) * over;
            player->actor.world.pos.y = sCrawler.climbFrom.y + sCrawler.climbRise * rise + arc;
            player->actor.velocity.y = 0.0f;
            if (sCrawler.timer >= kClimbFrames) {
                player->actor.world.pos = sCrawler.to;
                sCrawler.action = Action::None;
                sCrawler.jumping = false;
                if (sCrawler.faceWall) {
                    // Off a ladder or vines: the grid turns to the wall that was climbed, so the view looks straight
                    // ahead instead of off at an angle
                    sCrawler.faceWall = false;
                    sCrawler.gridYaw = (s16)(sCrawler.wallYaw - (s16)(sCrawler.facing * 0x4000));
                    sCrawler.gridOrigin = { sCrawler.to.x, 0.0f, sCrawler.to.z };
                    sCrawler.curYaw = FacingToYaw(sCrawler.facing);
                    sCrawler.lastRoom = gPlayState != NULL ? gPlayState->roomCtx.curRoom.num : -1;
                } else {
                    sCrawler.needCentre = true; // a new level: line the grid up again
                }
                Player_PlaySfx(&player->actor, NA_SE_PL_WALK_GROUND + player->floorSfxOffset);
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
    sCrawler.gridYaw = 0;
    sCrawler.pitch = 0;
    // Steps land on the automap's fixed world tiles (centres at multiples of the tile size)
    sCrawler.gridOrigin = { roundf(player->actor.world.pos.x / kTileSize) * kTileSize, 0.0f,
                            roundf(player->actor.world.pos.z / kTileSize) * kTileSize };
    sCrawler.facing = YawToFacing(player->actor.shape.rot.y);
    sCrawler.curYaw = player->actor.shape.rot.y;
    // Gohma's lair isn't laid out on the 80-unit grid: line the grid up with its corridors instead
    sCrawler.needCentre = gPlayState != NULL && (gPlayState->sceneNum == SCENE_DEKU_TREE_BOSS ||
                                                 gPlayState->sceneNum == SCENE_DEKU_TREE);
    sCrawler.lastRoom = -1;
    // Ease from wherever Link was facing onto the grid
    StartTurn(FacingToYaw(sCrawler.facing));
    player->actor.shape.shadowDraw = NULL;
}

// For levels that don't run along the world axes (Gohma's lair): turn the grid to match the nearest wall, move
// Link to the middle between that wall and the one opposite, and run the grid through him, so steps follow the
// corridor straight down its centre and the view looks along it.
void AlignToCorridor(PlayState* play, Player* player) {
    const f32 kReach = 220.0f;
    Vec3f pos = player->actor.world.pos;
    WallHit nearest;
    bool found = false;
    for (int i = 0; i < 16; i++) {
        WallHit w = ProbeWalls(play, pos, YawToDir((s16)(i * 0x1000)), kReach);
        if (w.hit && w.poly != NULL && (!found || w.dist < nearest.dist)) {
            nearest = w;
            found = true;
        }
    }
    bool known = false;
    s16 roomYaw = RoomGridYaw(play, play->roomCtx.curRoom.num, known);
    sCrawler.lastRoom = play->roomCtx.curRoom.num;
    if (known && roomYaw == 0) {
        // An axis-aligned room: the automap's own tiles (so the map and the steps agree)
        sCrawler.gridYaw = 0;
        sCrawler.gridOrigin = { 0.0f, 0.0f, 0.0f };
        Vec3f snapped = SnapToGrid(pos);
        player->actor.world.pos.x = snapped.x;
        player->actor.world.pos.z = snapped.z;
        sCrawler.facing = YawToFacing(sCrawler.curYaw);
        sCrawler.curYaw = FacingToYaw(sCrawler.facing);
        sCrawler.action = Action::None;
        player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = sCrawler.curYaw;
        return;
    }
    if (!found && !known) {
        return;
    }
    f32 nx = 0.0f, nz = 1.0f;
    if (found) {
        nx = COLPOLY_GET_NORMAL(nearest.poly->normal.x);
        nz = COLPOLY_GET_NORMAL(nearest.poly->normal.z);
    }
    if (!known && nx * nx + nz * nz < 0.5f) {
        return; // not an upright wall
    }
    s16 normalYaw = known ? roomYaw : Math_Atan2S(nz, nx);
    s32 g = normalYaw;
    while (g > 0x2000) {
        g -= 0x4000;
    }
    while (g < -0x2000) {
        g += 0x4000;
    }
    sCrawler.gridYaw = (s16)g;

    // Centre between walls along both grid axes (in a corridor only the cross-axis has walls in reach)
    for (int axis = 0; axis < 2; axis++) {
        Vec3f n = YawToDir((s16)(normalYaw + axis * 0x4000));
        WallHit a = ProbeWalls(play, pos, n, kReach);
        WallHit b = ProbeWalls(play, pos, { -n.x, 0.0f, -n.z }, kReach);
        if (a.hit && b.hit) {
            f32 shift = (a.dist - b.dist) * 0.5f;
            pos.x += n.x * shift;
            pos.z += n.z * shift;
        }
    }
    player->actor.world.pos.x = pos.x;
    player->actor.world.pos.z = pos.z;
    sCrawler.gridOrigin = { pos.x, 0.0f, pos.z };
    // Straight onto the corridor's line in one go (this happens as the view switches to first person, so a
    // cut reads better than a turn followed by a second adjustment)
    sCrawler.facing = YawToFacing(sCrawler.curYaw);
    sCrawler.curYaw = FacingToYaw(sCrawler.facing);
    sCrawler.action = Action::None;
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = sCrawler.curYaw;
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
    // B too: Link doesn't swing his sword while crawling; fights are Press Turn battles
    // C-Up too: OoT's first-person look would take the screen from the crawler. In the Husk all the C buttons:
    // OoT's items have no use on its floors (its Deku Sticks are Nocturne items)
    u16 kEatenButtons = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_Z | BTN_L | BTN_R | BTN_B | BTN_CUP;
    if (gPlayState != NULL && Husk_Active(gPlayState)) {
        kEatenButtons |= BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT;
    }
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

    FieldFire_Tick(play);
    // A climb moves Link itself: his own update (gravity) stays off until he's up
    if (sCrawler.action == Action::Climb) {
        UpdateAction(player);
        ClearMovementInput(input);
        for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
            pad->button &= ~(BTN_A | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT | BTN_CUP);
        }
        player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = sCrawler.curYaw;
        return false;
    }
    // Into another room: line the grid up with it
    if (play->sceneNum == SCENE_DEKU_TREE && sCrawler.action == Action::None &&
        play->roomCtx.curRoom.num != sCrawler.lastRoom && !Husk_Active(play)) {
        sCrawler.needCentre = true;
    }

    // The Husk puts Link on its floors (and keeps him there while the floor settles)
    if (Husk_Active(play) && Husk_Frame(play, player)) {
        ClearMovementInput(input);
        for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
            pad->button &= ~(BTN_A | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT | BTN_CUP);
        }
        return false;
    }

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

    // A at the real tree's torches and webs: Deku Stick fire
    if (!Husk_Active(play) && (input->press.button & BTN_A) && sCrawler.action == Action::None &&
        FieldFire_OnA(play, player, sCrawler.curYaw)) {
        for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
            pad->button &= ~BTN_A;
        }
    }

    // A in the Husk: torches, webs, stones, the ways up and down (and never Link's own A actions)
    if (Husk_Active(play)) {
        if ((input->press.button & BTN_A) && sCrawler.action == Action::None) {
            Husk_OnA(play, player, sCrawler.facing);
        }
        for (OSContPad* pad : { &input->cur, &input->press, &input->rel }) {
            pad->button &= ~BTN_A;
        }
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
    if (sCrawler.needCentre && !airborne) {
        sCrawler.needCentre = false;
        AlignToCorridor(play, player);
    }

    // Holding a direction keeps walking/turning, one tile or quarter turn at a time
    if (sCrawler.action == Action::None && !airborne) {
        // Facing + 1 is a quarter turn to the left (yaw grows counter-clockwise seen from above)
        if (up) {
            StartStep(play, player, sCrawler.facing, strafe); // Z + forward: drop down a gap instead of jumping
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

    // The real dungeons' enemies are random encounters now: no vanilla enemies roam them. (The Deku Scrubs of the
    // boss door's riddle and Gold Skulltulas stay.)
    COND_HOOK(ShouldActorInit, enabled, [](void* actorRef, bool* result) {
        Actor* actor = (Actor*)actorRef;
        if (gPlayState == NULL || gPlayState->sceneNum != SCENE_DEKU_TREE || actor->category != ACTORCAT_ENEMY) {
            return;
        }
        bool goldSkulltula = actor->id == ACTOR_EN_SW && (actor->params & 0xE000) != 0;
        if (actor->id != ACTOR_EN_HINTNUTS && !goldSkulltula) {
            *result = false;
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

void DungeonCrawler_SnapFacing(signed char facing) {
    sCrawler.facing = (s8)(facing & 3);
    sCrawler.action = Action::None;
    sCrawler.curYaw = FacingToYaw(sCrawler.facing);
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

void DungeonCrawler_FaceYaw(short yaw) {
    sCrawler.action = Action::None;
    sCrawler.facing = YawToFacing(yaw);
    StartTurn(yaw); // any angle: boss fights look straight at the room's centre
}
