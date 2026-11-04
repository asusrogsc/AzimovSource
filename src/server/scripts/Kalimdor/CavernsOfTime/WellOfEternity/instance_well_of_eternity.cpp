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
#include "SpellScript.h"
#include "GameObjectAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include <vector>

namespace WellOfEternity
{
ObjectData const creatureData[] =
{
    { NPC_PEROTHARN,    BOSS_PEROTHARN  },
    { NPC_QUEEN_AZSHARA, BOSS_QUEEN_AZSHARA }, // Azimov
    { NPC_MANNOROTH,        DATA_MANNOROTH      }, // Azimov
    { NPC_CAPTAIN_VAROTHEN, DATA_VAROTHEN       }, // Azimov
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

// ============================================================================
// Azimov : Reine Azshara (rencontre des Mages enchantés)
// ============================================================================
namespace QueenAzshara
{
enum Spells
{
    SPELL_TOTAL_OBEDIENCE   = 103241,

    // Mage de feu
    SPELL_FIREBALL          = 102265,
    SPELL_FIREBOMB          = 102263,
    // Mage de givre
    SPELL_ICE_FLING         = 102478,
    SPELL_COLDFLAME         = 102465,
    SPELL_BLADES_OF_ICE     = 102476,
    // Mage des Arcanes
    SPELL_ARCANE_SHOCK      = 102463,
    SPELL_ARCANE_BOMB       = 102455
};

enum Texts
{
    SAY_AGGRO       = 0,
    SAY_WAVE_1      = 1,
    SAY_WAVE_2      = 2,
    SAY_WAVE_3      = 3,
    SAY_OBEDIENCE   = 4,
    SAY_SLAY        = 5,
    SAY_END         = 6
};

enum Events
{
    EVENT_CHECK_MAGI = 1,
    EVENT_NEXT_WAVE,
    EVENT_TOTAL_OBEDIENCE,
    EVENT_DESPAWN,

    // Mages
    EVENT_SPELL_1,
    EVENT_SPELL_2,
    EVENT_SPELL_3
};

enum Misc
{
    NPC_MAGUS_FIRE      = 54882,
    NPC_MAGUS_ARCANE    = 54883,
    NPC_MAGUS_FROST     = 54884,
    ACTION_ACTIVATE     = 1
};

struct boss_woe_queen_azshara : public BossAI
{
    boss_woe_queen_azshara(Creature* creature) : BossAI(creature, BOSS_QUEEN_AZSHARA), _started(false), _finished(false), _wave(0) { }

    void JustAppeared() override
    {
        if (instance->GetBossState(BOSS_QUEEN_AZSHARA) == DONE)
        {
            me->DespawnOrUnsummon();
            return;
        }

        // Azshara ne se bat pas : elle est ciblable (pour interrompre) mais ne subit aucun dégât
        me->SetReactState(REACT_PASSIVE);
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
        SetCombatMovement(false);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_started && !_finished && who->IsPlayer() && !who->ToPlayer()->IsGameMaster() && me->IsWithinDistInMap(who, 35.0f))
            StartEncounter(who);
    }

    void AttackStart(Unit* who) override
    {
        if (!_started && !_finished && who && who->IsPlayer())
            StartEncounter(who);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        damage = 0;
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_SLAY, victim);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        if (_finished)
            return;

        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        // Les mages reprennent leur place, les morts reviennent
        for (ObjectGuid const& guid : _magi)
            if (Creature* magus = ObjectAccessor::GetCreature(*me, guid))
            {
                if (!magus->IsAlive())
                    magus->Respawn(true);
                else if (magus->IsAIEnabled())
                    magus->AI()->EnterEvadeMode();
            }
        _magi.clear();
        _started = false;
        _wave = 0;
        _EnterEvadeMode(why);
        me->GetMotionMaster()->MoveTargetedHome();
        instance->SetBossState(BOSS_QUEEN_AZSHARA, FAIL);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_started)
            return;

        if (!_finished && !UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CHECK_MAGI:
                    CheckMagi();
                    if (!_finished)
                        events.Repeat(1s);
                    break;
                case EVENT_NEXT_WAVE:
                    ActivateNextWave();
                    break;
                case EVENT_TOTAL_OBEDIENCE:
                    Talk(SAY_OBEDIENCE);
                    DoCastAOE(SPELL_TOTAL_OBEDIENCE);
                    events.Repeat(40s);
                    break;
                case EVENT_DESPAWN:
                    me->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
    }

private:
    void StartEncounter(Unit* who)
    {
        _started = true;
        Talk(SAY_AGGRO, who);
        instance->SetBossState(BOSS_QUEEN_AZSHARA, IN_PROGRESS);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        DoZoneInCombat();

        _magi.clear();
        for (uint32 entry : { NPC_MAGUS_FIRE, NPC_MAGUS_FROST, NPC_MAGUS_ARCANE })
        {
            std::list<Creature*> list;
            me->GetCreatureListWithEntryInGrid(list, entry, 120.0f);
            for (Creature* magus : list)
                if (magus->IsAlive())
                    _magi.push_back(magus->GetGUID());
        }

        _wave = 0;
        ActivateNextWave();
        events.ScheduleEvent(EVENT_CHECK_MAGI, 1s);
        events.ScheduleEvent(EVENT_TOTAL_OBEDIENCE, 25s);
    }

    // Deux mages entrent en jeu à chaque vague
    void ActivateNextWave()
    {
        events.CancelEvent(EVENT_NEXT_WAVE);
        uint8 activated = 0;
        for (ObjectGuid const& guid : _magi)
        {
            if (activated >= 2)
                break;
            Creature* magus = ObjectAccessor::GetCreature(*me, guid);
            if (!magus || !magus->IsAlive() || !magus->IsImmuneToPC())
                continue;
            if (magus->IsAIEnabled())
                magus->AI()->DoAction(ACTION_ACTIVATE);
            ++activated;
        }

        if (!activated)
            return;

        Talk(SAY_WAVE_1 + std::min<uint8>(_wave, 2));
        ++_wave;
        events.ScheduleEvent(EVENT_NEXT_WAVE, 60s); // les suivants arrivent de toute façon au bout d'une minute
    }

    void CheckMagi()
    {
        uint8 alive = 0;
        uint8 activeAlive = 0;
        for (ObjectGuid const& guid : _magi)
            if (Creature* magus = ObjectAccessor::GetCreature(*me, guid))
                if (magus->IsAlive())
                {
                    ++alive;
                    if (!magus->IsImmuneToPC())
                        ++activeAlive;
                }

        if (!alive)
        {
            Finish();
            return;
        }

        if (!activeAlive)
            ActivateNextWave();
    }

    // Tous les mages sont tombés : Azshara s'en va
    void Finish()
    {
        _finished = true;
        events.Reset();
        me->InterruptNonMeleeSpells(false);
        Talk(SAY_END);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        instance->SetBossState(BOSS_QUEEN_AZSHARA, DONE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->CombatStop(true);
        events.ScheduleEvent(EVENT_DESPAWN, 8s);
    }

    bool _started;
    bool _finished;
    uint8 _wave;
    std::vector<ObjectGuid> _magi;
};

// 54882 / 54883 / 54884 - Mage enchanté (feu / Arcanes / givre)
struct npc_woe_enchanted_magus : public ScriptedAI
{
    npc_woe_enchanted_magus(Creature* creature) : ScriptedAI(creature) { }

    void JustAppeared() override
    {
        SetInactive();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        ScriptedAI::EnterEvadeMode(why);
        SetInactive();
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_ACTIVATE)
            return;

        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
        me->SetReactState(REACT_AGGRESSIVE);
        DoZoneInCombat();
        if (Unit* target = SelectTarget(SELECT_TARGET_MINDISTANCE, 0, 100.0f, true))
            AttackStart(target);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_SPELL_1, 1s);
        _events.ScheduleEvent(EVENT_SPELL_2, 8s);
        _events.ScheduleEvent(EVENT_SPELL_3, 14s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (me->GetEntry())
            {
                case NPC_MAGUS_FIRE:
                    if (eventId == EVENT_SPELL_1) { DoCastVictim(SPELL_FIREBALL); _events.Repeat(3s); }
                    else if (eventId == EVENT_SPELL_2) { CastRandom(SPELL_FIREBOMB); _events.Repeat(12s); }
                    break;
                case NPC_MAGUS_FROST:
                    if (eventId == EVENT_SPELL_1) { CastRandom(SPELL_ICE_FLING); _events.Repeat(4s); }
                    else if (eventId == EVENT_SPELL_2) { DoCastVictim(SPELL_COLDFLAME); _events.Repeat(15s); }
                    else if (eventId == EVENT_SPELL_3) { CastRandom(SPELL_BLADES_OF_ICE); _events.Repeat(20s); }
                    break;
                case NPC_MAGUS_ARCANE:
                    if (eventId == EVENT_SPELL_1) { CastRandom(SPELL_ARCANE_BOMB); _events.Repeat(10s); }
                    else if (eventId == EVENT_SPELL_2) { DoCastSelf(SPELL_ARCANE_SHOCK); _events.Repeat(25s); }
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
    void SetInactive()
    {
        _events.Reset();
        me->SetReactState(REACT_PASSIVE);
        me->SetImmuneToPC(true);
    }

    void CastRandom(uint32 spellId)
    {
        if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
            DoCast(target, spellId);
        else
            DoCastVictim(spellId);
    }

    EventMap _events;
};

// 103241 - Obéissance totale : si elle n'est pas interrompue, chaque joueur perd 40 % de ses points de vie
// (le contrôle mental d'origine n'est pas appliqué, il bloquerait le groupe)
class spell_woe_total_obedience : public SpellScript
{
    void PreventAura(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
    }

    void HandleAfterHit()
    {
        Unit* target = GetHitUnit();
        Unit* caster = GetCaster() ? GetCaster()->ToUnit() : nullptr;
        if (!target || !caster || !target->IsPlayer())
            return;

        target->RemoveAurasDueToSpell(SPELL_TOTAL_OBEDIENCE);
        uint32 damage = target->CountPctFromMaxHealth(40);
        caster->SendSpellNonMeleeDamageLog(target, SPELL_TOTAL_OBEDIENCE, damage, SPELL_SCHOOL_MASK_ARCANE, 0, 0, false, 0);
        Unit::DealDamage(caster, target, damage, 0, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_ARCANE, GetSpellInfo(), false);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_woe_total_obedience::PreventAura, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
        OnEffectHitTarget.Register(&spell_woe_total_obedience::PreventAura, EFFECT_1, SPELL_EFFECT_APPLY_AURA);
        OnEffectHitTarget.Register(&spell_woe_total_obedience::PreventAura, EFFECT_2, SPELL_EFFECT_APPLY_AURA);
        AfterHit.Register(&spell_woe_total_obedience::HandleAfterHit);
    }
};
}

// ============================================================================
// Azimov : Mannoroth et Varo'then
// ============================================================================
namespace Mannoroth
{
enum Spells
{
    // Mannoroth
    SPELL_FEL_FIRESTORM     = 103888,
    SPELL_FELBLADE          = 103966,
    SPELL_EMBEDDED_BLADE    = 104823, // visuel seul (104820 fait partir l'arc sur les joueurs)
    SPELL_MAGISTRIKE_ARC    = 104822,
    SPELL_FEL_FIRE_NOVA     = 105093,
    // Varo'then
    SPELL_BLADE_DANCE       = 104995,
    // Gangreflammes au sol
    SPELL_FEL_FLAMES_AURA   = 103892
};

enum Texts
{
    // Mannoroth
    SAY_M_AGGRO     = 0,
    SAY_M_PORTAL    = 1,
    SAY_M_BLADE     = 2,
    SAY_M_SLAY      = 3,
    SAY_M_DEATH     = 4,
    // Varo'then
    SAY_V_AGGRO     = 0,
    SAY_V_SLAY      = 1,
    SAY_V_DEATH     = 2
};

enum Events
{
    EVENT_FEL_FIRESTORM = 1,
    EVENT_FELBLADE,
    EVENT_FEL_FIRE_NOVA,
    EVENT_SUMMON_DEMONS,
    EVENT_BLADE_DANCE
};

enum Misc
{
    NPC_VORACIOUS_FELHOUND  = 56073,
    NPC_DOOMGUARD_DEVASTATOR = 55739,
    NPC_ABYSSAL_DOOMBRINGER = 55510,
    ACTION_VAROTHEN_DIED    = 1
};

struct boss_woe_mannoroth : public BossAI
{
    boss_woe_mannoroth(Creature* creature) : BossAI(creature, BOSS_MANNOROTH_AND_VAROTHEN), _portalPhase(0), _bladeEmbedded(false) { }

    void JustAppeared() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_M_AGGRO, who);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        _portalPhase = 0;
        _bladeEmbedded = false;

        if (Creature* varothen = instance->GetCreature(DATA_VAROTHEN))
            if (varothen->IsAlive() && !varothen->IsEngaged() && varothen->IsAIEnabled())
                varothen->AI()->AttackStart(who);

        events.ScheduleEvent(EVENT_FELBLADE, 5s);
        events.ScheduleEvent(EVENT_FEL_FIRE_NOVA, 12s);
        events.ScheduleEvent(EVENT_FEL_FIRESTORM, 25s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        summons.DespawnAll();
        if (Creature* varothen = instance->GetCreature(DATA_VAROTHEN))
        {
            if (!varothen->IsAlive())
                varothen->Respawn(true);
            else if (varothen->IsAIEnabled())
                varothen->AI()->EnterEvadeMode();
        }
        _EnterEvadeMode(why);
        _DespawnAtEvade();
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        Talk(SAY_M_DEATH);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        if (Creature* varothen = instance->GetCreature(DATA_VAROTHEN))
            if (varothen->IsAlive())
                varothen->KillSelf();
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_M_SLAY, victim);
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        if (summon->GetEntry() != 55502) // Gangreflammes
            DoZoneInCombat(summon);
    }

    // Varo'then est mort : sa lame reste plantée dans Mannoroth
    void DoAction(int32 action) override
    {
        if (action != ACTION_VAROTHEN_DIED || _bladeEmbedded || !me->IsAlive())
            return;

        _bladeEmbedded = true;
        Talk(SAY_M_BLADE);
        DoCastSelf(SPELL_EMBEDDED_BLADE, true);
    }

    // Le Portail du Néant libère des démons à 85, 70, 60 et 50 %
    void DamageTaken(Unit* attacker, uint32& damage) override
    {
        // Lame enchâssée : les attaques des joueurs ont 15 % de chances de déclencher Arc Magifrappe sur Mannoroth
        if (_bladeEmbedded && attacker && attacker->GetCharmerOrOwnerPlayerOrPlayerItself() && damage && roll_chance_i(15))
        {
            uint32 const arc = me->CountPctFromMaxHealth(2);
            me->SendSpellNonMeleeDamageLog(me, SPELL_MAGISTRIKE_ARC, arc, SPELL_SCHOOL_MASK_ARCANE, 0, 0, false, 0);
            damage += arc;
        }

        static uint8 const thresholds[4] = { 85, 70, 60, 50 };
        if (_portalPhase < 4 && me->HealthBelowPctDamaged(thresholds[_portalPhase], damage))
        {
            if (_portalPhase == 0)
                Talk(SAY_M_PORTAL);
            SummonDemons(_portalPhase);
            ++_portalPhase;
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
                case EVENT_FELBLADE:
                    DoCastSelf(SPELL_FELBLADE);
                    events.Repeat(30s);
                    break;
                case EVENT_FEL_FIRE_NOVA:
                    DoCastAOE(SPELL_FEL_FIRE_NOVA);
                    events.Repeat(18s);
                    break;
                case EVENT_FEL_FIRESTORM:
                    DoCastSelf(SPELL_FEL_FIRESTORM);
                    events.Repeat(40s);
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
    void SummonDemons(uint8 phase)
    {
        uint32 entry = NPC_VORACIOUS_FELHOUND;
        uint8 count = 3;
        switch (phase)
        {
            case 1: count = 4; break;                                       // 70 % : plus de gangrechiens
            case 2: entry = NPC_DOOMGUARD_DEVASTATOR; count = 1; break;     // 60 %
            case 3: entry = NPC_ABYSSAL_DOOMBRINGER; count = 1; break;      // 50 %
            default: break;
        }

        for (uint8 i = 0; i < count; ++i)
        {
            Position pos = me->GetPosition();
            float const angle = frand(0.0f, 2.0f * float(M_PI));
            float const dist = frand(8.0f, 15.0f);
            pos.m_positionX += std::cos(angle) * dist;
            pos.m_positionY += std::sin(angle) * dist;
            me->UpdateGroundPositionZ(pos.m_positionX, pos.m_positionY, pos.m_positionZ);
            me->SummonCreature(entry, pos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);
        }
    }

    uint8 _portalPhase;
    bool _bladeEmbedded;
};

// 55419 - Capitaine Varo'then
struct npc_woe_captain_varothen : public ScriptedAI
{
    npc_woe_captain_varothen(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    void JustAppeared() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetImmuneToPC(false);
    }

    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_V_AGGRO, who);
        _events.Reset();
        _events.ScheduleEvent(EVENT_BLADE_DANCE, 15s);

        // Mannoroth entre en combat en même temps
        if (_instance)
            if (Creature* mannoroth = _instance->GetCreature(DATA_MANNOROTH))
                if (mannoroth->IsAlive() && !mannoroth->IsEngaged() && mannoroth->IsAIEnabled())
                    mannoroth->AI()->AttackStart(who);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_V_SLAY, victim);
    }

    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_V_DEATH);
        if (_instance)
            if (Creature* mannoroth = _instance->GetCreature(DATA_MANNOROTH))
                if (mannoroth->IsAIEnabled())
                    mannoroth->AI()->DoAction(ACTION_VAROTHEN_DIED);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            if (eventId == EVENT_BLADE_DANCE)
            {
                DoCastSelf(SPELL_BLADE_DANCE);
                _events.Repeat(25s);
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    EventMap _events;
};

