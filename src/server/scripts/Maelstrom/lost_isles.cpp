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

/*
 * Azimov : Îles Perdues, partie 1 (début de la zone), porté depuis AshamaneCore (Maelstrom/LostIsles/lost_isles.cpp, lignes 1-924)
 *  - TaskScheduler local à chaque IA (pas de Unit::GetScheduler dans le fork)
 *  - Player::GetSummonedCreatureByEntry -> recherche des invocations du lanceur (GetSummonerGUID)
 *  - Les escortes (35813, 36578, 36143, 36178, 36505) ont besoin de script_waypoint : liées seulement si le chemin existe
 *  - Les vieux scripts « gob » sans numéro de PNJ (fin du fichier Ashamane) ne sont pas portés ici
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TaskScheduler.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace LostIsles
{

// Créature invoquée par 'summoner' (remplace GetSummonedCreatureByEntry d'Ashamane)
Creature* GetSummonOf(Unit* summoner, uint32 entry, float range = 50.0f)
{
    std::list<Creature*> list;
    summoner->GetCreatureListWithEntryInGrid(list, entry, range);
    for (Creature* creature : list)
        if (creature->IsSummon() && creature->ToTempSummon()->GetSummonerGUID() == summoner->GetGUID())
            return creature;
    return nullptr;
}

enum DontGoIntoTheLight
{
    NPC_GIZMO                           = 36600,
    QUEST_DONT_GO_INTO_THE_LIGHT        = 14239,

    SPELL_DGITL_SUMMON_DOC_ZAPNOZZLE    = 69018,
    SPELL_DGITL_JUMP_CABLES             = 69022,
};

// 69010
class spell_lost_isles_near_death : public SpellScript
{
    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_DGITL_SUMMON_DOC_ZAPNOZZLE, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_lost_isles_near_death::HandleHit, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
    }
};

// 36608 Doc Zapnozzle
struct npc_zapnozzle : public ScriptedAI
{
    npc_zapnozzle(Creature* creature) : ScriptedAI(creature) { }

    Position const beachPosition = { 617.317383f, 3142.259277f, -0.114589f, 2.176555f };

    void Reset() override
    {
        me->SetCanFly(true);
        me->SetSwim(true);
        me->GetMotionMaster()->MovePoint(1, 538.785034f, 3271.091797f, -1.082726f);
        me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    void IsSummonedBy(Unit* summoner) override
    {
        _playerGuid = summoner->GetGUID();
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 1:
                me->SetCanFly(false);
                me->SetSwim(false);
                me->GetMotionMaster()->MoveJump(537.296875f, 3272.306396f, 0.166942f, 3.011669f, 10.f, 10.f, 2);
                break;
            case 2:
                Talk(0, GetSummoner());
                InitFirstEventSchedulers();
                break;
            case 3:
                me->SetCanFly(true);
                me->SetSwim(true);
                Talk(6, GetSummoner());
                me->GetMotionMaster()->MovePoint(4, beachPosition);
                me->DespawnOrUnsummon(5000);
                break;
            default:
                break;
        }
    }

    void InitFirstEventSchedulers()
    {
        _scheduler.Schedule(5s, [this](TaskContext /*context*/)
        {
            Talk(1, GetSummoner());
        }).Schedule(9s, [this](TaskContext /*context*/)
        {
            if (Creature* gizmo = me->FindNearestCreature(NPC_GIZMO, 10.f))
                gizmo->AI()->Talk(0, GetSummoner());
        }).Schedule(15s, [this](TaskContext /*context*/)
        {
            Talk(2, GetSummoner());
        }).Schedule(21s, [this](TaskContext /*context*/)
        {
            if (Player* player = GetSummoner())
                me->CastSpell(player, SPELL_DGITL_JUMP_CABLES, false);
        }).Schedule(27s, [this](TaskContext /*context*/)
        {
            Talk(3, GetSummoner());
        }).Schedule(31s, [this](TaskContext /*context*/)
        {
            if (Player* player = GetSummoner())
                me->CastSpell(player, SPELL_DGITL_JUMP_CABLES, false);
        }).Schedule(37s, [this](TaskContext /*context*/)
        {
            Talk(4, GetSummoner());
            me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        });
    }

    void QuestReward(Player* /*player*/, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_DONT_GO_INTO_THE_LIGHT)
            return;

        _scheduler.Schedule(1s, [this](TaskContext /*context*/)
        {
            Talk(5, GetSummoner());
        }).Schedule(4s, [this](TaskContext /*context*/)
        {
            me->GetMotionMaster()->MoveJump(539.637451f, 3266.728516f, -1.139869f, 5.053704f, 10.f, 10.f, 3);
        });
    }

