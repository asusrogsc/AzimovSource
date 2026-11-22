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

#include "ScriptMgr.h"
#include "Containers.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace howling_fjord_cata
{
enum Daegarnn
{
    QUEST_DEFEAT_AT_RING = 11300,
    NPC_FIRJUS = 24213,
    NPC_JLARBORN = 24215,
    NPC_YOROS = 24214,
    NPC_OLUF = 23931,
    NPC_PRISONER_1 = 24253, // looks the same but has different abilities
    NPC_PRISONER_2 = 24254,
    NPC_PRISONER_3 = 24255,
    SAY_TEXT = 0
};

enum GuideOurSights
{
    SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_01    = 43081,
    SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_02    = 43082
};

class spell_fjord_guide_our_sights_fx_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_01,
            SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_02
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_01);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_02);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_CANNON_PART_02);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_guide_our_sights_fx_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum SomeAssemblyRequired
{
    SPELL_MINDLESS_ABOMINATION_CONTROL         = 42168,
    SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON    = 42266,
    SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE      = 42267,
    SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2    = 42274
};

class spell_fjord_mindless_abomination_ping_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MINDLESS_ABOMINATION_CONTROL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_MINDLESS_ABOMINATION_CONTROL);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_mindless_abomination_ping_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_fjord_mindless_abomination_explosion_fx_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON,
            SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE,
            SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2
        });
    }

    void HandleScript(SpellEffIndex /*eff*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE_2);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_BONE);
        caster->CastSpell(caster, SPELL_RANDOM_CIRCUMFERENCE_POINT_POISON);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_mindless_abomination_explosion_fx_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum RivenwoodCaptives
{
    SPELL_SUMMON_BABY_RIVEN_WIDOWS        = 43275,
    SPELL_SUMMON_DARKCLAW_BAT             = 43276,
    SPELL_SUMMON_FANGGORE_WORG            = 43277,
    SPELL_SUMMON_GJALERBRON_RUNECASTER    = 43278,
    SPELL_SUMMON_GJALERBRON_SLEEPWATCHER  = 43279,
    SPELL_SUMMON_GJALERBRON_WARRIOR       = 43280,
    SPELL_SUMMON_PUTRID_HORROR            = 43281,
    SPELL_SUMMON_WINTERSKORN_BERSERKER    = 43282,
    SPELL_SUMMON_WINTERSKORN_WOODSMAN     = 43283,
    SPELL_SUMMON_WINTERSKORN_TRIBESMAN    = 43284,
    SPELL_SUMMON_WINTERSKORN_ORACLE       = 43285,
    SPELL_SUMMON_FREED_MIST_WHISPER_SCOUT = 43289
};

class spell_fjord_rivenwood_captives_not_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_rivenwood_captives_not_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_fjord_rivenwood_captives_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_SUMMON_FREED_MIST_WHISPER_SCOUT });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        if (roll_chance_i(80))
            target->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            target->CastSpell(caster, SPELL_SUMMON_FREED_MIST_WHISPER_SCOUT, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_rivenwood_captives_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum TheCleansing
{
    SPELL_CLEANSING_SOUL            = 43351,
    SPELL_SUMMON_INNER_TURMOIL      = 50167,
    SPELL_RECENT_MEDITATION         = 61720,
    SPELL_MIRROR_IMAGE_AURA         = 50218,

    QUEST_THE_CLEANSING_H           = 11317,
    QUEST_THE_CLEANSING_A           = 11322
};

class spell_fjord_the_cleansing_shrine_cast : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_RECENT_MEDITATION, SPELL_CLEANSING_SOUL }) &&
            sObjectMgr->GetQuestTemplate(QUEST_THE_CLEANSING_H) &&
            sObjectMgr->GetQuestTemplate(QUEST_THE_CLEANSING_A);
    }

    SpellCastResult CheckCast()
    {
        // Error is correct for quest check but may be not correct for aura and this may be a wrong place to send error
        if (Player* target = GetExplTargetUnit()->ToPlayer())
        {
            if (target->HasAura(SPELL_RECENT_MEDITATION) || (!(target->GetQuestStatus(QUEST_THE_CLEANSING_H) == QUEST_STATUS_INCOMPLETE ||
                target->GetQuestStatus(QUEST_THE_CLEANSING_A) == QUEST_STATUS_INCOMPLETE)))
            {
                Spell::SendCastResult(target, GetSpellInfo(), GetSpell()->m_SpellVisual, GetSpell()->m_castId, SPELL_FAILED_FIZZLE);
                return SPELL_FAILED_FIZZLE;
            }
        }
        return SPELL_CAST_OK;
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_CLEANSING_SOUL, true);
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_fjord_the_cleansing_shrine_cast::CheckCast);
        OnEffectHitTarget.Register(&spell_fjord_the_cleansing_shrine_cast::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_fjord_the_cleansing_cleansing_soul : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_INNER_TURMOIL, SPELL_RECENT_MEDITATION });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->SetStandState(UNIT_STAND_STATE_SIT);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->SetStandState(UNIT_STAND_STATE_STAND);
        target->CastSpell(target, SPELL_SUMMON_INNER_TURMOIL, true);
        target->CastSpell(target, SPELL_RECENT_MEDITATION, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_fjord_the_cleansing_cleansing_soul::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_fjord_the_cleansing_cleansing_soul::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_fjord_the_cleansing_mirror_image_script_effect : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MIRROR_IMAGE_AURA });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_MIRROR_IMAGE_AURA);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_the_cleansing_mirror_image_script_effect::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_fjord_the_cleansing_on_death_cast_on_master : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (TempSummon* casterSummon = GetCaster()->ToTempSummon())
            if (Unit* summoner = casterSummon->GetSummoner())
                summoner->CastSpell(summoner, uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_the_cleansing_on_death_cast_on_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheWayToHisHeart
{
    SPELL_CREATE_TASTY_REEF_FISH   = 12602,
    SPELL_FISHED_UP_REEF_SHARK     = 20713
};

class spell_fjord_the_way_to_his_heart_anuniaq_net : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_TASTY_REEF_FISH, SPELL_FISHED_UP_REEF_SHARK });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, roll_chance_i(75) ? SPELL_CREATE_TASTY_REEF_FISH : SPELL_FISHED_UP_REEF_SHARK, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_the_way_to_his_heart_anuniaq_net::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_fjord_the_way_to_his_heart_reverse_cast : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_the_way_to_his_heart_reverse_cast::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_fjord_the_way_to_his_heart_quest_complete : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (TempSummon* casterSummon = GetCaster()->ToTempSummon())
            if (Unit* summoner = casterSummon->GetSummoner())
                summoner->CastSpell(summoner, uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_the_way_to_his_heart_quest_complete::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BringDownThoseShields
{
    SPELL_SCOURGING_CRYSTAL_CONTROLLER     = 43878,
    SPELL_FORCE_SHIELD_ARCANE_PURPLE_X3    = 43874
};

class spell_fjord_scourging_crystal_controller : public SpellScript
{
    bool Validate(SpellInfo const* /*spellEntry*/) override
    {
        return ValidateSpellInfo({ SPELL_FORCE_SHIELD_ARCANE_PURPLE_X3, SPELL_SCOURGING_CRYSTAL_CONTROLLER });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (target->HasAura(SPELL_FORCE_SHIELD_ARCANE_PURPLE_X3))
                // Make sure nobody else is channeling the same target. Is it necessary?
                if (!target->HasAura(SPELL_SCOURGING_CRYSTAL_CONTROLLER))
                    GetCaster()->CastSpell(GetCaster(), SPELL_SCOURGING_CRYSTAL_CONTROLLER, GetCastItem());
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_scourging_crystal_controller::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum ApplyHeatAndStir
{
    SPELL_SPURTS_AND_SMOKE           = 38594,
    SPELL_FAILED_MIX_1               = 43376,
    SPELL_FAILED_MIX_2               = 43378,
    SPELL_FAILED_MIX_3               = 43970,
    SPELL_SUCCESSFUL_MIX             = 43377,

    NPC_GENERIC_TRIGGER_LAB          = 24042,

    SAY_CONCOCTION_1                 = 0,
    SAY_CONCOCTION_2                 = 1
};

class spell_fjord_mixing_blood : public SpellScript
{
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
            if (Creature* trigger = caster->FindNearestCreature(NPC_GENERIC_TRIGGER_LAB, 100.0f))
                trigger->AI()->DoCastSelf(SPELL_SPURTS_AND_SMOKE);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_mixing_blood::HandleEffect, EFFECT_1, SPELL_EFFECT_SEND_EVENT);
    }
};

