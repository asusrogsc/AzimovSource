/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Scripts for spells with SPELLFAMILY_GENERIC which cannot be included in AI script file
 * of creature using it or can't be bound to any player class.
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "spell_gen_"
 */

#include "ScriptMgr.h"
#include "Battleground.h"
#include "CellImpl.h"
#include "CommonPredicates.h"
#include "Containers.h"
#include "CreatureAI.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Log.h"
#include "MotionMaster.h"
#include "NPCPackets.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "PhasingHandler.h"
#include "ReputationMgr.h"
#include "SkillDiscovery.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "SpellScript.h"
#include "Vehicle.h"
#include "WorldStateMgr.h"

namespace spell_generic_cata
{
enum AdaptiveWarding
{
    SPELL_GEN_ADAPTIVE_WARDING_FIRE     = 28765,
    SPELL_GEN_ADAPTIVE_WARDING_NATURE   = 28768,
    SPELL_GEN_ADAPTIVE_WARDING_FROST    = 28766,
    SPELL_GEN_ADAPTIVE_WARDING_SHADOW   = 28769,
    SPELL_GEN_ADAPTIVE_WARDING_ARCANE   = 28770
};

enum AnimalBloodPoolSpell
{
    SPELL_ANIMAL_BLOOD      = 46221,
    SPELL_SPAWN_BLOOD_POOL  = 63471
};

class spell_spawn_blood_pool : public SpellScript
{
    void SetDest(SpellDestination& dest)
    {
        Unit* caster = GetCaster();
        Position summonPos = caster->GetPosition();
        LiquidData liquidStatus;
        if (caster->GetMap()->GetLiquidStatus(caster->GetPhaseShift(), caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ(), {}, &liquidStatus, caster->GetCollisionHeight()))
            summonPos.m_positionZ = liquidStatus.level;
        dest.Relocate(summonPos);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_spawn_blood_pool::SetDest, EFFECT_0, TARGET_DEST_CASTER);
    }
};

class spell_gen_arena_drink : public AuraScript
{
    bool Load() override
    {
        return GetCaster() && GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    bool Validate(SpellInfo const* spellInfo) override
    {
        if (!spellInfo->Effects[EFFECT_0].IsEffect() || !spellInfo->Effects[EFFECT_0].IsAura(SPELL_AURA_MOD_POWER_REGEN))
        {
            TC_LOG_ERROR("spells", "Aura {} structure has been changed - first aura is no longer SPELL_AURA_MOD_POWER_REGEN", GetId());
            return false;
        }

        return true;
    }

    void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& /*amplitude*/)
    {
        // Get SPELL_AURA_MOD_POWER_REGEN aura from spell
        AuraEffect* regen = GetAura()->Effects[EFFECT_0];
        if (!regen)
            return;

        // default case - not in arena
        if (!GetCaster()->ToPlayer()->InArena())
            isPeriodic = false;
    }