private:
    ObjectGuid _playerGuid;
    TaskScheduler _scheduler;

    Player* GetSummoner() { return ObjectAccessor::GetPlayer(*me, _playerGuid); }
};

enum GoblinEscapePod
{
    SPELL_SUMMON_LIVE_GOBLIN_SURVIVOR   = 66137,
    SPELL_TRADE_PRINCE_CONTROLLER_AURA  = 67433,
    SPELL_SUMMON_TRADE_PRINCE           = 67845,

    NPC_GOBLIN_SURVIVOR                 = 34748,
    NPC_TRADE_PRINCE_GALLYWIX           = 35649,
};

// 195188 Capsule de survie gobeline
struct go_goblin_escape_pod : public GameObjectAI
{
    go_goblin_escape_pod(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* player) override
    {
        if (!player->HasAura(SPELL_TRADE_PRINCE_CONTROLLER_AURA))
        {
            player->CastSpell(player, SPELL_SUMMON_TRADE_PRINCE, true);
            player->CastSpell(player, SPELL_TRADE_PRINCE_CONTROLLER_AURA, true);
        }
        else
            player->CastSpell(player, SPELL_SUMMON_LIVE_GOBLIN_SURVIVOR, true);

        player->KilledMonsterCredit(NPC_GOBLIN_SURVIVOR);
        me->DestroyForPlayer(player);
        return true;
    }
};

// 34748, 35649 : survivants sortis des capsules
struct npc_goblin_escape_pod : public ScriptedAI
{
    npc_goblin_escape_pod(Creature* creature) : ScriptedAI(creature) { }

    Position const beachPosition = { 617.317383f, 3142.259277f, -0.114589f, 2.176555f };

    void Reset() override
    {
        me->SetCanFly(true);
        me->SetSwim(true);

        _scheduler.Schedule(1s, [this](TaskContext /*context*/)
        {
            Player* nearestPlayer = me->SelectNearestPlayer(20.f);
            if (me->GetEntry() == NPC_TRADE_PRINCE_GALLYWIX)
                Talk(0, nearestPlayer);
            else
                Talk(urand(0, 3), nearestPlayer);
        }).Schedule(2s, [this](TaskContext /*context*/)
        {
            me->GetMotionMaster()->MovePoint(1, beachPosition);
            me->DespawnOrUnsummon(3000);
        });
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;
};

enum BombMonkey
{
    SPELL_MONKEY_COSMETIC_THROW_BOMB    = 66142,
    SPELL_NITRO_POTASSIUM_BANANAS       = 67917,
    SPELL_EXPLOSIVE_BANANA              = 67919,

    NPC_MONKEY_KILL_CREDIT              = 35760
};

// 34699 Singe à bombes
struct npc_bomb_monkey : public ScriptedAI
{
    npc_bomb_monkey(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _scheduler.CancelAll();
        _scheduler.Schedule(5s, 30s, [this](TaskContext context)
        {
            me->CastSpell(me, SPELL_MONKEY_COSMETIC_THROW_BOMB, false);
            context.Repeat(5s, 30s);
        });
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        Player* player = caster->ToPlayer();
        if (!player || spell->Id != SPELL_NITRO_POTASSIUM_BANANAS)
            return;

        if (me->IsNonMeleeSpellCast(true))
            me->InterruptNonMeleeSpells(true);

        _scheduler.CancelAll();
        player->KilledMonsterCredit(NPC_MONKEY_KILL_CREDIT);
        me->CastSpell(me, SPELL_EXPLOSIVE_BANANA, true);
        me->DespawnOrUnsummon(3000);
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    TaskScheduler _scheduler;
};

enum MinerTroublesQuest
{
    QUEST_MINER_TROUBLES                = 14021,