class spell_fjord_mixing_vrykul_blood : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
        {
            uint8 chance = urand(0, 99);
            uint32 spellId = 0;

            // 90% chance of getting one out of three failure effects
            if (chance < 30)
                spellId = SPELL_FAILED_MIX_1;
            else if (chance < 60)
                spellId = SPELL_FAILED_MIX_2;
            else if (chance < 90)
                spellId = SPELL_FAILED_MIX_3;
            else // 10% chance of successful cast
                spellId = SPELL_SUCCESSFUL_MIX;

            caster->CastSpell(caster, spellId, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_fjord_mixing_vrykul_blood::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

class spell_fjord_failed_mix_concoction_1 : public SpellScript
{
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
            if (Creature* trigger = caster->FindNearestCreature(NPC_GENERIC_TRIGGER_LAB, 100.0f))
                trigger->AI()->Talk(SAY_CONCOCTION_1, caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_failed_mix_concoction_1::HandleEffect, EFFECT_1, SPELL_EFFECT_SEND_EVENT);
    }
};

class spell_fjord_failed_mix_concoction_2 : public SpellScript
{
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
            if (Creature* trigger = caster->FindNearestCreature(NPC_GENERIC_TRIGGER_LAB, 100.0f))
                trigger->AI()->Talk(SAY_CONCOCTION_2, caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_fjord_failed_mix_concoction_2::HandleEffect, EFFECT_2, SPELL_EFFECT_SEND_EVENT);
    }
};

