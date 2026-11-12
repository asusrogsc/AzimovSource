/*
 * Azimov - Réputations partagées par le compte (façon bataillon du retail)
 *
 * Choix de l'utilisateur : à la connexion, chaque personnage monte au meilleur niveau de
 * réputation atteint par un personnage du compte. Jamais de baisse, et pas de partage
 * des factions de l'autre camp.
 *
 * - Table characters `account_reputations (account, faction, standing)` : meilleure valeur
 *   totale (base comprise) atteinte sur le compte, faction par faction.
 * - OnLogin / OnSave / OnLogout : les réputations du personnage remontent au compte.
 * - OnLogin : chaque faction du compte plus haute que celle du personnage est relevée.
 * - Exclus : réputation de guilde (1168), factions sans réputation, et toute faction dont la
 *   réputation de base pour ce personnage est inférieure à Neutre (= factions de l'autre camp :
 *   Hurlevent pour un Horde, Mag'har pour un Alliance, etc.).
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "WorldSession.h"
#include <string>

namespace
{
    constexpr uint32 FACTION_GUILD = 1168;

    bool IsShareableFor(Player* player, FactionEntry const* faction)
    {
        if (!faction || !faction->CanHaveReputation() || faction->ID == FACTION_GUILD)
            return false;

        // faction de l'autre camp : réputation de départ hostile / inamicale pour ce personnage
        return player->GetReputationMgr().GetBaseRank(faction) >= REP_NEUTRAL;
    }

    // réputations du personnage -> meilleure valeur du compte
    void SaveReputations(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        ReputationMgr const& mgr = player->GetReputationMgr();

        std::string values;
        uint32 count = 0;
        auto flush = [&]()
        {
            if (!count)
                return;
            CharacterDatabase.Execute(("INSERT INTO account_reputations (account, faction, standing) VALUES " + values +
                " ON DUPLICATE KEY UPDATE standing = GREATEST(standing, VALUES(standing))").c_str());
            values.clear();
            count = 0;
        };

        for (auto const& pair : mgr.GetStateList())
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(pair.second.ID);
            if (!IsShareableFor(player, faction))
                continue;

            int32 standing = mgr.GetReputation(faction);
            if (standing <= mgr.GetBaseReputation(faction))
                continue; // rien de gagné sur cette faction

            if (count)
                values += ',';
            values += "(" + std::to_string(accountId) + "," + std::to_string(faction->ID) + "," + std::to_string(standing) + ")";
            if (++count >= 300)
                flush();
        }
        flush();
    }

    // meilleure valeur du compte -> personnage
    uint32 LoadReputations(Player* player)
    {
        QueryResult result = CharacterDatabase.PQuery("SELECT faction, standing FROM account_reputations WHERE account = %u",
            player->GetSession()->GetAccountId());
        if (!result)
            return 0;

        ReputationMgr& mgr = player->GetReputationMgr();
        uint32 raised = 0;
        do
        {
            Field* fields = result->Fetch();
            FactionEntry const* faction = sFactionStore.LookupEntry(fields[0].GetUInt32());
            int32 standing = fields[1].GetInt32();
            if (!IsShareableFor(player, faction))
                continue;

            if (standing > mgr.GetReputation(faction) && mgr.SetReputation(faction, standing))
                ++raised;
        } while (result->NextRow());

        return raised;
    }
}

class azimov_account_reputations : public PlayerScript
{
public:
    azimov_account_reputations() : PlayerScript("azimov_account_reputations") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || !player->GetSession())
            return;

        SaveReputations(player);
        if (uint32 raised = LoadReputations(player))
            ChatHandler(player->GetSession()).PSendSysMessage("|cff66ccff[Réputations]|r %u réputation(s) relevée(s) au niveau de vos autres personnages.", raised);
    }

    void OnSave(Player* player) override
    {
        if (player && player->GetSession())
            SaveReputations(player);
    }

    void OnLogout(Player* player) override
    {
        if (player && player->GetSession())
            SaveReputations(player);
    }
};

void AddSC_azimov_account_reputations()
{
    new azimov_account_reputations();
}
