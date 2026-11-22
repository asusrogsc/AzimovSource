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
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "Vehicle.h"
#include "GameObject.h"

namespace sholazar_basin_cata
{
enum JunglePunch
{
    SPELL_OFFER                         = 51962,
    QUEST_TASTE_TEST                    = 12645,

    SAY_HEMET_HADRIUS_TAMARA_1          = 0,
    SAY_HEMET_HADRIUS_TAMARA_2          = 1,
    SAY_HEMET_HADRIUS_TAMARA_3          = 2,

    SAY_HEMET_4                         = 3, // unused
    SAY_HEMET_5                         = 4  // unused
};

enum NesingwaryChildrensWeek
{
    SPELL_ORPHAN_OUT                    = 58818,

    QUEST_THE_MIGHTY_HEMET_NESINGWARY   = 13957,

    ORPHAN_WOLVAR                       = 33532,

    TEXT_NESINGWARY_1                   = 5,

    TEXT_WOLVAR_ORPHAN_6                = 6,
    TEXT_WOLVAR_ORPHAN_7                = 7,
    TEXT_WOLVAR_ORPHAN_8                = 8,
    TEXT_WOLVAR_ORPHAN_9                = 9
};

enum SongOfWindAndWater
{
    // Spells
    SPELL_DEVOUR_WIND     = 52862,
    SPELL_DEVOUR_WATER    = 52864,
    // NPCs
    NPC_HAIPHOON_WATER    = 28999,
    NPC_HAIPHOON_AIR      = 28985
};

enum KickWhatKick
{
    NPC_LUCKY_WILHELM         = 28054,
    NPC_APPLE                 = 28053,
    NPC_DROSTAN               = 28328,
    NPC_CRUNCHY               = 28346,
    NPC_THICKBIRD             = 28093,

    SPELL_HIT_APPLE           = 51331,
    SPELL_MISS_APPLE          = 51332,
    SPELL_MISS_BIRD_APPLE     = 51366,
    SPELL_APPLE_FALL          = 51371,
    SPELL_BIRD_FALL           = 51369,

    EVENT_MISS                = 0,
    EVENT_HIT                 = 1,
    EVENT_MISS_BIRD           = 2,

