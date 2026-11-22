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
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace grizzly_hills_cata
{
enum
{
    SPELL_ENVISION_DRAKURU_01     = 47118,
    SPELL_ENVISION_DRAKURU_02     = 47150,
    SPELL_ENVISION_DRAKURU_03     = 47317,
    SPELL_ENVISION_DRAKURU_04     = 47406,
    SPELL_ENVISION_DRAKURU_05     = 50440
};

enum Floppy
{
    // Creature
    NPC_MRFLOPPY                = 26589,
    NPC_HUNGRY_WORG             = 26586,
    NPC_RAVENOUS_WORG           = 26590,   // RWORG
    NPC_EMILY                   = 26588,
    // Quest
    QUEST_PERILOUS_ADVENTURE    = 12027,
    // Spell
    SPELL_MRFLOPPY              = 47184,   // vehicle aura
    // Text
    SAY_WORGHAGGRO1             = 0, // Um... I think one of those wolves is back...
    SAY_WORGHAGGRO2             = 1, // He's going for Mr. Floppy!
    SAY_WORGRAGGRO3             = 2, // Oh, no! Look, it's another wolf, and it's a biiiiiig one!
    SAY_WORGRAGGRO4             = 3, // He's gonna eat Mr. Floppy! You gotta help Mr. Floppy! You just gotta!
    SAY_RANDOMAGGRO             = 4, // There's a big meanie attacking Mr. Floppy! Help!
    SAY_VICTORY1                = 5, // Let's get out of here before more wolves find us!
    SAY_VICTORY2                = 6, // Don't go toward the light, Mr. Floppy!
    SAY_VICTORY3                = 7, // Mr. Floppy, you're ok! Thank you so much for saving Mr. Floppy!
    SAY_VICTORY4                = 8, // I think I see the camp! We're almost home, Mr. Floppy! Let's go!
    TEXT_EMOTE_WP1              = 9, // Mr. Floppy revives
    TEXT_EMOTE_AGGRO            = 10, // The Ravenous Worg chomps down on Mr. Floppy
    SAY_QUEST_ACCEPT            = 11, // Are you ready, Mr. Floppy? Stay close to me and watch out for those wolves!
    SAY_QUEST_COMPLETE          = 12, // Thank you for helping me get back to the camp. Go tell Walter that I'm safe now!
    PATH_ESCORT_EMILY           = 212706
};

enum Outhouse
{
    // Sound
    SOUND_FEMALE                    = 12671,
    SOUND_MALE                      = 12670,
    // Spell
    SPELL_OUTHOUSE_GROANS           = 48382,
    SPELL_CAMERA_SHAKE              = 47533,
    SPELL_DUST_FIELD                = 48329
};

enum TallhornStage
{
    //Gameobject
    OBJECT_HAUNCH                   = 188665
};

enum AmberpineWoodsman
{
    // Creature
    NPC_TALLHORN_STAG                      = 26363
};

enum AmberpineWoodsmanEvents
{
    EVENT_WOODSMAN_1                       = 1,
    EVENT_WOODSMAN_2                       = 2
};

enum Skirmisher
{
    // Quest
    QUEST_OVERWHELMED           = 12288,
    // Spell
    SPELL_RENEW_SKIRMISHER      = 48812,
    SPELL_KILL_CREDIT           = 48813,
    // Text
    SAY_RANDOM                  = 0
};

enum SmokeEmOut
{
    // Quest
    QUEST_SMOKE_EM_OUT_A                        = 12323,
    QUEST_SMOKE_EM_OUT_H                        = 12324,
    // Spell
    SPELL_SMOKE_BOMB                            = 49075,
    SPELL_CHOP                                  = 43410,
    SPELL_VENTURE_STRAGGLER_CREDIT              = 49093,
    // Text
    SAY_SEO                                     = 0
};

enum StragglerEvents
{
    EVENT_STRAGGLER_1                           = 1,
    EVENT_STRAGGLER_2                           = 2,
    EVENT_STRAGGLER_3                           = 3,
    EVENT_STRAGGLER_4                           = 4,
    EVENT_CHOP                                  = 5
};

enum LakeFrog
{
    // Creature
    NPC_LAKE_FROG                          = 33211,
    NPC_LAKE_FROG_QUEST                    = 33224,
    NPC_MAIDEN_OF_ASHWOOD_LAKE             = 33220,
    // Items
    ITEM_WARTS_B_GONE_LIP_BALM             = 44986,
    // Spells
    SPELL_WARTSBGONE_LIP_BALM              = 62574,
    SPELL_FROG_LOVE                        = 62537, // for 1 minute !
    SPELL_WARTS                            = 62581,
    SPELL_MAIDEN_OF_ASHWOOD_LAKE_TRANSFORM = 62550,
    SPELL_SUMMON_ASHWOOD_BRAND             = 62554,
    SPELL_FROG_KISS                        = 62536,
    // Text
    SAY_MAIDEN_0                           = 0,
    SAY_MAIDEN_1                           = 1
};

enum LakeFrogEvents
{
    EVENT_LAKEFROG_1                       = 1,
    EVENT_LAKEFROG_2                       = 2,
    EVENT_LAKEFROG_3                       = 3,
    EVENT_LAKEFROG_4                       = 4,
    EVENT_LAKEFROG_5                       = 5
};

enum ShredderDelivery
{
    NPC_BROKEN_DOWN_SHREDDER               = 27354
};

enum InfectedWorgenBite
{
    SPELL_INFECTED_WORGEN_BITE = 53094,
    SPELL_WORGENS_CALL         = 53095
};

enum RedRocket
{
    SPELL_VEHICLE_WARHEAD_FUSE            = 49107,
    SPELL_ALLIANCE_KILL_CREDIT_TORPEDO    = 49510,
    SPELL_HORDE_KILL_CREDIT_TORPEDO       = 49340,
    NPC_HORDE_LUMBERBOAT                  = 27702,
    NPC_ALLIANCE_LUMBERBOAT               = 27688,
    SPELL_DETONATE                        = 49250
};

enum WarheadSpells
{
    SPELL_WARHEAD_Z_CHECK               = 61678,
    SPELL_WARHEAD_SEEKING_LUMBERSHIP    = 49331,
    SPELL_WARHEAD_FUSE                  = 49181
};

enum WarheadDenonate
{
    SPELL_PARACHUTE                     = 66154,
    SPELL_TORPEDO_EXPLOSION             = 49290,
    NPC_ALLIANCE_LUMBERBOAT_EXPLOSIONS  = 27689
};

class spell_grizzly_hills_script_cast_summon_image_of_drakuru : public SpellScript
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
        OnEffectHitTarget.Register(&spell_grizzly_hills_script_cast_summon_image_of_drakuru::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }

    uint32 _triggeredSpellId;

public:
    explicit spell_grizzly_hills_script_cast_summon_image_of_drakuru(uint32 triggeredSpellId) : _triggeredSpellId(triggeredSpellId) { }
};

