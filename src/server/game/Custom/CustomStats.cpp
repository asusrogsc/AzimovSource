/*
 * CustomStats - Polyvalence & Raffinage (voir CustomStats.h)
 */

#include "CustomStats.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Loot.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    CustomStats::Settings s_settings;
    thread_local bool s_inAbsorbCalculation = false;

    char const* const ADDON_PREFIX = "CSTATS";

    // Convertit un score en pourcentage
    float RatingToPct(Player const* player, int32 rating, float ratingPerPercent)
    {
        if (!player || rating <= 0)
            return 0.0f;

        // 0 ou moins = courbe officielle de la Hâte (le score "vaut" plus à bas niveau)
        if (ratingPerPercent <= 0.0f)
            return float(rating) * player->GetRatingMultiplier(CR_HASTE_MELEE);

        return float(rating) / ratingPerPercent;
    }

    // Le joueur qui "possède" l'unité (lui-même, ou le maître d'un familier/totem)
    Player const* GetOwningPlayer(Unit const* unit)
    {
        if (!unit)
            return nullptr;

        if (Player const* player = unit->ToPlayer())
            return player;

        if (!s_settings.Versatility.PetsInherit)
            return nullptr;

        return unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    bool IsPlayerControlled(Unit const* unit)
    {
        return unit && unit->GetCharmerOrOwnerPlayerOrPlayerItself() != nullptr;
    }

    // 130 % -> x2 garanti + 30 % de chance de x3
    uint32 RollMultiplier(float pct)
    {
        if (pct <= 0.0f)
            return 1;

        uint32 multiplier = 1 + uint32(pct / 100.0f);
        float remainder = std::fmod(pct, 100.0f);
        if (remainder > 0.0f && roll_chance_f(remainder))
            ++multiplier;

        return multiplier;
    }

    enum class ProfessionSpellType : uint8
    {
        None,
        Gathering,
        Crafting
    };

    ProfessionSpellType GetProfessionSpellType(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return ProfessionSpellType::None;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectInfo const& effect = spellInfo->Effects[i];
            if (effect.Effect == SPELL_EFFECT_SKINNING)
                return ProfessionSpellType::Gathering;

            if (effect.Effect == SPELL_EFFECT_OPEN_LOCK &&
                (effect.MiscValue == LOCKTYPE_HERBALISM || effect.MiscValue == LOCKTYPE_MINING))
                return ProfessionSpellType::Gathering;
        }

        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellInfo->Id);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            uint32 skill = itr->second->SkillLine;
            if (skill == SKILL_FISHING)
                continue;

            if (IsProfessionSkill(skill))
                return ProfessionSpellType::Crafting;
        }

        return ProfessionSpellType::None;
    }

    void Announce(Player const* player, char const* what, uint32 multiplier)
    {
        if (!s_settings.Refining.AnnounceBonus || multiplier <= 1 || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Raffinage]|r %s x%u !", what, multiplier);
    }
}

namespace CustomStats
{
    Settings const& Get()
    {
        return s_settings;
    }

