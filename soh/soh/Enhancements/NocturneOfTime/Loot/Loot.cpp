#include "Loot.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace Nocturne {

const char* RarityName(Rarity r) {
    switch (r) {
        case Rarity::Common:
            return "Common";
        case Rarity::Uncommon:
            return "Uncommon";
        case Rarity::Rare:
            return "Rare";
        case Rarity::Legendary:
            return "Legendary";
        default:
            return "?";
    }
}

Rgb RarityColor(Rarity r) {
    switch (r) {
        case Rarity::Uncommon:
            return { 90, 150, 255 }; // blue
        case Rarity::Rare:
            return { 190, 100, 255 }; // purple
        case Rarity::Legendary:
            return { 255, 150, 40 }; // orange
        case Rarity::Common:
        default:
            return { 235, 235, 235 }; // white
    }
}

const char* SlotName(Slot s) {
    switch (s) {
        case Slot::Weapon:
            return "Weapon";
        case Slot::Armor:
            return "Armor";
        case Slot::Accessory:
            return "Accessory";
        default:
            return "?";
    }
}

namespace {

StatMod Mod(StatKind kind, int value, Element element = Element::Slash) {
    return { kind, value, element };
}

ItemBase Base(int id, const char* name, Slot slot, Wielder wielder, int tier, std::vector<StatMod> mods,
              const char* description) {
    ItemBase b;
    b.description = description;
    b.id = id;
    b.name = name;
    b.slot = slot;
    b.wielder = wielder;
    b.tier = tier;
    b.mods = std::move(mods);
    return b;
}

ItemBase Legendary(int id, const char* name, Slot slot, Wielder wielder, std::vector<StatMod> mods,
                   UniqueEffect effect, const char* effectText, const char* description, const char* lore) {
    ItemBase b = Base(id, name, slot, wielder, 3, std::move(mods), description);
    b.lore = lore;
    b.legendary = true;
    b.effect = effect;
    b.effectText = effectText;
    return b;
}

std::vector<ItemBase> BuildBases() {
    using S = StatKind;
    return {
        // Link: swords and tunics
        Base(1, "Kokiri Sword", Slot::Weapon, Wielder::Link, 1, { Mod(S::WeaponPower, 3) },
             "A small blade forged by the Kokiri. Light and reliable."),
        Base(2, "Hylian Blade", Slot::Weapon, Wielder::Link, 2, { Mod(S::WeaponPower, 6) },
             "Standard steel of the Hylian guard."),
        Base(3, "Knight's Sword", Slot::Weapon, Wielder::Link, 3, { Mod(S::WeaponPower, 9) },
             "A broad blade once carried by Hyrule's knights."),
        Base(10, "Kokiri Tunic", Slot::Armor, Wielder::Link, 1, { Mod(S::Vi, 1), Mod(S::MaxHp, 6) },
             "Green cloth woven in the forest. Better than nothing."),
        Base(11, "Hylian Mail", Slot::Armor, Wielder::Link, 2, { Mod(S::Vi, 3), Mod(S::MaxHp, 10) },
             "Chain mail worn under a Hylian tabard."),
        Base(12, "Knight's Tunic", Slot::Armor, Wielder::Link, 3, { Mod(S::Vi, 5), Mod(S::MaxHp, 16) },
             "Padded plate from the castle armory."),
        // Saria: staves and robes
        Base(4, "Forest Staff", Slot::Weapon, Wielder::Saria, 1, { Mod(S::MagicPower, 3) },
             "A branch from the Lost Woods, still humming with life."),
        Base(5, "Fairy Ocarina", Slot::Weapon, Wielder::Saria, 2, { Mod(S::MagicPower, 5), Mod(S::MaxMp, 4) },
             "A simple ocarina. Songs carry further through it."),
        Base(6, "Sage's Rod", Slot::Weapon, Wielder::Saria, 3, { Mod(S::MagicPower, 8) },
             "A rod carved with the marks of the ancient sages."),
        Base(13, "Leaf Robe", Slot::Armor, Wielder::Saria, 1, { Mod(S::Vi, 1), Mod(S::MaxMp, 4) },
             "Woven from broad forest leaves. Light as air."),
        Base(14, "Forest Robe", Slot::Armor, Wielder::Saria, 2, { Mod(S::Vi, 2), Mod(S::MaxMp, 8) },
             "A mossy robe that soaks up the magic of the woods."),
        Base(15, "Sage's Robe", Slot::Armor, Wielder::Saria, 3,
             { Mod(S::Vi, 3), Mod(S::MaxMp, 12), Mod(S::MaxHp, 6) }, "Ceremonial robes of the Temple of Time."),
        // Anyone: accessories
        Base(20, "Deku Charm", Slot::Accessory, Wielder::Anyone, 1, { Mod(S::Lu, 2) },
             "A Deku Nut on a string. Said to bring luck."),
        Base(21, "Silver Ring", Slot::Accessory, Wielder::Anyone, 1, { Mod(S::MaxHp, 10) },
             "A plain silver band. It makes the wearer hardier."),
        Base(22, "Fairy Bracelet", Slot::Accessory, Wielder::Anyone, 2, { Mod(S::Ag, 2) },
             "Holds the faint glow of a fairy. Quickens the step."),
        Base(23, "Spirit Bead", Slot::Accessory, Wielder::Anyone, 2, { Mod(S::MaxMp, 6) },
             "A glass bead holding a small spark of spirit."),
        // Legendaries
        Legendary(100, "Biggoron's Edge", Slot::Weapon, Wielder::Link,
                  { Mod(S::WeaponPower, 14), Mod(S::CritPct, 15), Mod(S::St, 2) }, UniqueEffect::None,
                  "Critical hits come easily.", "The masterwork of the giant smith Biggoron.",
                  "Biggoron worked his forge atop Death Mountain for a year to make this blade, and swore he would "
                  "never make another. It never dulls, and the Gorons say it cuts truest under a full moon."),
        Legendary(101, "Fairy Ocarina of the Lost Woods", Slot::Weapon, Wielder::Saria,
                  { Mod(S::MagicPower, 9), Mod(S::HealPct, 50), Mod(S::MaxMp, 10) }, UniqueEffect::None,
                  "Healing songs restore half again as much.", "The first ocarina ever played in the Lost Woods.",
                  "Its songs once guided lost children home through the woods. Now they mend what the moon's curse "
                  "has broken, and the trees still lean in to listen."),
        Legendary(102, "Goron Tunic", Slot::Armor, Wielder::Link,
                  { Mod(S::Vi, 6), Mod(S::MaxHp, 20), Mod(S::Resist, 0, Element::Fire) }, UniqueEffect::None,
                  "Forged in Death Mountain: shrugs off flame.", "Fire-proof garb of the Goron smiths.",
                  "Woven from stone fibre in the heat of Death Mountain. Darunia's people wear it to walk the "
                  "molten heart of the mountain, and it has never once let the fire through."),
        Legendary(103, "Mask of Truth", Slot::Accessory, Wielder::Anyone, { Mod(S::Lu, 4) },
                  UniqueEffect::AutoAnalyze, "Reveals every foe's affinities when a battle starts.",
                  "A mask that sees through every lie.",
                  "Carved for one who would see beyond all deceit. Through its eyes no creature can hide what it "
                  "fears, and those who wear it too long begin to hear what others will not say."),
    };
}

// ---- Affixes --------------------------------------------------------------------------------------------

enum class AffixPlace { Prefix, Suffix };

struct Affix {
    const char* word;
    AffixPlace place;
    StatKind kind;
    Element element; // for Resist
    // which slots can roll it
    bool weapon, armor, accessory;
    // weapons: only bases that already have this power (no Sharp staves)
    bool needsWeaponPower = false, needsMagicPower = false;
};

const std::vector<Affix>& Affixes() {
    using S = StatKind;
    using E = Element;
    static const std::vector<Affix> list = {
        { "Mighty", AffixPlace::Prefix, S::St, E::Slash, true, true, true },
        { "Mystic", AffixPlace::Prefix, S::Ma, E::Slash, true, true, true },
        { "Sturdy", AffixPlace::Prefix, S::Vi, E::Slash, false, true, true },
        { "Swift", AffixPlace::Prefix, S::Ag, E::Slash, true, true, true },
        { "Lucky", AffixPlace::Prefix, S::Lu, E::Slash, true, false, true },
        { "Keen", AffixPlace::Prefix, S::CritPct, E::Slash, true, false, false, true, false },
        { "Sharp", AffixPlace::Prefix, S::WeaponPower, E::Slash, true, false, false, true, false },
        { "Arcane", AffixPlace::Prefix, S::MagicPower, E::Slash, true, false, false, false, true },
        { "of Vigor", AffixPlace::Suffix, S::MaxHp, E::Slash, true, true, true },
        { "of the Spring", AffixPlace::Suffix, S::MaxMp, E::Slash, true, true, true },
        { "of Embers", AffixPlace::Suffix, S::Resist, E::Fire, false, true, true },
        { "of Gales", AffixPlace::Suffix, S::Resist, E::Wind, false, true, true },
        { "of Thunder", AffixPlace::Suffix, S::Resist, E::Shock, false, true, true },
        { "of Frost", AffixPlace::Suffix, S::Resist, E::Ice, false, true, true },
        { "of Fortune", AffixPlace::Suffix, S::Lu, E::Slash, false, true, true },
        { "of Mending", AffixPlace::Suffix, S::HealPct, E::Slash, true, false, true, false, true },
    };
    return list;
}

bool HasMod(const ItemBase& base, StatKind kind) {
    for (const StatMod& m : base.mods) {
        if (m.kind == kind) {
            return true;
        }
    }
    return false;
}

bool AffixFits(const Affix& a, const ItemBase& base) {
    bool slotOk = (base.slot == Slot::Weapon && a.weapon) || (base.slot == Slot::Armor && a.armor) ||
                  (base.slot == Slot::Accessory && a.accessory);
    if (!slotOk) {
        return false;
    }
    if (a.needsWeaponPower && !HasMod(base, StatKind::WeaponPower)) {
        return false;
    }
    if (a.needsMagicPower && !HasMod(base, StatKind::MagicPower)) {
        return false;
    }
    return true;
}

// Affix strength grows with item level
int AffixValue(StatKind kind, int level) {
    switch (kind) {
        case StatKind::MaxHp:
            return 6 + level;
        case StatKind::MaxMp:
            return 4 + level / 2;
        case StatKind::WeaponPower:
        case StatKind::MagicPower:
            return 2 + level / 4;
        case StatKind::CritPct:
            return 4 + level / 3;
        case StatKind::HealPct:
            return 10 + level;
        case StatKind::Resist:
            return 0;
        default: // core stats
            return 1 + level / 5;
    }
}

int Pick(int n, const LootRng& rng) {
    return std::min(n - 1, (int)(rng() * n));
}

const char* kRareFirst[] = { "Gloom", "Doom", "Dusk", "Grim", "Storm", "Ember", "Bone", "Shadow", "Moon", "Ruin" };
const char* kRareWeapon[] = { "Fang", "Edge", "Bite", "Thorn", "Song" };
const char* kRareArmor[] = { "Shell", "Ward", "Mantle", "Hide", "Veil" };
const char* kRareAccessory[] = { "Charm", "Eye", "Loop", "Sigil", "Knot" };

uint32_t sNextUid = 1;

} // namespace

