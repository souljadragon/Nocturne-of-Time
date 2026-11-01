// Nocturne of Time: Queen Gohma balance simulation. Not part of the game build.
// Plays the boss fight thousands of times with the two party policies in GohmaPolicy.h and prints win rates.
// Build and run (from this folder):
//   N=../../soh/soh/Enhancements/NocturneOfTime
//   g++ -std=c++20 -O2 GohmaSim.cpp $N/Battle/BattleEngine.cpp $N/Battle/BattleData.cpp $N/Relics/Relics.cpp -o gohma_sim
//   ./gohma_sim [fromLevel toLevel]       win rates per level
//   ./gohma_sim level smart(0/1) seed     one fight, every message
#include "GohmaPolicy.h"
#include <cstdlib>

using namespace GohmaPolicy;

int main(int argc, char** argv) {
    if (argc > 3) {
        gVerbose = true;
        std::mt19937 r(atoi(argv[3]));
        Result res = Play(atoi(argv[1]), atoi(argv[2]) != 0, r, 4);
        printf("win %d timeout %d turns %d\n", res.win, res.timeout, res.turns);
        return 0;
    }
    int from = argc > 1 ? atoi(argv[1]) : 7, to = argc > 2 ? atoi(argv[2]) : 11;
    std::mt19937 rng(7);
    for (int level = from; level <= to; level++) {
        for (int smart = 0; smart < 2; smart++) {
            int wins = 0, timeouts = 0, turnSum = 0, n = 3000;
            for (int i = 0; i < n; i++) {
                Result r = Play(level, smart, rng, 4);
                wins += r.win;
                timeouts += r.timeout;
                turnSum += r.win ? r.turns : 0;
            }
            printf("Lv %2d %-5s: win %5.1f%%  timeout %5.1f%%  avg turns to win %.1f\n", level,
                   smart ? "smart" : "naive", 100.0 * wins / n, 100.0 * timeouts / n,
                   wins ? (double)turnSum / wins : 0.0);
        }
    }
}
