/*
 * azimov_power.cpp - Puissance Azimov (progression liée au compte, façon Legion Remix)
 *
 * Remplace azimov_account_remix + azimov_universal_loot.
 *
 * Score de compte (table auth.account_scoreaz) gagné par :
 *   - monstres tués (même logique que azimov_universal_loot : monnaie 900 + points, groupe à 50 m)
 *   - quêtes rendues (et quêtes journalières)
 *   - paliers de Manastorm terminés (appel depuis manastorm.cpp, voir AzimovPower_OnManastormComplete)
 *
 * Tous les X points (= 1 palier), les personnages du compte gagnent :
 *   - +X % d'XP (monstres ET quêtes)
 *   - de la Polyvalence et du Raffinage (via CustomStats, cumulés avec l'équipement)
 *   - l'aura 1000050 (affichage : 1 charge par % d'XP)
 *
 * Réglages : worldserver.conf, section "PUISSANCE AZIMOV", puis .reload config
 * Commande MJ : .azimovpower       (score du compte du joueur ciblé ou de soi-même)
 *               .azimovpower ajouter N (ajoute N points, tests)
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "CustomStats.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "RBAC.h"
#include "Random.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

using namespace Trinity::ChatCommands;

namespace
{
    char const* const ADDON_PREFIX = "AZPOW";

    struct PowerSettings
    {
        bool   Enabled              = true;
        uint32 AuraSpell            = 1000050;
        uint32 ScorePerTier         = 1000;   // points pour 1 palier

        // Bonus par palier
        uint32 XpPctPerTier         = 1;      // +1 % d'XP par palier
        uint32 MaxXpPct             = 200;
        uint32 VersaPerTier         = 26;     // score de Polyvalence par palier (26 = +0.2 % avec 130 score/%)
        uint32 MaxVersa             = 1300;
        uint32 RefiningPerTier      = 25;     // score de Raffinage par palier (25 = +0.5 % avec 50 score/%)
        uint32 MaxRefining          = 1250;

        // Gains : monstres (min-max, comme azimov_universal_loot)
        uint32 KillNormalMin        = 1,  KillNormalMax     = 3;
        uint32 KillEliteMin         = 3,  KillEliteMax      = 6;
        uint32 KillDungeonBossMin   = 8,  KillDungeonBossMax = 10;
        uint32 KillWorldBossMin     = 25, KillWorldBossMax  = 50;
        float  GroupRange           = 50.0f;
        uint32 CurrencyId           = 900;    // 0 = pas de monnaie

        // Gains : quêtes et Manastorm
        uint32 QuestPoints          = 5;
        uint32 DailyQuestPoints     = 5;
        uint32 ManastormBase        = 10;
        uint32 ManastormPerTier     = 5;

        bool   AnnounceTierUp       = true;
    };

    PowerSettings s_cfg;

    struct AppliedBonus
    {
        int32  versa = 0;
        int32  refining = 0;
        uint32 tier = 0;
        bool   initialized = false;
    };

    std::mutex s_lock;
    std::unordered_map<uint32, uint32> s_scores;                         // compte -> score
    std::unordered_map<ObjectGuid::LowType, AppliedBonus> s_applied;     // personnage -> bonus appliqués

    // ------------------------------------------------------------------
    // Calculs
    // ------------------------------------------------------------------

    uint32 TierOf(uint32 score)       { return s_cfg.ScorePerTier ? score / s_cfg.ScorePerTier : 0; }
    uint32 XpPctFor(uint32 tier)      { return std::min(tier * s_cfg.XpPctPerTier, s_cfg.MaxXpPct); }
    int32  VersaFor(uint32 tier)      { return int32(std::min(tier * s_cfg.VersaPerTier, s_cfg.MaxVersa)); }
    int32  RefiningFor(uint32 tier)   { return int32(std::min(tier * s_cfg.RefiningPerTier, s_cfg.MaxRefining)); }

    std::string FormatNumber(uint32 value)
    {
        std::string digits = std::to_string(value);
        std::string out;
        int count = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (count && count % 3 == 0)
                out.insert(out.begin(), ' ');
            out.insert(out.begin(), *it);
            ++count;
        }
        return out;
    }

    uint32 GetScore(uint32 accountId)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        auto itr = s_scores.find(accountId);
        return itr != s_scores.end() ? itr->second : 0;
    }

    void LoadScore(uint32 accountId)
    {
        uint32 score = 0;
        if (QueryResult result = LoginDatabase.PQuery("SELECT `score` FROM `account_scoreaz` WHERE `account_id` = %u", accountId))
            score = result->Fetch()[0].GetUInt32();

        std::lock_guard<std::mutex> guard(s_lock);
        s_scores[accountId] = score;
    }

    // ------------------------------------------------------------------
    // Client
    // ------------------------------------------------------------------

    // Format : "S:<score>:<palier>:<%xp>:<polyvalence>:<raffinage>:<points par palier>"
    void SendToClient(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 score = GetScore(player->GetSession()->GetAccountId());
        uint32 tier = TierOf(score);

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "S:%u:%u:%u:%d:%d:%u",
            score, tier, XpPctFor(tier), VersaFor(tier), RefiningFor(tier), s_cfg.ScorePerTier);
        player->WhisperAddon(buffer, ADDON_PREFIX, player);
    }

    // Aura d'affichage : 1 charge par % d'XP
    void RefreshAura(Player* player, uint32 xpPct)
    {
        if (!s_cfg.AuraSpell || !sSpellMgr->GetSpellInfo(s_cfg.AuraSpell))
            return;

        if (!s_cfg.Enabled || xpPct == 0)
        {
            player->RemoveAura(s_cfg.AuraSpell);
            return;
        }

        if (!player->IsAlive())
            return;

        Aura* aura = player->GetAura(s_cfg.AuraSpell);
        if (!aura)
        {
            player->CastSpell(player, s_cfg.AuraSpell, true);
            aura = player->GetAura(s_cfg.AuraSpell);
        }

        if (aura)
        {
            uint8 stacks = uint8(std::min<uint32>(xpPct, 255));
            if (aura->GetStackAmount() != stacks)
                aura->SetStackAmount(stacks);
            aura->SetMaxDuration(-1);
            aura->SetDuration(-1);
        }
    }

    // Applique (ou met à jour) Polyvalence, Raffinage et l'aura selon le score du compte
    void ApplyBonuses(Player* player, bool announce)
    {
        if (!player || !player->GetSession())
            return;

        uint32 score = GetScore(player->GetSession()->GetAccountId());
        uint32 tier = s_cfg.Enabled ? TierOf(score) : 0;
        int32 versa = s_cfg.Enabled ? VersaFor(tier) : 0;
        int32 refining = s_cfg.Enabled ? RefiningFor(tier) : 0;

        AppliedBonus old;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            AppliedBonus& entry = s_applied[player->GetGUID().GetCounter()];
            old = entry;
            entry.versa = versa;
            entry.refining = refining;
            entry.tier = tier;
            entry.initialized = true;
        }

        if (versa != old.versa)
            player->ApplyCustomVersatilityRating(versa - old.versa, true);
        if (refining != old.refining)
            player->ApplyCustomRefiningRating(refining - old.refining, true);

        RefreshAura(player, XpPctFor(tier));

        if (announce && s_cfg.AnnounceTierUp && old.initialized && tier > old.tier)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff00[Puissance Azimov]|r Nouveau palier ! |cffffd700%s|r points : +%u%% d'XP, +%d Polyvalence, +%d Raffinage.",
                FormatNumber(score).c_str(), XpPctFor(tier), versa, refining);
        }

        if (versa != old.versa || refining != old.refining)
            CustomStats::SendStatsToClient(player);
        SendToClient(player);
    }

    void AddScore(Player* player, uint32 points)
    {
        if (!s_cfg.Enabled || !player || !player->GetSession() || !points)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_scores[accountId] += points;
        }

        LoginDatabase.PExecute("INSERT INTO `account_scoreaz` (`account_id`, `score`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `score` = `score` + %u",
            accountId, points, points);

        ApplyBonuses(player, true);
    }

    void LoadConfig()
    {
        PowerSettings& c = s_cfg;
        c.Enabled            = sConfigMgr->GetBoolDefault("Azimov.Puissance.Enable", true);
        c.AuraSpell          = sConfigMgr->GetIntDefault("Azimov.Puissance.SortAura", 1000050);
        c.ScorePerTier       = std::max(1, sConfigMgr->GetIntDefault("Azimov.Puissance.PointsParPalier", 1000));
        c.XpPctPerTier       = sConfigMgr->GetIntDefault("Azimov.Puissance.XpParPalier", 1);
        c.MaxXpPct           = sConfigMgr->GetIntDefault("Azimov.Puissance.XpMax", 200);
        c.VersaPerTier       = sConfigMgr->GetIntDefault("Azimov.Puissance.PolyvalenceParPalier", 26);
        c.MaxVersa           = sConfigMgr->GetIntDefault("Azimov.Puissance.PolyvalenceMax", 1300);
        c.RefiningPerTier    = sConfigMgr->GetIntDefault("Azimov.Puissance.RaffinageParPalier", 25);
        c.MaxRefining        = sConfigMgr->GetIntDefault("Azimov.Puissance.RaffinageMax", 1250);
        c.KillNormalMin      = sConfigMgr->GetIntDefault("Azimov.Puissance.Monstre.Min", 1);
        c.KillNormalMax      = sConfigMgr->GetIntDefault("Azimov.Puissance.Monstre.Max", 3);
        c.KillEliteMin       = sConfigMgr->GetIntDefault("Azimov.Puissance.Elite.Min", 3);
        c.KillEliteMax       = sConfigMgr->GetIntDefault("Azimov.Puissance.Elite.Max", 6);
        c.KillDungeonBossMin = sConfigMgr->GetIntDefault("Azimov.Puissance.BossDonjon.Min", 8);
        c.KillDungeonBossMax = sConfigMgr->GetIntDefault("Azimov.Puissance.BossDonjon.Max", 10);
        c.KillWorldBossMin   = sConfigMgr->GetIntDefault("Azimov.Puissance.BossMonde.Min", 25);
        c.KillWorldBossMax   = sConfigMgr->GetIntDefault("Azimov.Puissance.BossMonde.Max", 50);
        c.GroupRange         = sConfigMgr->GetFloatDefault("Azimov.Puissance.DistanceGroupe", 50.0f);
        c.CurrencyId         = sConfigMgr->GetIntDefault("Azimov.Puissance.MonnaieId", 900);
        c.QuestPoints        = sConfigMgr->GetIntDefault("Azimov.Puissance.Quete", 5);
        c.DailyQuestPoints   = sConfigMgr->GetIntDefault("Azimov.Puissance.QueteJournaliere", 5);
        c.ManastormBase      = sConfigMgr->GetIntDefault("Azimov.Puissance.Manastorm.Base", 10);
        c.ManastormPerTier   = sConfigMgr->GetIntDefault("Azimov.Puissance.Manastorm.ParPalier", 5);
        c.AnnounceTierUp     = sConfigMgr->GetBoolDefault("Azimov.Puissance.AnnoncePalier", true);

        TC_LOG_INFO("server.loading", ">> Puissance Azimov : %s, %u points par palier, sort d'aura %u",
            c.Enabled ? "ON" : "OFF", c.ScorePerTier, c.AuraSpell);
    }

    uint32 RollPoints(uint32 minValue, uint32 maxValue)
    {
        if (maxValue <= minValue)
            return minValue;
        return urand(minValue, maxValue);
    }
}

// Appelé par manastorm.cpp quand un palier est terminé (voir LISEZMOI)
void AzimovPower_OnManastormComplete(Player* player, uint32 tier)
{
    AddScore(player, s_cfg.ManastormBase + s_cfg.ManastormPerTier * tier);
}

class azimov_power_world : public WorldScript
{
public:
    azimov_power_world() : WorldScript("azimov_power_world") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }
};

class azimov_power_player : public PlayerScript
{
public:
    azimov_power_player() : PlayerScript("azimov_power_player") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player->GetSession())
            return;

        LoadScore(player->GetSession()->GetAccountId());
        ApplyBonuses(player, false);

        if (!s_cfg.Enabled)
            return;

        uint32 score = GetScore(player->GetSession()->GetAccountId());
        uint32 tier = TierOf(score);
        if (tier > 0)
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff00[Puissance Azimov]|r Votre puissance de compte est de |cffffd700%s|r points : +%u%% d'XP, +%d Polyvalence, +%d Raffinage.",
                FormatNumber(score).c_str(), XpPctFor(tier), VersaFor(tier), RefiningFor(tier));
        else
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff00[Puissance Azimov]|r Votre puissance de compte est de |cffffd700%s|r points. Premier palier à %s points.",
                FormatNumber(score).c_str(), FormatNumber(s_cfg.ScorePerTier).c_str());
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(s_lock);
        s_applied.erase(player->GetGUID().GetCounter());
    }

    // L'aura disparaît à la mort : on la remet à chaque changement de carte ou de zone (sans message)
    void OnMapChanged(Player* player) override
    {
        RefreshAura(player, XpPctFor(TierOf(GetScore(player->GetSession()->GetAccountId()))));
    }

    void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        if (player->GetSession())
            RefreshAura(player, XpPctFor(TierOf(GetScore(player->GetSession()->GetAccountId()))));
    }

    // Bonus d'XP sur TOUTE l'XP (monstres, quêtes, exploration)
    void OnGiveXP(Player* player, uint32& amount, Unit* /*victim*/) override
    {
        if (!s_cfg.Enabled || !player || !player->GetSession() || amount == 0)
            return;

        uint32 pct = XpPctFor(TierOf(GetScore(player->GetSession()->GetAccountId())));
        if (pct)
            amount += CalculatePct(amount, pct);
    }

    // Monstres : même logique et mêmes protections que azimov_universal_loot
    void OnCreatureKill(Player* player, Creature* creature) override
    {
        if (!s_cfg.Enabled || !player || !creature || !player->GetSession() || !player->IsInWorld())
            return;

        if (creature->GetGUID() == player->GetGUID() || creature->GetOwnerGUID() == player->GetGUID())
            return;

        if (!creature->isDead())
            return;

        CreatureTemplate const* cTemplate = creature->GetCreatureTemplate();
        if (!cTemplate || cTemplate->type == CREATURE_TYPE_CRITTER)
            return;

        // En h�ro�que, le mod�le de difficult� n'a souvent pas de lootid : on regarde aussi le mod�le normal
        CreatureTemplate const* normalTemplate = sObjectMgr->GetCreatureTemplate(creature->GetEntry());
        bool hasLoot = cTemplate->lootid || cTemplate->maxgold
            || (normalTemplate && (normalTemplate->lootid || normalTemplate->maxgold));
        if (!hasLoot)
            return;

        if (!player->isHonorOrXPTarget(creature))
            return;

        uint32 amount;
        if (creature->isWorldBoss())
            amount = RollPoints(s_cfg.KillWorldBossMin, s_cfg.KillWorldBossMax);
        else if (creature->IsDungeonBoss())
            amount = RollPoints(s_cfg.KillDungeonBossMin, s_cfg.KillDungeonBossMax);
        else if (cTemplate->rank == CREATURE_ELITE_ELITE || cTemplate->rank == CREATURE_ELITE_RAREELITE)
            amount = RollPoints(s_cfg.KillEliteMin, s_cfg.KillEliteMax);
        else
            amount = RollPoints(s_cfg.KillNormalMin, s_cfg.KillNormalMax);

        if (!amount)
            return;

        auto reward = [&](Player* member)
        {
            if (s_cfg.CurrencyId)
                member->ModifyCurrency(s_cfg.CurrencyId, int32(amount));
            AddScore(member, amount);
        };

        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->GetSession() && member->IsAlive() && member->IsInWorld()
                    && member->IsInMap(player) && member->IsWithinDistInMap(player, s_cfg.GroupRange))
                    reward(member);
            }
        }
        else
            reward(player);
    }

    // Quêtes : uniquement au moment où la quête est rendue (pas à l'acceptation ni à l'abandon)
    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        if (!s_cfg.Enabled || !player)
            return;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            return;

        QuestStatus status = player->GetQuestStatus(questId);
        if (status == QUEST_STATUS_REWARDED)
            AddScore(player, s_cfg.QuestPoints);
        else if (status == QUEST_STATUS_NONE && quest->IsDaily() && player->IsDailyQuestDone(questId))
            AddScore(player, s_cfg.DailyQuestPoints);
    }

    // L'addon s'envoie "AZPOW_REQ" (whisper addon à soi-même) pour demander les valeurs
    void OnChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang == LANG_ADDON && receiver == player && msg == "AZPOW_REQ")
            SendToClient(player);
    }
};

