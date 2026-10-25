// Nocturne of Time: unit tests for loot generation and gear. Not part of the game build.
// Build and run (from this folder):
//   N=../../soh/soh/Enhancements/NocturneOfTime
//   g++ -std=c++20 LootTests.cpp $N/Loot/Loot.cpp $N/Party.cpp $N/Battle/BattleEngine.cpp $N/Battle/BattleData.cpp -o loot_tests
//   ./loot_tests

#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Loot/Loot.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Party.h"

#include <array>
#include <cstdio>
#include <map>
#include <random>
#include <set>

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

static std::mt19937 sGen(42);
static std::uniform_real_distribution<float> sDist(0.0f, 0.99999f);
static LootRng Rng() {
    return [] { return sDist(sGen); };
}

static int AffixCount(const Item& item) {
    return (int)item.mods.size() - (int)BaseOf(item).mods.size();
}

static void TestRarityOdds() {
    std::printf("Rarity odds by source\n");
    auto count = [](DropSource src, float luck) {
        std::map<Rarity, int> c;
        for (int i = 0; i < 20000; i++) {
            c[RollRarity(src, luck, Rng())]++;
        }
        std::printf("  source %d luck %.0f: C %5d  U %5d  R %5d  L %5d\n", (int)src, luck, c[Rarity::Common],
                    c[Rarity::Uncommon], c[Rarity::Rare], c[Rarity::Legendary]);
        return c;
    };
    std::map<Rarity, int> common = count(DropSource::Common, 0.0f);
    std::map<Rarity, int> lucky = count(DropSource::Common, 1.0f);
    std::map<Rarity, int> elite = count(DropSource::Elite, 0.0f);
    std::map<Rarity, int> boss = count(DropSource::Boss, 0.0f);

    // Common enemies: never Legendary, mostly white and blue, purple rare (~4%)
    CHECK(common[Rarity::Legendary] == 0 && lucky[Rarity::Legendary] == 0);
    CHECK(common[Rarity::Uncommon] > 20000 * 0.40 && common[Rarity::Common] < 20000 * 0.56); // blues nearly even
    CHECK(lucky[Rarity::Uncommon] > lucky[Rarity::Common]); // luck tips it toward blue
    CHECK(common[Rarity::Uncommon] > common[Rarity::Rare] * 4);
    CHECK(common[Rarity::Rare] < 20000 * 0.06);
    CHECK(lucky[Rarity::Rare] > common[Rarity::Rare]); // luck still helps
    // Elites and bosses can drop Legendaries; bosses never drop plain items
    CHECK(elite[Rarity::Legendary] > 0);
    CHECK(boss[Rarity::Common] == 0 && boss[Rarity::Legendary] > elite[Rarity::Legendary]);

    std::array<float, 4> odds = RarityOdds(DropSource::Common, 0.0f);
    CHECK(odds[2] > 3.9f && odds[2] < 4.1f); // purple unchanged at 4%
    CHECK(odds[0] + odds[1] + odds[2] + odds[3] > 99.9f && odds[0] + odds[1] + odds[2] + odds[3] < 100.1f);
}

static void TestAffixCounts() {
    std::printf("Affix counts and names per rarity\n");
    for (int i = 0; i < 2000; i++) {
        Item c = RollItemOfRarity(4, Rarity::Common, Rng());
        CHECK(AffixCount(c) == 0);
        CHECK(c.name == BaseOf(c).name);

        Item u = RollItemOfRarity(4, Rarity::Uncommon, Rng());
        CHECK(AffixCount(u) >= 1 && AffixCount(u) <= 2);
        CHECK(u.name.find(BaseOf(u).name) != std::string::npos); // "Prefix Base of Suffix"

        Item r = RollItemOfRarity(4, Rarity::Rare, Rng());
        CHECK(AffixCount(r) >= 3 && AffixCount(r) <= 4);
        CHECK(r.name.find(' ') != std::string::npos && r.name != BaseOf(r).name);

        Item l = RollItemOfRarity(4, Rarity::Legendary, Rng());
        CHECK(BaseOf(l).legendary);
        CHECK(AffixCount(l) == 0);
        CHECK(l.name == BaseOf(l).name);
    }
}

