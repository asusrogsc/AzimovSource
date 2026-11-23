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
 * Azimov : Kezan, porte depuis AshamaneCore (Maelstrom/LostIsles/kezan.cpp)
 *  - TaskScheduler local a chaque IA (pas de Unit::GetScheduler dans le fork)
 *  - QuestScript -> PlayerScript::OnQuestStatusChange
 *  - compteurs d'objectifs Legion -> GetReqKillOrCastCurrentCount
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureTextMgr.h"
#include "GameObject.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TaskScheduler.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace Kezan
{

// Creature invoquee par ce joueur (remplace Player::GetSummonedCreatureByEntry d'Ashamane)
Creature* GetPlayerSummon(Player* player, uint32 entry, float range = 100.0f)
{
    std::list<Creature*> list;
    player->GetCreatureListWithEntryInGrid(list, entry, range);
    for (Creature* creature : list)
        if (creature->IsSummon() && creature->ToTempSummon()->GetSummonerGUID() == player->GetGUID())
            return creature;
    return nullptr;
}

// IA de base avec un ordonnanceur
struct KezanScheduledAI : public ScriptedAI
{
    KezanScheduledAI(Creature* creature) : ScriptedAI(creature) { }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

protected:
    TaskScheduler _scheduler;
};

enum NPC_DeffiantTroll
{
    SPELL_RAGE                      = 45111,
    SPELL_SLEEP                     = 62248,
    DEFFIANT_KILL_CREDIT            = 34830,
    SPELL_GOBLIN_BELT_SHOCKER       = 66306,
    QUEST_GOOD_HELP_IS_HARD_TO_FIND = 14069,
    GO_DEPOSIT                      = 195489,
    GO_DEPOSIT_2                    = 195488,
    GO_DEPOSIT_CHUNK                = 195492,
};

// 34830
struct npc_defiant_troll : public ScriptedAI
{
    npc_defiant_troll(Creature* creature) : ScriptedAI(creature), rebuffTimer(5000) { }

    void Reset() override
    {
        rebuffTimer = 5000;
    }

    void MovementInform(uint32 /*type*/, uint32 /*id*/) override
    {
        if (GameObject* deposit = me->FindNearestGameObject(GO_DEPOSIT, 20.f))
            me->SetFacingToObject(deposit);
        me->HandleEmoteCommand(EMOTE_STATE_WORK_MINING);
        me->LoadEquipment(3, true);
    }

    void JustReachedHome() override { }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        if (!me->HasAura(SPELL_RAGE))
            return;

        Player* player = caster->ToPlayer();
        if (spell->Id == SPELL_GOBLIN_BELT_SHOCKER && player && player->GetQuestStatus(QUEST_GOOD_HELP_IS_HARD_TO_FIND) == QUEST_STATUS_INCOMPLETE)
        {
            player->KilledMonsterCredit(DEFFIANT_KILL_CREDIT, me->GetGUID());
            me->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
            me->RemoveAllAuras();

            Talk(urand(0, 6), player);

            if (GameObject* deposit = me->FindNearestGameObject(GO_DEPOSIT, 20.f))
                me->GetMotionMaster()->MovePoint(1, deposit->GetPositionX() - 2.f, deposit->GetPositionY(), deposit->GetPositionZ());
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (me->HasAura(SPELL_RAGE))
            return;

        if (rebuffTimer <= diff)
        {
            switch (irand(0, 2))
            {
                case 0:
                    me->HandleEmoteCommand(EMOTE_STATE_EXCLAIM);
                    break;
                case 1:
                    me->HandleEmoteCommand(EMOTE_STATE_DANCE);
                    break;
                case 2:
                    me->AddAura(SPELL_SLEEP, me);
                    break;
            }

            me->LoadEquipment(urand(1, 2), true);
            rebuffTimer = 60000;
            me->NearTeleportTo(me->GetHomePosition());
            me->AddAura(SPELL_RAGE, me);
        }
        else
            rebuffTimer -= diff;
    }

private:
    uint32 rebuffTimer;
};

enum QuestRollingMyHomies
{
    QUEST_ROLLING_WITH_MY_HOMIES            = 14071,

    NPC_HOT_ROD                             = 34840,

    NPC_IZZY                                = 34890,
    NPC_ACE                                 = 34892,
    NPC_GOBBER                              = 34954,

    NPC_IZZY_SUMMON                         = 34959,
    NPC_ACE_SUMMON                          = 34957,
    NPC_GOBBER_SUMMON                       = 34958,

    SPELL_GENERIC_INVISIBILITY_DETECTION_1  = 49416,
    SPELL_GENERIC_INVISIBILITY_DETECTION_2  = 49417,
    SPELL_GENERIC_INVISIBILITY_DETECTION_3  = 60922,
    SPELL_GENERIC_INVISIBILITY_DETECTION_4  = 90161,

    SPELL_SUMMON_ACE                        = 66597,
    SPELL_SUMMON_GOBBER                     = 66599,
    SPELL_SUMMON_IZZY                       = 66600,

    SPELL_RESUMMON_ACE                      = 66644,
    SPELL_RESUMMON_GOBBER                   = 66645,
    SPELL_RESUMMON_IZZY                     = 66646,

    SPELL_HOT_ROD_KNOCKBACK                 = 66301,

    ACTION_HOMIES_ENTER_VEHICLE             = 1,
};

std::unordered_map<uint32, std::tuple<uint32, uint32>> const seatPerHomie =
{
    { NPC_ACE_SUMMON,       { 1, SPELL_RESUMMON_ACE }       },
    { NPC_GOBBER_SUMMON,    { 2, SPELL_RESUMMON_GOBBER }    },
    { NPC_IZZY_SUMMON,      { 3, SPELL_RESUMMON_IZZY }      },
};

// pnj -> (credit = pnj invoque, sort d'invocation, detection d'invisibilite)
std::unordered_map<uint32, std::tuple<uint32, uint32, uint32>> const creditAndSpellsPerHomie =
{
    { NPC_ACE,      { NPC_ACE_SUMMON,       SPELL_SUMMON_ACE,       SPELL_GENERIC_INVISIBILITY_DETECTION_1 } },
    { NPC_GOBBER,   { NPC_GOBBER_SUMMON,    SPELL_SUMMON_GOBBER,    SPELL_GENERIC_INVISIBILITY_DETECTION_2 } },
    { NPC_IZZY,     { NPC_IZZY_SUMMON,      SPELL_SUMMON_IZZY,      SPELL_GENERIC_INVISIBILITY_DETECTION_3 } },
};

// 34840
struct npc_hot_rod_rolling_my_homies : public KezanScheduledAI
{
    npc_hot_rod_rolling_my_homies(Creature* creature) : KezanScheduledAI(creature) { }

    void OnCharmed(bool /*isNew*/) override { }

    void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
    {
        if (!apply)
        {
            if (who->IsPlayer())
                _scheduler.CancelAll();
            return;
        }

        if (!me->IsSummon())
            return;

        Player* player = who->ToPlayer();
        if (!player)
            return;

        ObjectGuid playerGuid = player->GetGUID();
        _scheduler.CancelAll();
        _scheduler.Schedule(250ms, [this, playerGuid](TaskContext context)
        {
            Player* driver = ObjectAccessor::GetPlayer(*me, playerGuid);
            if (!driver || driver->GetVehicleBase() != me)
                return;

            if (me->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FORWARD))
                driver->CastSpell(driver, SPELL_HOT_ROD_KNOCKBACK, true);

            context.Repeat();
        });

        for (auto const& itr : seatPerHomie)
        {
            if (Creature* homie = GetPlayerSummon(player, itr.first))
                homie->AI()->DoAction(ACTION_HOMIES_ENTER_VEHICLE);
            else if (player->GetQuestStatus(QUEST_ROLLING_WITH_MY_HOMIES) == QUEST_STATUS_REWARDED)
                player->CastSpell(player, std::get<1>(itr.second), true);
        }
    }
};