    SPELL_MINER_TROUBLESQUEST_ACCEPT    = 68061,
    SPELL_SUMMON_ORE_CART               = 68064,
    SPELL_MINER_TROUBLES_CHAINS         = 68122,

    NPC_FRIGHTENED_MINER                = 35813,
    NPC_ORE_CART                        = 35814,
    NPC_MINER_KILL_CREDIT               = 35816,
};

// 35769 Contremaître Dampwick
struct npc_foreman_dampwick : public ScriptedAI
{
    npc_foreman_dampwick(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_MINER_TROUBLES)
            return;

        Talk(0);
        me->CastSpell(player, SPELL_MINER_TROUBLESQUEST_ACCEPT, true);

        if (Creature* miner = GetSummonOf(player, NPC_FRIGHTENED_MINER))
        {
            miner->CastSpell(miner, SPELL_SUMMON_ORE_CART, true);
            if (Creature* oreCart = GetSummonOf(miner, NPC_ORE_CART))
            {
                oreCart->CastSpell(miner, SPELL_MINER_TROUBLES_CHAINS, true);
                oreCart->GetMotionMaster()->MoveFollow(miner, 1.f, float(M_PI));
            }

            if (EscortAI* escort = dynamic_cast<EscortAI*>(miner->AI()))
                escort->Start(false, false, player->GetGUID(), quest);
        }
    }
};

// 35813 Mineur effrayé (escorte : script_waypoint 35813)
struct npc_frightened_miner_escort : public EscortAI
{
    npc_frightened_miner_escort(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void JustDied(Unit* /*killer*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void TalkToEscortPlayer(uint8 id)
    {
        Talk(id, GetPlayerForEscort());
    }

    void WaypointStarted(uint32 pointId, uint32 /*pathId*/) override
    {
        me->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
        switch (pointId)
        {
            case 0:  TalkToEscortPlayer(0); break;
            case 4:  TalkToEscortPlayer(2); break;
            case 8:  TalkToEscortPlayer(3); break;
            case 10: TalkToEscortPlayer(4); break;
            default: break;
        }
    }

    void WaypointReached(uint32 pointId, uint32 /*pathId*/) override
    {
        switch (pointId)
        {
            case 3:
                TalkToEscortPlayer(1);
                break;
            case 4:
            case 7:
            case 10:
            case 12:
                me->HandleEmoteCommand(EMOTE_ONESHOT_WORK_MINING);   // 467 chez Ashamane (pioche)
                break;
            case 13:
                SetRun(true);
                TalkToEscortPlayer(5);
                if (Player* player = GetPlayerForEscort())
                    player->KilledMonsterCredit(NPC_MINER_KILL_CREDIT);
                if (Creature* oreCart = GetSummonOf(me, NPC_ORE_CART))
                    oreCart->DespawnOrUnsummon();
                break;
            default:
                break;
        }
    }
};

enum CapturingTheUnknown
{
    SPELL_CTU_FLASH                     = 68281,
    SPELL_CTU_BIND_SIGHT                = 70641,
    SPELL_CTU_SCREEN_EFFECT             = 70649,

    SPELL_CTU_SEE_INVIS_1               = 70661,
    SPELL_CTU_SEE_INVIS_2               = 70678,
    SPELL_CTU_SEE_INVIS_3               = 70680,
    SPELL_CTU_SEE_INVIS_4               = 70681,

