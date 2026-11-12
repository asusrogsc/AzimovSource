/*
 * Azimov - Illusions d'enchantement liées au compte (façon retail)
 *
 * Choix de l'utilisateur :
 * - une illusion est débloquée pour tout le compte quand un personnage a eu l'enchantement
 *   (permanent, avec un effet visuel) sur une arme ;
 * - appliquer une illusion coûte 10 po.
 *
 * - L'illusion ne change que l'effet visuel affiché (Item::SetAzimovIllusion, lu par
 *   Player::SetVisibleItemSlot dans le core) : l'enchantement réel et ses stats restent.
 * - Les illusions sont regroupées par effet visuel (SpellItemEnchantment.ItemVisual) : avoir eu
 *   un enchantement débloque toutes les illusions qui ont le même effet.
 * - Tables characters : `account_illusions (account, enchant)` et `item_illusions (item_guid, enchant)`.
 *
 * Messages d'addon (préfixe AZILL), chuchotés du joueur à lui-même :
 *   "AZILL:L"                    liste des illusions débloquées -> "R", "O:<enchant>,...", "E"
 *   "AZILL:A:<enchant>:<slot>"   appliquer sur l'arme équipée en 15 (main droite) ou 16 (main gauche)
 *   "AZILL:X:<slot>"             retirer l'illusion
 *   "N:<enchant>"                (serveur) nouvelle illusion débloquée
 */

#include "ScriptMgr.h"
#include "Bag.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include <set>
#include <string>
#include <unordered_map>

namespace
{
    constexpr uint32 ILLUSION_COST = 10 * GOLD; // 10 po
    std::string const ILLUSION_PREFIX = "AZILL";

    std::unordered_map<uint32, std::set<uint32>> AccountIllusions; // compte -> enchantements vus
    std::set<uint32> LoadedIllusionAccounts;

    uint32 VisualOf(uint32 enchantId)
    {
        if (SpellItemEnchantmentEntry const* entry = sSpellItemEnchantmentStore.LookupEntry(enchantId))
            return entry->ItemVisual;
        return 0;
    }

    bool IsWeapon(Item* item)
    {
        return item && item->GetTemplate() && item->GetTemplate()->GetClass() == ITEM_CLASS_WEAPON;
    }

    void SendIllusion(Player* player, std::string const& text)
    {
        player->WhisperAddon(text, ILLUSION_PREFIX, player);
    }

    void LoadIllusionAccount(uint32 accountId)
    {
        if (LoadedIllusionAccounts.count(accountId))
            return;

        std::set<uint32>& enchants = AccountIllusions[accountId];
        if (QueryResult result = CharacterDatabase.PQuery("SELECT enchant FROM account_illusions WHERE account = %u", accountId))
        {
            do
                enchants.insert((*result)[0].GetUInt32());
            while (result->NextRow());
        }
        LoadedIllusionAccounts.insert(accountId);
    }

    // enchantement permanent d'une arme -> illusion du compte
    void LearnFromItem(Player* player, Item* item, bool notify)
    {
        if (!IsWeapon(item))
            return;

        uint32 enchantId = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        if (!enchantId || !VisualOf(enchantId))
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        if (!AccountIllusions[accountId].insert(enchantId).second)
            return;

        CharacterDatabase.PExecute("INSERT IGNORE INTO account_illusions (account, enchant) VALUES (%u, %u)", accountId, enchantId);
        if (notify)
            SendIllusion(player, "N:" + std::to_string(enchantId));
    }

