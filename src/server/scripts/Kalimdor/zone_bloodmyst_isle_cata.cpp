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
#include "CellImpl.h"
#include "Containers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "SpellScript.h"

namespace zone_bloodmyst_isle_cata
{
enum EndingTheirWorldMisc
{
    SAY_SIRONAS_1                   = 0,

    SAY_LEGOSO_1                    = 0,
    SAY_LEGOSO_2                    = 1,
    SAY_LEGOSO_3                    = 2,
    SAY_LEGOSO_4                    = 3,
    SAY_LEGOSO_5                    = 4,
    SAY_LEGOSO_6                    = 5,
    SAY_LEGOSO_7                    = 6,
    SAY_LEGOSO_8                    = 7,
    SAY_LEGOSO_9                    = 8,
    SAY_LEGOSO_10                   = 9,
    SAY_LEGOSO_11                   = 10,
    SAY_LEGOSO_12                   = 11,
    SAY_LEGOSO_13                   = 12,
    SAY_LEGOSO_14                   = 13,
    SAY_LEGOSO_15                   = 14,
    SAY_LEGOSO_16                   = 15,
    SAY_LEGOSO_17                   = 16,
    SAY_LEGOSO_18                   = 17,
    SAY_LEGOSO_19                   = 18,
    SAY_LEGOSO_20                   = 19,
    SAY_LEGOSO_21                   = 20,

    SPELL_BLOODMYST_TESLA           = 31611,
    SPELL_SIRONAS_CHANNELING        = 31612,

    SPELL_UPPERCUT                  = 10966,
    SPELL_IMMOLATE                  = 12742,
    SPELL_CURSE_OF_BLOOD            = 8282,

    SPELL_FROST_SHOCK               = 8056,
    SPELL_HEALING_SURGE             = 8004,
    SPELL_SEARING_TOTEM             = 38116,
    SPELL_STRENGTH_OF_EARTH_TOTEM   = 31633,

    NPC_SIRONAS                     = 17678,
    NPC_BLOODMYST_TESLA_COIL        = 17979,
    NPC_LEGOSO                      = 17982,

    GO_DRAENEI_EXPLOSIVES_1         = 182088,
    GO_DRAENEI_EXPLOSIVES_2         = 182091,
    GO_FIRE_EXPLOSION               = 182071,

    ACTION_SIRONAS_CHANNEL_START    = 1,
    ACTION_SIRONAS_CHANNEL_STOP     = 2,

    ACTION_LEGOSO_SIRONAS_KILLED    = 1,

    EVENT_UPPERCUT                  = 1,
    EVENT_IMMOLATE                  = 2,
    EVENT_CURSE_OF_BLOOD            = 3,

    EVENT_FROST_SHOCK               = 1,
    EVENT_HEALING_SURGE             = 2,
    EVENT_SEARING_TOTEM             = 3,
    EVENT_STRENGTH_OF_EARTH_TOTEM   = 4,

    WP_START = 1,
    WP_EXPLOSIVES_FIRST_POINT       = 21,
    WP_EXPLOSIVES_FIRST_PLANT       = 22,
    WP_EXPLOSIVES_FIRST_RUNOFF      = 23,
    WP_EXPLOSIVES_FIRST_DETONATE    = 24,
    WP_DEBUG_1                      = 25,
    WP_DEBUG_2                      = 26,
    WP_SIRONAS_HILL                 = 33,
    WP_EXPLOSIVES_SECOND_BATTLEROAR = 35,
    WP_EXPLOSIVES_SECOND_PLANT      = 39,
    WP_EXPLOSIVES_SECOND_DETONATE   = 40,

