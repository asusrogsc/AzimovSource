/*
 * Azimov - Banque de bataillon (coffre partagé par tous les personnages du compte)
 *
 * Choix de l'utilisateur :
 * - accessible chez les banquiers (la fenêtre de l'addon s'ouvre avec la banque) ;
 * - 98 emplacements ; tout objet est accepté, y compris lié au personnage, sauf les objets de quête.
 *
 * - Les objets restent de vrais objets (`item_instance`, propriétaire vide), comme dans une
 *   banque de guilde : enchantements, gemmes, transmogrification, durabilité... sont conservés.
 * - Table characters `account_bank (account, slot, item_guid)`.
 * - Chaque action vérifie qu'un banquier vivant est à moins de 10 mètres.
 *
 * Messages d'addon (préfixe AZBNK), chuchotés du joueur à lui-même :
 *   "AZBNK:L"                          contenu -> "R", "I:<slot>,<objet>,<nombre>,<enchant>,<suffixe>;..." , "E"
 *   "AZBNK:D:<sac>:<case>:<emplacement>" déposer l'objet du sac (positions serveur) dans l'emplacement
 *                                       (255 = premier emplacement libre ou pile compatible)
 *   "AZBNK:W:<emplacement>"            retirer vers les sacs
 *   "AZBNK:M:<de>:<vers>"              déplacer / échanger deux emplacements de la banque
 */

