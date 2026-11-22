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
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace zuldrak_cata
{
enum CrusadeRecruit
{
    SPELL_QUEST_CREDIT                       = 50633,
    QUEST_TROLL_PATROL_INTESTINAL_FORTITUDE  = 12509,
    SAY_RECRUIT                              = 0
};

enum CrusadeRecruitEvents
{
    EVENT_RECRUIT_1                          = 1,
    EVENT_RECRUIT_2                          = 2
};

enum Finklestein
{
    // Creature
    NPC_FINKLESTEIN                          = 28205,
    // Item
    ITEM_KNOTROOT                            = 38338,
    ITEM_PICKLED_EAGLE_EGG                   = 38341,
    ITEM_SPECKLED_GUANO                      = 38337,
    ITEM_WITHERED_BATWING                    = 38339,
    ITEM_SEASONED_SLIDER_CIDER               = 38381,
    ITEM_PULVERIZED_GARGOYLE_TEETH           = 38384,
    ITEM_MUDDY_MIRE_MAGGOT                   = 38386,
    ITEM_SPIKY_SPIDER_EGG                    = 38393,
    ITEM_HAIRY_HERRING_HEAD                  = 38396,
    ITEM_PUTRID_PIRATE_PERSPIRATION          = 38397,
    ITEM_ICECROWN_BOTTLED_WATER              = 38398,
    ITEM_WASPS_WINGS                         = 38369,
    ITEM_PRISMATIC_MOJO                      = 38343,
    ITEM_RAPTOR_CLAW                         = 38370,
    ITEM_AMBERSEED                           = 38340,
    ITEM_SHRUNKEN_DRAGONS_CLAW               = 38344,
    ITEM_CHILLED_SERPENT_MUCUS               = 38346,
    ITEM_CRYSTALLIZED_HOGSNOT                = 38336,
    ITEM_CRUSHED_BASILISK_CRYSTALS           = 38379,
    ITEM_TROLLBANE                           = 38342,
    ITEM_FROZEN_SPIDER_ICHOR                 = 38345,
    // Quest
    QUEST_THE_ALCHEMIST_APPRENTICE_DAILY     = 12541,
    // Spells
    SPELL_ALCHEMIST_APPRENTICE_INVISBUFF     = 51216,
    SPELL_RANDOM_INGREDIENT_EASY_AURA        = 51015,
    SPELL_RANDOM_INGREDIENT_MEDIUM_AURA      = 51154,
    SPELL_RANDOM_INGREDIENT_HARD_AURA        = 51157,
    SPELL_RANDOM_INGREDIENT_EASY             = 51134,
    SPELL_RANDOM_INGREDIENT_MEDIUM           = 51105,
    SPELL_RANDOM_INGREDIENT_HARD             = 51107,
    SPELL_NEXT_INGREDIENT                    = 51049,
    SPELL_POT_CHECK                          = 51046,
    SPELL_THROW_INGREDIENT                   = 51025,
    SPELL_KILL_CREDIT                        = 51111,
    // Spell Fetch Easy
    SPELL_FETCH_KNOTROOT                     = 51018,
    SPELL_FETCH_PICKLED_EAGLE_EGG            = 51055,
    SPELL_FETCH_SPECKLED_GUANO               = 51057,
    SPELL_FETCH_WITHERED_BATWING             = 51059,
    SPELL_FETCH_SEASONED_SLIDER_CIDER        = 51062,
    SPELL_FETCH_PULVERIZED_GARGOYLE_TEETH    = 51064,
    SPELL_FETCH_MUDDY_MIRE_MAGGOT            = 51067,
    SPELL_FETCH_SPIKY_SPIDER_EGG             = 51069,
    SPELL_FETCH_HAIRY_HERRING_HEAD           = 51072,
    SPELL_FETCH_PUTRID_PIRATE_PERSPIRATION   = 51077,
    SPELL_FETCH_ICECROWN_BOTTLED_WATER       = 51079,
    // Spell Have Easy
    SPELL_HAVE_KNOTROOT                      = 51047,
    SPELL_HAVE_PICKLED_EAGLE_EGG             = 51056,
    SPELL_HAVE_SPECKLED_GUANO                = 51058,
    SPELL_HAVE_WITHERED_BATWING              = 51060,
    SPELL_HAVE_SEASONED_SLIDER_CIDER         = 51063,
    SPELL_HAVE_PULVERIZED_GARGOYLE_TEETH     = 51065,
    SPELL_HAVE_MUDDY_MIRE_MAGGOT             = 51068,
    SPELL_HAVE_SPIKY_SPIDER_EGG              = 51070,
    SPELL_HAVE_HAIRY_HERRING_HEAD            = 51075,
    SPELL_HAVE_PUTRID_PIRATE_PERSPIRATION    = 51078,
    SPELL_HAVE_ICECROWN_BOTTLED_WATER        = 51080,
    // Spell Fetch Medium
    SPELL_FETCH_WASPS_WINGS                  = 51081,
    SPELL_FETCH_PRISMATIC_MOJO               = 51083,
    SPELL_FETCH_RAPTOR_CLAW                  = 51085,
    SPELL_FETCH_AMBERSEED                    = 51087,
    SPELL_FETCH_SHRUNKEN_DRAGONS_CLAW        = 51091,
    // Spell Have Medium
    SPELL_HAVE_WASPS_WINGS                   = 51082,
    SPELL_HAVE_PRISMATIC_MOJO                = 51084,
    SPELL_HAVE_RAPTOR_CLAW                   = 51086,
    SPELL_HAVE_AMBERSEED                     = 51088,
    SPELL_HAVE_SHRUNKEN_DRAGONS_CLAW         = 51092,
    // Spell Fetch Hard
    SPELL_FETCH_CHILLED_SERPENT_MUCUS        = 51093,
    SPELL_FETCH_CRYSTALLIZED_HOGSNOT         = 51095,
    SPELL_FETCH_CRUSHED_BASILISK_CRYSTALS    = 51097,
    SPELL_FETCH_TROLLBANE                    = 51100,
    SPELL_FETCH_FROZEN_SPIDER_ICHOR          = 51102,
    // Spell Have Hard
    SPELL_HAVE_CHILLED_SERPENT_MUCUS         = 51094,
    SPELL_HAVE_CRYSTALLIZED_HOGSNOT          = 51096,
    SPELL_HAVE_CRUSHED_BASILISK_CRYSTALS     = 51098,
    SPELL_HAVE_TROLLBANE                     = 51101,
    SPELL_HAVE_FROZEN_SPIDER_ICHOR           = 51104,
    // Text
    SAY_EASY_123                             = 0,
    SAY_MEDIUM_4                             = 1,
    SAY_MEDIUM_5                             = 2,
    SAY_HARD_6                               = 3,
    SAY_RUINED                               = 4,
    // Text Easy
    SAY_KNOTROOT                             = 5,
    SAY_PICKLED_EAGLE_EGG                    = 6,
    SAY_SPECKLED_GUANO                       = 7,
    SAY_WITHERED_BATWING                     = 8,
    SAY_SEASONED_SLIDER_CIDER                = 9,
    SAY_PULVERIZED_GARGOYLE_TEETH            = 10,
    SAY_MUDDY_MIRE_MAGGOT                    = 11,
    SAY_SPIKY_SPIDER_EGG                     = 12,
    SAY_HAIRY_HERRING_HEAD                   = 13,
    SAY_PUTRID_PIRATE_PERSPIRATION           = 14,
    SAY_ICECROWN_BOTTLED_WATER               = 15,
    // Text Medium
    SAY_WASPS_WINGS                          = 16,
    SAY_PRISMATIC_MOJO                       = 17,
    SAY_RAPTOR_CLAW                          = 18,
    SAY_AMBERSEED                            = 19,
    SAY_SHRUNKEN_DRAGONS_CLAW                = 20,
    // Text Hard
    SAY_CHILLED_SERPENT_MUCUS                = 21,
    SAY_CRYSTALLIZED_HOGSNOT                 = 22,
    SAY_CRUSHED_BASILISK_CRYSTALS            = 23,
    SAY_TROLLBANE                            = 24,
    SAY_FROZEN_SPIDER_ICHOR                  = 25
};

enum FinklesteinEvents
{
    EVENT_TURN_TO_POT                      = 1,
    EVENT_TURN_BACK                        = 2,
    EVENT_EASY_123                         = 3,
    EVENT_MEDIUM_4                         = 4,
    EVENT_MEDIUM_5                         = 5,
    EVENT_HARD_6                           = 6
};

uint32 const FetchIngredients[21][4] =
{
    { SPELL_FETCH_KNOTROOT,                   SPELL_HAVE_KNOTROOT,                   ITEM_KNOTROOT,                   SAY_KNOTROOT                   },
    { SPELL_FETCH_PICKLED_EAGLE_EGG,          SPELL_HAVE_PICKLED_EAGLE_EGG,          ITEM_PICKLED_EAGLE_EGG,          SAY_PICKLED_EAGLE_EGG          },
    { SPELL_FETCH_SPECKLED_GUANO,             SPELL_HAVE_SPECKLED_GUANO,             ITEM_SPECKLED_GUANO,             SAY_SPECKLED_GUANO             },
    { SPELL_FETCH_WITHERED_BATWING,           SPELL_HAVE_WITHERED_BATWING,           ITEM_WITHERED_BATWING,           SAY_WITHERED_BATWING           },
    { SPELL_FETCH_SEASONED_SLIDER_CIDER,      SPELL_HAVE_SEASONED_SLIDER_CIDER,      ITEM_SEASONED_SLIDER_CIDER,      SAY_SEASONED_SLIDER_CIDER      },
    { SPELL_FETCH_PULVERIZED_GARGOYLE_TEETH,  SPELL_HAVE_PULVERIZED_GARGOYLE_TEETH,  ITEM_PULVERIZED_GARGOYLE_TEETH,  SAY_PULVERIZED_GARGOYLE_TEETH  },
    { SPELL_FETCH_MUDDY_MIRE_MAGGOT,          SPELL_HAVE_MUDDY_MIRE_MAGGOT,          ITEM_MUDDY_MIRE_MAGGOT,          SAY_MUDDY_MIRE_MAGGOT          },
    { SPELL_FETCH_SPIKY_SPIDER_EGG,           SPELL_HAVE_SPIKY_SPIDER_EGG,           ITEM_SPIKY_SPIDER_EGG,           SAY_SPIKY_SPIDER_EGG           },
    { SPELL_FETCH_HAIRY_HERRING_HEAD,         SPELL_HAVE_HAIRY_HERRING_HEAD,         ITEM_HAIRY_HERRING_HEAD,         SAY_HAIRY_HERRING_HEAD         },
    { SPELL_FETCH_PUTRID_PIRATE_PERSPIRATION, SPELL_HAVE_PUTRID_PIRATE_PERSPIRATION, ITEM_PUTRID_PIRATE_PERSPIRATION, SAY_PUTRID_PIRATE_PERSPIRATION },
    { SPELL_FETCH_ICECROWN_BOTTLED_WATER,     SPELL_HAVE_ICECROWN_BOTTLED_WATER,     ITEM_ICECROWN_BOTTLED_WATER,     SAY_ICECROWN_BOTTLED_WATER     },
    { SPELL_FETCH_WASPS_WINGS,                SPELL_HAVE_WASPS_WINGS,                ITEM_WASPS_WINGS,                SAY_WASPS_WINGS                },
    { SPELL_FETCH_PRISMATIC_MOJO,             SPELL_HAVE_PRISMATIC_MOJO,             ITEM_PRISMATIC_MOJO,             SAY_PRISMATIC_MOJO             },
    { SPELL_FETCH_RAPTOR_CLAW,                SPELL_HAVE_RAPTOR_CLAW,                ITEM_RAPTOR_CLAW,                SAY_RAPTOR_CLAW                },
    { SPELL_FETCH_AMBERSEED,                  SPELL_HAVE_AMBERSEED,                  ITEM_AMBERSEED,                  SAY_AMBERSEED                  },
    { SPELL_FETCH_SHRUNKEN_DRAGONS_CLAW,      SPELL_HAVE_SHRUNKEN_DRAGONS_CLAW,      ITEM_SHRUNKEN_DRAGONS_CLAW,      SAY_SHRUNKEN_DRAGONS_CLAW      },
    { SPELL_FETCH_CHILLED_SERPENT_MUCUS,      SPELL_HAVE_CHILLED_SERPENT_MUCUS,      ITEM_CHILLED_SERPENT_MUCUS,      SAY_CHILLED_SERPENT_MUCUS      },
    { SPELL_FETCH_CRYSTALLIZED_HOGSNOT,       SPELL_HAVE_CRYSTALLIZED_HOGSNOT,       ITEM_CRYSTALLIZED_HOGSNOT,       SAY_CRYSTALLIZED_HOGSNOT       },
    { SPELL_FETCH_CRUSHED_BASILISK_CRYSTALS,  SPELL_HAVE_CRUSHED_BASILISK_CRYSTALS,  ITEM_CRUSHED_BASILISK_CRYSTALS,  SAY_CRUSHED_BASILISK_CRYSTALS  },
    { SPELL_FETCH_TROLLBANE,                  SPELL_HAVE_TROLLBANE,                  ITEM_TROLLBANE,                  SAY_TROLLBANE                  },
    { SPELL_FETCH_FROZEN_SPIDER_ICHOR,        SPELL_HAVE_FROZEN_SPIDER_ICHOR,        ITEM_FROZEN_SPIDER_ICHOR,        SAY_FROZEN_SPIDER_ICHOR        }
};

enum StormCloud
{
    STORM_COULD         = 29939,
    HEALING_WINDS       = 55549,
    STORM_VISUAL        = 55708,
    GYMERS_GRAB         = 55516,
    RIDE_VEHICLE        = 43671
};

enum ScourgeDisguise
{
    SPELL_SCOURGE_DISGUISE             = 51966,
    SPELL_SCOURGE_DISGUISE_INSTABILITY = 51971,
    SPELL_SCOURGE_DISGUISE_EXPIRING    = 52010,
    SPELL_DROP_DISGUISE                = 54089,
    TEXT_DISGUISE_WARNING              = 28891
};

class spell_zuldrak_scourge_disguise : public AuraScript
{
    void ApplyEffect(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_SCOURGE_DISGUISE_INSTABILITY, true);
    }

    void RemoveEffect(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAura(SPELL_SCOURGE_DISGUISE_INSTABILITY);
    }

    void Register() override
    {
        OnEffectRemove.Register(&spell_zuldrak_scourge_disguise::RemoveEffect, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
        OnEffectApply.Register(&spell_zuldrak_scourge_disguise::ApplyEffect, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_zuldrak_scourge_disguise_instability : public AuraScript
{
    void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
    {
        isPeriodic = true;
        amplitude = irand(30, 240) * IN_MILLISECONDS;
    }

    void HandleDummyTick(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_SCOURGE_DISGUISE_EXPIRING, true);
    }

    void HandleUpdatePeriodic(AuraEffect* aurEff)
    {
        aurEff->CalculatePeriodic(GetCaster());
    }

    void Register() override
    {
        DoEffectCalcPeriodic.Register(&spell_zuldrak_scourge_disguise_instability::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectPeriodic.Register(&spell_zuldrak_scourge_disguise_instability::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectUpdatePeriodic.Register(&spell_zuldrak_scourge_disguise_instability::HandleUpdatePeriodic, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

class spell_zuldrak_scourge_disguise_expiring : public AuraScript
{
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
            if (player->HasAura(SPELL_SCOURGE_DISGUISE))
                player->Unit::Whisper(TEXT_DISGUISE_WARNING, player, true);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_SCOURGE_DISGUISE);
    }

    void Register() override
    {
        OnEffectApply.Register(&spell_zuldrak_scourge_disguise_expiring::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_zuldrak_scourge_disguise_expiring::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_zuldrak_drop_disguise : public SpellScript
{
    void HandleHit()
    {
        if (Unit* target = GetHitUnit())
            if (target->HasAura(SPELL_SCOURGE_DISGUISE))
                target->CastSpell(target, SPELL_SCOURGE_DISGUISE_EXPIRING, true);
    }

    void Register() override
    {
        AfterHit.Register(&spell_zuldrak_drop_disguise::HandleHit);
    }
};

enum Cocooned
{
    SPELL_SUMMON_SCOURGED_CAPTIVE      = 51597,
    SPELL_SUMMON_CAPTIVE_FOOTMAN       = 51599
};

class spell_zuldrak_cocooned_not_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_SCOURGED_CAPTIVE });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), SPELL_SUMMON_SCOURGED_CAPTIVE);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_cocooned_not_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_zuldrak_cocooned_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_SCOURGED_CAPTIVE, SPELL_SUMMON_CAPTIVE_FOOTMAN });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), roll_chance_i(66) ? SPELL_SUMMON_SCOURGED_CAPTIVE : SPELL_SUMMON_CAPTIVE_FOOTMAN);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_cocooned_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum Sabotage
{
    SPELL_EXPLODE_SCOURGEWAGON_ROLLER    = 52325,
    SPELL_EXPLODE_SCOURGEWAGON_FRAME     = 52329,
    SPELL_EXPLODE_SCOURGEWAGON_GRILL     = 52330,
    SPELL_EXPLODE_SCOURGEWAGON_WHEEL     = 52332
};

class spell_zuldrak_scourgewagon_explosion : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_EXPLODE_SCOURGEWAGON_ROLLER,
            SPELL_EXPLODE_SCOURGEWAGON_FRAME,
            SPELL_EXPLODE_SCOURGEWAGON_GRILL,
            SPELL_EXPLODE_SCOURGEWAGON_WHEEL
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_ROLLER);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_FRAME);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_GRILL);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_WHEEL);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_WHEEL);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_WHEEL);
        caster->CastSpell(caster, SPELL_EXPLODE_SCOURGEWAGON_WHEEL);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_zuldrak_scourgewagon_explosion::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_zuldrak_chains_of_the_scourge : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_1].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_chains_of_the_scourge::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Rampage
{
    SPELL_RAMPAGE_SUMMON_AKILZON    = 52934,
    SPELL_RAMPAGE_SUMMON_HALAZZI    = 52935,
    SPELL_RAMPAGE_SUMMON_JANALAI    = 52936,
    SPELL_RAMPAGE_SUMMON_NALORAKK   = 52937
};

