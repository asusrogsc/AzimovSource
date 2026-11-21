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
#include "CreatureAIImpl.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "World.h"

enum RamBlaBla
{
    SPELL_GIDDYUP                           = 42924,
    SPELL_RENTAL_RACING_RAM                 = 43883,
    SPELL_SWIFT_WORK_RAM                    = 43880,
    SPELL_RENTAL_RACING_RAM_AURA            = 42146,
    SPELL_RAM_LEVEL_NEUTRAL                 = 43310,
    SPELL_RAM_TROT                          = 42992,
    SPELL_RAM_CANTER                        = 42993,
    SPELL_RAM_GALLOP                        = 42994,
    SPELL_RAM_FATIGUE                       = 43052,
    SPELL_EXHAUSTED_RAM                     = 43332,
    SPELL_RELAY_RACE_TURN_IN                = 44501,

    // Quest
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_GREEN  = 43345,
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_YELLOW = 43346,
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_RED    = 43347
};








enum RamBlub
{
    // Horde
    QUEST_BARK_FOR_DROHNS_DISTILLERY        = 11407,
    QUEST_BARK_FOR_TCHALIS_VOODOO_BREWERY   = 11408,

    // Alliance
    QUEST_BARK_BARLEYBREW                   = 11293,
    QUEST_BARK_FOR_THUNDERBREWS             = 11294,

    // Bark for Drohn's Distillery!
    SAY_DROHN_DISTILLERY_1                  = 23520,
    SAY_DROHN_DISTILLERY_2                  = 23521,
    SAY_DROHN_DISTILLERY_3                  = 23522,
    SAY_DROHN_DISTILLERY_4                  = 23523,

    // Bark for T'chali's Voodoo Brewery!
    SAY_TCHALIS_VOODOO_1                    = 23524,
    SAY_TCHALIS_VOODOO_2                    = 23525,
    SAY_TCHALIS_VOODOO_3                    = 23526,
    SAY_TCHALIS_VOODOO_4                    = 23527,

    // Bark for the Barleybrews!
    SAY_BARLEYBREW_1                        = 23464,
    SAY_BARLEYBREW_2                        = 23465,
    SAY_BARLEYBREW_3                        = 23466,
    SAY_BARLEYBREW_4                        = 22941,

    // Bark for the Thunderbrews!
    SAY_THUNDERBREWS_1                      = 23467,
    SAY_THUNDERBREWS_2                      = 23468,
    SAY_THUNDERBREWS_3                      = 23469,
    SAY_THUNDERBREWS_4                      = 22942
};


enum BrewfestMountTransformation
{
    SPELL_MOUNT_RAM_100                         = 43900,
    SPELL_MOUNT_RAM_60                          = 43899,
    SPELL_MOUNT_KODO_100                        = 49379,
    SPELL_MOUNT_KODO_60                         = 49378,
    SPELL_BREWFEST_MOUNT_TRANSFORM              = 49357,
    SPELL_BREWFEST_MOUNT_TRANSFORM_REVERSE      = 52845,
};

// 49357 - Brewfest Mount Transformation
// 52845 - Brewfest Mount Transformation (Faction Swap)
class spell_brewfest_mount_transformation : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MOUNT_RAM_100,
            SPELL_MOUNT_RAM_60,
            SPELL_MOUNT_KODO_100,
            SPELL_MOUNT_KODO_60
        });
    }

    void HandleDummy(SpellEffIndex /* effIndex */)
    {
        Player* caster = GetCaster()->ToPlayer();
        if (caster->HasAuraType(SPELL_AURA_MOUNTED))
        {
            caster->RemoveAurasByType(SPELL_AURA_MOUNTED);
            uint32 spell_id;

            switch (GetSpellInfo()->Id)
            {
                case SPELL_BREWFEST_MOUNT_TRANSFORM:
                    if (caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                        spell_id = caster->GetTeam() == ALLIANCE ? SPELL_MOUNT_RAM_100 : SPELL_MOUNT_KODO_100;
                    else
                        spell_id = caster->GetTeam() == ALLIANCE ? SPELL_MOUNT_RAM_60 : SPELL_MOUNT_KODO_60;
                    break;
                case SPELL_BREWFEST_MOUNT_TRANSFORM_REVERSE:
                    if (caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                        spell_id = caster->GetTeam() == HORDE ? SPELL_MOUNT_RAM_100 : SPELL_MOUNT_KODO_100;
                    else
                        spell_id = caster->GetTeam() == HORDE ? SPELL_MOUNT_RAM_60 : SPELL_MOUNT_KODO_60;
                    break;
                default:
                    return;
            }
            caster->CastSpell(caster, spell_id, true);
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_brewfest_mount_transformation::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/*
    Brew of the Month
 January   [Wild Winter Pilsner]
    spell_brewfest_botm_the_beast_within
 February  [Izzard's Ever Flavor]
    spell_brewfest_botm_gassy
 March     [Aromatic Honey Brew]
    Nothing to script here
 April     [Metok's Bubble Bock]
    spell_brewfest_botm_bloated
    Incomplete (spells 49828, 49827, 49830, 49837)
 May       [Springtime Stout]
    Nothing to script here
 June      [Blackrock Lager]
    spell_brewfest_botm_internal_combustion
 July      [Stranglethorn Brew]
    spell_brewfest_botm_jungle_madness
 August    [Draenic Pale Ale]
    spell_brewfest_botm_pink_elekk
 September [Binary Brew]
    spell_brewfest_botm_teach_language
 October   [Autumnal Acorn Ale]
    Nothing to script here
 November  [Bartlett's Bitter Brew]
    spell_brewfest_botm_nauseous
    Incomplete
 December  [Lord of Frost's Private Label]
    Nothing to script here
*/

enum WildWinterPilsner
{
    SPELL_BOTM_UNLEASH_THE_BEAST    = 50099
};

// 50098 - The Beast Within
class spell_brewfest_botm_the_beast_within : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_UNLEASH_THE_BEAST });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_UNLEASH_THE_BEAST);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_brewfest_botm_the_beast_within::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum IzzardsEverFlavor
{
    SPELL_BOTM_BELCH_BREW_BELCH_VISUAL    = 49860
};

// 49864 - Gassy
class spell_brewfest_botm_gassy : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BELCH_BREW_BELCH_VISUAL });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BELCH_BREW_BELCH_VISUAL, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_brewfest_botm_gassy::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum MetoksBubbleBock
{
    SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE    = 50072
};

