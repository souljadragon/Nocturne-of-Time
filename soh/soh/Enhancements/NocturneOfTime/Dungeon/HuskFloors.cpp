#include "HuskFloors.h"

namespace Nocturne {

namespace {

std::vector<HuskFloor> Build() {
    return {
        // F1 rebuilt Nov 1 (Etrian-style, the vanilla tree's own textures): the Great Hollow, a tall hall round the
        // trunk's shaft, under a great web ('w'). Burn the web and drop through it to F2.
        { "The Great Hollow",
          {
              "#################",
              "#####C.B....P.###",
              "#####..B..i...C##",
              "########.########",
              "########.########",
              "#####t.....t#####",
              "#...#.......#...#",
              "#...#..www..#...#",
              "#T.i...www..A..C#",
              "#...#..www..#...#",
              "#k..#.......#...#",
              "#####t.....t#####",
              "########.########",
              "########.########",
              "########i########",
              "########.########",
              "########S########",
              "#################",
          },
          true,
          { { 5, 5, 11, 11, HuskSpace::Hall },
            { 1, 6, 3, 10, HuskSpace::Room },
            { 13, 6, 15, 10, HuskSpace::Room },
            { 5, 1, 14, 2, HuskSpace::Room } } },
        { "The Rootways",
          {
              "#####################",
              "#################C.D#",
              "###..........P..#...#",
              "###.######.####.#...#",
              "###.#..C##A####.W...#",
              "###.....##.####B##v##",
              "###.#.A.##.####B##.##",
              "###.######i####B##.##",
              "#C...###H....##B##.##",
              "#....###.....##B##.##",
              "#.L.E.....U........##",
              "#....###.....##t#####",
              "#k...###.....########",
              "##########.##########",
              "##########.##########",
              "##########.t#########",
              "###...A....##########",
              "#.....###############",
              "#T....###############",
              "#....C###############",
              "#####################",
          },
          false,
          { { 8, 8, 12, 12, HuskSpace::Room }, { 1, 8, 4, 12, HuskSpace::Room }, { 1, 16, 5, 19, HuskSpace::Room },
            { 17, 1, 19, 4, HuskSpace::Room } } },
        { "Gohma's Lair",
          {
              "#################",
              "#X...##.U.##C..X#",
              "#....##...##....#",
              "#....##.i.##....#",
              "##.#####.#####.##",
              "##.#####.#####.##",
              "##.............##",
              "##.###########.##",
              "##.#XRR###C..#.##",
              "##.RRRR###..A..##",
              "##.#RRR###.L.#.##",
              "##.###########.##",
              "##.............##",
              "########G########",
              "#######H..#######",
              "#######.Q.#######",
              "#################",
          },
          false,
          { { 1, 1, 4, 3, HuskSpace::Room }, { 12, 1, 15, 3, HuskSpace::Room }, { 7, 1, 9, 3, HuskSpace::Room },
            { 3, 8, 6, 10, HuskSpace::Room }, { 10, 8, 12, 10, HuskSpace::Room } } },
    };
}

struct Engraving {
    int floor, x, y;
    const char* text;
};

const Engraving kEngravings[] = {
    { 0, 8, 14, "The Husk breathes. Every step wakes something in the dark." },
    { 0, 3, 8, "Fire dies in twelve steps. The Great Web fears it." },
    { 0, 10, 2, "Where the floor sags, the Husk swallows." },
    { 1, 10, 7, "Thorns tear the flesh, but the thorn path is short." },
    { 2, 8, 3, "Three hearts beat for the Queen. Still them." },
};

} // namespace

const HuskFloor& HuskFloorOf(int floor) {
    static const std::vector<HuskFloor> floors = Build();
    if (floor < 0) {
        floor = 0;
    }
    if (floor >= kHuskFloors) {
        floor = kHuskFloors - 1;
    }
    return floors[floor];
}

char HuskTile(int floor, int x, int y) {
    if (floor < 0 || floor >= kHuskFloors) {
        return '#';
    }
    const HuskFloor& f = HuskFloorOf(floor);
    if (y < 0 || y >= f.Height() || x < 0 || x >= f.Width()) {
        return '#';
    }
    return f.rows[y][x];
}

bool HuskTileSolid(char tile) {
    return tile == '#' || tile == 'T' || tile == 't' || tile == 'H';
}

HuskSpace HuskSpaceAt(int floor, int x, int y) {
    if (floor < 0 || floor >= kHuskFloors) {
        return HuskSpace::Corridor;
    }
    for (const HuskArea& a : HuskFloorOf(floor).areas) {
        if (x >= a.x0 && x <= a.x1 && y >= a.y0 && y <= a.y1) {
            return a.space;
        }
    }
    return HuskSpace::Corridor;
}

bool HuskFind(int floor, char tile, HuskSpot& out) {
    const HuskFloor& f = HuskFloorOf(floor);
    for (int y = 0; y < f.Height(); y++) {
        for (int x = 0; x < f.Width(); x++) {
            if (f.rows[y][x] == tile) {
                out = { floor, x, y };
                return true;
            }
        }
    }
    return false;
}

bool HuskLink(int floor, int x, int y, HuskSpot& to) {
    char t = HuskTile(floor, x, y);
    if (t == 'D' && floor + 1 < kHuskFloors) {
        return HuskFind(floor + 1, 'U', to);
    }
    if (t == 'U' && floor > 0) {
        if (HuskFind(floor - 1, 'D', to)) {
            return true;
        }
        // Up the shaft: the first web tile is the shaft's north-west corner, so the walkway south of it is 3 down
        if (HuskFind(floor - 1, 'w', to)) {
            to.y += 3;
            return true;
        }
        return false;
    }
    if (t == 'w' && floor + 1 < kHuskFloors) {
        return HuskFind(floor + 1, 'U', to);
    }
    if (t == 'P' && floor + 1 < kHuskFloors) {
        return HuskFind(floor + 1, 'L', to);
    }
    return false;
}

std::string HuskEngraving(int floor, int x, int y) {
    for (const Engraving& e : kEngravings) {
        if (e.floor == floor && e.x == x && e.y == y) {
            return e.text;
        }
    }
    return "";
}

} // namespace Nocturne
