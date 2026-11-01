// Nocturne of Time: unit tests for loot generation, gear, consumables and the moon. Not part of the game build.
// Build and run (from this folder):
//   N=../../soh/soh/Enhancements/NocturneOfTime
//   g++ -std=c++20 LootTests.cpp $N/Loot/Loot.cpp $N/Party.cpp $N/Items/Consumables.cpp $N/Moon/Moon.cpp \
//       $N/Save/NocturneSave.cpp $N/Battle/BattleEngine.cpp $N/Battle/BattleData.cpp $N/Relics/Relics.cpp \
//       -o loot_tests
//   ./loot_tests

#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Loot/Loot.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Party.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Moon/Moon.h"
#include "../../soh/soh/Enhancements/NocturneOfTime/Save/NocturneSave.h"

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
    CHECK(boss[Rarity::Uncommon] == 0 && boss[Rarity::Rare] > boss[Rarity::Legendary]); // purple or better

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
        CHECK(AffixCount(l) == kLegendaryAffixes);
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
    CHECK(l2.AffinityTo(Element::Fire) == Affinity::Drain); // its legendary power
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
    CHECK(lines.size() == 2); // Vitality, max HP (its Fire power is its effect text)
    CHECK(!BaseOf(goron).effectText.empty());
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
    // Every Rare name has a story of its own: no two names share one, and none is the plain description
    std::map<std::string, std::string> seen;
    for (int i = 0; i < 6000; i++) {
        Item r = RollItemOfRarity(5, Rarity::Rare, Rng());
        std::string lore = ItemLore(r);
        CHECK(lore != ItemDescription(r));
        CHECK(lore.size() <= 128); // one page of the item panel
        auto it = seen.find(r.name);
        if (it == seen.end()) {
            for (const auto& [name, other] : seen) {
                CHECK(other != lore);
            }
            seen[r.name] = lore;
        } else {
            CHECK(it->second == lore); // the same name always tells the same story
        }
        Item u = RollItemOfRarity(5, Rarity::Uncommon, Rng());
        CHECK(ItemLore(u) == ItemDescription(u));
    }
    CHECK(seen.size() == 150); // 10 first words x 5 second words x 3 slots
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
    // Skills come with levels: at 5 Saria knows Mire Hex (5) but not Brittle Hex (10)
    auto knows = [](const Combatant& c, const char* n) {
        for (const Skill& sk : c.skills) {
            if (sk.name == n) {
                return true;
            }
        }
        return false;
    };
    Combatant saria5 = Party_Effective(members[1]);
    CHECK(knows(saria5, "Mire Hex") && !knows(saria5, "Brittle Hex"));
    CHECK(saria5.skills[0].name == "Attack");

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

static void TestConsumables() {
    std::printf("Consumables\n");
    Party_Reset();
    Party_EnsureCreated(5);
    auto& counts = Party_Consumables();
    CHECK(counts[(size_t)ConsumableId::RedPotion] == 3 && counts[(size_t)ConsumableId::HylianHerb] == 1);

    // Using a potion on someone at full HP does nothing and costs nothing
    CHECK(Party_UseConsumable(ConsumableId::RedPotion, 0).empty());
    CHECK(counts[(size_t)ConsumableId::RedPotion] == 3);
    Party_Members()[0].base.hp = 10;
    std::vector<std::string> r = Party_UseConsumable(ConsumableId::RedPotion, 0);
    CHECK(!r.empty() && Party_Members()[0].base.hp == 70 && counts[(size_t)ConsumableId::RedPotion] == 2);

    // Poison carries on the party and is cured by an herb; battle-only items are refused outside battle
    Party_Members()[1].base.ailment = Ailment::Poison;
    CHECK(!Party_UseConsumable(ConsumableId::HylianHerb, 1).empty());
    CHECK(Party_Members()[1].base.ailment == Ailment::None && counts[(size_t)ConsumableId::HylianHerb] == 0);
    Party_AddConsumable(ConsumableId::BottledFairy);
    CHECK(Party_UseConsumable(ConsumableId::BottledFairy, 0).empty());

    // Carry limit
    CHECK(Party_AddConsumable(ConsumableId::GreenPotion, 50) == kMaxCarry);
    CHECK(Party_AddConsumable(ConsumableId::GreenPotion) == 0);

    // Drops: every kind turns up, Red Potions most often, Blue Potions rarely
    std::map<ConsumableId, int> seen;
    LootRng rng = Rng();
    for (int i = 0; i < 20000; i++) {
        seen[RollConsumable(rng)]++;
    }
    CHECK((int)seen.size() == (int)ConsumableId::Count);
    CHECK(seen[ConsumableId::RedPotion] > seen[ConsumableId::HylianHerb]);
    CHECK(seen[ConsumableId::BluePotion] < 20000 * 0.05);
    for (size_t i = 0; i < (size_t)ConsumableId::Count; i++) {
        CHECK(!ConsumableOf((ConsumableId)i).name.empty() && !ConsumableOf((ConsumableId)i).description.empty());
    }
    Party_Reset();
}

