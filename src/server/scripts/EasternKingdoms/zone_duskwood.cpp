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
SDName: Duskwood
SD%Complete: 100
SDComment: Quest Support:8735
SDCategory: Duskwood
EndScriptData */

#include "ScriptMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "SpellScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "ObjectAccessor.h"
#include "MotionMaster.h"
#include "QuestDef.h"
#include <list>

namespace Duskwood
{
enum TwilightCorrupter
{
    NPC_TWILIGHT_CORRUPTER          = 15625,
    YELL_TWILIGHT_CORRUPTOR_RESPAWN = 0,
    YELL_TWILIGHT_CORRUPTOR_AGGRO   = 1,
    YELL_TWILIGHT_CORRUPTOR_KILL    = 2,

    SPELL_SOUL_CORRUPTION           = 25805,
    SPELL_CREATURE_OF_NIGHTMARE     = 25806,
    SPELL_LEVEL_UP                  = 24312,

    EVENT_SOUL_CORRUPTION           = 1,
    EVENT_CREATURE_OF_NIGHTMARE     = 2,

    QUEST_NIGHTMARES_CORRUPTION     = 8735
};

/*######
# boss_twilight_corrupter
######*/

class boss_twilight_corrupter : public CreatureScript
{
public:
    boss_twilight_corrupter() : CreatureScript("boss_twilight_corrupter") { }

    struct boss_twilight_corrupterAI : public ScriptedAI
    {
        boss_twilight_corrupterAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            KillCount = 0;
        }

        void Reset() override
        {
            _events.Reset();
            Initialize();
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(YELL_TWILIGHT_CORRUPTOR_AGGRO);
            _events.ScheduleEvent(EVENT_SOUL_CORRUPTION, 15000);
            _events.ScheduleEvent(EVENT_CREATURE_OF_NIGHTMARE, 30000);
        }

        void KilledUnit(Unit* victim) override
        {
            if (victim->GetTypeId() == TYPEID_PLAYER)
            {
                ++KillCount;
                Talk(YELL_TWILIGHT_CORRUPTOR_KILL, victim);

                if (KillCount == 3)
                {
                    DoCast(me, SPELL_LEVEL_UP, true);
                    KillCount = 0;
                }
            }
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
                switch (eventId)
                {
                    case EVENT_SOUL_CORRUPTION:
                        DoCastAOE(SPELL_SOUL_CORRUPTION);
                        _events.ScheduleEvent(EVENT_SOUL_CORRUPTION, urand(15000, 19000));
                        break;
                    case EVENT_CREATURE_OF_NIGHTMARE:
                        if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                            DoCast(target, SPELL_CREATURE_OF_NIGHTMARE);
                        _events.ScheduleEvent(EVENT_CREATURE_OF_NIGHTMARE, 45000);
                        break;
                    default:
                        break;
                }
            }

            DoMeleeAttackIfReady();
        }

        private:
            EventMap _events;
            uint8 KillCount;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_twilight_corrupterAI(creature);
    }
};

/*######
# at_twilight_grove
######*/

Position const TwillightCorrupter = { -10328.16f, -489.57f, 49.95f, 0.0f };

class at_twilight_grove : public AreaTriggerScript
{
    public:
        at_twilight_grove() : AreaTriggerScript("at_twilight_grove") { }

        bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
        {
            if (player->GetQuestStatus(QUEST_NIGHTMARES_CORRUPTION) == QUEST_STATUS_INCOMPLETE)
                if (!player->FindNearestCreature(NPC_TWILIGHT_CORRUPTER, 500.0f, true))
                    if (Creature* corrupter = player->SummonCreature(NPC_TWILIGHT_CORRUPTER, TwillightCorrupter, TEMPSUMMON_MANUAL_DESPAWN, 60000))
                        corrupter->AI()->Talk(YELL_TWILIGHT_CORRUPTOR_RESPAWN, player);

            return false;
        };
};
}

// ---- Azimov : quêtes 26720 et 26674, portées de SkyFire_548 (GPL) ----
/*######
## npc_lurking_potion - Quest 26720 A Curse We Cannot Lift
######*/

enum LurkingPotion
{
    QUEST_A_CURSE_WE_CANNOT_LIFT    = 26720,

    NPC_LURKING_WORGEN              = 43814,
    NPC_LURKING_WORGEN_KILL_CREDIT  = 43860,

    SPELL_STUNNING_POUNCE           = 81957,
    SPELL_HARRIS_AMPULE             = 82058,

    SAY_HESITATE                    = 0
};

static bool PlayerHasActiveQuest(Player* player)
{
    if (!player)
        return false;

    QuestStatus status = player->GetQuestStatus(QUEST_A_CURSE_WE_CANNOT_LIFT);
    return status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE;
}

