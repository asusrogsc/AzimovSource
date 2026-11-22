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

/* ScriptData
SDName: Blades_Edge_Mountains
SD%Complete: 90
SDComment: Quest support: 10503, 10504, 10594, 10609, 10821. Ogri'la->Skettis Flight.
SDCategory: Blade's Edge Mountains
EndScriptData */

/* ContentData
npc_nether_drake
go_legion_obelisk
EndContentData */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "CreatureAIImpl.h"
#include "GameObjectAI.h"
#include "GridNotifiersImpl.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace blades_edge_mountains_cata
{
enum Netherdrake
{
    //Used by 20021, 21817, 21820, 21821, 21823 but not existing in database
    SAY_NIHIL_1                 = 0,
    SAY_NIHIL_2                 = 1,
    SAY_NIHIL_3                 = 2,
    SAY_NIHIL_4                 = 3,
    SAY_NIHIL_INTERRUPT         = 4,

    ENTRY_WHELP                 = 20021,
    ENTRY_PROTO                 = 21821,
    ENTRY_ADOLE                 = 21817,
    ENTRY_MATUR                 = 21820,
    ENTRY_NIHIL                 = 21823,

    SPELL_T_PHASE_MODULATOR     = 37573,

    SPELL_ARCANE_BLAST          = 38881,
    SPELL_MANA_BURN             = 38884,
    SPELL_INTANGIBLE_PRESENCE   = 36513
};

enum LegionObelisk
{
    GO_LEGION_OBELISK_ONE           = 185193,
    GO_LEGION_OBELISK_TWO           = 185195,
    GO_LEGION_OBELISK_THREE         = 185196,
    GO_LEGION_OBELISK_FOUR          = 185197,
    GO_LEGION_OBELISK_FIVE          = 185198,
    NPC_DOOMCRYER                   = 19963,
    QUEST_YOURE_FIRED               = 10821
};

enum SimonGame
{
    NPC_SIMON_BUNNY                 = 22923,
    NPC_APEXIS_GUARDIAN             = 22275,

    GO_APEXIS_RELIC                 = 185890,
    GO_APEXIS_MONUMENT              = 185944,
    GO_AURA_BLUE                    = 185872,
    GO_AURA_GREEN                   = 185873,
    GO_AURA_RED                     = 185874,
    GO_AURA_YELLOW                  = 185875,

    GO_BLUE_CLUSTER_DISPLAY         = 7369,
    GO_GREEN_CLUSTER_DISPLAY        = 7371,
    GO_RED_CLUSTER_DISPLAY          = 7373,
    GO_YELLOW_CLUSTER_DISPLAY       = 7375,
    GO_BLUE_CLUSTER_DISPLAY_LARGE   = 7364,
    GO_GREEN_CLUSTER_DISPLAY_LARGE  = 7365,
    GO_RED_CLUSTER_DISPLAY_LARGE    = 7366,
    GO_YELLOW_CLUSTER_DISPLAY_LARGE = 7367,

    SPELL_PRE_GAME_BLUE             = 40176,
    SPELL_PRE_GAME_GREEN            = 40177,
    SPELL_PRE_GAME_RED              = 40178,
    SPELL_PRE_GAME_YELLOW           = 40179,
    SPELL_VISUAL_BLUE               = 40244,
    SPELL_VISUAL_GREEN              = 40245,
    SPELL_VISUAL_RED                = 40246,
    SPELL_VISUAL_YELLOW             = 40247,

    SOUND_BLUE                      = 11588,
    SOUND_GREEN                     = 11589,
    SOUND_RED                       = 11590,
    SOUND_YELLOW                    = 11591,
    SOUND_DISABLE_NODE              = 11758,

    SPELL_AUDIBLE_GAME_TICK         = 40391,
    SPELL_VISUAL_START_PLAYER_LEVEL = 40436,
    SPELL_VISUAL_START_AI_LEVEL     = 40387,

    SPELL_BAD_PRESS_TRIGGER         = 41241,
    SPELL_BAD_PRESS_DAMAGE          = 40065,
    SPELL_REWARD_BUFF_1             = 40310,
    SPELL_REWARD_BUFF_2             = 40311,
    SPELL_REWARD_BUFF_3             = 40312,
};

enum SimonEvents
{
    EVENT_SIMON_SETUP_PRE_GAME         = 1,
    EVENT_SIMON_PLAY_SEQUENCE          = 2,
    EVENT_SIMON_RESET_CLUSTERS         = 3,
    EVENT_SIMON_PERIODIC_PLAYER_CHECK  = 4,
    EVENT_SIMON_TOO_LONG_TIME          = 5,
    EVENT_SIMON_GAME_TICK              = 6,
    EVENT_SIMON_ROUND_FINISHED         = 7,

    ACTION_SIMON_CORRECT_FULL_SEQUENCE = 8,
    ACTION_SIMON_WRONG_SEQUENCE        = 9,
    ACTION_SIMON_ROUND_FINISHED        = 10,
};

enum SimonColors
{
    SIMON_BLUE          = 0,
    SIMON_RED           = 1,
    SIMON_GREEN         = 2,
    SIMON_YELLOW        = 3,
    SIMON_MAX_COLORS    = 4,
};

enum ApexisRelic
{
    QUEST_CRYSTALS            = 11025,
    GOSSIP_TEXT_ID            = 10948,

    ITEM_APEXIS_SHARD         = 32569,
    SPELL_TAKE_REAGENTS_SOLO  = 41145,
    SPELL_TAKE_REAGENTS_GROUP = 41146,
};

enum ScannerMasterBunny
{
    NPC_OSCILLATING_FREQUENCY_SCANNER_TOP_BUNNY = 21759,
    GO_OSCILLATING_FREQUENCY_SCANNER            = 184926,
    SPELL_OSCILLATION_FIELD                     = 37408,
    QUEST_GAUGING_THE_RESONANT_FREQUENCY        = 10594
};

enum Scratches
{
    SPELL_LASHHAN_CHANNELING     = 36904
};

class spell_bem_dispelling_analysis : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LASHHAN_CHANNELING });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_LASHHAN_CHANNELING);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_bem_dispelling_analysis::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ACurseUponBothOfYourClans
{
    NPC_OGRE_BUILDING_BUNNY_LARGE     = 21351
};

