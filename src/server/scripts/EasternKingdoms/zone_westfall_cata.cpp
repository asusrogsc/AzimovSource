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

#include "CombatAI.h"
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "SharedDefines.h"
#include "TemporarySummon.h"

namespace zone_westfall_cata
{
enum WestfallCata
{
    NPC_JANGOLODE_MINE_GLUBTOK      = 42492,
    NPC_JANGOLODE_MINE_FIGURE       = 42515,
    SPELL_JANGOLODE_SUMMON_FIGURE   = 79265,
    SPELL_JANGOLODE_SUMMON_GLUBTOK  = 79263,
};

// Le PNJ invoqué par ce joueur (remplace FindNearestCreatureWithOptions + OwnerGuid de cata_classic)
Creature* FindPlayerActor(Player* player, uint32 entry)
{
    std::list<Creature*> list;
    player->GetCreatureListWithEntryInGrid(list, entry, 50.0f);
    for (Creature* creature : list)
    {
        if (creature->GetOwnerGUID() == player->GetGUID())
            return creature;
        if (TempSummon* summon = creature->ToTempSummon())
            if (summon->GetSummonerGUID() == player->GetGUID())
                return creature;
    }
    return nullptr;
}

// 79262 - Summon Lou's House
class spell_westfall_summon_lous_house : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(nullptr, SPELL_JANGOLODE_SUMMON_FIGURE, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
        caster->CastSpell(nullptr, SPELL_JANGOLODE_SUMMON_GLUBTOK, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_westfall_summon_lous_house::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 79273 - Despawn Jangolode Actor
class spell_westfall_despawn_jangolode_actor : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        targets.clear();
        if (Creature* creature = FindPlayerActor(player, NPC_JANGOLODE_MINE_GLUBTOK))
            targets.push_back(creature);
        if (Creature* creature = FindPlayerActor(player, NPC_JANGOLODE_MINE_FIGURE))
            targets.push_back(creature);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_westfall_despawn_jangolode_actor::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

// 79297 - General Trigger 4: Glubtok
class spell_westfall_livin_the_life_ping_glubtok : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        targets.clear();
        if (Creature* creature = FindPlayerActor(player, NPC_JANGOLODE_MINE_GLUBTOK))
            targets.push_back(creature);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_westfall_livin_the_life_ping_glubtok::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

// 79287 - General Trigger 4: Figure
class spell_westfall_livin_the_life_ping_figure : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        targets.clear();
        if (Creature* creature = FindPlayerActor(player, NPC_JANGOLODE_MINE_FIGURE))
            targets.push_back(creature);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_westfall_livin_the_life_ping_figure::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

}

void AddSC_zone_westfall_cata()
{
    using namespace zone_westfall_cata;
    RegisterSpellScript(spell_westfall_summon_lous_house);
    RegisterSpellScript(spell_westfall_despawn_jangolode_actor);
    RegisterSpellScript(spell_westfall_livin_the_life_ping_glubtok);
    RegisterSpellScript(spell_westfall_livin_the_life_ping_figure);
}