enum TheExplorersLeagueOutpost
{
    SPELL_TAXI_TO_EXPLORERS_LEAGUE     = 44280
};

class spell_fjord_taxi_to_explorers_league_outpost : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TAXI_TO_EXPLORERS_LEAGUE });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_TAXI_TO_EXPLORERS_LEAGUE);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_fjord_taxi_to_explorers_league_outpost::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum InWorgsClothing
{
    SPELL_WORG_DISGUISE_DUMMY     = 68347,
    SPELL_WORG_DISGUISE           = 43369
};

class spell_fjord_worg_disguise : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WORG_DISGUISE_DUMMY });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_WORG_DISGUISE_DUMMY);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_WORG_DISGUISE_DUMMY);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_fjord_worg_disguise::AfterApply, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_fjord_worg_disguise::AfterRemove, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_fjord_worg_disguise_dummy : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WORG_DISGUISE });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_WORG_DISGUISE);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_fjord_worg_disguise_dummy::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum TowersOfCertainDoomHorde
{
    SPELL_BRAVES_FLARE_EFFECT    = 43106
};

class spell_fjord_braves_flare : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BRAVES_FLARE_EFFECT });
    }

    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_BRAVES_FLARE_EFFECT, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_fjord_braves_flare::HandleAfterCast);
    }
};

enum TowersOfCertainDoomAlliance
{
    SPELL_SERGEANTS_FLARE_EFFECT    = 43068
};

class spell_fjord_sergeants_flare : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SERGEANTS_FLARE_EFFECT });
    }

    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_SERGEANTS_FLARE_EFFECT, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_fjord_sergeants_flare::HandleAfterCast);
    }
};
}

void AddSC_howling_fjord_cata()
{
    using namespace howling_fjord_cata;
    RegisterSpellScript(spell_fjord_guide_our_sights_fx_master);
    RegisterSpellScript(spell_fjord_mindless_abomination_ping_master);
    RegisterSpellScript(spell_fjord_mindless_abomination_explosion_fx_master);
    RegisterSpellScript(spell_fjord_rivenwood_captives_not_on_quest);
    RegisterSpellScript(spell_fjord_rivenwood_captives_on_quest);
    RegisterSpellScript(spell_fjord_the_cleansing_shrine_cast);
    RegisterSpellScript(spell_fjord_the_cleansing_cleansing_soul);
    RegisterSpellScript(spell_fjord_the_cleansing_mirror_image_script_effect);
    RegisterSpellScript(spell_fjord_the_cleansing_on_death_cast_on_master);
    RegisterSpellScript(spell_fjord_the_way_to_his_heart_anuniaq_net);
    RegisterSpellScript(spell_fjord_the_way_to_his_heart_reverse_cast);
    RegisterSpellScript(spell_fjord_the_way_to_his_heart_quest_complete);
    RegisterSpellScript(spell_fjord_scourging_crystal_controller);
    RegisterSpellScript(spell_fjord_mixing_blood);
    RegisterSpellScript(spell_fjord_mixing_vrykul_blood);
    RegisterSpellScript(spell_fjord_failed_mix_concoction_1);
    RegisterSpellScript(spell_fjord_failed_mix_concoction_2);
    RegisterSpellScript(spell_fjord_taxi_to_explorers_league_outpost);
    RegisterSpellScript(spell_fjord_worg_disguise);
    RegisterSpellScript(spell_fjord_worg_disguise_dummy);
    RegisterSpellScript(spell_fjord_braves_flare);
    RegisterSpellScript(spell_fjord_sergeants_flare);
}