class spell_bem_wicked_strong_fetish : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->SummonCreature(NPC_OGRE_BUILDING_BUNNY_LARGE, caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30000);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_bem_wicked_strong_fetish::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheSmallestCreature
{
    SPELL_CHARM_REXXARS_RODENT          = 38586,
    SPELL_COAX_MARMOT                   = 38544,
    SPELL_STEALTH_MARMOT                = 42347,
    SPELL_GREEN_EYE_GROG_CREDIT         = 38996,
    SPELL_RIPE_MOONSHINE_CREDIT         = 38997,
    SPELL_FERMENTED_SEED_BEER_CREDIT    = 38998,

    NPC_MARMOT                          = 22189,
    NPC_GREEN_SPOT_GROG_KEG_CREDIT      = 22356,
    NPC_RIPE_MOONSHINE_KEG_CREDIT       = 22367,
    NPC_FERMENTED_SEED_BEER_KEG_CREDIT  = 22368
};

struct npc_q10720_keg_credit : public ScriptedAI
{
    npc_q10720_keg_credit(Creature * creature) : ScriptedAI(creature)
    {
        // Neccessary hack to allow spell 38629 to hit the keg credit (visibility is checked against the summoner, not the caster)
        creature->m_invisibilityDetect.AddFlag(INVISIBILITY_UNK4);
    }
};

struct npc_q10720_marmot : public ScriptedAI
{
    using ScriptedAI::ScriptedAI;