    PHASE_NONE                      = 0,
    PHASE_CONTINUE                  = -1,
    PHASE_WP_26                     = 1,
    PHASE_WP_22                     = 2,
    PHASE_PLANT_FIRST_KNEEL         = 3,
    PHASE_PLANT_FIRST_STAND         = 4,
    PHASE_PLANT_FIRST_WORK          = 5,
    PHASE_PLANT_FIRST_FINISH        = 6,
    PHASE_PLANT_FIRST_TIMER_1       = 7,
    PHASE_PLANT_FIRST_TIMER_2       = 8,
    PHASE_PLANT_FIRST_TIMER_3       = 9,
    PHASE_PLANT_FIRST_DETONATE      = 10,
    PHASE_PLANT_FIRST_SPEECH        = 11,
    PHASE_PLANT_FIRST_ROTATE        = 12,
    PHASE_PLANT_FIRST_POINT         = 13,
    PHASE_FEEL_SIRONAS_1            = 14,
    PHASE_FEEL_SIRONAS_2            = 15,
    PHASE_MEET_SIRONAS_ROAR         = 16,
    PHASE_MEET_SIRONAS_TURN         = 17,
    PHASE_MEET_SIRONAS_SPEECH       = 18,
    PHASE_PLANT_SECOND_KNEEL        = 19,
    PHASE_PLANT_SECOND_SPEECH       = 20,
    PHASE_PLANT_SECOND_STAND        = 21,
    PHASE_PLANT_SECOND_FINISH       = 22,
    PHASE_PLANT_SECOND_WAIT         = 23,
    PHASE_PLANT_SECOND_TIMER_1      = 24,
    PHASE_PLANT_SECOND_TIMER_2      = 25,
    PHASE_PLANT_SECOND_TIMER_3      = 26,
    PHASE_PLANT_SECOND_DETONATE     = 27,
    PHASE_FIGHT_SIRONAS_STOP        = 28,
    PHASE_FIGHT_SIRONAS_SPEECH_1    = 29,
    PHASE_FIGHT_SIRONAS_SPEECH_2    = 30,
    PHASE_FIGHT_SIRONAS_START       = 31,
    PHASE_SIRONAS_SLAIN_SPEECH_1    = 32,
    PHASE_SIRONAS_SLAIN_EMOTE_1     = 33,
    PHASE_SIRONAS_SLAIN_EMOTE_2     = 34,
    PHASE_SIRONAS_SLAIN_SPEECH_2    = 35,

    DATA_EVENT_STARTER_GUID         = 0,

    MAX_EXPLOSIVES                  = 5,

    QUEST_ENDING_THEIR_WORLD        = 9759
};

Position const ExplosivesPos[2][MAX_EXPLOSIVES] =
{
    {
        { -1954.946f, -10654.714f, 110.448f },
        { -1956.331f, -10654.494f, 110.869f },
        { -1955.906f, -10656.221f, 110.791f },
        { -1957.294f, -10656.000f, 111.219f },
        { -1954.462f, -10656.451f, 110.404f }
    },
    {
        { -1915.137f, -10583.651f, 178.365f },
        { -1914.006f, -10582.964f, 178.471f },
        { -1912.717f, -10582.398f, 178.658f },
        { -1915.056f, -10582.251f, 178.162f },
        { -1913.883f, -10581.778f, 178.346f }
    }
};

static constexpr uint32 PATH_ESCORT_LEGOSO = 143858;

enum FreeWebbedBloodmyst
{
    SPELL_FREE_WEBBED_1      = 30954,
    SPELL_FREE_WEBBED_2      = 30955,
    SPELL_FREE_WEBBED_3      = 30956,
    SPELL_FREE_WEBBED_4      = 30957,
    SPELL_FREE_WEBBED_5      = 30958,
    SPELL_FREE_WEBBED_6      = 30959,
    SPELL_FREE_WEBBED_7      = 30960,
    SPELL_FREE_WEBBED_8      = 30961,
    SPELL_FREE_WEBBED_9      = 30962,
    SPELL_FREE_WEBBED_10     = 30963,
    SPELL_FREE_WEBBED_11     = 31010
};

uint32 const CocoonSummonSpells[10] =
{
    SPELL_FREE_WEBBED_1, SPELL_FREE_WEBBED_2, SPELL_FREE_WEBBED_3, SPELL_FREE_WEBBED_4, SPELL_FREE_WEBBED_5,
    SPELL_FREE_WEBBED_6, SPELL_FREE_WEBBED_7, SPELL_FREE_WEBBED_8, SPELL_FREE_WEBBED_9, SPELL_FREE_WEBBED_10
};

class spell_free_webbed : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_free_webbed::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_free_webbed_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREE_WEBBED_11 });
    }

    // This one is a bit different from the one used in Terokkar. There is additional spell 31011 which apply periodic aura to trigger
    // summon spell 31010 after 1.5 sec. However in retail Expedition Researcher is summoned instantly, we'll use 31010 directly
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        if (roll_chance_i(66))
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            target->CastSpell(caster, SPELL_FREE_WEBBED_11, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_free_webbed_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};
}

void AddSC_zone_bloodmyst_isle_cata()
{
    using namespace zone_bloodmyst_isle_cata;
    RegisterSpellScript(spell_free_webbed);
    RegisterSpellScript(spell_free_webbed_on_quest);
}
