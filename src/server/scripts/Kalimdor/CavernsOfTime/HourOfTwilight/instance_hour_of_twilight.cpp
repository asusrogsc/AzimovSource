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
#include "InstanceScript.h"
#include "hour_of_twilight.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include <vector>

namespace HourOfTwilight
{
ObjectData const creatureData[] =
{
    { NPC_ARCURION, DATA_ARCURION }, // Azimov
    { NPC_ASIRA_DAWNSLAYER, DATA_ASIRA_DAWNSLAYER }, // Azimov
    { 0, 0  } // END
};

ObjectData const gameobjectData[] =
{
    { 0, 0 } // END
};

DoorData const doorData[] =
{
    { GO_ICEWALL_EXIT,      DATA_ARCURION, DOOR_TYPE_PASSAGE }, // Azimov
    { GO_ICEWALL_ENTRANCE,  DATA_ARCURION, DOOR_TYPE_ROOM    }, // Azimov
    { 0, 0, DOOR_TYPE_ROOM } // END
};

class instance_hour_of_twilight : public InstanceMapScript
{
public:
    instance_hour_of_twilight() : InstanceMapScript(HOTScriptName, 940) { }

    struct instance_hour_of_twilight_InstanceMapScript : public InstanceScript
    {
        instance_hour_of_twilight_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadDoorData(doorData);
            LoadObjectData(creatureData, gameobjectData);
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_hour_of_twilight_InstanceMapScript(map);
    }
};

// ============================================================================
// Azimov : Arcurion
// ============================================================================
namespace Arcurion
{
enum Spells
{
    SPELL_HAND_OF_FROST     = 102593,
    SPELL_CHAINS_OF_FROST   = 102582,
    SPELL_TORRENT_OF_FROST  = 104050
};

enum Texts
{
    SAY_INTRO   = 0,
    SAY_AGGRO   = 1,
    SAY_TORRENT = 2,
    SAY_SLAY    = 3,
    SAY_DEATH   = 4
};

enum Events
{
    EVENT_HAND_OF_FROST = 1,
    EVENT_CHAINS_OF_FROST,
    EVENT_SERVITORS,
    EVENT_TORRENT
};

struct boss_arcurion : public BossAI
{
    boss_arcurion(Creature* creature) : BossAI(creature, DATA_ARCURION), _introDone(false), _torrent(false) { }

    void JustAppeared() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
        SetCombatMovement(false); // reste sur place, comme sur retail
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_introDone && who->IsPlayer() && !who->ToPlayer()->IsGameMaster() && me->IsWithinDistInMap(who, 70.0f))
        {
            _introDone = true;
            Talk(SAY_INTRO);
        }