const std::vector<ItemBase>& AllBases() {
    static const std::vector<ItemBase> bases = BuildBases();
    return bases;
}

const ItemBase& BaseOf(const Item& item) {
    for (const ItemBase& b : AllBases()) {
        if (b.id == item.baseId) {
            return b;
        }
    }
    return AllBases().front();
}

std::array<float, 4> RarityOdds(DropSource source, float luck) {
    luck = std::clamp(luck, 0.0f, 1.0f);
    std::array<float, 4> w;
    switch (source) {
        case DropSource::Boss:
            w = { 0.0f, 40.0f - 10.0f * luck, 45.0f, 15.0f + 10.0f * luck };
            break;
        case DropSource::Elite:
            w = { 40.0f - 15.0f * luck, 40.0f, 17.0f + 10.0f * luck, 3.0f + 5.0f * luck };
            break;
        case DropSource::Common:
        default:
            // Mostly whites and blues, nearly even; purples are a rare treat; never Legendary
            w = { 52.0f - 27.0f * luck, 44.0f + 16.0f * luck, 4.0f + 11.0f * luck, 0.0f };
            break;
    }
    float total = w[0] + w[1] + w[2] + w[3];
    for (float& v : w) {
        v = v * 100.0f / total;
    }
    return w;
}

Rarity RollRarity(DropSource source, float luck, const LootRng& rng) {
    std::array<float, 4> odds = RarityOdds(source, luck);
    float roll = rng() * 100.0f;
    for (int i = 0; i < 4; i++) {
        if (odds[i] > 0.0f && roll < odds[i]) {
            return (Rarity)i;
        }
        roll -= odds[i];
    }
    return odds[0] > 0.0f ? Rarity::Common : Rarity::Uncommon;
}