static void TestMoon() {
    std::printf("Moon phases\n");
    Moon_Reset();
    CHECK(Moon_Phase() == MoonPhase::New && Moon_Fullness() == 0.0f);
    CHECK(Moon_EncounterScale() < 0.61f && !Moon_BlocksEscape() && Moon_DropScale() == 1.0f);
    // New Moon: mostly lone enemies
    auto counts = [] {
        std::array<int, 4> n{};
        for (int i = 0; i < 1000; i++) {
            n[Moon_EnemyCount(i / 1000.0f)]++;
        }
        return n;
    };
    std::array<int, 4> dark = counts();
    CHECK(dark[1] > dark[2] && dark[2] > dark[3] && dark[0] == 0);
    // New -> Full over 7 phases, back down to New over 7 more; each phase lasts kMoonStepsPerPhase steps
    std::vector<int> seen;
    int changes = 0;
    for (int step = 0; step < kMoonStepsPerPhase * kMoonCycleLength; step++) {
        if (Moon_OnStep()) {
            changes++;
            seen.push_back((int)Moon_Phase());
        }
    }
    CHECK(changes == kMoonCycleLength);
    std::vector<int> expected = { 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0 };
    CHECK(seen == expected);
    CHECK(Moon_Phase() == MoonPhase::New);

    for (int i = 0; i < kMoonStepsPerPhase * 7; i++) {
        Moon_OnStep();
    }
    CHECK(Moon_Phase() == MoonPhase::Full && Moon_Fullness() == 1.0f && !Moon_Waxing());
    CHECK(Moon_EncounterScale() > 1.99f && Moon_LootLuck() == 1.0f && Moon_DropScale() > 1.59f);
    CHECK(Moon_BlocksEscape());
    // Full Moon: mostly packs of three, rarely alone
    std::array<int, 4> bright = counts();
    CHECK(bright[3] > bright[2] && bright[2] > bright[1] && bright[1] < 100);
    // A Full Moon tips common drops toward blue and purple, still never Legendary
    std::array<float, 4> full = RarityOdds(DropSource::Common, Moon_LootLuck());
    std::array<float, 4> noLuck = RarityOdds(DropSource::Common, 0.0f);
    CHECK(full[1] > noLuck[1] && full[2] > noLuck[2] * 3 && full[3] == 0.0f);
    CHECK(std::string(MoonPhaseName(MoonPhase::Half)) == "Half Moon");
    Moon_Reset();
}

