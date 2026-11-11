/*
 * Azimov - Hauts faits liés au compte
 *
 * Choix de l'utilisateur :
 * - seuls les hauts faits TERMINÉS sont partagés (pas la progression en cours) ;
 * - les titres sont partagés ; les récompenses en objets ne sont pas renvoyées.
 *
 * - Table characters `account_achievements (account, achievement, date)` : date de la première obtention.
 * - OnLogin / OnSave : les hauts faits du personnage sont ajoutés au compte (rattrapage compris).
 * - OnLogin : le personnage reçoit en silence les hauts faits du compte qui lui manquent
 *   (AchievementMgr<Player>::AddAccountAchievement, ajouté au core), puis la liste est renvoyée au client.
 * - Exclus : « Premier du royaume », hauts faits de guilde, compteurs (statistiques),
 *   et hauts faits réservés à l'autre faction.
 */

#include "ScriptMgr.h"
#include "AchievementMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Player.h"
#include "WorldSession.h"
#include <string>
#include <unordered_map>

namespace
{
    // nombre de hauts faits déjà envoyés au compte, par personnage (évite de tout réécrire à chaque sauvegarde)
    std::unordered_map<ObjectGuid::LowType, size_t> SyncedCount;

    bool IsShareable(AchievementEntry const* achievement)
    {
        if (!achievement)
            return false;

        return !(achievement->Flags & (ACHIEVEMENT_FLAG_COUNTER | ACHIEVEMENT_FLAG_REALM_FIRST_REACH |
            ACHIEVEMENT_FLAG_REALM_FIRST_KILL | ACHIEVEMENT_FLAG_REALM_FIRST_GUILD | ACHIEVEMENT_FLAG_GUILD));
    }

    bool FitsFaction(AchievementEntry const* achievement, Player* player)
    {
        if (achievement->Faction == -1)
            return true;
        return achievement->Faction == (player->GetTeam() == ALLIANCE ? 1 : 0);
    }

    // hauts faits du personnage -> compte
    void SaveToAccount(Player* player, bool force)
    {
        AchievementMgr<Player>* mgr = player->GetAchievementMgr();
        if (!mgr)
            return;

        CompletedAchievementMap const& completed = mgr->GetCompletedAchievements();
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        if (!force)
        {
            auto itr = SyncedCount.find(guid);
            if (itr != SyncedCount.end() && itr->second == completed.size())
                return;
        }
        SyncedCount[guid] = completed.size();

        uint32 accountId = player->GetSession()->GetAccountId();
        std::string values;
        uint32 count = 0;

        auto flush = [&]()
        {
            if (!count)
                return;
            CharacterDatabase.Execute(("INSERT IGNORE INTO account_achievements (account, achievement, date) VALUES " + values).c_str());
            values.clear();
            count = 0;
        };

        for (auto const& pair : completed)
        {
            if (!IsShareable(sAchievementStore.LookupEntry(pair.first)))
                continue;

            if (count)
                values += ',';
            values += "(" + std::to_string(accountId) + "," + std::to_string(pair.first) + "," + std::to_string(uint64(pair.second.date)) + ")";
            if (++count >= 400)
                flush();
        }
        flush();
    }

    // compte -> personnage
    uint32 LoadFromAccount(Player* player)
    {
        AchievementMgr<Player>* mgr = player->GetAchievementMgr();
        if (!mgr)
            return 0;

        uint32 added = 0;
        if (QueryResult result = CharacterDatabase.PQuery("SELECT achievement, date FROM account_achievements WHERE account = %u", player->GetSession()->GetAccountId()))
        {
            do
            {
                Field* fields = result->Fetch();
                AchievementEntry const* achievement = sAchievementStore.LookupEntry(fields[0].GetUInt32());
                if (!IsShareable(achievement) || !FitsFaction(achievement, player) || player->HasAchieved(achievement->ID))
                    continue;

                if (mgr->AddAccountAchievement(achievement, time_t(fields[1].GetUInt64())))
                    ++added;
            } while (result->NextRow());
        }

        return added;
    }
}

class azimov_account_achievements : public PlayerScript
{
public:
    azimov_account_achievements() : PlayerScript("azimov_account_achievements") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || !player->GetSession())
            return;

        SaveToAccount(player, true);

        if (uint32 added = LoadFromAccount(player))
        {
            // les données des hauts faits ont déjà été envoyées avant l'entrée dans le monde : on les renvoie
            player->GetAchievementMgr()->SendAllAchievementData(player);
            ChatHandler(player->GetSession()).PSendSysMessage("|cff66ccff[Hauts faits]|r %u haut(s) fait(s) obtenu(s) par vos autres personnages ajouté(s) à ce personnage.", added);
            // les nouveaux hauts faits (méta, paliers de points) partent aussi au compte
            SaveToAccount(player, true);
        }
    }

    void OnSave(Player* player) override
    {
        if (player && player->GetSession())
            SaveToAccount(player, false);
    }

    void OnLogout(Player* player) override
    {
        if (!player)
            return;
        if (player->GetSession())
            SaveToAccount(player, false);
        SyncedCount.erase(player->GetGUID().GetCounter());
    }
};

void AddSC_azimov_account_achievements()
{
    new azimov_account_achievements();
}
