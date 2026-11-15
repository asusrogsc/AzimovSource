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

#include "Player.h"
#include "Chat.h"
#include "Map.h"
#include <vector>
#include <cmath>
#include "Containers.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "CombatAI.h"
#include "CreatureAIImpl.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SharedDefines.h"
#include "TemporarySummon.h"
#include "ObjectMgr.h"
#include "Group.h"

namespace Westfall
{
enum WestfallQuest
{
    QUEST_MURDER_WAS_THE_CASE_THAT_THEY_GAVE_ME = 26209,
    QUEST_LOUS_PARTING_THOUGHTS                 = 26232
};

enum WestfallCreature
{
    NPC_HOMELESS_STORMWIND_CITIZEN_1  = 42386,
    NPC_HOMELESS_STORMWIND_CITIZEN_2  = 42384,
    NPC_WEST_PLAINS_DRIFTER           = 42391,
    NPC_TRANSIENT                     = 42383,
    NPC_THUG                          = 42387,
    MPC_LOUS_PARTING_THOUGHTS_TRIGGER = 42562,
    NPC_SMALL_TIME_HUSTLER            = 42390,
    NPC_MERCENARY                     = 42656,
    NPC_AGENT_KEARNEN                 = 7024
};

enum WestfallSpell
{
    SPELL_HOBO_INFORMATION_1         = 79181,
    SPELL_HOBO_INFORMATION_2         = 79182,
    SPELL_HOBO_INFORMATION_3         = 79183,
    SPELL_HOBO_INFORMATION_4         = 79184,
    SPELL_SUMMON_RAGAMUFFIN_LOOTER   = 79169,
    SPELL_SUMMON_RAGAMUFFIN_LOOTER_1 = 79170,
    SPELL_SUMMON_RAGAMUFFIN_LOOTER_2 = 79171,
    SPELL_SUMMON_RAGAMUFFIN_LOOTER_3 = 79172,
    SPELL_SUMMON_RAGAMUFFIN_LOOTER_4 = 79173,
    SPELL_AGGRO_HOBO                 = 79168,
    SPELL_HOBO_INFORMATION           = 79184,
    SPELL_KILL_SHOT_TRIGGERED        = 79525
};

class spell_westfall_unbound_energy : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (targets.empty())
            return;

        Unit* caster = GetCaster();
        targets.remove_if([caster](WorldObject const* target)->bool
        {
            return caster == target;
        });

        if (targets.size() > 1)
            Trinity::Containers::RandomResize(targets, 1);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_westfall_unbound_energy::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

enum ItsAlive
{
    // Events
    EVENT_CHECK_AREA                = 1,
    EVENT_DESPAWN_HARVESTER         = 2,

    // Texts
    SAY_ANNOUNCE_OUT_OF_AREA        = 0,

    // Area Ids
    AREA_ID_THE_MOEST_FARM          = 918,

    // Creatures
    NPC_ENERGIZED_HARVEST_REAPER    = 42342,
    NPC_OVERLOADED_HARVEST_GOLEM    = 42601
};

struct npc_westfall_overloaded_harvest_golem : public VehicleAI
{
    npc_westfall_overloaded_harvest_golem(Creature* creature) : VehicleAI(creature) { }

