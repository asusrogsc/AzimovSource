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
#include "CreatureAIImpl.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

namespace terokkar_forest_cata
{
enum UnkorTheRuthless
{
    SAY_SUBMIT              = 0,
    REQUIRED_KILL_COUNT     = 10,
    SPELL_PULVERIZE         = 2676,
    QUEST_DONTKILLTHEFATONE = 9889,
    NPC_BOULDERFIST_INVADER = 18260
};

class spell_skyguard_flare : public SpellScript
{
    void ModDestHeight(SpellDestination& dest)
    {
        dest._position.m_positionZ = GetCaster()->GetMap()->GetHeight(GetCaster()->GetPhaseShift(), dest._position.GetPositionX(), dest._position.GetPositionY(), MAX_HEIGHT);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_skyguard_flare::ModDestHeight, EFFECT_0, TARGET_DEST_TARGET_RANDOM);
    }
};

enum TakenInTheNight
{
    SPELL_FREE_WEBBED_1      = 38953,
    SPELL_FREE_WEBBED_2      = 38955,
    SPELL_FREE_WEBBED_3      = 38956,
    SPELL_FREE_WEBBED_4      = 38957,
    SPELL_FREE_WEBBED_5      = 38958,
    SPELL_FREE_WEBBED_6      = 38978
};

std::array<uint32, 5> const CocoonSummonSpells =
{
    SPELL_FREE_WEBBED_1, SPELL_FREE_WEBBED_2, SPELL_FREE_WEBBED_3, SPELL_FREE_WEBBED_4, SPELL_FREE_WEBBED_5
};

class spell_terokkar_free_webbed : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_terokkar_free_webbed::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_terokkar_free_webbed_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREE_WEBBED_6 });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        if (roll_chance_i(66))
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            target->CastSpell(caster, SPELL_FREE_WEBBED_6, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_terokkar_free_webbed_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum WhoAreThey
{
    SPELL_SHADOWY_DISGUISE          = 32756,
    SPELL_MALE_SHADOWY_DISGUISE     = 38080,
    SPELL_FEMALE_SHADOWY_DISGUISE   = 38081
};

class spell_terokkar_shadowy_disguise_cast_from_questgiver : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOWY_DISGUISE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_terokkar_shadowy_disguise_cast_from_questgiver::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_terokkar_shadowy_disguise : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MALE_SHADOWY_DISGUISE, SPELL_FEMALE_SHADOWY_DISGUISE });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
            target->CastSpell(target, target->getGender() == GENDER_MALE ? SPELL_MALE_SHADOWY_DISGUISE : SPELL_FEMALE_SHADOWY_DISGUISE);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAurasDueToSpell(SPELL_MALE_SHADOWY_DISGUISE);
        target->RemoveAurasDueToSpell(SPELL_FEMALE_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_terokkar_shadowy_disguise::AfterApply, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_terokkar_shadowy_disguise::AfterRemove, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_terokkar_cancel_shadowy_disguise : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOWY_DISGUISE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_terokkar_cancel_shadowy_disguise::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Translocation
{
    SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_DOWN     = 32572,
    SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_UP       = 32568,
    SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_DOWN        = 32569,
    SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_UP          = 32571
};

class spell_terokkar_translocation_firewing_point : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), _triggeredSpellId);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_terokkar_translocation_firewing_point::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }

    uint32 _triggeredSpellId;

public:
    explicit spell_terokkar_translocation_firewing_point(Translocation triggeredSpellId) : _triggeredSpellId(triggeredSpellId) { }
};

enum Fumping
{
    SPELL_SUMMON_SAND_GNOME    = 39240,
    SPELL_SUMMON_BONE_SLICER   = 39241,

    SPELL_SUMMON_SAND_GNOME_1  = 39247,
    SPELL_SUMMON_BONE_SLICER_1 = 39245,
    SPELL_SUMMON_HAISHULUD     = 39248,
    SPELL_DESPAWN_CLEFTHOOF    = 39250
};

class spell_terokkar_fumping : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_SAND_GNOME, SPELL_SUMMON_BONE_SLICER });
    }

    void SetDest(SpellDestination& dest)
    {
        Position const offset = { 0.5f, 0.5f, 5.0f, 0.0f };
        dest.RelocateOffset(offset);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_SAND_GNOME, SPELL_SUMMON_BONE_SLICER), true);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_terokkar_fumping::SetDest, EFFECT_1, TARGET_DEST_CASTER);
        OnEffectHit.Register(&spell_terokkar_fumping::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

class spell_terokkar_fumping_the_big_bone_worm : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_SAND_GNOME_1,
            SPELL_SUMMON_BONE_SLICER_1,
            SPELL_SUMMON_HAISHULUD,
            SPELL_DESPAWN_CLEFTHOOF
        });
    }

    void SetDest(SpellDestination& dest)
    {
        Position const offset = { 0.5f, 0.5f, 5.0f, 0.0f };
        dest.RelocateOffset(offset);
    }

    void HandleSummon(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_SAND_GNOME_1, SPELL_SUMMON_BONE_SLICER_1, SPELL_SUMMON_HAISHULUD), true);
    }

    void HandleDespawn(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_DESPAWN_CLEFTHOOF, true);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_terokkar_fumping_the_big_bone_worm::SetDest, EFFECT_1, TARGET_DEST_CASTER);
        OnEffectHit.Register(&spell_terokkar_fumping_the_big_bone_worm::HandleSummon, EFFECT_1, SPELL_EFFECT_DUMMY);
        OnEffectHit.Register(&spell_terokkar_fumping_the_big_bone_worm::HandleDespawn, EFFECT_2, SPELL_EFFECT_DUMMY);
    }
};
}

void AddSC_terokkar_forest_cata()
{
    using namespace terokkar_forest_cata;
    RegisterSpellScript(spell_skyguard_flare);
    RegisterSpellScript(spell_terokkar_free_webbed);
    RegisterSpellScript(spell_terokkar_free_webbed_on_quest);
    RegisterSpellScript(spell_terokkar_shadowy_disguise_cast_from_questgiver);
    RegisterSpellScript(spell_terokkar_shadowy_disguise);
    RegisterSpellScript(spell_terokkar_cancel_shadowy_disguise);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_building_down", SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_DOWN);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_building_up", SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_UP);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_tower_down", SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_DOWN);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_tower_up", SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_UP);
    RegisterSpellScript(spell_terokkar_fumping);
    RegisterSpellScript(spell_terokkar_fumping_the_big_bone_worm);
}
