/*
 * azimov_quetes_gnomes.cpp - Quêtes du départ des gnomes / Ironforge refaites en blizzlike
 *
 *  27635 Decontamination           : Sanitron 500 (46185) traverse le sas, les Decontamination Bunny (46165)
 *                                    lancent les étapes 1, 2, 3 (la 3e valide la quête), puis éjecte devant Braggle.
 *  25839 The Ultrasafe Personnel Launcher : Ultrasafe Personnel Launcher (41398) propulse le joueur
 *                                    jusqu'à la Retraite des Givrecrins, près de Slamp Wobblecog (41298).
 *  26118 Seize the Ambassador      : les Sturdy Manacles (sort 78628) ne marchent que près de Slaghammer (42146) ;
 *                                    l'ambassadeur menotté (42153) suit le joueur et se rend devant Moira (42129).
 */

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include <algorithm>
#include <list>

// ---------------------------------------------------------------- 27635 Decontamination
enum Decontamination
{
    QUEST_DECONTAMINATION   = 27635,
    NPC_DECON_BUNNY         = 46165,
    SPELL_DECON_STAGE_1     = 86075,
    SPELL_DECON_STAGE_2     = 86084,
    SPELL_DECON_STAGE_3     = 86086,   // valide la quête
    SPELL_IRRADIATED        = 80653,

    POINT_DECON_1 = 1,
    POINT_DECON_2,
    POINT_DECON_3,
    POINT_DECON_END,

    EVENT_DECON_START = 1,
    EVENT_DECON_NEXT,
    EVENT_DECON_EJECT,
    EVENT_DECON_HOME
};

// Le sas descend vers le sud (y décroissant), entre les deux rangées de bunnies, jusqu'à Braggle
static float const DeconStepY[4] = { 724.0f, 715.0f, 706.0f, 697.0f };

struct npc_azimov_sanitron_500 : public ScriptedAI
{
    npc_azimov_sanitron_500(Creature* creature) : ScriptedAI(creature), _step(0) { }

    void Reset() override
    {
        _events.Reset();
        _passenger.Clear();
        _step = 0;
    }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
        if (!passenger || !passenger->IsPlayer())
            return;

        if (apply)
        {
            _passenger = passenger->GetGUID();
            _step = 0;
            me->SetWalk(true);
            _events.ScheduleEvent(EVENT_DECON_START, 1500ms);
        }
        else if (_step < POINT_DECON_END)
        {
            // le joueur est descendu en route : retour à la place
            _events.Reset();
            _events.ScheduleEvent(EVENT_DECON_HOME, 1s);
        }
    }

    Player* GetPassenger() const
    {
        return ObjectAccessor::GetPlayer(*me, _passenger);
    }

    void StageOnPassenger(uint32 spellId)
    {
        Player* player = GetPassenger();
        if (!player)
            return;

        std::list<Creature*> bunnies;
        me->GetCreatureListWithEntryInGrid(bunnies, NPC_DECON_BUNNY, 14.0f);
        if (bunnies.empty())
            player->CastSpell(player, spellId, true);
        else
            for (Creature* bunny : bunnies)
                bunny->CastSpell(player, spellId, true);
    }

    void MoveToStep()
    {
        ++_step;
        Position pos = me->GetHomePosition();
        pos.m_positionY = DeconStepY[_step - 1];
        me->GetMotionMaster()->MovePoint(_step, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), false);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id == 0 || id > POINT_DECON_END)
            return;

        switch (id)
        {
            case POINT_DECON_1: StageOnPassenger(SPELL_DECON_STAGE_1); break;
            case POINT_DECON_2: StageOnPassenger(SPELL_DECON_STAGE_2); break;
            case POINT_DECON_3:
                StageOnPassenger(SPELL_DECON_STAGE_3);
                if (Player* player = GetPassenger())
                {
                    player->RemoveAurasDueToSpell(SPELL_IRRADIATED);
                    if (player->GetQuestStatus(QUEST_DECONTAMINATION) == QUEST_STATUS_INCOMPLETE)
                        player->AreaExploredOrEventHappens(QUEST_DECONTAMINATION);   // au cas où le sort ne passe pas
                }
                break;
            case POINT_DECON_END:
                _events.ScheduleEvent(EVENT_DECON_EJECT, 1s);
                return;
        }
        _events.ScheduleEvent(EVENT_DECON_NEXT, 2500ms);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DECON_START:
                case EVENT_DECON_NEXT:
                    MoveToStep();
                    break;
                case EVENT_DECON_EJECT:
                    if (Vehicle* kit = me->GetVehicleKit())
                        kit->RemoveAllPassengers();
                    _events.ScheduleEvent(EVENT_DECON_HOME, 2s);
                    break;
                case EVENT_DECON_HOME:
                    _step = 0;
                    _passenger.Clear();
                    me->GetMotionMaster()->MoveTargetedHome();
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _passenger;
    uint8 _step;
};

// ---------------------------------------------------------------- 25839 Ultrasafe Personnel Launcher
enum UltrasafeLauncher
{
    QUEST_ULTRASAFE_LAUNCHER = 25839,
    NPC_SLAMP_WOBBLECOG      = 41298,
    SPELL_VLD_PARACHUTE      = 77404,
    EVENT_LAUNCH             = 1
};