    SAY_WILHELM_MISS          = 0,
    SAY_WILHELM_HIT           = 1,
    SAY_DROSTAN_REPLY_MISS    = 0,
};

class spell_sholazar_shoot_rjr : public SpellScript
{
    SpellCastResult CheckCast()
    {
        if (Unit* target = GetExplTargetUnit())
            if (target->GetEntry() == NPC_LUCKY_WILHELM)
                return SPELL_CAST_OK;

        SetCustomCastResultMessage(SPELL_CUSTOM_ERROR_MUST_TARGET_WILHELM);
        return SPELL_FAILED_CUSTOM_ERROR;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        uint32 roll = urand(1, 100);

        uint8 ev;
        if (roll <= 50)
            ev = EVENT_MISS;
        else if (roll <= 83)
            ev = EVENT_HIT;
        else
            ev = EVENT_MISS_BIRD;

        Unit* shooter = GetCaster();
        Creature* wilhelm = GetHitUnit()->ToCreature();
        Creature* apple = shooter->FindNearestCreature(NPC_APPLE, 30);
        Creature* drostan = shooter->FindNearestCreature(NPC_DROSTAN, 30);

        if (!wilhelm || !apple || !drostan)
            return;

        switch (ev)
        {
            case EVENT_MISS_BIRD:
            {
                Creature* crunchy = shooter->FindNearestCreature(NPC_CRUNCHY, 30);
                Creature* bird = shooter->FindNearestCreature(NPC_THICKBIRD, 30);

                if (bird && crunchy)
                {
                    shooter->CastSpell(bird, SPELL_MISS_BIRD_APPLE);
                    bird->CastSpell(bird, SPELL_BIRD_FALL);
                    wilhelm->AI()->Talk(SAY_WILHELM_MISS);
                    drostan->AI()->Talk(SAY_DROSTAN_REPLY_MISS);

                    bird->KillSelf();
                    crunchy->GetMotionMaster()->MovePoint(0, bird->GetPositionX(), bird->GetPositionY(),
                        bird->GetMap()->GetWaterOrGroundLevel(bird->GetPhaseShift(), bird->GetPositionX(), bird->GetPositionY(), bird->GetPositionZ()));
                    /// @todo Make crunchy perform emote eat when he reaches the bird
                    break;
                }
                [[fallthrough]];
            }
            case EVENT_MISS:
            {
                shooter->CastSpell(wilhelm, SPELL_MISS_APPLE);
                wilhelm->AI()->Talk(SAY_WILHELM_MISS);
                drostan->AI()->Talk(SAY_DROSTAN_REPLY_MISS);
                break;
            }
            case EVENT_HIT:
            {
                shooter->CastSpell(apple, SPELL_HIT_APPLE);
                apple->CastSpell(apple, SPELL_APPLE_FALL);
                wilhelm->AI()->Talk(SAY_WILHELM_HIT);
                if (Player* player = shooter->ToPlayer())
                    player->KilledMonsterCredit(NPC_APPLE);
                break;
            }
        }
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_sholazar_shoot_rjr::CheckCast);
        OnEffectHitTarget.Register(&spell_sholazar_shoot_rjr::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum TheLifewardensWrath
{
    NPC_FREYAS_PRESENCE          = 28563,

    WHISPER_ACTIVATE             = 0,

    SPELL_FREYA_DUMMY_TRIGGER    = 51335,
    SPELL_LASHER_EMERGE          = 48195,
    SPELL_WILD_GROWTH            = 52948
};

class spell_sholazar_call_of_the_lifewarden : public SpellScript
{
    void HandleSendEvent(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        if (Creature* presence = GetCaster()->FindNearestCreature(NPC_FREYAS_PRESENCE, 50.0f))
        {
            presence->AI()->Talk(WHISPER_ACTIVATE, GetCaster());
            presence->AI()->SetData(1, 1);
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_sholazar_call_of_the_lifewarden::HandleSendEvent, EFFECT_0, SPELL_EFFECT_SEND_EVENT);
    }
};

class spell_sholazar_freya_dummy : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_FREYA_DUMMY_TRIGGER,
            SPELL_LASHER_EMERGE,
            SPELL_WILD_GROWTH
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();

        target->CastSpell(target, SPELL_FREYA_DUMMY_TRIGGER, true);
        target->CastSpell(target, SPELL_LASHER_EMERGE, false);
        target->CastSpell(target, SPELL_WILD_GROWTH, false);
        // This probably is scripted by the Emerge spell from above
        target->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        target->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        target->SetImmuneToNPC(false);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_freya_dummy::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ReturnedSevenfold
{
    SPELL_FREYAS_WARD           = 51845,
    SPELL_SEVENFOLD_RETRIBUTION = 51856,
    SPELL_DEATHBOLT             = 51855
};

class spell_sholazar_deathbolt : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_FREYAS_WARD,
            SPELL_SEVENFOLD_RETRIBUTION,
            SPELL_DEATHBOLT
        });
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        if (target->HasAura(SPELL_FREYAS_WARD))
            target->CastSpell(caster, SPELL_SEVENFOLD_RETRIBUTION, true);
        else
            caster->CastSpell(target, SPELL_DEATHBOLT, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_deathbolt::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BurningToHelp
{
    SPELL_HYDRA_SPUTUM     = 52307
};

class spell_sholazar_take_sputum_sample : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return true && ValidateSpellInfo(
        {
            uint32(spellInfo->Effects[EFFECT_0].CalcValue()),
            uint32(spellInfo->Effects[EFFECT_1].CalcValue())
        });
    }

    SpellCastResult CheckCast()
    {
        if (!GetCaster()->HasAura(uint32(GetSpellInfo()->Effects[EFFECT_1].CalcValue())))
            return SPELL_FAILED_CASTER_AURASTATE;

        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_sholazar_take_sputum_sample::CheckCast);
        OnEffectHit.Register(&spell_sholazar_take_sputum_sample::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_sholazar_sputum_collected : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HYDRA_SPUTUM });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_HYDRA_SPUTUM);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_sholazar_sputum_collected::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ACleansingSong
{
    SPELL_SUMMON_SPIRIT_ATHA        = 52954,
    SPELL_SUMMON_SPIRIT_HAKHALAN    = 52958,
    SPELL_SUMMON_SPIRIT_KOOSU       = 52959,

    AREA_BITTERTIDE_LAKE            = 4385,
    AREA_RIVERS_HEART               = 4290,
    AREA_WINTERGRASP_RIVER          = 4388
};

