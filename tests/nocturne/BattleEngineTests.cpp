// Nocturne of Time: unit tests for the battle rules. Not part of the game build.
// Build and run (from this folder):
//   g++ -std=c++20 BattleEngineTests.cpp ../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleEngine.cpp ../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.cpp -o battle_tests
//   ./battle_tests

#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleEngine.h"

#include <cstdio>
#include <deque>
#include <random>

using namespace Nocturne;

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        sChecks++;                                                        \
        if (!(cond)) {                                                    \
            sFailures++;                                                  \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

// Scripted randomness: returns queued values, then `fallback`.
struct ScriptedRng {
    std::deque<float> values;
    float fallback = 0.5f;
    float operator()() {
        if (values.empty()) {
            return fallback;
        }
        float v = values.front();
        values.pop_front();
        return v;
    }
};

// A deterministic "always hit, never crit, mid variance" rng: 0.5 is under every hit chance in the data and
// above every crit chance.
static std::function<float()> Steady() {
    return [] { return 0.5f; };
}

static bool HasEvent(const Events& events, BattleEvent::Type type) {
    for (const BattleEvent& e : events) {
        if (e.type == type) {
            return true;
        }
    }
    return false;
}

static void TestPressTurnIcons() {
    std::printf("Press Turn icon rules\n");
    PressTurns p{ 2, 0 };
    p.UseHalf();
    CHECK(p.solid == 1 && p.blinking == 1);
    p.UseHalf();
    CHECK(p.solid == 0 && p.blinking == 2);
    p.UseFull();
    CHECK(p.solid == 0 && p.blinking == 1);
    p.UseHalf(); // only blinking left: consumes it
    CHECK(p.Empty());

    PressTurns q{ 2, 1 };
    q.UseFull(); // blinking goes first
    CHECK(q.solid == 2 && q.blinking == 0);
    q.UseTwo();
    CHECK(q.Empty());

    PressTurns r{ 4, 0 };
    r.UseAll();
    CHECK(r.Empty());
}

static void TestStartOfBattle() {
    std::printf("Start of battle\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(b.Phase() == BattlePhase::PartyCommand);
    CHECK(b.Icons().solid == 2 && b.Icons().blinking == 0);
    CHECK(b.ActivePartyMember() == 0);
}

static void TestWeaknessGivesHalfTurn() {
    std::printf("Hitting a weakness costs half an icon\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    Events ev = b.UseSkill(0, 0); // Link's sword (Slash) on Deku Baba (weak to Slash)
    CHECK(HasEvent(ev, BattleEvent::Type::Weak));
    CHECK(HasEvent(ev, BattleEvent::Type::Hit));
    CHECK(b.Icons().solid == 1 && b.Icons().blinking == 1);
    CHECK(b.ActivePartyMember() == 1); // Saria is up next
    CHECK(b.Phase() == BattlePhase::PartyCommand);
}

static void TestNullLosesTwoIcons() {
    std::printf("Null costs two icons\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::FireKeese) }, Steady());
    b.Pass(); // Link passes: half icon, Saria up
    CHECK(b.ActivePartyMember() == 1);
    Events ev = b.UseSkill(0, 0); // Saria's staff (Strike) vs Fire Keese (Null Strike)
    CHECK(HasEvent(ev, BattleEvent::Type::Null));
    CHECK(b.Phase() == BattlePhase::EnemyTurn); // 1 solid + 1 blinking - 2 = 0
}

static void TestDrainEndsTurn() {
    std::printf("Drain ends the whole turn\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba), MakeEnemy(EnemyKind::DekuBaba) },
             Steady());
    b.Pass();
    int hpBefore = b.Enemies()[0].hp;
    Events ev = b.UseSkill(1, 0); // Saria's Gale (Wind) vs Deku Baba (Drain Wind)
    CHECK(HasEvent(ev, BattleEvent::Type::Drain));
    CHECK(HasEvent(ev, BattleEvent::Type::TurnLost));
    CHECK(b.Enemies()[0].hp >= hpBefore);
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    CHECK(b.Icons().solid == 2); // two living Deku Babas
}

static void TestMissLosesTwoIcons() {
    std::printf("A miss costs two icons\n");
    ScriptedRng rng;
    rng.values = { 0.999f }; // first accuracy roll fails
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::Skulltula) }, std::ref(rng));
    Events ev = b.UseSkill(0, 0);
    CHECK(HasEvent(ev, BattleEvent::Type::Miss));
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
}

static void TestCriticalGivesHalfTurn() {
    std::printf("A critical costs half an icon\n");
    ScriptedRng rng;
    rng.values = { 0.1f, 0.5f, 0.0f }; // hit, mid variance, crit
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::Skulltula) }, std::ref(rng));
    Events ev = b.UseSkill(0, 0); // Slash vs Skulltula (Resist Slash) but crit
    CHECK(HasEvent(ev, BattleEvent::Type::Critical));
    CHECK(HasEvent(ev, BattleEvent::Type::Resist));
    CHECK(b.Icons().solid == 1 && b.Icons().blinking == 1);
}