    void CalcAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& /*canBeRecalculated*/)
    {
        AuraEffect* regen = GetAura()->Effects[EFFECT_0];
        if (!regen)
            return;

        // default case - not in arena
        if (!GetCaster()->ToPlayer()->InArena())
            regen->ChangeAmount(amount);
    }

    void UpdatePeriodic(AuraEffect* aurEff)
    {
        AuraEffect* regen = GetAura()->Effects[EFFECT_0];
        if (!regen)
            return;

        // **********************************************
        // This feature used only in arenas
        // **********************************************
        // Here need increase mana regen per tick (6 second rule)
        // on 0 tick -   0  (handled in 2 second)
        // on 1 tick - 166% (handled in 4 second)
        // on 2 tick - 133% (handled in 6 second)

        // Apply bonus for 1 - 4 tick
        switch (aurEff->GetTickNumber())
        {
            case 1:   // 0%
                regen->ChangeAmount(0);
                break;
            case 2:   // 166%
                regen->ChangeAmount(aurEff->GetAmount() * 5 / 3);
                break;
            case 3:   // 133%
                regen->ChangeAmount(aurEff->GetAmount() * 4 / 3);
                break;
            default:  // 100% - normal regen
                regen->ChangeAmount(aurEff->GetAmount());
                // No need to update after 4th tick
                aurEff->SetPeriodic(false);
                break;
        }
    }

    void Register() override
    {
        DoEffectCalcPeriodic.Register(&spell_gen_arena_drink::CalcPeriodic, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
        DoEffectCalcAmount.Register(&spell_gen_arena_drink::CalcAmount, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
        OnEffectUpdatePeriodic.Register(&spell_gen_arena_drink::UpdatePeriodic, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_gen_aura_of_fear : public AuraScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return spellInfo->Effects[EFFECT_0].IsEffect() && ValidateSpellInfo({ spellInfo->Effects[EFFECT_0].TriggerSpell });
    }

    void PeriodicTick(AuraEffect const* aurEff)
    {
        PreventDefaultAction();
        if (!roll_chance_i(GetSpellInfo()->ProcChance))
            return;

        GetTarget()->CastSpell(nullptr, aurEff->GetSpellInfo()->Effects[aurEff->GetEffIndex()].TriggerSpell, true);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_aura_of_fear::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum GenericBandage
{
    SPELL_RECENTLY_BANDAGED     = 11196
};

enum BloodReserve
{
    SPELL_GEN_BLOOD_RESERVE_AURA = 64568,
    SPELL_GEN_BLOOD_RESERVE_HEAL = 64569
};

enum Bonked
{
    SPELL_BONKED            = 62991,
    SPELL_FOAM_SWORD_DEFEAT = 62994,
    SPELL_ON_GUARD          = 62972
};

enum BreakShieldSpells
{
    SPELL_BREAK_SHIELD_DAMAGE_2K                 = 62626,
    SPELL_BREAK_SHIELD_DAMAGE_10K                = 64590,

    SPELL_BREAK_SHIELD_TRIGGER_FACTION_MOUNTS    = 62575, // Also on ToC5 mounts
    SPELL_BREAK_SHIELD_TRIGGER_CAMPAING_WARHORSE = 64595,
    SPELL_BREAK_SHIELD_TRIGGER_UNK               = 66480
};

class spell_gen_cancel_aura : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_cancel_aura::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_cast_caster_to_target : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_cast_caster_to_target::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_cast_target_to_target : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_cast_target_to_target::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum CannibalizeSpells
{
    SPELL_CANNIBALIZE_TRIGGERED = 20578
};

class spell_gen_chains_of_ice : public AuraScript
{
    void UpdatePeriodic(AuraEffect* aurEff)
    {
        // Get 0 effect aura
        AuraEffect* slow = GetAura()->Effects[EFFECT_0];
        if (!slow)
            return;

        int32 newAmount = std::min(slow->GetAmount() + aurEff->GetAmount(), 0);
        slow->ChangeAmount(newAmount);
    }

    void Register() override
    {
        OnEffectUpdatePeriodic.Register(&spell_gen_chains_of_ice::UpdatePeriodic, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
    }
};

enum Clone
{
    SPELL_NIGHTMARE_FIGMENT_MIRROR_IMAGE        = 57528
};

enum CloneWeaponSpells
{
    SPELL_COPY_WEAPON_AURA       = 41054,
    SPELL_COPY_WEAPON_2_AURA     = 63418,
    SPELL_COPY_WEAPON_3_AURA     = 69893,

    SPELL_COPY_OFFHAND_AURA      = 45205,
    SPELL_COPY_OFFHAND_2_AURA    = 69896,

    SPELL_COPY_RANGED_AURA       = 57594
};

class spell_gen_consumption : public SpellScript
{
    void CalculateDamage(SpellEffectInfo const& /*spellEffectInfo*/, Unit const* /*victim*/, int32& damage, int32& /*flatMod*/, float& /*pctMod*/) const
    {
        if (SpellInfo const* createdBySpell = sSpellMgr->GetSpellInfo(GetCaster()->GetUInt32Value(UNIT_CREATED_BY_SPELL)))
            damage = createdBySpell->Effects[EFFECT_1].CalcValue();
    }

    void Register() override
    {
        CalcDamage.Register(&spell_gen_consumption::CalculateDamage);
    }
};

enum CreateLanceSpells
{
    SPELL_CREATE_LANCE_ALLIANCE = 63914,
    SPELL_CREATE_LANCE_HORDE    = 63919
};

enum DalaranDisguiseSpells
{
    SPELL_SUNREAVER_DISGUISE_TRIGGER       = 69672,
    SPELL_SUNREAVER_DISGUISE_FEMALE        = 70973,
    SPELL_SUNREAVER_DISGUISE_MALE          = 70974,

    SPELL_SILVER_COVENANT_DISGUISE_TRIGGER = 69673,
    SPELL_SILVER_COVENANT_DISGUISE_FEMALE  = 70971,
    SPELL_SILVER_COVENANT_DISGUISE_MALE    = 70972
};

enum FungalDecay
{
    // found in sniffs, there is no duration entry we can possibly use
    AURA_DURATION = 12600
};

enum DefendVisuals
{
    SPELL_VISUAL_SHIELD_1 = 63130,
    SPELL_VISUAL_SHIELD_2 = 63131,
    SPELL_VISUAL_SHIELD_3 = 63132
};

class spell_gen_despawn_aura : public AuraScript
{
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Creature* target = GetTarget()->ToCreature())
            target->DespawnOrUnsummon();
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_gen_despawn_aura::OnRemove, EFFECT_FIRST_FOUND, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_gen_despawn_target : public SpellScript
{
    void HandleDespawn(SpellEffIndex effIndex)
    {
        if (GetSpellInfo()->Effects[effIndex].IsEffect(SPELL_EFFECT_DUMMY) || GetSpellInfo()->Effects[effIndex].IsEffect(SPELL_EFFECT_SCRIPT_EFFECT))
            if (Creature* target = GetHitCreature())
                target->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_despawn_target::HandleDespawn, EFFECT_ALL, SPELL_EFFECT_ANY);
    }
};

enum DivineStormSpell
{
    SPELL_DIVINE_STORM      = 53385,
};

enum DreamingGlory
{
    SPELL_DREAMING_GLORY    = 28694
};

class spell_gen_dreaming_glory : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DREAMING_GLORY });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_DREAMING_GLORY, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_dreaming_glory::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum EtherealPet
{
    NPC_ETHEREAL_SOUL_TRADER        = 27914,

    SAY_STEAL_ESSENCE               = 1,
    SAY_CREATE_TOKEN                = 2,

    SPELL_PROC_TRIGGER_ON_KILL_AURA = 50051,
    SPELL_ETHEREAL_PET_AURA         = 50055,
    SPELL_CREATE_TOKEN              = 50063,
    SPELL_STEAL_ESSENCE_VISUAL      = 50101
};

enum Feast
{
    SPELL_GREAT_FEAST                   = 57337,
    SPELL_FISH_FEAST                    = 57397,
    SPELL_GIGANTIC_FEAST                = 58466,
    SPELL_SMALL_FEAST                   = 58475,
    SPELL_BOUNTIFUL_FEAST               = 66477,

    SPELL_FEAST_FOOD                    = 45548,
    SPELL_FEAST_DRINK                   = 57073,
    SPELL_BOUNTIFUL_FEAST_DRINK         = 66041,
    SPELL_BOUNTIFUL_FEAST_FOOD          = 66478,

    SPELL_GREAT_FEAST_REFRESHMENT       = 57338,
    SPELL_FISH_FEAST_REFRESHMENT        = 57398,
    SPELL_GIGANTIC_FEAST_REFRESHMENT    = 58467,
    SPELL_SMALL_FEAST_REFRESHMENT       = 58477,
    SPELL_BOUNTIFUL_FEAST_REFRESHMENT   = 66622
};

class spell_gen_feast : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_FEAST_FOOD,
            SPELL_FEAST_DRINK,
            SPELL_BOUNTIFUL_FEAST_DRINK,
            SPELL_BOUNTIFUL_FEAST_FOOD,

            SPELL_GREAT_FEAST_REFRESHMENT,
            SPELL_FISH_FEAST_REFRESHMENT,
            SPELL_GIGANTIC_FEAST_REFRESHMENT,
            SPELL_SMALL_FEAST_REFRESHMENT,
            SPELL_BOUNTIFUL_FEAST_REFRESHMENT
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();

        switch (GetSpellInfo()->Id)
        {
            case SPELL_GREAT_FEAST:
                target->CastSpell(target, SPELL_FEAST_FOOD);
                target->CastSpell(target, SPELL_FEAST_DRINK);
                target->CastSpell(target, SPELL_GREAT_FEAST_REFRESHMENT);
                break;
            case SPELL_FISH_FEAST:
                target->CastSpell(target, SPELL_FEAST_FOOD);
                target->CastSpell(target, SPELL_FEAST_DRINK);
                target->CastSpell(target, SPELL_FISH_FEAST_REFRESHMENT);
                break;
            case SPELL_GIGANTIC_FEAST:
                target->CastSpell(target, SPELL_FEAST_FOOD);
                target->CastSpell(target, SPELL_FEAST_DRINK);
                target->CastSpell(target, SPELL_GIGANTIC_FEAST_REFRESHMENT);
                break;
            case SPELL_SMALL_FEAST:
                target->CastSpell(target, SPELL_FEAST_FOOD);
                target->CastSpell(target, SPELL_FEAST_DRINK);
                target->CastSpell(target, SPELL_SMALL_FEAST_REFRESHMENT);
                break;
            case SPELL_BOUNTIFUL_FEAST:
                target->CastSpell(target, SPELL_BOUNTIFUL_FEAST_REFRESHMENT);
                target->CastSpell(target, SPELL_BOUNTIFUL_FEAST_DRINK);
                target->CastSpell(target, SPELL_BOUNTIFUL_FEAST_FOOD);
                break;
            default:
                break;
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_feast::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum FuriousRage
{
    EMOTE_FURIOUS_RAGE       = 19415,
    EMOTE_EXHAUSTED          = 18368,
    SPELL_EXHAUSTION         = 35492
};

class spell_gen_furious_rage : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EXHAUSTION }) &&
            (sObjectMgr->GetBroadcastText(EMOTE_FURIOUS_RAGE) != nullptr) &&
            (sObjectMgr->GetBroadcastText(EMOTE_EXHAUSTED) != nullptr);
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->Unit::TextEmote(EMOTE_FURIOUS_RAGE, target, false);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (!GetTargetApplication()->GetRemoveMode().HasFlag(AuraRemoveFlags::Expired))
            return;

        Unit* target = GetTarget();
        target->Unit::TextEmote(EMOTE_EXHAUSTED, target, false);
        target->CastSpell(target, SPELL_EXHAUSTION, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_gen_furious_rage::AfterApply, EFFECT_0, SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_gen_furious_rage::AfterRemove, EFFECT_0, SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_gen_5000_gold : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* target = GetHitPlayer())
            target->ModifyMoney(5000 * GOLD);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_5000_gold::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum FishingSpells
{
    SPELL_FISHING_NO_FISHING_POLE   = 131476,
    SPELL_FISHING_WITH_POLE         = 131490
};

enum TransporterBackfires
{
    SPELL_TRANSPORTER_MALFUNCTION_POLYMORPH     = 23444,
    SPELL_TRANSPORTER_EVIL_TWIN                 = 23445,
    SPELL_TRANSPORTER_MALFUNCTION_MISS          = 36902
};

enum GnomishTransporter
{
    SPELL_TRANSPORTER_SUCCESS                   = 23441,
    SPELL_TRANSPORTER_FAILURE                   = 23446
};

class spell_gen_gryphon_wyvern_mount_check : public AuraScript
{
    void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        Unit* owner = target->GetOwner();

        if (!owner)
            return;

        if (owner->IsMounted())
            target->SetDisableGravity(true);
        else
            target->SetDisableGravity(false);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_gryphon_wyvern_mount_check::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_gen_hate_to_zero_caster_target : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (target->CanHaveThreatList())
                target->GetThreatManager().ModifyThreatByPercent(GetCaster(), -100);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_hate_to_zero_caster_target::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum Interrupt
{
    SPELL_GEN_THROW_INTERRUPT           = 32747
};

enum GenericLifebloom
{
    SPELL_TUR_RAGEPAW_LIFEBLOOM_FINAL_HEAL              = 52552,
    SPELL_CENARION_SCOUT_LIFEBLOOM_FINAL_HEAL           = 53692,
    SPELL_TWISTED_VISAGE_LIFEBLOOM_FINAL_HEAL           = 57763,
    SPELL_FACTION_CHAMPIONS_DRU_LIFEBLOOM_FINAL_HEAL    = 66094
};

enum ChargeSpells
{
    SPELL_CHARGE_DAMAGE_8K5             = 62874,
    SPELL_CHARGE_DAMAGE_20K             = 68498,
    SPELL_CHARGE_DAMAGE_45K             = 64591,

    SPELL_CHARGE_CHARGING_EFFECT_8K5    = 63661,
    SPELL_CHARGE_CHARGING_EFFECT_20K_1  = 68284,
    SPELL_CHARGE_CHARGING_EFFECT_20K_2  = 68501,
    SPELL_CHARGE_CHARGING_EFFECT_45K_1  = 62563,
    SPELL_CHARGE_CHARGING_EFFECT_45K_2  = 66481,

    SPELL_CHARGE_TRIGGER_FACTION_MOUNTS = 62960,
    SPELL_CHARGE_TRIGGER_TRIAL_CHAMPION = 68282,

    SPELL_CHARGE_MISS_EFFECT            = 62977,
};

enum MossCoveredFeet
{
    SPELL_FALL_DOWN = 6869
};

enum Netherbloom : uint32
{
    SPELL_NETHERBLOOM_POLLEN_1      = 28703
};

enum NightmareVine
{
    SPELL_NIGHTMARE_POLLEN      = 28721
};

class spell_gen_nitrous_boost : public AuraScript
{
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();

        if (GetCaster() && GetTarget()->GetPower(POWER_MANA) >= 10)
            GetTarget()->ModifyPower(POWER_MANA, -10);
        else
            Remove();
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_nitrous_boost::PeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum ObsidianArmor
{
    SPELL_GEN_OBSIDIAN_ARMOR_HOLY       = 27536,
    SPELL_GEN_OBSIDIAN_ARMOR_FIRE       = 27533,
    SPELL_GEN_OBSIDIAN_ARMOR_NATURE     = 27538,
    SPELL_GEN_OBSIDIAN_ARMOR_FROST      = 27534,
    SPELL_GEN_OBSIDIAN_ARMOR_SHADOW     = 27535,
    SPELL_GEN_OBSIDIAN_ARMOR_ARCANE     = 27540
};

enum OrcDisguiseSpells
{
    SPELL_ORC_DISGUISE_TRIGGER       = 45759,
    SPELL_ORC_DISGUISE_MALE          = 45760,
    SPELL_ORC_DISGUISE_FEMALE        = 45762
};

enum ParalyticPoison
{
    SPELL_PARALYSIS = 35202
};

class spell_gen_player_say : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return sObjectMgr->GetBroadcastText(uint32(spellInfo->Effects[EFFECT_0].CalcValue())) != nullptr;
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        // Note: target here is always player; caster here is gameobject, creature or player (self cast)
        if (Unit* target = GetHitUnit())
            target->Unit::Say(uint32(GetEffectValue()), target);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_player_say::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ParachuteSpells
{
    SPELL_PARACHUTE         = 45472,
    SPELL_PARACHUTE_BUFF    = 44795,
};

enum PetSummoned
{
    NPC_DOOMGUARD       = 11859,
    NPC_INFERNAL        = 89,
    NPC_IMP             = 416
};

class spell_gen_pet_wait : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->GetMotionMaster()->Clear();
        GetCaster()->GetMotionMaster()->MoveIdle();
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_gen_pet_wait::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ProfessionResearch
{
    SPELL_NORTHREND_INSCRIPTION_RESEARCH = 61177
};

enum TrinketSpells
{
    SPELL_PVP_TRINKET_ALLIANCE  = 97403,
    SPELL_PVP_TRINKET_HORDE     = 97404
};

class spell_gen_remove_impairing_auras : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        GetHitUnit()->RemoveMovementImpairingAuras(true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_remove_impairing_auras::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_restoration : public AuraScript
{
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();

        Unit* target = GetTarget();
        if (!target)
            return;

        int32 heal = target->CountPctFromMaxHealth(10);
        HealInfo healInfo(target, target, heal, GetSpellInfo(), GetSpellInfo()->GetSchoolMask());
        target->HealBySpell(healInfo);

        /// @todo: should proc other auras?
        if (int32 mana = target->GetMaxPower(POWER_MANA))
        {
            mana /= 10;
            target->EnergizeBySpell(target, GetSpellInfo(), mana, POWER_MANA);
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_restoration::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

class spell_gen_remove_on_health_pct : public AuraScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return spellInfo->Effects[EFFECT_1].IsEffect();
    }

    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        // they apply damage so no need to check for ticks here

        if (GetTarget()->HealthAbovePct(GetSpellInfo()->Effects[EFFECT_1].CalcValue()))
        {
            Remove(AuraRemoveFlags::ByEnemySpell);
            PreventDefaultAction();
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_remove_on_health_pct::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

class spell_gen_remove_on_full_health : public AuraScript
{
    void PeriodicTick(AuraEffect const* aurEff)
    {
        // if it has only periodic effect, allow 1 tick
        bool onlyEffect = !GetSpellInfo()->Effects[EFFECT_1].IsEffect();
        if (onlyEffect && aurEff->GetTickNumber() <= 1)
            return;

        if (GetTarget()->IsFullHealth())
        {
            Remove(AuraRemoveFlags::ByEnemySpell);
            PreventDefaultAction();
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_remove_on_full_health::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

class spell_gen_remove_on_full_health_pct : public AuraScript
{
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        // they apply damage so no need to check for ticks here

        if (GetTarget()->IsFullHealth())
        {
            Remove(AuraRemoveFlags::ByEnemySpell);
            PreventDefaultAction();
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_remove_on_full_health_pct::PeriodicTick, EFFECT_2, SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
    }
};

enum Replenishment
{
    SPELL_REPLENISHMENT             = 57669,
    SPELL_INFINITE_REPLENISHMENT    = 61782
};

class spell_gen_revive_self : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->SetFullHealth();
        GetCaster()->SetFullPower(POWER_MANA);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_gen_revive_self::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum RunningWildMountIds
{
    SPELL_ALTERED_FORM          = 97709
};

enum SeaforiumSpells
{
    SPELL_PLANT_CHARGES_CREDIT_ACHIEVEMENT  = 60937
};

enum TournamentMountsSpells
{
    SPELL_LANCE_EQUIPPED     = 62853
};

enum MountedDuelSpells
{
    SPELL_ON_TOURNAMENT_MOUNT = 63034,
    SPELL_MOUNTED_DUEL        = 62875
};

enum Teleporting
{
    AREA_VIOLET_CITADEL_SPIRE   = 4637,

    SPELL_TELEPORT_SPIRE_DOWN   = 59316,
    SPELL_TELEPORT_SPIRE_UP     = 59314
};

class spell_gen_teleporting : public SpellScript
{
    void HandleScript(SpellEffIndex /* effIndex */)
    {
        Unit* target = GetHitUnit();
        if (target->GetTypeId() != TYPEID_PLAYER)
            return;

        // return from top
        if (target->ToPlayer()->GetAreaId() == AREA_VIOLET_CITADEL_SPIRE)
            target->CastSpell(target, SPELL_TELEPORT_SPIRE_DOWN, true);
            // teleport atop
        else
            target->CastSpell(target, SPELL_TELEPORT_SPIRE_UP, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_teleporting::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_trigger_exclude_caster_aura_spell : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->ExcludeCasterAuraSpell });
    }

    void HandleTrigger()
    {
        // Blizz seems to just apply aura without bothering to cast
        GetCaster()->AddAura(GetSpellInfo()->ExcludeCasterAuraSpell, GetCaster());
    }

    void Register() override
    {
        AfterCast.Register(&spell_gen_trigger_exclude_caster_aura_spell::HandleTrigger);
    }
};

class spell_gen_trigger_exclude_target_aura_spell : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->ExcludeTargetAuraSpell });
    }

    void HandleTrigger()
    {
        if (Unit* target = GetHitUnit())
            // Blizz seems to just apply aura without bothering to cast
            GetCaster()->AddAura(GetSpellInfo()->ExcludeTargetAuraSpell, target);
    }

    void Register() override
    {
        AfterHit.Register(&spell_gen_trigger_exclude_target_aura_spell::HandleTrigger);
    }
};

enum PvPTrinketTriggeredSpells
{
    SPELL_WILL_OF_THE_FORSAKEN_COOLDOWN_TRIGGER         = 72752,
    SPELL_WILL_OF_THE_FORSAKEN_COOLDOWN_TRIGGER_WOTF    = 72757
};

enum FriendOrFowl
{
    SPELL_TURKEY_VENGEANCE      = 25285
};

enum FoamSword
{
    ITEM_FOAM_SWORD_GREEN   = 45061,
    ITEM_FOAM_SWORD_PINK    = 45176,
    ITEM_FOAM_SWORD_BLUE    = 45177,
    ITEM_FOAM_SWORD_RED     = 45178,
    ITEM_FOAM_SWORD_YELLOW  = 45179
};

enum VampiricTouch
{
    SPELL_VAMPIRIC_TOUCH_HEAL   = 52724
};

enum VehicleScaling
{
    SPELL_GEAR_SCALING      = 66668
};

enum VendorBarkTrigger
{
    NPC_AMPHITHEATER_VENDOR     = 30098,
    SAY_AMPHITHEATER_VENDOR     = 0
};

enum WhisperGulchYoggSaronWhisper
{
    SPELL_YOGG_SARON_WHISPER_DUMMY  = 29072
};

class spell_gen_whisper_to_controller : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return sObjectMgr->GetBroadcastText(uint32(spellInfo->Effects[EFFECT_0].CalcValue())) != nullptr;
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (TempSummon* casterSummon = GetCaster()->ToTempSummon())
            if (Player* target = casterSummon->GetSummoner()->ToPlayer())
                casterSummon->Unit::Whisper(uint32(GetEffectValue()), target, false);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_gen_whisper_to_controller::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum WhisperToControllerTexts
{
    WHISPER_FUTURE_YOU   = 2,
    WHISPER_DEFENDER     = 1,
    WHISPER_PAST_YOU     = 2
};

enum GMFreeze
{
    SPELL_GM_FREEZE = 9454
};

enum RequiredMixologySpells
{
    SPELL_MIXOLOGY                      = 53042,
    // Flasks
    SPELL_FLASK_OF_THE_FROST_WYRM       = 53755,
    SPELL_FLASK_OF_STONEBLOOD           = 53758,
    SPELL_FLASK_OF_ENDLESS_RAGE         = 53760,
    SPELL_FLASK_OF_PURE_MOJO            = 54212,
    SPELL_LESSER_FLASK_OF_RESISTANCE    = 62380,
    SPELL_LESSER_FLASK_OF_TOUGHNESS     = 53752,
    SPELL_FLASK_OF_BLINDING_LIGHT       = 28521,
    SPELL_FLASK_OF_CHROMATIC_WONDER     = 42735,
    SPELL_FLASK_OF_FORTIFICATION        = 28518,
    SPELL_FLASK_OF_MIGHTY_RESTORATION   = 28519,
    SPELL_FLASK_OF_PURE_DEATH           = 28540,
    SPELL_FLASK_OF_RELENTLESS_ASSAULT   = 28520,
    SPELL_FLASK_OF_CHROMATIC_RESISTANCE = 17629,
    SPELL_FLASK_OF_DISTILLED_WISDOM     = 17627,
    SPELL_FLASK_OF_SUPREME_POWER        = 17628,
    SPELL_FLASK_OF_THE_TITANS           = 17626,
    // Elixirs
    SPELL_ELIXIR_OF_MIGHTY_AGILITY      = 28497,
    SPELL_ELIXIR_OF_ACCURACY            = 60340,
    SPELL_ELIXIR_OF_DEADLY_STRIKES      = 60341,
    SPELL_ELIXIR_OF_MIGHTY_DEFENSE      = 60343,
    SPELL_ELIXIR_OF_EXPERTISE           = 60344,
    SPELL_ELIXIR_OF_ARMOR_PIERCING      = 60345,
    SPELL_ELIXIR_OF_LIGHTNING_SPEED     = 60346,
    SPELL_ELIXIR_OF_MIGHTY_FORTITUDE    = 53751,
    SPELL_ELIXIR_OF_MIGHTY_MAGEBLOOD    = 53764,
    SPELL_ELIXIR_OF_MIGHTY_STRENGTH     = 53748,
    SPELL_ELIXIR_OF_MIGHTY_TOUGHTS      = 60347,
    SPELL_ELIXIR_OF_PROTECTION          = 53763,
    SPELL_ELIXIR_OF_SPIRIT              = 53747,
    SPELL_GURUS_ELIXIR                  = 53749,
    SPELL_SHADOWPOWER_ELIXIR            = 33721,
    SPELL_WRATH_ELIXIR                  = 53746,
    SPELL_ELIXIR_OF_EMPOWERMENT         = 28514,
    SPELL_ELIXIR_OF_MAJOR_MAGEBLOOD     = 28509,
    SPELL_ELIXIR_OF_MAJOR_SHADOW_POWER  = 28503,
    SPELL_ELIXIR_OF_MAJOR_DEFENSE       = 28502,
    SPELL_FEL_STRENGTH_ELIXIR           = 38954,
    SPELL_ELIXIR_OF_IRONSKIN            = 39628,
    SPELL_ELIXIR_OF_MAJOR_AGILITY       = 54494,
    SPELL_ELIXIR_OF_DRAENIC_WISDOM      = 39627,
    SPELL_ELIXIR_OF_MAJOR_FIREPOWER     = 28501,
    SPELL_ELIXIR_OF_MAJOR_FROST_POWER   = 28493,
    SPELL_EARTHEN_ELIXIR                = 39626,
    SPELL_ELIXIR_OF_MASTERY             = 33726,
    SPELL_ELIXIR_OF_HEALING_POWER       = 28491,
    SPELL_ELIXIR_OF_MAJOR_FORTITUDE     = 39625,
    SPELL_ELIXIR_OF_MAJOR_STRENGTH      = 28490,
    SPELL_ADEPTS_ELIXIR                 = 54452,
    SPELL_ONSLAUGHT_ELIXIR              = 33720,
    SPELL_MIGHTY_TROLLS_BLOOD_ELIXIR    = 24361,
    SPELL_GREATER_ARCANE_ELIXIR         = 17539,
    SPELL_ELIXIR_OF_THE_MONGOOSE        = 17538,
    SPELL_ELIXIR_OF_BRUTE_FORCE         = 17537,
    SPELL_ELIXIR_OF_SAGES               = 17535,
    SPELL_ELIXIR_OF_SUPERIOR_DEFENSE    = 11348,
    SPELL_ELIXIR_OF_DEMONSLAYING        = 11406,
    SPELL_ELIXIR_OF_GREATER_FIREPOWER   = 26276,
    SPELL_ELIXIR_OF_SHADOW_POWER        = 11474,
    SPELL_MAGEBLOOD_ELIXIR              = 24363,
    SPELL_ELIXIR_OF_GIANTS              = 11405,
    SPELL_ELIXIR_OF_GREATER_AGILITY     = 11334,
    SPELL_ARCANE_ELIXIR                 = 11390,
    SPELL_ELIXIR_OF_GREATER_INTELLECT   = 11396,
    SPELL_ELIXIR_OF_GREATER_DEFENSE     = 11349,
    SPELL_ELIXIR_OF_FROST_POWER         = 21920,
    SPELL_ELIXIR_OF_AGILITY             = 11328,
    SPELL_MAJOR_TROLLS_BLLOOD_ELIXIR    =  3223,
    SPELL_ELIXIR_OF_FORTITUDE           =  3593,
    SPELL_ELIXIR_OF_OGRES_STRENGTH      =  3164,
    SPELL_ELIXIR_OF_FIREPOWER           =  7844,
    SPELL_ELIXIR_OF_LESSER_AGILITY      =  3160,
    SPELL_ELIXIR_OF_DEFENSE             =  3220,
    SPELL_STRONG_TROLLS_BLOOD_ELIXIR    =  3222,
    SPELL_ELIXIR_OF_MINOR_ACCURACY      = 63729,
    SPELL_ELIXIR_OF_WISDOM              =  3166,
    SPELL_ELIXIR_OF_GIANTH_GROWTH       =  8212,
    SPELL_ELIXIR_OF_MINOR_AGILITY       =  2374,
    SPELL_ELIXIR_OF_MINOR_FORTITUDE     =  2378,
    SPELL_WEAK_TROLLS_BLOOD_ELIXIR      =  3219,
    SPELL_ELIXIR_OF_LIONS_STRENGTH      =  2367,
    SPELL_ELIXIR_OF_MINOR_DEFENSE       =   673
};

enum LandmineKnockbackAchievement
{
    SPELL_LANDMINE_KNOCKBACK_ACHIEVEMENT = 57064
};

enum PonySpells
{
    ACHIEV_PONY_UP              = 3736,
    MOUNT_PONY                  = 29736
};

enum CorruptinPlagueEntrys
{
    NPC_APEXIS_FLAYER       = 22175,
    NPC_SHARD_HIDE_BOAR     = 22180,
    NPC_AETHER_RAY          = 22181,
    SPELL_CORRUPTING_PLAGUE = 40350
};

class CorruptingPlagueSearcher
{
public:
    CorruptingPlagueSearcher(Unit* obj, float distance) : _unit(obj), _distance(distance)  { }

    bool operator()(Unit* u) const
    {
        if (_unit->GetDistance2d(u) < _distance &&
            (u->GetEntry() == NPC_APEXIS_FLAYER || u->GetEntry() == NPC_SHARD_HIDE_BOAR || u->GetEntry() == NPC_AETHER_RAY) &&
            !u->HasAura(SPELL_CORRUPTING_PLAGUE))
            return true;

        return false;
    }

private:
    Unit* _unit;
    float _distance;
};

class spell_corrupting_plague_aura : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CORRUPTING_PLAGUE });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* owner = GetTarget();

        std::list<Creature*> targets;
        CorruptingPlagueSearcher creature_check(owner, 15.0f);
        Trinity::CreatureListSearcher<CorruptingPlagueSearcher> creature_searcher(owner, targets, creature_check);
        Cell::VisitGridObjects(owner, creature_searcher, 15.0f);

        if (!targets.empty())
            return;

        PreventDefaultAction();
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_corrupting_plague_aura::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum StasisFieldEntrys
{
    NPC_DAGGERTAIL_LIZARD    = 22255,
    SPELL_STASIS_FIELD       = 40307
};

class StasisFieldSearcher
{
public:
    StasisFieldSearcher(Unit* obj, float distance) : _unit(obj), _distance(distance)  { }

    bool operator()(Unit* u) const
    {
        if (_unit->GetDistance2d(u) < _distance &&
            (u->GetEntry() == NPC_APEXIS_FLAYER || u->GetEntry() == NPC_SHARD_HIDE_BOAR || u->GetEntry() == NPC_AETHER_RAY || u->GetEntry() == NPC_DAGGERTAIL_LIZARD) &&
            !u->HasAura(SPELL_STASIS_FIELD))
            return true;

        return false;
    }

private:
    Unit* _unit;
    float _distance;
};

class spell_stasis_field_aura : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_STASIS_FIELD });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* owner = GetTarget();

        std::list<Creature*> targets;
        StasisFieldSearcher creature_check(owner, 15.0f);
        Trinity::CreatureListSearcher<StasisFieldSearcher> creature_searcher(owner, targets, creature_check);
        Cell::VisitGridObjects(owner, creature_searcher, 15.0f);

        if (!targets.empty())
            return;

        PreventDefaultAction();
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_stasis_field_aura::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum SiegeTankControl
{
    SPELL_SIEGE_TANK_CONTROL = 47963
};

