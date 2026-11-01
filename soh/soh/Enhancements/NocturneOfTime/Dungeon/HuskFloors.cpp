#include "HuskFloors.h"

namespace Nocturne {

namespace {

std::vector<HuskFloor> Build() {
    return {
        { "The Hollow",
          {
              "#################",
              "#.BB.####......C#",
              "#CBB.####...P...#",
              "#.BB..###.......#",
              "#####.#####i#####",
              "#####.......#####",
              "#...#.##W##.#...#",
              "#...#.#...#.#...#",
              "#T.i..#.D.#..A.C#",
              "#...#.#...#.#...#",
              "#k..#.#####.#...#",
              "#####.......#####",
              "########.########",
              "########i########",
              "########.########",
              "########S########",
              "#################",
          },
          true },
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
          false },
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
          false },
    };
}

struct Engraving {
    int floor, x, y;
    const char* text;
};

const Engraving kEngravings[] = {
    { 0, 8, 13, "The Husk breathes. Every step wakes something in the dark." },
    { 0, 3, 8, "Fire dies in twelve steps, or when blood is drawn." },
    { 0, 11, 4, "Where the floor sags, the Husk swallows." },
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
        return HuskFind(floor - 1, 'D', to);
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