Item MakeCommon(int baseId) {
    Item item;
    item.uid = sNextUid++;
    item.baseId = baseId;
    item.rarity = Rarity::Common;
    const ItemBase& b = BaseOf(item);
    item.name = b.name;
    item.mods = b.mods;
    return item;
}

Item RollItemOfRarity(int itemLevel, Rarity rarity, const LootRng& rng) {
    const std::vector<ItemBase>& bases = AllBases();
    Item item;
    item.uid = sNextUid++;
    item.level = std::max(1, itemLevel);
    item.rarity = rarity;

    if (rarity == Rarity::Legendary) {
        std::vector<const ItemBase*> legends;
        for (const ItemBase& b : bases) {
            if (b.legendary) {
                legends.push_back(&b);
            }
        }
        const ItemBase& b = *legends[Pick((int)legends.size(), rng)];
        item.baseId = b.id;
        item.name = b.name;
        item.mods = b.mods;
        return item;
    }

    // Base tier follows the item level, with a chance of one tier higher
    int tier = std::clamp(1 + item.level / 8, 1, 3);
    if (tier < 3 && rng() < 0.25f) {
        tier++;
    }
    std::vector<const ItemBase*> pool;
    for (const ItemBase& b : bases) {
        if (!b.legendary && b.tier == tier) {
            pool.push_back(&b);
        }
    }
    if (pool.empty()) {
        for (const ItemBase& b : bases) {
            if (!b.legendary) {
                pool.push_back(&b);
            }
        }
    }
    const ItemBase& base = *pool[Pick((int)pool.size(), rng)];
    item.baseId = base.id;
    item.mods = base.mods;

    int affixCount = 0;
    if (rarity == Rarity::Uncommon) {
        affixCount = 1 + (rng() < 0.5f ? 1 : 0);
    } else if (rarity == Rarity::Rare) {
        affixCount = 3 + (rng() < 0.5f ? 1 : 0);
    }

    // Choose distinct affixes; Uncommon items take at most one prefix and one suffix so the name reads
    std::vector<const Affix*> candidates;
    for (const Affix& a : Affixes()) {
        if (AffixFits(a, base)) {
            candidates.push_back(&a);
        }
    }
    const Affix* prefix = nullptr;
    const Affix* suffix = nullptr;
    std::set<const Affix*> chosen;
    int guard = 0;
    while ((int)chosen.size() < affixCount && !candidates.empty() && guard++ < 64) {
        const Affix* a = candidates[Pick((int)candidates.size(), rng)];
        if (chosen.count(a)) {
            continue;
        }
        // One affix per stat (resists may repeat for different elements)
        bool sameStat = false;
        for (const Affix* c : chosen) {
            if (c->kind == a->kind && (a->kind != StatKind::Resist || c->element == a->element)) {
                sameStat = true;
            }
        }
        if (sameStat) {
            continue;
        }
        if (rarity == Rarity::Uncommon) {
            if ((a->place == AffixPlace::Prefix && prefix) || (a->place == AffixPlace::Suffix && suffix)) {
                continue;
            }
        }
        chosen.insert(a);
        if (a->place == AffixPlace::Prefix && !prefix) {
            prefix = a;
        } else if (a->place == AffixPlace::Suffix && !suffix) {
            suffix = a;
        }
        item.mods.push_back(Mod(a->kind, AffixValue(a->kind, item.level), a->element));
    }

    if (rarity == Rarity::Rare) {
        const char* first = kRareFirst[Pick(10, rng)];
        const char* second = base.slot == Slot::Weapon  ? kRareWeapon[Pick(5, rng)]
                             : base.slot == Slot::Armor ? kRareArmor[Pick(5, rng)]
                                                        : kRareAccessory[Pick(5, rng)];
        item.name = std::string(first) + " " + second;
    } else {
        item.name = base.name;
        if (prefix) {
            item.name = std::string(prefix->word) + " " + item.name;
        }
        if (suffix) {
            item.name += std::string(" ") + suffix->word;
        }
    }
    return item;
}