    template <typename F>
    void ForEachCarriedItem(Player* player, F&& func)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < BANK_SLOT_BAG_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;
            func(item);
            if (item->IsBag())
                if (Bag* bag = item->ToBag())
                    for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                        if (Item* inner = bag->GetItemByPos(uint8(i)))
                            func(inner);
        }
    }

    void ScanIllusions(Player* player, bool notify)
    {
        ForEachCarriedItem(player, [player, notify](Item* item) { LearnFromItem(player, item, notify); });
    }

    // l'illusion d'un enchantement est disponible si le compte a vu un enchantement au même effet visuel
    bool HasIllusion(Player* player, uint32 enchantId)
    {
        uint32 visual = VisualOf(enchantId);
        if (!visual)
            return false;

        for (uint32 seen : AccountIllusions[player->GetSession()->GetAccountId()])
            if (seen == enchantId || VisualOf(seen) == visual)
                return true;
        return false;
    }

    void SendIllusionList(Player* player)
    {
        ScanIllusions(player, false); // un enchantement posé depuis la dernière sauvegarde compte aussi

        std::set<uint32> const& enchants = AccountIllusions[player->GetSession()->GetAccountId()];
        SendIllusion(player, "R");
        std::string chunk;
        for (uint32 enchant : enchants)
        {
            std::string id = std::to_string(enchant);
            if (chunk.size() + id.size() + 1 > 240)
            {
                SendIllusion(player, "O:" + chunk);
                chunk.clear();
            }
            if (!chunk.empty())
                chunk += ',';
            chunk += id;
        }
        if (!chunk.empty())
            SendIllusion(player, "O:" + chunk);
        SendIllusion(player, "E");
    }

    Item* IllusionTarget(Player* player, uint8 slot)
    {
        if (slot != EQUIPMENT_SLOT_MAINHAND && slot != EQUIPMENT_SLOT_OFFHAND)
            return nullptr;

        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        return IsWeapon(item) ? item : nullptr;
    }

    void SetIllusion(Player* player, Item* item, uint8 slot, uint32 enchantId)
    {
        item->SetAzimovIllusion(enchantId);
        if (enchantId)
            CharacterDatabase.PExecute("REPLACE INTO item_illusions (item_guid, enchant) VALUES (%u, %u)", item->GetGUID().GetCounter(), enchantId);
        else
            CharacterDatabase.PExecute("DELETE FROM item_illusions WHERE item_guid = %u", item->GetGUID().GetCounter());
        player->SetVisibleItemSlot(slot, item);
    }

    void ApplyIllusion(Player* player, uint32 enchantId, uint8 slot)
    {
        ChatHandler chat(player->GetSession());
        if (!HasIllusion(player, enchantId))
        {
            chat.SendSysMessage("Vous n'avez pas encore débloqué cette illusion.");
            return;
        }

        Item* item = IllusionTarget(player, slot);
        if (!item)
        {
            chat.SendSysMessage(slot == EQUIPMENT_SLOT_OFFHAND ? "Aucune arme équipée en main gauche." : "Aucune arme équipée en main droite.");
            return;
        }

        if (!player->HasEnoughMoney(uint64(ILLUSION_COST)))
        {
            chat.SendSysMessage("Il vous faut 10 pièces d'or pour appliquer une illusion.");
            return;
        }

        player->ModifyMoney(-int64(ILLUSION_COST));
        SetIllusion(player, item, slot, enchantId);
        SendIllusion(player, "S:" + std::to_string(slot) + ":" + std::to_string(enchantId));
    }

    void RemoveIllusion(Player* player, uint8 slot)
    {
        if (Item* item = IllusionTarget(player, slot))
        {
            SetIllusion(player, item, slot, 0);
            SendIllusion(player, "S:" + std::to_string(slot) + ":0");
        }
    }

    // illusions enregistrées des armes du personnage -> en mémoire, puis mise à jour de l'affichage
    void LoadItemIllusions(Player* player)
    {
        if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT ii.item_guid, ii.enchant FROM item_illusions ii JOIN item_instance it ON it.guid = ii.item_guid WHERE it.owner_guid = %u",
            player->GetGUID().GetCounter()))
        {
            std::unordered_map<uint32, uint32> byGuid;
            do
                byGuid[(*result)[0].GetUInt32()] = (*result)[1].GetUInt32();
            while (result->NextRow());

            ForEachCarriedItem(player, [&byGuid](Item* item)
            {
                auto itr = byGuid.find(item->GetGUID().GetCounter());
                if (itr != byGuid.end())
                    item->SetAzimovIllusion(itr->second);
            });
        }

        for (uint8 slot : { uint8(EQUIPMENT_SLOT_MAINHAND), uint8(EQUIPMENT_SLOT_OFFHAND), uint8(EQUIPMENT_SLOT_RANGED) })
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                player->SetVisibleItemSlot(slot, item);
    }
}

class azimov_account_illusions : public PlayerScript
{
public:
    azimov_account_illusions() : PlayerScript("azimov_account_illusions") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        LoadIllusionAccount(player->GetSession()->GetAccountId());
        ScanIllusions(player, false);
        LoadItemIllusions(player);
    }

    void OnSave(Player* player) override
    {
        if (player && player->IsInWorld())
            ScanIllusions(player, true);
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!player || player != receiver || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        if (msg.compare(0, 6, "AZILL:") != 0)
            return;

        LoadIllusionAccount(player->GetSession()->GetAccountId());

        if (msg == "AZILL:L")
            SendIllusionList(player);
        else if (msg.compare(0, 8, "AZILL:A:") == 0)
        {
            char* end = nullptr;
            uint32 enchantId = uint32(strtoul(msg.c_str() + 8, &end, 10));
            uint8 slot = EQUIPMENT_SLOT_MAINHAND;
            if (end && *end == ':')
                slot = uint8(strtoul(end + 1, nullptr, 10));
            ApplyIllusion(player, enchantId, slot);
        }
        else if (msg.compare(0, 8, "AZILL:X:") == 0)
            RemoveIllusion(player, uint8(strtoul(msg.c_str() + 8, nullptr, 10)));
    }
};

class azimov_account_illusions_world : public WorldScript
{
public:
    azimov_account_illusions_world() : WorldScript("azimov_account_illusions_world") { }

    // ménage : illusions d'objets qui n'existent plus
    void OnStartup() override
    {
        CharacterDatabase.Execute("DELETE ii FROM item_illusions ii LEFT JOIN item_instance it ON it.guid = ii.item_guid WHERE it.guid IS NULL");
    }
};

void AddSC_azimov_account_illusions()
{
    new azimov_account_illusions();
    new azimov_account_illusions_world();
}