static void TestSave() {
    std::printf("Save and load\n");
    Party_Reset();
    Moon_Reset();
    Party_EnsureCreated(5);
    std::vector<PartyMember>& members = Party_Members();
    // Make the state distinctive: hurt, levelled, poisoned/burned, new gear, items, a later moon
    members[0].base.level = 9;
    members[0].base.st = 21;
    members[0].xp = 1234;
    members[0].unspentStatPoints = 2;
    members[0].base.hp = 7;
    members[0].base.ailment = Ailment::Poison;
    members[1].base.mp = 3;
    members[1].base.ailment = Ailment::Burn;
    members[1].base.ailmentTurns = 2;
    for (int i = 0; i < 6; i++) {
        Party_Bag().push_back(RollItem(6, DropSource::Elite, 0.5f, Rng()));
    }
    Party_Bag().push_back(MakeCommon(2));
    CHECK(Party_Equip(0, (int)Party_Bag().size() - 1)); // Hylian Blade on Link
    Party_AddConsumable(ConsumableId::LonLonMilk, 4);
    Moon_SetState(5, 11);

    const std::vector<PartyMember> before = members;
    const std::vector<Item> bagBefore = Party_Bag();
    const auto stockBefore = Party_Consumables();
    nlohmann::json saved = Save_Write();
    CHECK(saved["version"] == kSaveVersion);

    // Through text, as on disk, into a clean state
    nlohmann::json reread = nlohmann::json::parse(saved.dump());
    Party_Reset();
    Moon_Reset();
    Save_Read(reread);

    CHECK(members.size() == before.size());
    for (size_t m = 0; m < members.size() && m < before.size(); m++) {
        const Combatant& a = members[m].base;
        const Combatant& b = before[m].base;
        CHECK(a.name == b.name && a.level == b.level && a.hp == b.hp && a.mp == b.mp && a.maxHp == b.maxHp);
        CHECK(a.st == b.st && a.ma == b.ma && a.vi == b.vi && a.ag == b.ag && a.lu == b.lu);
        CHECK(members[m].xp == before[m].xp && members[m].unspentStatPoints == before[m].unspentStatPoints);
        for (size_t s = 0; s < (size_t)Slot::Count; s++) {
            CHECK(members[m].gear[s].has_value() == before[m].gear[s].has_value());
            if (members[m].gear[s] && before[m].gear[s]) {
                CHECK(members[m].gear[s]->uid == before[m].gear[s]->uid);
                CHECK(members[m].gear[s]->name == before[m].gear[s]->name);
            }
        }
    }
    CHECK(members[0].base.ailment == Ailment::Poison); // poison lasts
    CHECK(members[1].base.ailment == Ailment::None);   // a burn is over once the battle is
    CHECK(Party_Effective(members[0]).weaponPower == 6);

    CHECK(Party_Bag().size() == bagBefore.size());
    for (size_t i = 0; i < Party_Bag().size() && i < bagBefore.size(); i++) {
        const Item& a = Party_Bag()[i];
        const Item& b = bagBefore[i];
        CHECK(a.uid == b.uid && a.baseId == b.baseId && a.rarity == b.rarity && a.level == b.level && a.name == b.name);
        CHECK(a.mods.size() == b.mods.size());
        CHECK(DescribeItem(a) == DescribeItem(b));
    }
    CHECK(Party_Consumables() == stockBefore);
    CHECK(Moon_CyclePos() == 5 && Moon_StepsInPhase() == 11);

    // New drops never reuse a saved item's number
    uint32_t maxUid = 0;
    for (const Item& it : Party_Bag()) {
        maxUid = std::max(maxUid, it.uid);
    }
    CHECK(RollItem(3, DropSource::Common, 0.0f, Rng()).uid > maxUid);

    // Damaged or foreign data starts fresh instead of crashing
    Save_Read(nlohmann::json::parse("{\"party\": 5, \"bag\": [{\"x\": 1}], \"moon\": \"full\"}"));
    CHECK(Party_Members().empty() && Party_Bag().empty());
    CHECK(Moon_CyclePos() == 0);
    Save_Read(nlohmann::json());
    CHECK(Party_Members().empty());
    Party_Reset();
    Moon_Reset();
}