class spell_zuldrak_rampage_summon_zulaman_gods_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_RAMPAGE_SUMMON_AKILZON,
            SPELL_RAMPAGE_SUMMON_HALAZZI,
            SPELL_RAMPAGE_SUMMON_JANALAI,
            SPELL_RAMPAGE_SUMMON_NALORAKK
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_RAMPAGE_SUMMON_AKILZON);
        caster->CastSpell(caster, SPELL_RAMPAGE_SUMMON_HALAZZI);
        caster->CastSpell(caster, SPELL_RAMPAGE_SUMMON_JANALAI);
        caster->CastSpell(caster, SPELL_RAMPAGE_SUMMON_NALORAKK);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_zuldrak_rampage_summon_zulaman_gods_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BreakingThroughJinAlai
{
    SPELL_BREAKING_THROUGH_SUMMON_AKILZON    = 51861,
    SPELL_BREAKING_THROUGH_SUMMON_JANALAI    = 52452,
    SPELL_BREAKING_THROUGH_SUMMON_HALAZZI    = 52453,
    SPELL_BREAKING_THROUGH_SUMMON_NALORAKK   = 52454
};

class spell_zuldrak_breaking_through_summon_zulaman_gods_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BREAKING_THROUGH_SUMMON_AKILZON,
            SPELL_BREAKING_THROUGH_SUMMON_JANALAI,
            SPELL_BREAKING_THROUGH_SUMMON_HALAZZI,
            SPELL_BREAKING_THROUGH_SUMMON_NALORAKK
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_BREAKING_THROUGH_SUMMON_AKILZON);
        caster->CastSpell(caster, SPELL_BREAKING_THROUGH_SUMMON_JANALAI);
        caster->CastSpell(caster, SPELL_BREAKING_THROUGH_SUMMON_HALAZZI);
        caster->CastSpell(caster, SPELL_BREAKING_THROUGH_SUMMON_NALORAKK);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_zuldrak_breaking_through_summon_zulaman_gods_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum KickinNassAndTakinManes
{
    SPELL_SUMMON_NASS     = 51865
};

