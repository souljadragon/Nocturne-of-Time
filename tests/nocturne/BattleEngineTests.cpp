// Nocturne of Time: unit tests for the battle rules. Not part of the game build.
// Build and run (from this folder):
//   g++ -std=c++20 BattleEngineTests.cpp ../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleEngine.cpp ../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.cpp -o battle_tests
//   ./battle_tests

#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleEngine.h"

#include <cstdio>
#include <deque>
#include <array>
#include <cmath>
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
    CHECK(XpForLevel(1) == 0 && XpForLevel(2) == XpToNext(1));
    CHECK(XpToNext(5) == 150 && XpToNext(10) == 480);
    for (int l = 2; l < 99; l++) {
        CHECK(XpToNext(l) > XpToNext(l - 1));
    }
    // Over-levelled members earn less; stronger foes give a bonus
    CHECK(ScaledXp(100, 5, 4) == 100);
    CHECK(ScaledXp(100, 10, 4) == 77);
    CHECK(ScaledXp(100, 20, 4) == 55);
    CHECK(ScaledXp(100, 4, 8) == 125);
    CHECK(ScaledXp(1, 50, 1) == 1);
    // Each member has their own pace: the same EXP levels Link and Saria at different times
    CHECK(XpForLevel(10, "Link") == XpForLevel(10));
    CHECK(XpForLevel(10, "Saria") > XpForLevel(10, "Link"));
    {
        int xp = XpForLevel(5, "Link"), xpS = XpForLevel(5, "Saria"), lvL = 5, lvS = 5, apart = 0;
        for (int battle = 0; battle < 60; battle++) {
            xp += 50;
            xpS += 50;
            while (xp >= XpForLevel(lvL + 1, "Link")) {
                lvL++;
            }
            while (xpS >= XpForLevel(lvS + 1, "Saria")) {
                lvS++;
            }
            apart += lvL != lvS;
        }
        CHECK(apart > 10); // often a level apart
        CHECK(lvL >= lvS && lvL - lvS <= 2);
    }
}

static void TestLevelUpPoints() {
    std::printf("Level-up stat points\n");
    std::mt19937 gen(7);
    std::uniform_real_distribution<float> dist(0.0f, 0.99999f);
    auto rng = [&] { return dist(gen); };

    // Every level spreads exactly kAutoStatPoints automatically
    Combatant saria = MakeSaria(1);
    int sumBefore = saria.st + saria.ma + saria.vi + saria.ag + saria.lu;
    LevelGains g = LevelUp(saria, rng);
    int sumAfter = saria.st + saria.ma + saria.vi + saria.ag + saria.lu;
    CHECK(g.leveled && saria.level == 2);
    CHECK(sumAfter - sumBefore == kAutoStatPoints);
    int gained = 0;
    for (int v : g.stats) {
        gained += v;
    }
    CHECK(gained == kAutoStatPoints);
    CHECK(g.hp > 0 && g.mp > 0);

    // Over many levels the archetype shows: Saria's Magic grows fastest, Link's Strength; others still grow
    Combatant s2 = MakeSaria(1), l2 = MakeLink(1);
    std::array<int, (size_t)Stat::Count> sariaTotal{}, linkTotal{};
    for (int i = 0; i < 60; i++) {
        LevelGains gs = LevelUp(s2, rng), gl = LevelUp(l2, rng);
        for (size_t k = 0; k < sariaTotal.size(); k++) {
            sariaTotal[k] += gs.stats[k];
            linkTotal[k] += gl.stats[k];
        }
    }
    CHECK(sariaTotal[(size_t)Stat::Ma] > sariaTotal[(size_t)Stat::Vi]);
    CHECK(sariaTotal[(size_t)Stat::Ma] > sariaTotal[(size_t)Stat::St] * 4);
    CHECK(sariaTotal[(size_t)Stat::St] > 0); // occasional Strength for variety
    CHECK(linkTotal[(size_t)Stat::St] > linkTotal[(size_t)Stat::Ma] * 2);
    std::printf("  Saria over 60 levels: St %d Ma %d Vi %d Ag %d Lu %d\n", sariaTotal[0], sariaTotal[1],
                sariaTotal[2], sariaTotal[3], sariaTotal[4]);

    // A fresh party is always identical, and lands near the old level-5 strength (~33 stat points for Link)
    Combatant a = MakeLink(5), b = MakeLink(5);
    CHECK(a.st == b.st && a.ma == b.ma && a.vi == b.vi && a.ag == b.ag && a.lu == b.lu);
    int linkSum = a.st + a.ma + a.vi + a.ag + a.lu;
    CHECK(linkSum == 14 + 4 * (kAutoStatPoints + kManualStatPoints));
    // Stats stop at the cap
    Combatant capped = MakeSaria(1);
    capped.ma = kStatCap;
    for (int i = 0; i < 40; i++) {
        LevelUp(capped, rng);
    }
    CHECK(capped.ma == kStatCap);
    // The old balance gave Saria Ma 35 by level 12; now even all chosen points in Magic stay in the low 20s
    Combatant s12 = MakeSaria(12);
    CHECK(s12.ma <= 26);
    std::printf("  Saria at level 12: Ma %d (all chosen points in Magic)\n", s12.ma);
    std::printf("  Link at level 5: St %d Ma %d Vi %d Ag %d Lu %d\n", a.st, a.ma, a.vi, a.ag, a.lu);
}