    void JustAppeared() override
    {
        _events.ScheduleEvent(EVENT_CHECK_AREA, 1s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CHECK_AREA:
                    if (me->GetAreaId() != AREA_ID_THE_MOEST_FARM)
                    {
                        if (Unit* owner = me->GetCharmerOrOwner())
                            Talk(SAY_ANNOUNCE_OUT_OF_AREA, owner);
                        _events.ScheduleEvent(EVENT_DESPAWN_HARVESTER, 8s);
                    }
                    else
                        _events.Repeat(1s);
                    break;
                case EVENT_DESPAWN_HARVESTER:
                    if (me->GetAreaId() != AREA_ID_THE_MOEST_FARM)
                        me->DespawnOrUnsummon();
                    else
                        _events.ScheduleEvent(EVENT_CHECK_AREA, 1s);
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
};

class spell_westfall_reaping_blows : public AuraScript
{
    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();
        if (Creature* reaper = GetTarget()->FindNearestCreature(NPC_ENERGIZED_HARVEST_REAPER, 5.f, true))
            GetTarget()->CastSpell(reaper, GetSpellInfo()->Effects[EFFECT_1].TriggerSpell, true);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_westfall_reaping_blows::HandlePeriodic, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

class spell_westfall_wake_harvest_golem : public SpellScript
{
    SpellCastResult CheckTarget()
    {
        Unit* target = GetExplTargetUnit();
        if (!target || !target->IsCreature())
            return SPELL_FAILED_BAD_TARGETS;

        return SPELL_CAST_OK;
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->IsPlayer())
            return;

        if (Creature* target = GetHitCreature())
        {
            caster->ToPlayer()->KilledMonsterCredit(NPC_OVERLOADED_HARVEST_GOLEM);
            target->DespawnOrUnsummon(100ms);
        }
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_westfall_wake_harvest_golem::CheckTarget);
        OnEffectHitTarget.Register(&spell_westfall_wake_harvest_golem::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// Azimov : 79275 vise le « maître » de la Silhouette (cible 27), qui n'est pas toujours le joueur :
// le crédit de « Mener la belle vie » (26228) n'arrivait alors jamais. On le donne directement
// au joueur caché dans l'ancienne « maison » de Lou (véhicule 42500) et on le fait sortir.
static constexpr uint32 QUEST_LIVIN_THE_LIFE = 26228;
static constexpr uint32 NPC_LOUS_OLD_HOUSE   = 42500;

class spell_westfall_quest_credit_jangolode_event : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            target->ExitVehicle();
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        std::list<Player*> players;
        caster->GetPlayerListInGrid(players, 60.0f);
        for (Player* player : players)
        {
            Unit* house = player->GetVehicleBase();
            if (!house || house->GetEntry() != NPC_LOUS_OLD_HOUSE)
                continue;

            if (player->GetQuestStatus(QUEST_LIVIN_THE_LIFE) == QUEST_STATUS_INCOMPLETE)
                player->AreaExploredOrEventHappens(QUEST_LIVIN_THE_LIFE);

            player->ExitVehicle();
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_westfall_quest_credit_jangolode_event::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
        AfterCast.Register(&spell_westfall_quest_credit_jangolode_event::HandleAfterCast);
    }
};

enum HoboGossip
{
    GOSSIP_MENU_ID_HOBO = 11635
};

enum HoboGossipOption
{
    OPTION_QUESTION = 0,
    OPTION_PAY      = 1,
};

enum HoboQuestObjective
{
    CLUE1 = 0,
    CLUE2 = 1,
    CLUE3 = 2,
    CLUE4 = 3
};

enum HoboText
{
    SAY_CLUE1          = 0,
    SAY_CLUE2          = 1,
    SAY_CLUE3          = 2,
    SAY_CLUE4          = 3,
    SAY_AGGRO_BRIBE    = 4,
    SAY_AGGRO_CONVINCE = 5,
    SAY_EVENT          = 6,
    SAY_JACKPOT_INTRO  = 7,
    SAY_JACKPOT_END    = 8,
    SAY_PROPERTY_RAGE  = 9,
    SAY_FLEE           = 10
};

enum HoboEvent
{
    EVENT_TALK           = 1,
    EVENT_JACKPOT_INTRO  = 2,
    EVENT_CRY            = 3,
    EVENT_PROPERTY_RAGE  = 4,
    EVENT_JACKPOT_MIDDLE = 5,
    EVENT_JACKPOT_END    = 6,
    EVENT_RESUME_MOVE    = 7,
    EVENT_GROUP_OOC      = 1
};

enum HoboAction
{
    ACTION_AGGRO_HOBO      = 1,
    ACTION_AGGRO_HOBO_DONE = 2
};

struct npc_westfall_hobo_witness : public ScriptedAI
{
    npc_westfall_hobo_witness(Creature* creature) : ScriptedAI(creature), _bribeFailed(false), _hoboRage(false), _flee(false) { }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint8 clueGainChance = 0;
        switch (gossipListId)
        {
           case OPTION_QUESTION:
           {
               clueGainChance = 25;
               break;
           }
           case OPTION_PAY:
           {
               clueGainChance = 75;
               _bribeFailed = true;
               break;
           }
           default:
               return true;
        }

        me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        CloseGossipMenuFor(player);
        if (roll_chance_i(clueGainChance))
            GiveClue(player);
        else
        {
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC);
            AttackStart(player);
        }

        return false;
    }

    void GiveClue(Player* player)
    {
        if (!player)
            return;

        _events.CancelEventGroup(EVENT_GROUP_OOC);
        me->SetFacingToObject(player);

        uint16 slot = player->FindQuestSlot(QUEST_MURDER_WAS_THE_CASE_THAT_THEY_GAVE_ME);

        if (player->GetQuestSlotCounter(slot, CLUE1) == 0)
        {
            player->CastSpell(player, SPELL_HOBO_INFORMATION_1);
            Talk(SAY_CLUE1, player);
            me->DespawnOrUnsummon(12s);
            return;
        }

        if (player->GetQuestSlotCounter(slot, CLUE2) == 0)
        {
            player->CastSpell(player, SPELL_HOBO_INFORMATION_2);
            Talk(SAY_CLUE2, player);
            me->DespawnOrUnsummon(12s);
            return;
        }

        if (player->GetQuestSlotCounter(slot, CLUE3) == 0)
        {
            player->CastSpell(player, SPELL_HOBO_INFORMATION_3);
            Talk(SAY_CLUE3, player);
            me->DespawnOrUnsummon(12s);
            return;
        }

        if (player->GetQuestSlotCounter(slot, CLUE4) == 0)
        {
            player->CastSpell(player, SPELL_HOBO_INFORMATION_4);
            Talk(SAY_CLUE4, player);
            me->DespawnOrUnsummon(12s);
            return;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        me->SetAIAnimKitId(0);
        _events.CancelEventGroup(EVENT_GROUP_OOC);

        if (!who->IsPlayer())
            return;

        Talk(_bribeFailed ? SAY_AGGRO_BRIBE : SAY_AGGRO_CONVINCE, who);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!_flee && me->HealthBelowPctDamaged(20, damage))
        {
            _flee = true;
            me->DoFleeToGetAssistance();
            Talk(SAY_FLEE);
        }
    }

    void JustDied(Unit* who) override
    {
        if (who && who->IsCreature() && who->ToCreature()->IsAIEnabled())
            who->ToCreature()->AI()->DoAction(ACTION_AGGRO_HOBO_DONE);
        me->CastSpell(me, SPELL_SUMMON_RAGAMUFFIN_LOOTER);
    }

    void Reset() override
    {
        ScriptedAI::Reset();
        _events.Reset();
        _bribeFailed = false;
        _hoboRage = false;
        _flee = false;
        me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC);
        _events.ScheduleEvent(EVENT_RESUME_MOVE, 0, EVENT_GROUP_OOC);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_AGGRO_HOBO:
                if (Unit* target = ObjectAccessor::GetUnit(*me, _targetGUID))
                {
                    _hoboRage = true;
                    me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                    me->PauseMovement();
                    me->SetFacingToObject(target);
                    AttackStart(target);
                }
                break;
            case ACTION_AGGRO_HOBO_DONE:
                _hoboRage = false;
                break;
            default:
                return;
        }
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        _targetGUID = guid;
        DoAction(ACTION_AGGRO_HOBO);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TALK:
                    me->PauseMovement();
                    Talk(SAY_EVENT);
                    _events.ScheduleEvent(EVENT_RESUME_MOVE, 6s, EVENT_GROUP_OOC);
                    break;
                case EVENT_JACKPOT_INTRO:
                    me->PauseMovement();
                    Talk(SAY_JACKPOT_INTRO);
                    _events.ScheduleEvent(EVENT_JACKPOT_MIDDLE, 2s + 500ms, EVENT_GROUP_OOC);
                    break;
                case EVENT_JACKPOT_MIDDLE:
                    me->SetAIAnimKitId(648);
                    Talk(SAY_JACKPOT_END);
                    _events.ScheduleEvent(EVENT_JACKPOT_END, 6s, EVENT_GROUP_OOC);
                    break;
                case EVENT_JACKPOT_END:
                    me->SetAIAnimKitId(0);
                    _events.ScheduleEvent(EVENT_RESUME_MOVE, 4s, EVENT_GROUP_OOC);
                    break;
                case EVENT_CRY:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
                    me->PauseMovement();
                    _events.ScheduleEvent(EVENT_RESUME_MOVE, 2s, EVENT_GROUP_OOC);
                    break;
                case EVENT_PROPERTY_RAGE:
                {
                    uint32 creatureId = RAND(NPC_HOMELESS_STORMWIND_CITIZEN_1, NPC_HOMELESS_STORMWIND_CITIZEN_2, NPC_WEST_PLAINS_DRIFTER, NPC_TRANSIENT);
                    if (Creature* creature = GetClosestCreatureWithEntry(me, creatureId, 25.0f))
                    {
                        if (!creature->IsAlive() || creature->IsInCombat())
                        {
                            _events.ScheduleEvent(EVENT_RESUME_MOVE, 0, EVENT_GROUP_OOC);
                            return;
                        }
                        _hoboRage = true;
                        Talk(SAY_PROPERTY_RAGE);
                        me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                        me->PauseMovement();
                        me->CastSpell(creature, SPELL_AGGRO_HOBO, true);
                        me->SetFacingToObject(creature);
                    }
                    else
                        _events.ScheduleEvent(EVENT_RESUME_MOVE, 0, EVENT_GROUP_OOC);
                    break;
                }
                case EVENT_RESUME_MOVE:
                    me->ResumeMovement();
                    if (roll_chance_i(50)) // prevent spam
                    {
                        if (roll_chance_i(70)) // chance to trigger simple talk event
                            _events.ScheduleEvent(EVENT_TALK, 30s, 80s, EVENT_GROUP_OOC);
                        else
                            _events.ScheduleEvent(urand(EVENT_JACKPOT_INTRO, EVENT_PROPERTY_RAGE), 30s, 80s, EVENT_GROUP_OOC);
                    }
                    else
                        _events.Repeat(30s, 80s);
                    break;
                default:
                    break;
            }
        }

        if (!_hoboRage)
            if (!UpdateVictim())
                return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    ObjectGuid _targetGUID;
    bool _bribeFailed;
    bool _hoboRage;
    bool _flee;
};

