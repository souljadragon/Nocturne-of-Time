#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ActorDB.h"
#include "soh/cvar_prefixes.h"

#include "HuskDungeon.h"
#include "HuskFloors.h"
#include "HuskTextures.h"
#include "HuskProps.h"
#include "align_asset_macro.h"
#include "../Party.h"
#include "../DungeonCrawler.h"
#include "../Battle/BattleScene.h"
#include "../Battle/BattleData.h"
#include "../Boss/BossGate.h"
#include "../Loot/Loot.h"
#include "../Moon/Moon.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <memory>
extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
extern Mtx gMtxClear;
void Play_PerformSave(PlayState* play);
}

using namespace Nocturne;

namespace {

// ---- Where the floors are built -------------------------------------------------------------------------
// Far from the vanilla tree (outside its collision box); each floor at its own height so the automap keeps them
// apart. Since the Etrian-style rebuild a tile is 160 units (twice the old one): corridors wide enough for a party
// and its foes, with high roofs, rooms higher still and halls whose roof is lost in the dark.
constexpr f32 kTile = 160.0f;
constexpr f32 kHalf = 80.0f;
constexpr f32 kWallH = 280.0f;  // a corridor's roof
constexpr f32 kRoomH = 400.0f;  // a room's
constexpr f32 kHallH = 760.0f;  // a great hall's
constexpr f32 kShaftDepth = 600.0f; // the Great Web's shaft, down to the floor below
constexpr f32 kOriginX = 6400.0f, kOriginZ = 6400.0f; // multiples of kTile, so the automap's cells line up
// The battle chamber (see "The battle chamber" below), off the west of every floor
constexpr f32 kStageXc = kOriginX - 2400.0f, kStageZc = kOriginZ + 1600.0f;
constexpr int kStageHalfWc = 2, kStageDepthc = 4;
constexpr f32 kTopY = 3000.0f, kFloorStep = 600.0f;
constexpr int kStickSteps = 12;
constexpr int kLayoutVersion = 2; // 2: F1 rebuilt as the Great Hollow (Nov 1)
constexpr int kMessageFrames = 90; // 4.5 seconds
constexpr int kDrawRadius = 8;     // tiles drawn around the camera

f32 FloorY(int floor) {
    return kTopY - kFloorStep * floor;
}

f32 RoofOf(int f, int x, int y) {
    switch (HuskSpaceAt(f, x, y)) {
        case HuskSpace::Hall:
            return kHallH;
        case HuskSpace::Room:
            return kRoomH;
        default:
            return kWallH;
    }
}

bool ModOn() {
    return CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) != 0;
}

using Spot = std::tuple<int, int, int>; // floor, x, y

// ---- Saved state ----------------------------------------------------------------------------------------
struct State {
    std::set<Spot> chests, webs, torches, read, sacs, elites, pitsFound, wiresFound;
    bool stoneF3 = false;     // the party has saved at F3's Sheikah Stone
    int loadFloor = -1, loadX = 0, loadY = 0; // a save made at a Sheikah Stone reloads beside it
};
State sState;

// ---- This visit -----------------------------------------------------------------------------------------
struct Visit {
    std::set<Spot> wiresTripped;
    std::set<Spot> stonesUsed;
};
Visit sVisit;

int sFloor = 0;
int sStick = 0;           // steps left on the burning stick
bool sStickOutInBattle = false;
std::string sMsg;
uint32_t sMsgColor = 0;
int sMsgTimer = 0;
bool sSavingAtStone = false;

// Where Link appears when he next enters the scene (or a floor)
struct Arrival {
    bool pending = false;
    int floor = 0, x = 0, y = 0;
    s8 facing = 2;
};
Arrival sArrival;
int sPlaceFrames = 0;     // Link is held in place while the floor's collision settles
Actor* sWorld = nullptr;  // this floor's world actor
int sWorldActorId = -1;
// A fixed fight waiting for its result: 'X' an egg sac, 'E' the elite
char sFixedKind = 0;
Spot sFixedSpot;

void Say(const std::string& text, uint32_t color = 0xFCE0A800u) {
    sMsg = text;
    sMsgColor = color;
    sMsgTimer = kMessageFrames;
}

bool Has(const std::set<Spot>& s, int f, int x, int y) {
    return s.count({ f, x, y }) > 0;
}

// What blocks a step right now
bool Solid(int f, int x, int y) {
    char t = HuskTile(f, x, y);
    if (HuskTileSolid(t)) {
        return true;
    }
    if (t == 'W' || t == 'w') {
        return !Has(sState.webs, f, x, y);
    }
    if (t == 'G') {
        return !Husk_GateOpen();
    }
    // Chests stand on their tiles (opened from the next one, facing them); egg sacs until they're destroyed
    if (t == 'C' || t == 'k') {
        return true;
    }
    if (t == 'X') {
        return !Has(sState.sacs, f, x, y);
    }
    return false;
}

void TileOf(const Vec3f& pos, int& x, int& y) {
    x = (int)lroundf((pos.x - kOriginX) / kTile);
    y = (int)lroundf((pos.z - kOriginZ) / kTile);
}

Vec3f CentreOf(int f, int x, int y) {
    return { kOriginX + x * kTile, FloorY(f), kOriginZ + y * kTile };
}

void DirOf(int facing, int& dx, int& dy) {
    static const int kDx[4] = { 0, 1, 0, -1 }, kDy[4] = { 1, 0, -1, 0 };
    dx = kDx[facing & 3];
    dy = kDy[facing & 3];
}

int SacsDestroyed() {
    int n = 0;
    for (const Spot& s : sState.sacs) {
        n += std::get<0>(s) == 2 ? 1 : 0;
    }
    return n;
}

// ---- Collision ------------------------------------------------------------------------------------------
struct Collision {
    std::vector<Vec3s> vtx;
    std::vector<CollisionPoly> polys;
    SurfaceType surface = { { 0, 0 } };
    CamData cam = { CAM_SET_DUNGEON0, 0, nullptr };
    CollisionHeader header{};
};
Collision sCol;

void AddTri(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& facing) {
    Vec3f p[3] = { a, b, c };
    Vec3f u = { b.x - a.x, b.y - a.y, b.z - a.z }, v = { c.x - a.x, c.y - a.y, c.z - a.z };
    Vec3f n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
    if (n.x * facing.x + n.y * facing.y + n.z * facing.z < 0.0f) {
        std::swap(p[1], p[2]); // wind so the normal points the way the face looks
        n = { -n.x, -n.y, -n.z };
    }
    f32 len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len < 0.001f) {
        return;
    }
    n = { n.x / len, n.y / len, n.z / len };
    CollisionPoly poly{};
    u16 base = (u16)sCol.vtx.size();
    for (const Vec3f& q : p) {
        sCol.vtx.push_back({ (s16)lroundf(q.x), (s16)lroundf(q.y), (s16)lroundf(q.z) });
    }
    poly.type = 0;
    poly.flags_vIA = base;
    poly.flags_vIB = (u16)(base + 1);
    poly.vIC = (u16)(base + 2);
    poly.normal = { (s16)lroundf(n.x * 0x7FFF), (s16)lroundf(n.y * 0x7FFF), (s16)lroundf(n.z * 0x7FFF) };
    poly.dist = (s16)lroundf(-(n.x * p[0].x + n.y * p[0].y + n.z * p[0].z));
    sCol.polys.push_back(poly);
}

void AddQuad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, const Vec3f& facing) {
    AddTri(a, b, c, facing);
    AddTri(a, c, d, facing);
}

