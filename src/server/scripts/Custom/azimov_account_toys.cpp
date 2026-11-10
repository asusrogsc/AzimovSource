/*
 * Azimov - Coffre à jouets lié au compte (comme sur retail)
 *
 * Un jouet est un objet de la liste ToyItems (jouets retail présents dans le client 4.3.4).
 * - Il rejoint la collection du compte (`account_toys`, base characters) dès qu'un personnage
 *   l'a dans ses sacs ou en banque, ou l'utilise.
 * - Le journal Collections (addon client) reçoit la liste des jouets du compte par message
 *   d'addon (préfixe AZTOY) et demande au serveur d'utiliser un jouet sans avoir l'objet :
 *   le client s'envoie à lui-même "AZTOY:U:<objet>" en chuchotement d'addon.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <set>
#include <string>
#include <unordered_map>

namespace
{
    // objet -> sort utilisé (SpellID1 de l'objet, déclencheur « utiliser »)
    std::unordered_map<uint32, uint32> const ToyItems =
    {
        { 1973, 16739 },
        { 13379, 18400 },
        { 17712, 21848 },
        { 17716, 21935 },
        { 18660, 23126 },
        { 18984, 23442 },
        { 18986, 23453 },
        { 21540, 26265 },
        { 23767, 30261 },
        { 30542, 36890 },
        { 30544, 36941 },
        { 30690, 37346 },
        { 32542, 40527 },
        { 32566, 40530 },
        { 32782, 41301 },
        { 33079, 42365 },
        { 33219, 42753 },
        { 33223, 42766 },
        { 33927, 43808 },
        { 34480, 45094 },
        { 34499, 45135 },
        { 34686, 45426 },
        { 35227, 46203 },
        { 35275, 46354 },
        { 36862, 47776 },
        { 36863, 47770 },
        { 37254, 48332 },
        { 37460, 48722 },
        { 37710, 49352 },
        { 38301, 50317 },
        { 38578, 51640 },
        { 40727, 89496 },
        { 40768, 54710 },
        { 43499, 58501 },
        { 43824, 59317 },
        { 44430, 60458 },
        { 44606, 61031 },
        { 44719, 61340 },
        { 44820, 61856 },
        { 45011, 62736 },
        { 45013, 62745 },
        { 45014, 62742 },
        { 45015, 62738 },
        { 45016, 62743 },
        { 45017, 62744 },
        { 45018, 62737 },
        { 45019, 62741 },
        { 45020, 62740 },
        { 45021, 62739 },
        { 45057, 62949 },
        { 45063, 62985 },
        { 45984, 64385 },
        { 46709, 65451 },
        { 46780, 65783 },
        { 46843, 66241 },
        { 48933, 67833 },
        { 49703, 30161 },
        { 49704, 30167 },
        { 50471, 71909 },
        { 52201, 73320 },
        { 52253, 73331 },
        { 54212, 74890 },
        { 54343, 75111 },
        { 54437, 62040 },
        { 54438, 75135 },
        { 54452, 75136 },
        { 54651, 75531 },
        { 54653, 75532 },
        { 60854, 84342 },
        { 63141, 89054 },
        { 63269, 95214 },
        { 64358, 95007 },
        { 64361, 91743 },
        { 64373, 94690 },
        { 64383, 91749 },
        { 64456, 91752 },
        { 64481, 91754 },
        { 64482, 91756 },
        { 64488, 94719 },
        { 64646, 91760 },
        { 64651, 91771 },
        { 64881, 92146 },
        { 64997, 89054 },
        { 66888, 6405 },
        { 67097, 93636 },
        { 68806, 96312 },
        { 69215, 97394 },
        { 69227, 97555 },
        { 69775, 98444 },
        { 69776, 98445 },
        { 69777, 98447 },
        { 70159, 99732 },
        { 70161, 99659 },
        { 70722, 99767 },
        { 71137, 100959 },
        { 71259, 101184 },
        { 72159, 102362 },
        { 72161, 102365 },
        { 75042, 105229 }
    };

    std::unordered_map<uint32, std::set<uint32>> AccountToys; // compte -> objets

    std::string const ADDON_PREFIX = "AZTOY";
    std::string const USE_COMMAND = "AZTOY:U:";

    void SendToyList(Player* player)
    {
        std::set<uint32> const& toys = AccountToys[player->GetSession()->GetAccountId()];
        // "R" = réinitialiser, puis des morceaux "O:<id>,<id>..." (< 250 caractères), puis "E" = fin
        player->WhisperAddon("R", ADDON_PREFIX, player);
        std::string chunk;
        for (uint32 item : toys)
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

    bool AddToy(Player* player, uint32 item)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        if (!AccountToys[accountId].insert(item).second)
            return false;
        CharacterDatabase.PExecute("INSERT IGNORE INTO account_toys (account, item) VALUES (%u, %u)", accountId, item);
        return true;
    }

    // Jouets présents dans les sacs ou la banque du personnage
    bool CollectCarriedToys(Player* player)
    {
        bool added = false;
        for (auto const& [item, spell] : ToyItems)
            if (player->HasItemCount(item, 1, true))
                added |= AddToy(player, item);
        return added;
    }

    void UseToy(Player* player, uint32 item)
    {
        auto itr = ToyItems.find(item);
        if (itr == ToyItems.end())
            return;

        if (!AccountToys[player->GetSession()->GetAccountId()].count(item))
        {
            ChatHandler(player->GetSession()).SendSysMessage("Vous ne possédez pas encore ce jouet.");
            return;
        }

        SpellInfo const* info = sSpellMgr->GetSpellInfo(itr->second);
        if (!info)
            return;

        Unit* target = player;
        if (info->NeedsExplicitUnitTarget())
            if (Unit* selected = player->GetSelectedUnit())
                target = selected;

        player->CastSpell(target, info->Id, false);
    }
}

class azimov_account_toys : public PlayerScript
{
public:
    azimov_account_toys() : PlayerScript("azimov_account_toys") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        std::set<uint32>& toys = AccountToys[accountId];
        toys.clear();
        if (QueryResult result = CharacterDatabase.PQuery("SELECT item FROM account_toys WHERE account = %u", accountId))
        {
            do
                toys.insert((*result)[0].GetUInt32());
            while (result->NextRow());
        }

        CollectCarriedToys(player);
        SendToyList(player);
    }

    void OnSave(Player* player) override
    {
        if (player && CollectCarriedToys(player))
            SendToyList(player);
    }

    // Utiliser un jouet depuis ses sacs l'ajoute à la collection
    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!player || !spell || !spell->m_castItemEntry)
            return;

        if (ToyItems.count(spell->m_castItemEntry) && AddToy(player, spell->m_castItemEntry))
            SendToyList(player);
    }

    // Demandes du journal Collections : chuchotement d'addon du joueur à lui-même
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (!player || player != receiver || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        if (msg == "AZTOY:L")
        {
            SendToyList(player);
            return;
        }

        if (msg.compare(0, USE_COMMAND.size(), USE_COMMAND) != 0)
            return;

        uint32 item = uint32(strtoul(msg.c_str() + USE_COMMAND.size(), nullptr, 10));
        UseToy(player, item);
    }
};

void AddSC_azimov_account_toys()
{
    new azimov_account_toys();
}