// 55502 - Gangreflammes (au sol) : brûlent les joueurs proches puis disparaissent
struct npc_woe_fel_flames : public NullCreatureAI
{
    npc_woe_fel_flames(Creature* creature) : NullCreatureAI(creature) { }

    void JustAppeared() override
    {
        DoCastSelf(SPELL_FEL_FLAMES_AURA, true);
        me->DespawnOrUnsummon(15s);
    }
};
}

// ============================================================================
// Azimov : Appareil de transit temporel du Puits d'éternité
// ============================================================================
namespace TimeTransit
{
constexpr uint32 DEST_COURTYARD = 1;
constexpr uint32 DEST_PALACE    = 2;
constexpr uint32 DEST_WELL      = 3;

Position const CourtyardPos = { 3229.0f, -4995.0f, 194.2f, 3.9f };
Position const PalacePos    = { 3454.3f, -5084.9f, 213.7f, 4.7f };
Position const WellPos      = { 3130.0f, -5555.0f, 18.3f, 5.6f };

uint32 const GOSSIP_TEXT_SELECT_DESTINATION = 18848; // « Choisissez votre destination. »

struct go_woe_time_transit_device : public GameObjectAI
{
    go_woe_time_transit_device(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Cour des Lumières (entrée)", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + DEST_COURTYARD);
        if (instance->GetBossState(BOSS_PEROTHARN) == DONE)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Palais d'Azshara", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + DEST_PALACE);
        if (instance->GetBossState(BOSS_QUEEN_AZSHARA) == DONE)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Rive du Puits d'éternité (Mannoroth)", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + DEST_WELL);

        SendGossipMenuFor(player, GOSSIP_TEXT_SELECT_DESTINATION, me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId) - GOSSIP_ACTION_INFO_DEF;
        CloseGossipMenuFor(player);

        Position const* dest = nullptr;
        switch (action)
        {
            case DEST_COURTYARD: dest = &CourtyardPos; break;
            case DEST_PALACE:    dest = &PalacePos;    break;
            case DEST_WELL:      dest = &WellPos;      break;
            default: return true;
        }

        player->NearTeleportTo(dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ(), dest->GetOrientation());
        return true;
    }
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

    // Azimov : Reine Azshara
    using namespace WellOfEternity::QueenAzshara;
    RegisterWellOfEternityCreatureAI(boss_woe_queen_azshara);
    RegisterWellOfEternityCreatureAI(npc_woe_enchanted_magus);
    RegisterSpellScript(spell_woe_total_obedience);

    // Azimov : Mannoroth et Varo'then
    using namespace WellOfEternity::Mannoroth;
    RegisterWellOfEternityCreatureAI(boss_woe_mannoroth);
    RegisterWellOfEternityCreatureAI(npc_woe_captain_varothen);
    RegisterWellOfEternityCreatureAI(npc_woe_fel_flames);

    // Azimov : Appareil de transit temporel
    RegisterGameObjectAI(WellOfEternity::TimeTransit::go_woe_time_transit_device);
}