void BuildCollision(int f) {
    sCol.vtx.clear();
    sCol.polys.clear();
    const HuskFloor& fl = HuskFloorOf(f);
    f32 y0 = FloorY(f), y1 = y0 + kHallH;
    auto walk = [&](int x, int y) { return !Solid(f, x, y); };
    // Floor: one quad under the whole grid
    {
        f32 x0 = kOriginX - kHalf, z0 = kOriginZ - kHalf;
        f32 x1 = x0 + fl.Width() * kTile, z1 = z0 + fl.Height() * kTile;
        AddQuad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, { 0, 1, 0 });
    }
    // The battle chamber's floor
    {
        f32 x0 = kStageXc - (kStageHalfWc + 0.5f) * kTile, x1 = kStageXc + (kStageHalfWc + 0.5f) * kTile;
        f32 z0 = kStageZc - (kStageDepthc + 0.5f) * kTile, z1 = kStageZc + 0.5f * kTile;
        AddQuad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, { 0, 1, 0 });
    }
    // North / south faces, merged along each row
    for (int side = 0; side < 2; side++) {
        int dy = side == 0 ? -1 : 1; // the wall is on this side of the walkable tile
        for (int y = 0; y < fl.Height(); y++) {
            int x = 0;
            while (x < fl.Width()) {
                if (!(walk(x, y) && Solid(f, x, y + dy))) {
                    x++;
                    continue;
                }
                int start = x;
                while (x < fl.Width() && walk(x, y) && Solid(f, x, y + dy)) {
                    x++;
                }
                f32 z = kOriginZ + y * kTile + dy * kHalf;
                f32 xa = kOriginX + start * kTile - kHalf, xb = kOriginX + x * kTile - kHalf;
                AddQuad({ xa, y0, z }, { xb, y0, z }, { xb, y1, z }, { xa, y1, z }, { 0, 0, (f32)-dy });
            }
        }
    }
    // East / west faces, merged down each column
    for (int side = 0; side < 2; side++) {
        int dx = side == 0 ? -1 : 1;
        for (int x = 0; x < fl.Width(); x++) {
            int y = 0;
            while (y < fl.Height()) {
                if (!(walk(x, y) && Solid(f, x + dx, y))) {
                    y++;
                    continue;
                }
                int start = y;
                while (y < fl.Height() && walk(x, y) && Solid(f, x + dx, y)) {
                    y++;
                }
                f32 xw = kOriginX + x * kTile + dx * kHalf;
                f32 za = kOriginZ + start * kTile - kHalf, zb = kOriginZ + y * kTile - kHalf;
                AddQuad({ xw, y0, za }, { xw, y0, zb }, { xw, y1, zb }, { xw, y1, za }, { (f32)-dx, 0, 0 });
            }
        }
    }
    Vec3s mn = { 32767, 32767, 32767 }, mx = { -32767, -32767, -32767 };
    for (const Vec3s& v : sCol.vtx) {
        mn = { std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z) };
        mx = { std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z) };
    }
    CollisionHeader& h = sCol.header;
    h = {};
    h.minBounds = mn;
    h.maxBounds = mx;
    h.numVertices = (u16)sCol.vtx.size();
    h.vtxList = sCol.vtx.data();
    h.numPolygons = (u16)sCol.polys.size();
    h.polyList = sCol.polys.data();
    h.surfaceTypeList = &sCol.surface;
    h.cameraDataList = &sCol.cam;
    h.cameraDataListLen = 1;
    h.numWaterBoxes = 0;
    h.waterBoxes = nullptr;
}

