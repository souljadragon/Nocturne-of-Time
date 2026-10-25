// Nocturne of Time: SMT-style automap.
//
// The world is divided into fixed 80-unit tiles (the crawler's step size) and stacked into floors by height.
// Every tile Link stands on is recorded with the walls and doors on its four sides, and drawn as a small map
// in the bottom-right corner (replacing the vanilla minimap while the crawler is active). L toggles a large
// centred map. Maps are kept per dungeon for the play session; saving them comes with the save-data work.

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "Automap.h"
#include "UI/RetroUI.h"
#include "DungeonCrawler.h"
#include "Battle/BattleScene.h"
#include "soh/frame_interpolation.h"

#include <math.h>
#include <algorithm>
#include <unordered_map>

extern "C" {
#include "global.h"
#include "macros.h"
extern PlayState* gPlayState;
int16_t OTRGetRectDimensionFromRightEdge(float v);
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

namespace {

constexpr f32 kTileSize = 80.0f;    // must match the crawler's step
constexpr f32 kFloorHeight = 120.0f; // vertical size of one map floor
constexpr f32 kWallProbe = 44.0f;   // walls within this of the tile centre (half a tile + a little) count as that side's wall

// Directions match the crawler's facing: 0 = +Z, 1 = +X, 2 = -Z, 3 = -X.
struct Cell {
    u8 walls = 0; // bit d set: wall on side d
    u8 doors = 0; // bit d set: door on side d
};

struct CellKey {
    s32 x, z, floor;
    bool operator==(const CellKey& o) const {
        return x == o.x && z == o.z && floor == o.floor;
    }
};

struct CellKeyHash {
    size_t operator()(const CellKey& k) const {
        return (size_t)(((uint64_t)(u16)k.x << 32) | ((uint64_t)(u16)k.z << 16) | (uint64_t)(u16)k.floor);
    }
};

using FloorMap = std::unordered_map<CellKey, Cell, CellKeyHash>;

std::unordered_map<s16, FloorMap> sMaps; // per scene
bool sLarge = false;

// Last tile Link stood on, to clear walls he walks straight through
bool sHasLast = false;
s16 sLastScene = -1;
u32 sLastFrame = 0; // gameplay frame of the last update; a gap means a door, cutscene or climb happened


CellKey KeyFor(const Vec3f& pos) {
    return { (s32)floorf(pos.x / kTileSize + 0.5f), (s32)floorf(pos.z / kTileSize + 0.5f),
             (s32)floorf(pos.y / kFloorHeight + 0.5f) };
}

Vec3f DirFor(s32 d) {
    s16 yaw = (s16)(d * 0x4000);
    return { Math_SinS(yaw), 0.0f, Math_CosS(yaw) };
}

Cell ScanCell(PlayState* play, const Vec3f& pos) {
    static const f32 kProbeHeights[] = { 20.0f, 45.0f };
    Cell cell;
    for (s32 d = 0; d < 4; d++) {
        Vec3f dir = DirFor(d);
        for (f32 h : kProbeHeights) {
            Vec3f a = { pos.x, pos.y + h, pos.z };
            Vec3f b = { pos.x + dir.x * kWallProbe, pos.y + h, pos.z + dir.z * kWallProbe };
            Vec3f hit;
            CollisionPoly* poly;
            s32 bgId;
            if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
                cell.walls |= (1 << d);
                if (bgId != BGCHECK_SCENE) {
                    DynaPolyActor* dyna = DynaPoly_GetActor(&play->colCtx, bgId);
                    if (dyna != NULL && dyna->actor.category == ACTORCAT_DOOR) {
                        cell.doors |= (1 << d);
                    }
                }
                break;
            }
        }
    }
    return cell;
}

// ---- Drawing --------------------------------------------------------------------------------------------

// Famicom palette (see UI/RetroUI): green rooms, light-green walls, blue doors, Link in yellow
constexpr Retro::Color kFloor = Retro::GreenDark;
constexpr Retro::Color kWall = Retro::GreenLight;
constexpr Retro::Color kDoor = Retro::Blue;
constexpr Retro::Color kPlayer = Retro::Yellow;

} // namespace

