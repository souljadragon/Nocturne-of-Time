#pragma once

// Nocturne of Time: the Hollowed Husk's three floors as text grids (design/hollowed-husk-layout.md in the
// project). One character = one tile (160 units since the Etrian-style rebuild); x runs east, y runs south, (0, 0) is
// the top-left. Pure C++.

#include <string>
#include <vector>

namespace Nocturne {

constexpr int kHuskFloors = 3;

// How high a stretch of floor is roofed: corridors are the default; rooms are taller; a hall is a great hollow whose
// roof is lost in the dark
enum class HuskSpace : char { Corridor = '.', Room = 'r', Hall = '^' };

struct HuskArea {
    int x0, y0, x1, y1; // inclusive
    HuskSpace space;
};

struct HuskFloor {
    const char* name;  // "The Hollow"
    std::vector<std::string> rows;
    bool visiblePits;  // F1's pit is plain to see; deeper ones look like floor until found
    std::vector<HuskArea> areas; // rooms and halls (anything else is a corridor)
    int Width() const {
        return rows.empty() ? 0 : (int)rows[0].size();
    }
    int Height() const {
        return (int)rows.size();
    }
};

const HuskFloor& HuskFloorOf(int floor);
// The tile at (x, y); '#' outside the grid
char HuskTile(int floor, int x, int y);
// Tiles that block a step whatever the state (walls, torches, stones, water); webs and the gate block until
// opened
bool HuskTileSolid(char tile);
// The space a tile is in
HuskSpace HuskSpaceAt(int floor, int x, int y);

struct HuskSpot {
    int floor = 0, x = 0, y = 0;
};
// Where things lead. False if the tile leads nowhere.
// - 'D' (way down) -> the next floor's 'U'; 'U' (way up) -> the floor above's 'D'
// - 'P' (pit) -> the next floor's 'L'
// - 'w' (the Great Web, once burned: a shaft) -> the next floor's 'U'; and a 'U' below a floor with no 'D' climbs
//   back up to the walkway just south of that floor's shaft
bool HuskLink(int floor, int x, int y, HuskSpot& to);
// The first tile of `tile` on `floor` (e.g. 'S', 'H'); false if none
bool HuskFind(int floor, char tile, HuskSpot& out);
// The hint an engraving 'i' shows ("" if none)
std::string HuskEngraving(int floor, int x, int y);

} // namespace Nocturne