class azimov_power_command : public CommandScript
{
public:
    azimov_power_command() : CommandScript("azimov_power_command") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> azimovTable =
        {
            { "ajouter", rbac::RBAC_PERM_COMMAND_GPS, false, &HandleAddCommand,  "" },
            { "",        rbac::RBAC_PERM_COMMAND_GPS, false, &HandleInfoCommand, "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "azimovpower", rbac::RBAC_PERM_COMMAND_GPS, false, nullptr, "", azimovTable },
        };
        return commandTable;
    }

    static bool HandleInfoCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target || !target->GetSession())
            return false;

        uint32 accountId = target->GetSession()->GetAccountId();
        uint32 score = GetScore(accountId);
        uint32 tier = TierOf(score);
        handler->PSendSysMessage("|cffffd700%s|r (compte %u) : %s points, palier %u, +%u%% XP, +%d Polyvalence, +%d Raffinage",
            target->GetName().c_str(), accountId, FormatNumber(score).c_str(), tier, XpPctFor(tier), VersaFor(tier), RefiningFor(tier));
        return true;
    }

    static bool HandleAddCommand(ChatHandler* handler, char const* args)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target || !args || !*args)
            return false;

        int32 points = atoi(args);
        if (points <= 0)
            return false;

        AddScore(target, uint32(points));
        return HandleInfoCommand(handler, nullptr);
    }
};

void AddSC_azimov_power()
{
    new azimov_power_world();
    new azimov_power_player();
    new azimov_power_command();
}
