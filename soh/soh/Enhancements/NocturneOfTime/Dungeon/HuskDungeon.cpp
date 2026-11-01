#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ActorDB.h"
#include "soh/cvar_prefixes.h"

#include "HuskDungeon.h"
#include "HuskFloors.h"
#include "HuskTextures.h"
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
// Far from the vanilla tree (outside its collision box), on the automap's 80-unit tiles; each floor at its own
// height so the automap keeps them apart.
constexpr f32 kTile = 80.0f;
constexpr f32 kHalf = 40.0f;
constexpr f32 kWallH = 120.0f;
constexpr f32 kOriginX = 6000.0f, kOriginZ = 6000.0f;
constexpr f32 kTopY = 3000.0f, kFloorStep = 600.0f;
constexpr int kStickSteps = 12;
constexpr int kMessageFrames = 90; // 4.5 seconds
constexpr int kDrawRadius = 7;     // tiles drawn around the camera

f32 FloorY(int floor) {
    return kTopY - kFloorStep * floor;
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
    if (t == 'W') {
        return !Has(sState.webs, f, x, y);
    }
    if (t == 'G') {
        return !Husk_GateOpen();
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
    f32 y0 = FloorY(f), y1 = y0 + kWallH;
    auto walk = [&](int x, int y) { return !Solid(f, x, y); };
    // Floor: one quad under the whole grid
    {
        f32 x0 = kOriginX - kHalf, z0 = kOriginZ - kHalf;
        f32 x1 = x0 + fl.Width() * kTile, z1 = z0 + fl.Height() * kTile;
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
struct Quad {
    Vec3f p[4];
    f32 s[4], t[4];
    int tex;
    bool xlu;
};
std::vector<Quad> sQuads;
Vec3f sEye;
s16 sEyeYaw;

void Emit(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, int tex, f32 sMax, f32 tMax, bool xlu) {
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
    sQuads.push_back(q);
}

// A flat quad on the floor of a tile (slightly raised so it doesn't fight the floor)
void FloorDecal(int f, int x, int y, int tex, f32 size = kHalf, f32 lift = 0.6f) {
    Vec3f c = CentreOf(f, x, y);
    f32 h = c.y + lift;
    Emit({ c.x - size, h, c.z + size }, { c.x + size, h, c.z + size }, { c.x + size, h, c.z - size },
         { c.x - size, h, c.z - size }, tex, 1, 1, true);
}

// Two upright quads crossing at the tile's centre (webs, the gate, vines)
void Cross(int f, int x, int y, int tex, f32 h0, f32 h1, f32 half = kHalf) {
    Vec3f c = CentreOf(f, x, y);
    Emit({ c.x - half, c.y + h0, c.z }, { c.x + half, c.y + h0, c.z }, { c.x + half, c.y + h1, c.z },
         { c.x - half, c.y + h1, c.z }, tex, 1, 1, true);
    Emit({ c.x, c.y + h0, c.z - half }, { c.x, c.y + h0, c.z + half }, { c.x, c.y + h1, c.z + half },
         { c.x, c.y + h1, c.z - half }, tex, 1, 1, true);
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

void BuildQuads(PlayState* play) {
    sQuads.clear();
    int f = sFloor;
    int cx, cy;
    TileOf(sEye, cx, cy);
    for (int y = cy - kDrawRadius; y <= cy + kDrawRadius; y++) {
        for (int x = cx - kDrawRadius; x <= cx + kDrawRadius; x++) {
            char t = HuskTile(f, x, y);
            if (t == '#') {
                continue;
            }
            Vec3f c = CentreOf(f, x, y);
            f32 y0 = c.y, y1 = c.y + kWallH;
            // Floor and ceiling under everything that isn't a wall block
            Emit({ c.x - kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z - kHalf },
                 { c.x - kHalf, y0, c.z - kHalf }, HuskTex::Floor, 1, 1, false);
            Emit({ c.x - kHalf, y1, c.z - kHalf }, { c.x + kHalf, y1, c.z - kHalf }, { c.x + kHalf, y1, c.z + kHalf },
                 { c.x - kHalf, y1, c.z + kHalf }, HuskTex::Ceiling, 1, 1, false);
            // Walls toward neighbouring wall blocks
            if (HuskTile(f, x, y - 1) == '#') {
                Emit({ c.x + kHalf, y0, c.z - kHalf }, { c.x - kHalf, y0, c.z - kHalf },
                     { c.x - kHalf, y1, c.z - kHalf }, { c.x + kHalf, y1, c.z - kHalf }, HuskTex::Wall, 1, 1.5f, false);
            }
            if (HuskTile(f, x, y + 1) == '#') {
                Emit({ c.x - kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z + kHalf },
                     { c.x + kHalf, y1, c.z + kHalf }, { c.x - kHalf, y1, c.z + kHalf }, HuskTex::Wall, 1, 1.5f, false);
            }
            if (HuskTile(f, x - 1, y) == '#') {
                Emit({ c.x - kHalf, y0, c.z - kHalf }, { c.x - kHalf, y0, c.z + kHalf },
                     { c.x - kHalf, y1, c.z + kHalf }, { c.x - kHalf, y1, c.z - kHalf }, HuskTex::Wall, 1, 1.5f, false);
            }
            if (HuskTile(f, x + 1, y) == '#') {
                Emit({ c.x + kHalf, y0, c.z + kHalf }, { c.x + kHalf, y0, c.z - kHalf },
                     { c.x + kHalf, y1, c.z - kHalf }, { c.x + kHalf, y1, c.z + kHalf }, HuskTex::Wall, 1, 1.5f, false);
            }
            // What stands on the tile
            switch (t) {
                case 'T':
                case 't':
                    Box(c, 12.0f, 70.0f, HuskTex::Post, HuskTex::Post, 2.0f);
                    if (TorchLit(f, x, y)) {
                        Billboard({ c.x, c.y + 66.0f, c.z }, 30.0f, 34.0f,
                                  (play->gameplayFrames / 3) % 2 ? HuskTex::Flame0 : HuskTex::Flame1);
                    }
                    break;
                case 'H':
                    Box(c, 18.0f, 64.0f, HuskTex::Stone, HuskTex::Stone, 1.0f);
                    break;
                case 'W':
                    if (!Has(sState.webs, f, x, y)) {
                        Cross(f, x, y, HuskTex::Web, 0.0f, kWallH);
                    }
                    break;
                case 'G':
                    if (!Husk_GateOpen()) {
                        Cross(f, x, y, HuskTex::Gate, 0.0f, kWallH);
                    }
                    break;
                case 'C':
                case 'k': {
                    bool open = Has(sState.chests, f, x, y);
                    Box(c, 16.0f, 22.0f, HuskTex::Chest, open ? HuskTex::ChestOpen : HuskTex::Chest, 0.6f);
                    break;
                }
                case 'X':
                    if (!Has(sState.sacs, f, x, y)) {
                        Billboard(c, 70.0f, 70.0f, HuskTex::Sac);
                    }
                    break;
                case 'U':
                    Cross(f, x, y, HuskTex::Vines, 0.0f, kWallH, 20.0f);
                    break;
                case 'D':
                    FloorDecal(f, x, y, HuskTex::Hole);
                    break;
                case 'P':
                    if (HuskFloorOf(f).visiblePits || Has(sState.pitsFound, f, x, y)) {
                        FloorDecal(f, x, y, HuskTex::Hole);
                    }
                    break;
                case 'B':
                    FloorDecal(f, x, y, HuskTex::Brambles);
                    break;
                case 'R':
                    FloorDecal(f, x, y, HuskTex::Rot);
                    break;
                case 'i':
                    FloorDecal(f, x, y, HuskTex::Mark, 24.0f);
                    break;
                case 'A':
                    if (Has(sState.wiresFound, f, x, y)) {
                        FloorDecal(f, x, y, HuskTex::Mark, 30.0f);
                    }
                    break;
                case 'S':
                case 'Q':
                    FloorDecal(f, x, y, HuskTex::Glow);
                    break;
                default:
                    break;
            }
        }
    }
}

// Light: darker with distance, warmer near lit torches
u8 Shade(const Vec3f& p) {
    f32 dx = p.x - sEye.x, dy = p.y - sEye.y, dz = p.z - sEye.z;
    f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
    f32 b = std::clamp(1.15f - d / 480.0f, 0.06f, 1.0f);
    return (u8)(b * 255.0f);
}

void SetupMaterial(Gfx*& g, int tex, bool xlu) {
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
    gDPLoadTextureBlock(g++, HuskTex::kAll[tex], G_IM_FMT_RGBA, G_IM_SIZ_32b, HuskTex::kSize, HuskTex::kSize, 0,
                        G_TX_WRAP, G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
}

void DrawQuads(PlayState* play, Gfx*& g, bool xlu) {
    for (int tex = 0; tex < HuskTex::Count; tex++) {
        std::vector<const Quad*> list;
        for (const Quad& q : sQuads) {
            if (q.tex == tex && q.xlu == xlu) {
                list.push_back(&q);
            }
        }
        if (list.empty()) {
            continue;
        }
        SetupMaterial(g, tex, xlu);
        for (size_t i = 0; i < list.size(); i += 8) {
            size_t n = std::min<size_t>(8, list.size() - i);
            Vtx* v = (Vtx*)Graph_Alloc(play->state.gfxCtx, sizeof(Vtx) * 4 * n);
            for (size_t k = 0; k < n; k++) {
                const Quad& q = *list[i + k];
                for (int c = 0; c < 4; c++) {
                    Vtx& o = v[k * 4 + c];
                    u8 s = Shade(q.p[c]);
                    o.v.ob[0] = (s16)lroundf(q.p[c].x);
                    o.v.ob[1] = (s16)lroundf(q.p[c].y);
                    o.v.ob[2] = (s16)lroundf(q.p[c].z);
                    o.v.flag = 0;
                    o.v.tc[0] = (s16)(q.s[c] * HuskTex::kSize * 32.0f);
                    o.v.tc[1] = (s16)(q.t[c] * HuskTex::kSize * 32.0f);
                    o.v.cn[0] = s;
                    o.v.cn[1] = s;
                    o.v.cn[2] = s;
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

void World_Update(Actor*, PlayState*) {
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
    return play != NULL && ModOn() && play->sceneNum == SCENE_DEKU_TREE;
}

int Husk_Floor() {
    return sFloor;
}

bool Husk_Frame(PlayState* play, Player* player) {
    if (sMsgTimer > 0 && --sMsgTimer == 0) {
        sMsg.clear();
    }
    // A battle puts the stick out
    if (Battle_IsActive() && sStick > 0) {
        sStick = 0;
        sStickOutInBattle = true;
    }
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
        case 'C':
        case 'k':
            if (!Has(sState.chests, f, x, y)) {
                sState.chests.insert({ f, x, y });
                if (t == 'k') {
                    int got = Party_AddConsumable(ConsumableId::DekuStick, 5);
                    Say(got > 0 ? "Found " + std::to_string(got) + " Deku Sticks!" : "Deku Sticks... but you can't carry more.",
                        0xF8B80000u);
                } else {
                    Say(FindLoot(f, x, y), 0xF8B80000u);
                }
                Audio_PlaySfxGeneral(NA_SE_EV_TBOX_OPEN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                return true;
            }
            break;
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
        case 'X':
            if (!Has(sState.sacs, f, x, y)) {
                sFixedKind = 'X';
                sFixedSpot = { f, x, y };
                std::vector<int> larvae = { (int)EnemyKind::GohmaLarva, (int)EnemyKind::GohmaLarva };
                if (Moon_Phase() >= MoonPhase::SixEighths) {
                    larvae.push_back((int)EnemyKind::GohmaLarva);
                }
                Battle_StartFixed(play, player, larvae);
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

void Husk_OnBattleEnd(bool victory) {
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
                         { "stoneF3", sState.stoneF3 } };
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