static bool IsWeakenedLurkingWorgen(Creature* creature)
{
    if (!creature || creature->GetEntry() != NPC_LURKING_WORGEN)
        return false;

    if (creature->HealthAbovePct(20))
        return false;

    return creature->GetReactState() == REACT_PASSIVE;
}

static Creature* ResolveAmpuleTarget(Player* player, Unit* primaryTarget)
{
    if (!player)
        return NULL;

    if (primaryTarget)
        if (Creature* creature = primaryTarget->ToCreature())
            return creature;

    if (Unit* selected = player->GetSelectedUnit())
        if (Creature* creature = selected->ToCreature())
            return creature;

    if (Unit* victim = player->GetVictim())
        if (Creature* creature = victim->ToCreature())
            return creature;

    return NULL;
}

static bool TryApplyHarrisAmpule(Player* player, Creature* creature)
{
    if (!player || !IsWeakenedLurkingWorgen(creature))
        return false;

    if (!PlayerHasActiveQuest(player))
        return false;

    player->KilledMonsterCredit(NPC_LURKING_WORGEN_KILL_CREDIT);
    creature->DespawnOrUnsummon(1000);
    return true;
}

class npc_lurking_potion : public CreatureScript
{
public:
    npc_lurking_potion() : CreatureScript("npc_lurking_potion") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lurking_potionAI(creature);
    }

    struct npc_lurking_potionAI : public ScriptedAI
    {
        npc_lurking_potionAI(Creature* creature) : ScriptedAI(creature),
            _pounced(false), _hesitated(false), _pounceTargetGUID() { }

        bool _pounced;
        bool _hesitated;
        ObjectGuid _pounceTargetGUID;

        bool IsHesitating() const { return _hesitated; }

        void EnterEvadeMode(EvadeReason why) override
        {
            if (_hesitated || _pounced)
                return;

            ScriptedAI::EnterEvadeMode(why);
        }

        void JustAppeared() override
        {
            _pounced = false;
            _hesitated = false;
            _pounceTargetGUID.Clear();
            me->setRegeneratingHealth(true);
            ScriptedAI::JustAppeared();
        }

        void Reset() override
        {
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_NONE);

            if (_hesitated)
            {
                me->setRegeneratingHealth(false);
                me->SetReactState(REACT_PASSIVE);
                me->SetHealth(me->CountPctFromMaxHealth(20));
            }
            else
            {
                _pounced = false;
                _pounceTargetGUID.Clear();
                me->setRegeneratingHealth(true);
                me->SetReactState(REACT_AGGRESSIVE);
            }
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
        }

        void TriggerHesitate()
        {
            if (_hesitated)
                return;

            _hesitated = true;
            Talk(SAY_HESITATE);
            me->AttackStop();
            me->setRegeneratingHealth(false);
            me->SetReactState(REACT_PASSIVE);
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveIdle();

            uint32 floorHealth = me->CountPctFromMaxHealth(20);
            if (me->GetHealth() > floorHealth)
                me->SetHealth(floorHealth);
        }

        void StartPounce(Player* player)
        {
            if (!player || _pounced || _hesitated)
                return;

            _pounced = true;
            _pounceTargetGUID = player->GetGUID();

            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_NONE);
            me->SetReactState(REACT_PASSIVE);
            me->AttackStop();
            me->CastSpell(player, SPELL_STUNNING_POUNCE, false);
        }

        void EngageQuestPlayer(Player* player)
        {
            if (!PlayerHasActiveQuest(player))
                return;

            _pounceTargetGUID = player->GetGUID();
            me->SetReactState(REACT_AGGRESSIVE);
            AttackStart(player);
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (!_pounced && !_hesitated && who->GetTypeId() == TypeID::TYPEID_PLAYER)
            {
                if (Player* player = who->ToPlayer())
                {
                    if (PlayerHasActiveQuest(player) &&
                        me->IsWithinDistInMap(player, 25.0f) && me->IsWithinLOSInMap(player))
                    {
                        StartPounce(player);
                        return;
                    }
                }
            }

            ScriptedAI::MoveInLineOfSight(who);
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (_hesitated || !_pounced)
                return;

            bool pounceComplete = (type == EFFECT_MOTION_TYPE && id == EVENT_JUMP) ||
                                  (type == POINT_MOTION_TYPE && id == EVENT_CHARGE);
            if (!pounceComplete)
                return;

            if (Player* player = ObjectAccessor::GetPlayer(*me, _pounceTargetGUID))
                EngageQuestPlayer(player);
        }

        void DamageTaken(Unit* doneBy, uint32& damage) override
        {
            if (_hesitated)
            {
                damage = 0;
                return;
            }

            if (Player* player = doneBy->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                if (PlayerHasActiveQuest(player))
                {
                    if (!_pounced)
                    {
                        if (me->IsWithinDistInMap(player, 25.0f) && me->IsWithinLOSInMap(player))
                            StartPounce(player);
                        else
                            EngageQuestPlayer(player);
                    }
                    else if (me->GetReactState() == REACT_PASSIVE)
                        EngageQuestPlayer(player);
                }
            }

            if (me->HealthBelowPctDamaged(21, damage))
            {
                uint32 floorHealth = me->CountPctFromMaxHealth(20);
                if (me->GetHealth() > floorHealth)
                    damage = me->GetHealth() - floorHealth;
                else
                    damage = 0;
            }

            if (!me->HealthAbovePct(20))
                TriggerHesitate();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!_hesitated && !me->HealthAbovePct(20))
                TriggerHesitate();

            if (_hesitated)
                return;

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        void SpellHit(WorldObject* caster, SpellInfo const* spell) override
        {
            if (!_hesitated || !spell || spell->Id != SPELL_HARRIS_AMPULE)
                return;

            Player* player = caster->ToPlayer();
            if (!player)
                return;

            TryApplyHarrisAmpule(player, me);
        }
    };
};

