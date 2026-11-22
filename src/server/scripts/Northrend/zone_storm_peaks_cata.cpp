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
#include "CombatAI.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "WorldSession.h"

namespace storm_peaks_cata
{
enum BrunnhildarPrisoner
{
    SPELL_ICE_PRISON           = 54894,
    SPELL_ICE_LANCE            = 55046,
    SPELL_FREE_PRISONER        = 55048,
    SPELL_RIDE_DRAKE           = 55074,
    SPELL_SHARD_IMPACT         = 55047
};

class spell_storm_peaks_free_brunnhildar_prisoner : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ICE_PRISON });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_ICE_PRISON);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_storm_peaks_free_brunnhildar_prisoner::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum FreedProtoDrake
{
    NPC_DRAKE                           = 29709,

    AREA_VALLEY_OF_ANCIENT_WINTERS      = 4437,

    TEXT_EMOTE                          = 0,

    SPELL_KILL_CREDIT_PRISONER          = 55144,
    SPELL_SUMMON_LIBERATED              = 55073,
    SPELL_KILL_CREDIT_DRAKE             = 55143,

    EVENT_CHECK_AREA                    = 1,
    EVENT_REACHED_HOME                  = 2,
};

static constexpr uint32 PATH_ESCORT_ICEFANG = 236818;

enum NPCs
{
    NPC_HYLDSMEET_DRAKERIDER = 29694
};

enum BrannBronzebeard
{
    NPC_BRANN_BRONZEBEARD = 31810,
    NPC_A_DISTANT_VOICE   = 31814,
    OBJECT_TOL_SIGNAL_1   = 193590,
    OBJECT_TOL_SIGNAL_2   = 193591,
    OBJECT_TOL_SIGNAL_3   = 193592,
    OBJECT_TOL_SIGNAL_4   = 193593,
    OBJECT_TOL_SIGNAL_5   = 193594,
    SPELL_RESURRECTION    = 58854,
    SAY_BRANN_1           = 0,
    SAY_BRANN_2           = 1,
    SAY_BRANN_3           = 2,
    SAY_VOICE_1           = 0,
    SAY_VOICE_2           = 1,
    SAY_VOICE_3           = 2,
    SAY_VOICE_4           = 3,
    SAY_VOICE_5           = 4,

    EVENT_SCRIPT_1        = 3,
    EVENT_SCRIPT_2        = 4,
    EVENT_SCRIPT_3        = 5,
    EVENT_SCRIPT_4        = 6,
    EVENT_SCRIPT_5        = 7,
    EVENT_SCRIPT_6        = 8,
    EVENT_SCRIPT_7        = 9,
    EVENT_SCRIPT_8        = 10,
    EVENT_SCRIPT_9        = 11,
    EVENT_SCRIPT_10       = 12,
    EVENT_SCRIPT_11       = 13,
    EVENT_SCRIPT_12       = 14,
    EVENT_SCRIPT_13       = 15
};

enum WildWyrm
{
    PATH_WILD_WYRM                      = (30275 * 10) << 3,

    // Phase 1
    SPELL_PLAYER_MOUNT_WYRM             = 56672,
    SPELL_FIGHT_WYRM                    = 56673,
    SPELL_SPEAR_OF_HODIR                = 56671,
    SPELL_GRIP                          = 56689,
    SPELL_GRAB_ON                       = 60533,
    SPELL_DODGE_CLAWS                   = 56704,
    SPELL_THRUST_SPEAR                  = 56690,
    SPELL_MIGHTY_SPEAR_THRUST           = 60586,
    SPELL_CLAW_SWIPE_PERIODIC           = 60689,
    SPELL_CLAW_SWIPE_DAMAGE             = 60776,
    SPELL_FULL_HEAL_MANA                = 32432,
    SPELL_LOW_HEALTH_TRIGGER            = 60596,

    // Phase 2
    SPELL_EJECT_PASSENGER_1             = 60603,
    SPELL_PRY_JAWS_OPEN                 = 56706,
    SPELL_FATAL_STRIKE                  = 60587,
    SPELL_FATAL_STRIKE_DAMAGE           = 60881,
    SPELL_JAWS_OF_DEATH_PERIODIC        = 56692,
    SPELL_FLY_STATE_VISUAL              = 60865,

    // Dead phase
    SPELL_WYRM_KILL_CREDIT              = 56703,
    SPELL_FALLING_DRAGON_FEIGN_DEATH    = 55795,
    SPELL_EJECT_ALL_PASSENGERS          = 50630,