// 34892 Ace, 34954 Gobber, 34890 Izzy
struct npc_homies : public ScriptedAI
{
    npc_homies(Creature* creature) : ScriptedAI(creature) { }

    void MoveInLineOfSight(Unit* who) override
    {
        if (who->GetEntry() != NPC_HOT_ROD || !who->IsVehicle())
            return;

        Unit* driver = who->GetVehicleKit()->GetPassenger(0);
        if (!driver)
            return;

        Player* player = driver->ToPlayer();
        if (!player)
            return;

        if (player->GetQuestStatus(QUEST_ROLLING_WITH_MY_HOMIES) != QUEST_STATUS_INCOMPLETE)
            return;

        if (me->GetDistance(player) > 10.f)
            return;

        auto itr = creditAndSpellsPerHomie.find(me->GetEntry());
        if (itr == creditAndSpellsPerHomie.end())
            return;

        uint32 creditEntry, summonSpell, invisibilityDetectionSpell;
        std::tie(creditEntry, summonSpell, invisibilityDetectionSpell) = itr->second;

        if (player->GetReqKillOrCastCurrentCount(QUEST_ROLLING_WITH_MY_HOMIES, creditEntry))
            return;

        player->RemoveAurasDueToSpell(invisibilityDetectionSpell);
        player->CastSpell(player, summonSpell, true);
    }
};

// 34957 Ace, 34958 Gobber, 34959 Izzy (invoques)
struct npc_homies_summon : public ScriptedAI
{
    npc_homies_summon(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (Player* player = summoner->ToPlayer())
            if (player->GetQuestStatus(QUEST_ROLLING_WITH_MY_HOMIES) == QUEST_STATUS_INCOMPLETE)
                player->KilledMonsterCredit(me->GetEntry());

        DoAction(ACTION_HOMIES_ENTER_VEHICLE);
    }