enum FreezingCircleMisc
{
    SPELL_FREEZING_CIRCLE_PIT_OF_SARON_NORMAL = 69574,
    SPELL_FREEZING_CIRCLE_PIT_OF_SARON_HEROIC = 70276,
    SPELL_FREEZING_CIRCLE                     = 34787,
    SPELL_FREEZING_CIRCLE_SCENARIO            = 141383,
    MAP_ID_BLOOD_IN_THE_SNOW_SCENARIO         = 1130
};

enum CannonBlast
{
    SPELL_CANNON_BLAST          = 42578,
    SPELL_CANNON_BLAST_DAMAGE   = 42576
};

class spell_gen_cannon_blast : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CANNON_BLAST });
    }
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        int32 bp = GetEffectValue();
        Unit* target = GetHitUnit();
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(bp);
        target->CastSpell(target, SPELL_CANNON_BLAST_DAMAGE, args);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_cannon_blast::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_submerged : public SpellScript
{
    void HandleScript(SpellEffIndex /*eff*/)
    {
        if (Creature* target = GetHitCreature())
            target->SetStandState(UNIT_STAND_STATE_SUBMERGED);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_submerged::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum KazrogalHellfireMark
{
    SPELL_MARK_OF_KAZROGAL_HELLFIRE = 189512,
    SPELL_MARK_OF_KAZROGAL_DAMAGE_HELLFIRE = 189515
};

enum AuraProcRemoveSpells
{
    SPELL_FACE_RAGE         = 99947,
    SPELL_IMPATIENT_MIND    = 187213
};

enum DefenderOfAzerothData
{
    SPELL_DEATH_GATE_TELEPORT_STORMWIND = 316999,
    SPELL_DEATH_GATE_TELEPORT_ORGRIMMAR = 317000,

    QUEST_DEFENDER_OF_AZEROTH_ALLIANCE  = 58902,
    QUEST_DEFENDER_OF_AZEROTH_HORDE     = 58903,

    NPC_NAZGRIM                         = 161706,
    NPC_TROLLBANE                       = 161707,
    NPC_WHITEMANE                       = 161708,
    NPC_MOGRAINE                        = 161709,
};

class spell_gen_anchor_here : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Creature* creature = GetHitCreature())
            creature->SetHomePosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation());
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_anchor_here::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum AncestralCallSpells
{
    SPELL_RICTUS_OF_THE_LAUGHING_SKULL  = 274739,
    SPELL_ZEAL_OF_THE_BURNING_BLADE     = 274740,
    SPELL_FEROCITY_OF_THE_FROSTWOLF     = 274741,
    SPELL_MIGHT_OF_THE_BLACKROCK        = 274742
};