class spell_zuldrak_summon_nass : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_NASS });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_SUMMON_NASS);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_summon_nass::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ConvocationAtZolHeb
{
    SPELL_AKALIS_STUN           = 52989,
    SPELL_FLICKERING_FLAMES     = 53504
};

class spell_zuldrak_remove_akalis_stun : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_AKALIS_STUN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_AKALIS_STUN);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_remove_akalis_stun::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_zuldrak_quenching_mist : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLICKERING_FLAMES });
    }

    void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_FLICKERING_FLAMES);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_zuldrak_quenching_mist::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_HEAL);
    }
};

class spell_zuldrak_akalis_stun : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLICKERING_FLAMES });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_FLICKERING_FLAMES);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_zuldrak_akalis_stun::AfterApply, EFFECT_0, SPELL_AURA_MOD_STUN, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_zuldrak_summon_stefan : public SpellScript
{
    void SetDest(SpellDestination& dest)
    {
        // Adjust effect summon position (sniff)
        Position const offset = { 0.0f, 0.0f, 30.0f, 0.0f };
        dest.RelocateOffset(offset);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_zuldrak_summon_stefan::SetDest, EFFECT_0, TARGET_DEST_CASTER_BACK);
    }
};

enum GluttonousLurkers
{
    SPELL_SUMMON_GORGED_LURKING_BASILISK    = 50928
};