    void DoAction(int32 /*action*/) override
    {
        if (!me->IsSummon())
            return;

        Unit* summoner = me->ToTempSummon()->GetSummoner();
        if (!summoner)
            return;

        if (Vehicle* vehicle = summoner->GetVehicle())
        {
            auto itr = seatPerHomie.find(me->GetEntry());
            if (itr != seatPerHomie.end())
                me->EnterVehicle(vehicle->GetBase(), int8(std::get<0>(itr->second)));
        }
    }
};

enum NPC_BlingBling
{
    SPELL_OUTFIT            = 66781,
    SPELL_COOL_SHADE        = 66782,
    SPELL_SHINY_BLING       = 66780,
    QUEST_BLINGBLING_MALE   = 14109,
    QUEST_BLINGBLING_FEMALE = 14110,
    NPC_SCABO               = 35128,
    NPC_MISSA               = 35130,
    NPC_DIASTEME            = 35126,
};

// 35126, 35128, 35130
struct npc_bling_bling : public ScriptedAI
{
    npc_bling_bling(Creature* creature) : ScriptedAI(creature) { }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        if (player->GetQuestStatus(QUEST_BLINGBLING_MALE) != QUEST_STATUS_INCOMPLETE &&
            player->GetQuestStatus(QUEST_BLINGBLING_FEMALE) != QUEST_STATUS_INCOMPLETE)
            return false;

        switch (me->GetEntry())
        {
            case NPC_SCABO:
                player->CastSpell(player, SPELL_OUTFIT, true);
                break;
            case NPC_MISSA:
                player->CastSpell(player, SPELL_COOL_SHADE, true);
                break;
            case NPC_DIASTEME:
                player->CastSpell(player, SPELL_SHINY_BLING, true);
                break;
            default:
                break;
        }

        CloseGossipMenuFor(player);
        return true;
    }
};

enum KezanPartygoer : uint32
{
    QUEST_LIFE_OF_THE_PARTY_MALE    = 14113,
    QUEST_LIFE_OF_THE_PARTY_FEMALE  = 14153,

    NPC_PARTYGOER_KILLCREDIT        = 35175,

    SPELL_HAPPY_PARTYGOER           = 66916,
    SPELL_COSMETIC_STUN             = 46957,
    SPELL_COSMETIC_DRUNKEN          = 55664,
    SPELL_FIREWORKS_BLUE            = 66917,
    SPELL_FIREWORKS_GREEN           = 66918,
    SPELL_FIREWORKS_RED             = 66919,
    SPELL_SUMMON_DISCO_BALL         = 66930,
    SPELL_SUMMON_BUCKET             = 66931,

    SPELL_DRINK                     = 66909,
    SPELL_BUCKET                    = 66910,
    SPELL_DANCE                     = 66911,
    SPELL_FIREWORKS                 = 66912,
    SPELL_FOOD                      = 66913,

    SPELL_NEED_DRINK                = 75042,
    SPELL_NEED_BUCKET               = 75044,
    SPELL_NEED_DANCE                = 75046,
    SPELL_NEED_FIREWORKS            = 75048,
    SPELL_NEED_FOOD                 = 75050,

    TALK_NEED_DANCE                 = 0,
    TALK_NEED_BUCKET                = 1,
    TALK_NEED_DRINK                 = 2,
    TALK_NEED_FIREWORKS             = 3,
    TALK_NEED_FOOD                  = 4,

    TALK_DRINK                      = 5,
    TALK_BUCKET                     = 6,
    TALK_DANCE                      = 7,
    TALK_FIREWORKS                  = 8,
    TALK_FOOD                       = 9,
};

// 35175, 35186
struct npc_kezan_partygoer : public KezanScheduledAI
{
    npc_kezan_partygoer(Creature* creature) : KezanScheduledAI(creature) { }

    std::vector<std::tuple<uint32, uint32, uint32, uint32>> const talkAndSpells =
    {
        { SPELL_DRINK,      SPELL_NEED_DRINK,       TALK_NEED_DRINK,        TALK_DRINK      },
        { SPELL_BUCKET,     SPELL_NEED_BUCKET,      TALK_NEED_BUCKET,       TALK_BUCKET     },
        { SPELL_DANCE,      SPELL_NEED_DANCE,       TALK_NEED_DANCE,        TALK_DANCE      },
        { SPELL_FIREWORKS,  SPELL_NEED_FIREWORKS,   TALK_NEED_FIREWORKS,    TALK_FIREWORKS  },
        { SPELL_FOOD,       SPELL_NEED_FOOD,        TALK_NEED_FOOD,         TALK_FOOD       },
    };

    void Reset() override
    {
        _scheduler.CancelAll();
        ApplyNeedAura();
    }