class spell_gen_eject_passengers_3_8 : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Vehicle* vehicle = GetHitUnit()->GetVehicleKit();
        if (!vehicle)
            return;

        for (uint8 i = 2; i < 8; i++)
        {
            if (Unit* passenger = vehicle->GetPassenger(i))
                passenger->ExitVehicle();
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_eject_passengers_3_8::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_reverse_cast_target_to_caster_triggered: public SpellScript
{
    void HandleScript(SpellEffIndex effIndex)
    {
        GetHitUnit()->CastSpell(GetCaster(), GetSpellInfo()->Effects[effIndex].CalcValue(), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_reverse_cast_target_to_caster_triggered::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_gen_despawn_all_summons_owned_by_caster : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
        {
            Creature* target = GetHitCreature();

            if (target->GetOwner() == caster)
                target->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_despawn_all_summons_owned_by_caster::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum SkinningLearningSpell
{
     SPELL_CLASSIC_SKINNING      = 265856,
     SPELL_OUTLAND_SKINNING      = 265858,
     SPELL_NORTHREND_SKINNING    = 265860,
     SPELL_CATACLYSM_SKINNING    = 265862,
     SPELL_PANDARIA_SKINNING     = 265864,
     SPELL_DRAENOR_SKINNING      = 265866,
     SPELL_LEGION_SKINNING       = 265868,
     SPELL_KUL_TIRAN_SKINNING    = 265870,
     SPELL_ZANDALARI_SKINNING    = 265872,
     SPELL_SHADOWLANDS_SKINNING  = 308570,
     SPELL_DRAGON_ISLES_SKINNING = 366263
};

enum BloodlustExhaustionSpell : uint32
{
    SPELL_SHAMAN_SATED               = 57724, // Bloodlust
    SPELL_SHAMAN_EXHAUSTION          = 57723, // Heroism, Drums
    SPELL_MAGE_TEMPORAL_DISPLACEMENT = 80354,
    SPELL_HUNTER_FATIGUED            = 264689,
    SPELL_EVOKER_EXHAUSTION          = 390435,
};

enum GoblinTownInABoxSpells
{
    SPELL_TOY_STABILITY             = 440366,
    SPELL_CRAZY_GADGETS_TRIGGERED   = 437918,
    SPELL_OVERCHARGED_TOWN          = 448591,
    SPELL_SUMMON_TOWN_TRIGGER       = 440366
};

static constexpr uint32 SPELL_PVP_TRINKET_NEUTRAL = 97979;
}

void AddSC_spell_generic_cata()
{
    using namespace spell_generic_cata;
    RegisterSpellScript(spell_spawn_blood_pool);
    RegisterSpellScript(spell_gen_arena_drink);
    RegisterSpellScript(spell_gen_aura_of_fear);
    RegisterSpellScript(spell_gen_cancel_aura);
    RegisterSpellScript(spell_gen_cast_caster_to_target);
    RegisterSpellScript(spell_gen_cast_target_to_target);
    RegisterSpellScript(spell_gen_chains_of_ice);
    RegisterSpellScript(spell_gen_consumption);
    RegisterSpellScript(spell_gen_despawn_aura);
    RegisterSpellScript(spell_gen_despawn_target);
    RegisterSpellScript(spell_gen_dreaming_glory);
    RegisterSpellScript(spell_gen_feast);
    RegisterSpellScript(spell_gen_furious_rage);
    RegisterSpellScript(spell_gen_5000_gold);
    RegisterSpellScript(spell_gen_gryphon_wyvern_mount_check);
    RegisterSpellScript(spell_gen_hate_to_zero_caster_target);
    RegisterSpellScript(spell_gen_nitrous_boost);
    RegisterSpellScript(spell_gen_player_say);
    RegisterSpellScript(spell_gen_pet_wait);
    RegisterSpellScript(spell_gen_remove_impairing_auras);
    RegisterSpellScript(spell_gen_restoration);
    RegisterSpellScript(spell_gen_remove_on_health_pct);
    RegisterSpellScript(spell_gen_remove_on_full_health);
    RegisterSpellScript(spell_gen_remove_on_full_health_pct);
    RegisterSpellScript(spell_gen_revive_self);
    RegisterSpellScript(spell_gen_teleporting);
    RegisterSpellScript(spell_gen_trigger_exclude_caster_aura_spell);
    RegisterSpellScript(spell_gen_trigger_exclude_target_aura_spell);
    RegisterSpellScript(spell_gen_whisper_to_controller);
    RegisterSpellScript(spell_corrupting_plague_aura);
    RegisterSpellScript(spell_stasis_field_aura);
    RegisterSpellScript(spell_gen_cannon_blast);
    RegisterSpellScript(spell_gen_submerged);
    RegisterSpellScript(spell_gen_anchor_here);
    RegisterSpellScript(spell_gen_eject_passengers_3_8);
    RegisterSpellScript(spell_gen_reverse_cast_target_to_caster_triggered);
    RegisterSpellScript(spell_gen_despawn_all_summons_owned_by_caster);
}