    SAY_SWIPE                           = 0,
    SAY_DODGED                          = 1,
    SAY_PHASE_2                         = 2,
    SAY_GRIP_WARN                       = 3,
    SAY_STRIKE_MISS                     = 4,

    ACTION_CLAW_SWIPE_WARN              = 1,
    ACTION_CLAW_SWIPE_DODGE             = 2,
    ACTION_GRIP_FAILING                 = 3,
    ACTION_GRIP_LOST                    = 4,
    ACTION_FATAL_STRIKE_MISS            = 5,

    POINT_START_FIGHT                   = 1,
    POINT_FALL                          = 2,

    SEAT_INITIAL                        = 0,
    SEAT_MOUTH                          = 1,

    PHASE_INITIAL                       = 0,
    PHASE_MOUTH                         = 1,
    PHASE_DEAD                          = 2,
    PHASE_MAX                           = 3
};

uint32 const WyrmControlSpells[PHASE_MAX][ControllableSpellsCount] =
{
    { SPELL_GRAB_ON,       SPELL_DODGE_CLAWS, SPELL_THRUST_SPEAR, SPELL_MIGHTY_SPEAR_THRUST },
    { SPELL_PRY_JAWS_OPEN, 0,                 SPELL_FATAL_STRIKE, 0                         },
    { 0,                   0,                 0,                  0                         }
};

enum JokkumScriptcast
{
    NPC_KINGJOKKUM                   = 30331,
    NPC_THORIM                       = 30390,
    PATH_JOKKUM                      = 16577600,
    PATH_JOKKUM_END                  = 16577608,
    SAY_HOLD_ON                      = 0,
    SAY_JOKKUM_1                     = 1,
    SAY_JOKKUM_2                     = 2,
    SAY_JOKKUM_3                     = 3,
    SAY_JOKKUM_4                     = 4,
    SAY_JOKKUM_5                     = 5,
    SAY_JOKKUM_6                     = 6,
    SAY_JOKKUM_7                     = 7,
    SAY_JOKKUM_8                     = 8,
    SAY_THORIM_1                     = 0,
    SAY_THORIM_2                     = 1,
    SAY_THORIM_3                     = 2,
    SAY_THORIM_4                     = 3,
    SPELL_JOKKUM_SUMMON              = 56541,
    SPELL_JOKKUM_KILL_CREDIT         = 56545,
    SPELL_PLAYER_CAST_VERANUS_SUMMON = 56650,
    SPELL_SUMMON_VERANUS_AND_THORIM  = 56649,
    EVENT_KROLMIR_1                  = 16,
    EVENT_KROLMIR_2                  = 17,
    EVENT_KROLMIR_3                  = 18,
    EVENT_KROLMIR_4                  = 19,
    EVENT_KROLMIR_5                  = 20,
    EVENT_KROLMIR_6                  = 21,
    EVENT_KROLMIR_7                  = 22,
    EVENT_KROLMIR_8                  = 23,
    EVENT_KROLMIR_9                  = 24,
};



enum CloseRift
{
    SPELL_DESPAWN_RIFT          = 61665
};

enum MountingHodirsHelm
{
    TEXT_PRONOUNCEMENT_1     = 30906,
    TEXT_PRONOUNCEMENT_2     = 30907,
    NPC_HODIRS_HELM_KC       = 30210
};

class spell_storm_peaks_read_pronouncement : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sBroadcastTextStore.HasRecord(TEXT_PRONOUNCEMENT_1) &&
            sBroadcastTextStore.HasRecord(TEXT_PRONOUNCEMENT_2) &&
            sObjectMgr->GetCreatureTemplate(NPC_HODIRS_HELM_KC);
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
        {
            target->Unit::Whisper(TEXT_PRONOUNCEMENT_1, target, true);
            target->KilledMonsterCredit(NPC_HODIRS_HELM_KC);
            target->Unit::Whisper(TEXT_PRONOUNCEMENT_2, target, true);
        }
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_storm_peaks_read_pronouncement::OnApply, EFFECT_0, SPELL_AURA_NONE, AURA_EFFECT_HANDLE_REAL);
    }
};

enum JormuttarIsSooFat
{
    SPELL_CREATE_BEAR_FLANK    = 56566,
    SPELL_BEAR_FLANK_FAIL      = 56569,
    TEXT_CARVE_FAIL            = 30986
};