    void ApplyNeedAura()
    {
        _selectedNeed = urand(0, 4);
        uint32 needAura = std::get<1>(talkAndSpells[_selectedNeed]);
        me->AddAura(needAura, me);
        me->RemoveAurasDueToSpell(SPELL_HAPPY_PARTYGOER);

        if (needAura == SPELL_NEED_BUCKET)
        {
            me->AddAura(SPELL_COSMETIC_STUN, me);
            me->AddAura(SPELL_COSMETIC_DRUNKEN, me);
        }

        _scheduler.Schedule(10s, 120s, [this](TaskContext context)
        {
            Talk(std::get<2>(talkAndSpells[_selectedNeed]));
            context.Repeat(10s, 120s);
        });
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        if (me->HasAura(SPELL_HAPPY_PARTYGOER))
            return;

        if (spell->Id != std::get<0>(talkAndSpells[_selectedNeed]))
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        if (player->GetQuestStatus(QUEST_LIFE_OF_THE_PARTY_MALE) != QUEST_STATUS_INCOMPLETE &&
            player->GetQuestStatus(QUEST_LIFE_OF_THE_PARTY_FEMALE) != QUEST_STATUS_INCOMPLETE)
            return;

        me->RemoveAllAuras();
        Talk(std::get<3>(talkAndSpells[_selectedNeed]), player);
        me->AddAura(SPELL_HAPPY_PARTYGOER, me);
        player->KilledMonsterCredit(NPC_PARTYGOER_KILLCREDIT);

        switch (spell->Id)
        {
            case SPELL_BUCKET:
                me->CastSpell(me, SPELL_SUMMON_BUCKET, true);
                break;
            case SPELL_DANCE:
                me->CastSpell(me, SPELL_SUMMON_DISCO_BALL, true);
                break;
            default:
                break;
        }

        _scheduler.CancelAll();
        _scheduler.Schedule(30s, [this](TaskContext /*context*/)
        {
            ApplyNeedAura();
        });
    }

private:
    uint8 _selectedNeed = 0;
};