static void TestAilments() {
    std::printf("Ailments\n");
    // A Skulltula whose only move is a sure-fire poison bite
    Combatant spider = MakeEnemy(EnemyKind::Skulltula);
    Skill fang;
    fang.name = "Venom Fang";
    fang.element = Element::Pierce;
    fang.power = 4;
    fang.inflict = Ailment::Poison;
    fang.inflictChance = 1.0f;
    spider.skills = { fang };
    Battle b({ MakeLink(5), MakeSaria(5) }, { spider }, Steady());
    b.Guard();
    b.Guard();
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    Events ev = b.EnemyAct();
    int poisoned = -1;
    for (int i = 0; i < 2; i++) {
        if (b.Party()[i].ailment == Ailment::Poison) {
            poisoned = i;
        }
    }
    CHECK(poisoned >= 0);
    // The enemy turn ended, so the party's turn began with a poison tick
    CHECK(b.Phase() == BattlePhase::PartyCommand);
    bool tick = false;
    for (const BattleEvent& e : ev) {
        tick |= e.type == BattleEvent::Type::Hit && e.text.find("poison") != std::string::npos;
    }
    CHECK(tick);

    // Poison never takes the last HP
    Combatant weak = MakeLink(5);
    weak.hp = 1;
    weak.ailment = Ailment::Poison;
    Battle b2({ weak, MakeSaria(5) }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(b2.Party()[0].hp == 1 && b2.Party()[0].Alive());
    CHECK(AilmentDamage(Ailment::Poison, 120) == 10 && AilmentDamage(Ailment::Burn, 5) == 1);

    // Burn wears off after a few of the victim's turns
    Combatant burned = MakeSaria(5);
    burned.ailment = Ailment::Burn;
    burned.ailmentTurns = kBurnTurns;
    Battle b3({ MakeLink(5), burned }, { MakeEnemy(EnemyKind::DekuBaba) }, [] { return 0.99f; }); // enemies miss
    for (int round = 0; round < kBurnTurns + 1 && b3.Phase() == BattlePhase::PartyCommand; round++) {
        b3.Guard();
        b3.Guard();
        while (b3.Phase() == BattlePhase::EnemyTurn) {
            b3.EnemyAct();
        }
    }
    CHECK(b3.Party()[1].ailment == Ailment::None);
}

static void TestItems() {
    std::printf("Items\n");
    ItemEffect potion;
    potion.hp = 60;
    ItemEffect revive;
    revive.revive = 0.5f;
    ItemEffect herb;
    herb.cures = Ailment::Poison;
    ItemEffect milk;
    milk.hp = 40;
    milk.target = TargetType::AllAllies;

    Combatant hurt = MakeSaria(5);
    hurt.hp = 10;
    Combatant down = MakeSaria(5);
    down.hp = 0;
    Battle b({ MakeLink(5), hurt }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(!b.CanUseItem(potion, 0)); // Link is at full HP: it would do nothing
    CHECK(b.CanUseItem(potion, 1));
    Events ev = b.UseItem("Red Potion", potion, 1);
    CHECK(!ev.empty() && HasEvent(ev, BattleEvent::Type::Heal));
    CHECK(b.Party()[1].hp == std::min(70, b.Party()[1].maxHp));
    CHECK(b.Icons().Total() == 1); // one icon used

    // Revival only works on the fallen; potions don't raise the dead
    Battle b2({ MakeLink(5), down }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(!b2.CanUseItem(potion, 1));
    CHECK(!b2.CanUseItem(revive, 0));
    CHECK(b2.CanUseItem(revive, 1));
    b2.UseItem("Bottled Fairy", revive, 1);
    CHECK(b2.Party()[1].hp == (int)std::lround(b2.Party()[1].maxHp * 0.5f));

    // Cures, party-wide items, and use outside battle
    Combatant sick = MakeLink(5);
    sick.ailment = Ailment::Poison;
    CHECK(ItemHelps(herb, sick) && !ItemHelps(herb, MakeLink(5)));
    std::vector<std::string> msgs;
    CHECK(ApplyItemEffect(herb, sick, &msgs) && sick.ailment == Ailment::None && msgs.size() == 1);
    CHECK(!ApplyItemEffect(herb, sick, nullptr));
    Combatant a = MakeLink(5), c = MakeSaria(5);
    a.hp = 5;
    Battle b3({ a, c }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(b3.CanUseItem(milk, -1));
    b3.UseItem("Lon Lon Milk", milk, -1);
    CHECK(b3.Party()[0].hp == 45 && b3.Party()[1].hp == b3.Party()[1].maxHp);
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
            enemies.push_back(MakeEnemy((EnemyKind)((round + i) % kRandomEnemyKinds)));
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

static void TestBossPressTurns() {
    std::printf("Boss: two icons a turn\n");
    Battle b({ MakeLink(8), MakeSaria(8) }, { MakeQueenGohma(2) }, Steady());
    b.Guard();
    b.Guard();
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    CHECK(b.Icons().Total() == 2);
    b.EnemyAct();
    CHECK(b.Phase() == BattlePhase::EnemyTurn); // she acts again
    b.EnemyAct();
    CHECK(b.Phase() == BattlePhase::PartyCommand);

    // Her eye: the slingshot (Pierce) hits a weakness for an extra half turn
    Battle c({ MakeLink(8), MakeSaria(8) }, { MakeQueenGohma(2) }, Steady());
    Events ev = c.UseSkill(2, 0);
    CHECK(HasEvent(ev, BattleEvent::Type::Weak));
    CHECK(c.Icons().solid == 1 && c.Icons().blinking == 1);
}

static void TestStun() {
    std::printf("Stun skips the victim's next turn\n");
    // Saria is stunned before her turn: the party gets one icon, and only Link acts
    Combatant saria = MakeSaria(8);
    saria.stunTurns = 1;
    Battle b({ MakeLink(8), saria }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(b.Icons().Total() == 1);
    CHECK(b.ActivePartyMember() == 0);
    CHECK(b.Party()[1].skipTurn && b.Party()[1].stunTurns == 0);
    b.Guard();
    CHECK(b.Phase() == BattlePhase::EnemyTurn);
    b.EnemyAct();
    CHECK(b.Phase() == BattlePhase::PartyCommand);
    CHECK(b.Icons().Total() == 2 && !b.Party()[1].skipTurn); // back the next turn

    // Stunning Gaze: always hits with this rng, and 0.5 rolls are within the stun chance only if we push it up
    ScriptedRng rng;
    rng.fallback = 0.1f; // hits, low variance, stuns
    Combatant gohma = MakeQueenGohma(0);
    gohma.skills = { gohma.skills[2] }; // only the gaze
    Battle g({ MakeLink(8), MakeSaria(8) }, { gohma }, [&] { return rng(); });
    g.Guard();
    g.Guard();
    Events ev = g.EnemyAct();
    bool stunned = false;
    for (const BattleEvent& e : ev) {
        stunned |= e.text.find("is stunned!") != std::string::npos;
    }
    CHECK(stunned);
    int stunnedCount = 0;
    for (const Combatant& c : g.Party()) {
        stunnedCount += c.stunTurns;
    }
    CHECK(stunnedCount >= 1);

    // A whole party stunned: the turn goes straight to the enemies
    Combatant l = MakeLink(8), s = MakeSaria(8);
    l.stunTurns = 1;
    s.stunTurns = 1;
    Battle all({ l, s }, { MakeEnemy(EnemyKind::DekuBaba) }, Steady());
    CHECK(all.Phase() == BattlePhase::EnemyTurn);
    all.EnemyAct();
    CHECK(all.Phase() == BattlePhase::PartyCommand && all.Icons().Total() == 2);
}

static void TestSummonAtHalf() {
    std::printf("Gohma lays eggs below half HP\n");
    Combatant gohma = MakeQueenGohma(3);
    gohma.hp = gohma.maxHp / 2 + 5;
    Battle b({ MakeLink(8), MakeSaria(8) }, { gohma }, Steady());
    CHECK(b.Enemies().size() == 1);
    Events ev = b.UseSkill(0, 0);
    CHECK(HasEvent(ev, BattleEvent::Type::Summon));
    CHECK(b.Enemies().size() == 4);
    CHECK(b.Enemies()[1].name == "Gohma Larva A" && b.Enemies()[3].name == "Gohma Larva C");
    // only once
    b.Pass();
    while (b.Phase() == BattlePhase::EnemyTurn) {
        b.EnemyAct();
    }
    CHECK(b.Enemies().size() == 4);
    // the enemy turn now has her two icons plus one per larva
    b.Guard();
    b.Guard();
    if (b.Phase() == BattlePhase::EnemyTurn) {
        int alive = 0;
        for (size_t i = 1; i < b.Enemies().size(); i++) {
            alive += b.Enemies()[i].Alive() ? 1 : 0;
        }
        CHECK(b.Icons().Total() == 2 + alive);
    }
}

static void TestTimeLimit() {
    std::printf("Time limit\n");
    Combatant tough = MakeEnemy(EnemyKind::Skulltula);
    tough.maxHp = tough.hp = 99999;
    tough.skills = { tough.skills[0] };
    tough.skills[0].power = 0;
    tough.st = 0;
    Battle b({ MakeLink(8), MakeSaria(8) }, { tough }, Steady());
    b.SetTurnLimit(3);
    CHECK(b.TurnsLeft() == 3);
    int turns = 0;
    while (b.Phase() == BattlePhase::PartyCommand || b.Phase() == BattlePhase::EnemyTurn) {
        if (b.Phase() == BattlePhase::EnemyTurn) {
            b.EnemyAct();
        } else {
            b.Guard();
            if (b.Phase() == BattlePhase::EnemyTurn) {
                turns++;
            }
        }
        if (turns > 10) {
            break;
        }
    }
    CHECK(turns == 3);
    CHECK(b.Phase() == BattlePhase::Defeat && b.TimedOut());
    CHECK(b.Party()[0].Alive()); // not by damage

    // The warning, and larvae buying time back
    Battle w({ MakeLink(20), MakeSaria(20) }, { MakeEnemy(EnemyKind::GohmaLarva), tough }, Steady());
    w.SetTurnLimit(6);
    w.Guard();
    w.Guard(); // turn 1 over
    while (w.Phase() == BattlePhase::EnemyTurn) {
        w.EnemyAct();
    }
    CHECK(w.TurnsLeft() == 5);
    Events ev = w.UseSkill(0, 0); // Link's sword (Slash) on the larva: weak, defeated
    CHECK(!w.Enemies()[0].Alive());
    CHECK(w.TurnsLeft() == 6);
    bool bonus = false;
    for (const BattleEvent& e : ev) {
        bonus |= e.text == "Time +1!";
    }
    CHECK(bonus);

    Battle x({ MakeLink(8), MakeSaria(8) }, { tough }, Steady());
    x.SetTurnLimit(Battle::kTimeWarning + 1);
    x.Guard();
    x.Guard();
    Events all;
    while (x.Phase() == BattlePhase::EnemyTurn) {
        Events e = x.EnemyAct();
        all.insert(all.end(), e.begin(), e.end());
    }
    CHECK(HasEvent(all, BattleEvent::Type::Warning));
}

static void TestNoEscape() {
    std::printf("No running from bosses\n");
    Battle b({ MakeLink(8), MakeSaria(8) }, { MakeQueenGohma(2) }, [] { return 0.0f; });
    b.SetNoEscape(true);
    Events ev = b.Run();
    CHECK(b.Phase() == BattlePhase::PartyCommand && b.Icons().Total() == 2);
    CHECK(!ev.empty() && ev[0].text == "There's no escape!");
}

// A sensible player against Gohma at the level the gate is likely reached (8 battles from level 5)
static void TestGohmaBalance() {
    std::printf("Gohma balance (simulated)\n");
    std::mt19937 gen(99);
    std::uniform_real_distribution<float> dist(0.0f, 0.99999f);
    for (int level : { 7, 8, 9 }) {
        int wins = 0, timeouts = 0, totalTurns = 0;
        const int kRounds = 400;
        for (int round = 0; round < kRounds; round++) {
            Combatant link = MakeLink(level), saria = MakeSaria(level);
            link.weaponPower = 6; // a Hylian Blade or so
            link.maxHp += 6;
            link.hp = link.maxHp;
            Battle b({ link, saria }, { MakeQueenGohma(2) }, [&] { return dist(gen); });
            b.SetTurnLimit(kGohmaTurnLimit);
            b.SetNoEscape(true);
            int turns = 0;
            bool wasParty = true;
            while ((b.Phase() == BattlePhase::PartyCommand || b.Phase() == BattlePhase::EnemyTurn) && turns < 200) {
                if (b.Phase() == BattlePhase::EnemyTurn) {
                    if (wasParty) {
                        turns++;
                    }
                    wasParty = false;
                    b.EnemyAct();
                    continue;
                }
                wasParty = true;
                int who = b.ActivePartyMember();
                const std::vector<Combatant>& e = b.Enemies();
                int larvae = 0;
                for (size_t i = 1; i < e.size(); i++) {
                    larvae += e[i].Alive() ? 1 : 0;
                }
                if (who == 1) {
                    // Saria: heal anyone below 45%, else wind
                    int hurt = -1;
                    for (int i = 0; i < 2; i++) {
                        if (b.Party()[i].Alive() && b.Party()[i].hp * 100 < b.Party()[i].maxHp * 45) {
                            hurt = i;
                        }
                    }
                    if (hurt >= 0 && b.CanUseSkill(3)) {
                        b.UseSkill(3, hurt);
                    } else if (larvae >= 2 && b.CanUseSkill(2)) {
                        b.UseSkill(2, -1);
                    } else if (b.CanUseSkill(1)) {
                        b.UseSkill(1, 0);
                    } else {
                        b.UseSkill(0, 0);
                    }
                } else {
                    if (larvae >= 2 && b.CanUseSkill(1)) {
                        b.UseSkill(1, -1); // Spin Attack
                    } else {
                        b.UseSkill(2, e[0].Alive() ? 0 : 1); // slingshot at the eye
                    }
                }
            }
            if (b.Phase() == BattlePhase::Victory) {
                wins++;
                totalTurns += turns + 1;
            }
            timeouts += b.TimedOut() ? 1 : 0;
        }
        std::printf("  level %d: %d%% wins, %d%% timeouts, %.1f turns per win\n", level, wins * 100 / kRounds,
                    timeouts * 100 / kRounds, wins ? (float)totalTurns / wins : 0.0f);
        if (level == 9) {
            CHECK(wins > kRounds / 2); // beatable when prepared
        }
        if (level == 7) {
            CHECK(wins < kRounds); // but not a pushover
        }
    }
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
    TestLevelUpPoints();
    TestAilments();
    TestItems();
    TestRandomBattlesFinish();
    TestBossPressTurns();
    TestStun();
    TestSummonAtHalf();
    TestTimeLimit();
    TestNoEscape();
    TestGohmaBalance();
    std::printf("\n%d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