    void LoadConfig()
    {
        VersatilitySettings& v = s_settings.Versatility;
        v.Enabled              = sConfigMgr->GetBoolDefault("CustomStats.Polyvalence.Enable", true);
        v.RatingPerPercent     = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.ScoreParPourcent", 130.0f);
        v.DamageDoneFactor     = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.FacteurDegats", 1.0f);
        v.HealingDoneFactor    = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.FacteurSoins", 1.0f);
        v.AbsorbDoneFactor     = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.FacteurBoucliers", 1.0f);
        v.DamageTakenFactor    = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.FacteurDegatsSubis", 0.5f);
        v.PvPFactor            = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.FacteurJcJ", 1.0f);
        v.MaxDonePct           = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.MaxBonusPourcent", 50.0f);
        v.MaxTakenReductionPct = sConfigMgr->GetFloatDefault("CustomStats.Polyvalence.MaxReductionPourcent", 25.0f);
        v.PetsInherit          = sConfigMgr->GetBoolDefault("CustomStats.Polyvalence.Familiers", true);

        RefiningSettings& r = s_settings.Refining;
        r.Enabled          = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.Enable", true);
        r.RatingPerPercent = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.ScoreParPourcent", 50.0f);
        r.SpeedFactor      = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.FacteurVitesse", 1.0f);
        r.MaxSpeedPct      = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.MaxVitessePourcent", 100.0f);
        r.MinCastTimeMs    = uint32(std::max(0, sConfigMgr->GetIntDefault("CustomStats.Raffinage.TempsIncantationMinMs", 250)));
        r.SpeedOnCrafting  = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.VitesseArtisanat", true);
        r.YieldFactor      = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.FacteurRecolte", 1.0f);
        r.MaxYieldPct      = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.MaxRecoltePourcent", 200.0f);
        r.Herbalism        = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.Herboristerie", true);
        r.Mining           = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.Minage", true);
        r.Skinning         = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.Depecage", true);
        r.CraftYieldFactor = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.FacteurArtisanat", 0.0f);
        r.MaxCraftYieldPct = sConfigMgr->GetFloatDefault("CustomStats.Raffinage.MaxArtisanatPourcent", 100.0f);
        r.AnnounceBonus    = sConfigMgr->GetBoolDefault("CustomStats.Raffinage.Annonce", true);

        TC_LOG_INFO("server.loading", ">> CustomStats : Polyvalence %s (%.1f score/%%), Raffinage %s (%.1f score/%%)",
            v.Enabled ? "ON" : "OFF", v.RatingPerPercent, r.Enabled ? "ON" : "OFF", r.RatingPerPercent);
    }

    // ------------------------------------------------------------------
    // Polyvalence
    // ------------------------------------------------------------------

    float GetVersatilityPct(Player const* player)
    {
        if (!s_settings.Versatility.Enabled || !player)
            return 0.0f;

        return RatingToPct(player, player->GetCustomVersatilityRating(), s_settings.Versatility.RatingPerPercent);
    }

    float ApplyVersatilityDone(Unit const* doer, Unit const* target, float amount, AmountKind kind)
    {
        if (!s_settings.Versatility.Enabled || amount <= 0.0f)
            return amount;

        // Bouclier en cours de calcul : le bonus sera appliqué une seule fois, dans AuraEffect::CalculateAmount
        if (kind == AmountKind::Healing && s_inAbsorbCalculation)
            return amount;

        Player const* owner = GetOwningPlayer(doer);
        if (!owner)
            return amount;

        float pct = GetVersatilityPct(owner);
        if (pct <= 0.0f)
            return amount;

        switch (kind)
        {
            case AmountKind::Damage:  pct *= s_settings.Versatility.DamageDoneFactor;  break;
            case AmountKind::Healing: pct *= s_settings.Versatility.HealingDoneFactor; break;
            case AmountKind::Absorb:  pct *= s_settings.Versatility.AbsorbDoneFactor;  break;
        }

        if (kind == AmountKind::Damage && IsPlayerControlled(target))
            pct *= s_settings.Versatility.PvPFactor;

        pct = std::min(pct, s_settings.Versatility.MaxDonePct);
        return amount * (1.0f + pct / 100.0f);
    }

    float ApplyVersatilityTaken(Unit const* victim, Unit const* attacker, float amount)
    {
        if (!s_settings.Versatility.Enabled || amount <= 0.0f)
            return amount;

        Player const* owner = GetOwningPlayer(victim);
        if (!owner)
            return amount;

        float pct = GetVersatilityPct(owner) * s_settings.Versatility.DamageTakenFactor;
        if (pct <= 0.0f)
            return amount;

        if (IsPlayerControlled(attacker))
            pct *= s_settings.Versatility.PvPFactor;

        pct = std::min(pct, s_settings.Versatility.MaxTakenReductionPct);
        return amount * (1.0f - pct / 100.0f);
    }

    void SetInAbsorbCalculation(bool value)
    {
        s_inAbsorbCalculation = value;
    }

    bool IsInAbsorbCalculation()
    {
        return s_inAbsorbCalculation;
    }

    // ------------------------------------------------------------------
    // Raffinage
    // ------------------------------------------------------------------

    float GetRefiningPct(Player const* player)
    {
        if (!s_settings.Refining.Enabled || !player)
            return 0.0f;

        return RatingToPct(player, player->GetCustomRefiningRating(), s_settings.Refining.RatingPerPercent);
    }

