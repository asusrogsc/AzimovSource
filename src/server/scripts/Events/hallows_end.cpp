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
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

enum HallowEndCandysSpells
{
    SPELL_HALLOWS_END_CANDY_ORANGE_GIANT          = 24924, // Effect 1: Apply Aura: Mod Size, Value: 30%
    SPELL_HALLOWS_END_CANDY_SKELETON              = 24925, // Effect 1: Apply Aura: Change Model (Skeleton). Effect 2: Apply Aura: Underwater Breathing
    SPELL_HALLOWS_END_CANDY_PIRATE                = 24926, // Effect 1: Apply Aura: Increase Swim Speed, Value: 50%
    SPELL_HALLOWS_END_CANDY_GHOST                 = 24927, // Effect 1: Apply Aura: Levitate / Hover. Effect 2: Apply Aura: Slow Fall, Effect 3: Apply Aura: Water Walking
    SPELL_HALLOWS_END_CANDY_FEMALE_DEFIAS_PIRATE  = 44742, // Effect 1: Apply Aura: Change Model (Defias Pirate, Female). Effect 2: Increase Swim Speed, Value: 50%
    SPELL_HALLOWS_END_CANDY_MALE_DEFIAS_PIRATE    = 44743  // Effect 1: Apply Aura: Change Model (Defias Pirate, Male).   Effect 2: Increase Swim Speed, Value: 50%
};

std::array<uint32, 4> const CandysSpells =
{
    SPELL_HALLOWS_END_CANDY_ORANGE_GIANT,
    SPELL_HALLOWS_END_CANDY_SKELETON,
    SPELL_HALLOWS_END_CANDY_PIRATE,
    SPELL_HALLOWS_END_CANDY_GHOST
};

enum HallowsEndChildrensCustomeSpells
{
    SPELL_HALLOWS_END_SCARY_TIKI_MASK             = 97095,
    SPELL_HALLOWS_END_SCARY_WITCH_HAT             = 97134,
    SPELL_HALLOWS_END_SCARY_PUMPKIN_MASK          = 97144,
    SPELL_HALLOWS_END_SCARY_TIKI_MASK_2           = 100315,
    SPELL_HALLOWS_END_FANCY_TOP_HAT               = 100321
};

enum HallowsEndMiscSpells
{
    SPELL_HALLOWS_END_DUMMY_NUKE                  = 21912,
    SPELL_HALLOWS_END_DREAD_FERTILIZER            = 191546
};



enum TrickInitial
{
    SPELL_TRICK_INITIAL     = 24750
};

// 24714 - Trick
class spell_hallow_end_trick_initial : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRICK_INITIAL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TRICK_INITIAL);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_hallow_end_trick_initial::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TrickSpells
{
    SPELL_PIRATE_COSTUME_MALE           = 24708,
    SPELL_PIRATE_COSTUME_FEMALE         = 24709,
    SPELL_NINJA_COSTUME_MALE            = 24710,
    SPELL_NINJA_COSTUME_FEMALE          = 24711,
    SPELL_LEPER_GNOME_COSTUME_MALE      = 24712,
    SPELL_LEPER_GNOME_COSTUME_FEMALE    = 24713,
    SPELL_SKELETON_COSTUME              = 24723,
    SPELL_GHOST_COSTUME_MALE            = 24735,
    SPELL_GHOST_COSTUME_FEMALE          = 24736,
    SPELL_TRICK_BUFF                    = 24753,
};


enum TrickOrTreatSpells
{
    SPELL_TRICK                 = 24714,
    SPELL_TREAT                 = 24715,
    SPELL_TRICKED_OR_TREATED    = 24755,
    SPELL_TRICKY_TREAT_SPEED    = 42919,
    SPELL_TRICKY_TREAT_TRIGGER  = 42965,
    SPELL_UPSET_TUMMY           = 42966
};



enum HallowendData
{
    SPELL_HALLOWED_WAND_PIRATE             = 24717,
    SPELL_HALLOWED_WAND_NINJA              = 24718,
    SPELL_HALLOWED_WAND_LEPER_GNOME        = 24719,
    SPELL_HALLOWED_WAND_RANDOM             = 24720,
    SPELL_HALLOWED_WAND_SKELETON           = 24724,
    SPELL_HALLOWED_WAND_WISP               = 24733,
    SPELL_HALLOWED_WAND_GHOST              = 24737,
    SPELL_HALLOWED_WAND_BAT                = 24741
};


enum CreateWaterBucket
{
    SPELL_CREATE_WATER_BUCKET_BARREL_SPLASH    = 43244,
    SPELL_JUST_LOOTED_WATER_BARREL             = 44410
};

// 42144 - Headless Horseman - Create Water Bucket
class spell_hallow_end_create_water_bucket : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_WATER_BUCKET_BARREL_SPLASH, SPELL_JUST_LOOTED_WATER_BARREL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->CastSpell(target, SPELL_CREATE_WATER_BUCKET_BARREL_SPLASH);
        target->CastSpell(target, SPELL_JUST_LOOTED_WATER_BARREL);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_hallow_end_create_water_bucket::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 97135 - Children's Costume Aura
class spell_hallows_end_childrens_custome_aura : public AuraScript
{
    static constexpr std::array<uint32, 5> ChildrensCustomeSpells =
    {
        SPELL_HALLOWS_END_SCARY_TIKI_MASK,
        SPELL_HALLOWS_END_SCARY_WITCH_HAT,
        SPELL_HALLOWS_END_SCARY_PUMPKIN_MASK,
        SPELL_HALLOWS_END_SCARY_TIKI_MASK_2,
        SPELL_HALLOWS_END_FANCY_TOP_HAT
    };

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(ChildrensCustomeSpells);
    }

    void HandleAfterApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), Trinity::Containers::SelectRandomContainerElement(ChildrensCustomeSpells), aurEff);
    }

    void HandlePeriodic(AuraEffect const* aurEff)
    {
        for (uint32 spell : ChildrensCustomeSpells)
            GetTarget()->RemoveAura(spell);

        GetTarget()->CastSpell(GetTarget(), Trinity::Containers::SelectRandomContainerElement(ChildrensCustomeSpells), aurEff);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_hallows_end_childrens_custome_aura::HandleAfterApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic.Register(&spell_hallows_end_childrens_custome_aura::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 191547 - Powder Blast
class spell_hallows_end_powder_blast : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HALLOWS_END_DUMMY_NUKE, SPELL_HALLOWS_END_DREAD_FERTILIZER });
    }

    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_HALLOWS_END_DUMMY_NUKE);
        eventInfo.GetActor()->CastSpell(GetTarget(), SPELL_HALLOWS_END_DREAD_FERTILIZER);
    }

    void Register() override
    {
        OnEffectProc.Register(&spell_hallows_end_powder_blast::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

void AddSC_event_hallows_end()
{
    RegisterSpellScript(spell_hallow_end_trick_initial);
    RegisterSpellScript(spell_hallow_end_create_water_bucket);
    RegisterSpellScript(spell_hallows_end_childrens_custome_aura);
    RegisterSpellScript(spell_hallows_end_powder_blast);
}
