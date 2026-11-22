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
SDName: Netherstorm
SD%Complete: 80
SDComment: Quest support: 10337, 10652 (special flight paths), 10198, 10191
SDCategory: Netherstorm
EndScriptData */

/* ContentData
npc_commander_dawnforge
EndContentData */

#include "ScriptMgr.h"
#include "Containers.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

namespace netherstorm_cata
{
enum CommanderDawnforgeData
{
    SAY_COMMANDER_DAWNFORGE_1       = 0,
    SAY_COMMANDER_DAWNFORGE_2       = 1,
    SAY_COMMANDER_DAWNFORGE_3       = 2,
    SAY_COMMANDER_DAWNFORGE_4       = 3,
    SAY_COMMANDER_DAWNFORGE_5       = 4,

    SAY_ARCANIST_ARDONIS_1          = 0,
    SAY_ARCANIST_ARDONIS_2          = 1,

    SAY_PATHALEON_CULATOR_IMAGE_1   = 0,
    SAY_PATHALEON_CULATOR_IMAGE_2   = 1,
    SAY_PATHALEON_CULATOR_IMAGE_2_1 = 2,
    SAY_PATHALEON_CULATOR_IMAGE_2_2 = 3,

    QUEST_INFO_GATHERING            = 10198,
    SPELL_SUNFURY_DISGUISE          = 34603,
};

const uint32 CreatureEntry[3] =
{
    19830,                                                // Ardonis
    19831,                                                // Dawnforge
    21504                                                 // Pathaleon
};

enum PhaseHunterData
{
    QUEST_RECHARGING_THE_BATTERIES  = 10190,

    NPC_PHASE_HUNTER_ENTRY          = 18879,
    NPC_DRAINED_PHASE_HUNTER_ENTRY  = 19595,

    EMOTE_WEAK                      = 0,

    // Spells
    SPELL_RECHARGING_BATTERY        = 34219,
    SPELL_PHASE_SLIP                = 36574,
    SPELL_MANA_BURN                 = 13321,
    SPELL_MATERIALIZE               = 34804,
    SPELL_DE_MATERIALIZE            = 34814,
};

enum TeleportThis
{
    SPELL_TELEPORTER_KILL_CREDIT_1   = 38982,    // 22348
    SPELL_TELEPORTER_KILL_CREDIT_2   = 38983,    // 22351
    SPELL_TELEPORTER_KILL_CREDIT_3   = 38984,    // 22350
    NPC_WESTERN_TELEPORTER_CREDIT    = 22348,
    NPC_EASTERN_TELEPORTER_CREDIT    = 22351,
    NPC_CENTRAL_TELEPORTER_CREDIT    = 22350
};

class spell_netherstorm_detonate_teleporter : public SpellScript
{
    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_UNIT;
    }

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_TELEPORTER_KILL_CREDIT_1,
            SPELL_TELEPORTER_KILL_CREDIT_2,
            SPELL_TELEPORTER_KILL_CREDIT_3
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Creature* creature = GetHitCreature())
        {
            if (Unit* charmer = GetCaster()->GetCharmerOrOwner())
            {
                if (Player* player = charmer->ToPlayer())
                {
                    uint32 spellId = 0;

                    switch (creature->GetEntry())
                    {
                        case NPC_WESTERN_TELEPORTER_CREDIT:
                            spellId = SPELL_TELEPORTER_KILL_CREDIT_1;
                            break;
                        case NPC_EASTERN_TELEPORTER_CREDIT:
                            spellId = SPELL_TELEPORTER_KILL_CREDIT_2;
                            break;
                        case NPC_CENTRAL_TELEPORTER_CREDIT:
                            spellId = SPELL_TELEPORTER_KILL_CREDIT_3;
                            break;
                        default:
                            return;
                    }

                    player->CastSpell(player, spellId);
                }
            }
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_netherstorm_detonate_teleporter::HandleScript, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum DeathblowToTheLegion
{
    SPELL_SOCRETHAR_TO_SEAT          = 35743,
    SPELL_SOCRETHAR_FROM_SEAT        = 35744,

    AREA_INVASION_POINT_OVERLORD     = 3900,
    AREA_SOCRETHARS_SEAT             = 3742
};

class spell_netherstorm_socrethars_stone : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SOCRETHAR_TO_SEAT, SPELL_SOCRETHAR_FROM_SEAT });
    }

    void HandleDummy(SpellEffIndex /* effIndex */)
    {
        Unit* caster = GetCaster();
        switch (caster->GetAreaId())
        {
            case AREA_INVASION_POINT_OVERLORD:
                caster->CastSpell(caster, SPELL_SOCRETHAR_TO_SEAT);
                break;
            case AREA_SOCRETHARS_SEAT:
                caster->CastSpell(caster, SPELL_SOCRETHAR_FROM_SEAT);
                break;
            default:
                return;
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_netherstorm_socrethars_stone::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};
}

void AddSC_netherstorm_cata()
{
    using namespace netherstorm_cata;
    RegisterSpellScript(spell_netherstorm_detonate_teleporter);
    RegisterSpellScript(spell_netherstorm_socrethars_stone);
}