static void TestAffixesFitTheirItem() {
    std::printf("Affixes only roll where they make sense\n");
    for (int i = 0; i < 3000; i++) {
        Item it = RollItemOfRarity(6, Rarity::Rare, Rng());
        const ItemBase& b = BaseOf(it);
        std::set<std::string> seen;
        for (size_t m = b.mods.size(); m < it.mods.size(); m++) {
            const StatMod& mod = it.mods[m];
            seen.insert(DescribeMod(mod));
            if (mod.kind == StatKind::Resist) {
                CHECK(b.slot != Slot::Weapon);
            }
            if (mod.kind == StatKind::CritPct || mod.kind == StatKind::WeaponPower) {
                CHECK(b.slot == Slot::Weapon);
            }
        }
        CHECK(seen.size() == it.mods.size() - b.mods.size()); // no duplicate affixes
    }
}

static void TestTiersFollowLevel() {
    std::printf("Base tier follows item level\n");
    int early = 0, late = 0;
    for (int i = 0; i < 2000; i++) {
        early += BaseOf(RollItemOfRarity(3, Rarity::Common, Rng())).tier;
        late += BaseOf(RollItemOfRarity(20, Rarity::Common, Rng())).tier;
    }
    CHECK(early < late);
    CHECK(early < 2000 * 1.5);
}

static void TestEquipRules() {
    std::printf("Who can equip what\n");
    Item sword = MakeCommon(BaseId::KokiriSword);
    Item staff = MakeCommon(BaseId::ForestStaff);
    Item charm = MakeCommon(20);
    CHECK(CanEquip(sword, "Link") && !CanEquip(sword, "Saria"));
    CHECK(CanEquip(staff, "Saria") && !CanEquip(staff, "Link"));
    CHECK(CanEquip(charm, "Link") && CanEquip(charm, "Saria"));
    CHECK(sword.uid != staff.uid);
}

static void TestApplyGear() {
    std::printf("Gear changes battle stats\n");
    Combatant link = MakeLink(5);
    int st = link.st, maxHp = link.maxHp;

    Item sword = MakeCommon(BaseId::KokiriSword);
    Item tunic = MakeCommon(BaseId::KokiriTunic);
    Item goron = MakeCommon(102); // Goron Tunic: Resist Fire
    Combatant c = link;
    ApplyGear(c, { &sword, &tunic });
    CHECK(c.weaponPower == 3);
    CHECK(c.maxHp == maxHp + 6 && c.vi == link.vi + 1);
    CHECK(c.st == st);

    // Weapon power raises damage
    Combatant baba = MakeEnemy(EnemyKind::DekuBaba);
    Battle b({ link }, { baba }, [] { return 0.5f; });
    int bare = b.BaseDamage(link, baba, link.skills[0]);
    int armed = b.BaseDamage(c, baba, c.skills[0]);
    CHECK(armed > bare);

    // Resist covers a weakness (Saria is weak to Fire) and resists otherwise
    Combatant saria = MakeSaria(5);
    StatMod resistFire{ StatKind::Resist, 0, Element::Fire };
    Item ember = MakeCommon(20);
    ember.mods.push_back(resistFire);
    Combatant s2 = saria;
    ApplyGear(s2, { &ember });
    CHECK(saria.AffinityTo(Element::Fire) == Affinity::Weak);
    CHECK(s2.AffinityTo(Element::Fire) == Affinity::Normal);
    Combatant l2 = link;
    ApplyGear(l2, { &goron });
    CHECK(l2.AffinityTo(Element::Fire) == Affinity::Resist);
    // Never weakens a better affinity (Saria drains Wind)
    Item gale = MakeCommon(20);
    gale.mods.push_back({ StatKind::Resist, 0, Element::Wind });
    Combatant s3 = saria;
    ApplyGear(s3, { &gale });
    CHECK(s3.AffinityTo(Element::Wind) == Affinity::Drain);

    // Healing bonus
    Item ocarina = MakeCommon(101); // +50% healing, +9 magic
    Combatant s4 = saria;
    ApplyGear(s4, { &ocarina });
    CHECK(s4.healBonus > 0.49f && s4.magicPower == 9);
}

