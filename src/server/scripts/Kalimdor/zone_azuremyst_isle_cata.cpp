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
SDName: Azuremyst_Isle
SD%Complete: 75
SDComment: Quest support: 9283, 9537, 9582, 9554, ? (special flight path, proper model for mount missing). Injured Draenei cosmetic only, 9582.
SDCategory: Azuremyst Isle
EndScriptData */

/* ContentData
npc_draenei_survivor
npc_engineer_spark_overgrind
npc_injured_draenei
npc_magwin
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace zone_azuremyst_isle_cata
{
enum draeneiSurvivor
{
    SAY_THANK_FOR_HEAL     = 0,
    SAY_ASK_FOR_HELP       = 1,
    SPELL_IRRIDATION       = 35046,
    SPELL_STUNNED          = 28630,
    EVENT_CAN_ASK_FOR_HELP = 1,
    EVENT_THANK_PLAYER     = 2,
    EVENT_RUN_AWAY         = 3
};

Position const CrashSite = { -4115.25f, -13754.75f };

enum Overgrind
{
    SAY_TEXT        = 0,
    SAY_EMOTE       = 1,
    ATTACK_YELL     = 2,

    AREA_COVE       = 3579,
    AREA_ISLE       = 3639,
    QUEST_GNOMERCY  = 9537,
    SPELL_DYNAMITE  = 7978
};

enum Magwin
{
    SAY_START                   = 0,
    SAY_AGGRO                   = 1,
    SAY_PROGRESS                = 2,
    SAY_END1                    = 3,
    SAY_END2                    = 4,
    EMOTE_HUG                   = 5,
    NPC_COWLEN                  = 17311,
    SAY_COWLEN                  = 0,
    EVENT_ACCEPT_QUEST          = 1,
    EVENT_START_ESCORT          = 2,
    EVENT_STAND                 = 3,
    EVENT_TALK_END              = 4,
    EVENT_COWLEN_TALK           = 5,
    QUEST_A_CRY_FOR_HELP        = 9528,
    PATH_ESCORT_MAGWIN          = 138498,
};

enum Geezle
{
    QUEST_TREES_COMPANY = 9531,

    SPELL_TREE_DISGUISE = 30298,

    GEEZLE_SAY_1    = 0,
    SPARK_SAY_2     = 3,
    SPARK_SAY_3     = 4,
    GEEZLE_SAY_4    = 1,
    SPARK_SAY_5     = 5,
    SPARK_SAY_6     = 6,
    GEEZLE_SAY_7    = 2,

    EMOTE_SPARK     = 7,

    NPC_SPARK       = 17243,
    GO_NAGA_FLAG    = 181694
};

Position const SparkPos = {-5029.91f, -11291.79f, 8.096f, 0.0f};

class spell_inoculate_nestlewood : public AuraScript
{
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        if (GetTarget()->GetTypeId() != TYPEID_UNIT) // prevent error reports in case ignored player target
            PreventDefaultAction();
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_inoculate_nestlewood::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum RedSnapperVeryTasty
{
    SPELL_FISHED_UP_RED_SNAPPER  = 29867,
    SPELL_FISHED_UP_MURLOC       = 29869
};

class spell_azuremyst_isle_cast_fishing_net : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FISHED_UP_RED_SNAPPER, SPELL_FISHED_UP_MURLOC });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), roll_chance_i(66) ? SPELL_FISHED_UP_RED_SNAPPER : SPELL_FISHED_UP_MURLOC);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_azuremyst_isle_cast_fishing_net::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum TotemOfVark
{
    SPELL_SHADOW_OF_THE_FOREST_SI_DND     = 32213
};

class spell_azuremyst_isle_shadow_of_the_forest_creature : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOW_OF_THE_FOREST_SI_DND });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_SHADOW_OF_THE_FOREST_SI_DND, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_azuremyst_isle_shadow_of_the_forest_creature::AfterApply, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_azuremyst_isle_shadow_of_the_forest_player : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOW_OF_THE_FOREST_SI_DND });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(GetTarget(), SPELL_SHADOW_OF_THE_FOREST_SI_DND, true);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_SHADOW_OF_THE_FOREST_SI_DND);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_azuremyst_isle_shadow_of_the_forest_player::AfterApply, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_azuremyst_isle_shadow_of_the_forest_player::AfterRemove, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};
}

void AddSC_zone_azuremyst_isle_cata()
{
    using namespace zone_azuremyst_isle_cata;
    RegisterSpellScript(spell_inoculate_nestlewood);
    RegisterSpellScript(spell_azuremyst_isle_cast_fishing_net);
    RegisterSpellScript(spell_azuremyst_isle_shadow_of_the_forest_creature);
    RegisterSpellScript(spell_azuremyst_isle_shadow_of_the_forest_player);
}