// 66912
class spell_kezan_fireworks : public SpellScript
{
    void HandleFireworks(SpellEffIndex /*effIndex*/)
    {
        static uint32 const fireworks[3] = { SPELL_FIREWORKS_BLUE, SPELL_FIREWORKS_GREEN, SPELL_FIREWORKS_RED };
        GetCaster()->CastSpell(GetCaster(), fireworks[urand(0, 2)], true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_kezan_fireworks::HandleFireworks, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum GreatBankHeist
{
    NPC_FBOK_VAULT              = 35486,

    SPELL_CONTROL_VEHICLE       = 67476,
    SPELL_BLASTCRACKERS         = 67508,
    SPELL_KAJAMITE_DRILL        = 67522,
    SPELL_EAR_O_SCOPE           = 67524,
    SPELL_INFINIFOLD_LOCKPICK   = 67525,
    SPELL_AMAZIN_G_RAY          = 67526,

    SPELL_POWER_CORRECT         = 67493,
    SPELL_POWER_INCORRECT       = 67494,

    SPELL_TIMER                 = 67502,

    SPELL_SUCCESS_CREATE_ITEM   = 67492,

    TALK_TRIPLE_DOT             = 0,
    TALK_INTRO_1                = 1,
    TALK_INTRO_2                = 2,
    TALK_INTRO_3                = 3,
    TALK_INTRO_4                = 4,
    TALK_CORRECT                = 5,
    TALK_TIMED_OUT              = 6,
    TALK_INCORRECT              = 7,
    TALK_USE_BLASTCRACKERS      = 8,
    TALK_USE_EAR_O_SCOPE        = 9,
    TALK_USE_INFINIFOLD_LOCKPICK= 10,
    TALK_USE_KAJAMITE_DRILL     = 11,
    TALK_USE_AMAZIN_G_RAY       = 12,
    TALK_SUCCESS                = 13,
};

// 67555
class spell_great_bank_heist_vault_interact : public SpellScript
{
    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster->IsPlayer() || caster->GetVehicle())
            return;

        if (Creature* vault = caster->SummonCreature(NPC_FBOK_VAULT, caster->GetPosition(), TEMPSUMMON_MANUAL_DESPAWN, 0, 0, caster->GetGUID()))
            caster->CastSpell(vault, SPELL_CONTROL_VEHICLE, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_great_bank_heist_vault_interact::HandleHit, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 35486
struct npc_first_bank_of_kezan_vault : public KezanScheduledAI
{
    npc_first_bank_of_kezan_vault(Creature* creature) : KezanScheduledAI(creature) { }

    std::vector<std::tuple<uint32, uint32>> const talkAndSpells =
    {
        { SPELL_BLASTCRACKERS,          TALK_USE_BLASTCRACKERS          },
        { SPELL_KAJAMITE_DRILL,         TALK_USE_KAJAMITE_DRILL         },
        { SPELL_EAR_O_SCOPE,            TALK_USE_EAR_O_SCOPE            },
        { SPELL_INFINIFOLD_LOCKPICK,    TALK_USE_INFINIFOLD_LOCKPICK    },
        { SPELL_AMAZIN_G_RAY,           TALK_USE_AMAZIN_G_RAY           },
    };

    void OnCharmed(bool /*isNew*/) override { }

    void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
    {
        if (!apply)
        {
            _scheduler.CancelAll();
            _playerGuid.Clear();
            me->DespawnOrUnsummon(2000);
            return;
        }

        if (who->IsPlayer())
        {
            _playerGuid = who->GetGUID();
            me->SetMaxPower(POWER_MANA, 100);
            me->SetPower(POWER_MANA, 0);
            me->SetControlled(true, UNIT_STATE_ROOT);
            InitializeEvent();
        }
    }

    Player* GetPlayer() const { return _playerGuid.IsEmpty() ? nullptr : ObjectAccessor::GetPlayer(*me, _playerGuid); }

    void InitializeEvent()
    {
        _scheduler.Schedule(1s, [this](TaskContext /*context*/)
        {
            VaultTalk(TALK_INTRO_1);
        }).Schedule(6s, [this](TaskContext /*context*/)
        {
            VaultTalk(TALK_INTRO_2);
        }).Schedule(13s, [this](TaskContext /*context*/)
        {
            VaultTalk(TALK_INTRO_3);
        }).Schedule(20s, [this](TaskContext /*context*/)
        {
            VaultTalk(TALK_INTRO_4);
        }).Schedule(23s, [this](TaskContext /*context*/)
        {
            SelectNextAction();
        });
    }

    void SelectNextAction()
    {
        _scheduler.Schedule(1s, [this](TaskContext /*context*/)
        {
            Player* player = GetPlayer();
            if (!player)
                return;

            _nextAction = urand(0, 4);
            VaultTalk(std::get<1>(talkAndSpells[_nextAction]));
            DoCast(player, SPELL_TIMER);

            _scheduler.Schedule(5s, [this](TaskContext /*context*/)
            {
                VaultTalk(TALK_TIMED_OUT);
                SelectNextAction();
            });
        });
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spell) override
    {
        if (spell->Id == SPELL_TIMER)
            return;

        Player* player = GetPlayer();
        if (!player || !player->HasAura(SPELL_TIMER))
            return;

        _scheduler.CancelAll();
        player->RemoveAurasDueToSpell(SPELL_TIMER);

        uint32 requiredSpell = std::get<0>(talkAndSpells[_nextAction]);
        if (requiredSpell != spell->Id)
        {
            VaultTalk(TALK_INCORRECT);
            DoCast(me, SPELL_POWER_INCORRECT);
            SelectNextAction();
            return;
        }

        VaultTalk(TALK_CORRECT);
        DoCast(me, SPELL_POWER_CORRECT);

        if (me->GetPower(POWER_MANA) >= 100)
        {
            VaultTalk(TALK_SUCCESS);
            player->KilledMonsterCredit(me->GetEntry());
            DoCast(player, SPELL_SUCCESS_CREATE_ITEM);
            me->KillSelf();
        }
        else
            SelectNextAction();
    }

    void VaultTalk(uint8 id)
    {
        Player* player = GetPlayer();
        Talk(id, player);
        Talk(TALK_TRIPLE_DOT, player);
    }

private:
    ObjectGuid _playerGuid;
    uint8 _nextAction = 0;
};

enum
{
    NPC_MOOK_DISGUISE   = 48925
};

// 67435
class spell_hobart_ingenious_cap_of_mook_foolery : public AuraScript
{
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Creature* mookDisguise = GetTarget()->SummonCreature(NPC_MOOK_DISGUISE, *GetTarget(), TEMPSUMMON_MANUAL_DESPAWN))
            mookDisguise->EnterVehicle(GetTarget());
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
            if (Creature* mookDisguise = GetPlayerSummon(player, NPC_MOOK_DISGUISE, 30.0f))
                mookDisguise->DespawnOrUnsummon();
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_hobart_ingenious_cap_of_mook_foolery::OnApply, EFFECT_1, SPELL_AURA_SET_VEHICLE_ID, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_hobart_ingenious_cap_of_mook_foolery::OnRemove, EFFECT_1, SPELL_AURA_SET_VEHICLE_ID, AURA_EFFECT_HANDLE_REAL);
    }
};

// 67682
class spell_kabummbomb : public SpellScript
{
    void HandleActivateGameobject(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player || player->GetQuestStatus(14124) != QUEST_STATUS_INCOMPLETE)
            return;

        if (GameObject* deposit = player->FindNearestGameObject(GO_DEPOSIT_2, 20.0f))
        {
            player->SummonGameObject(GO_DEPOSIT_CHUNK, deposit->GetPositionX() - 1, deposit->GetPositionY(), deposit->GetPositionZ() + 1, 0, QuaternionData(), 0);
            player->SummonGameObject(GO_DEPOSIT_CHUNK, deposit->GetPositionX() - 1, deposit->GetPositionY() - 1, deposit->GetPositionZ() + 1, 0, QuaternionData(), 0);
            player->SummonGameObject(GO_DEPOSIT_CHUNK, deposit->GetPositionX() - 1, deposit->GetPositionY() + 1, deposit->GetPositionZ() + 1, 0, QuaternionData(), 0);
            deposit->Delete();
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_kabummbomb::HandleActivateGameobject, EFFECT_0, SPELL_EFFECT_ACTIVATE_OBJECT);
    }
};