    int32 ModifyCastTime(Player const* player, SpellInfo const* spellInfo, int32 castTime)
    {
        if (!s_settings.Refining.Enabled || !player || castTime <= 0)
            return castTime;

        ProfessionSpellType type = GetProfessionSpellType(spellInfo);
        if (type == ProfessionSpellType::None)
            return castTime;

        if (type == ProfessionSpellType::Crafting && !s_settings.Refining.SpeedOnCrafting)
            return castTime;

        float speedPct = std::min(GetRefiningPct(player) * s_settings.Refining.SpeedFactor, s_settings.Refining.MaxSpeedPct);
        if (speedPct <= 0.0f)
            return castTime;

        // Comme la Hâte : +100 % de vitesse = temps divisé par 2
        int32 newCastTime = int32(float(castTime) / (1.0f + speedPct / 100.0f));
        return std::max(newCastTime, std::min(castTime, int32(s_settings.Refining.MinCastTimeMs)));
    }

    bool IsGatheringNode(GameObject const* go)
    {
        if (!go)
            return false;

        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return false;

        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] != LOCK_KEY_SKILL)
                continue;

            if (lock->Index[i] == LOCKTYPE_HERBALISM && s_settings.Refining.Herbalism)
                return true;

            if (lock->Index[i] == LOCKTYPE_MINING && s_settings.Refining.Mining)
                return true;
        }

        return false;
    }

    void ApplyGatheringYield(Player const* player, Loot* loot, GatherSource source)
    {
        if (!s_settings.Refining.Enabled || !player || !loot)
            return;

        if (source == GatherSource::Skinning && !s_settings.Refining.Skinning)
            return;

        float yieldPct = std::min(GetRefiningPct(player) * s_settings.Refining.YieldFactor, s_settings.Refining.MaxYieldPct);
        uint32 multiplier = RollMultiplier(yieldPct);
        if (multiplier <= 1)
            return;

        bool changed = false;
        for (LootItem& item : loot->items)
        {
            if (item.is_currency || item.needs_quest || !item.count)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto)
                continue;

            uint32 maxCount = std::min<uint32>(255, std::max<uint32>(1, proto->GetMaxStackSize()));
            uint32 newCount = std::min<uint32>(uint32(item.count) * multiplier, maxCount);
            if (newCount != item.count)
            {
                item.count = uint8(newCount);
                changed = true;
            }
        }

        if (changed)
            Announce(player, "Récolte", multiplier);
    }

    uint32 RollCraftMultiplier(Player const* player, SpellInfo const* spellInfo)
    {
        if (!s_settings.Refining.Enabled || !player || s_settings.Refining.CraftYieldFactor <= 0.0f)
            return 1;

        if (GetProfessionSpellType(spellInfo) != ProfessionSpellType::Crafting)
            return 1;

        float pct = std::min(GetRefiningPct(player) * s_settings.Refining.CraftYieldFactor, s_settings.Refining.MaxCraftYieldPct);
        uint32 multiplier = RollMultiplier(pct);
        Announce(player, "Artisanat", multiplier);
        return multiplier;
    }

    // ------------------------------------------------------------------
    // Addon client
    // Format : "V:<score>:<%dégâts>:<%réduction>;R:<score>:<%vitesse>:<%récolte>"
    // ------------------------------------------------------------------

    void SendStatsToClient(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        float versaPct = GetVersatilityPct(player);
        float donePct  = std::min(versaPct * s_settings.Versatility.DamageDoneFactor, s_settings.Versatility.MaxDonePct);
        float takenPct = std::min(versaPct * s_settings.Versatility.DamageTakenFactor, s_settings.Versatility.MaxTakenReductionPct);

        float refPct   = GetRefiningPct(player);
        float speedPct = std::min(refPct * s_settings.Refining.SpeedFactor, s_settings.Refining.MaxSpeedPct);
        float yieldPct = std::min(refPct * s_settings.Refining.YieldFactor, s_settings.Refining.MaxYieldPct);

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "V:%d:%.2f:%.2f;R:%d:%.2f:%.2f",
            player->GetCustomVersatilityRating(), donePct, takenPct,
            player->GetCustomRefiningRating(), speedPct, yieldPct);

        player->WhisperAddon(buffer, ADDON_PREFIX, player);
    }
}
