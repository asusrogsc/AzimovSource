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
 * Spell and creature scripts in this file are not ordered but grouped(all scripts related to same creature in same place).
 * Scriptnames of spells and creatures in this file should be prefixed with "spell_pet_gen_" and "npc_pet_gen_" respectively.
 */

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "PetDefines.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace pet_generic_cata
{
enum PandarenMonkMisc
{
    SPELL_PANDAREN_MONK = 69800,
    EVENT_FOCUS = 1,
    EVENT_EMOTE = 2,
    EVENT_FOLLOW = 3,
    EVENT_DRINK = 4
};

enum SoulTrader
{
    SAY_SOUL_TRADER_INTRO           = 0,

    SPELL_ETHEREAL_ONSUMMON         = 50052,
    SPELL_ETHEREAL_PET_REMOVE_AURA  = 50055
};

enum LichPet
{
    SPELL_LICH_PET_AURA         = 69732,
    SPELL_LICH_PET_AURA_ONKILL  = 69731,
    SPELL_LICH_PET_EMOTE        = 70049,

    NPC_LICH_PET                = 36979
};

class spell_pet_gen_lich_pet_onsummon : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LICH_PET_AURA });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->CastSpell(target, SPELL_LICH_PET_AURA, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_pet_gen_lich_pet_onsummon::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_pet_gen_lich_pet_aura_remove : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LICH_PET_AURA });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_LICH_PET_AURA);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_pet_gen_lich_pet_aura_remove::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_pet_gen_lich_pet_aura : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LICH_PET_AURA_ONKILL });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        return eventInfo.GetActionTarget()->IsPlayer();
    }

    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* owner = GetUnitOwner();

        std::list<TempSummon*> minionList;
        owner->GetAllMinionsByEntry(minionList, NPC_LICH_PET);
        for (TempSummon* minion : minionList)
            owner->CastSpell(minion, SPELL_LICH_PET_AURA_ONKILL, true);
    }

    void Register() override
    {
        DoCheckProc.Register(&spell_pet_gen_lich_pet_aura::CheckProc);
        OnEffectProc.Register(&spell_pet_gen_lich_pet_aura::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

class spell_pet_gen_lich_pet_periodic_emote : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LICH_PET_EMOTE });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        // The chance to cast this spell is not 100%.
        // Triggered spell roots creature for 3 sec and plays anim and sound (doesn't require any script).
        // Emote and sound never shows up in sniffs because both comes from spell visual directly.
        // Both 69683 and 70050 can trigger spells at once and are not linked together in any way.
        // Effect of 70050 is overlapped by effect of 69683 but not instantly (69683 is a series of spell casts, takes longer to execute).
        // However, for some reason emote is not played if creature is idle and only if creature is moving or is already rooted.
        // For now it's scripted manually in script below to play emote always.
        if (roll_chance_i(50))
            GetTarget()->CastSpell(GetTarget(), SPELL_LICH_PET_EMOTE, true);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_pet_gen_lich_pet_periodic_emote::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

class spell_pet_gen_lich_pet_emote : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->HandleEmoteCommand(EMOTE_ONESHOT_CUSTOM_SPELL_01);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_pet_gen_lich_pet_emote::AfterApply, EFFECT_0, SPELL_AURA_MOD_ROOT, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_pet_gen_lich_pet_focus : public SpellScript
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
        OnEffectHitTarget.Register(&spell_pet_gen_lich_pet_focus::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_pet_gen_toxic_wasteling_find_target : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_pet_gen_toxic_wasteling_find_target::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_pet_gen_toxic_wasteling_devour : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Creature* target = GetHitCreature())
            target->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_pet_gen_toxic_wasteling_devour::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ElwynnLambMisc
{
    // Spells
    SPELL_SLEEPING_SLEEP        = 32951,
    SPELL_SUICIDE               = 45254,
    SPELL_ELWYNN_FOREST_WOLF    = 62701,

    // Sound
    SOUND_WOLF_HOWL             = 9036,
};

struct npc_elwynn_forest_wolf : public NullCreatureAI
{
    npc_elwynn_forest_wolf(Creature* creature) : NullCreatureAI(creature), _chasing(false) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (!summoner->IsCreature())
            return;

        _summonerGUID = summoner->GetGUID();
        _ScheduleBeforeChasingEvents();
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == CHASE_MOTION_TYPE && id == _summonerGUID.GetCounter())
            _ScheduleAfterChasingEvents();
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);

        if (_chasing && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            _ScheduleAfterChasingEvents();
    }

private:
    void _ScheduleBeforeChasingEvents()
    {
        _scheduler.Schedule(1s, [this](TaskContext /*context*/)
        {
            me->PlayDistanceSound(SOUND_WOLF_HOWL);
            me->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
        })
        .Schedule(4s, [this](TaskContext /*context*/)
        {
            if (Creature* summoner = ObjectAccessor::GetCreature(*me, _summonerGUID))
                if (me->Attack(summoner, false))
                    me->GetMotionMaster()->MoveChase(summoner);

            _chasing = true;
        });
    }

    void _ScheduleAfterChasingEvents()
    {
        _chasing = false;
        me->GetMotionMaster()->Clear();

        _scheduler.Schedule(2s, [this](TaskContext /*context*/)
        {
            DoCastAOE(SPELL_ELWYNN_FOREST_WOLF);
        })
        .Schedule(4s, [this](TaskContext /*context*/)
        {
            DoCastSelf(SPELL_SLEEPING_SLEEP);
            me->DespawnOrUnsummon(7s);
        });
    }

    ObjectGuid _summonerGUID;
    bool _chasing;
    TaskScheduler _scheduler;
};

class spell_gen_elwynn_forest_wolf : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUICIDE });
    }

    void HandleDummy(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (Creature* target = GetHitCreature())
        {
            target->CastSpell(target, SPELL_SUICIDE, true);
            target->DespawnOrUnsummon(4s);
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_elwynn_forest_wolf::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_gen_elwynn_lamb : public AuraScript
{
    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        // Based on WotLK Classic sniffs (3.4.3 52237).
        if (!GetTarget()->IsOutdoors() || !roll_chance_i(5))
            PreventDefaultAction();
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_gen_elwynn_lamb::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};
}

void AddSC_pet_generic_cata()
{
    using namespace pet_generic_cata;
    RegisterSpellScript(spell_pet_gen_lich_pet_onsummon);
    RegisterSpellScript(spell_pet_gen_lich_pet_aura_remove);
    RegisterSpellScript(spell_pet_gen_lich_pet_aura);
    RegisterSpellScript(spell_pet_gen_lich_pet_periodic_emote);
    RegisterSpellScript(spell_pet_gen_lich_pet_emote);
    RegisterSpellScript(spell_pet_gen_lich_pet_focus);
    RegisterSpellScript(spell_pet_gen_toxic_wasteling_find_target);
    RegisterSpellScript(spell_pet_gen_toxic_wasteling_devour);
    RegisterCreatureAI(npc_elwynn_forest_wolf);
    RegisterSpellScript(spell_gen_elwynn_forest_wolf);
    RegisterSpellScript(spell_gen_elwynn_lamb);
}