class spell_zuldrak_zuldrak_rat : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_GORGED_LURKING_BASILISK });
    }

    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        if (GetHitAura() && GetHitAura()->GetStackAmount() >= GetSpellInfo()->StackAmount)
        {
            GetHitUnit()->CastSpell((Unit*) nullptr, SPELL_SUMMON_GORGED_LURKING_BASILISK, true);
            if (Creature* basilisk = GetHitUnit()->ToCreature())
                basilisk->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_zuldrak_rat::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheStormKingsVengeance
{
    SPELL_RIDE_GYMER            = 43671,
    SPELL_GRABBED               = 55424,
    SPELL_VARGUL_EXPLOSION      = 55569
};

class spell_zuldrak_gymers_grab : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_RIDE_GYMER, SPELL_GRABBED });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (!GetHitCreature())
            return;
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(2);
        GetHitCreature()->CastSpell(GetCaster(), SPELL_RIDE_GYMER, args);
        GetHitCreature()->CastSpell(GetHitCreature(), SPELL_GRABBED, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_gymers_grab::HandleScript, EFFECT_0,  SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_zuldrak_gymers_throw : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_VARGUL_EXPLOSION });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (caster->IsVehicle())
            if (Unit* passenger = caster->GetVehicleKit()->GetPassenger(1))
            {
                passenger->ExitVehicle();
                caster->CastSpell(passenger, SPELL_VARGUL_EXPLOSION, true);
            }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_zuldrak_gymers_throw::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum LabWork
{
    SPELL_LAB_WORK_HAVE_WITHERED_BATWING        = 51060,
    SPELL_LAB_WORK_HAVE_MUDDY_MIRE_MAGGOT       = 51068,
    SPELL_LAB_WORK_HAVE_AMBERSEED               = 51088,
    SPELL_LAB_WORK_HAVE_CHILLED_SERPENT_MUCUS   = 51094,

    SPELL_WITHERED_BATWING_KILL_CREDIT          = 51226,
    SPELL_MUDDY_MIRE_MAGGOT_KILL_CREDIT         = 51227,
    SPELL_AMBERSEED_KILL_CREDIT                 = 51228,
    SPELL_CHILLED_SERPENT_MUCUS_KILL_CREDIT     = 51229
};