    NPC_CTU_BUNNY_1                     = 37872,
    NPC_CTU_BUNNY_2                     = 37895,
    NPC_CTU_BUNNY_3                     = 37896,
    NPC_CTU_BUNNY_4                     = 37897,
};

std::map<uint32, uint32> const ctuSpellByBunny
{
    { NPC_CTU_BUNNY_1,  SPELL_CTU_SEE_INVIS_1 },
    { NPC_CTU_BUNNY_2,  SPELL_CTU_SEE_INVIS_2 },
    { NPC_CTU_BUNNY_3,  SPELL_CTU_SEE_INVIS_3 },
    { NPC_CTU_BUNNY_4,  SPELL_CTU_SEE_INVIS_4 },
};

// 68280 Instantané (Capturer l'inconnu)
class spell_ctu_snapflash : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* unit = GetHitUnit();
        Player* player = GetCaster()->ToPlayer();
        if (!unit || !player)
            return;

        auto itr = ctuSpellByBunny.find(unit->GetEntry());
        if (itr == ctuSpellByBunny.end())
            return;

        player->RemoveAurasDueToSpell(itr->second);
        player->CastSpell(unit, SPELL_CTU_BIND_SIGHT, true);
        player->CastSpell(player, SPELL_CTU_SCREEN_EFFECT, true);

        uint32 entry = unit->GetEntry();
        ObjectGuid guid = unit->GetGUID();
        player->m_Events.AddEventAtOffset([player, entry, guid]()
        {
            player->RemoveAurasDueToSpell(SPELL_CTU_SCREEN_EFFECT);
            player->KilledMonsterCredit(entry, guid);
        }, 3s);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_ctu_snapflash::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 70641
class spell_ctu_bind_sight : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* unit = GetHitUnit())
            unit->CastSpell(unit, SPELL_CTU_FLASH, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_ctu_bind_sight::HandleDummy, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
    }
};

enum LostIslesWeed
{
    QUEST_WEED_WHACKER          = 14236,
    SPELL_WEED_WHACKER_AURA     = 68212,
    SPELL_WEED_WHACKER_HIT      = 68213,
    NPC_WEED_KILL_CREDIT        = 35897,
};

// 35896 Mauvaise herbe (entrée à confirmer au diagnostic)
struct npc_lost_isles_weed : public ScriptedAI
{
    npc_lost_isles_weed(Creature* creature) : ScriptedAI(creature) { }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        if (!me->IsAlive() || spell->Id != SPELL_WEED_WHACKER_HIT)
            return;

        if (Player* player = caster->ToPlayer())
        {
            player->KilledMonsterCredit(NPC_WEED_KILL_CREDIT);
            Unit::Kill(player, me);
        }
    }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void UpdateAI(uint32 /*diff*/) override { }
};

// Débroussailleuse (objet de quête 14236) : active / coupe l'aura de fauchage
class spell_weed_whacker : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        if (player->HasAura(SPELL_WEED_WHACKER_AURA))
            player->RemoveAurasDueToSpell(SPELL_WEED_WHACKER_AURA);
        else if (player->GetQuestStatus(QUEST_WEED_WHACKER) == QUEST_STATUS_INCOMPLETE)
            player->CastSpell(player, SPELL_WEED_WHACKER_AURA, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_weed_whacker::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum Infrared
{
    QUEST_INFRARED_INFRADEAD        = 14238,
    QUEST_TO_THE_CLIFFS             = 14240,

    NPC_ORC_SCOUT                   = 36100,
    NPC_CLIFF_BASTIA                = 36585,

    SPELL_INFRARED_QUEST_ACCEPT     = 68344,
    SPELL_INFRARED_ORC_SCOUT_AURA   = 68338,
    SPELL_CLIFF_SUMMON_BASTIA       = 68973,
};

// 35917 Kilag Sangrecroc
struct npc_kilag_gorefang : public ScriptedAI
{
    npc_kilag_gorefang(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_INFRARED_INFRADEAD)
        {
            Talk(0, player);
            me->CastSpell(player, SPELL_INFRARED_QUEST_ACCEPT, true);
        }
        else if (quest->GetQuestId() == QUEST_TO_THE_CLIFFS)
        {
            player->CastSpell(player, SPELL_CLIFF_SUMMON_BASTIA, true);
            if (Creature* bastia = GetSummonOf(player, NPC_CLIFF_BASTIA))
            {
                Talk(1, player);
                if (EscortAI* escort = dynamic_cast<EscortAI*>(bastia->AI()))
                    escort->Start(false, true, player->GetGUID(), quest);
            }
        }
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() == QUEST_INFRARED_INFRADEAD)
            player->RemoveAurasDueToSpell(SPELL_INFRARED_ORC_SCOUT_AURA);
    }
};