// Zone d'atterrissage : quelques mètres devant Slamp Wobblecog (Retraite des Givrecrins)
static Position const LauncherDropZone = { -5686.5f, -913.6f, 398.4f, 5.48f };

struct npc_azimov_ultrasafe_launcher : public ScriptedAI
{
    npc_azimov_ultrasafe_launcher(Creature* creature) : ScriptedAI(creature) { }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
        if (!apply || !passenger || !passenger->IsPlayer())
            return;
        _passenger = passenger->GetGUID();
        _events.ScheduleEvent(EVENT_LAUNCH, 2s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() != EVENT_LAUNCH)
            return;

        Player* player = ObjectAccessor::GetPlayer(*me, _passenger);
        _passenger.Clear();
        if (!player)
            return;

        player->ExitVehicle();

        // trajectoire en cloche : ~5 s de vol, ~50 m de hauteur
        float const gravity = 19.29110527f;
        float const speedZ = 45.0f;
        float const flightTime = 2.0f * speedZ / gravity;
        float dist = player->GetExactDist2d(LauncherDropZone.GetPositionX(), LauncherDropZone.GetPositionY());
        float speedXY = std::max(10.0f, dist / flightTime);
        float z = player->GetMapHeight(LauncherDropZone.GetPositionX(), LauncherDropZone.GetPositionY(), LauncherDropZone.GetPositionZ() + 10.0f);
        if (z <= INVALID_HEIGHT)
            z = LauncherDropZone.GetPositionZ();

        player->GetMotionMaster()->MoveJump(LauncherDropZone.GetPositionX(), LauncherDropZone.GetPositionY(), z, LauncherDropZone.GetOrientation(), speedXY, speedZ);

        if (player->GetQuestStatus(QUEST_ULTRASAFE_LAUNCHER) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(QUEST_ULTRASAFE_LAUNCHER);
    }

private:
    EventMap _events;
    ObjectGuid _passenger;
};

// ---------------------------------------------------------------- 26118 Seize the Ambassador
enum SeizeTheAmbassador
{
    QUEST_SEIZE_THE_AMBASSADOR = 26118,
    NPC_SLAGHAMMER             = 42146,
    NPC_SLAGHAMMER_ARRESTED    = 42153,
    NPC_MOIRA                  = 42129,
    SPELL_ARREST               = 78628,
    SPELL_ARREST_CREDIT        = 78642,
    EVENT_ARREST_CHECK         = 1
};

class spell_azimov_arrest_slaghammer : public SpellScript
{
    SpellCastResult CheckCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->FindNearestCreature(NPC_SLAGHAMMER, 15.0f))
            return SPELL_FAILED_OUT_OF_RANGE;
        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_azimov_arrest_slaghammer::CheckCast);
    }
};

struct npc_azimov_slaghammer_arrested : public ScriptedAI
{
    npc_azimov_slaghammer_arrested(Creature* creature) : ScriptedAI(creature), _ticks(0), _done(false) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (!summoner || !summoner->IsPlayer())
        {
            me->DespawnOrUnsummon();
            return;
        }
        _player = summoner->GetGUID();

        // l'ambassadeur « libre » disparaît le temps de l'arrestation
        if (Creature* free = me->FindNearestCreature(NPC_SLAGHAMMER, 30.0f))
            free->DespawnOrUnsummon(0, 60s);

        me->SetReactState(REACT_PASSIVE);
        me->SetWalk(false);
        me->GetMotionMaster()->MoveFollow(summoner, 2.5f, float(M_PI));
        me->Say("Bien, bien... Je vous suis. Inutile de serrer si fort ces menottes !", LANG_UNIVERSAL);
        _events.ScheduleEvent(EVENT_ARREST_CHECK, 1s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() != EVENT_ARREST_CHECK || _done)
            return;

        Player* player = ObjectAccessor::GetPlayer(*me, _player);
        if (!player || !player->IsAlive() || player->GetQuestStatus(QUEST_SEIZE_THE_AMBASSADOR) != QUEST_STATUS_INCOMPLETE
            || !me->IsWithinDistInMap(player, 80.0f) || ++_ticks > 300)
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (me->FindNearestCreature(NPC_MOIRA, 12.0f))
        {
            _done = true;
            me->GetMotionMaster()->Clear();
            me->Say("Tr\xC3\xA8s bien, Moira. Je me rends.", LANG_UNIVERSAL);
            me->CastSpell(player, SPELL_ARREST_CREDIT, true);
            if (player->GetQuestStatus(QUEST_SEIZE_THE_AMBASSADOR) == QUEST_STATUS_INCOMPLETE)
                player->AreaExploredOrEventHappens(QUEST_SEIZE_THE_AMBASSADOR);
            me->DespawnOrUnsummon(4s);
            return;
        }

        _events.ScheduleEvent(EVENT_ARREST_CHECK, 1s);
    }

private:
    EventMap _events;
    ObjectGuid _player;
    uint32 _ticks;
    bool _done;
};

void AddSC_azimov_quetes_gnomes()
{
    RegisterCreatureAI(npc_azimov_sanitron_500);
    RegisterCreatureAI(npc_azimov_ultrasafe_launcher);
    RegisterCreatureAI(npc_azimov_slaghammer_arrested);
    RegisterSpellScript(spell_azimov_arrest_slaghammer);
}
