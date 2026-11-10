/*
 * Azimov - Apparences liées au compte (transmogrification façon retail)
 *
 * - Tout objet d'armure ou d'arme pouvant servir de source de transmogrification est ajouté
 *   à la collection du compte (`account_appearances`, base characters) dès qu'un personnage
 *   l'obtient : butin, quête, achat, banque, équipement (hook OnItemObtained dans le core).
 * - Depuis le journal Collections (addon client), on applique une apparence débloquée sur
 *   l'objet équipé correspondant, sans avoir l'objet source. Prix = celui du transmogrificateur.
 * Messages d'addon (préfixe AZAPP), chuchotés du joueur à lui-même :
 *   "AZAPP:L"          liste des objets débloqués
 *   "AZAPP:A:<objet>"  appliquer l'apparence de cet objet sur l'emplacement équipé correspondant
 *   "AZAPP:X:<slot>"   retirer la transmogrification d'un emplacement équipé (0-18)
 *   "AZAPP:P:<objet>"  prix (en cuivre) pour appliquer cette apparence -> "P:<objet>:<prix>" (-1 si impossible)
 * Réponses : "R", "O:<id>,<id>..." , "E" (liste) et "N:<id>" (nouvelle apparence débloquée).
 */

#include "ScriptMgr.h"
#include "Bag.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    std::unordered_map<uint32, std::set<uint32>> AccountAppearances; // compte -> objets sources
    std::set<uint32> LoadedAccounts;

    std::string const ADDON_PREFIX = "AZAPP";

    // Même règles que Item::CanBeTransmogrified (objet source), mais sur le modèle d'objet
    bool IsAppearanceSource(ItemTemplate const* proto)
    {
        if (!proto)
            return false;

        if (proto->GetClass() != ITEM_CLASS_ARMOR && proto->GetClass() != ITEM_CLASS_WEAPON)
            return false;

        if (proto->GetClass() == ITEM_CLASS_WEAPON && proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return false;

        switch (proto->GetInventoryType())
        {
            case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE:
            case INVTYPE_WAIST: case INVTYPE_LEGS: case INVTYPE_FEET: case INVTYPE_WRISTS:
            case INVTYPE_HANDS: case INVTYPE_CLOAK: case INVTYPE_WEAPON: case INVTYPE_SHIELD:
            case INVTYPE_RANGED: case INVTYPE_2HWEAPON: case INVTYPE_WEAPONMAINHAND:
            case INVTYPE_WEAPONOFFHAND: case INVTYPE_HOLDABLE: case INVTYPE_THROWN:
            case INVTYPE_RANGEDRIGHT:
                break;
            default:
                return false;
        }

        if (proto->GetFlags2() & (ITEM_FLAG2_NO_ALTER_ITEM_VISUAL | ITEM_FLAG2_NO_SOURCE_FOR_ITEM_VISUAL))
            return false;

        if (proto->GetFlags2() & ITEM_FLAG2_IGNORE_QUALITY_FOR_ITEM_VISUAL_SOURCE)
            return true;

        if (proto->GetRandomProperty() || proto->GetRandomSuffix())
            return true;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            if (proto->GetItemStatValue(i) != 0)
                return true;

        return false;
    }

    // Groupe d'emplacement d'une pièce : les apparences ne passent qu'à l'intérieur d'un même groupe
    //   armures : par emplacement, quelle que soit la matière (tissu, cuir, mailles, plaques) ; torse = robe
    //   armes   : une main (toutes armes à une main), deux mains, armes à distance (arc / fusil / arbalète),
    //             baguettes, armes de jet ; bouclier et objet tenu en main gauche restent à part
    uint32 AppearanceGroup(ItemTemplate const* proto)
    {
        uint32 inv = proto->GetInventoryType();
        if (proto->GetClass() == ITEM_CLASS_ARMOR)
            return inv == INVTYPE_ROBE ? uint32(INVTYPE_CHEST) : inv;

        switch (inv)
        {
            case INVTYPE_WEAPON:
            case INVTYPE_WEAPONMAINHAND:
            case INVTYPE_WEAPONOFFHAND:
                return 100;                                   // une main
            case INVTYPE_2HWEAPON:
                return 101;                                   // deux mains
            default:
                break;
        }

        if (proto->IsRangedWeapon())
            return 102;                                       // arc, fusil, arbalète
        if (proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_WAND)
            return 103;
        if (proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_THROWN)
            return 104;
        return 200 + inv;
    }

    // Azimov : toute matière d'armure et tout type d'arme (sauf une main <-> deux mains)
    bool CanApply(Item const* target, ItemTemplate const* source)
    {
        ItemTemplate const* dest = target->GetTemplate();
        if (!dest || !source || source->GetId() == dest->GetId())
            return false;

        if (!target->CanTransmogrify() || !IsAppearanceSource(source))
            return false;

        if (source->GetClass() != dest->GetClass())
            return false;

        return AppearanceGroup(source) == AppearanceGroup(dest);
    }

    // Objet équipé qui recevrait cette apparence (nullptr si aucun)
    Item* FindTarget(Player* player, ItemTemplate const* source, uint8& slotOut);

    // Emplacements équipés à essayer pour une apparence, dans l'ordre
    std::vector<uint8> CandidateSlots(ItemTemplate const* source)
    {
        switch (source->GetInventoryType())
        {
            case INVTYPE_HEAD:      return { EQUIPMENT_SLOT_HEAD };
            case INVTYPE_SHOULDERS: return { EQUIPMENT_SLOT_SHOULDERS };
            case INVTYPE_CHEST:
            case INVTYPE_ROBE:      return { EQUIPMENT_SLOT_CHEST };
            case INVTYPE_WAIST:     return { EQUIPMENT_SLOT_WAIST };
            case INVTYPE_LEGS:      return { EQUIPMENT_SLOT_LEGS };
            case INVTYPE_FEET:      return { EQUIPMENT_SLOT_FEET };
            case INVTYPE_WRISTS:    return { EQUIPMENT_SLOT_WRISTS };
            case INVTYPE_HANDS:     return { EQUIPMENT_SLOT_HANDS };
            case INVTYPE_CLOAK:     return { EQUIPMENT_SLOT_BACK };
            case INVTYPE_SHIELD:
            case INVTYPE_HOLDABLE:
            case INVTYPE_WEAPONOFFHAND: return { EQUIPMENT_SLOT_OFFHAND };
            case INVTYPE_RANGED:
            case INVTYPE_THROWN:
            case INVTYPE_RANGEDRIGHT:   return { EQUIPMENT_SLOT_RANGED };
            default:                    return { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
        }
    }

    Item* FindTarget(Player* player, ItemTemplate const* source, uint8& slotOut)
    {
        for (uint8 slot : CandidateSlots(source))
        {
            Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (target && CanApply(target, source))
            {
                slotOut = slot;
                return target;
            }
        }
        return nullptr;
    }

    void Send(Player* player, std::string const& text)
    {
        player->WhisperAddon(text, ADDON_PREFIX, player);
    }

    void SendList(Player* player)
    {
        std::set<uint32> const& items = AccountAppearances[player->GetSession()->GetAccountId()];
        Send(player, "R");
        std::string chunk;
        for (uint32 item : items)
        {
            std::string id = std::to_string(item);
            if (chunk.size() + id.size() + 1 > 240)
            {
                Send(player, "O:" + chunk);
                chunk.clear();
            }
            if (!chunk.empty())
                chunk += ',';
            chunk += id;
        }
        if (!chunk.empty())
            Send(player, "O:" + chunk);
        Send(player, "E");
    }

    void LoadAccount(uint32 accountId)
    {
        if (LoadedAccounts.count(accountId))
            return;

        std::set<uint32>& items = AccountAppearances[accountId];
        if (QueryResult result = CharacterDatabase.PQuery("SELECT item FROM account_appearances WHERE account = %u", accountId))
        {
            do
                items.insert((*result)[0].GetUInt32());
            while (result->NextRow());
        }
        LoadedAccounts.insert(accountId);
    }

    // Ajoute un objet à la collection. Renvoie vrai si c'est une nouvelle apparence.
    bool Learn(Player* player, uint32 itemEntry, bool notify)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        if (!LoadedAccounts.count(accountId))
            return false;

        if (AccountAppearances[accountId].count(itemEntry))
            return false;

        if (!IsAppearanceSource(sObjectMgr->GetItemTemplate(itemEntry)))
            return false;

        AccountAppearances[accountId].insert(itemEntry);
        CharacterDatabase.PExecute("INSERT IGNORE INTO account_appearances (account, item) VALUES (%u, %u)", accountId, itemEntry);
        if (notify)
            Send(player, "N:" + std::to_string(itemEntry));
        return true;
    }

    // Tout ce que le personnage porte, a dans ses sacs ou en banque
    void LearnCarried(Player* player)
    {
        auto learnItem = [player](Item* item)
        {
            if (item)
                Learn(player, item->GetEntry(), false);
        };

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < BANK_SLOT_BAG_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            learnItem(item);
            if (item && item->IsBag())
                if (Bag* bag = item->ToBag())
                    for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                        learnItem(bag->GetItemByPos(uint8(i)));
        }
    }

    void Apply(Player* player, uint32 sourceEntry)
    {
        ChatHandler chat(player->GetSession());
        uint32 accountId = player->GetSession()->GetAccountId();
        if (!AccountAppearances[accountId].count(sourceEntry))
        {
            chat.SendSysMessage("Vous n'avez pas encore débloqué cette apparence.");
            return;
        }

        ItemTemplate const* source = sObjectMgr->GetItemTemplate(sourceEntry);
        if (!source)
            return;

        uint8 slot = 0;
        if (Item* target = FindTarget(player, source, slot))
        {
            uint32 cost = target->GetSpecialPrice();
            if (!player->HasEnoughMoney(uint64(cost)))
            {
                player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, nullptr, 0, 0);
                return;
            }
            player->ModifyMoney(-int64(cost));

            target->SetEnchantment(TRANSMOGRIFY_ENCHANTMENT_SLOT, sourceEntry, 0, 0);
            player->SetVisibleItemSlot(slot, target);
            target->UpdatePlayedTime(player);
            target->SetOwnerGUID(player->GetGUID());
            target->SetNotRefundable(player);
            target->ClearSoulboundTradeable(player);
            target->SetState(ITEM_CHANGED, player);
            Send(player, "P:" + std::to_string(sourceEntry) + ":" + std::to_string(cost));
            return;
        }

        chat.SendSysMessage("Aucun objet équipé compatible avec cette apparence (même emplacement ; une main et deux mains ne se mélangent pas).");
    }

    // Prix en cuivre pour appliquer cette apparence sur l'objet équipé correspondant (-1 : aucun objet compatible)
    void SendPrice(Player* player, uint32 sourceEntry)
    {
        ItemTemplate const* source = sObjectMgr->GetItemTemplate(sourceEntry);
        uint8 slot = 0;
        Item* target = source ? FindTarget(player, source, slot) : nullptr;
        Send(player, "P:" + std::to_string(sourceEntry) + ":" + (target ? std::to_string(target->GetSpecialPrice()) : std::string("-1")));
    }

    void Reset(Player* player, uint8 slot)
    {
        if (slot >= EQUIPMENT_SLOT_END)
            return;

        Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!target || !target->GetEnchantmentId(TRANSMOGRIFY_ENCHANTMENT_SLOT))
            return;

        target->ClearEnchantment(TRANSMOGRIFY_ENCHANTMENT_SLOT);
        player->SetVisibleItemSlot(slot, target);
        target->SetState(ITEM_CHANGED, player);
    }
}

class azimov_account_appearances : public PlayerScript
{
public:
    azimov_account_appearances() : PlayerScript("azimov_account_appearances") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        LoadAccount(player->GetSession()->GetAccountId());
        LearnCarried(player);
    }

    void OnItemObtained(Player* player, uint32 itemEntry) override
    {
        // pendant le chargement du personnage, OnLogin s'en charge
        if (!player || !player->IsInWorld())
            return;

        Learn(player, itemEntry, true);
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!player || player != receiver || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        if (msg.compare(0, 6, "AZAPP:") != 0)
            return;

        if (msg == "AZAPP:L")
            SendList(player);
        else if (msg.compare(0, 8, "AZAPP:A:") == 0)
            Apply(player, uint32(strtoul(msg.c_str() + 8, nullptr, 10)));
        else if (msg.compare(0, 8, "AZAPP:P:") == 0)
            SendPrice(player, uint32(strtoul(msg.c_str() + 8, nullptr, 10)));
        else if (msg.compare(0, 8, "AZAPP:X:") == 0)
            Reset(player, uint8(strtoul(msg.c_str() + 8, nullptr, 10)));
    }
};

void AddSC_azimov_account_appearances()
{
    new azimov_account_appearances();
}