static void TestEnemyExploitsWeakness() {
    std::printf("Enemies get the same bonus for hitting a party weakness\n");
    // Fire Keese: rolls pick its second skill (Flame Dive) and the weak target (Saria)
    ScriptedRng rng;
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::FireKeese) }, std::ref(rng));
    b.Guard(); // Link
    b.Guard(); // Saria -> enemy turn, but Saria guarding covers her weakness
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    CHECK(b.Icons().solid == 1);
    rng.values = { 0.9f, 0.1f, 0.0f }; // skill index 1 (Flame Dive), choose weak pool, pick Saria
    Events ev = b.EnemyAct();
    CHECK(!HasEvent(ev, BattleEvent::Type::Weak)); // guarding

    // Fresh battle where Saria isn't guarding
    ScriptedRng rng2;
    Battle c({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::FireKeese), MakeEnemy(EnemyKind::FireKeese) },
             std::ref(rng2));
    c.Guard(); // Link: one full icon
    c.Pass();  // Saria: solid -> blinking
    c.Pass();  // Link: uses the blinking icon (passing only ever turns solid icons blinking)
    CHECK(c.Phase() == BattlePhase::EnemyTurn);
    CHECK(c.Icons().solid == 2);
    rng2.values = { 0.9f, 0.1f, 0.0f };
    Events ev2 = c.EnemyAct();
    CHECK(HasEvent(ev2, BattleEvent::Type::Weak));
    CHECK(c.Icons().solid == 1 && c.Icons().blinking == 1);
    CHECK(c.Party()[1].hp < c.Party()[1].maxHp);
}

