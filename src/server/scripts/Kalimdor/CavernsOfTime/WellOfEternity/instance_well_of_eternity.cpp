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
#include "well_of_eternity.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

namespace WellOfEternity
{
ObjectData const creatureData[] =
{
    { NPC_PEROTHARN,    BOSS_PEROTHARN  },
    { 0,                0               } // END
};

ObjectData const gameobjectData[] =
{
    { GO_WOE_COURTYARD_DOOR01,  DATA_WOE_COURTYARD_DOOR01   },
    { 0,                        0                           } // END
};

DoorData const doorData[] =
{
    { GO_LARGE_FIREWALL_DOOR,   BOSS_PEROTHARN, DOOR_TYPE_ROOM },
    { GO_SMALL_FIREWALL_DOOR,   BOSS_PEROTHARN, DOOR_TYPE_ROOM },
    { 0,                        0,              DOOR_TYPE_ROOM } // END
};

class instance_well_of_eternity : public InstanceMapScript
{
public:
    instance_well_of_eternity() : InstanceMapScript(WOEScriptName, 939) { }

    struct instance_well_of_eternity_InstanceMapScript : public InstanceScript
    {
        instance_well_of_eternity_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadDoorData(doorData);
            LoadObjectData(creatureData, gameobjectData);
        }

        // Azimov : l'événement d'Illidan (portails) n'est pas scripté, on ouvre directement l'accès à la cour
        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_WOE_COURTYARD_DOOR01:
                case GO_INVISIBLE_FIRE_WALL:
                    go->SetGoState(GO_STATE_ACTIVE);
                    break;
                default:
                    break;
            }
        }

        // Azimov : quête « Cartographier les voies temporelles » (Calice d'Alurmi)
        void OnUnitDeath(Unit* unit) override
        {
            if (!unit->IsCreature())
                return;

            uint32 credit = 0;
            switch (unit->GetEntry())
            {
                case NPC_LEGION_DEMON:
                case NPC_LEGION_DEMON_COURTYARD:
                    credit = NPC_CREDIT_LEGION_DEMON_ARCHIVED;
                    break;
                case NPC_ROYAL_HANDMAIDEN:
                    credit = NPC_CREDIT_ROYAL_HANDMAIDEN_ARCHIVED;
                    break;
                case NPC_CAPTAIN_VAROTHEN:
                    credit = NPC_CREDIT_CAPTAIN_VAROTHEN_ARCHIVED;
                    break;
                default:
                    return;
            }

            for (MapReference const& ref : instance->GetPlayers())
                if (Player* player = ref.GetSource())
                    if (player->IsAlive() || player->GetDistance(unit) < 100.0f)
                        player->KilledMonsterCredit(credit);
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_well_of_eternity_InstanceMapScript(map);
    }
};

// ============================================================================
// Azimov : Peroth'arn
// ============================================================================
namespace Perotharn
{
enum Spells
{
    SPELL_FEL_FLAMES        = 108141,
    SPELL_FEL_DECAY         = 105544,
    SPELL_CORRUPTING_TOUCH  = 104939,
    SPELL_CAMOUFLAGE        = 105341, // Dissimulation (invisible)
    SPELL_EASY_PREY         = 105493, // Proie facile : un Œil repère un joueur
    SPELL_FEL_QUICKNESS     = 105526, // Vivacité gangrenée
    SPELL_ENDLESS_FRENZY    = 105521  // Frénésie infinie (20 %)
};

enum Texts
{
    SAY_INTRO   = 0,
    SAY_AGGRO   = 1,
    SAY_HIDE    = 2,
    SAY_FOUND   = 3,
    SAY_FRENZY  = 4,
    SAY_SLAY    = 5,
    SAY_DEATH   = 6
};

enum Events
{
    EVENT_FEL_FLAMES = 1,
    EVENT_FEL_DECAY,
    EVENT_END_HUNT
};

enum Misc
{
    NPC_EYE_OF_PEROTHARN    = 55868,
    DATA_PREY_FOUND         = 1,
    EYE_COUNT               = 6
};

struct boss_perotharn : public BossAI
{
    boss_perotharn(Creature* creature) : BossAI(creature, BOSS_PEROTHARN), _introDone(false), _hunted(false), _hunting(false), _frenzy(false) { }

    void JustAppeared() override
    {
        // Sans l'événement d'Illidan, Peroth'arn est attaquable directement
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_introDone && who->IsPlayer() && !who->ToPlayer()->IsGameMaster() && me->IsWithinDistInMap(who, 60.0f))
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
        DoCastSelf(SPELL_CORRUPTING_TOUCH, true);
        ScheduleCombatEvents();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        summons.DespawnAll();
        me->RemoveAurasDueToSpell(SPELL_CAMOUFLAGE);
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        _hunted = _hunting = _frenzy = false;
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
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (_hunting)
        {
            damage = 0;
            return;
        }