// 68338 : fin de l'aura -> l'éclaireur orc invoqué disparaît
class spell_infrared_orc_scout : public AuraScript
{
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Creature* orcScout = GetSummonOf(GetTarget(), NPC_ORC_SCOUT))
            orcScout->DespawnOrUnsummon();
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_infrared_orc_scout::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 36585 Bastia (escorte : script_waypoint 36585)
struct npc_cliff_bastia : public EscortAI
{
    npc_cliff_bastia(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void Reset() override
    {
        me->SetSpeedRate(MOVE_RUN, 2.0f);
    }

    void WaypointReached(uint32 pointId, uint32 /*pathId*/) override
    {
        if (pointId == 20)
            me->DespawnOrUnsummon();
    }
};

enum PreciousCargo
{
    QUEST_PRECIOUS_CARGO                = 14242,

    SPELL_PRECIOUS_CARGO_QUEST_ACCEPT   = 68386,
    SPELL_PRECIOUS_CARGO_KILL_CREDIT    = 69081,

    NPC_GYROCOPTER                      = 36143,
};

// 36127 (donneur de « Précieuse cargaison »)
struct npc_gyrocopter_quest_giver : public ScriptedAI
{
    npc_gyrocopter_quest_giver(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_PRECIOUS_CARGO)
            return;

        player->CastSpell(player, SPELL_PRECIOUS_CARGO_QUEST_ACCEPT, true);
        if (Creature* gyrocopter = GetSummonOf(player, NPC_GYROCOPTER))
            if (EscortAI* escort = dynamic_cast<EscortAI*>(gyrocopter->AI()))
                escort->Start(false, true, player->GetGUID(), quest);
    }
};

// 36143 Gyrocoptère (escorte : script_waypoint 36143)
struct npc_precious_cargo_gyrocopter : public EscortAI
{
    npc_precious_cargo_gyrocopter(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void Reset() override
    {
        me->SetCanFly(true);
        me->SetSpeedRate(MOVE_FLIGHT, 3.0f);
    }

    void WaypointReached(uint32 pointId, uint32 /*pathId*/) override
    {
        if (pointId != 2)
            return;

        if (Player* player = GetPlayerForEscort())
            me->CastSpell(player, SPELL_PRECIOUS_CARGO_KILL_CREDIT, true);
        me->DespawnOrUnsummon();
    }
};

// 36145 Thrall prisonnier
struct npc_lost_isles_thrall_prisoner : public ScriptedAI
{
    npc_lost_isles_thrall_prisoner(Creature* creature) : ScriptedAI(creature) { }

    void MoveInLineOfSight(Unit* who) override
    {
        if (Player* player = who->ToPlayer())
            if (player->GetQuestStatus(QUEST_PRECIOUS_CARGO) == QUEST_STATUS_INCOMPLETE && player->GetDistance(me) < 5.f)
                player->KilledMonsterCredit(me->GetEntry());
    }

    void QuestAccept(Player* player, Quest const* /*quest*/) override
    {
        Talk(0, player);
    }
};

enum WarchiefsRevenge
{
    QUEST_WARCHIEF_REVENGE  = 14243,
    SPELL_WR_QUEST_ACCEPT   = 68408,
    NPC_WR_CYCLONE          = 36178,
};

// Thrall en haut du bateau (« La vengeance du chef de guerre ») : entrée à confirmer (Ashamane réutilise 36127 par erreur)
struct npc_lost_isles_thrall_top_boat : public ScriptedAI
{
    npc_lost_isles_thrall_top_boat(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() != QUEST_WARCHIEF_REVENGE)
            return;