static void TestLegendaryPowers() {
    std::printf("Legendary powers\n");
    auto legend = [](int id) {
        for (int i = 0; i < 500; i++) {
            Item it = RollItemOfRarity(10, Rarity::Legendary, Rng());
            if (it.baseId == id) {
                return it;
            }
        }
        return Item();
    };
    // Legendary affixes are stronger than a Rare's of the same level
    Item tunic = legend(102);
    CHECK(tunic.baseId == 102);
    Combatant link = MakeLink(10);
    Item tunicCopy = tunic;
    ApplyGear(link, { &tunicCopy });
    CHECK(link.AffinityTo(Element::Fire) == Affinity::Drain);

    Item ocarina = legend(101);
    Combatant saria = MakeSaria(10);
    ApplyGear(saria, { &ocarina });
    CHECK(saria.healCures);
    Combatant hurt = MakeLink(10);
    hurt.hp = 10;
    hurt.ailment = Ailment::Poison;
    Battle h({ saria, hurt }, { MakeEnemy(EnemyKind::DekuBaba) }, [] { return 0.5f; });
    h.UseSkill(3, 1); // Song of Healing on Link
    CHECK(h.Party()[1].ailment == Ailment::None && h.Party()[1].hp > 10);

    Item edge = legend(100);
    Combatant swordsman = MakeLink(10);
    ApplyGear(swordsman, { &edge });
    CHECK(swordsman.ignoreResist);
    Combatant baba = MakeEnemy(EnemyKind::DekuBaba); // resists Strike, but Link's sword is Slash
    Combatant skull = MakeEnemy(EnemyKind::Skulltula); // resists Slash
    Battle e({ swordsman, MakeSaria(10) }, { skull }, [] { return 0.5f; });
    Events ev = e.UseSkill(0, 0);
    bool resisted = false;
    for (const BattleEvent& x : ev) {
        resisted |= x.type == BattleEvent::Type::Resist;
    }
    CHECK(!resisted);
    (void)baba;
}

static bool InPool(const PartyMember& m, const char* name, bool* permanent = nullptr) {
    for (const PoolSkill& p : Party_SkillPool(m)) {
        if (p.skill.name == name) {
            if (permanent) {
                *permanent = p.permanent;
            }
            return true;
        }
    }
    return false;
}

static bool InBattle(const PartyMember& m, const char* name) {
    for (const Skill& s : Party_Effective(m).skills) {
        if (s.name == name) {
            return true;
        }
    }
    return false;
}