class spell_westfall_summon_ragamuffin_looter : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
            {
                SPELL_SUMMON_RAGAMUFFIN_LOOTER_1,
                SPELL_SUMMON_RAGAMUFFIN_LOOTER_2,
                SPELL_SUMMON_RAGAMUFFIN_LOOTER_3,
                SPELL_SUMMON_RAGAMUFFIN_LOOTER_4
            });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_SUMMON_RAGAMUFFIN_LOOTER_1, true);
        caster->CastSpell(caster, SPELL_SUMMON_RAGAMUFFIN_LOOTER_2, true);
        caster->CastSpell(caster, SPELL_SUMMON_RAGAMUFFIN_LOOTER_3, true);
        caster->CastSpell(caster, SPELL_SUMMON_RAGAMUFFIN_LOOTER_4, true);
    }

    void Register() override
    {
        OnEffectLaunch.Register(&spell_westfall_summon_ragamuffin_looter::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_westfall_aggro_hobo : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Creature* creature = GetHitCreature())
            if (creature->IsAIEnabled())
                creature->AI()->SetGUID(GetCaster()->GetGUID(), 0);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_westfall_aggro_hobo::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum BridgeRefugee
{
    // Events
    EVENT_START_MOVE = 1,
    EVENT_CRIME_SCENE_DRIFTER_TALK_0,
    EVENT_CRIME_SCENE_TALK_12,
    EVENT_CRIME_SCENE_DRIFTER_TALK_1,
    EVENT_CRIME_SCENE_TALK_13,
    EVENT_CRIME_SCENE_DRIFTER_TALK_2,
    EVENT_SENTINEL_HILL_GUARD1_TALK_1,
    EVENT_SENTINEL_HILL_TALK_14,
    EVENT_SENTINEL_HILL_TALK_3,
    EVENT_SENTINEL_HILL_GUARD2_TALK_2,
    EVENT_SENTINEL_HILL_GUARD2_TALK_3,
    EVENT_SENTINEL_HILL_TALK_4,
    EVENT_SENTINEL_HILL_TALK_5,
    EVENT_SENTINEL_HILL_TALK_15,
    EVENT_SENTINEL_HILL_TALK_16,
    EVENT_CANCEL,

    // Action
    ACTION_CRIME_SCENE          = 1,
    ACTION_SENTINEL_HILL_GATE_1 = 2,
    ACTION_SENTINEL_HILL_GATE_2 = 3,
    ACTION_SENTINEL_HILL_TALK_3 = 4,
    ACTION_SENTINEL_HILL_TALK_4 = 5,
    ACTION_SENTINEL_HILL_TALK_5 = 6,

    // MovePoints
    POINT_CRIME_SCENE        = 0,
    POINT_SENTINEL_HILL_GATE = 1,
    POINT_DESPAWN            = 2,

    // Text
    SAY_TRANSIENT_11 = 11,
    SAY_TRANSIENT_12 = 12,
    SAY_TRANSIENT_13 = 13,
    SAY_TRANSIENT_14 = 14,
    SAY_TRANSIENT_15 = 15,
    SAY_TRANSIENT_16 = 16,
    SAY_WEST_PLAINS_DRIFTER_0 = 0,
    SAY_WEST_PLAINS_DRIFTER_1 = 1,
    SAY_WEST_PLAINS_DRIFTER_2 = 2,
    SAY_WEST_PLAINS_DRIFTER_3 = 3,
    SAY_WEST_PLAINS_DRIFTER_4 = 4,
    SAY_WEST_PLAINS_DRIFTER_5 = 5,
    SAY_SENTINEL_HILL_GUARD_0 = 0,
    SAY_SENTINEL_HILL_GUARD_1 = 1,
    SAY_SENTINEL_HILL_GUARD_2 = 2,
    SAY_SENTINEL_HILL_GUARD_3 = 3,

    // DisplayID
    DISPLAY_HOBO_CART = 32849,

    // CreatureID
    NPC_HOBO_CART                    = 42399,
    NPC_WEST_PLAINS_DRIFTER_FOLLOWER = 42400,
    NPC_SENTINEL_HILL_GUARD          = 42407
};

Position const SentinelHillGuard1Pos = { -10341.4f, 978.075f, 31.2339f };
Position const SentinelHillGuard2Pos = { -10341.3f, 981.691f, 31.4328f };

struct npc_westfall_refugee_bridge_to_sentinelhill : public ScriptedAI
{
    npc_westfall_refugee_bridge_to_sentinelhill(Creature* creature) : ScriptedAI(creature) { }

    void JustAppeared() override
    {
        _events.ScheduleEvent(EVENT_START_MOVE, 4s);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != WAYPOINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
        case POINT_CRIME_SCENE:
            Talk(SAY_TRANSIENT_11);
            _events.ScheduleEvent(EVENT_CRIME_SCENE_DRIFTER_TALK_0, 6s);
            break;
        case POINT_SENTINEL_HILL_GATE:
        {
            std::vector<Creature*> guardList;
            Creature* guard1 = nullptr;
            Creature* guard2 = nullptr;
            me->GetCreatureListWithEntryInGrid(guardList, NPC_SENTINEL_HILL_GUARD, 8.f);
            for (Creature* guard : guardList)
            {
                if (guard->GetHomePosition().GetExactDist2d(SentinelHillGuard1Pos) < 0.1f)
                    guard1 = guard;

                if (guard->GetHomePosition().GetExactDist2d(SentinelHillGuard2Pos) < 0.1f)
                    guard2 = guard;

                if (guard1 && guard2)
                    break;
            }

            if (guard1 && guard2 && !guard1->IsInCombat() && !guard2->IsInCombat())
            {
                if (guard1->IsAIEnabled())
                {
                    guard1->AI()->DoAction(ACTION_SENTINEL_HILL_GATE_1);
                    guard1->AI()->SetGUID(me->GetGUID(), 0);
                }

                if (guard2->IsAIEnabled())
                {
                    guard2->AI()->DoAction(ACTION_SENTINEL_HILL_GATE_2);
                    guard2->AI()->SetGUID(me->GetGUID(), 0);
                }

                Creature* creature = ObjectAccessor::GetCreature(*me, _westPlainsDrifter);

                if (roll_chance_i(50))
                    _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_14, 10s);
                else
                {
                    if (creature)
                        if (creature->IsAIEnabled())
                            creature->AI()->DoAction(ACTION_SENTINEL_HILL_TALK_3);
                }

                if (roll_chance_i(50))
                    _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_15, 32s + 500ms);
                else
                {
                    if (creature)
                        if (creature->IsAIEnabled())
                            creature->AI()->DoAction(ACTION_SENTINEL_HILL_TALK_4);
                }

                if (roll_chance_i(50))
                    _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_15, 38s + 500ms);
                else
                {
                    if (creature)
                        if (creature->IsAIEnabled())
                            creature->AI()->DoAction(ACTION_SENTINEL_HILL_TALK_5);
                }
            }
            else
                _events.ScheduleEvent(EVENT_CANCEL, 5min);
            break;
        }
        case POINT_DESPAWN:
            _events.ScheduleEvent(EVENT_CANCEL, 0);
            break;
        default:
            break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START_MOVE:
                    me->GetMotionMaster()->MovePath(me->GetEntry() * 100, false);
                    break;
                case EVENT_CRIME_SCENE_DRIFTER_TALK_0:
                    if (Creature* creature = me->FindNearestCreature(NPC_WEST_PLAINS_DRIFTER_FOLLOWER, 5.f))
                    {
                        if (creature->IsAIEnabled())
                            creature->AI()->DoAction(ACTION_CRIME_SCENE);
                        _westPlainsDrifter = creature->GetGUID();
                    }
                    _events.ScheduleEvent(EVENT_CRIME_SCENE_TALK_12, 6s);
                    break;
                case EVENT_CRIME_SCENE_TALK_12:
                    Talk(SAY_TRANSIENT_12);
                    _events.ScheduleEvent(EVENT_CRIME_SCENE_TALK_13, 12s);
                    break;
                case EVENT_CRIME_SCENE_TALK_13:
                    Talk(SAY_TRANSIENT_13);
                    break;
                case EVENT_SENTINEL_HILL_TALK_14:
                    Talk(SAY_TRANSIENT_14);
                    break;
                case EVENT_SENTINEL_HILL_TALK_15:
                    Talk(SAY_TRANSIENT_15);
                    break;
                case EVENT_SENTINEL_HILL_TALK_16:
                    Talk(SAY_TRANSIENT_16);
                    break;
                case EVENT_CANCEL:
                    if (Creature* creature = me->FindNearestCreature(NPC_WEST_PLAINS_DRIFTER_FOLLOWER, 5.f))
                        creature->DespawnOrUnsummon();
                    me->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _westPlainsDrifter;
};