class spell_zuldrak_have_ingredient : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WITHERED_BATWING_KILL_CREDIT,
            SPELL_MUDDY_MIRE_MAGGOT_KILL_CREDIT,
            SPELL_AMBERSEED_KILL_CREDIT,
            SPELL_CHILLED_SERPENT_MUCUS_KILL_CREDIT
        });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        switch (GetId())
        {
            case SPELL_LAB_WORK_HAVE_WITHERED_BATWING:
                GetTarget()->CastSpell(GetTarget(), SPELL_WITHERED_BATWING_KILL_CREDIT);
                break;
            case SPELL_LAB_WORK_HAVE_MUDDY_MIRE_MAGGOT:
                GetTarget()->CastSpell(GetTarget(), SPELL_MUDDY_MIRE_MAGGOT_KILL_CREDIT);
                break;
            case SPELL_LAB_WORK_HAVE_AMBERSEED:
                GetTarget()->CastSpell(GetTarget(), SPELL_AMBERSEED_KILL_CREDIT);
                break;
            case SPELL_LAB_WORK_HAVE_CHILLED_SERPENT_MUCUS:
                GetTarget()->CastSpell(GetTarget(), SPELL_CHILLED_SERPENT_MUCUS_KILL_CREDIT);
                break;
            default:
                break;
        }
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_zuldrak_have_ingredient::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_zuldrak_summon_escort_aura : public AuraScript
{
    enum Disclosure
    {
        SPELL_SCOURGE_DISGUISE          = 51966,
        SPELL_SUMMON_ESCORT             = 52775,
        SPELL_SCOURGE_DISGUISE_ESCORT   = 52842
    };

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SCOURGE_DISGUISE,
            SPELL_SUMMON_ESCORT,
            SPELL_SCOURGE_DISGUISE_ESCORT
        });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAurasDueToSpell(SPELL_SCOURGE_DISGUISE);
        target->CastSpell(target, SPELL_SUMMON_ESCORT);
        target->CastSpell(target, SPELL_SCOURGE_DISGUISE_ESCORT);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_zuldrak_summon_escort_aura::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};
}