Item RollItem(int itemLevel, DropSource source, float luck, const LootRng& rng) {
    return RollItemOfRarity(itemLevel, RollRarity(source, luck, rng), rng);
}

bool CanEquip(const Item& item, const std::string& memberName) {
    const ItemBase& b = BaseOf(item);
    switch (b.wielder) {
        case Wielder::Link:
            return memberName == "Link";
        case Wielder::Saria:
            return memberName == "Saria";
        case Wielder::Anyone:
        default:
            return true;
    }
}

void ApplyGear(Combatant& c, const std::vector<const Item*>& gear) {
    for (const Item* item : gear) {
        if (item == nullptr) {
            continue;
        }
        for (const StatMod& m : item->mods) {
            switch (m.kind) {
                case StatKind::St:
                    c.st += m.value;
                    break;
                case StatKind::Ma:
                    c.ma += m.value;
                    break;
                case StatKind::Vi:
                    c.vi += m.value;
                    break;
                case StatKind::Ag:
                    c.ag += m.value;
                    break;
                case StatKind::Lu:
                    c.lu += m.value;
                    break;
                case StatKind::MaxHp:
                    c.maxHp += m.value;
                    break;
                case StatKind::MaxMp:
                    c.maxMp += m.value;
                    break;
                case StatKind::WeaponPower:
                    c.weaponPower += m.value;
                    break;
                case StatKind::MagicPower:
                    c.magicPower += m.value;
                    break;
                case StatKind::CritPct:
                    c.critBonus += m.value / 100.0f;
                    break;
                case StatKind::HealPct:
                    c.healBonus += m.value / 100.0f;
                    break;
                case StatKind::Resist: {
                    // Covers a weakness, or resists a normal hit; never weakens a better affinity
                    Affinity& a = c.affinity[(size_t)m.element];
                    if (a == Affinity::Weak) {
                        a = Affinity::Normal;
                    } else if (a == Affinity::Normal) {
                        a = Affinity::Resist;
                    }
                    break;
                }
            }
        }
    }
}