        player->CastSpell(player, SPELL_WR_QUEST_ACCEPT, true);
        if (Creature* cyclone = GetSummonOf(player, NPC_WR_CYCLONE))
            cyclone->AI()->DoAction(0);
    }
};

// 36178 Cyclone (escorte : script_waypoint 36178)
struct npc_warchief_revenge_cyclone : public EscortAI
{
    npc_warchief_revenge_cyclone(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void Reset() override
    {
        me->SetCanFly(true);
        me->SetSpeedRate(MOVE_FLIGHT, 1.0f);
    }

    void DoAction(int32 /*action*/) override
    {
        me->GetMotionMaster()->MovePoint(1001, 1001.373047f, 3832.485840f, 14.062465f);

        _scheduler.Schedule(1s, [this](TaskContext context)
        {
            Player* player = nullptr;
            if (Vehicle* vehicle = me->GetVehicleKit())
                if (Unit* passenger = vehicle->GetPassenger(0))
                    player = passenger->ToPlayer();
            if (!player)
                return;

            if (player->GetQuestStatus(QUEST_WARCHIEF_REVENGE) == QUEST_STATUS_COMPLETE)
            {
                SetEscortPaused(true);
                me->SetSpeedRate(MOVE_FLIGHT, 5.0f);
                me->GetMotionMaster()->MovePoint(1002, 885.869568f, 3104.882324f, 175.560944f, false);
            }
            else
                context.Repeat();
        });
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
        EscortAI::UpdateAI(diff);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        EscortAI::MovementInform(type, id);
        if (type != POINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 1001:
                Start(false, true, ObjectGuid::Empty, nullptr, false, true);
                break;
            case 1002:
                me->GetMotionMaster()->MovePoint(1003, 863.672180f, 2778.023193f, 126.243454f, false);
                break;
            case 1003:
                // le PNJ lâche le joueur en l'air avant le point : on le pose
                if (Vehicle* vehicle = me->GetVehicleKit())
                    if (Unit* passenger = vehicle->GetPassenger(0))
                        if (Player* player = passenger->ToPlayer())
                            player->NearTeleportTo(863.672180f, 2778.023193f, 126.243454f, 0.0f);
                if (me->IsAlive())
                    me->KillSelf();
                break;
            default:
                break;
        }
    }

private:
    TaskScheduler _scheduler;
};

enum UpUpAndAway
{
    QUEST_UP_UP_AND_AWAY            = 14244,

    SPELL_ROCKET_BLAST              = 66110,
    SPELL_SUMMON_CHARACTER_ROCKET   = 68806,
    SPELL_UUAA_KILL_CREDIT          = 68813,
    SPELL_SUMMON_GALLYWIX           = 68815,
    SPELL_SUMMON_GALLYWIX_ROCKET    = 68819,

    NPC_GALLYWIX_ROCKET             = 36514
};

// 36425 Sassy Hartzang (fronde à fusées)
struct npc_sassy_rocket_sling : public ScriptedAI
{
    npc_sassy_rocket_sling(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_UP_UP_AND_AWAY)
            player->CastSpell(player, SPELL_SUMMON_GALLYWIX, true);
    }
};

// 36513 Gallywix (s'envole en fusée)
struct npc_gallywix_sling_rocket : public ScriptedAI
{
    npc_gallywix_sling_rocket(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        Talk(0);

        _scheduler.Schedule(5s, [this](TaskContext /*context*/)
        {
            Talk(1);
        }).Schedule(10s, [this](TaskContext /*context*/)
        {
            if (TempSummon* summon = me->ToTempSummon())
                Talk(2, summon->GetSummoner());
        });

        me->CastSpell(me, SPELL_SUMMON_GALLYWIX_ROCKET, true);
        if (Creature* rocket = GetSummonOf(me, NPC_GALLYWIX_ROCKET))
        {
            rocket->SetCanFly(true);
            rocket->SetSpeedRate(MOVE_FLIGHT, 3.0f);
        }

        me->GetMotionMaster()->MovePoint(1, 869.895996f, 2744.302246f, 122.355782f);
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != 1)
            return;