#include "ScriptMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint8 BANK_SLOTS = 98;
    constexpr uint8 BANK_ANY_SLOT = 255;
    std::string const BANK_PREFIX = "AZBNK";

    using BankSlots = std::array<Item*, BANK_SLOTS>;
    std::unordered_map<uint32, BankSlots> AccountBanks;

    struct BankerCheck
    {
        explicit BankerCheck(WorldObject const* source) : _source(source) { }

        bool operator()(Creature* creature) const
        {
            return creature->IsAlive() && creature->IsBanker() && _source->IsWithinDistInMap(creature, 10.0f);
        }

    private:
        WorldObject const* _source;
    };

    bool NearBanker(Player* player)
    {
        Creature* banker = nullptr;
        BankerCheck check(player);
        Trinity::CreatureSearcher<BankerCheck> searcher(player, banker, check);
        Cell::VisitGridObjects(player, searcher, 10.0f);
        return banker != nullptr;
    }

    void SendBank(Player* player, std::string const& text)
    {
        player->WhisperAddon(text, BANK_PREFIX, player);
    }

    void Notify(Player* player, char const* text)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cff66ccff[Banque de bataillon]|r %s", text);
    }

    BankSlots& LoadBank(uint32 accountId)
    {
        auto itr = AccountBanks.find(accountId);
        if (itr != AccountBanks.end())
            return itr->second;

        BankSlots& slots = AccountBanks[accountId];
        slots.fill(nullptr);

        //                                                      0            1                2      3         4        5      6             7                   8                 9           10            11     12       13       14
        if (QueryResult result = CharacterDatabase.PQuery("SELECT ii.creatorGuid, ii.giftCreatorGuid, ii.count, ii.duration, ii.charges, ii.flags, ii.enchantments, ii.randomPropertyType, ii.randomPropertyId, ii.durability, ii.creationTime, ii.text, ab.slot, ii.guid, ii.itemEntry "
            "FROM account_bank ab JOIN item_instance ii ON ii.guid = ab.item_guid WHERE ab.account = %u", accountId))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 slot = fields[12].GetUInt8();
                ObjectGuid::LowType guid = fields[13].GetUInt32();
                uint32 entry = fields[14].GetUInt32();
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (slot >= BANK_SLOTS || !proto || slots[slot])
                    continue;

                Item* item = NewItemOrBag(proto);
                if (!item->LoadFromDB(guid, ObjectGuid::Empty, fields, entry))
                {
                    delete item;
                    continue;
                }
                slots[slot] = item;
            } while (result->NextRow());
        }
        return slots;
    }

    void SendBankList(Player* player)
    {
        BankSlots& slots = LoadBank(player->GetSession()->GetAccountId());
        SendBank(player, "R");
        std::string chunk;
        for (uint8 slot = 0; slot < BANK_SLOTS; ++slot)
        {
            Item* item = slots[slot];
            if (!item)
                continue;

            std::string entry = std::to_string(slot) + "," + std::to_string(item->GetEntry()) + "," + std::to_string(item->GetCount()) + ","
                + std::to_string(item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT)) + "," + std::to_string(item->GetItemRandomPropertyId());
            if (chunk.size() + entry.size() + 3 > 240)
            {
                SendBank(player, "I:" + chunk);
                chunk.clear();
            }
            if (!chunk.empty())
                chunk += ';';
            chunk += entry;
        }
        if (!chunk.empty())
            SendBank(player, "I:" + chunk);
        SendBank(player, "E");
    }

    bool CanDeposit(Player* player, Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->GetClass() == ITEM_CLASS_QUEST || proto->GetStartQuest())
        {
            Notify(player, "Les objets de quête ne peuvent pas être déposés.");
            return false;
        }
        if (item->IsNotEmptyBag())
        {
            Notify(player, "Videz ce sac avant de le déposer.");
            return false;
        }
        if ((proto->GetFlags() & ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION))
        {
            Notify(player, "Les objets invoqués ou temporaires ne peuvent pas être déposés.");
            return false;
        }
        if (item->IsEquipped() || player->GetTradeData())
        {
            Notify(player, "Cet objet ne peut pas être déposé maintenant.");
            return false;
        }
        return true;
    }

    // 255 = première pile compatible non pleine, sinon premier emplacement libre
    uint8 FindSlot(BankSlots const& slots, Item* item)
    {
        if (item->GetMaxStackCount() > 1)
            for (uint8 slot = 0; slot < BANK_SLOTS; ++slot)
                if (slots[slot] && slots[slot]->GetEntry() == item->GetEntry() && slots[slot]->GetCount() < slots[slot]->GetMaxStackCount())
                    return slot;

        for (uint8 slot = 0; slot < BANK_SLOTS; ++slot)
            if (!slots[slot])
                return slot;
        return BANK_ANY_SLOT;
    }

    void Deposit(Player* player, uint8 bag, uint8 pos, uint8 slot)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        BankSlots& slots = LoadBank(accountId);

        Item* item = player->GetItemByPos(bag, pos);
        if (!item || !CanDeposit(player, item))
            return;

        if (slot == BANK_ANY_SLOT)
            slot = FindSlot(slots, item);
        if (slot >= BANK_SLOTS)
        {
            Notify(player, "La banque de bataillon est pleine.");
            return;
        }

        Item* target = slots[slot];
        if (target)
        {
            // même objet empilable : on complète la pile de la banque
            if (target->GetEntry() != item->GetEntry() || target->GetCount() >= target->GetMaxStackCount())
            {
                Notify(player, "Cet emplacement est déjà occupé.");
                return;
            }

            uint32 moved = std::min(target->GetMaxStackCount() - target->GetCount(), item->GetCount());
            target->SetCount(target->GetCount() + moved);
            CharacterDatabase.PExecute("UPDATE item_instance SET count = %u WHERE guid = %u", target->GetCount(), target->GetGUID().GetCounter());
            uint32 destroy = moved;
            player->DestroyItemCount(item, destroy, true);
        }
        else
        {
            item->SetNotRefundable(player);
            player->MoveItemFromInventory(bag, pos, true);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->DeleteFromInventoryDB(trans);
            item->SetOwnerGUID(ObjectGuid::Empty);
            item->SetState(ITEM_CHANGED);
            item->SaveToDB(trans);
            trans->PAppend("REPLACE INTO account_bank (account, slot, item_guid) VALUES (%u, %u, %u)", accountId, slot, item->GetGUID().GetCounter());
            player->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);
            slots[slot] = item;
        }
        SendBankList(player);
    }

    void Withdraw(Player* player, uint8 slot)
    {
        if (slot >= BANK_SLOTS)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        BankSlots& slots = LoadBank(accountId);
        Item* item = slots[slot];
        if (!item)
            return;

        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
        {
            Notify(player, "Vos sacs sont pleins.");
            return;
        }

        slots[slot] = nullptr;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM account_bank WHERE account = %u AND slot = %u", accountId, slot);
        item->SetState(ITEM_UNCHANGED); // comme pour le courrier : sinon l'objet ne peut plus être retiré plus tard
        player->MoveItemToInventory(dest, item, true);
        player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        SendBankList(player);
    }

    void MoveSlot(Player* player, uint8 from, uint8 to)
    {
        if (from >= BANK_SLOTS || to >= BANK_SLOTS || from == to)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        BankSlots& slots = LoadBank(accountId);
        if (!slots[from])
            return;

        Item* a = slots[from];
        Item* b = slots[to];

        // même objet empilable : fusion des piles
        if (b && b->GetEntry() == a->GetEntry() && b->GetMaxStackCount() > 1 && b->GetCount() < b->GetMaxStackCount())
        {
            uint32 moved = std::min(b->GetMaxStackCount() - b->GetCount(), a->GetCount());
            b->SetCount(b->GetCount() + moved);
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            trans->PAppend("UPDATE item_instance SET count = %u WHERE guid = %u", b->GetCount(), b->GetGUID().GetCounter());
            if (moved >= a->GetCount())
            {
                trans->PAppend("DELETE FROM account_bank WHERE account = %u AND slot = %u", accountId, from);
                trans->PAppend("DELETE FROM item_instance WHERE guid = %u", a->GetGUID().GetCounter());
                slots[from] = nullptr;
                delete a;
            }
            else
            {
                a->SetCount(a->GetCount() - moved);
                trans->PAppend("UPDATE item_instance SET count = %u WHERE guid = %u", a->GetCount(), a->GetGUID().GetCounter());
            }
            CharacterDatabase.CommitTransaction(trans);
            SendBankList(player);
            return;
        }

        slots[from] = b;
        slots[to] = a;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM account_bank WHERE account = %u AND slot IN (%u, %u)", accountId, from, to);
        trans->PAppend("INSERT INTO account_bank (account, slot, item_guid) VALUES (%u, %u, %u)", accountId, to, a->GetGUID().GetCounter());
        if (b)
            trans->PAppend("INSERT INTO account_bank (account, slot, item_guid) VALUES (%u, %u, %u)", accountId, from, b->GetGUID().GetCounter());
        CharacterDatabase.CommitTransaction(trans);
        SendBankList(player);
    }
}

class azimov_account_bank : public PlayerScript
{
public:
    azimov_account_bank() : PlayerScript("azimov_account_bank") { }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!player || player != receiver || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        if (msg.compare(0, 6, "AZBNK:") != 0)
            return;

        if (!NearBanker(player))
        {
            Notify(player, "Parlez à un banquier pour utiliser la banque de bataillon.");
            return;
        }

        std::vector<uint32> args;
        char const* p = msg.c_str() + 8;
        if (msg.size() > 8)
        {
            char* end = nullptr;
            do
            {
                args.push_back(uint32(strtoul(p, &end, 10)));
                p = end + 1;
            } while (end && *end == ':');
        }

        char command = msg.size() > 6 ? msg[6] : 0;
        switch (command)
        {
            case 'L': SendBankList(player); break;
            case 'D': if (args.size() >= 3) Deposit(player, uint8(args[0]), uint8(args[1]), uint8(args[2])); break;
            case 'W': if (args.size() >= 1) Withdraw(player, uint8(args[0])); break;
            case 'M': if (args.size() >= 2) MoveSlot(player, uint8(args[0]), uint8(args[1])); break;
            default: break;
        }
    }
};

void AddSC_azimov_account_bank()
{
    new azimov_account_bank();
}