enum
{
    NPC_KEZAN_CITIZEN   = 35063,
    NPC_HIRED_LOOTER    = 35234,
};

// 35063, 35234
struct npc_citoyen_gob : public KezanScheduledAI
{
    npc_citoyen_gob(Creature* creature) : KezanScheduledAI(creature) { }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        if (spell->Id != SPELL_HOT_ROD_KNOCKBACK)
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        _scheduler.CancelAll();

        if (me->GetEntry() == NPC_HIRED_LOOTER)
        {
            me->KillSelf();
            if (player->GetQuestStatus(14121) == QUEST_STATUS_INCOMPLETE)
                player->AddItem(47530, 1);
            return;
        }

        if (me->GetEntry() == NPC_KEZAN_CITIZEN && roll_chance_i(50))
            Talk(urand(0, 7));

        _scheduler.Schedule(3s, [this](TaskContext /*context*/)
        {
            me->GetMotionMaster()->MoveTargetedHome();
        });
    }
};

enum
{
    SPELL_GASBOT_SUMMON         = 70252,
    SPELL_GASBOT_SUMMON_VISUAL  = 70254,
    SPELL_GASBOT_GAS_STREAM     = 70256,
    SPELL_GASBOT_BIG_FIRE       = 70259,
    SPELL_GASBOT_EXPLOSION      = 70262,
};

// 70253
class spell_summon_gasbot : public SpellScript
{
    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_GASBOT_SUMMON, true);
        GetCaster()->CastSpell(GetCaster(), SPELL_GASBOT_SUMMON_VISUAL, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_summon_gasbot::HandleHit, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 37598
struct npc_kezan_gasbot : public KezanScheduledAI
{
    npc_kezan_gasbot(Creature* creature) : KezanScheduledAI(creature) { }

    void Reset() override
    {
        DoCast(me, SPELL_GASBOT_GAS_STREAM);

        _scheduler.Schedule(500ms, [this](TaskContext /*context*/)
        {
            me->GetMotionMaster()->MovePoint(1, -8423.85f, 1365.88f, 104.7f);
        });
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != 1)
            return;

        DoCast(me, SPELL_GASBOT_BIG_FIRE);
        DoCast(me, SPELL_GASBOT_EXPLOSION);

        if (me->IsSummon())
            if (Unit* summoner = me->ToTempSummon()->GetSummoner())
                if (Player* player = summoner->ToPlayer())
                    player->KilledMonsterCredit(me->GetEntry());
    }
};

enum
{
    QUEST_NECESSARY_ROUGHNESS           = 24502,
    QUEST_FOURTH_AND_GOAL               = 28414,

    SPELL_DESPAWN_SHARKS                = 69987,
    SPELL_SUMMON_BILGWATER_BUCCANEER    = 70015,
    SPELL_SUMMON_BILGWATER_BUCCANEER_2  = 70075,
    SPELL_GROUND_RUMBLE_EARTHQUAKE      = 78607,

    SPELL_THROW_FOOTBOMB                = 69993,
    SPELL_KICK_FOOTBOMB                 = 70052,

    SPELL_SUMMON_DEATHWING              = 66322,

    NPC_SUMMON_BILGWATER_BUCCANEER      = 37179,
    NPC_SUMMON_BILGWATER_BUCCANEER_2    = 37213,
    NPC_FOURTH_AND_GOAL_TARGET          = 37203,
};

// Quetes 14071, 24502, 28414 (remplace les QuestScript d'Ashamane)
class player_kezan_quests : public PlayerScript
{
public:
    player_kezan_quests() : PlayerScript("player_kezan_quests") { }

    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        QuestStatus status = player->GetQuestStatus(questId);