    void IsSummonedBy(Unit* summoner) override
    {
        summoner->CastSpell(me, SPELL_CHARM_REXXARS_RODENT, true);
    }
};

class spell_bem_coax_marmot : public AuraScript
{
    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // prevent loading the aura from db
        if (GetTarget()->GetCharmedGUID().IsEmpty())
            Remove();
    }

    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Creature* marmot = Object::ToCreature(GetTarget()->GetCharmed()))
            if (marmot->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_COAX_MARMOT)
                marmot->DespawnOrUnsummon();
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_bem_coax_marmot::HandleEffectApply, EFFECT_1, SPELL_AURA_MOD_INVISIBILITY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_bem_coax_marmot::HandleEffectRemove, EFFECT_1, SPELL_AURA_MOD_INVISIBILITY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_bem_charm_rexxars_rodent : public AuraScript
{
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
            caster->RemoveAurasDueToSpell(SPELL_COAX_MARMOT);

        if (Creature* creature = GetTarget()->ToCreature())
            creature->DespawnOrUnsummon(1ms);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_bem_charm_rexxars_rodent::OnRemove, EFFECT_0, SPELL_AURA_MOD_POSSESS, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_bem_q10720_poison_keg : public SpellScript
{
    bool Validate(SpellInfo const* /*spellEntry*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_GREEN_EYE_GROG_CREDIT,
            SPELL_RIPE_MOONSHINE_CREDIT,
            SPELL_FERMENTED_SEED_BEER_CREDIT
        });
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = GetCaster()->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            uint32 spellId = 0;
            switch (GetHitUnit()->GetEntry())
            {
                case NPC_GREEN_SPOT_GROG_KEG_CREDIT:
                    spellId = SPELL_GREEN_EYE_GROG_CREDIT;
                    break;
                case NPC_RIPE_MOONSHINE_KEG_CREDIT:
                    spellId = SPELL_RIPE_MOONSHINE_CREDIT;
                    break;
                case NPC_FERMENTED_SEED_BEER_KEG_CREDIT:
                    spellId = SPELL_FERMENTED_SEED_BEER_CREDIT;
                    break;
                default:
                    return;
            }

            player->CastSpell(nullptr, spellId, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_bem_q10720_poison_keg::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BombingRun
{
    SPELL_FEL_FLAK_FIRE       = 40075,
    SPELL_FLAK_CANNON_TRIGGER = 40110,
    SPELL_CHOOSE_LOC          = 40056,
    SPELL_AGGRO_CHECK         = 40112,

    NPC_FEL_CANNON2           = 23082
};

class spell_bem_kfc_the_bolt : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FEL_FLAK_FIRE });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_FEL_FLAK_FIRE, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_bem_kfc_the_bolt::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_bem_aggro_check_aura : public AuraScript
{
    void HandleTriggerSpell(AuraEffect const* /*aurEff*/)
    {
        if (Unit* target = GetTarget())
            // On trigger proccing
            target->CastSpell(target, SPELL_AGGRO_CHECK);
    }

    void Register() override
    {
       OnEffectPeriodic.Register(&spell_bem_aggro_check_aura::HandleTriggerSpell, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

class spell_bem_aggro_check : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* playerTarget = GetHitPlayer())
            // Check if found player target is on fly mount or using flying form
            if (playerTarget->HasAuraType(SPELL_AURA_FLY) || playerTarget->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
                playerTarget->CastSpell(playerTarget, SPELL_FLAK_CANNON_TRIGGER, TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_bem_aggro_check::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_bem_aggro_burst : public AuraScript
{
    void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
    {
        if (Unit* target = GetTarget())
            // On each tick cast Choose Loc to trigger summon
            target->CastSpell(target, SPELL_CHOOSE_LOC);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_bem_aggro_burst::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_bem_choose_loc : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        // Check for player that is in 65 y range
        std::vector<Player*> playerList;
        caster->GetPlayerListInGrid(playerList, 65.0f);
        for (Player* player : playerList)
            // Check if found player target is on fly mount or using flying form
            if (player->HasAuraType(SPELL_AURA_FLY) || player->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
                // Summom Fel Cannon (bunny version) at found player
                caster->SummonCreature(NPC_FEL_CANNON2, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_bem_choose_loc::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_bem_check_fly_mount : public SpellScript
{
    SpellCastResult CheckRequirement()
    {
        Unit* caster = GetCaster();
        // This spell will be cast only if caster has one of these auras
        if (!(caster->HasAuraType(SPELL_AURA_FLY) || caster->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)))
            return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;
        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_bem_check_fly_mount::CheckRequirement);
    }
};

enum ApexisSwiftness
{
    SPELL_APEXIS_VIBRATIONS               = 40623,
    SPELL_APEXIS_EMANATIONS               = 40625,
    SPELL_APEXIS_ENLIGHTENMENT            = 40626,
    SPELL_SWIFTNESS_APEXIS_VIBRATIONS     = 40624,
    SPELL_SWIFTNESS_APEXIS_EMANATIONS     = 40627,
    SPELL_SWIFTNESS_APEXIS_ENLIGHTENMENT  = 40628
};

class spell_bem_apexis_swiftness : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SWIFTNESS_APEXIS_VIBRATIONS,
            SPELL_SWIFTNESS_APEXIS_EMANATIONS,
            SPELL_SWIFTNESS_APEXIS_ENLIGHTENMENT
        });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        switch (GetId())
        {
            case SPELL_APEXIS_VIBRATIONS:
                GetTarget()->RemoveAurasDueToSpell(SPELL_SWIFTNESS_APEXIS_VIBRATIONS);
                break;
            case SPELL_APEXIS_EMANATIONS:
                GetTarget()->RemoveAurasDueToSpell(SPELL_SWIFTNESS_APEXIS_EMANATIONS);
                break;
            case SPELL_APEXIS_ENLIGHTENMENT:
                GetTarget()->RemoveAurasDueToSpell(SPELL_SWIFTNESS_APEXIS_ENLIGHTENMENT);
                break;
            default:
                break;
        }
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_bem_apexis_swiftness::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum VisionGuide
{
    SPELL_VISION_GUIDE     = 36573
};

class spell_bem_vision_guide : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_VISION_GUIDE });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_VISION_GUIDE, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_bem_vision_guide::AfterApply, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum OnSpiritsWings
{
    SPELL_REXXARS_BIRD_EFFECT   = 39074
};

class spell_bem_summon_spirit : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_REXXARS_BIRD_EFFECT });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        // This spell script requires sniff verification
        GetCaster()->CastSpell(GetCaster(), SPELL_REXXARS_BIRD_EFFECT, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_bem_summon_spirit::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};
}

void AddSC_blades_edge_mountains_cata()
{
    using namespace blades_edge_mountains_cata;
    RegisterSpellScript(spell_bem_dispelling_analysis);
    RegisterSpellScript(spell_bem_wicked_strong_fetish);
    RegisterCreatureAI(npc_q10720_keg_credit);
    RegisterCreatureAI(npc_q10720_marmot);
    RegisterSpellScript(spell_bem_coax_marmot);
    RegisterSpellScript(spell_bem_charm_rexxars_rodent);
    RegisterSpellScript(spell_bem_q10720_poison_keg);
    RegisterSpellScript(spell_bem_kfc_the_bolt);
    RegisterSpellScript(spell_bem_aggro_check_aura);
    RegisterSpellScript(spell_bem_aggro_check);
    RegisterSpellScript(spell_bem_aggro_burst);
    RegisterSpellScript(spell_bem_choose_loc);
    RegisterSpellScript(spell_bem_check_fly_mount);
    RegisterSpellScript(spell_bem_apexis_swiftness);
    RegisterSpellScript(spell_bem_vision_guide);
    RegisterSpellScript(spell_bem_summon_spirit);
}