struct npc_westfall_west_plains_drifter_walk_to_sentinel : public ScriptedAI
{
    npc_westfall_west_plains_drifter_walk_to_sentinel(Creature* creature) : ScriptedAI(creature) { }

    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_HOBO_CART)
            summon->SetDisplayId(DISPLAY_HOBO_CART);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_CRIME_SCENE:
                Talk(SAY_WEST_PLAINS_DRIFTER_0);
                _events.ScheduleEvent(EVENT_CRIME_SCENE_DRIFTER_TALK_1, 12s);
                break;
            case ACTION_SENTINEL_HILL_TALK_3:
                _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_3, 10s);
                break;
            case ACTION_SENTINEL_HILL_TALK_4:
                _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_4, 32s + 500ms);
                break;
            case ACTION_SENTINEL_HILL_TALK_5:
                _events.ScheduleEvent(EVENT_SENTINEL_HILL_TALK_5, 38s + 500ms);
                break;
            default:
                return;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CRIME_SCENE_DRIFTER_TALK_1:
                    Talk(SAY_WEST_PLAINS_DRIFTER_1);
                    _events.ScheduleEvent(EVENT_CRIME_SCENE_DRIFTER_TALK_2, 11s);
                    break;
                case EVENT_CRIME_SCENE_DRIFTER_TALK_2:
                    Talk(SAY_WEST_PLAINS_DRIFTER_2);
                    break;
                case EVENT_SENTINEL_HILL_TALK_3:
                    Talk(SAY_WEST_PLAINS_DRIFTER_3);
                    break;
                case EVENT_SENTINEL_HILL_TALK_4:
                    Talk(SAY_WEST_PLAINS_DRIFTER_4);
                    break;
                case EVENT_SENTINEL_HILL_TALK_5:
                    Talk(SAY_WEST_PLAINS_DRIFTER_5);
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
};

struct npc_westfall_sentinel_hill_guard : public ScriptedAI
{
    npc_westfall_sentinel_hill_guard(Creature* creature) : ScriptedAI(creature) { }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_SENTINEL_HILL_GATE_1:
                Talk(SAY_SENTINEL_HILL_GUARD_0);
                _events.ScheduleEvent(EVENT_SENTINEL_HILL_GUARD1_TALK_1, 4s);
                break;
            case ACTION_SENTINEL_HILL_GATE_2:
                _events.ScheduleEvent(EVENT_SENTINEL_HILL_GUARD2_TALK_2, 17s);
                break;
            default:
                return;
        }
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        _transientGuid = guid;
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SENTINEL_HILL_GUARD1_TALK_1:
                    Talk(SAY_SENTINEL_HILL_GUARD_1);
                    break;
                case EVENT_SENTINEL_HILL_GUARD2_TALK_2:
                    Talk(SAY_SENTINEL_HILL_GUARD_2);
                    _events.ScheduleEvent(EVENT_SENTINEL_HILL_GUARD2_TALK_3, 6s);
                    break;
                case EVENT_SENTINEL_HILL_GUARD2_TALK_3:
                    Talk(SAY_SENTINEL_HILL_GUARD_3);
                    break;
                default:
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    ObjectGuid _transientGuid;
};

enum ThugText
{
    SAY_THUG_0 = 0,
    SAY_THUG_1 = 1,
    SAY_THUG_2 = 2,
    SAY_THUG_3 = 3,
    SAY_THUG_4 = 4,
    SAY_THUG_5 = 5,
    SAY_THUG_6 = 6,
    SAY_WARNING = 0
};

enum ThugEvent
{
    EVENT_SUMMON_THUGS = 1,
    EVENT_THUG1_SAY_0  = 2,
    EVENT_THUG2_SAY_1  = 3,
    EVENT_THUG2_SAY_2  = 4,
    EVENT_THUG3_SAY_3  = 5,
    EVENT_THUG1_SAY_4  = 6,
    EVENT_THUG1_SAY_5  = 7,
    EVENT_THUG1_SAY_6  = 8,
    EVENT_THUG_CREDIT  = 9,
    EVENT_THUG_RESET   = 10,
    EVENT_THUG_SHOOT_1 = 11,
    EVENT_THUG_SHOOT_2 = 12,
    EVENT_THUG_SCREAM  = 13,
    EVENT_THUG_WARNING = 14
};

enum ThugAction
{
    ACTION_THUG_RESET        = 1
};

enum ThugData
{
    DATA_THUG_DEATH = 1
};

enum ThugMisc
{
    OBJECT_SOUND_SHOOTING = 15071,
    OBJECT_SOUND_SCREAM   = 17852
};

class at_westfall_two_shoed_lou_thugs : public AreaTriggerScript
{
public:
    at_westfall_two_shoed_lou_thugs() : AreaTriggerScript("at_westfall_two_shoed_lou_thugs") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (player->IsAlive())
            if (player->GetQuestStatus(QUEST_LOUS_PARTING_THOUGHTS) == QUEST_STATUS_INCOMPLETE)
                if (Creature* lousPartingThoughtsTrigger = player->FindNearestCreature(MPC_LOUS_PARTING_THOUGHTS_TRIGGER, 50.0f, true))
                    if (lousPartingThoughtsTrigger->IsAIEnabled())
                        lousPartingThoughtsTrigger->AI()->SetGUID(player->GetGUID());

        return true;
    }
};

struct npc_westfall_thug : public ScriptedAI
{
    npc_westfall_thug(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        ScriptedAI::Reset();
        _events.Reset();
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (Unit* summon = me->ToTempSummon()->GetSummoner())
            if (Creature* creature = summon->ToCreature())
                if (creature->IsAIEnabled())
                    creature->AI()->DoAction(ACTION_THUG_RESET);

        me->DespawnOrUnsummon();
    }

    void JustDied(Unit* /*who*/) override
    {
        if (Unit* summon = me->ToTempSummon()->GetSummoner())
            if (Creature* creature = summon->ToCreature())
                if (creature->IsAIEnabled())
                    creature->AI()->SetData(0, DATA_THUG_DEATH);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
};

Position const ThugPos[4] =
{
    { -9859.36f, 1332.42f, 41.9859f, 2.49582f  },
    { -9862.52f, 1332.08f, 41.9859f, 0.855211f },
    { -9863.49f, 1335.49f, 41.9859f, 5.63741f  },
    { -9860.43f, 1335.46f, 41.9859f, 4.11898f  },
};

struct npc_westfall_lous_parting_thoughts_trigger : public ScriptedAI
{
    npc_westfall_lous_parting_thoughts_trigger(Creature* creature) : ScriptedAI(creature), _summons(), _thugDeathCount(0) { }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!_eventInvokerGUID.IsEmpty())
            return;

        _eventInvokerGUID = guid;