std::string ItemDescription(const Item& item) {
    return BaseOf(item).description;
}

std::string ItemLore(const Item& item) {
    const ItemBase& b = BaseOf(item);
    if (b.legendary) {
        return b.lore;
    }
    if (item.rarity != Rarity::Rare) {
        return b.description;
    }
    // Rare items: a history from the first word of their name
    static const std::pair<const char*, const char*> kHistory[] = {
        { "Gloom", "The endless gloom of the Lost Woods still clings to it." },
        { "Doom", "Every hand that carried it before met a grim end." },
        { "Dusk", "It seems to darken as evening falls." },
        { "Grim", "Scarred and pitted from countless battles." },
        { "Storm", "The air crackles faintly around it." },
        { "Ember", "Faintly warm, as if pulled from a dying fire." },
        { "Bone", "Worked with pale bone from no beast anyone can name." },
        { "Shadow", "It casts no shadow of its own." },
        { "Moon", "It glows softly under the moon, brightest when it is full." },
        { "Ruin", "Recovered from the ruins of a fallen part of Hyrule." },
    };
    std::string history = "Something changed it long ago.";
    for (const auto& [word, text] : kHistory) {
        if (item.name.rfind(word, 0) == 0) {
            history = text;
            break;
        }
    }
    const char* article = (b.name[0] == 'A' || b.name[0] == 'E' || b.name[0] == 'I' || b.name[0] == 'O' ||
                           b.name[0] == 'U')
                              ? "an"
                              : "a";
    return history + " Once " + article + " ordinary " + b.name + ", it is something else now.";
}

std::string DescribeMod(const StatMod& m) {
    auto plus = [](const char* label, int v, const char* unit = "") {
        return std::string(label) + " +" + std::to_string(v) + unit;
    };
    switch (m.kind) {
        case StatKind::St:
            return plus("St", m.value);
        case StatKind::Ma:
            return plus("Ma", m.value);
        case StatKind::Vi:
            return plus("Vi", m.value);
        case StatKind::Ag:
            return plus("Ag", m.value);
        case StatKind::Lu:
            return plus("Lu", m.value);
        case StatKind::MaxHp:
            return plus("Max HP", m.value);
        case StatKind::MaxMp:
            return plus("Max MP", m.value);
        case StatKind::WeaponPower:
            return plus("Attack", m.value);
        case StatKind::MagicPower:
            return plus("Magic", m.value);
        case StatKind::CritPct:
            return plus("Critical", m.value, "%");
        case StatKind::HealPct:
            return plus("Healing", m.value, "%");
        case StatKind::Resist:
            return std::string("Resist ") + ElementName(m.element);
    }
    return "";
}

std::vector<std::string> DescribeItem(const Item& item) {
    std::vector<std::string> lines;
    for (const StatMod& m : item.mods) {
        lines.push_back(DescribeMod(m));
    }
    return lines;
}

} // namespace Nocturne