enum EscapeFromSilverbrook
{
    SPELL_SUMMON_WORGEN = 48681
};

class spell_grizzly_hills_escape_from_silverbrook : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_WORGEN });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_SUMMON_WORGEN, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_grizzly_hills_escape_from_silverbrook::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_grizzly_hills_escape_from_silverbrook_summon_worgen : public SpellScript
{
    void ModDest(SpellDestination& dest)
    {
        float dist = GetSpellInfo()->Effects[EFFECT_0].CalcRadius(GetCaster());
        float angle = frand(0.75f, 1.25f) * float(M_PI);

        Position pos = GetCaster()->GetNearPosition(dist, angle);
        dest.Relocate(pos);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_grizzly_hills_escape_from_silverbrook_summon_worgen::ModDest, EFFECT_0, TARGET_DEST_CASTER_SUMMON);
    }
};

class spell_grizzly_hills_hand_over_reins : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* caster = GetCaster()->ToCreature();
        GetHitUnit()->ExitVehicle();

        if (caster)
            caster->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_grizzly_hills_hand_over_reins::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum SeeYouOnTheOtherSide
{
    SPELL_SUMMON_YOUR_CORPSE    = 61612,
    SPELL_ON_THE_OTHER_SIDE     = 61611
};

class spell_grizzly_hills_rage_of_jinarrak : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_YOUR_CORPSE, SPELL_ON_THE_OTHER_SIDE });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_SUMMON_YOUR_CORPSE, true);
        target->CastSpell(target, SPELL_ON_THE_OTHER_SIDE, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_grizzly_hills_rage_of_jinarrak::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_grizzly_hills_ganjo_ressurection : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ON_THE_OTHER_SIDE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_ON_THE_OTHER_SIDE);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_grizzly_hills_ganjo_ressurection::HandleScript, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
    }
};