        _events.ScheduleEvent(EVENT_THUG1_SAY_0, 0s);
    }

    void SetData(uint32 /*type*/, uint32 data) override
    {
        if (data == DATA_THUG_DEATH)
        {
            _thugDeathCount++;

            if (_thugDeathCount >= 4)
            {
                _events.ScheduleEvent(EVENT_THUG_CREDIT, 0s);
                DoAction(ACTION_THUG_RESET);
            }
        }
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_THUG_RESET:
                _events.ScheduleEvent(EVENT_SUMMON_THUGS, 60s);
                break;
        }
    }

    void JustAppeared() override
    {
        _events.ScheduleEvent(EVENT_SUMMON_THUGS, 0s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SUMMON_THUGS:
                    _summons[0] = me->SummonCreature(NPC_THUG, ThugPos[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s)->GetGUID();
                    _summons[1] = me->SummonCreature(NPC_THUG, ThugPos[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s)->GetGUID();
                    _summons[2] = me->SummonCreature(NPC_THUG, ThugPos[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s)->GetGUID();
                    _summons[3] = me->SummonCreature(NPC_THUG, ThugPos[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s)->GetGUID();
                    _eventInvokerGUID = ObjectGuid::Empty;
                    _thugDeathCount = 0;
                    break;
                case EVENT_THUG1_SAY_0:
                {
                    Creature* thug1 = ObjectAccessor::GetCreature(*me, _summons[0]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug1)
                        if (thug1->IsAIEnabled())
                            thug1->AI()->Talk(SAY_THUG_0, invoker);
                    _events.ScheduleEvent(EVENT_THUG2_SAY_1, 5s);
                    break;
                }
                case EVENT_THUG2_SAY_1:
                {
                    Creature* thug2 = ObjectAccessor::GetCreature(*me, _summons[1]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug2)
                        if (thug2->IsAIEnabled())
                            thug2->AI()->Talk(SAY_THUG_1, invoker);
                    _events.ScheduleEvent(EVENT_THUG2_SAY_2, 5s);
                    break;
                }
                case EVENT_THUG2_SAY_2:
                {
                    Creature* thug2 = ObjectAccessor::GetCreature(*me, _summons[1]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug2)
                        if (thug2->IsAIEnabled())
                            thug2->AI()->Talk(SAY_THUG_2, invoker);
                    _events.ScheduleEvent(EVENT_THUG3_SAY_3, 8s + 500ms);
                    break;
                }
                case EVENT_THUG3_SAY_3:
                {
                    Creature* thug3 = ObjectAccessor::GetCreature(*me, _summons[2]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug3)
                        if (thug3->IsAIEnabled())
                            thug3->AI()->Talk(SAY_THUG_3, invoker);
                    _events.ScheduleEvent(EVENT_THUG1_SAY_4, 5s);
                    break;
                }
                case EVENT_THUG1_SAY_4:
                {
                    Creature* thug1 = ObjectAccessor::GetCreature(*me, _summons[0]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug1)
                    {
                        if (thug1->IsAIEnabled())
                            thug1->AI()->Talk(SAY_THUG_4, invoker);

                        for (ObjectGuid const& guid : _summons)
                            if (Creature* thug = ObjectAccessor::GetCreature(*me, guid))
                                thug->SetFacingToObject(invoker);
                    }

                    _events.ScheduleEvent(EVENT_THUG1_SAY_5, 8s + 500ms);
                    break;
                }
                case EVENT_THUG1_SAY_5:
                {
                    Creature* thug1 = ObjectAccessor::GetCreature(*me, _summons[0]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug1)
                        if (thug1->IsAIEnabled())
                            thug1->AI()->Talk(SAY_THUG_5, invoker);


                    _events.ScheduleEvent(EVENT_THUG1_SAY_6, 5s);
                    break;
                }
                case EVENT_THUG1_SAY_6:
                {
                    Creature* thug1 = ObjectAccessor::GetCreature(*me, _summons[0]);
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker && thug1)
                    {
                        if (thug1->IsAIEnabled())
                            thug1->AI()->Talk(SAY_THUG_6, invoker);

                        for (ObjectGuid const& guid : _summons)
                            if (Creature* thug = ObjectAccessor::GetCreature(*me, guid))
                            {
                                thug->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC);
                                if (thug->IsAIEnabled())
                                    thug->AI()->AttackStart(invoker);
                            }
                    }
                    break;
                }
                case EVENT_THUG_CREDIT:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                    {
                        invoker->CastSpell(invoker, SPELL_HOBO_INFORMATION, TriggerCastFlags(TRIGGERED_FULL_MASK));

                        if (Group* group = invoker->GetGroup())
                        {
                            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                            {
                                Player* groupMember = itr->GetSource();
                                if (groupMember && groupMember->IsInMap(invoker) && groupMember->GetQuestStatus(QUEST_LOUS_PARTING_THOUGHTS) == QUEST_STATUS_INCOMPLETE && groupMember->GetDistance(me) <= 75.f)
                                {
                                    groupMember->CastSpell(groupMember, SPELL_HOBO_INFORMATION, TriggerCastFlags(TRIGGERED_FULL_MASK));
                                }
                            }
                        }
                    }
                    _events.ScheduleEvent(EVENT_THUG_SHOOT_1, 1s + 500ms);
                    break;
                }
                case EVENT_THUG_SHOOT_1:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                        me->PlayDistanceSound(OBJECT_SOUND_SHOOTING, invoker);

                    _events.ScheduleEvent(EVENT_THUG_SHOOT_2, 1s + 200ms);
                    break;
                }
                case EVENT_THUG_SHOOT_2:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                        me->PlayDistanceSound(OBJECT_SOUND_SHOOTING, invoker);

                    _events.ScheduleEvent(EVENT_THUG_SCREAM, 1s + 200ms);
                    break;
                }
                case EVENT_THUG_SCREAM:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                        me->PlayDistanceSound(OBJECT_SOUND_SCREAM, invoker);

                    _events.ScheduleEvent(EVENT_THUG_WARNING, 2s + 500ms);
                    break;
                }
                case EVENT_THUG_WARNING:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                        Talk(SAY_WARNING, invoker);
                    break;
                }
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _eventInvokerGUID;
    std::array<ObjectGuid, 4> _summons;
    uint32 _thugDeathCount;
};

enum SmallTimeEvent
{
    EVENT_TALK_0 = 1,
    EVENT_TALK_1 = 2,
    EVENT_RESET  = 3
};

enum SmallTimeSay
{
    SAY_SMALL_TIME_0 = 0,
    SAY_SMALL_TIME_1 = 1
};

class at_westfall_small_time_hustler : public AreaTriggerScript
{
public:
    at_westfall_small_time_hustler() : AreaTriggerScript("at_westfall_small_time_hustler") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (player->IsAlive())
            if (Creature* lousPartingThoughtsTrigger = player->FindNearestCreature(NPC_SMALL_TIME_HUSTLER, 10.0f, true))
                if (lousPartingThoughtsTrigger->IsAIEnabled())
                    lousPartingThoughtsTrigger->AI()->SetGUID(player->GetGUID());

        return true;
    }
};

struct npc_westfall_small_time_hustler : public ScriptedAI
{
    npc_westfall_small_time_hustler(Creature* creature) : ScriptedAI(creature) { }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!_eventInvokerGUID.IsEmpty())
            return;

        _eventInvokerGUID = guid;

        _events.ScheduleEvent(EVENT_TALK_0, 0s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TALK_0:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                    {
                        me->SetFacingToObject(invoker);
                        Talk(SAY_SMALL_TIME_0, invoker);
                        _events.ScheduleEvent(EVENT_TALK_1, 4s);
                    }
                    break;
                }
                case EVENT_TALK_1:
                {
                    Player* invoker = ObjectAccessor::GetPlayer(*me, _eventInvokerGUID);
                    if (invoker)
                    {
                        Talk(SAY_SMALL_TIME_1, invoker);
                        _events.ScheduleEvent(EVENT_RESET, 60s);
                    }
                    break;
                }
                case EVENT_RESET:
                {
                    _eventInvokerGUID = ObjectGuid::Empty;
                    break;
                }
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _eventInvokerGUID;
};

enum WestFallStewNpc
{
    NPC_WESTFALL_STEW = 42617
};

enum WestFallStewSpell
{
    SPELL_FULL_BELLY     = 79451,
    SPELL_COSMETIC_SLEEP = 78677
};

enum WestFallStewEvent
{
    EVENT_START = 1,
    EVENT_MOVE_TO_STEW,
    EVENT_RETURN_TO_HOME
};