static void TestDescriptions() {
    std::printf("Descriptions\n");
    Item goron = MakeCommon(102);
    std::vector<std::string> lines = DescribeItem(goron);
    CHECK(lines.size() == 3);
    CHECK(lines[2] == "Resist Fire");
    CHECK(DescribeMod({ StatKind::CritPct, 15 }) == "Critical +15%");
    CHECK(RarityColor(Rarity::Legendary).r == 255);
}

static void TestLore() {
    std::printf("Descriptions and lore\n");
    for (const ItemBase& b : AllBases()) {
        CHECK(!b.description.empty());
        if (b.legendary) {
            CHECK(b.lore.size() > b.description.size());
        }
    }
    for (int i = 0; i < 200; i++) {
        Item r = RollItemOfRarity(5, Rarity::Rare, Rng());
        std::string lore = ItemLore(r);
        CHECK(lore.find(BaseOf(r).name) != std::string::npos); // "Once an ordinary <base>..."
        CHECK(lore.find("Something changed it") == std::string::npos); // every first word has a history
        Item u = RollItemOfRarity(5, Rarity::Uncommon, Rng());
        CHECK(ItemLore(u) == ItemDescription(u));
    }
    CHECK(ItemLore(MakeCommon(100)).find("Biggoron") != std::string::npos);
}

static void TestParty() {
    std::printf("Party equipment\n");
    Party_Reset();
    Party_EnsureCreated(5);
    std::vector<PartyMember>& members = Party_Members();
    CHECK(members.size() == 2);
    CHECK(members[0].gear[(size_t)Slot::Weapon].has_value()); // starting Kokiri Sword
    Combatant start = Party_Effective(members[0]);
    CHECK(start.weaponPower == 3);
    CHECK(start.hp == start.maxHp); // healed including gear HP

    // Equip a bigger sword: the old one goes to the bag
    Item blade = MakeCommon(2); // Hylian Blade, Attack +6
    Party_Bag().push_back(blade);
    CHECK(Party_Equip(0, 0));
    CHECK(Party_Effective(members[0]).weaponPower == 6);
    CHECK(Party_Bag().size() == 1 && Party_Bag()[0].baseId == BaseId::KokiriSword);

    // Saria can't wear Link's sword
    CHECK(!Party_Equip(1, 0));
    CHECK(Party_Bag().size() == 1);

    // Removing armour lowers max HP and clamps current HP
    int hpWith = Party_Effective(members[0]).hp;
    Party_Unequip(0, Slot::Armor);
    Combatant after = Party_Effective(members[0]);
    CHECK(after.maxHp == start.maxHp - 6);
    CHECK(after.hp <= after.maxHp && hpWith > after.hp);
    CHECK(Party_Bag().size() == 2);

    // Effects
    CHECK(!Party_HasEffect(UniqueEffect::AutoAnalyze));
    Party_Bag().push_back(MakeCommon(103)); // Mask of Truth
    CHECK(Party_Equip(1, (int)Party_Bag().size() - 1));
    CHECK(Party_HasEffect(UniqueEffect::AutoAnalyze));
    Party_Reset();
    CHECK(Party_Members().empty() && Party_Bag().empty());
}

int main() {
    TestRarityOdds();
    TestAffixCounts();
    TestAffixesFitTheirItem();
    TestTiersFollowLevel();
    TestEquipRules();
    TestApplyGear();
    TestDescriptions();
    TestParty();
    TestLore();
    std::printf("\n%d checks, %d failed\n", sChecks, sFailures);

    std::printf("\nSample drops (level 5):\n");
    for (int i = 0; i < 12; i++) {
        Item it = RollItem(5, DropSource::Common, 0.3f, Rng());
        std::printf("  [%-9s] %-30s", RarityName(it.rarity), it.name.c_str());
        for (const std::string& l : DescribeItem(it)) {
            std::printf(" %s;", l.c_str());
        }
        std::printf("\n");
    }
    return sFailures == 0 ? 0 : 1;
}
