/*
 * CustomStats - Polyvalence & Raffinage
 *  - charge/recharge la config (.reload config)
 *  - envoie les valeurs à l'addon client "CustomStats"
 *  - commande MJ  .customstats  pour vérifier les valeurs d'un joueur (ou de soi-même)
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "CustomStats.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"
#include <algorithm>
#include <string>

using namespace Trinity::ChatCommands;

class CustomStats_WorldScript : public WorldScript
{
public:
    CustomStats_WorldScript() : WorldScript("CustomStats_WorldScript") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        CustomStats::LoadConfig();
    }
};

class CustomStats_PlayerScript : public PlayerScript
{
public:
    CustomStats_PlayerScript() : PlayerScript("CustomStats_PlayerScript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        CustomStats::SendStatsToClient(player);
    }

    // L'addon s'envoie "CSTATS_REQ" à lui-même (whisper addon) pour demander une mise à jour
    void OnChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || receiver != player)
            return;

        if (msg == "CSTATS_REQ")
            CustomStats::SendStatsToClient(player);
    }
};

class CustomStats_CommandScript : public CommandScript
{
public:
    CustomStats_CommandScript() : CommandScript("CustomStats_CommandScript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable =
        {
            // même niveau de droit que .gps (MJ)
            { "customstats", rbac::RBAC_PERM_COMMAND_GPS, false, &HandleCustomStatsCommand, "" },
        };
        return commandTable;
    }

    static bool HandleCustomStatsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;

        CustomStats::Settings const& s = CustomStats::Get();
        float versa = CustomStats::GetVersatilityPct(target);
        float ref   = CustomStats::GetRefiningPct(target);

        handler->PSendSysMessage("|cffffd100%s|r", target->GetName().c_str());
        handler->PSendSysMessage("Polyvalence : %d score = +%.2f%% dégâts, +%.2f%% soins, -%.2f%% dégâts subis",
            target->GetCustomVersatilityRating(),
            std::min(versa * s.Versatility.DamageDoneFactor, s.Versatility.MaxDonePct),
            std::min(versa * s.Versatility.HealingDoneFactor, s.Versatility.MaxDonePct),
            std::min(versa * s.Versatility.DamageTakenFactor, s.Versatility.MaxTakenReductionPct));
        handler->PSendSysMessage("Raffinage : %d score = +%.2f%% vitesse, +%.2f%% taux de récolte, +%.2f%% artisanat",
            target->GetCustomRefiningRating(),
            std::min(ref * s.Refining.SpeedFactor, s.Refining.MaxSpeedPct),
            std::min(ref * s.Refining.YieldFactor, s.Refining.MaxYieldPct),
            std::min(ref * s.Refining.CraftYieldFactor, s.Refining.MaxCraftYieldPct));
        return true;
    }
};

void AddSC_custom_stats()
{
    new CustomStats_WorldScript();
    new CustomStats_PlayerScript();
    new CustomStats_CommandScript();
}