enum WestFallText
{
    SAY_NPC_HOMELESS_STORMWIND_CITIZEN_1 = 11,
    SAY_NPC_HOMELESS_STORMWIND_CITIZEN_2 = 12,
    SAY_NPC_TRANSIENT                    = 17,
    SAY_NPC_WEST_PLAINS_DRIFTER          = 11,
    SAY_NPC_WEST_PLAINS_DRIFTER_FOLLOWER = 6
};

struct npc_westfall_westfall_stew: public ScriptedAI
{
    npc_westfall_westfall_stew(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (!summoner)
            return;

        std::vector<Creature*> homelessCitizens;
        me->GetCreatureListWithEntryInGrid(homelessCitizens, NPC_HOMELESS_STORMWIND_CITIZEN_1, 8.f);
        me->GetCreatureListWithEntryInGrid(homelessCitizens, NPC_HOMELESS_STORMWIND_CITIZEN_2, 8.f);
        me->GetCreatureListWithEntryInGrid(homelessCitizens, NPC_TRANSIENT, 8.f);
        me->GetCreatureListWithEntryInGrid(homelessCitizens, NPC_WEST_PLAINS_DRIFTER, 8.f);
        me->GetCreatureListWithEntryInGrid(homelessCitizens, NPC_WEST_PLAINS_DRIFTER_FOLLOWER, 8.f);
        for (Creature* homelessCitizen : homelessCitizens)
        {
            if (homelessCitizen->HasAura(SPELL_FULL_BELLY))
                continue;

            homelessCitizen->AI()->SetGUID(me->GetGUID());
        }
    }

private:
    EventMap _events;
};

struct npc_westfall_homless_stormwind_citizen : public ScriptedAI
{
    npc_westfall_homless_stormwind_citizen(Creature* creature) : ScriptedAI(creature) { }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!_westfallStewGUID.IsEmpty())
            return;

        _westfallStewGUID = guid;

        _events.ScheduleEvent(EVENT_START, 0);
    }

    void MovementInform(uint32 MotionType, uint32 PointId) override
    {
        if ((MotionType == POINT_MOTION_TYPE) && (PointId == 1))
        {
            me->CastSpell(me, SPELL_FULL_BELLY);

            if (Creature* stew = ObjectAccessor::GetCreature(*me, _westfallStewGUID))
            {
                if (Unit* summoner = stew->ToTempSummon()->GetSummoner())
                    if (Player* player = summoner->ToPlayer())
                        player->KilledMonsterCredit(NPC_WESTFALL_STEW);

                if (roll_chance_i(50))
                {
                    switch (me->GetEntry())
                    {
                        case NPC_HOMELESS_STORMWIND_CITIZEN_1:
                            Talk(SAY_NPC_HOMELESS_STORMWIND_CITIZEN_1, stew);
                            break;
                        case NPC_HOMELESS_STORMWIND_CITIZEN_2:
                            Talk(SAY_NPC_HOMELESS_STORMWIND_CITIZEN_2, stew);
                            break;
                        case NPC_TRANSIENT:
                            Talk(SAY_NPC_TRANSIENT, stew);
                            break;
                        case NPC_WEST_PLAINS_DRIFTER:
                            Talk(SAY_NPC_WEST_PLAINS_DRIFTER, stew);
                            break;
                        case NPC_WEST_PLAINS_DRIFTER_FOLLOWER:
                            Talk(SAY_NPC_WEST_PLAINS_DRIFTER_FOLLOWER, stew);
                            break;
                        default:
                            break;
                    }
                }
            }
            _events.ScheduleEvent(EVENT_RETURN_TO_HOME, 5s + 500ms);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START:
                {
                    me->SetStandState(UNIT_STAND_STATE_STAND);

                    if (me->HasAura(SPELL_COSMETIC_SLEEP))
                        me->RemoveAurasDueToSpell(SPELL_COSMETIC_SLEEP);

                    _events.ScheduleEvent(EVENT_MOVE_TO_STEW, 2s);
                    break;
                }
                case EVENT_MOVE_TO_STEW:
                {
                    Creature* stew = ObjectAccessor::GetCreature(*me, _westfallStewGUID);
                    if (stew)
                    {
                        float x, y, z;
                        stew->GetContactPoint(me, x, y, z, 0.2f);
                        me->GetMotionMaster()->MovePoint(1, x, y, z);
                    }
                    else
                        me->GetMotionMaster()->MoveTargetedHome();
                    break;
                }
                case EVENT_RETURN_TO_HOME:
                {
                    me->GetMotionMaster()->MoveTargetedHome();
                    break;
                }
                default:
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    ObjectGuid _westfallStewGUID;
};

struct npc_westfall_west_plains_drifter : public ScriptedAI
{
    npc_westfall_west_plains_drifter(Creature* creature) : ScriptedAI(creature) { }

    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_HOBO_CART)
            summon->SetDisplayId(DISPLAY_HOBO_CART);
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!_westfallStewGUID.IsEmpty())
            return;

        _westfallStewGUID = guid;
        _events.ScheduleEvent(EVENT_START, 0);
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType != POINT_MOTION_TYPE || pointId != 1)
            return;

        me->CastSpell(me, SPELL_FULL_BELLY);

        if (Creature* stew = ObjectAccessor::GetCreature(*me, _westfallStewGUID))
        {
            // Should never happen, unless the stew has been spawned via db which indicates dirty db data
            if (!stew->IsSummon())
                return;

            if (Unit* summoner = stew->ToTempSummon()->GetSummoner())
                if (Player* player = summoner->ToPlayer())
                    player->KilledMonsterCredit(NPC_WESTFALL_STEW);

            if (roll_chance_i(50))
            {
                switch (me->GetEntry())
                {
                    case NPC_HOMELESS_STORMWIND_CITIZEN_1:
                        Talk(SAY_NPC_HOMELESS_STORMWIND_CITIZEN_1, stew);
                        break;
                    case NPC_HOMELESS_STORMWIND_CITIZEN_2:
                        Talk(SAY_NPC_HOMELESS_STORMWIND_CITIZEN_2, stew);
                        break;
                    case NPC_TRANSIENT:
                        Talk(SAY_NPC_TRANSIENT, stew);
                        break;
                    case NPC_WEST_PLAINS_DRIFTER:
                        Talk(SAY_NPC_WEST_PLAINS_DRIFTER, stew);
                        break;
                    case NPC_WEST_PLAINS_DRIFTER_FOLLOWER:
                        Talk(SAY_NPC_WEST_PLAINS_DRIFTER_FOLLOWER, stew);
                        break;
                    default:
                        break;
                }
            }
        }
        _events.ScheduleEvent(EVENT_RETURN_TO_HOME, 5s + 500ms);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START:
                    me->SetStandState(UNIT_STAND_STATE_STAND);

                    if (me->HasAura(SPELL_COSMETIC_SLEEP))
                        me->RemoveAurasDueToSpell(SPELL_COSMETIC_SLEEP);

                    _events.ScheduleEvent(EVENT_MOVE_TO_STEW, 2s);
                    break;
                case EVENT_MOVE_TO_STEW:
                    if (Creature* stew = ObjectAccessor::GetCreature(*me, _westfallStewGUID))
                    {
                        float x, y, z;
                        stew->GetContactPoint(me, x, y, z, 0.2f);
                        me->GetMotionMaster()->MovePoint(1, x, y, z);
                    }
                    else
                        me->GetMotionMaster()->MoveTargetedHome();
                    break;
                case EVENT_RETURN_TO_HOME:
                    me->GetMotionMaster()->MoveTargetedHome();
                    break;
                default:
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    ObjectGuid _westfallStewGUID;
};