// 49822 - Bloated
class spell_brewfest_botm_bloated : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_brewfest_botm_bloated::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum BlackrockLager
{
    SPELL_BOTM_BELCH_FIRE_VISUAL    = 49737
};

// 49738 - Internal Combustion
class spell_brewfest_botm_internal_combustion : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BELCH_FIRE_VISUAL });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BELCH_FIRE_VISUAL, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_brewfest_botm_internal_combustion::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum StranglethornBrew
{
    SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT    = 50010
};

// 49962 - Jungle Madness!
class spell_brewfest_botm_jungle_madness : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT });
    }

    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_brewfest_botm_jungle_madness::HandleAfterCast);
    }
};

enum DraenicPaleAle
{
    SPELL_BOTM_PINK_ELEKK    = 49908
};

// 42264 - Weak Alcohol
class spell_brewfest_botm_pink_elekk : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_PINK_ELEKK });
    }

    void HandleAfterCast()
    {
        // TODO: Needs additional research, this spell is most likely used if drunk state is high enough.
        if (roll_chance_i(50))
            GetCaster()->CastSpell(GetCaster(), SPELL_BOTM_PINK_ELEKK);
    }

    void Register() override
    {
        AfterCast.Register(&spell_brewfest_botm_pink_elekk::HandleAfterCast);
    }
};

enum BinaryBrew
{
    SPELL_LEARN_GNOMISH_BINARY      = 50242,
    SPELL_LEARN_GOBLIN_BINARY       = 50246
};

// 50243 - Teach Language
class spell_brewfest_botm_teach_language : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEARN_GNOMISH_BINARY, SPELL_LEARN_GOBLIN_BINARY });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            caster->CastSpell(caster, caster->GetTeam() == ALLIANCE ? SPELL_LEARN_GNOMISH_BINARY : SPELL_LEARN_GOBLIN_BINARY, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_brewfest_botm_teach_language::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum BartlettsBitterBrew
{
    SPELL_BOTM_VOMIT_BREW_VOMIT_VISUAL    = 49867
};

// 49869 - Nauseous
class spell_brewfest_botm_nauseous : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_VOMIT_BREW_VOMIT_VISUAL });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_VOMIT_BREW_VOMIT_VISUAL, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_brewfest_botm_nauseous::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum CreateEmptyBrewBottle
{
    SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE    = 51655
};

// 42254, 42255, 42256, 42257, 42258, 42259, 42260, 42261, 42263, 42264, 43959, 43961 - Weak Alcohol
class spell_brewfest_botm_weak_alcohol : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE });
    }

    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_brewfest_botm_weak_alcohol::HandleAfterCast);
    }
};

enum EmptyBottleThrow
{
    SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE    = 51695,   // Just unit, not creature
    SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND      = 51697
};

// 51694 - BOTM - Empty Bottle Throw - Resolve
class spell_brewfest_botm_empty_bottle_throw_resolve : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE,
            SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        if (Unit* target = GetHitUnit())
            caster->CastSpell(target, SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE, true);
        else
            caster->CastSpell(GetHitDest()->GetPosition(), SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_brewfest_botm_empty_bottle_throw_resolve::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum MoleMachine
{
    SPELL_PORT_TO_GRIM_GUZZLER     = 47523
};

// 49466 - Mole Machine Portal Schedule
class spell_brewfest_mole_machine_portal_schedule : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PORT_TO_GRIM_GUZZLER });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_PORT_TO_GRIM_GUZZLER);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_brewfest_mole_machine_portal_schedule::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_event_brewfest()
{
    RegisterSpellScript(spell_brewfest_mount_transformation);
    RegisterSpellScript(spell_brewfest_botm_the_beast_within);
    RegisterSpellScript(spell_brewfest_botm_gassy);
    RegisterSpellScript(spell_brewfest_botm_bloated);
    RegisterSpellScript(spell_brewfest_botm_internal_combustion);
    RegisterSpellScript(spell_brewfest_botm_jungle_madness);
    RegisterSpellScript(spell_brewfest_botm_pink_elekk);
    RegisterSpellScript(spell_brewfest_botm_teach_language);
    RegisterSpellScript(spell_brewfest_botm_nauseous);
    RegisterSpellScript(spell_brewfest_botm_weak_alcohol);
    RegisterSpellScript(spell_brewfest_botm_empty_bottle_throw_resolve);
    RegisterSpellScript(spell_brewfest_mole_machine_portal_schedule);
}