/*######
## spell_q26720_harris_ampule - Quest 26720 Harris's Ampule
######*/

class spell_q26720_harris_ampule : public SpellScriptLoader
{
public:
    spell_q26720_harris_ampule() : SpellScriptLoader("spell_q26720_harris_ampule") { }

    class spell_q26720_harris_ampule_SpellScript : public SpellScript
    {
        Creature* GetAmpuleCreatureFromCaster(Player* player)
        {
            return ResolveAmpuleTarget(player, GetExplTargetUnit());
        }

        SpellCastResult CheckCast()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return SpellCastResult::SPELL_CAST_OK;

            if (!PlayerHasActiveQuest(player))
                return SpellCastResult::SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;

            if (!IsWeakenedLurkingWorgen(GetAmpuleCreatureFromCaster(player)))
                return SpellCastResult::SPELL_FAILED_BAD_TARGETS;

            return SpellCastResult::SPELL_CAST_OK;
        }

        void HandleAmpule(SpellEffIndex /*effIndex*/)
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return;

            Creature* creature = GetHitCreature();
            if (!creature)
                creature = GetAmpuleCreatureFromCaster(player);

            TryApplyHarrisAmpule(player, creature);
        }

        void HandleAmpuleCast()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return;

            TryApplyHarrisAmpule(player, GetAmpuleCreatureFromCaster(player));
        }

        void Register() override
        {
            OnCheckCast.Register(&spell_q26720_harris_ampule_SpellScript::CheckCast);
            OnCast.Register(&spell_q26720_harris_ampule_SpellScript::HandleAmpuleCast);
            // MoP DBC: EFFECT_0 is SPELL_EFFECT_DUMMY only.
            OnEffectHitTarget.Register(&spell_q26720_harris_ampule_SpellScript::HandleAmpule, EFFECT_0, SPELL_EFFECT_DUMMY);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_q26720_harris_ampule_SpellScript();
    }
};

/*######
## spell_q26674_call_stalvan
######*/

class spell_q26674_call_stalvan : public SpellScriptLoader
{
public:
    spell_q26674_call_stalvan() : SpellScriptLoader("spell_q26674_call_stalvan") { }

    class spell_q26674_call_stalvan_SpellScript : public SpellScript
    {
        void HandleScript()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return;

            // Summon Stalvan and Tobias.
            if (Creature* stalvan = player->SummonCreature(315, -10371.669922f, -1251.914185f, 35.9100023f, 5.536726f, TempSummonType::TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000))
            {
                stalvan->SetReactState(REACT_PASSIVE);
                stalvan->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
            }
            if (Creature* tobias = player->SummonCreature(35124, -10361.395508f, -1256.723022f, 35.859238f, 3.17049f, TempSummonType::TEMPSUMMON_TIMED_DESPAWN, 60000))
            {
                tobias->SetReactState(REACT_PASSIVE);
                tobias->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
                
                // Scale the summoned Tobias so he doesn't get instantly one-shot by Stalvan during the RP combat
                tobias->SetLevel(32);
                tobias->SetMaxHealth(25000);
                tobias->SetFullHealth();
            }
        }

        void Register() override
        {
            AfterCast.Register(&spell_q26674_call_stalvan_SpellScript::HandleScript);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_q26674_call_stalvan_SpellScript();
    }
};

void AddSC_duskwood()
{
    new npc_lurking_potion();
    new spell_q26720_harris_ampule();
    new spell_q26674_call_stalvan();
    using namespace Duskwood;
    new boss_twilight_corrupter();
    new at_twilight_grove();
}