// Outside the anonymous namespace: OPEN_DISPS declares FrameInterpolation helpers at block scope, which must
// resolve to the global extern "C" functions.
void Automap_Draw(PlayState* play, int boxX, int boxY, int boxSize) {
    Player* player = GET_PLAYER(play);
    auto sceneIt = sMaps.find(play->sceneNum);
    CellKey here = KeyFor(player->actor.world.pos);

    const s32 radius = sLarge ? 12 : 4; // tiles shown each side of Link (big map: 25 x 25 tiles at 8px)
    // screen pixels per tile: the corner map fills its box (inside a 4px window edge)
    const s32 cellPx = sLarge ? 8 : std::max(4, (boxSize - 8) / (2 * radius + 1));
    const s32 sizePx = (2 * radius + 1) * cellPx;
    s32 x0, y0;
    if (sLarge) {
        x0 = SCREEN_WIDTH / 2 - sizePx / 2;
        y0 = 120 - sizePx / 2; // between the big map's title and key
    } else {
        x0 = boxX + (boxSize - sizePx) / 2;
        y0 = boxY + (boxSize - sizePx) / 2;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Retro::Painter p(&OVERLAY_DISP);
    auto rect = [&](s32 x, s32 y, s32 w, s32 h, Retro::Color c) { p.Rect(x, y, w, h, c); };

    // Black window with a white rule
    if (sLarge) {
        p.Window(x0 - 4, y0 - 4, sizePx + 8, sizePx + 8, Retro::White, 255);
    } else {
        p.Window(boxX, boxY, boxSize, boxSize);
    }

    // Screen up is -Z (north), screen right is +X.
    if (sceneIt != sMaps.end()) {
        const FloorMap& cells = sceneIt->second;
        for (s32 dz = -radius; dz <= radius; dz++) {
            for (s32 dx = -radius; dx <= radius; dx++) {
                auto it = cells.find({ here.x + dx, here.z + dz, here.floor });
                if (it == cells.end()) {
                    continue;
                }
                const Cell& cell = it->second;
                s32 sx = x0 + (dx + radius) * cellPx;
                s32 sy = y0 + (dz + radius) * cellPx;
                rect(sx, sy, cellPx, cellPx, kFloor);

                // Side d -> edge: 0 (+Z) bottom, 1 (+X) right, 2 (-Z) top, 3 (-X) left
                for (s32 d = 0; d < 4; d++) {
                    if (!(cell.walls & (1 << d))) {
                        continue;
                    }
                    Retro::Color c = (cell.doors & (1 << d)) ? kDoor : kWall;
                    switch (d) {
                        case 0:
                            rect(sx, sy + cellPx - 1, cellPx, 1, c);
                            break;
                        case 1:
                            rect(sx + cellPx - 1, sy, 1, cellPx, c);
                            break;
                        case 2:
                            rect(sx, sy, cellPx, 1, c);
                            break;
                        case 3:
                            rect(sx, sy, 1, cellPx, c);
                            break;
                    }
                }
            }
        }
    }

    // Link: a dot with a nub pointing the way he faces
    s32 cx = x0 + radius * cellPx + cellPx / 2;
    s32 cy = y0 + radius * cellPx + cellPx / 2;
    rect(cx - 1, cy - 1, 3, 3, kPlayer);
    switch (DungeonCrawler_GetFacing()) {
        case 0:
            rect(cx, cy + 2, 1, 2, kPlayer);
            break;
        case 1:
            rect(cx + 2, cy, 2, 1, kPlayer);
            break;
        case 2:
            rect(cx, cy - 3, 1, 2, kPlayer);
            break;
        case 3:
            rect(cx - 3, cy, 2, 1, kPlayer);
            break;
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

void RegisterAutomap() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;

    COND_VB_SHOULD(VB_DRAW_MINIMAP, enabled, {
        PlayState* play = va_arg(args, PlayState*);
        if (DungeonCrawler_IsActive() && play->pauseCtx.state == 0) {
            *should = false; // the retro field HUD draws the automap instead (UI/FieldHud)
        }
    });

    // Maps belong to a save file
    COND_HOOK(OnLoadGame, enabled, [](int32_t) {
        sMaps.clear();
        sLarge = false;
        sHasLast = false;
    });
}

} // namespace

void Automap_Update(PlayState* play, Player* player) {
    static CellKey sLastKey = { 0, 0, 0 };

    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        sHasLast = false;
        return;
    }
    FloorMap& cells = sMaps[play->sceneNum];
    CellKey key = KeyFor(player->actor.world.pos);

    if (cells.find(key) == cells.end()) {
        // Scan from the tile centre so the result doesn't depend on where in the tile Link stands
        Vec3f centre = { key.x * kTileSize, player->actor.world.pos.y, key.z * kTileSize };
        cells[key] = ScanCell(play, centre);
    }

    // Walking from one tile straight into its neighbour proves there's no wall between them
    if (sHasLast && sLastScene == play->sceneNum && sLastFrame + 1 == play->gameplayFrames && sLastKey.floor == key.floor && !(sLastKey == key)) {
        s32 dx = key.x - sLastKey.x;
        s32 dz = key.z - sLastKey.z;
        s32 side = -1; // side of the last tile that was crossed
        if (dx == 0 && dz == 1) {
            side = 0;
        } else if (dx == 1 && dz == 0) {
            side = 1;
        } else if (dx == 0 && dz == -1) {
            side = 2;
        } else if (dx == -1 && dz == 0) {
            side = 3;
        }
        auto last = cells.find(sLastKey);
        if (side >= 0 && last != cells.end()) {
            s32 opposite = (side + 2) & 3;
            last->second.walls &= ~(1 << side);
            last->second.doors &= ~(1 << side);
            cells[key].walls &= ~(1 << opposite);
            cells[key].doors &= ~(1 << opposite);
        }
    }
    sLastKey = key;
    sLastScene = play->sceneNum;
    sLastFrame = play->gameplayFrames;
    sHasLast = true;
}

void Automap_ToggleLarge() {
    sLarge = !sLarge;
}

bool Automap_IsLarge() {
    return sLarge;
}

static RegisterShipInitFunc initFunc(RegisterAutomap, { CVAR_CRAWLER_NAME });