        if (!_hunted && me->HealthBelowPctDamaged(70, damage))
        {
            _hunted = true;
            StartHunt();
        }
        else if (!_frenzy && me->HealthBelowPctDamaged(20, damage))
        {
            _frenzy = true;
            Talk(SAY_FRENZY);
            DoCastSelf(SPELL_ENDLESS_FRENZY, true);
        }
    }

    // Un Œil a repéré un joueur
    void SetGUID(ObjectGuid const& guid, int32 type) override
    {
        if (type != DATA_PREY_FOUND || !_hunting)
            return;

        EndHunt(ObjectAccessor::GetUnit(*me, guid));
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (!_hunting && me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FEL_FLAMES:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
                        DoCast(target, SPELL_FEL_FLAMES);
                    events.Repeat(8s + 500ms);
                    break;
                case EVENT_FEL_DECAY:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
                        DoCast(target, SPELL_FEL_DECAY);
                    events.Repeat(17s);
                    break;
                case EVENT_END_HUNT:
                    EndHunt(nullptr);
                    break;
                default:
                    break;
            }

            if (!_hunting && me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (!_hunting)
            DoMeleeAttackIfReady();
    }

private:
    void ScheduleCombatEvents()
    {
        events.ScheduleEvent(EVENT_FEL_FLAMES, 8s);
        events.ScheduleEvent(EVENT_FEL_DECAY, 14s);
    }

    // 70 % : Peroth'arn disparaît, ses Yeux fouillent la cour pendant 40 s
    void StartHunt()
    {
        _hunting = true;
        events.Reset();
        Talk(SAY_HIDE);
        me->InterruptNonMeleeSpells(false);
        me->AttackStop();
        me->SetReactState(REACT_PASSIVE);
        me->GetMotionMaster()->Clear();
        me->StopMoving();
        DoCastSelf(SPELL_CAMOUFLAGE, true);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);

        for (uint8 i = 0; i < EYE_COUNT; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(EYE_COUNT);
            float const dist = frand(8.0f, 22.0f);
            Position pos = me->GetPosition();
            pos.m_positionX += std::cos(angle) * dist;
            pos.m_positionY += std::sin(angle) * dist;
            me->UpdateGroundPositionZ(pos.m_positionX, pos.m_positionY, pos.m_positionZ);
            me->SummonCreature(NPC_EYE_OF_PEROTHARN, pos, TEMPSUMMON_TIMED_DESPAWN, 45000);
        }

        events.ScheduleEvent(EVENT_END_HUNT, 40s);
    }

    void EndHunt(Unit* prey)
    {
        if (!_hunting)
            return;

        _hunting = false;
        events.Reset();
        summons.DespawnEntry(NPC_EYE_OF_PEROTHARN);
        me->RemoveAurasDueToSpell(SPELL_CAMOUFLAGE);
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetReactState(REACT_AGGRESSIVE);

        if (prey && prey->IsAlive())
        {
            Talk(SAY_FOUND, prey);
            me->NearTeleportTo(prey->GetPositionX(), prey->GetPositionY(), prey->GetPositionZ(), me->GetAngle(prey));
            DoCastSelf(SPELL_FEL_QUICKNESS, true);
            AddThreat(prey, 1000000.0f);
            AttackStart(prey);
        }
        else if (Unit* target = SelectTarget(SELECT_TARGET_MAXTHREAT, 0, 100.0f, true))
            AttackStart(target);

        ScheduleCombatEvents();
    }

    bool _introDone;
    bool _hunted;
    bool _hunting;
    bool _frenzy;
};

// 55868 - Œil de Peroth'arn : erre dans la cour, le premier joueur qu'il approche est repéré
struct npc_perotharn_eye : public NullCreatureAI
{
    npc_perotharn_eye(Creature* creature) : NullCreatureAI(creature), _checkTimer(1000), _found(false) { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        me->SetWalk(true);
        me->GetMotionMaster()->MoveRandom(15.0f);
    }

    void UpdateAI(uint32 diff) override
    {
        if (_found)
            return;

        if (_checkTimer > diff)
        {
            _checkTimer -= diff;
            return;
        }
        _checkTimer = 500;

        for (MapReference const& ref : me->GetMap()->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (!player || !player->IsAlive() || player->IsGameMaster() || !me->IsWithinDist(player, 4.0f))
                continue;

            _found = true;
            me->CastSpell(player, SPELL_EASY_PREY, true);
            if (InstanceScript* instance = me->GetInstanceScript())
                if (Creature* perotharn = instance->GetCreature(BOSS_PEROTHARN))
                    if (perotharn->IsAIEnabled())
                        perotharn->AI()->SetGUID(player->GetGUID(), DATA_PREY_FOUND);
            return;
        }
    }

private:
    uint32 _checkTimer;
    bool _found;
};
}
}

void AddSC_instance_well_of_eternity()
{
    using namespace WellOfEternity;
    new instance_well_of_eternity();

    // Azimov : Peroth'arn
    using namespace WellOfEternity::Perotharn;
    RegisterWellOfEternityCreatureAI(boss_perotharn);
    RegisterWellOfEternityCreatureAI(npc_perotharn_eye);
}