// ---- Drawing --------------------------------------------------------------------------------------------
// The Deku Tree's own textures, read from the game's assets at runtime (nothing is copied into the mod): its
// trunk bark, mossy floors, hanging roots, root lattices, the great web and ivy. Ids from kVanBase up; the Husk's
// own textures (props: chests, flames, stones...) keep their HuskTex ids below it.
constexpr int kVanBase = 100;
namespace Van {
enum : int { Bark = kVanBase, BarkPale, Moss, Roof, RoofDark, Roots, Lattice, GreatWeb, Ivy, Stone, Water, End };
static const ALIGN_ASSET(2) char kTlut[] = "__OTR__scenes/nonmq/ydan_scene/ydan_sceneTLUT_00B810";
static const ALIGN_ASSET(2) char kBark[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_0109C0";
static const ALIGN_ASSET(2) char kBarkPale[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_00CDC0";
static const ALIGN_ASSET(2) char kMoss[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_00B5C0";
static const ALIGN_ASSET(2) char kRoof[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_00E1C0";
static const ALIGN_ASSET(2) char kRoofDark[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_00E9C0";
static const ALIGN_ASSET(2) char kRoots[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_0071C0";
static const ALIGN_ASSET(2) char kLattice[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_012F48";
static const ALIGN_ASSET(2) char kWeb[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_014348";
static const ALIGN_ASSET(2) char kIvy[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_11Tex_0044D8";
static const ALIGN_ASSET(2) char kStone[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_3Tex_00A8C0";
static const ALIGN_ASSET(2) char kWater[] = "__OTR__scenes/nonmq/ydan_scene/ydan_room_0Tex_013F48";
struct Info {
    const char* path;
    u8 fmt, siz;
    int w, h, maskS, maskT;
};
const Info kInfo[End - kVanBase] = {
    { kBark, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 64, 5, 6 },     { kBarkPale, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 64, 5, 6 },
    { kMoss, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 64, 5, 6 },     { kRoof, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 64, 5, 6 },
    { kRoofDark, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 64, 5, 6 }, { kRoots, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 64, 5, 6 },
    { kLattice, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 64, 5, 6 }, { kWeb, G_IM_FMT_IA, G_IM_SIZ_16b, 32, 64, 5, 6 },
    { kIvy, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 64, 5, 6 },  { kStone, G_IM_FMT_CI, G_IM_SIZ_8b, 64, 32, 6, 5 },
    { kWater, G_IM_FMT_CI, G_IM_SIZ_8b, 32, 32, 5, 5 },
};
} // namespace Van

struct Quad {
    Vec3f p[4];
    f32 s[4], t[4];
    int tex;
    bool xlu;
    bool clampT; // hanging roots: one strip, not repeated down the wall
    f32 tr, tg, tb; // light scale per channel (the white web toned down, rot floors gone sickly purple)
};
std::vector<Quad> sQuads;
Vec3f sEye;
s16 sEyeYaw;
std::vector<Vec3f> sLights; // lit torches near the camera (and the burning stick)

void Emit(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, int tex, f32 sMax, f32 tMax, bool xlu,
          bool clampT = false) {
    Quad q;
    q.p[0] = a;
    q.p[1] = b;
    q.p[2] = c;
    q.p[3] = d;
    q.s[0] = 0;
    q.s[1] = sMax;
    q.s[2] = sMax;
    q.s[3] = 0;
    q.t[0] = tMax;
    q.t[1] = tMax;
    q.t[2] = 0;
    q.t[3] = 0;
    q.tex = tex;
    q.xlu = xlu;
    q.clampT = clampT;
    q.tr = q.tg = q.tb = 1.0f;
    sQuads.push_back(q);
}

// A flat quad on the floor of a tile (slightly raised so it doesn't fight the floor)
void FloorDecal(int f, int x, int y, int tex, f32 size = kHalf * 0.6f, f32 lift = 0.6f) {
    Vec3f c = CentreOf(f, x, y);
    f32 h = c.y + lift;
    Emit({ c.x - size, h, c.z + size }, { c.x + size, h, c.z + size }, { c.x + size, h, c.z - size },
         { c.x - size, h, c.z - size }, tex, 1, 1, true);
}

// Two upright quads crossing at the tile's centre (webs, the gate, vines)
void Cross(int f, int x, int y, int tex, f32 h0, f32 h1, f32 half = kHalf, f32 tRepeat = 1.0f) {
    Vec3f c = CentreOf(f, x, y);
    Emit({ c.x - half, c.y + h0, c.z }, { c.x + half, c.y + h0, c.z }, { c.x + half, c.y + h1, c.z },
         { c.x - half, c.y + h1, c.z }, tex, 1, tRepeat, true);
    Emit({ c.x, c.y + h0, c.z - half }, { c.x, c.y + h0, c.z + half }, { c.x, c.y + h1, c.z + half },
         { c.x, c.y + h1, c.z - half }, tex, 1, tRepeat, true);
}

// A quad that always faces the camera
void Billboard(const Vec3f& base, f32 w, f32 h, int tex) {
    f32 rx = Math_CosS(sEyeYaw), rz = -Math_SinS(sEyeYaw);
    f32 hw = w * 0.5f;
    Emit({ base.x - rx * hw, base.y, base.z - rz * hw }, { base.x + rx * hw, base.y, base.z + rz * hw },
         { base.x + rx * hw, base.y + h, base.z + rz * hw }, { base.x - rx * hw, base.y + h, base.z - rz * hw }, tex,
         1, 1, true);
}

// A box standing on a tile (pillars, chests)
void Box(const Vec3f& c, f32 halfW, f32 h, int sideTex, int topTex, f32 tRepeat) {
    f32 x0 = c.x - halfW, x1 = c.x + halfW, z0 = c.z - halfW, z1 = c.z + halfW, y0 = c.y, y1 = c.y + h;
    Emit({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, sideTex, 1, tRepeat, false);
    Emit({ x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, sideTex, 1, tRepeat, false);
    Emit({ x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, sideTex, 1, tRepeat, false);
    Emit({ x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 }, sideTex, 1, tRepeat, false);
    if (topTex >= 0) {
        Emit({ x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 }, topTex, 1, 1, false);
    }
}

bool TorchLit(int f, int x, int y) {
    char t = HuskTile(f, x, y);
    return t == 'T' || (t == 't' && Has(sState.torches, f, x, y));
}

void Tint(size_t from, f32 r, f32 g, f32 b) {
    for (size_t i = from; i < sQuads.size(); i++) {
        sQuads[i].tr = r;
        sQuads[i].tg = g;
        sQuads[i].tb = b;
    }
}

// A steady per-wall number, so dressing (lattices, ivy) doesn't flicker or shuffle as Link walks
u32 WallHash(int f, int x, int y, int side) {
    u32 h = (u32)(f * 73856093) ^ (u32)(x * 19349663) ^ (u32)(y * 83492791) ^ (u32)(side * 2654435761u);
    h ^= h >> 13;
    h *= 0x5bd1e995;
    return h ^ (h >> 15);
}

const int kDx[4] = { 0, 1, 0, -1 }, kDy[4] = { 1, 0, -1, 0 }; // crawler facings: 0 south, 1 east, 2 north, 3 west

// Walkable for dressing purposes (a chest or a stone stands on its tile; walls and pillars don't count)
bool Open(int f, int x, int y) {
    char t = HuskTile(f, x, y);
    return t != '#' && t != 'T' && t != 't' && t != 'H' && t != 'C' && t != 'k' && t != 'w';
}

// Which way a prop on a dead-end or wall-side tile should face: toward the first open neighbour
s16 FaceOpen(int f, int x, int y) {
    for (int d = 0; d < 4; d++) {
        if (Open(f, x + kDx[d], y + kDy[d])) {
            return (s16)(d * 0x4000);
        }
    }
    return 0;
}

// A web or gate across a passage: its plane runs across the way through (a north-south passage gets a plane
// running east-west, yaw 0)
s16 AcrossYaw(int f, int x, int y) {
    return (Open(f, x, y - 1) || Open(f, x, y + 1)) ? 0 : 0x4000;
}

// One wall face of a walkable tile, toward the wall block on `side` (0 south, 1 east, 2 north, 3 west), with its
// dressing: bark up to the roof, a root lattice on a few room walls, ivy climbing some hall walls, and roots
// hanging from the roof in corridors and rooms
void WallFace(int f, int x, int y, int side, f32 y0, f32 roof) {
    Vec3f c = CentreOf(f, x, y);
    Vec3f a, b, n;
    switch (side) {
        case 0: a = { c.x - kHalf, 0, c.z + kHalf }; b = { c.x + kHalf, 0, c.z + kHalf }; n = { 0, 0, -1 }; break;
        case 1: a = { c.x + kHalf, 0, c.z + kHalf }; b = { c.x + kHalf, 0, c.z - kHalf }; n = { -1, 0, 0 }; break;
        case 2: a = { c.x + kHalf, 0, c.z - kHalf }; b = { c.x - kHalf, 0, c.z - kHalf }; n = { 0, 0, 1 }; break;
        default: a = { c.x - kHalf, 0, c.z - kHalf }; b = { c.x - kHalf, 0, c.z + kHalf }; n = { 1, 0, 0 }; break;
    }
    f32 h = roof - y0;
    HuskSpace space = HuskSpaceAt(f, x, y);
    // bark: 32 texels across 40 units, 64 down 80 (about the vanilla tree's own density)
    Emit({ a.x, y0, a.z }, { b.x, y0, b.z }, { b.x, roof, b.z }, { a.x, roof, a.z }, Van::Bark, 4.0f, h / 80.0f, false);
    u32 hash = WallHash(f, x, y, side);
    auto off = [&](const Vec3f& p, f32 d) { return Vec3f{ p.x + n.x * d, 0, p.z + n.z * d }; };
    if (space != HuskSpace::Corridor && (hash % 6) == 0) {
        // A root lattice grown over the lower wall
        Vec3f a1 = off(a, 1.5f), b1 = off(b, 1.5f);
        Emit({ a1.x, y0, a1.z }, { b1.x, y0, b1.z }, { b1.x, y0 + 160.0f, b1.z }, { a1.x, y0 + 160.0f, a1.z },
             Van::Lattice, 4.0f, 2.0f, true);
    }
    if (space == HuskSpace::Hall) {
        if ((hash >> 4) % 3 == 0) {
            // Ivy climbing into the dark
            Vec3f a2 = off(a, 2.5f), b2 = off(b, 2.5f);
            Emit({ a2.x, y0, a2.z }, { b2.x, y0, b2.z }, { b2.x, y0 + 480.0f, b2.z }, { a2.x, y0 + 480.0f, a2.z },
                 Van::Ivy, 2.0f, 6.0f, true);
        }
    } else {
        // Roots hanging from the roof: one strip, their ragged ends showing
        Vec3f a3 = off(a, 3.0f), b3 = off(b, 3.0f);
        Emit({ a3.x, roof - 96.0f, a3.z }, { b3.x, roof - 96.0f, b3.z }, { b3.x, roof, b3.z }, { a3.x, roof, a3.z },
             Van::Roots, 4.0f, 1.0f, true, true);
    }
}

// A hole in the floor (the Great Web's shaft, ways down, pits once found): bark walls dropping into the dark
// where it meets solid floor, and a faint floor far below
void Hole(int f, int x, int y, f32 depth, bool (*same)(char)) {
    Vec3f c = CentreOf(f, x, y);
    f32 y0 = c.y, yb = c.y - depth;
    auto open = [&](int nx, int ny) { return same(HuskTile(f, nx, ny)); };
    f32 t = depth / 80.0f;
    if (!open(x, y + 1)) {
        Emit({ c.x + kHalf, yb, c.z + kHalf }, { c.x - kHalf, yb, c.z + kHalf }, { c.x - kHalf, y0, c.z + kHalf },
             { c.x + kHalf, y0, c.z + kHalf }, Van::Bark, 4.0f, t, false);
    }
    if (!open(x, y - 1)) {
        Emit({ c.x - kHalf, yb, c.z - kHalf }, { c.x + kHalf, yb, c.z - kHalf }, { c.x + kHalf, y0, c.z - kHalf },
             { c.x - kHalf, y0, c.z - kHalf }, Van::Bark, 4.0f, t, false);
    }
    if (!open(x + 1, y)) {
        Emit({ c.x + kHalf, yb, c.z - kHalf }, { c.x + kHalf, yb, c.z + kHalf }, { c.x + kHalf, y0, c.z + kHalf },
             { c.x + kHalf, y0, c.z - kHalf }, Van::Bark, 4.0f, t, false);
    }
    if (!open(x - 1, y)) {
        Emit({ c.x - kHalf, yb, c.z + kHalf }, { c.x - kHalf, yb, c.z - kHalf }, { c.x - kHalf, y0, c.z - kHalf },
             { c.x - kHalf, y0, c.z + kHalf }, Van::Bark, 4.0f, t, false);
    }
    Emit({ c.x - kHalf, yb, c.z + kHalf }, { c.x + kHalf, yb, c.z + kHalf }, { c.x + kHalf, yb, c.z - kHalf },
         { c.x - kHalf, yb, c.z - kHalf }, Van::Moss, 4.0f, 2.0f, false);
}

bool IsShaft(char t) {
    return t == 'w';
}
bool IsDrop(char t) {
    return t == 'D' || t == 'P';
}
bool IsWater(char t) {
    return t == '~';
}

// ---- Props: OoT's own models, drawn after the walls -------------------------------------------------------
struct Prop {
    enum Kind { Torch, WallWeb, FloorWeb, Stone, Sac, Chest } kind;
    Vec3f p;
    s16 yaw;
    f32 a, b;
    bool flag;
};
std::vector<Prop> sProps;

// ---- The battle chamber -------------------------------------------------------------------------------------
// Husk battles are fought in a chamber of their own, off the map and dressed like the floor (the Famicom games'
// battle screens): a corridor never leaves enemies standing in its walls again. Link is moved there when a battle
// starts and back when it ends.
constexpr f32 kStageX = kStageXc, kStageZ = kStageZc;
constexpr int kStageHalfW = kStageHalfWc, kStageDepth = kStageDepthc; // tiles either side of Link, and ahead
struct StageVisit {
    bool active = false;
    Vec3f returnPos;
    s8 returnFacing = 2;
};
StageVisit sStage;

void BuildStage(PlayState* play) {
    f32 y0 = FloorY(sFloor), roof = y0 + kRoomH;
    for (int ty = -kStageDepth; ty <= 0; ty++) {
        for (int tx = -kStageHalfW; tx <= kStageHalfW; tx++) {
            Vec3f c = { kStageX + tx * kTile, y0, kStageZ + ty * kTile };
            size_t first = sQuads.size();
            Emit({ c.x - kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z - kHalf },
                 { c.x - kHalf, y0, c.z - kHalf }, Van::Moss, 4.0f, 2.0f, false);
            if (sFloor == 2) {
                Tint(first, 0.85f, 0.62f, 0.9f); // Gohma's lair: the rot has got in
            }
            Emit({ c.x - kHalf, roof, c.z - kHalf }, { c.x + kHalf, roof, c.z - kHalf }, { c.x + kHalf, roof, c.z + kHalf },
                 { c.x - kHalf, roof, c.z + kHalf }, Van::Roof, 4.0f, 2.0f, false);
            auto wall = [&](Vec3f a, Vec3f b, bool dress) {
                Emit({ a.x, y0, a.z }, { b.x, y0, b.z }, { b.x, roof, b.z }, { a.x, roof, a.z }, Van::Bark, 4.0f,
                     kRoomH / 80.0f, false);
                if (dress) {
                    Emit({ a.x, roof - 96.0f, a.z }, { b.x, roof - 96.0f, b.z }, { b.x, roof, b.z }, { a.x, roof, a.z },
                         Van::Roots, 4.0f, 1.0f, true, true);
                }
            };
            if (ty == -kStageDepth) {
                wall({ c.x + kHalf, 0, c.z - kHalf + 2 }, { c.x - kHalf, 0, c.z - kHalf + 2 }, true);
                if (tx == 0 || tx == -2 || tx == 2) {
                    Emit({ c.x - kHalf, y0, c.z - kHalf + 4 }, { c.x + kHalf, y0, c.z - kHalf + 4 },
                         { c.x + kHalf, y0 + 160.0f, c.z - kHalf + 4 }, { c.x - kHalf, y0 + 160.0f, c.z - kHalf + 4 },
                         Van::Lattice, 4.0f, 2.0f, true);
                }
            }
            if (ty == 0) {
                wall({ c.x - kHalf, 0, c.z + kHalf }, { c.x + kHalf, 0, c.z + kHalf }, false);
            }
            if (tx == -kStageHalfW) {
                wall({ c.x - kHalf, 0, c.z - kHalf }, { c.x - kHalf, 0, c.z + kHalf }, true);
            }
            if (tx == kStageHalfW) {
                wall({ c.x + kHalf, 0, c.z + kHalf }, { c.x + kHalf, 0, c.z - kHalf }, true);
            }
        }
    }
    // Two torches burning at the back corners
    for (int side = -1; side <= 1; side += 2) {
        Vec3f t = { kStageX + side * (kStageHalfW * kTile + 20.0f), y0, kStageZ - kStageDepth * kTile - 20.0f };
        sProps.push_back({ Prop::Torch, t, 0, 1.7f, 0, true });
        sLights.push_back({ t.x, t.y + 100.0f, t.z });
    }
    (void)play;
}

void BuildQuads(PlayState* play) {
    sQuads.clear();
    sLights.clear();
    sProps.clear();
    if (sStage.active) {
        BuildStage(play);
        return;
    }
    int f = sFloor;
    int cx, cy;
    TileOf(sEye, cx, cy);
    if (sStick > 0) {
        sLights.push_back({ sEye.x, sEye.y + 20.0f, sEye.z }); // the burning stick, held up
    }
    bool greatWebDrawn = false;
    for (int y = cy - kDrawRadius; y <= cy + kDrawRadius; y++) {
        for (int x = cx - kDrawRadius; x <= cx + kDrawRadius; x++) {
            char t = HuskTile(f, x, y);
            if (t == '#') {
                continue;
            }
            Vec3f c = CentreOf(f, x, y);
            f32 y0 = c.y, roof = y0 + RoofOf(f, x, y);
            HuskSpace space = HuskSpaceAt(f, x, y);
            bool pitShown = t == 'P' && (HuskFloorOf(f).visiblePits || Has(sState.pitsFound, f, x, y));
            // Floor (or a hole)
            if (t == 'w') {
                Hole(f, x, y, kShaftDepth, IsShaft);
            } else if (t == 'D' || pitShown) {
                Hole(f, x, y, 360.0f, IsDrop);
            } else if (t == '~') {
                // The tree's basement water, as under the old lobby's web: a sunken pool, slowly drifting
                Hole(f, x, y, 70.0f, IsWater);
                f32 h = y0 - 22.0f, drift = (play->gameplayFrames % 160) / 160.0f;
                size_t first = sQuads.size();
                Emit({ c.x - kHalf, h, c.z + kHalf }, { c.x + kHalf, h, c.z + kHalf }, { c.x + kHalf, h, c.z - kHalf },
                     { c.x - kHalf, h, c.z - kHalf }, Van::Water, 3.0f, 3.0f, false);
                for (int k = 0; k < 4; k++) {
                    sQuads.back().s[k] += drift;
                    sQuads.back().t[k] += drift * 0.5f;
                }
                Tint(first, 0.75f, 0.9f, 0.95f);
            } else {
                size_t first = sQuads.size();
                Emit({ c.x - kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z + kHalf },
                     { c.x + kHalf, y0, c.z - kHalf }, { c.x - kHalf, y0, c.z - kHalf }, Van::Moss, 4.0f, 2.0f, false);
                if (t == 'R') {
                    Tint(first, 0.8f, 0.5f, 0.95f); // rot: the moss gone sickly
                } else if (t == 'B') {
                    Tint(first, 0.9f, 0.72f, 0.62f);
                }
            }
            // Roof, or the shaft up where vines climb to the floor above
            if (t == 'U' && f > 0) {
                f32 top = roof + 520.0f;
                for (int side = 0; side < 4; side++) {
                    Vec3f m = { c.x + kDx[side] * kHalf, 0, c.z + kDy[side] * kHalf };
                    f32 px = kDy[side] != 0 ? kHalf : 0.0f, pz = kDx[side] != 0 ? kHalf : 0.0f;
                    Emit({ m.x - px, roof, m.z - pz }, { m.x + px, roof, m.z + pz }, { m.x + px, top, m.z + pz },
                         { m.x - px, top, m.z - pz }, Van::Bark, 4.0f, 520.0f / 80.0f, false);
                }
            } else {
                Emit({ c.x - kHalf, roof, c.z - kHalf }, { c.x + kHalf, roof, c.z - kHalf },
                     { c.x + kHalf, roof, c.z + kHalf }, { c.x - kHalf, roof, c.z + kHalf },
                     space == HuskSpace::Hall ? Van::RoofDark : Van::Roof, 4.0f, 2.0f, false);
            }
            // Walls toward neighbouring wall blocks, and where a passage meets a taller room
            for (int side = 0; side < 4; side++) {
                int nx = x + kDx[side], ny = y + kDy[side];
                if (HuskTile(f, nx, ny) == '#') {
                    WallFace(f, x, y, side, y0, roof);
                    continue;
                }
                f32 other = y0 + RoofOf(f, nx, ny);
                Vec3f m = { c.x + kDx[side] * kHalf, 0, c.z + kDy[side] * kHalf };
                f32 px = kDy[side] != 0 ? kHalf : 0.0f, pz = kDx[side] != 0 ? kHalf : 0.0f;
                if (other < roof) {
                    // Close the step between the two roofs with bark
                    Emit({ m.x - px, other, m.z - pz }, { m.x + px, other, m.z + pz }, { m.x + px, roof, m.z + pz },
                         { m.x - px, roof, m.z - pz }, Van::Bark, 4.0f, (roof - other) / 80.0f, false);
                }
                if (space == HuskSpace::Corridor && HuskSpaceAt(f, nx, ny) != HuskSpace::Corridor) {
                    // A threshold: root posts either side of the opening and a lintel across it
                    for (int e = -1; e <= 1; e += 2) {
                        Vec3f pc = { m.x + e * (px - 14.0f * (px != 0)), y0, m.z + e * (pz - 14.0f * (pz != 0)) };
                        pc.x -= kDx[side] * 10.0f;
                        pc.z -= kDy[side] * 10.0f;
                        Box(pc, 16.0f, roof - y0, Van::Bark, -1, (roof - y0) / 80.0f);
                    }
                    f32 l0 = roof - 36.0f;
                    Vec3f mm = { m.x - kDx[side] * 4.0f, 0, m.z - kDy[side] * 4.0f };
                    Emit({ mm.x - px, l0, mm.z - pz }, { mm.x + px, l0, mm.z + pz }, { mm.x + px, roof, mm.z + pz },
                         { mm.x - px, roof, mm.z - pz }, Van::Bark, 4.0f, 0.45f, false);
                    Emit({ mm.x - px, roof - 110.0f, mm.z - pz }, { mm.x + px, roof - 110.0f, mm.z + pz },
                         { mm.x + px, l0, mm.z + pz }, { mm.x - px, l0, mm.z - pz }, Van::Roots, 4.0f, 1.0f, true, true);
                }
            }
            // What stands on the tile
            switch (t) {
                case 'T':
                case 't': {
                    bool lit = TorchLit(f, x, y);
                    sProps.push_back({ Prop::Torch, c, 0, 1.7f, 0, lit });
                    if (lit) {
                        sLights.push_back({ c.x, c.y + 100.0f, c.z });
                    }
                    break;
                }
                case 'H':
                    sProps.push_back({ Prop::Stone, c, FaceOpen(f, x, y), 0.15f, 0, false }); // 1.5x OoT's own
                    break;
                case 'W':
                    if (!Has(sState.webs, f, x, y)) {
                        sProps.push_back({ Prop::WallWeb, c, AcrossYaw(f, x, y), kTile + 16.0f, roof - y0, false });
                    }
                    break;
                case 'w':
                    if (!greatWebDrawn && !Has(sState.webs, f, x, y)) {
                        // The Great Web, the tree's own, stretched across the whole shaft
                        greatWebDrawn = true;
                        int n = 0;
                        f32 sx = 0, sz = 0;
                        const HuskFloor& fl = HuskFloorOf(f);
                        int x0 = 99, x1 = -1;
                        for (int wy = 0; wy < fl.Height(); wy++) {
                            for (int wx = 0; wx < fl.Width(); wx++) {
                                if (HuskTile(f, wx, wy) == 'w') {
                                    Vec3f wc = CentreOf(f, wx, wy);
                                    sx += wc.x;
                                    sz += wc.z;
                                    n++;
                                    x0 = std::min(x0, wx);
                                    x1 = std::max(x1, wx);
                                }
                            }
                        }
                        sProps.push_back({ Prop::FloorWeb, { sx / n, y0 - 2.0f, sz / n }, 0,
                                           (x1 - x0 + 1) * kTile + 40.0f, 0, false });
                    }
                    break;
                case 'G':
                    if (!Husk_GateOpen()) {
                        // A membrane of sinew across the way: the tree's web texture, gone red
                        s16 yaw = AcrossYaw(f, x, y);
                        f32 ex = yaw == 0 ? kHalf : 0.0f, ez = yaw == 0 ? 0.0f : kHalf;
                        size_t first = sQuads.size();
                        Emit({ c.x - ex, y0, c.z - ez }, { c.x + ex, y0, c.z + ez }, { c.x + ex, roof, c.z + ez },
                             { c.x - ex, roof, c.z - ez }, Van::GreatWeb, 2.0f, (roof - y0) / 80.0f, true);
                        Tint(first, 0.95f, 0.3f, 0.45f);
                    }
                    break;
                case 'C':
                case 'k':
                    sProps.push_back({ Prop::Chest, c, FaceOpen(f, x, y), 0.013f, 0, Has(sState.chests, f, x, y) });
                    break;
                case 'X':
                    if (!Has(sState.sacs, f, x, y)) {
                        sProps.push_back({ Prop::Sac, c, 0, 130.0f, 0, false });
                    }
                    break;
                case 'U': {
                    // Vines hanging down the shaft from the floor above
                    size_t first = sQuads.size();
                    Cross(f, x, y, Van::Ivy, 0.0f, roof - y0 + 520.0f, 34.0f, (roof - y0 + 520.0f) / 80.0f);
                    Tint(first, 0.8f, 0.85f, 0.8f);
                    break;
                }
                case 'B': {
                    // Thorny roots knotted low across the tile
                    size_t first = sQuads.size();
                    Cross(f, x, y, Van::Ivy, 0.0f, 64.0f, kHalf * 0.85f, 0.8f);
                    Tint(first, 0.85f, 0.45f, 0.3f);
                    break;
                }
                case 'i':
                    FloorDecal(f, x, y, HuskTex::Mark, 26.0f);
                    break;
                case 'A':
                    if (Has(sState.wiresFound, f, x, y)) {
                        FloorDecal(f, x, y, HuskTex::Mark, 34.0f);
                    }
                    break;
                case 'S':
                case 'Q':
                    FloorDecal(f, x, y, HuskTex::Glow, 50.0f);
                    break;
                default:
                    break;
            }
        }
    }
}

// Light: the dark swallows everything a few tiles off; lit torches and a burning stick throw warm light
void Shade(const Vec3f& p, u8 out[3]) {
    f32 dx = p.x - sEye.x, dy = p.y - sEye.y, dz = p.z - sEye.z;
    f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
    f32 base = std::clamp(1.1f - d / 1000.0f, 0.04f, 1.0f);
    // the tree's own cast: a little green in the dark
    f32 r = base * 0.86f, g = base * 0.92f, b = base * 0.74f;
    for (const Vec3f& l : sLights) {
        f32 lx = p.x - l.x, ly = p.y - l.y, lz = p.z - l.z;
        f32 ld = sqrtf(lx * lx + ly * ly + lz * lz);
        f32 k = std::clamp(1.0f - ld / 520.0f, 0.0f, 1.0f);
        k *= k;
        r += k * 0.75f;
        g += k * 0.45f;
        b += k * 0.15f;
    }
    out[0] = (u8)(std::min(r, 1.0f) * 255.0f);
    out[1] = (u8)(std::min(g, 1.0f) * 255.0f);
    out[2] = (u8)(std::min(b, 1.0f) * 255.0f);
}

void SetupMaterial(Gfx*& g, int tex, bool xlu, bool clampT) {
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    if (xlu) {
        gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    } else {
        gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }
    gDPSetCombineLERP(g++, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    u32 cmt = clampT ? G_TX_CLAMP : G_TX_WRAP;
    if (tex >= kVanBase) {
        const Van::Info& v = Van::kInfo[tex - kVanBase];
        if (v.fmt == G_IM_FMT_CI) {
            gDPSetTextureLUT(g++, G_TT_RGBA16);
            gDPLoadTLUT_pal256(g++, Van::kTlut);
            gDPLoadTextureBlock(g++, v.path, G_IM_FMT_CI, G_IM_SIZ_8b, v.w, v.h, 0, G_TX_WRAP, cmt, v.maskS,
                                v.maskT, G_TX_NOLOD, G_TX_NOLOD);
        } else {
            gDPSetTextureLUT(g++, G_TT_NONE);
            gDPLoadTextureBlock(g++, v.path, v.fmt, G_IM_SIZ_16b, v.w, v.h, 0, G_TX_WRAP, cmt, v.maskS, v.maskT,
                                G_TX_NOLOD, G_TX_NOLOD);
        }
        return;
    }
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPLoadTextureBlock(g++, HuskTex::kAll[tex], G_IM_FMT_RGBA, G_IM_SIZ_32b, HuskTex::kSize, HuskTex::kSize, 0,
                        G_TX_WRAP, G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
}

void TexSize(int tex, int& w, int& h) {
    if (tex >= kVanBase) {
        w = Van::kInfo[tex - kVanBase].w;
        h = Van::kInfo[tex - kVanBase].h;
    } else {
        w = h = HuskTex::kSize;
    }
}

void DrawQuads(PlayState* play, Gfx*& g, bool xlu) {
    std::vector<int> texIds;
    for (int t = 0; t < HuskTex::Count; t++) {
        texIds.push_back(t);
    }
    for (int t = kVanBase; t < Van::End; t++) {
        texIds.push_back(t);
    }
    for (int tex : texIds) {
        for (int clampPass = 0; clampPass < 2; clampPass++) {
            std::vector<const Quad*> list;
            for (const Quad& q : sQuads) {
                if (q.tex == tex && q.xlu == xlu && q.clampT == (clampPass == 1)) {
                    list.push_back(&q);
                }
            }
            if (list.empty()) {
                continue;
            }
            SetupMaterial(g, tex, xlu, clampPass == 1);
            int tw, th;
            TexSize(tex, tw, th);
            for (size_t i = 0; i < list.size(); i += 8) {
                size_t n = std::min<size_t>(8, list.size() - i);
                Vtx* v = (Vtx*)Graph_Alloc(play->state.gfxCtx, sizeof(Vtx) * 4 * n);
                for (size_t k = 0; k < n; k++) {
                    const Quad& q = *list[i + k];
                    for (int c = 0; c < 4; c++) {
                        Vtx& o = v[k * 4 + c];
                        u8 rgb[3];
                        Shade(q.p[c], rgb);
                        o.v.ob[0] = (s16)lroundf(q.p[c].x);
                        o.v.ob[1] = (s16)lroundf(q.p[c].y);
                        o.v.ob[2] = (s16)lroundf(q.p[c].z);
                        o.v.flag = 0;
                        o.v.tc[0] = (s16)(q.s[c] * tw * 32.0f);
                        o.v.tc[1] = (s16)(q.t[c] * th * 32.0f);
                        o.v.cn[0] = (u8)std::min(255.0f, rgb[0] * q.tr);
                        o.v.cn[1] = (u8)std::min(255.0f, rgb[1] * q.tg);
                        o.v.cn[2] = (u8)std::min(255.0f, rgb[2] * q.tb);
                        o.v.cn[3] = 255;
                    }
                }
                gSPVertex(g++, (uintptr_t)v, (u32)(n * 4), 0);
                for (size_t k = 0; k < n; k++) {
                    u32 b = (u32)(k * 4);
                    gSP2Triangles(g++, b, b + 1, b + 2, 0, b, b + 2, b + 3, 0);
                }
            }
        }
    }
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_NONE);
}

// ---- The world actor ------------------------------------------------------------------------------------
struct World {
    DynaPolyActor dyna;
};

void World_Init(Actor* thisx, PlayState* play) {
    World* self = (World*)thisx;
    Actor_SetScale(thisx, 1.0f);
    thisx->world.pos = thisx->home.pos = { 0.0f, 0.0f, 0.0f };
    thisx->shape.rot = thisx->world.rot = { 0, 0, 0 };
    DynaPolyActor_Init(&self->dyna, 0);
    BuildCollision(thisx->params);
    self->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, &sCol.header);
}

void World_Destroy(Actor* thisx, PlayState* play) {
    World* self = (World*)thisx;
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, self->dyna.bgId);
    if (sWorld == thisx) {
        sWorld = nullptr;
    }
}

void World_Update(Actor*, PlayState* play) {
    HuskProps_Update(play);
}

void World_Draw(Actor* thisx, PlayState* play) {
    Camera* cam = Play_GetCamera(play, CAM_ID_MAIN);
    sEye = cam->eye;
    sEyeYaw = Math_Vec3f_Yaw(&cam->eye, &cam->at);
    BuildQuads(play);
    // (straight into the display lists: OPEN_DISPS can't be used inside an anonymous namespace)
    GraphicsContext* gfx = play->state.gfxCtx;
    Gfx* opa = gfx->polyOpa.p;
    gSPMatrix(opa++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    DrawQuads(play, opa, false);
    gfx->polyOpa.p = opa;
    Gfx* xlu = gfx->polyXlu.p;
    gSPMatrix(xlu++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    DrawQuads(play, xlu, true);
    gfx->polyXlu.p = xlu;
    // OoT's own props on top
    for (const Prop& p : sProps) {
        switch (p.kind) {
            case Prop::Torch:
                HuskProps_Torch(play, p.p.x, p.p.y, p.p.z, p.a, p.flag ? 1 : 0);
                break;
            case Prop::WallWeb:
                HuskProps_WallWeb(play, p.p.x, p.p.y, p.p.z, p.yaw, p.a, p.b);
                break;
            case Prop::FloorWeb:
                HuskProps_FloorWeb(play, p.p.x, p.p.y, p.p.z, p.a);
                break;
            case Prop::Stone:
                HuskProps_SheikahStone(play, p.p.x, p.p.y, p.p.z, p.yaw, p.a);
                break;
            case Prop::Sac:
                HuskProps_EggSac(play, p.p.x, p.p.y, p.p.z, p.a);
                break;
            case Prop::Chest:
                HuskProps_Chest(play, p.p.x, p.p.y, p.p.z, p.yaw, p.a, p.flag ? 1 : 0);
                break;
        }
    }
}

int WorldActorId() {
    if (sWorldActorId < 0) {
        ActorDBInit init;
        init.name = "Nocturne_HuskWorld";
        init.desc = "Nocturne of Time: a Hollowed Husk floor";
        init.category = ACTORCAT_BG;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(World);
        init.init = World_Init;
        init.destroy = World_Destroy;
        init.update = World_Update;
        init.draw = World_Draw;
        sWorldActorId = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return sWorldActorId;
}

// Rebuild the current floor's collision on the world actor (a new floor, a web burned, the gate opened). The old
// collision is taken out first, so the one actor only ever holds one floor's.
void Rebuild(PlayState* play) {
    if (sWorld == nullptr) {
        return;
    }
    World* w = (World*)sWorld;
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, w->dyna.bgId);
    BuildCollision(sFloor);
    w->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, sWorld, &sCol.header);
}

void SpawnWorld(PlayState* play) {
    if (sWorld != nullptr) {
        Rebuild(play);
        return;
    }
    sWorld = Actor_Spawn(&play->actorCtx, play, WorldActorId(), 0.0f, 0.0f, 0.0f, 0, 0, 0, (s16)sFloor);
}

// ---- Moving Link around ---------------------------------------------------------------------------------
void PlaceLink(PlayState* play, Player* player) {
    Vec3f c = CentreOf(sArrival.floor, sArrival.x, sArrival.y);
    player->actor.world.pos = player->actor.prevPos = player->actor.home.pos = c;
    player->actor.velocity = { 0.0f, 0.0f, 0.0f };
    player->actor.speedXZ = 0.0f;
    player->linearVelocity = 0.0f;
    s16 yaw = (s16)(sArrival.facing * 0x4000);
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
}

void GoTo(PlayState* play, Player* player, int floor, int x, int y, s8 facing) {
    bool newFloor = floor != sFloor || sWorld == nullptr;
    sFloor = floor;
    sArrival = { true, floor, x, y, facing };
    if (newFloor) {
        SpawnWorld(play);
    }
    sPlaceFrames = 4;
    PlaceLink(play, player);
    DungeonCrawler_SnapFacing(facing);
}

void Warp(PlayState* play, s16 entrance) {
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
}

// Hurt every member by a share of their max HP, never the last HP
void HurtParty(f32 share) {
    for (PartyMember& m : Party_Members()) {
        Combatant e = Party_Effective(m);
        int dmg = std::max(1, (int)(e.maxHp * share));
        m.base.hp = std::max(1, std::min(m.base.hp, e.maxHp) - dmg);
    }
}

std::string FindLoot(int f, int x, int y) {
    // F1's east chest holds the Kokiri Ocarina, the answer to the Husk Weaver and the Queen
    if (f == 0 && x == 15 && y == 8 && Party_GiveRelic(RelicId::KokiriOcarina)) {
        return "Found the Kokiri Ocarina!\nA Relic: equip it in Link's SKILLS.";
    }
    auto rng = [] { return Rand_ZeroOne() * 0.99999f; };
    Item item = RollItem(4 + 2 * f, DropSource::Common, 0.3f + 0.1f * f, rng);
    Party_Bag().push_back(item);
    return "Found " + item.name + "!";
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------------

extern "C" int Husk_HidesRooms(void) {
    return gPlayState != NULL && Husk_Active(gPlayState) ? 1 : 0;
}

bool Husk_Active(PlayState* play) {
    // On again since the Etrian-style rebuild (Nov 1): the grid floors, dressed in the Deku Tree's own textures.
    // Nocturne.HuskFloors 0 goes back to crawling the vanilla tree.
    return play != NULL && ModOn() && CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.HuskFloors"), 1) != 0 &&
           play->sceneNum == SCENE_DEKU_TREE;
}

float Husk_TileSize() {
    return kTile;
}

int Husk_Floor() {
    return sFloor;
}

bool Husk_Frame(PlayState* play, Player* player) {
    if (sMsgTimer > 0 && --sMsgTimer == 0) {
        sMsg.clear();
    }
    // (A battle no longer puts the stick out: it made burning webs impossible with encounters about)
    if (!Battle_IsActive() && sStickOutInBattle) {
        sStickOutInBattle = false;
        Say("Your stick went out in the fight.", 0xF8380000u);
    }
    if (sWorld == nullptr) {
        // First frame in the scene: build the floor and put Link on it
        if (!sArrival.pending) {
            HuskSpot s;
            if (sState.loadFloor >= 0) {
                sArrival = { true, sState.loadFloor, sState.loadX, sState.loadY, 2 };
                sState.loadFloor = -1;
            } else if (HuskFind(0, 'S', s)) {
                sArrival = { true, 0, s.x, s.y, 2 };
            }
        }
        sFloor = sArrival.floor;
        SpawnWorld(play);
        sPlaceFrames = 4;
        DungeonCrawler_SnapFacing(sArrival.facing);
    }
    if (sPlaceFrames > 0) {
        PlaceLink(play, player);
        if (--sPlaceFrames == 0) {
            sArrival.pending = false;
        }
        return true;
    }
    return false;
}

bool Husk_CanStep(PlayState* play, const Vec3f& from, int facing, Vec3f& to) {
    int x, y, dx, dy;
    TileOf(from, x, y);
    DirOf(facing, dx, dy);
    int nx = x + dx, ny = y + dy;
    if (Solid(sFloor, nx, ny)) {
        char t = HuskTile(sFloor, nx, ny);
        if (t == 'C' || t == 'k') {
            Say(Has(sState.chests, sFloor, nx, ny) ? "An empty chest." : "A treasure chest. A: open it.");
        } else if (t == 'X') {
            Say("A swollen egg sac pulses in the way. A: tear it open.", 0xC8A0FF00u);
        }
        return false;
    }
    // Ledges: only ever crossed going south
    if (dy < 0 && (HuskTile(sFloor, nx, ny) == 'v' || HuskTile(sFloor, x, y) == 'v')) {
        return false;
    }
    to = CentreOf(sFloor, nx, ny);
    to.y = from.y;
    return true;
}

bool Husk_OnStepEnd(PlayState* play, Player* player) {
    int x, y;
    TileOf(player->actor.world.pos, x, y);
    int f = sFloor;
    char t = HuskTile(f, x, y);

    // The stick burns down a step at a time
    if (sStick > 0 && --sStick == 0) {
        Say("The stick burns out.", 0xF8380000u);
    }

    switch (t) {
        case 'i':
            if (!Has(sState.read, f, x, y)) {
                sState.read.insert({ f, x, y });
                Say("Carved in the bark: \"" + HuskEngraving(f, x, y) + "\"");
            }
            break;
        case 'B': {
            HurtParty(0.04f);
            Player_PlaySfx(&player->actor, NA_SE_PL_DAMAGE);
            Say("The thorns tear at the party.", 0xF8380000u);
            break;
        }
        case 'R': {
            bool any = false;
            for (PartyMember& m : Party_Members()) {
                if (m.base.hp > 0 && m.base.ailment == Ailment::None && !Party_Effective(m).ailmentWard &&
                    Rand_ZeroOne() < 0.2f) {
                    m.base.ailment = Ailment::Poison;
                    Say(m.base.name + " is poisoned by the rot!", 0xC080FF00u);
                    any = true;
                }
            }
            (void)any;
            break;
        }
        case 'w': {
            // Down through the burned Great Web, as in the old tree: a long drop, but soft roots below
            HuskSpot to;
            if (HuskLink(f, x, y, to)) {
                GoTo(play, player, to.floor, to.x, to.y, DungeonCrawler_GetFacing());
                bool pool = HuskTile(to.floor, to.x, to.y - 1) == '~' || HuskTile(to.floor, to.x - 1, to.y - 1) == '~';
                Player_PlaySfx(&player->actor, pool ? NA_SE_EV_DIVE_INTO_WATER_L : NA_SE_PL_BODY_HIT);
                Say(pool ? "The party drops through the hollow and lands among the roots of a black pool."
                         : "The party drops through the hollow to " + std::string(HuskFloorOf(to.floor).name) + ".");
                return true;
            }
            break;
        }
        case 'P': {
            HuskSpot to;
            if (HuskLink(f, x, y, to)) {
                sState.pitsFound.insert({ f, x, y });
                HurtParty(0.10f);
                Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
                GoTo(play, player, to.floor, to.x, to.y, DungeonCrawler_GetFacing());
                Say("The floor gives way! The party falls to " + std::string(HuskFloorOf(to.floor).name) + ".",
                    0xF8380000u);
                return true;
            }
            break;
        }
        case 'A':
            if (!sVisit.wiresTripped.count({ f, x, y })) {
                sVisit.wiresTripped.insert({ f, x, y });
                sState.wiresFound.insert({ f, x, y });
                Say("A tripwire! Something drops from the dark.", 0xF8380000u);
                Battle_StartTripwire(play, player);
                return true;
            }
            break;
        case 'E':
            if (!Has(sState.elites, f, x, y)) {
                // The Husk Weaver comes with the new enemies; until then a Skulltula brood guards the den
                sFixedKind = 'E';
                sFixedSpot = { f, x, y };
                Battle_StartFixed(play, player,
                                  { (int)EnemyKind::Skulltula, (int)EnemyKind::Skulltula, (int)EnemyKind::Skulltula });
                return true;
            }
            break;
        case 'Q':
            BossGate_EnterArena(play);
            return true;
        case 'D':
            Say("A hole into the dark below. A: climb down.");
            return true;
        case 'U':
            Say("Roots climb up toward the floor above. A: climb up.");
            return true;
        case 'S':
            Say("Daylight, far above. A: leave the Husk.");
            return true;
        default:
            break;
    }
    if (t == 'L' || t == 'U' || t == 'D') {
        return true;
    }
    return false;
}

bool Husk_OnA(PlayState* play, Player* player, int facing) {
    int x, y, dx, dy;
    TileOf(player->actor.world.pos, x, y);
    DirOf(facing, dx, dy);
    int f = sFloor, fx = x + dx, fy = y + dy;
    char here = HuskTile(f, x, y), ahead = HuskTile(f, fx, fy);

    // Ways up and down, and out
    if (here == 'D' || here == 'U') {
        HuskSpot to;
        if (HuskLink(f, x, y, to)) {
            GoTo(play, player, to.floor, to.x, to.y, DungeonCrawler_GetFacing());
            Say(std::string(here == 'D' ? "Down into " : "Up to ") + HuskFloorOf(to.floor).name + ".");
            return true;
        }
    }
    if (here == 'S') {
        sVisit = {};
        Warp(play, ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE);
        return true;
    }

    // Fire
    if (ahead == 'T' || (ahead == 't' && Has(sState.torches, f, fx, fy))) {
        if (sStick > 0) {
            sStick = kStickSteps;
            Say("Your stick blazes anew!", 0xF8B80000u);
        } else if (Party_Consumables()[(size_t)ConsumableId::DekuStick] > 0) {
            Party_Consumables()[(size_t)ConsumableId::DekuStick]--;
            sStick = kStickSteps;
            Say("Your stick blazes! It will burn for " + std::to_string(kStickSteps) + " steps.", 0xF8B80000u);
        } else {
            Say("A torch. You have no Deku Sticks to light.");
            return true;
        }
        Audio_PlaySfxGeneral(NA_SE_EV_FLAME_IGNITION, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return true;
    }
    if (ahead == 't') {
        if (sStick > 0) {
            sState.torches.insert({ f, fx, fy });
            Say("The torch catches!", 0xF8B80000u);
            Audio_PlaySfxGeneral(NA_SE_EV_FLAME_IGNITION, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else {
            Say("A cold torch. A burning stick would light it.");
        }
        return true;
    }
    if (ahead == 'W' && !Has(sState.webs, f, fx, fy)) {
        if (sStick > 0) {
            sState.webs.insert({ f, fx, fy });
            Say("The web shrivels in the flame!", 0xF8B80000u);
            Audio_PlaySfxGeneral(NA_SE_EV_FLAME_IGNITION, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Rebuild(play);
        } else {
            Say("A thick web blocks the way. Fire would burn it.");
        }
        return true;
    }
    if (ahead == 'C' || ahead == 'k') {
        if (Has(sState.chests, f, fx, fy)) {
            Say("The chest is empty.");
            return true;
        }
        sState.chests.insert({ f, fx, fy });
        if (ahead == 'k') {
            int got = Party_AddConsumable(ConsumableId::DekuStick, 5);
            Say(got > 0 ? "Found " + std::to_string(got) + " Deku Sticks!" : "Deku Sticks... but you can't carry more.",
                0xF8B80000u);
        } else {
            Say(FindLoot(f, fx, fy), 0xF8B80000u);
        }
        Vec3f c = CentreOf(f, fx, fy);
        HuskProps_OpenChest(c.x, c.y, c.z);
        Audio_PlaySfxGeneral(NA_SE_EV_TBOX_OPEN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return true;
    }
    if (ahead == 'X' && !Has(sState.sacs, f, fx, fy)) {
        sFixedKind = 'X';
        sFixedSpot = { f, fx, fy };
        std::vector<int> larvae = { (int)EnemyKind::GohmaLarva, (int)EnemyKind::GohmaLarva };
        if (Moon_Phase() >= MoonPhase::SixEighths) {
            larvae.push_back((int)EnemyKind::GohmaLarva);
        }
        Say("The sac splits open!", 0xC8A0FF00u);
        Battle_StartFixed(play, player, larvae);
        return true;
    }
    if (ahead == 'w' && !Has(sState.webs, f, fx, fy)) {
        if (sStick > 0) {
            // The whole web goes up at once
            const HuskFloor& fl = HuskFloorOf(f);
            for (int wy = 0; wy < fl.Height(); wy++) {
                for (int wx = 0; wx < fl.Width(); wx++) {
                    if (HuskTile(f, wx, wy) == 'w') {
                        sState.webs.insert({ f, wx, wy });
                    }
                }
            }
            Say("The Great Web goes up in flame! The hollow below lies open.", 0xF8B80000u);
            Audio_PlaySfxGeneral(NA_SE_EV_FLAME_IGNITION, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Rebuild(play);
        } else {
            Say("The Great Web, stretched across the hollow. Fire would burn it.");
        }
        return true;
    }
    if (ahead == 'G' && !Husk_GateOpen()) {
        Say("A membrane of flesh seals the way. Sacs destroyed: " + std::to_string(SacsDestroyed()) + "/3.",
            0xC8A0FF00u);
        return true;
    }
    if (ahead == 'H') {
        bool first = !sVisit.stonesUsed.count({ f, fx, fy });
        sVisit.stonesUsed.insert({ f, fx, fy });
        if (first) {
            Party_HealAll();
        }
        if (f == 2) {
            sState.stoneF3 = true;
        }
        sState.loadFloor = f;
        sState.loadX = x;
        sState.loadY = y;
        sSavingAtStone = true;
        Play_PerformSave(play);
        sSavingAtStone = false;
        sState.loadFloor = -1; // only a reload uses it
        Say(first ? "The Sheikah Stone hums. The party is healed. Saved." : "The Sheikah Stone hums. Saved.",
            0x3CBCFC00u);
        Audio_PlaySfxGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return true;
    }
    return false;
}

const char* Husk_Message() {
    return sMsg.empty() ? nullptr : sMsg.c_str();
}

uint32_t Husk_MessageColor() {
    return sMsgColor;
}

int Husk_StickSteps() {
    return sStick;
}

bool Husk_GateOpen() {
    return SacsDestroyed() >= 3;
}

bool Husk_GatePosition(float& x, float& y, float& z) {
    HuskSpot g;
    if (sFloor != 2 || !HuskFind(2, 'G', g)) {
        return false;
    }
    Vec3f c = CentreOf(2, g.x, g.y);
    x = c.x;
    y = c.y;
    z = c.z;
    return true;
}

void Husk_EnterBattleStage(PlayState* play, Player* player) {
    if (!Husk_Active(play) || sStage.active || sWorld == nullptr) {
        return;
    }
    sStage.active = true;
    sStage.returnPos = player->actor.world.pos;
    sStage.returnFacing = DungeonCrawler_GetFacing();
    Vec3f p = { kStageX, FloorY(sFloor), kStageZ };
    player->actor.world.pos = player->actor.prevPos = p;
    player->actor.velocity = { 0.0f, 0.0f, 0.0f };
    DungeonCrawler_SnapFacing(2); // the chamber opens to the north
}

void Husk_OnBattleEnd(bool victory) {
    if (sStage.active) {
        sStage.active = false;
        if (gPlayState != NULL) {
            Player* player = GET_PLAYER(gPlayState);
            player->actor.world.pos = player->actor.prevPos = sStage.returnPos;
            player->actor.velocity = { 0.0f, 0.0f, 0.0f };
            DungeonCrawler_SnapFacing(sStage.returnFacing);
        }
    }
    if (sFixedKind == 0) {
        return;
    }
    char kind = sFixedKind;
    sFixedKind = 0;
    if (!victory) {
        return;
    }
    if (kind == 'X') {
        sState.sacs.insert(sFixedSpot);
        int n = SacsDestroyed();
        if (n >= 3) {
            Say("The membrane tears open! The way to the Queen is clear.", 0x3CBCFC00u);
            if (gPlayState != NULL) {
                Rebuild(gPlayState);
            }
        } else {
            Say("The membrane quivers... (" + std::to_string(n) + "/3)", 0xC8A0FF00u);
        }
    } else if (kind == 'E') {
        sState.elites.insert(sFixedSpot);
        Say("The den falls quiet.", 0x3CBCFC00u);
    }
}

void Husk_OnBossResult(bool won) {
    HuskSpot q;
    HuskFind(2, 'Q', q);
    if (won) {
        sArrival = { true, 2, q.x, q.y - 1, 2 }; // just inside the gate, facing away from the arena
    } else if (sState.stoneF3) {
        HuskSpot h;
        HuskFind(2, 'H', h);
        sArrival = { true, 2, h.x + 1, h.y, 3 }; // beside the stone, facing it
    } else {
        sArrival = {};
        sVisit = {};
    }
}

void Husk_Reset() {
    sState = {};
    sVisit = {};
    sFloor = 0;
    sStick = 0;
    sMsg.clear();
    sMsgTimer = 0;
    sArrival = {};
    sFixedKind = 0;
}

namespace {
nlohmann::json SpotsToJson(const std::set<Spot>& s) {
    nlohmann::json a = nlohmann::json::array();
    for (const Spot& p : s) {
        a.push_back({ std::get<0>(p), std::get<1>(p), std::get<2>(p) });
    }
    return a;
}

void SpotsFromJson(const nlohmann::json& j, const char* key, std::set<Spot>& out) {
    out.clear();
    if (!j.contains(key) || !j[key].is_array()) {
        return;
    }
    for (const nlohmann::json& p : j[key]) {
        if (p.is_array() && p.size() == 3) {
            out.insert({ p[0].get<int>(), p[1].get<int>(), p[2].get<int>() });
        }
    }
}
} // namespace

nlohmann::json Husk_Save() {
    nlohmann::json j = { { "chests", SpotsToJson(sState.chests) }, { "webs", SpotsToJson(sState.webs) },
                         { "torches", SpotsToJson(sState.torches) }, { "read", SpotsToJson(sState.read) },
                         { "sacs", SpotsToJson(sState.sacs) },       { "elites", SpotsToJson(sState.elites) },
                         { "pits", SpotsToJson(sState.pitsFound) },  { "wires", SpotsToJson(sState.wiresFound) },
                         { "stoneF3", sState.stoneF3 }, { "layout", kLayoutVersion } };
    // A save made at a Sheikah Stone reloads beside it; any other save starts at the entrance
    if (sSavingAtStone) {
        j["loadAt"] = { sState.loadFloor, sState.loadX, sState.loadY };
    }
    return j;
}

void Husk_Load(const nlohmann::json& j) {
    Husk_Reset();
    if (!j.is_object()) {
        return;
    }
    SpotsFromJson(j, "chests", sState.chests);
    SpotsFromJson(j, "webs", sState.webs);
    SpotsFromJson(j, "torches", sState.torches);
    SpotsFromJson(j, "read", sState.read);
    SpotsFromJson(j, "sacs", sState.sacs);
    SpotsFromJson(j, "elites", sState.elites);
    SpotsFromJson(j, "pits", sState.pitsFound);
    SpotsFromJson(j, "wires", sState.wiresFound);
    sState.stoneF3 = j.value("stoneF3", false);
    if (j.value("layout", 1) < kLayoutVersion) {
        // F1 was rebuilt: what was opened or burned there was on the old grid
        for (std::set<Spot>* set : { &sState.chests, &sState.webs, &sState.torches, &sState.read, &sState.pitsFound,
                                     &sState.wiresFound }) {
            for (auto it = set->begin(); it != set->end();) {
                it = std::get<0>(*it) == 0 ? set->erase(it) : std::next(it);
            }
        }
    }
    if (j.contains("loadAt") && j["loadAt"].is_array() && j["loadAt"].size() == 3) {
        sState.loadFloor = j["loadAt"][0].get<int>();
        sState.loadX = j["loadAt"][1].get<int>();
        sState.loadY = j["loadAt"][2].get<int>();
    }
}

// ---- Hooks ----------------------------------------------------------------------------------------------
namespace {

void RegisterHusk() {
    bool enabled = ModOn();
    // No vanilla actors in the Husk: its enemies, chests, doors and webs are the grid's now
    COND_HOOK(ShouldActorInit, enabled, [](void* actorRef, bool* result) {
        Actor* actor = (Actor*)actorRef;
        if (gPlayState == NULL || !Husk_Active(gPlayState)) {
            return;
        }
        if (actor->id < ACTOR_ID_MAX && actor->id != ACTOR_PLAYER && actor->id != ACTOR_EN_ELF) {
            *result = false;
        }
    });
    COND_HOOK(OnSceneInit, enabled, [](int16_t scene) {
        sWorld = nullptr; // the old scene's actors are gone
        sStage = {};
        HuskProps_Reset();
        sPlaceFrames = 0;
        sStick = 0;
        if (scene != SCENE_DEKU_TREE && scene != SCENE_DEKU_TREE_BOSS) {
            sVisit = {}; // out of the dungeon: tripwires re-arm, stones heal again
            sArrival = {};
        }
    });
}

RegisterShipInitFunc initFunc(RegisterHusk, { CVAR_ENHANCEMENT("Nocturne.DungeonCrawler") });

} // namespace