void AddSC_zuldrak_cata()
{
    using namespace zuldrak_cata;
    RegisterSpellScript(spell_zuldrak_scourge_disguise);
    RegisterSpellScript(spell_zuldrak_scourge_disguise_instability);
    RegisterSpellScript(spell_zuldrak_scourge_disguise_expiring);
    RegisterSpellScript(spell_zuldrak_drop_disguise);
    RegisterSpellScript(spell_zuldrak_cocooned_not_on_quest);
    RegisterSpellScript(spell_zuldrak_cocooned_on_quest);
    RegisterSpellScript(spell_zuldrak_scourgewagon_explosion);
    RegisterSpellScript(spell_zuldrak_chains_of_the_scourge);
    RegisterSpellScript(spell_zuldrak_rampage_summon_zulaman_gods_master);
    RegisterSpellScript(spell_zuldrak_breaking_through_summon_zulaman_gods_master);
    RegisterSpellScript(spell_zuldrak_summon_nass);
    RegisterSpellScript(spell_zuldrak_remove_akalis_stun);
    RegisterSpellScript(spell_zuldrak_quenching_mist);
    RegisterSpellScript(spell_zuldrak_akalis_stun);
    RegisterSpellScript(spell_zuldrak_summon_stefan);
    RegisterSpellScript(spell_zuldrak_zuldrak_rat);
    RegisterSpellScript(spell_zuldrak_gymers_grab);
    RegisterSpellScript(spell_zuldrak_gymers_throw);
    RegisterSpellScript(spell_zuldrak_have_ingredient);
    RegisterSpellScript(spell_zuldrak_summon_escort_aura);
}