static void TestRelicsAndSkills() {
    std::printf("Relics and active skills\n");
    Party_Reset();
    Party_EnsureCreated(5);
    PartyMember& link = Party_Members()[0];
    PartyMember& saria = Party_Members()[1];

    // Link starts with the Fairy Slingshot on: its first skill usable, not his for good yet
    CHECK(Party_Relics()[(size_t)RelicId::FairySlingshot].owned);
    CHECK(Party_EquippedRelic() == (int)RelicId::FairySlingshot);
    bool perm = true;
    CHECK(InPool(link, "Slingshot", &perm) && !perm);
    CHECK(!InPool(link, "Seed Scatter"));
    CHECK(InBattle(link, "Slingshot") && InBattle(link, "Jump Slash")); // sword skills come with levels
    Combatant eff = Party_Effective(link);
    CHECK(eff.AffinityTo(Element::Pierce) == Affinity::Resist && eff.AffinityTo(Element::Fire) == Affinity::Weak);
    // Relics give no stats
    Party_UnequipRelic();
    Combatant bare = Party_Effective(link);
    CHECK(bare.st == eff.st && bare.maxHp == eff.maxHp && bare.weaponPower == eff.weaponPower);
    CHECK(bare.AffinityTo(Element::Fire) == Affinity::Normal);
    CHECK(!InPool(link, "Slingshot") && !InBattle(link, "Slingshot")); // not mastered: gone with the Relic
    CHECK(Party_EquipRelic(RelicId::FairySlingshot));
    CHECK(InBattle(link, "Slingshot")); // back in the active list (it was offered before, and came back)

    // Mastery 1: Slingshot is Link's for good, Seed Scatter unlocks
    std::vector<std::string> lines = Party_AddRelicMastery(RelicOf(RelicId::FairySlingshot).thresholds[0]);
    CHECK(!lines.empty() && lines[0] == "Fairy Slingshot reached Mastery 1!\nSlingshot is now learned.");
    CHECK(InPool(link, "Slingshot", &perm) && perm);
    CHECK(InPool(link, "Seed Scatter", &perm) && !perm);
    CHECK(InBattle(link, "Seed Scatter")); // joined automatically: there was room
    Party_UnequipRelic();
    CHECK(InBattle(link, "Slingshot") && !InBattle(link, "Seed Scatter"));
    CHECK(!Party_EquipRelic(RelicId::DekuShield)); // not owned
    CHECK(Party_GiveRelic(RelicId::DekuShield) && !Party_GiveRelic(RelicId::DekuShield));
    CHECK(Party_EquipRelic(RelicId::DekuShield));
    CHECK(InBattle(link, "Shield Bash"));
    Combatant shield = Party_Effective(link);
    CHECK(shield.AffinityTo(Element::Slash) == Affinity::Resist && shield.AffinityTo(Element::Pierce) == Affinity::Normal);
    // Mastered all the way
    Party_AddRelicMastery(100000);
    CHECK(RelicMastery(RelicOf(RelicId::DekuShield), Party_Relics()[(size_t)RelicId::DekuShield].points) ==
          RelicOf(RelicId::DekuShield).MaxMastery());
    Party_UnequipRelic();
    CHECK(InPool(link, "Shield Charge") && InPool(link, "Shield Wall"));
    CHECK(Party_AddRelicMastery(50).empty()); // nothing equipped: nothing gained

    // Eight skills at most; taking one off keeps it off; the pool keeps everything
    saria.base.level = 20;
    Party_RefreshSkills(saria);
    CHECK((int)saria.activeSkills.size() == kMaxActiveSkills);
    CHECK(Party_SkillPool(saria).size() == 10);
    CHECK(!Party_ActivateSkill(saria, "Brittle Hex")); // full
    std::string out = saria.activeSkills[0];
    Party_DeactivateSkill(saria, out);
    CHECK(Party_ActivateSkill(saria, "Brittle Hex"));
    Party_RefreshSkills(saria);
    CHECK(!Party_IsActive(saria, out));
    CHECK((int)Party_Effective(saria).skills.size() == kMaxActiveSkills + 1); // + Attack
    CHECK(InBattle(saria, "Brittle Hex"));

    // Saved and loaded
    nlohmann::json saved = Save_Write();
    std::vector<std::string> activeBefore = saria.activeSkills;
    auto relicsBefore = Party_Relics();
    Save_Read(nlohmann::json::parse(saved.dump()));
    CHECK(Party_Members().size() == 2);
    CHECK(Party_Members()[1].activeSkills == activeBefore);
    CHECK(Party_EquippedRelic() == -1);
    for (size_t i = 0; i < relicsBefore.size(); i++) {
        CHECK(Party_Relics()[i].owned == relicsBefore[i].owned && Party_Relics()[i].points == relicsBefore[i].points);
    }
    // A save from before Relics: Link gets his Fairy Slingshot
    saved.erase("relics");
    saved.erase("relic");
    Save_Read(saved);
    CHECK(Party_EquippedRelic() == (int)RelicId::FairySlingshot);
    CHECK(InBattle(Party_Members()[0], "Slingshot"));
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
    TestConsumables();
    TestMoon();
    TestSave();
    TestLegendaryPowers();
    TestRelicsAndSkills();
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