        switch (questId)
        {
            case QUEST_ROLLING_WITH_MY_HOMIES:
                if (status == QUEST_STATUS_INCOMPLETE)
                {
                    player->CastSpell(player, SPELL_GENERIC_INVISIBILITY_DETECTION_1, true);
                    player->CastSpell(player, SPELL_GENERIC_INVISIBILITY_DETECTION_2, true);
                    player->CastSpell(player, SPELL_GENERIC_INVISIBILITY_DETECTION_3, true);
                }
                break;
            case QUEST_NECESSARY_ROUGHNESS:
                // npc_coach_crosscheck (OnQuestAccept) d'Ashamane : le PNJ 37106 garde sa SmartAI, on le fait ici
                if (status == QUEST_STATUS_INCOMPLETE && !player->GetVehicle()
                    && !player->GetReqKillOrCastCurrentCount(QUEST_NECESSARY_ROUGHNESS, 48271))
                {
                    player->CastSpell(player, SPELL_SUMMON_BILGWATER_BUCCANEER, true);
                    player->RemoveAurasDueToSpell(SPELL_GENERIC_INVISIBILITY_DETECTION_4);
                }
                break;
            case QUEST_FOURTH_AND_GOAL:
                if (status == QUEST_STATUS_INCOMPLETE)
                {
                    if (player->GetReqKillOrCastCurrentCount(QUEST_FOURTH_AND_GOAL, NPC_FOURTH_AND_GOAL_TARGET))
                        break;

                    player->ExitVehicle();
                    player->m_Events.AddEventAtOffset([player]()
                    {
                        player->RemoveAurasDueToSpell(SPELL_GENERIC_INVISIBILITY_DETECTION_4);
                        player->NearTeleportTo(-8250.910156f, 1484.290039f, 41.499901f, 3.124140f);
                        player->m_Events.AddEventAtOffset([player]()
                        {
                            player->CastSpell(player, SPELL_SUMMON_BILGWATER_BUCCANEER_2, true);
                        }, 1s);
                    }, 1s);
                }
                else if (status == QUEST_STATUS_COMPLETE)
                {
                    player->CastSpell(player, SPELL_GROUND_RUMBLE_EARTHQUAKE, true);
                    player->CastSpell(player, SPELL_SUMMON_DEATHWING, true);
                }
                else if (status == QUEST_STATUS_REWARDED)
                    player->CastSpell(player, SPELL_GROUND_RUMBLE_EARTHQUAKE, true);
                break;
            default:
                break;
        }
    }
};

Position const SharkPos[8] =
{
    { -8278.00f, 1485.00f, 43.14f, 6.28f },
    { -8285.00f, 1485.00f, 43.90f, 6.28f },
    { -8285.61f, 1489.94f, 43.90f, 6.28f },
    { -8285.76f, 1480.03f, 43.90f, 6.28f },
    { -8290.68f, 1475.38f, 43.95f, 6.28f },
    { -8291.06f, 1480.00f, 43.98f, 6.28f },
    { -8290.98f, 1487.85f, 43.99f, 6.28f },
    { -8290.90f, 1492.98f, 43.90f, 6.28f },
};

// 37179, 37213
struct npc_boucanier_gob : public KezanScheduledAI
{
    npc_boucanier_gob(Creature* creature) : KezanScheduledAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (me->GetEntry() == NPC_SUMMON_BILGWATER_BUCCANEER_2 && summoner->IsPlayer())
            summoner->EnterVehicle(me);
    }

    void OnCharmed(bool /*isNew*/) override { }

    void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
    {
        Player* player = who->ToPlayer();
        if (!player)
            return;

        if (apply)
        {
            me->SetSpeedRate(MOVE_RUN, 0.001f);
            player->RemoveAurasDueToSpell(SPELL_GENERIC_INVISIBILITY_DETECTION_4);

            if (player->GetQuestStatus(QUEST_NECESSARY_ROUGHNESS) == QUEST_STATUS_INCOMPLETE)
            {
                player->KilledMonsterCredit(48271, me->GetGUID());
                me->AddAura(70016, me);

                for (uint8 count = 0; count < 8; ++count)
                    me->SummonCreature(37114, SharkPos[count], TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120 * IN_MILLISECONDS);
            }
            else if (player->GetQuestStatus(QUEST_FOURTH_AND_GOAL) == QUEST_STATUS_INCOMPLETE)
                me->AddAura(70065, me);
        }
        else
        {
            player->CastSpell(player, SPELL_GENERIC_INVISIBILITY_DETECTION_4, true);
            me->CastSpell(me, SPELL_DESPAWN_SHARKS, true);
            _scheduler.Schedule(1s, [this](TaskContext /*context*/)
            {
                me->DespawnOrUnsummon();
            });
        }
    }
};

// 37114
struct npc_shark_gob : public KezanScheduledAI
{
    npc_shark_gob(Creature* creature) : KezanScheduledAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        me->SetWalk(true);

        _scheduler.Schedule(3s, [this](TaskContext /*context*/)
        {
            me->GetMotionMaster()->MovePoint(1, -8260.0f, me->GetPositionY(), me->GetPositionZ());
        });
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spell) override
    {
        if (spell->Id != SPELL_THROW_FOOTBOMB)
            return;

        Unit* unitCaster = caster->ToUnit();
        if (!unitCaster)
            return;

        if (Vehicle* kit = unitCaster->GetVehicleKit())
            if (Unit* passenger = kit->GetPassenger(0))
                if (Player* player = passenger->ToPlayer())
                    player->KilledMonsterCredit(me->GetEntry(), me->GetGUID());

        Unit::Kill(unitCaster, me);
    }

    void JustReachedHome() override { }
};

// 70052
class spell_fourth_and_goal_kick_footbomb : public SpellScript
{
    void After()
    {
        if (Vehicle* kit = GetCaster()->GetVehicleKit())
            if (Unit* passenger = kit->GetPassenger(0))
                if (Player* player = passenger->ToPlayer())
                    player->KilledMonsterCredit(NPC_FOURTH_AND_GOAL_TARGET);
    }

    void Register() override
    {
        AfterCast.Register(&spell_fourth_and_goal_kick_footbomb::After);
    }
};