enum MakkiWintergale
{
    SPELL_FLIGHT_ONEQUAH_TO_LIGHTS_BREACH     = 53289
};

class spell_grizzly_hills_flight_onequah_to_lights_breach : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLIGHT_ONEQUAH_TO_LIGHTS_BREACH });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_FLIGHT_ONEQUAH_TO_LIGHTS_BREACH);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_grizzly_hills_flight_onequah_to_lights_breach::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum SamuelClearbook
{
    SPELL_FLIGHT_WESTFALL_TO_LIGHTS_BREACH     = 53310
};

class spell_grizzly_hills_flight_westfall_to_lights_breach : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLIGHT_WESTFALL_TO_LIGHTS_BREACH });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_FLIGHT_WESTFALL_TO_LIGHTS_BREACH);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_grizzly_hills_flight_westfall_to_lights_breach::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};
}

void AddSC_grizzly_hills_cata()
{
    using namespace grizzly_hills_cata;
    RegisterSpellScriptWithArgs(spell_grizzly_hills_script_cast_summon_image_of_drakuru, "spell_grizzly_hills_script_cast_summon_image_of_drakuru_01", SPELL_ENVISION_DRAKURU_01);
    RegisterSpellScriptWithArgs(spell_grizzly_hills_script_cast_summon_image_of_drakuru, "spell_grizzly_hills_script_cast_summon_image_of_drakuru_02", SPELL_ENVISION_DRAKURU_02);
    RegisterSpellScriptWithArgs(spell_grizzly_hills_script_cast_summon_image_of_drakuru, "spell_grizzly_hills_script_cast_summon_image_of_drakuru_03", SPELL_ENVISION_DRAKURU_03);
    RegisterSpellScriptWithArgs(spell_grizzly_hills_script_cast_summon_image_of_drakuru, "spell_grizzly_hills_script_cast_summon_image_of_drakuru_04", SPELL_ENVISION_DRAKURU_04);
    RegisterSpellScriptWithArgs(spell_grizzly_hills_script_cast_summon_image_of_drakuru, "spell_grizzly_hills_script_cast_summon_image_of_drakuru_05", SPELL_ENVISION_DRAKURU_05);
    RegisterSpellScript(spell_grizzly_hills_escape_from_silverbrook);
    RegisterSpellScript(spell_grizzly_hills_escape_from_silverbrook_summon_worgen);
    RegisterSpellScript(spell_grizzly_hills_hand_over_reins);
    RegisterSpellScript(spell_grizzly_hills_rage_of_jinarrak);
    RegisterSpellScript(spell_grizzly_hills_ganjo_ressurection);
    RegisterSpellScript(spell_grizzly_hills_flight_onequah_to_lights_breach);
    RegisterSpellScript(spell_grizzly_hills_flight_westfall_to_lights_breach);
}