class spell_sholazar_song_of_cleansing : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_SPIRIT_ATHA,
            SPELL_SUMMON_SPIRIT_HAKHALAN,
            SPELL_SUMMON_SPIRIT_KOOSU
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        switch (caster->GetAreaId())
        {
            case AREA_BITTERTIDE_LAKE:
                caster->CastSpell(caster, SPELL_SUMMON_SPIRIT_ATHA);
                break;
            case AREA_RIVERS_HEART:
                caster->CastSpell(caster, SPELL_SUMMON_SPIRIT_HAKHALAN);
                break;
            case AREA_WINTERGRASP_RIVER:
                caster->CastSpell(caster, SPELL_SUMMON_SPIRIT_KOOSU);
                break;
            default:
                break;
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_sholazar_song_of_cleansing::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum StrengthOfTheTempest
{
    SPELL_CREATE_POWER_OF_THE_TEMPEST   = 53067
};

class spell_sholazar_lightning_strike : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_POWER_OF_THE_TEMPEST });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_CREATE_POWER_OF_THE_TEMPEST);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_lightning_strike::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_sholazar_flight_to_sholazar : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return sObjectMgr->GetBroadcastText(uint32(spellInfo->Effects[EFFECT_0].CalcValue())) != nullptr;
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        // 51076 (triggers 51071) is Area Aura - Party and applies on player too (no aura in sniffs on player)
        // That makes both player and creature say
        if (Creature* caster = GetCaster()->ToCreature())
            caster->Unit::Say(uint32(GetEffectValue()), caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_sholazar_flight_to_sholazar::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum SomeMakeLemonadeSomeMakeLiquor
{
    SPELL_BANANAS_FALL_TO_GROUND    = 51836,
    SPELL_ORANGE_FALLS_TO_GROUND    = 51837,
    SPELL_PAPAYA_FALLS_TO_GROUND    = 51839,
    SPELL_TRIGGER_SPAWN_DWARF       = 52078
};

class spell_sholazar_despawn_fruit_tosser : public SpellScript
{
    bool Validate(SpellInfo const* /*spellEntry*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BANANAS_FALL_TO_GROUND,
            SPELL_ORANGE_FALLS_TO_GROUND,
            SPELL_PAPAYA_FALLS_TO_GROUND,
            SPELL_TRIGGER_SPAWN_DWARF
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        uint32 spellId = SPELL_BANANAS_FALL_TO_GROUND;
        switch (urand(0, 3))
        {
            case 1: spellId = SPELL_ORANGE_FALLS_TO_GROUND; break;
            case 2: spellId = SPELL_PAPAYA_FALLS_TO_GROUND; break;
        }
        // sometimes, if you're lucky, you get a dwarf
        if (roll_chance_i(5))
            spellId = SPELL_TRIGGER_SPAWN_DWARF;

        GetHitUnit()->CastSpell(GetCaster(), spellId);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_despawn_fruit_tosser::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum SalvagingLifesStength
{
    NPC_SHARD_KILL_CREDIT       = 29303
};

class spell_sholazar_lifeblood_dummy : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) }) &&
            sObjectMgr->GetCreatureTemplate(NPC_SHARD_KILL_CREDIT);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (GetHitUnit()->HasAura(uint32(GetEffectValue())))
        {
            if (Player* caster = GetCaster()->ToPlayer())
                caster->KilledMonsterCredit(NPC_SHARD_KILL_CREDIT);

            GetHitUnit()->RemoveAurasDueToSpell(uint32(GetEffectValue()));
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_lifeblood_dummy::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum AMammothUndertaking
{
    SPELL_RIDE_SHATTERTUSK_MAMMOTH     = 51658
};

class spell_sholazar_mount_mammoth_cue : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_RIDE_SHATTERTUSK_MAMMOTH });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), SPELL_RIDE_SHATTERTUSK_MAMMOTH, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_sholazar_mount_mammoth_cue::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};
}

void AddSC_sholazar_basin_cata()
{
    using namespace sholazar_basin_cata;
    RegisterSpellScript(spell_sholazar_shoot_rjr);
    RegisterSpellScript(spell_sholazar_call_of_the_lifewarden);
    RegisterSpellScript(spell_sholazar_freya_dummy);
    RegisterSpellScript(spell_sholazar_deathbolt);
    RegisterSpellScript(spell_sholazar_take_sputum_sample);
    RegisterSpellScript(spell_sholazar_sputum_collected);
    RegisterSpellScript(spell_sholazar_song_of_cleansing);
    RegisterSpellScript(spell_sholazar_lightning_strike);
    RegisterSpellScript(spell_sholazar_flight_to_sholazar);
    RegisterSpellScript(spell_sholazar_despawn_fruit_tosser);
    RegisterSpellScript(spell_sholazar_lifeblood_dummy);
    RegisterSpellScript(spell_sholazar_mount_mammoth_cue);
}
