#include "Consumables.h"

#include <array>

namespace Nocturne {

namespace {

ItemEffect Heal(int hp, int mp, TargetType target = TargetType::OneAlly) {
    ItemEffect e;
    e.target = target;
    e.hp = hp;
    e.mp = mp;
    return e;
}

ItemEffect Cure(Ailment a) {
    ItemEffect e;
    e.cures = a;
    return e;
}

std::array<ConsumableDef, (size_t)ConsumableId::Count> MakeAll() {
    std::array<ConsumableDef, (size_t)ConsumableId::Count> all{};
    all[(size_t)ConsumableId::RedPotion] = { ConsumableId::RedPotion, "Red Potion",
                                             "A bitter red tonic. Restores 60 HP to one ally.", Heal(60, 0), false,
                                             40 };
    all[(size_t)ConsumableId::GreenPotion] = { ConsumableId::GreenPotion, "Green Potion",
                                               "Brewed from forest herbs. Restores 25 MP to one ally.", Heal(0, 25),
                                               false, 16 };
    ItemEffect blue;
    blue.fullHp = true;
    blue.fullMp = true;
    all[(size_t)ConsumableId::BluePotion] = { ConsumableId::BluePotion, "Blue Potion",
                                              "The finest potion there is. Fully restores one ally's HP and MP.",
                                              blue, false, 3 };
    all[(size_t)ConsumableId::LonLonMilk] = { ConsumableId::LonLonMilk, "Lon Lon Milk",
                                              "Fresh from the ranch, shared around. Restores 40 HP to the party.",
                                              Heal(40, 0, TargetType::AllAllies), false, 8 };
    all[(size_t)ConsumableId::HylianHerb] = { ConsumableId::HylianHerb, "Hylian Herb",
                                              "A pungent leaf that draws out venom. Cures Poison.",
                                              Cure(Ailment::Poison), false, 18 };
    all[(size_t)ConsumableId::ZoraWater] = { ConsumableId::ZoraWater, "Zora's Water",
                                             "Cold water from Zora's Domain. Soothes and cures a Burn.",
                                             Cure(Ailment::Burn), true, 10 };
    ItemEffect fairy;
    fairy.revive = 0.5f;
    all[(size_t)ConsumableId::BottledFairy] = { ConsumableId::BottledFairy, "Bottled Fairy",
                                                "A fairy in a bottle. Revives a fallen ally with half their HP.",
                                                fairy, true, 5 };
    return all;
}

const std::array<ConsumableDef, (size_t)ConsumableId::Count>& All() {
    static const auto all = MakeAll();
    return all;
}

} // namespace

const ConsumableDef& ConsumableOf(ConsumableId id) {
    size_t i = (size_t)id < All().size() ? (size_t)id : 0;
    return All()[i];
}

ConsumableId RollConsumable(const std::function<float()>& rand01) {
    int total = 0;
    for (const ConsumableDef& d : All()) {
        total += d.dropWeight;
    }
    float roll = rand01() * (float)total;
    for (const ConsumableDef& d : All()) {
        if (roll < (float)d.dropWeight) {
            return d.id;
        }
        roll -= (float)d.dropWeight;
    }
    return ConsumableId::RedPotion;
}

} // namespace Nocturne
