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
#include "Player.h"
#include "SpellScript.h"

namespace zone_blasted_lands_cata
{
enum YouAreRakhlikhDemon
{
    SPELL_TELEPORT_TO_RAZELIKH_GROUP    = 27694
};

class spell_blasted_lands_teleport_to_razelikh_group : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TELEPORT_TO_RAZELIKH_GROUP });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(nullptr, SPELL_TELEPORT_TO_RAZELIKH_GROUP, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_blasted_lands_teleport_to_razelikh_group::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TeleportToTanaan
{
    SPELL_TELEPORT_TO_TANAAN = 167771,

    MOVIE_INTO_THE_PORTAL    = 185
};
}

void AddSC_zone_blasted_lands_cata()
{
    using namespace zone_blasted_lands_cata;
    RegisterSpellScript(spell_blasted_lands_teleport_to_razelikh_group);
}
