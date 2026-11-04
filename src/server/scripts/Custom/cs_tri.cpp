/*
 * cs_tri.cpp - Rangement automatique des sacs et de la banque (style retail)
 *
 * Commandes :
 *   .tri          range les sacs (sac a dos + sacs generiques)
 *   .tri banque   range la banque (il faut etre a cote d'un banquier)
 *
 * 1) Regroupe les piles incompletes d'un meme objet.
 * 2) Trie : consommables, artisanat, gemmes, glyphes, recettes, equipement,
 *    sacs, quete, divers, puis objets gris a la fin.
 *    Dans chaque groupe : sous-classe, qualite (desc), niveau d'objet (desc), ID.
 *
 * Les sacs speciaux (herboriste, mineur, etc.) et les objets equipes ne sont
 * pas touches. Tous les deplacements passent par Player::SwapItem, qui fait
 * les memes verifications que lorsqu'un joueur deplace un objet a la main.
 */

#include "ScriptMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"
#include <algorithm>
#include <vector>

namespace
{
    // Permissions RBAC custom (plage 1000+), voir le fichier SQL fourni
    constexpr uint32 RBAC_PERM_COMMAND_TRI        = 1000;
    constexpr uint32 RBAC_PERM_COMMAND_TRI_BANQUE = 1001;

    constexpr float BANKER_SEARCH_RANGE = 15.0f;
    constexpr uint32 MAX_MOVES = 2000; // garde-fou

    // Ordre des groupes, proche de retail
    uint32 GroupRank(ItemTemplate const* proto)
    {
        if (proto->GetQuality() == ITEM_QUALITY_POOR)
            return 100; // camelote toujours a la fin

        switch (proto->GetClass())
        {
            case ITEM_CLASS_CONSUMABLE: return 0;
            case ITEM_CLASS_TRADE_GOODS: return 1;
            case ITEM_CLASS_GEM:         return 2;
            case ITEM_CLASS_GLYPH:       return 3;
            case ITEM_CLASS_RECIPE:      return 4;
            case ITEM_CLASS_WEAPON:      return 5;
            case ITEM_CLASS_ARMOR:       return 5;
            case ITEM_CLASS_CONTAINER:   return 6;
            case ITEM_CLASS_QUEST:       return 7;
            case ITEM_CLASS_MISCELLANEOUS: return 8;
            default:                     return 9;
        }
    }

    // true si a doit etre range avant b
    bool SortBefore(Item const* a, Item const* b)
    {
        ItemTemplate const* pa = a->GetTemplate();
        ItemTemplate const* pb = b->GetTemplate();

        uint32 ga = GroupRank(pa), gb = GroupRank(pb);
        if (ga != gb) return ga < gb;
        if (pa->GetClass() != pb->GetClass()) return pa->GetClass() < pb->GetClass();
        if (pa->GetSubClass() != pb->GetSubClass()) return pa->GetSubClass() < pb->GetSubClass();
        if (pa->GetQuality() != pb->GetQuality()) return pa->GetQuality() > pb->GetQuality();
        if (pa->GetBaseItemLevel() != pb->GetBaseItemLevel()) return pa->GetBaseItemLevel() > pb->GetBaseItemLevel();
        if (a->GetEntry() != b->GetEntry()) return a->GetEntry() < b->GetEntry();
        if (a->GetCount() != b->GetCount()) return a->GetCount() > b->GetCount(); // piles pleines d'abord
        return a->GetPos() < b->GetPos(); // stable
    }

    bool IsGenericBag(Bag const* bag)
    {
        return bag && bag->GetTemplate()->GetClass() == ITEM_CLASS_CONTAINER
                   && bag->GetTemplate()->GetSubClass() == ITEM_SUBCLASS_CONTAINER;
    }

    uint16 MakePos(uint8 bag, uint8 slot) { return uint16(bag) << 8 | slot; }

    // Liste ordonnee des emplacements a trier
    std::vector<uint16> CollectSlots(Player* player, bool bank)
    {
        std::vector<uint16> slots;

        if (!bank)
        {
            for (uint8 s = INVENTORY_SLOT_ITEM_START; s < INVENTORY_SLOT_ITEM_END; ++s)
                slots.push_back(MakePos(INVENTORY_SLOT_BAG_0, s));

            for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
                if (Bag* bag = player->GetBagByPos(b))
                    if (IsGenericBag(bag))
                        for (uint32 s = 0; s < bag->GetBagSize(); ++s)
                            slots.push_back(MakePos(b, uint8(s)));
        }
        else
        {
            for (uint8 s = BANK_SLOT_ITEM_START; s < BANK_SLOT_ITEM_END; ++s)
                slots.push_back(MakePos(INVENTORY_SLOT_BAG_0, s));

            for (uint8 b = BANK_SLOT_BAG_START; b < BANK_SLOT_BAG_END; ++b)
                if (Bag* bag = player->GetBagByPos(b))
                    if (IsGenericBag(bag))
                        for (uint32 s = 0; s < bag->GetBagSize(); ++s)
                            slots.push_back(MakePos(b, uint8(s)));
        }

        return slots;
    }