static void TestHealing() {
    std::printf("Healing restores HP up to the maximum\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    b.PartyMutable()[0].hp = 10;
    b.Pass(); // Link
    int mpBefore = b.Party()[1].mp;
    Events ev = b.UseSkill(3, 0); // Song of Healing on Link
    CHECK(HasEvent(ev, BattleEvent::Type::Heal));
    CHECK(b.Party()[0].hp == 50 || b.Party()[0].hp == 10 + 40);
    CHECK(b.Party()[1].mp == mpBefore - 5);
}

static void TestVictoryAndRewards() {
    std::printf("Victory and rewards\n");
    Battle b({ MakeLink(30), MakeSaria(30) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    Events ev = b.UseSkill(0, 0);
    CHECK(HasEvent(ev, BattleEvent::Type::Defeated));
    CHECK(HasEvent(ev, BattleEvent::Type::Victory));
    CHECK(b.Phase() == BattlePhase::Victory);
    CHECK(b.XpEarned() == 20);
    int r = b.RupeesEarned();
    CHECK(r >= 5 && r <= 10);
    CHECK(b.RupeesEarned() == r); // stable
}

static void TestLinkFallingIsDefeat() {
    std::printf("Link falling is a defeat even if Saria stands\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::Skulltula) }, Steady());
    b.PartyMutable()[0].hp = 1;
    b.Guard();
    b.Guard(); // even guarded, any hit takes Link's last HP
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    // Steady 0.5: skill index (int)(0.5*2)=1 -> Spin (all) hits Link for more than 1
    Events ev = b.EnemyAct();
    CHECK(b.Phase() == BattlePhase::Defeat);
    CHECK(HasEvent(ev, BattleEvent::Type::Defeat));
}

static void TestSkillCosts() {
    std::printf("Skill costs\n");
    Battle b({ MakeLink(5), MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    b.PartyMutable()[0].hp = 6;
    CHECK(!b.CanUseSkill(1)); // Spin Attack costs 6 HP; can't spend the last HP
    b.PartyMutable()[0].hp = 20;
    CHECK(b.CanUseSkill(1));
    b.UseSkill(1, -1);
    CHECK(b.Party()[0].hp == 14);
}

static void TestLevels() {
    std::printf("Levels\n");
    Combatant l1 = MakeLink(1), l5 = MakeLink(5);
    CHECK(l5.level == 5);
    CHECK(l5.maxHp > l1.maxHp && l5.st > l1.st);
    CHECK(l5.hp == l5.maxHp);
    CHECK(XpForLevel(1) == 0 && XpForLevel(2) == 40);
}

static void TestRandomBattlesFinish() {
    std::printf("Random battles always finish\n");
    std::mt19937 gen(1234);
    std::uniform_real_distribution<float> dist(0.0f, 0.99999f);
    int outcomes[5] = {};
    for (int round = 0; round < 500; round++) {
        std::vector<Combatant> enemies;
        int n = 1 + round % 3;
        for (int i = 0; i < n; i++) {
            enemies.push_back(MakeEnemy((EnemyKind)((round + i) % (int)EnemyKind::Count)));
        }
        Battle b({ MakeLink(5), MakeSaria(5) }, enemies, [&] { return dist(gen); });
        int steps = 0;
        while ((b.Phase() == BattlePhase::PartyCommand || b.Phase() == BattlePhase::EnemyTurn) && steps < 2000) {
            steps++;
            if (b.Phase() == BattlePhase::EnemyTurn) {
                b.EnemyAct();
                continue;
            }
            int who = b.ActivePartyMember();
            int roll = (int)(dist(gen) * 10);
            int target = (int)(dist(gen) * b.Enemies().size());
            if (roll == 0) {
                b.Guard();
            } else if (roll == 1) {
                b.Pass();
            } else if (roll == 2) {
                b.Analyze(target);
            } else {
                int skill = (int)(dist(gen) * b.Party()[who].skills.size());
                if (!b.CanUseSkill(skill)) {
                    skill = 0;
                }
                b.UseSkill(skill, b.Party()[who].skills[skill].heal ? (int)(dist(gen) * 2) : target);
            }
        }
        CHECK(steps < 2000);
        outcomes[(int)b.Phase()]++;
        for (const Combatant& c : b.Party()) {
            CHECK(c.hp >= 0 && c.hp <= c.maxHp && c.mp >= 0);
        }
    }
    std::printf("  outcomes: victory %d, defeat %d\n", outcomes[(int)BattlePhase::Victory],
                outcomes[(int)BattlePhase::Defeat]);
    CHECK(outcomes[(int)BattlePhase::Victory] > 0);
}

int main() {
    TestPressTurnIcons();
    TestStartOfBattle();
    TestWeaknessGivesHalfTurn();
    TestNullLosesTwoIcons();
    TestDrainEndsTurn();
    TestMissLosesTwoIcons();
    TestCriticalGivesHalfTurn();
    TestEnemyExploitsWeakness();
    TestHealing();
    TestVictoryAndRewards();
    TestLinkFallingIsDefeat();
    TestSkillCosts();
    TestLevels();
    TestRandomBattlesFinish();
    std::printf("\n%d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