class spell_westfall_sniper_fire_proc : public AuraScript
{
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        _target = (eventInfo.GetProcTarget() == GetTarget() ? eventInfo.GetActor() : eventInfo.GetProcTarget());
        return _target && _target->IsCreature() && _target->GetEntry() == NPC_MERCENARY;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(_target, GetSpellInfo()->Effects[aurEff->GetEffIndex()].TriggerSpell);

    }

    void Register() override
    {
        DoCheckProc.Register(&spell_westfall_sniper_fire_proc::CheckProc);
        OnEffectProc.Register(&spell_westfall_sniper_fire_proc::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }

private:
    Unit* _target = nullptr;
};

enum AgentKearnenText
{
    SAY_0 = 0
};

class spell_westfall_sniper_fire : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_KILL_SHOT_TRIGGERED });
    }

    void HandleDummyEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        Unit* caster = GetCaster();
        if (caster && target)
            if (Creature* agent = caster->FindNearestCreature(NPC_AGENT_KEARNEN, 100.f))
            {
                agent->CastSpell(target, SPELL_KILL_SHOT_TRIGGERED); // 79525
                if (agent->IsAIEnabled())
                    agent->AI()->Talk(SAY_0, caster);
            }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_westfall_sniper_fire::HandleDummyEffect, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};
}

// Azimov : « L'aube d'un nouveau jour » (26297) : l'événement du rassemblement de Ruisselune ne démarre pas.
// On valide l'objectif quand le joueur arrive au cœur de Ruisselune (point d'intérêt de la quête).
static constexpr uint32 QUEST_DAWNING_OF_A_NEW_DAY = 26297;
static Position const MoonbrookGatheringPos = { -11013.0f, 1475.0f, 47.0f, 0.0f };
static constexpr float MOONBROOK_GATHERING_RADIUS = 80.0f;
static constexpr uint32 AREA_MOONBROOK = 20;

// Azimov : « Le soulèvement de la Confrérie » (26322) : scène de la Colline des Sentinelles (étage de l'auberge).
// Aucun script ni texte dans la base : la scène est jouée ici, dialogues écrits en français dans l'esprit
// de l'originale. Elle démarre quand un joueur qui a la quête monte à l'étage où se tiennent Gryan et Horatio.
static constexpr uint32 QUEST_RISE_OF_THE_BROTHERHOOD = 26322;
static constexpr uint32 NPC_RISE_GRYAN        = 234;
static constexpr uint32 NPC_RISE_HORATIO      = 42308;
static constexpr uint32 NPC_RISE_DANUVIN      = 821;
static constexpr uint32 NPC_RISE_GALIAAN      = 878;
static constexpr uint32 NPC_RISE_VANESSA      = 42371;
static constexpr uint32 NPC_RISE_BLACKGUARD   = 42769;
static constexpr uint32 NPC_RISE_BANNER       = 42683;
static Position const RiseCenterPos   = { -10508.5f, 1044.5f, 60.52f, 0.0f };
static Position const RiseVanessaFrom = { -10499.5f, 1044.0f, 60.61f, 3.14f };
static Position const RiseVanessaTo   = { -10505.6f, 1045.0f, 60.61f, 3.10f };
static Position const RiseBannerPos   = { -10507.0f, 1043.2f, 60.61f, 0.0f };
static Position const RiseBlackguardPos[] =
{
    { -10503.2f, 1048.4f, 60.61f, 3.9f },
    { -10513.4f, 1043.2f, 60.61f, 0.2f },
    { -10505.0f, 1038.3f, 60.61f, 2.0f },
    { -10512.2f, 1049.2f, 60.61f, 5.3f },
};

struct RiseOfTheBrotherhoodEvent
{
    bool Active = false;
    uint8 Step = 0;
    int32 Timer = 0;
    std::string HeroName;
    ObjectGuid Gryan, Horatio, Danuvin, Galiaan, Vanessa, Banner;
    std::vector<ObjectGuid> Blackguards;

    Creature* Get(Map* map, ObjectGuid const& guid) { return guid.IsEmpty() ? nullptr : map->GetCreature(guid); }
    void Say(Map* map, ObjectGuid const& guid, std::string const& text) { if (Creature* c = Get(map, guid)) c->Say(text, LANG_UNIVERSAL); }
    void Yell(Map* map, ObjectGuid const& guid, std::string const& text) { if (Creature* c = Get(map, guid)) c->Yell(text, LANG_UNIVERSAL); }
    void Stand(Map* map, ObjectGuid const& guid, uint8 state) { if (Creature* c = Get(map, guid)) c->SetStandState(state); }

    template <typename F>
    void ForNearbyPlayers(Map* map, F&& func)
    {
        for (auto const& ref : map->GetPlayers())
            if (Player* player = ref.GetSource())
                if (player->IsInWorld() && player->GetExactDist(&RiseCenterPos) < 25.0f)
                    func(player);
    }

    void Start(Player* hero)
    {
        Active = true;
        Step = 0;
        Timer = 1000;
        HeroName = hero->GetName();
        Blackguards.clear();
        Banner.Clear();
        Vanessa.Clear();
        auto find = [hero](uint32 entry) { Creature* c = hero->FindNearestCreature(entry, 30.0f); return c ? c->GetGUID() : ObjectGuid::Empty; };
        Gryan = find(NPC_RISE_GRYAN);
        Horatio = find(NPC_RISE_HORATIO);
        Danuvin = find(NPC_RISE_DANUVIN);
        Galiaan = find(NPC_RISE_GALIAAN);
    }

