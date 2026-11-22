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
SDName: Shadowmoon_Valley
SD%Complete: 100
SDComment: Quest support: 10804, 10854, 10458, 10481, 10480, 10781, 10451.
SDCategory: Shadowmoon Valley
EndScriptData */

/* ContentData
npc_invis_infernal_caster
npc_infernal_attacker
npc_mature_netherwing_drake
npc_enslaved_netherwing_drake
npc_earthmender_wilda
npc_torloth_the_magnificent
npc_illidari_spawn
npc_lord_illidan_stormrage
go_crystal_prison
npc_enraged_spirit
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace shadowmoon_valley_cata
{
enum InvisInfernalCaster
{
    EVENT_CAST_SUMMON_INFERNAL = 1,
    NPC_INFERNAL_ATTACKER      = 21419,
    MODEL_INVISIBLE            = 20577,
    MODEL_INFERNAL             = 17312,
    SPELL_SUMMON_INFERNAL      = 37277,
    SPELL_SPAWN_AND_PACIFY     = 37791,
    SPELL_TRANSFORM_INFERNAL   = 37794,
    TYPE_INFERNAL              = 1,
    DATA_DIED                  = 1
};

enum MatureNetherwing
{
    SAY_JUST_EATEN              = 0,

    SPELL_PLACE_CARCASS         = 38439,
    SPELL_JUST_EATEN            = 38502,
    SPELL_NETHER_BREATH         = 38467,
    POINT_ID                    = 1,

    GO_CARCASS                  = 185155,

    QUEST_KINDNESS              = 10804,
    NPC_EVENT_PINGER            = 22131
};

enum EnshlavedNetherwingDrake
{
    // Spells
    SPELL_HIT_FORCE_OF_NELTHARAKU   = 38762,
    SPELL_FORCE_OF_NELTHARAKU       = 38775,

    // Creatures
    NPC_DRAGONMAW_SUBJUGATOR        = 21718,
    NPC_ESCAPE_DUMMY                = 22317,

    // Quests
    QUEST_THE_FORCE_OF_NELTHARAKU   = 10854,

    // Movement
    POINT_MOVE_UP                   = 1
};

enum Earthmender
{
    SAY_WIL_START               = 0,
    SAY_WIL_AGGRO               = 1,
    SAY_WIL_PROGRESS1           = 2,
    SAY_WIL_PROGRESS2           = 3,
    SAY_WIL_FIND_EXIT           = 4,
    SAY_WIL_JUST_AHEAD          = 5,
    SAY_WIL_END                 = 6,

    SPELL_CHAIN_LIGHTNING       = 16006,
    SPELL_EARTHBING_TOTEM       = 15786,
    SPELL_FROST_SHOCK           = 12548,
    SPELL_HEALING_WAVE          = 12491,

    QUEST_ESCAPE_COILSCAR       = 10451,
    NPC_COILSKAR_ASSASSIN       = 21044,

    PATH_ESCORT_WILDA           = 168218,
};

enum Enraged_Dpirits
{
    // QUESTS
    QUEST_ENRAGED_SPIRITS_FIRE_EARTH        = 10458,
    QUEST_ENRAGED_SPIRITS_AIR               = 10481,
    QUEST_ENRAGED_SPIRITS_WATER             = 10480,

    // Totem
    ENTRY_TOTEM_OF_SPIRITS                  = 21071,
    RADIUS_TOTEM_OF_SPIRITS                 = 15,

    // SPIRITS
    NPC_ENRAGED_EARTH_SPIRIT                = 21050,
    NPC_ENRAGED_FIRE_SPIRIT                 = 21061,
    NPC_ENRAGED_AIR_SPIRIT                  = 21060,
    NPC_ENRAGED_WATER_SPIRIT                = 21059,

    // ENRAGED WATER SPIRIT SPELLS
    SPELL_STORMBOLT                         = 38032,

    // ENRAGED AIR SPIRIT SPELLS
    SPELL_AIR_SPIRIT_CHAIN_LIGHTNING        = 12058,
    SPELL_HURRICANE                         = 32717,
    SPELL_ENRAGE                            = 8599,

    // ENRAGED FIRE SPIRIT SPELLS - Will be using the enrage spell from Air Spirit
    SPELL_FEL_FIREBALL                      = 36247,
    SPELL_FEL_FIRE_AURA                     = 36006, // Earth spirit uses this one

    // ENRAGED EARTH SPIRIT SPELLS
    SPELL_FIERY_BOULDER                     = 38498,
    SPELL_SUMMON_ENRAGED_EARTH_SHARD        = 38365,

    // SOULS
    NPC_EARTHEN_SOUL                        = 21073,
    NPC_FIERY_SOUL                          = 21097,
    NPC_ENRAGED_AIRY_SOUL                   = 21116,
    NPC_ENRAGED_WATERY_SOUL                 = 21109, // wrong model

    // SPELL KILLCREDIT - not working!?! - using KilledMonsterCredit
    SPELL_EARTHEN_SOUL_CAPTURED_CREDIT      = 36108,
    SPELL_FIERY_SOUL_CAPTURED_CREDIT        = 36117,
    SPELL_AIRY_SOUL_CAPTURED_CREDIT         = 36182,
    SPELL_WATERY_SOUL_CAPTURED_CREDIT       = 36171,

    // KilledMonsterCredit Workaround
    NPC_CREDIT_FIRE                         = 21094,
    NPC_CREDIT_WATER                        = 21095,
    NPC_CREDIT_AIR                          = 21096,
    NPC_CREDIT_EARTH                        = 21092,

    // Captured Spell / Buff
    SPELL_SOUL_CAPTURED                     = 36115
};

enum Enraged_Spirits_Events
{
    EVENT_ENRAGED_WATER_SPIRIT                  = 1,
    EVENT_ENRAGED_FIRE_SPIRIT                   = 2,
    EVENT_ENRAGED_EARTH_SPIRIT                  = 3,
    EVENT_ENRAGED_AIR_SPIRIT_CHAIN_LIGHTNING    = 4,
    EVENT_ENRAGED_AIR_SPIRIT_HURRICANE          = 5
};

enum ZuluhedChains
{
    NPC_KARYNAKU    = 22112,
};

enum ShadowMoonTuberEnum
{
    SPELL_WHISTLE               = 36652,
    SPELL_SHADOWMOON_TUBER      = 36462,

    NPC_BOAR_ENTRY              = 21195,
    GO_SHADOWMOON_TUBER_MOUND   = 184701,

    POINT_TUBER                 = 1,
    TYPE_BOAR                   = 1,
    DATA_BOAR                   = 1
};

enum DissensionAmongstTheRanks
{
    SPELL_ILLIDARI_DISGUISE_MALE          = 38225,
    SPELL_ILLIDARI_DISGUISE_FEMALE        = 38227,
    SPELL_KILL_CREDIT_CRAZED_COLOSSUS     = 38228
};

class spell_shadowmoon_illidari_agent_illusion : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ILLIDARI_DISGUISE_MALE, SPELL_ILLIDARI_DISGUISE_FEMALE });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
            target->CastSpell(target, target->getGender() == GENDER_MALE ?
            SPELL_ILLIDARI_DISGUISE_MALE : SPELL_ILLIDARI_DISGUISE_FEMALE);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAurasDueToSpell(SPELL_ILLIDARI_DISGUISE_MALE);
        target->RemoveAurasDueToSpell(SPELL_ILLIDARI_DISGUISE_FEMALE);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_shadowmoon_illidari_agent_illusion::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_shadowmoon_illidari_agent_illusion::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_shadowmoon_quest_credit_crazed_colossus : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo(
        {
            uint32(spellInfo->Effects[EFFECT_0].CalcValue()),
            SPELL_KILL_CREDIT_CRAZED_COLOSSUS
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        if (target->HasAura(uint32(GetEffectValue())))
            target->CastSpell(target, SPELL_KILL_CREDIT_CRAZED_COLOSSUS);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_shadowmoon_quest_credit_crazed_colossus::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ANecessaryDistraction
{
    SPELL_BANISH_AZALOTH     = 37833
};

class spell_shadowmoon_unbanish_azaloth : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BANISH_AZALOTH });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_BANISH_AZALOTH);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_shadowmoon_unbanish_azaloth::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum FranklyItMakesNoSense
{
    SPELL_ARCANO_SCORP_CONTROL_01     = 37868,
    SPELL_ARCANO_SCORP_CONTROL_02     = 37893,
    SPELL_ARCANO_SCORP_CONTROL_03     = 37895
};

class spell_shadowmoon_arcano_scorp_control : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), _triggeredSpellId);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_shadowmoon_arcano_scorp_control::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }

    uint32 _triggeredSpellId;

public:
    explicit spell_shadowmoon_arcano_scorp_control(uint32 triggeredSpellId) : _triggeredSpellId(triggeredSpellId) { }
};
}

void AddSC_shadowmoon_valley_cata()
{
    using namespace shadowmoon_valley_cata;
    RegisterSpellScript(spell_shadowmoon_illidari_agent_illusion);
    RegisterSpellScript(spell_shadowmoon_quest_credit_crazed_colossus);
    RegisterSpellScript(spell_shadowmoon_unbanish_azaloth);
    RegisterSpellScriptWithArgs(spell_shadowmoon_arcano_scorp_control, "spell_shadowmoon_arcano_scorp_control_01", SPELL_ARCANO_SCORP_CONTROL_01);
    RegisterSpellScriptWithArgs(spell_shadowmoon_arcano_scorp_control, "spell_shadowmoon_arcano_scorp_control_02", SPELL_ARCANO_SCORP_CONTROL_02);
    RegisterSpellScriptWithArgs(spell_shadowmoon_arcano_scorp_control, "spell_shadowmoon_arcano_scorp_control_03", SPELL_ARCANO_SCORP_CONTROL_03);
}