class spell_storm_peaks_bear_flank_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_BEAR_FLANK, SPELL_BEAR_FLANK_FAIL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), roll_chance_i(20) ? SPELL_CREATE_BEAR_FLANK : SPELL_BEAR_FLANK_FAIL);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_storm_peaks_bear_flank_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_storm_peaks_bear_flank_fail : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sBroadcastTextStore.HasRecord(TEXT_CARVE_FAIL);
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
            target->Unit::Whisper(TEXT_CARVE_FAIL, target, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_storm_peaks_bear_flank_fail::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum AmpleInspiration
{
    SPELL_QUIET_SUICIDE              = 3617,
    SPELL_SUMMON_MAIN_MAMMOTH_MEAT   = 57444,
    SPELL_MAMMOTH_SUMMON_OBJECT_1    = 54627,
    SPELL_MAMMOTH_SUMMON_OBJECT_2    = 54628,
    SPELL_MAMMOTH_SUMMON_OBJECT_3    = 54623,

    ITEM_EXPLOSIVE_DEVICE            = 40686
};

class spell_storm_peaks_mammoth_explosion_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_QUIET_SUICIDE,
            SPELL_SUMMON_MAIN_MAMMOTH_MEAT,
            SPELL_MAMMOTH_SUMMON_OBJECT_1,
            SPELL_MAMMOTH_SUMMON_OBJECT_2,
            SPELL_MAMMOTH_SUMMON_OBJECT_3
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_QUIET_SUICIDE);
        caster->CastSpell(caster, SPELL_SUMMON_MAIN_MAMMOTH_MEAT);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_3);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_3);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_storm_peaks_mammoth_explosion_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_storm_peaks_unstable_explosive_detonation : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sObjectMgr->GetItemTemplate(ITEM_EXPLOSIVE_DEVICE);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            caster->DestroyItemCount(ITEM_EXPLOSIVE_DEVICE, 1, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_storm_peaks_unstable_explosive_detonation::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum MendingFences
{
    SPELL_SUMMON_EARTHEN    = 55528
};

class spell_storm_peaks_call_of_earth : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_EARTHEN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        uint8 count = urand(2, 6);
        for (uint8 i = 0; i < count; i++)
            GetCaster()->CastSpell(GetCaster(), SPELL_SUMMON_EARTHEN, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_storm_peaks_call_of_earth::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BearlyHangingOn
{
    NPC_FROSTGIANT             = 29351,
    NPC_FROSTWORG              = 29358,
    SPELL_FROSTGIANT_CREDIT    = 58184,
    SPELL_FROSTWORG_CREDIT     = 58183,
    SPELL_IMMOLATION           = 54690,
    SPELL_ABLAZE               = 54683
};

class spell_storm_peaks_flaming_arrow_triggered_effect : public AuraScript
{
    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
        {
            Unit* target = GetTarget();
            // Already in fire
            if (target->HasAura(SPELL_ABLAZE))
                return;

            if (Player* player = caster->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                switch (target->GetEntry())
                {
                    case NPC_FROSTWORG:
                        target->CastSpell(player, SPELL_FROSTWORG_CREDIT, true);
                        target->CastSpell(target, SPELL_IMMOLATION, true);
                        target->CastSpell(target, SPELL_ABLAZE, true);
                        break;
                    case NPC_FROSTGIANT:
                        target->CastSpell(player, SPELL_FROSTGIANT_CREDIT, true);
                        target->CastSpell(target, SPELL_IMMOLATION, true);
                        target->CastSpell(target, SPELL_ABLAZE, true);
                        break;
                }
            }
        }
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_storm_peaks_flaming_arrow_triggered_effect::HandleEffectApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

enum CatchingUpWithBrann
{
    SPELL_DESPAWN_BRANN    = 61121,
    SPELL_CONTACT_BRANN    = 55038
};

class spell_storm_peaks_contact_brann : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DESPAWN_BRANN, SPELL_CONTACT_BRANN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_DESPAWN_BRANN);
        caster->CastSpell(caster, SPELL_CONTACT_BRANN);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_storm_peaks_contact_brann::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};
}

void AddSC_storm_peaks_cata()
{
    using namespace storm_peaks_cata;
    RegisterSpellScript(spell_storm_peaks_free_brunnhildar_prisoner);
    RegisterSpellScript(spell_storm_peaks_read_pronouncement);
    RegisterSpellScript(spell_storm_peaks_bear_flank_master);
    RegisterSpellScript(spell_storm_peaks_bear_flank_fail);
    RegisterSpellScript(spell_storm_peaks_mammoth_explosion_master);
    RegisterSpellScript(spell_storm_peaks_unstable_explosive_detonation);
    RegisterSpellScript(spell_storm_peaks_call_of_earth);
    RegisterSpellScript(spell_storm_peaks_flaming_arrow_triggered_effect);
    RegisterSpellScript(spell_storm_peaks_contact_brann);
}