        if (Creature* rocket = GetSummonOf(me, NPC_GALLYWIX_ROCKET))
        {
            me->EnterVehicle(rocket);
            ObjectGuid rocketGuid = rocket->GetGUID();
            _scheduler.Schedule(2s, [this, rocketGuid](TaskContext /*context*/)
            {
                if (Creature* r = ObjectAccessor::GetCreature(*me, rocketGuid))
                {
                    r->CastSpell(r, SPELL_ROCKET_BLAST, true);
                    r->GetMotionMaster()->MovePoint(1, 907.387085f, 2662.028320f, 193.833740f);
                    r->DespawnOrUnsummon(5000);
                }
            });
        }
    }

private:
    TaskScheduler _scheduler;
};

// 196439 Fronde à fusées
struct go_rocket_sling : public GameObjectAI
{
    go_rocket_sling(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* player) override
    {
        // objet de type 2 (donneur de quêtes) : sans la quête, on laisse le dialogue normal
        if (player->GetQuestStatus(QUEST_UP_UP_AND_AWAY) == QUEST_STATUS_NONE || player->GetVehicle())
            return false;
        player->CastSpell(player, SPELL_SUMMON_CHARACTER_ROCKET, true);
        return true;
    }
};

// 36505 Fusée de la fronde (escorte : script_waypoint 36505)
struct npc_sling_rocket : public EscortAI
{
    npc_sling_rocket(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
    {
        if (!apply)
            return;

        me->SetCanFly(true);
        me->SetSpeedRate(MOVE_FLIGHT, 3.0f);
        me->CastSpell(me, SPELL_ROCKET_BLAST, true);
        Start(false, true, who->GetGUID());
    }

    void WaypointReached(uint32 pointId, uint32 /*pathId*/) override
    {
        if (pointId != 3)
            return;

        if (Vehicle* vehicle = me->GetVehicleKit())
            if (Unit* passenger = vehicle->GetPassenger(0))
                me->CastSpell(passenger, SPELL_UUAA_KILL_CREDIT, true);
        me->DespawnOrUnsummon();
    }
};

// 71091 (Il y a un bateau ici)
class spell_tiab_effect2 : public SpellScript
{
    void HandleDummy(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (Player* player = GetCaster()->ToPlayer())
            player->KilledMonsterCredit(38024);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_tiab_effect2::HandleDummy, EFFECT_0, SPELL_EFFECT_FORCE_CAST);
    }
};

}

void AddSC_lost_isles()
{
    using namespace LostIsles;

    RegisterSpellScript(spell_lost_isles_near_death);
    RegisterCreatureAI(npc_zapnozzle);
    RegisterGameObjectAI(go_goblin_escape_pod);
    RegisterCreatureAI(npc_goblin_escape_pod);
    RegisterCreatureAI(npc_bomb_monkey);
    RegisterCreatureAI(npc_foreman_dampwick);
    RegisterCreatureAI(npc_frightened_miner_escort);
    RegisterSpellScript(spell_ctu_snapflash);
    RegisterSpellScript(spell_ctu_bind_sight);
    RegisterCreatureAI(npc_lost_isles_weed);
    RegisterSpellScript(spell_weed_whacker);
    RegisterCreatureAI(npc_kilag_gorefang);
    RegisterSpellScript(spell_infrared_orc_scout);
    RegisterCreatureAI(npc_cliff_bastia);
    RegisterCreatureAI(npc_gyrocopter_quest_giver);
    RegisterCreatureAI(npc_precious_cargo_gyrocopter);
    RegisterCreatureAI(npc_lost_isles_thrall_prisoner);
    RegisterCreatureAI(npc_lost_isles_thrall_top_boat);
    RegisterCreatureAI(npc_warchief_revenge_cyclone);
    RegisterCreatureAI(npc_sassy_rocket_sling);
    RegisterCreatureAI(npc_gallywix_sling_rocket);
    RegisterGameObjectAI(go_rocket_sling);
    RegisterCreatureAI(npc_sling_rocket);
    RegisterSpellScript(spell_tiab_effect2);
}