        BossAI::MoveInLineOfSight(who);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO, who);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        _torrent = false;
        events.ScheduleEvent(EVENT_HAND_OF_FROST, 2s);
        events.ScheduleEvent(EVENT_CHAINS_OF_FROST, 12s);
        events.ScheduleEvent(EVENT_SERVITORS, 5s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        summons.DespawnAll();
        _EnterEvadeMode(why);
        _DespawnAtEvade();
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        Talk(SAY_DEATH);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_SLAY, victim);
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        DoZoneInCombat(summon);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!_torrent && me->HealthBelowPctDamaged(30, damage))
        {
            _torrent = true;
            Talk(SAY_TORRENT);
            events.CancelEvent(EVENT_CHAINS_OF_FROST);
            events.CancelEvent(EVENT_SERVITORS);
            events.ScheduleEvent(EVENT_TORRENT, 1s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_HAND_OF_FROST:
                    DoCastVictim(SPELL_HAND_OF_FROST);
                    events.Repeat(3s);
                    break;
                case EVENT_CHAINS_OF_FROST:
                    DoCastAOE(SPELL_CHAINS_OF_FROST);
                    events.Repeat(20s);
                    break;
                case EVENT_SERVITORS:
                    SummonServitors();
                    events.Repeat(25s);
                    break;
                case EVENT_TORRENT:
                    DoCastAOE(SPELL_TORRENT_OF_FROST);
                    events.Repeat(7s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    // Deux Serviteurs gelés sortent de points d'apparition au hasard autour du canyon
    void SummonServitors()
    {
        std::list<Creature*> points;
        me->GetCreatureListWithEntryInGrid(points, NPC_FROZEN_SERVITOR_SPAWN_POINT, 120.0f);
        std::vector<Creature*> list(points.begin(), points.end());
        for (uint8 i = 0; i < 2 && !list.empty(); ++i)
        {
            uint32 const idx = urand(0, uint32(list.size() - 1));
            me->SummonCreature(NPC_FROZEN_SERVITOR, list[idx]->GetPosition(), TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);
            list.erase(list.begin() + idx);
        }
    }

    bool _introDone;
    bool _torrent;
};
}

// ============================================================================
// Azimov : Asira Aubetueuse
// ============================================================================
namespace Asira
{
enum Spells
{
    SPELL_MARK_OF_SILENCE       = 102726,
    SPELL_SILENCED              = 103587,
    SPELL_THROW_KNIFE           = 103597,
    SPELL_CHOKING_SMOKE_BOMB    = 103558,
    SPELL_BLADE_BARRIER         = 103419,
    SPELL_LESSER_BLADE_BARRIER  = 103562
};

enum Texts
{
    SAY_AGGRO   = 0,
    SAY_SMOKE   = 1,
    SAY_SLAY    = 2,
    SAY_DEATH   = 3
};

enum Events
{
    EVENT_MARK_OF_SILENCE = 1,
    EVENT_CHECK_MARKED,
    EVENT_SMOKE_BOMB,
    EVENT_THROW_KNIFE
};

struct boss_asira_dawnslayer : public BossAI
{
    boss_asira_dawnslayer(Creature* creature) : BossAI(creature, DATA_ASIRA_DAWNSLAYER), _barrier(false), _lesserBarrier(false) { }

    void JustAppeared() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
        if (me->IsMounted())
            me->Dismount();
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO, who);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        _barrier = false;
        _lesserBarrier = false;
        events.ScheduleEvent(EVENT_MARK_OF_SILENCE, 3s);
        events.ScheduleEvent(EVENT_CHECK_MARKED, 1s);
        events.ScheduleEvent(EVENT_SMOKE_BOMB, 15s);
        events.ScheduleEvent(EVENT_THROW_KNIFE, 8s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        me->RemoveAurasDueToSpell(SPELL_BLADE_BARRIER);
        me->RemoveAurasDueToSpell(SPELL_LESSER_BLADE_BARRIER);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_MARK_OF_SILENCE);
        summons.DespawnAll();
        _EnterEvadeMode(why);
        _DespawnAtEvade();
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        Talk(SAY_DEATH);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_MARK_OF_SILENCE);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_SLAY, victim);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        // 30 % : Barrière de lames ; une fois brisée, Barrière de lames inférieure
        if (!_barrier && me->HealthBelowPctDamaged(30, damage))
        {
            _barrier = true;
            DoCastSelf(SPELL_BLADE_BARRIER, true);
        }
        else if (_barrier && !_lesserBarrier && !me->HasAura(SPELL_BLADE_BARRIER))
        {
            _lesserBarrier = true;
            DoCastSelf(SPELL_LESSER_BLADE_BARRIER, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_MARK_OF_SILENCE:
                {
                    // Marque jusqu'à deux joueurs non marqués
                    std::list<Unit*> targets;
                    SelectTargetList(targets, 2, SELECT_TARGET_RANDOM, 0, 0.0f, true, false, -int32(SPELL_MARK_OF_SILENCE));
                    for (Unit* target : targets)
                        me->AddAura(SPELL_MARK_OF_SILENCE, target);
                    events.Repeat(25s);
                    break;
                }
                case EVENT_CHECK_MARKED:
                {
                    // Un joueur marqué qui incante reçoit un couteau et est réduit au silence
                    for (auto const& ref : me->GetMap()->GetPlayers())
                    {
                        Player* player = ref.GetSource();
                        if (!player || !player->IsAlive() || player->IsGameMaster() || !player->HasAura(SPELL_MARK_OF_SILENCE))
                            continue;
                        if (player->HasUnitState(UNIT_STATE_CASTING) && me->IsWithinDistInMap(player, 60.0f))
                        {
                            player->InterruptNonMeleeSpells(false);
                            DoCast(player, SPELL_THROW_KNIFE, true);
                            me->CastSpell(player, SPELL_SILENCED, true);
                            player->RemoveAurasDueToSpell(SPELL_MARK_OF_SILENCE);
                        }
                    }
                    events.Repeat(500ms);
                    break;
                }
                case EVENT_SMOKE_BOMB:
                    Talk(SAY_SMOKE);
                    DoCastSelf(SPELL_CHOKING_SMOKE_BOMB);
                    events.Repeat(25s);
                    break;
                case EVENT_THROW_KNIFE:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 50.0f, true))
                        DoCast(target, SPELL_THROW_KNIFE);
                    events.Repeat(10s, 14s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    bool _barrier;
    bool _lesserBarrier;
};
}
}

void AddSC_instance_hour_of_twilight()
{
    using namespace HourOfTwilight;
    new instance_hour_of_twilight();

    // Azimov : Arcurion
    using namespace HourOfTwilight::Arcurion;
    RegisterHourOfTwilightCreatureAI(boss_arcurion);

    // Azimov : Asira Aubetueuse
    using namespace HourOfTwilight::Asira;
    RegisterHourOfTwilightCreatureAI(boss_asira_dawnslayer);
}