    // une étape ; renvoie le délai avant la suivante (0 = fin)
    int32 DoStep(Map* map)
    {
        Creature* gryan = Get(map, Gryan);
        switch (Step++)
        {
            case 0:
                Say(map, Gryan, "Lieutenant, " + HeroName + " m'a tout raconté. VanCleef avait une fille... et elle aurait tout vu, cette nuit-là.");
                return 6000;
            case 1:
                Say(map, Horatio, "Elle est peut-être plus près que vous ne le croyez, maréchal. Les sans-abri parlent d'une femme qui leur promet une « aube nouvelle ».");
                return 7000;
            case 2:
                Say(map, Danuvin, "Maréchal ! Les gnolls se replient... et les sentinelles du chemin ne répondent plus !");
                return 4000;
            case 3:
                if (gryan)
                    for (Position const& pos : RiseBlackguardPos)
                        if (TempSummon* guard = gryan->SummonCreature(NPC_RISE_BLACKGUARD, pos, TEMPSUMMON_TIMED_DESPAWN, 60000))
                        {
                            guard->SetFaction(35);
                            guard->SetReactState(REACT_PASSIVE);
                            if (Blackguards.empty())
                                guard->TextEmote("Des Gardes noirs défias jaillissent des ombres !");
                            Blackguards.push_back(guard->GetGUID());
                        }
                Say(map, Gryan, "EN GARDE !");
                return 3000;
            case 4:
                // les gardes de la colline tombent, assommés
                Stand(map, Danuvin, UNIT_STAND_STATE_DEAD);
                Stand(map, Galiaan, UNIT_STAND_STATE_DEAD);
                Stand(map, Horatio, UNIT_STAND_STATE_KNEEL);
                if (gryan)
                    if (TempSummon* vanessa = gryan->SummonCreature(NPC_RISE_VANESSA, RiseVanessaFrom, TEMPSUMMON_TIMED_DESPAWN, 60000))
                    {
                        vanessa->SetReactState(REACT_PASSIVE);
                        vanessa->SetWalk(true);
                        vanessa->GetMotionMaster()->MovePoint(1, RiseVanessaTo);
                        Vanessa = vanessa->GetGUID();
                    }
                return 5000;
            case 5:
                Yell(map, Vanessa, "Bonsoir, maréchal Chope-Fer. Vous me cherchiez ? Je suis Vanessa VanCleef.");
                return 6000;
            case 6:
                Say(map, Vanessa, "Vous avez laissé mon père mourir et la Marche de l'Ouest pourrir. Ses enfants oubliés sont devenus les miens.");
                return 7000;
            case 7:
                Say(map, Vanessa, "Non, je ne suis pas venue vous tuer. Pas encore. Je veux que vous voyiez ce que nous ferons de Hurlevent.");
                return 6000;
            case 8:
                if (Creature* vanessa = Get(map, Vanessa))
                    vanessa->TextEmote("Vanessa VanCleef vous injecte un poison paralysant !", nullptr, true);
                ForNearbyPlayers(map, [](Player* player) { player->SetStandState(UNIT_STAND_STATE_DEAD); });
                Stand(map, Gryan, UNIT_STAND_STATE_KNEEL);
                return 4000;
            case 9:
                Yell(map, Vanessa, "Frères et sœurs de la Marche de l'Ouest, levez-vous ! Ce soir, la Confrérie défias renaît !");
                if (gryan)
                    if (TempSummon* banner = gryan->SummonCreature(NPC_RISE_BANNER, RiseBannerPos, TEMPSUMMON_TIMED_DESPAWN, 120000))
                        Banner = banner->GetGUID();
                return 5000;
            case 10:
                // Vanessa et ses Gardes noirs disparaissent dans l'ombre
                if (Creature* vanessa = Get(map, Vanessa))
                    vanessa->DespawnOrUnsummon();
                for (ObjectGuid const& guid : Blackguards)
                    if (Creature* guard = Get(map, guid))
                        guard->DespawnOrUnsummon();
                return 4000;
            case 11:
                ForNearbyPlayers(map, [](Player* player)
                {
                    player->SetStandState(UNIT_STAND_STATE_STAND);
                    if (player->GetQuestStatus(QUEST_RISE_OF_THE_BROTHERHOOD) == QUEST_STATUS_INCOMPLETE)
                        player->AreaExploredOrEventHappens(QUEST_RISE_OF_THE_BROTHERHOOD);
                });
                Stand(map, Danuvin, UNIT_STAND_STATE_STAND);
                Stand(map, Galiaan, UNIT_STAND_STATE_STAND);
                Stand(map, Horatio, UNIT_STAND_STATE_STAND);
                Stand(map, Gryan, UNIT_STAND_STATE_STAND);
                Say(map, Gryan, "Par la Lumière... Elle était là, sous notre nez, depuis le début. " + HeroName + ", nous devons prévenir Hurlevent !");
                return 0;
            default:
                return 0;
        }
    }

    void Update(Map* map, uint32 diff)
    {
        if (!Active)
            return;
        Timer -= int32(diff);
        if (Timer > 0)
            return;
        Timer = DoStep(map);
        if (Timer <= 0)
            Active = false;
    }
};

class world_map_azimov_moonbrook_gathering : public WorldMapScript
{
public:
    world_map_azimov_moonbrook_gathering() : WorldMapScript("world_map_azimov_moonbrook_gathering", 0) { }

    void OnUpdate(Map* map, uint32 diff) override
    {
        _rise.Update(map, diff);

        if (_timer > diff)
        {
            _timer -= diff;
            return;
        }
        _timer = 2000;

        for (auto const& ref : map->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (!player || !player->IsInWorld() || !player->IsAlive())
                continue;

            // « Le soulèvement de la Confrérie » : à l'étage de l'auberge de la Colline des Sentinelles
            if (!_rise.Active && player->GetQuestStatus(QUEST_RISE_OF_THE_BROTHERHOOD) == QUEST_STATUS_INCOMPLETE
                && player->GetExactDist(&RiseCenterPos) < 12.0f && std::fabs(player->GetPositionZ() - RiseCenterPos.GetPositionZ()) < 4.0f)
                _rise.Start(player);

            if (player->GetQuestStatus(QUEST_DAWNING_OF_A_NEW_DAY) != QUEST_STATUS_INCOMPLETE)
                continue;

            // dans la zone de Ruisselune (AreaTable 20) ou près du point de la quête
            if (player->GetAreaId() != AREA_MOONBROOK
                && player->GetExactDist2d(MoonbrookGatheringPos.GetPositionX(), MoonbrookGatheringPos.GetPositionY()) > MOONBROOK_GATHERING_RADIUS)
                continue;

            ChatHandler(player->GetSession()).PSendSysMessage("|cffffd700Vous vous mêlez à la foule et entendez la Silhouette promettre une « aube nouvelle » aux sans-abri de la Marche de l'Ouest…|r");
            player->CompleteQuest(QUEST_DAWNING_OF_A_NEW_DAY);
        }
    }

private:
    uint32 _timer = 2000;
    RiseOfTheBrotherhoodEvent _rise;
};

// Azimov : « Une vision du passé » (26320) : le Brûle-encens est refusé à l'entrée des Mortemines
// (« Requiert votre présence à l'intérieur de l'entrée des Mortemines »). La quête est validée en entrant
// dans le donjon des Mortemines (carte 36), sans la vision.
static constexpr uint32 QUEST_A_VISION_OF_THE_PAST = 26320;
static constexpr uint32 MAP_DEADMINES = 36;

class player_azimov_vision_of_the_past : public PlayerScript
{
public:
    player_azimov_vision_of_the_past() : PlayerScript("player_azimov_vision_of_the_past") { }

    void OnMapChanged(Player* player) override
    {
        if (!player || player->GetMapId() != MAP_DEADMINES)
            return;

        if (player->GetQuestStatus(QUEST_A_VISION_OF_THE_PAST) != QUEST_STATUS_INCOMPLETE)
            return;

        ChatHandler(player->GetSession()).PSendSysMessage("|cffffd700L'encens se consume à l'entrée des Mortemines… Vous revoyez la nuit où Edwin VanCleef est tombé, et sa fille Vanessa jurer vengeance.|r");
        player->CompleteQuest(QUEST_A_VISION_OF_THE_PAST);
    }
};

void AddSC_westfall()
{
    using namespace Westfall;
    RegisterSpellScript(spell_westfall_unbound_energy);
    RegisterCreatureAI(npc_westfall_overloaded_harvest_golem);
    RegisterSpellScript(spell_westfall_reaping_blows);
    RegisterSpellScript(spell_westfall_wake_harvest_golem);
    RegisterSpellScript(spell_westfall_quest_credit_jangolode_event);
    RegisterCreatureAI(npc_westfall_hobo_witness);
    RegisterSpellScript(spell_westfall_summon_ragamuffin_looter);
    RegisterSpellScript(spell_westfall_aggro_hobo);
    RegisterCreatureAI(npc_westfall_refugee_bridge_to_sentinelhill);
    RegisterCreatureAI(npc_westfall_west_plains_drifter_walk_to_sentinel);
    RegisterCreatureAI(npc_westfall_sentinel_hill_guard);
    new at_westfall_two_shoed_lou_thugs();
    RegisterCreatureAI(npc_westfall_thug);
    RegisterCreatureAI(npc_westfall_lous_parting_thoughts_trigger);
    new at_westfall_small_time_hustler();
    RegisterCreatureAI(npc_westfall_small_time_hustler);
    RegisterCreatureAI(npc_westfall_westfall_stew);
    RegisterCreatureAI(npc_westfall_homless_stormwind_citizen);
    RegisterCreatureAI(npc_westfall_west_plains_drifter);
    RegisterSpellScript(spell_westfall_sniper_fire_proc);
    RegisterSpellScript(spell_westfall_sniper_fire);
    new world_map_azimov_moonbrook_gathering();
    new player_azimov_vision_of_the_past();
}
