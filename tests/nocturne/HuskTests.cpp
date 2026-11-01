// Nocturne of Time: checks on the Hollowed Husk's floor grids. Not part of the game build.
//   N=../../soh/soh/Enhancements/NocturneOfTime
//   g++ -std=c++20 HuskTests.cpp $N/Dungeon/HuskFloors.cpp -o husk_tests && ./husk_tests
#include "../../soh/soh/Enhancements/NocturneOfTime/Dungeon/HuskFloors.h"
#include <cstdio>
#include <queue>
#include <set>

using namespace Nocturne;
static int sFail = 0, sChecks = 0;
#define CHECK(c)                                                             \
    do {                                                                     \
        sChecks++;                                                           \
        if (!(c)) {                                                          \
            sFail++;                                                         \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                    \
    } while (0)

int main() {
    const int sizes[3] = { 17, 21, 17 };
    for (int f = 0; f < kHuskFloors; f++) {
        const HuskFloor& fl = HuskFloorOf(f);
        CHECK(fl.Width() == sizes[f] && fl.Height() == sizes[f]);
        for (const std::string& r : fl.rows) {
            CHECK((int)r.size() == fl.Width());
            CHECK(r.front() == '#' && r.back() == '#');
        }
        // Every tile walkable once webs burn and the gate opens is reachable from the arrival tile
        HuskSpot start;
        CHECK(HuskFind(f, f == 0 ? 'S' : 'U', start));
        std::set<std::pair<int, int>> seen;
        std::queue<std::pair<int, int>> q;
        q.push({ start.x, start.y });
        seen.insert({ start.x, start.y });
        while (!q.empty()) {
            auto [x, y] = q.front();
            q.pop();
            const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { 1, 0, -1, 0 };
            for (int d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                char t = HuskTile(f, nx, ny);
                if (HuskTileSolid(t) || seen.count({ nx, ny })) {
                    continue;
                }
                if (dy[d] < 0 && (t == 'v' || HuskTile(f, x, y) == 'v')) {
                    continue; // ledges only going south
                }
                seen.insert({ nx, ny });
                q.push({ nx, ny });
            }
        }
        for (int y = 0; y < fl.Height(); y++) {
            for (int x = 0; x < fl.Width(); x++) {
                char t = HuskTile(f, x, y);
                if (!HuskTileSolid(t) && t != 'W' && t != 'G' && !seen.count({ x, y })) {
                    std::printf("  floor %d: (%d,%d) '%c' unreachable\n", f, x, y, t);
                    CHECK(false);
                }
                if (t == 'i') {
                    CHECK(!HuskEngraving(f, x, y).empty());
                }
            }
        }
    }
    HuskSpot to;
    CHECK(HuskLink(0, 8, 8, to) && to.floor == 1 && to.x == 10 && to.y == 10); // F1 hole -> F2 vines
    CHECK(HuskLink(1, 10, 10, to) && to.floor == 0 && to.x == 8 && to.y == 8);
    CHECK(HuskLink(1, 19, 1, to) && to.floor == 2 && to.x == 8 && to.y == 1);
    CHECK(HuskLink(0, 12, 2, to) && to.floor == 1 && to.x == 2 && to.y == 10); // F1 pit -> F2 den
    CHECK(HuskLink(1, 13, 2, to) && to.floor == 2 && to.x == 11 && to.y == 10);
    CHECK(HuskTile(0, 15, 8) == 'C'); // the Kokiri Ocarina's chest
    std::printf("%d checks, %d failed\n", sChecks, sFail);
    return sFail ? 1 : 0;
}