    std::vector<Item*> ItemsIn(Player* player, std::vector<uint16> const& slots, size_t from = 0)
    {
        std::vector<Item*> items;
        for (size_t i = from; i < slots.size(); ++i)
            if (Item* item = player->GetItemByPos(slots[i]))
                items.push_back(item);
        return items;
    }

    // Etape 1 : regroupe les piles incompletes. Retourne le nombre de fusions.
    uint32 MergeStacks(Player* player, std::vector<uint16> const& slots, uint32& moves)
    {
        uint32 merges = 0;
        bool progress = true;

        while (progress && moves < MAX_MOVES)
        {
            progress = false;
            std::vector<Item*> items = ItemsIn(player, slots);

            for (size_t i = 0; i < items.size() && !progress; ++i)
            {
                Item* first = items[i];
                uint32 max = first->GetTemplate()->GetMaxStackSize();
                if (max <= 1 || first->GetCount() >= max)
                    continue;

                // derniere pile incomplete du meme objet
                for (size_t j = items.size() - 1; j > i; --j)
                {
                    Item* last = items[j];
                    if (last->GetEntry() != first->GetEntry() || last->GetCount() >= max)
                        continue;

                    uint32 before = first->GetCount();
                    uint16 firstPos = first->GetPos();
                    player->SwapItem(last->GetPos(), firstPos);
                    ++moves;

                    // verification : la pile cible doit avoir grossi
                    Item* now = player->GetItemByPos(firstPos);
                    if (now && now->GetEntry() == first->GetEntry() && now->GetCount() > before)
                    {
                        ++merges;
                        progress = true; // on recommence avec l'etat reel
                    }
                    break; // fusion impossible pour cette paire : on passe a la suivante
                }
            }
        }

        return merges;
    }

    // Etape 2 : tri par selection, un SwapItem par emplacement mal place.
    // Retourne false si un deplacement a ete refuse.
    bool SortSlots(Player* player, std::vector<uint16> const& slots, uint32& moves)
    {
        for (size_t i = 0; i < slots.size() && moves < MAX_MOVES; ++i)
        {
            std::vector<Item*> rest = ItemsIn(player, slots, i);
            if (rest.empty())
                return true; // il ne reste que des emplacements vides

            Item* best = *std::min_element(rest.begin(), rest.end(), SortBefore);
            if (best->GetPos() == slots[i])
                continue;

            uint32 entry = best->GetEntry();
            uint32 count = best->GetCount();
            player->SwapItem(best->GetPos(), slots[i]);
            ++moves;

            // On verifie le contenu plutot que le pointeur : avec deux piles du meme
            // objet, SwapItem echange les quantites au lieu des objets.
            Item* now = player->GetItemByPos(slots[i]);
            if (!now || now->GetEntry() != entry || now->GetCount() != count)
                return false;
        }

        return true;
    }

    struct NearbyBankerCheck
    {
        explicit NearbyBankerCheck(Player const* player) : _player(player) { }

        bool operator()(Creature* creature) const
        {
            return _player->GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_BANKER) != nullptr;
        }

    private:
        Player const* _player;
    };

    bool IsNearBanker(Player* player)
    {
        Creature* banker = nullptr;
        NearbyBankerCheck check(player);
        Trinity::CreatureSearcher<NearbyBankerCheck> searcher(player, banker, check);
        Cell::VisitGridObjects(player, searcher, BANKER_SEARCH_RANGE);
        return banker != nullptr;
    }
}

class tri_commandscript : public CommandScript
{
public:
    tri_commandscript() : CommandScript("tri_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> triCommandTable =
        {
            { "banque", RBAC_PERM_COMMAND_TRI_BANQUE, false, &HandleTriBanqueCommand, "" },
            { "",       RBAC_PERM_COMMAND_TRI,        false, &HandleTriCommand,       "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "tri", RBAC_PERM_COMMAND_TRI, false, nullptr, "", triCommandTable },
        };
        return commandTable;
    }

    static bool HandleTriCommand(ChatHandler* handler, char const* /*args*/)
    {
        return DoSort(handler, false);
    }

    static bool HandleTriBanqueCommand(ChatHandler* handler, char const* /*args*/)
    {
        return DoSort(handler, true);
    }

private:
    static bool DoSort(ChatHandler* handler, bool bank)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;

        if (!player->IsAlive())
        {
            handler->SendSysMessage("Impossible de ranger vos sacs en etant mort.");
            return true;
        }

        if (player->GetTradeData())
        {
            handler->SendSysMessage("Impossible de ranger vos sacs pendant un echange.");
            return true;
        }

        if (bank && !IsNearBanker(player))
        {
            handler->SendSysMessage("Vous devez etre aupres d'un banquier pour ranger la banque.");
            return true;
        }

        std::vector<uint16> slots = CollectSlots(player, bank);
        uint32 moves = 0;

        MergeStacks(player, slots, moves);
        bool ok = SortSlots(player, slots, moves);

        if (!ok)
            handler->SendSysMessage("Rangement interrompu : un objet n'a pas pu etre deplace.");
        else if (moves == 0)
            handler->SendSysMessage(bank ? "Votre banque est deja rangee." : "Vos sacs sont deja ranges.");
        else
            handler->SendSysMessage(bank ? "Banque rangee." : "Sacs ranges.");

        return true;
    }
};

void AddSC_tri_commandscript()
{
    new tri_commandscript();
}