// 37203
struct npc_fourth_and_goal_target : public ScriptedAI
{
    npc_fourth_and_goal_target(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->setActive(true);
    }

    void UpdateAI(uint32 /*diff*/) override { }

    void JustReachedHome() override { }
};

// 48572
struct npc_fourth_and_goal_deathwing : public ScriptedAI
{
    npc_fourth_and_goal_deathwing(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->setActive(true);
    }

    void UpdateAI(uint32 /*diff*/) override { }
};

// 69987
class spell_kezan_despawn_sharks : public SpellScript
{
    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        if (Creature* creature = GetHitCreature())
            if (creature->IsSummon())
                if (creature->ToTempSummon()->GetSummoner() == GetCaster())
                    creature->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_kezan_despawn_sharks::HandleHit, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum EndHotRod : uint32
{
    PATH_END_HOT_ROD    = 3767600, // waypoint_data (Azimov) : a remplir si absent
};

// 37676
struct npc_end_hot_rod : public ScriptedAI
{
    npc_end_hot_rod(Creature* creature) : ScriptedAI(creature) { }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void Reset() override
    {
        me->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
        me->SetSpeedRate(MOVE_RUN, 20.f);
        me->SetSpeedRate(MOVE_WALK, 20.f);
    }

    void PassengerBoarded(Unit* /*who*/, int8 /*seatId*/, bool apply) override
    {
        if (apply && !_started)
        {
            _started = true;
            me->GetMotionMaster()->MovePath(PATH_END_HOT_ROD, false);
        }
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != WAYPOINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 4:
                SassyTalk(0);
                break;
            case 14:
                SassyTalk(1);
                break;
            case 47:
                SassyTalk(2);
                break;
            case 48:
                me->KillSelf();
                break;
            default:
                break;
        }
    }

    void SassyTalk(uint8 id)
    {
        if (Vehicle* kit = me->GetVehicleKit())
            if (Unit* sassy = kit->GetPassenger(0))
                if (Creature* sassyCreature = sassy->ToCreature())
                    sassyCreature->AI()->Talk(id, kit->GetPassenger(1));
    }

private:
    bool _started = false;
};

// 91847 : a la fin du film de depart de Kezan, teleportation + lien aux Iles perdues
// Pas de AddMovieDelayedAction dans le fork : on attend la fin du film (SetMovie(0) a CMSG_COMPLETE_MOVIE), 90 s max
class LifeSavingsMovieEvent : public BasicEvent
{
public:
    LifeSavingsMovieEvent(Player* player, uint32 tick) : _player(player), _tick(tick) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        if ((_player->GetMovie() != 0 || _tick < 3) && _tick < 90)
        {
            _player->m_Events.AddEventAtOffset(new LifeSavingsMovieEvent(_player, _tick + 1), 1s);
            return true;
        }

        _player->RemoveAurasDueToSpell(91847);
        _player->CastSpell(_player, 74100, true); // Life Savings: Teleport & Bind to the Lost Isles
        return true;
    }

private:
    Player* _player;
    uint32 _tick;
};

class spell_life_saving_complete : public SpellScript
{
    void HandleHit()
    {
        if (Player* player = GetCaster()->ToPlayer())
            player->m_Events.AddEventAtOffset(new LifeSavingsMovieEvent(player, 1), 1s);
    }

    void Register() override
    {
        BeforeHit.Register(&spell_life_saving_complete::HandleHit);
    }
};

}

void AddSC_kezan()
{
    using namespace Kezan;

    RegisterCreatureAI(npc_defiant_troll);
    RegisterCreatureAI(npc_hot_rod_rolling_my_homies);
    RegisterCreatureAI(npc_homies);
    RegisterCreatureAI(npc_homies_summon);
    RegisterCreatureAI(npc_bling_bling);
    RegisterCreatureAI(npc_kezan_partygoer);
    RegisterSpellScript(spell_kezan_fireworks);
    RegisterSpellScript(spell_kabummbomb);
    RegisterCreatureAI(npc_shark_gob);
    RegisterCreatureAI(npc_boucanier_gob);
    RegisterSpellScript(spell_fourth_and_goal_kick_footbomb);
    RegisterCreatureAI(npc_fourth_and_goal_target);
    RegisterCreatureAI(npc_fourth_and_goal_deathwing);
    RegisterCreatureAI(npc_citoyen_gob);
    RegisterSpellScript(spell_great_bank_heist_vault_interact);
    RegisterCreatureAI(npc_first_bank_of_kezan_vault);
    RegisterSpellScript(spell_hobart_ingenious_cap_of_mook_foolery);
    RegisterSpellScript(spell_summon_gasbot);
    RegisterCreatureAI(npc_kezan_gasbot);
    RegisterSpellScript(spell_kezan_despawn_sharks);
    RegisterCreatureAI(npc_end_hot_rod);
    RegisterSpellScript(spell_life_saving_complete);
    new player_kezan_quests();
}
