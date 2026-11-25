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

#include "MotionMaster.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "WaypointDefines.h"

namespace zone_mulgore_cata
{
enum EagleSpirit
{
    SPELL_EJECT_ALL_PASSENGERS = 50630,
    SPELL_SPIRIT_FORM          = 69324
};

class npc_eagle_spirit : public CreatureScript
{
public:
    npc_eagle_spirit() : CreatureScript("npc_eagle_spirit") { }

    struct npc_eagle_spirit_AI : public ScriptedAI
    {
        npc_eagle_spirit_AI(Creature* creature) : ScriptedAI(creature) { }

        void PassengerBoarded(Unit* /*who*/, int8 /*seatId*/, bool apply) override
        {
            if (!apply)
                return;

            // chemin de vol de cata_classic (WaypointPath construit ici : pas de chemin en base)
            static WaypointPath path(0, {
                WaypointNode(0, -2884.155f, -71.08681f, 242.0678f),
                WaypointNode(1, -2720.592f, -111.0035f, 242.5955f),
                WaypointNode(2, -2683.951f, -382.9010f, 231.1792f),
                WaypointNode(3, -2619.148f, -484.9288f, 231.1792f),
                WaypointNode(4, -2543.868f, -525.3333f, 231.1792f),
                WaypointNode(5, -2465.321f, -502.4896f, 190.7347f),
                WaypointNode(6, -2343.872f, -401.8281f, -8.320873f) });
            me->SetCanFly(true);
            me->SetDisableGravity(true);
            me->GetMotionMaster()->MovePath(path, false);
            me->CastSpell(me, SPELL_SPIRIT_FORM);
        }

        void WaypointPathEnded(uint32 /*pointId*/, uint32 /*pathId*/) override
        {
            DoCast(SPELL_EJECT_ALL_PASSENGERS);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_eagle_spirit_AI(creature);
    }
};
}

void AddSC_zone_mulgore_cata()
{
    using namespace zone_mulgore_cata;
    new npc_eagle_spirit();
}
