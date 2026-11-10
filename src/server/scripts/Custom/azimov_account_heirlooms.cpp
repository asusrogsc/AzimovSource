/*
 * Azimov - Héritages liés au compte (comme sur retail)
 *
 * Un objet d'héritage (qualité 7) rejoint la collection du compte (`account_heirlooms`,
 * base characters) dès qu'un personnage l'a sur lui, dans ses sacs ou en banque.
 * Depuis le journal Collections (addon client), n'importe quel personnage du compte peut
 * en créer un exemplaire. Messages d'addon (préfixe AZHRL), chuchotés du joueur à lui-même :
 *   "AZHRL:L" = liste des héritages du compte   "AZHRL:C:<objet>" = créer l'objet
 * Réponses du serveur : "R" (réinitialiser), "O:<id>,<id>..." puis "E" (fin).
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include <set>
#include <string>
#include <unordered_map>

namespace
{
    // Héritages Blizzard présents dans le client 4.3.4 (qualité 7, objets équipables)
    std::set<uint32> const HeirloomItems =
    {
        38691, 42943, 42944, 42945, 42946, 42947, 42948, 42949, 42950, 42951, 42952, 42984, 42985,
        42991, 42992, 44091, 44092, 44093, 44094, 44095, 44096, 44097, 44098, 44099, 44100, 44101,
        44102, 44103, 44105, 44107, 48677, 48683, 48685, 48687, 48689, 48691, 48716, 48718, 50255,
        61931, 61935, 61936, 61937, 61942, 61958, 62023, 62024, 62025, 62026, 62027, 62029, 62035,
        62036, 62037, 62038, 62039, 62040, 69887, 69888, 69889, 69890, 69891, 69892, 69893
    };

    std::unordered_map<uint32, std::set<uint32>> AccountHeirlooms; // compte -> objets

    std::string const ADDON_PREFIX = "AZHRL";
    std::string const CREATE_COMMAND = "AZHRL:C:";

    void SendHeirloomList(Player* player)
    {
        std::set<uint32> const& items = AccountHeirlooms[player->GetSession()->GetAccountId()];
        player->WhisperAddon("R", ADDON_PREFIX, player);
        std::string chunk;
        for (uint32 item : items)
        {
            std::string id = std::to_string(item);
            if (chunk.size() + id.size() + 1 > 240)
            {
                player->WhisperAddon("O:" + chunk, ADDON_PREFIX, player);
                chunk.clear();
            }
            if (!chunk.empty())
                chunk += ',';
            chunk += id;
        }
        if (!chunk.empty())
            player->WhisperAddon("O:" + chunk, ADDON_PREFIX, player);
        player->WhisperAddon("E", ADDON_PREFIX, player);
    }

    // Héritages portés, dans les sacs ou en banque
    bool CollectCarriedHeirlooms(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        bool added = false;
        for (uint32 item : HeirloomItems)
        {
            if (!player->HasItemCount(item, 1, true))
                continue;
            if (AccountHeirlooms[accountId].insert(item).second)
            {
                CharacterDatabase.PExecute("INSERT IGNORE INTO account_heirlooms (account, item) VALUES (%u, %u)", accountId, item);
                added = true;
            }
        }
        return added;
    }

    void CreateHeirloom(Player* player, uint32 item)
    {
        ChatHandler chat(player->GetSession());
        if (!HeirloomItems.count(item) || !AccountHeirlooms[player->GetSession()->GetAccountId()].count(item))
        {
            chat.SendSysMessage("Vous n'avez pas encore débloqué cet héritage.");
            return;
        }

        if (player->HasItemCount(item, 1, true))
        {
            chat.SendSysMessage("Vous possédez déjà cet héritage (sur vous, dans vos sacs ou en banque).");
            return;
        }

        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, 1);
        if (msg != EQUIP_ERR_OK)
        {
            player->SendEquipError(msg, nullptr, nullptr, item);
            return;
        }

        if (Item* newItem = player->StoreNewItem(dest, item, true))
            player->SendNewItem(newItem, 1, true, false);
    }
}

class azimov_account_heirlooms : public PlayerScript
{
public:
    azimov_account_heirlooms() : PlayerScript("azimov_account_heirlooms") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        std::set<uint32>& items = AccountHeirlooms[accountId];
        items.clear();
        if (QueryResult result = CharacterDatabase.PQuery("SELECT item FROM account_heirlooms WHERE account = %u", accountId))
        {
            do
                items.insert((*result)[0].GetUInt32());
            while (result->NextRow());
        }

        CollectCarriedHeirlooms(player);
    }

    void OnSave(Player* player) override
    {
        if (player && CollectCarriedHeirlooms(player))
            SendHeirloomList(player);
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!player || player != receiver || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        if (msg == "AZHRL:L")
        {
            CollectCarriedHeirlooms(player);
            SendHeirloomList(player);
            return;
        }

        if (msg.compare(0, CREATE_COMMAND.size(), CREATE_COMMAND) != 0)
            return;

        CreateHeirloom(player, uint32(strtoul(msg.c_str() + CREATE_COMMAND.size(), nullptr, 10)));
    }
};

void AddSC_azimov_account_heirlooms()
{
    new azimov_account_heirlooms();
}
