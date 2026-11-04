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

#include "end_time.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "ObjectAccessor.h"
#include <unordered_map>
#include <vector>
#include "TemporarySummon.h"
#include "MotionMaster.h"
#include "Map.h"

namespace EndTime
{
enum Events
{
    // Nozdormu
    EVENT_TALK_ENCOUNTER_INTRO = 1,
    EVENT_TALK_ENCOUNTER_OUTRO_1,
    EVENT_TALK_ENCOUNTER_OUTRO_2,
    EVENT_TALK_ENCOUNTER_OUTRO_3,
    EVENT_TALK_ENCOUNTER_OUTRO_4
};

enum Actions
{
    // Nozdormu
    ACTION_ENCOUNTER_INTRO = 1,
    ACTION_ENCOUNTER_OUTRO = 2
};

enum Texts
{
    // Nozdormu
    SAY_ENCOUNTER_INTRO     = 0,
    SAY_ENCOUNTER_OUTRO_1   = 1,
    SAY_ENCOUNTER_OUTRO_2   = 2,
    SAY_ENCOUNTER_OUTRO_3   = 3,
    SAY_ENCOUNTER_OUTRO_4   = 4
};

enum GossipMenuIds
{
    GOSSIP_MENU_ID_NOZDORMU                 = 13360,
    GOSSIP_MENU_OPTION_ID_WELL_OF_ETERNITY  = 0
};

Position const NozdormuEncounterTeleportPosition    = { 4033.37f, -294.457f, 181.613f, 5.8119464f };
Position const NozdormuDefeatTeleportPosition       = { 4138.48f, -429.436f, 122.259f, 5.8119464f };

struct npc_end_time_nozdormu : public NullCreatureAI
{
    npc_end_time_nozdormu(Creature* creature) : NullCreatureAI(creature),  _instance(nullptr), _introDone(false) { }

    void InitializeAI() override
    {
        _instance = me->GetInstanceScript();
    }

    bool GossipHello(Player* player) override
    {
        if (!_instance)
            return true;

        if (_instance->GetBossState(DATA_MUROZOND) == DONE)
            AddGossipItemFor(player, GOSSIP_MENU_ID_NOZDORMU, GOSSIP_MENU_OPTION_ID_WELL_OF_ETERNITY, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, player->GetGossipTextId(GOSSIP_MENU_ID_NOZDORMU, me), me->GetGUID());

        return true;
    }

    bool GossipSelect(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        // Todo: Well of Eternity teleport

        return true;
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_ENCOUNTER_INTRO:
                if (!_introDone)
                {
                    _events.ScheduleEvent(EVENT_TALK_ENCOUNTER_INTRO, 8s + 400ms);
                    _introDone = true;
                }
                break;
            case ACTION_ENCOUNTER_OUTRO:
                me->setActive(true);
                me->NearTeleportTo(NozdormuDefeatTeleportPosition);
                _events.ScheduleEvent(EVENT_TALK_ENCOUNTER_OUTRO_1, 8s + 700ms);
                me->setActive(false);
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
                case EVENT_TALK_ENCOUNTER_INTRO:
                    Talk(SAY_ENCOUNTER_INTRO);
                    me->setActive(true);
                    me->NearTeleportTo(NozdormuEncounterTeleportPosition);
                    me->setActive(false);
                    break;
                case EVENT_TALK_ENCOUNTER_OUTRO_1:
                    Talk(SAY_ENCOUNTER_OUTRO_1);
                    _events.ScheduleEvent(EVENT_TALK_ENCOUNTER_OUTRO_2, 17s);
                    break;
                case EVENT_TALK_ENCOUNTER_OUTRO_2:
                    Talk(SAY_ENCOUNTER_OUTRO_2);
                    _events.ScheduleEvent(EVENT_TALK_ENCOUNTER_OUTRO_3, 17s);
                    break;
                case EVENT_TALK_ENCOUNTER_OUTRO_3:
                    Talk(SAY_ENCOUNTER_OUTRO_3);
                    _events.ScheduleEvent(EVENT_TALK_ENCOUNTER_OUTRO_4, 14s);
                    break;
                case EVENT_TALK_ENCOUNTER_OUTRO_4:
                    Talk(SAY_ENCOUNTER_OUTRO_4);
                    me->SetFacingTo(3.1765f);
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    InstanceScript* _instance;
    bool _introDone;
};

enum TimeTransitDeviceAreaIds
{
    AREA_ID_ENTRYWAY_OF_TIME = 5796
};

enum TimeTransitDeviceSpells
{
    SPELL_TELEPORT_TO_ENTRANCE              = 102564,
    SPELL_TELEPORT_TO_BLUE_DRAGONSHRINE     = 102126,
    SPELL_TELEPORT_TO_RUBY_DRAGONSHRINE     = 102579,
    SPELL_TELEPORT_TO_BLACK_DRAGONSHRINE    = 103868,
    SPELL_TELEPORT_TO_EMERALD_DRAGONSHRINE  = 104761,
    SPELL_TELEPORT_TO_BRONZE_DRAGONSHRINE   = 104764
};

enum TimeTransitGossipMenuIds
{
    GOSSIP_MENU_ID_SELECT_YOUR_DESTINATION = 13321
};

enum TimeTransitGossipIndexes
{
    GOSSIP_INDEX_TELEPORT_TO_ENTRYWAY_OF_TIME                   = 0,
    GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_FIRST_ECHO       = 1,
    GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_FIRST_ECHO    = 2,
    GOSSIP_INDEX_TELEPORT_TO_BLUE_DRAGONSHRINE_FIRST_ECHO       = 3,
    GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_FIRST_ECHO      = 4,
    GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_SECOND_ECHO      = 5,
    GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_SECOND_ECHO   = 6,
    GOSSIP_INDEX_TELEPORT_TO_BLUE_DRAGONSHRINE_SECOND_ECHO      = 7,
    GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_SECOND_ECHO     = 8,
    GOSSIP_INDEX_TELEPORT_TO_BRONZE_DRAGONSHRINE                = 9
};

static std::unordered_map<uint32 /*gossipMenuId*/, uint32 /*teleportSpellId*/> TransitDeviceTeleportSpells =
{
    { GOSSIP_INDEX_TELEPORT_TO_ENTRYWAY_OF_TIME,                    SPELL_TELEPORT_TO_ENTRANCE              },
    { GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_FIRST_ECHO,        SPELL_TELEPORT_TO_RUBY_DRAGONSHRINE     },
    { GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_FIRST_ECHO,     SPELL_TELEPORT_TO_EMERALD_DRAGONSHRINE  },
    { GOSSIP_INDEX_TELEPORT_TO_BLUE_DRAGONSHRINE_FIRST_ECHO,        SPELL_TELEPORT_TO_BLUE_DRAGONSHRINE     },
    { GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_FIRST_ECHO,       SPELL_TELEPORT_TO_BLACK_DRAGONSHRINE    },
    { GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_SECOND_ECHO,       SPELL_TELEPORT_TO_RUBY_DRAGONSHRINE     },
    { GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_SECOND_ECHO,    SPELL_TELEPORT_TO_EMERALD_DRAGONSHRINE  },
    { GOSSIP_INDEX_TELEPORT_TO_BLUE_DRAGONSHRINE_SECOND_ECHO,       SPELL_TELEPORT_TO_BLUE_DRAGONSHRINE     },
    { GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_SECOND_ECHO,      SPELL_TELEPORT_TO_BLACK_DRAGONSHRINE    },
    { GOSSIP_INDEX_TELEPORT_TO_BRONZE_DRAGONSHRINE,                 SPELL_TELEPORT_TO_BRONZE_DRAGONSHRINE   }
};

struct go_end_time_time_transit_device : public GameObjectAI
{
    go_end_time_time_transit_device(GameObject* gameObject) : GameObjectAI(gameObject), _instance(nullptr) { }

    void InitializeAI() override
    {
        _instance = me->GetInstanceScript();
    }

    bool GossipHello(Player* player) override
    {
        if (!_instance)
            return false;

        if (player->GetAreaId() != AREA_ID_ENTRYWAY_OF_TIME)
            AddGossipItemFor(player, GOSSIP_MENU_ID_SELECT_YOUR_DESTINATION, GOSSIP_INDEX_TELEPORT_TO_ENTRYWAY_OF_TIME, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + AsUnderlyingType(GOSSIP_INDEX_TELEPORT_TO_ENTRYWAY_OF_TIME));

        // Azimov : les deux échos de l'instance sont Jaina (sanctuaire bleu) puis Baine (sanctuaire noir),
        // puis le sanctuaire de bronze (Murozond) quand les deux sont vaincus.
        auto addOption = [&](TimeTransitGossipIndexes index)
        {
            AddGossipItemFor(player, GOSSIP_MENU_ID_SELECT_YOUR_DESTINATION, index, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + AsUnderlyingType(index));
        };

        bool const jainaDone   = _instance->GetBossState(DATA_ECHO_OF_JAINA) == DONE;
        bool const baineDone   = _instance->GetBossState(DATA_ECHO_OF_BAINE) == DONE;
        bool const tyrandeDone = _instance->GetBossState(DATA_ECHO_OF_TYRANDE) == DONE;
        bool const sylvanasDone = _instance->GetBossState(DATA_ECHO_OF_SYLVANAS) == DONE;
        uint8 const defeatedEchoes = uint8(jainaDone) + uint8(baineDone) + uint8(tyrandeDone) + uint8(sylvanasDone);

        // Premier écho : Jaina. Second écho au choix : Sylvanas, Tyrande ou Baine. Puis Murozond.
        addOption(GOSSIP_INDEX_TELEPORT_TO_BLUE_DRAGONSHRINE_FIRST_ECHO);
        if (jainaDone && defeatedEchoes < 2)
        {
            addOption(GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_SECOND_ECHO);
            addOption(GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_SECOND_ECHO);
            addOption(GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_SECOND_ECHO);
        }
        else if (sylvanasDone)
            addOption(GOSSIP_INDEX_TELEPORT_TO_RUBY_DRAGONSHRINE_SECOND_ECHO);
        else if (tyrandeDone)
            addOption(GOSSIP_INDEX_TELEPORT_TO_EMERALD_DRAGONSHRINE_SECOND_ECHO);
        else if (baineDone)
            addOption(GOSSIP_INDEX_TELEPORT_TO_BLACK_DRAGONSHRINE_SECOND_ECHO);

        if (defeatedEchoes >= 2)
            addOption(GOSSIP_INDEX_TELEPORT_TO_BRONZE_DRAGONSHRINE);

        SendGossipMenuFor(player, player->GetGossipTextId(GOSSIP_MENU_ID_SELECT_YOUR_DESTINATION, me), me->GetGUID());

        return true;
    }

    bool GossipSelect(Player* player, uint32 /*gossipMenuId*/, uint32 action) override
    {
        uint32 index = player->PlayerTalkClass->GetGossipOptionAction(action) - GOSSIP_ACTION_INFO_DEF;
        auto itr = TransitDeviceTeleportSpells.find(index);
        if (itr != TransitDeviceTeleportSpells.end())
            player->CastSpell(player, itr->second);
        ClearGossipMenuFor(player);

        return true;
    }

private:
    InstanceScript* _instance;
};

struct go_end_time_fragment_of_jainas_staff : public GameObjectAI
{
    go_end_time_fragment_of_jainas_staff(GameObject* gameObject) : GameObjectAI(gameObject), _instance(nullptr) { }

    void InitializeAI() override
    {
        _instance = me->GetInstanceScript();
    }

    bool GossipHello(Player* /*player*/) override
    {
        if (!_instance)
            return false;

        _instance->SetData(DATA_COLLECTED_FRAGMENT_OF_JAINAS_STAFF, 0);
        me->DespawnOrUnsummon();

        return true;
    }

private:
    InstanceScript* _instance;
};

// ============================================================================
// Azimov : Écho de Baine (sanctuaire draconique noir)
// ============================================================================
namespace EchoOfBaine
{
enum Spells
{
    // Baine
    SPELL_PULVERIZE_JUMP        = 101626, // bond vers la cible
    SPELL_PULVERIZE_IMPACT      = 101627, // dégâts + destruction de la plate-forme
    SPELL_THROW_TOTEM           = 101615, // lance le totem sur une cible
    SPELL_MOLTEN_AXE            = 101836, // hache en fusion (Baine dans la lave)

    // Joueurs
    SPELL_MAGMA                 = 101619, // joueur dans la lave
    SPELL_THROW_TOTEM_BACK_AURA = 107837, // joueur qui porte le totem
    SPELL_THROW_TOTEM_BACK      = 101603, // renvoi du totem sur Baine
    SPELL_THROW_TOTEM_BACK_HIT  = 101602, // impact du totem renvoyé (étourdit Baine)

    // Totem
    SPELL_BAINES_TOTEM          = 101594  // visuel du totem au sol
};

enum Texts
{
    SAY_INTRO       = 0,
    SAY_AGGRO       = 1,
    SAY_PULVERIZE   = 2,
    SAY_SLAY        = 3,
    SAY_DEATH       = 4
};

enum Events
{
    EVENT_PULVERIZE = 1,
    EVENT_THROW_TOTEM,
    EVENT_CHECK_LAVA
};

enum Creatures
{
    NPC_BAINES_TOTEM = 54434
};

struct boss_echo_of_baine : public BossAI
{
    boss_echo_of_baine(Creature* creature) : BossAI(creature, DATA_ECHO_OF_BAINE), _introDone(false) { }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_introDone && who->IsPlayer() && !who->ToPlayer()->IsGameMaster() && me->IsWithinDistInMap(who, 80.0f))
        {
            _introDone = true;
            Talk(SAY_INTRO);
        }

        BossAI::MoveInLineOfSight(who);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_SLAY, victim);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO, who);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        events.ScheduleEvent(EVENT_THROW_TOTEM, 10s);
        events.ScheduleEvent(EVENT_PULVERIZE, 30s);
        events.ScheduleEvent(EVENT_CHECK_LAVA, 1s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        RemoveTotemsFromPlayers();
        summons.DespawnAll();
        _EnterEvadeMode(why);
        _DespawnAtEvade();
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        Talk(SAY_DEATH);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        RemoveTotemsFromPlayers();
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        if (summon->GetEntry() == NPC_BAINES_TOTEM)
            summon->DespawnOrUnsummon(60s);
    }

    // Le totem renvoyé touche Baine : le joueur ne le porte plus
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_THROW_TOTEM_BACK_HIT)
            return;
        if (Unit* unitCaster = caster ? caster->ToUnit() : nullptr)
            unitCaster->RemoveAurasDueToSpell(SPELL_THROW_TOTEM_BACK_AURA);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        // Fin du bond de Pulvérisation : impact et destruction de la plate-forme
        if (type == EFFECT_MOTION_TYPE && id == EVENT_JUMP)
        {
            DoCastSelf(SPELL_PULVERIZE_IMPACT, true);
            me->SetReactState(REACT_AGGRESSIVE);
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
                case EVENT_THROW_TOTEM:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 1, 100.0f, true))
                        DoCast(target, SPELL_THROW_TOTEM);
                    else if (Unit* victim = me->GetVictim())
                        DoCast(victim, SPELL_THROW_TOTEM);
                    events.Repeat(25s);
                    break;
                case EVENT_PULVERIZE:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 100.0f, true))
                    {
                        Talk(SAY_PULVERIZE);
                        me->SetReactState(REACT_PASSIVE);
                        me->AttackStop();
                        DoCast(target, SPELL_PULVERIZE_JUMP);
                    }
                    events.Repeat(40s);
                    break;
                case EVENT_CHECK_LAVA:
                    CheckLava();
                    events.Repeat(1s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    // Baine dans la lave : hache en fusion. Joueurs dans la lave : Magma.
    void CheckLava()
    {
        if (me->IsInWater() && !me->HasAura(SPELL_MOLTEN_AXE))
            DoCastSelf(SPELL_MOLTEN_AXE, true);

        for (MapReference const& ref : me->GetMap()->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (!player || !player->IsAlive() || player->IsGameMaster() || !player->IsWithinDist(me, 150.0f))
                continue;

            if (player->IsInWater() && !player->HasAura(SPELL_MAGMA))
                player->CastSpell(player, SPELL_MAGMA, true);
        }
    }

    void RemoveTotemsFromPlayers()
    {
        for (MapReference const& ref : me->GetMap()->GetPlayers())
            if (Player* player = ref.GetSource())
                player->RemoveAurasDueToSpell(SPELL_THROW_TOTEM_BACK_AURA);
    }

    bool _introDone;
};

// 54434 - Totem de Baine : on le ramasse d'un clic (npc_spellclick_spells -> 107837)
struct npc_echo_of_baine_baines_totem : public NullCreatureAI
{
    npc_echo_of_baine_baines_totem(Creature* creature) : NullCreatureAI(creature) { }

    void JustAppeared() override
    {
        DoCastSelf(SPELL_BAINES_TOTEM, true);
    }

    void OnSpellClick(Unit* /*clicker*/, bool& result) override
    {
        if (result)
            me->DespawnOrUnsummon();
    }
};

}

// ============================================================================
// Azimov : Écho de Tyrande (sanctuaire draconique émeraude)
// ============================================================================
namespace EchoOfTyrande
{
enum Spells
{
    SPELL_MOONBOLT          = 102193,
    SPELL_STARDUST          = 102173,
    SPELL_MOONLANCE         = 102151, // invoque la lance (54574)
    SPELL_MOONLANCE_AURA    = 102150, // dégâts + étourdissement autour de la lance
    SPELL_MOONLANCE_SPLIT   = 102152, // la lance se divise en 3 (54580-54582)
    SPELL_LUNAR_GUIDANCE    = 102472, // 80 % et 55 % : hâte
    SPELL_TEARS_OF_ELUNE    = 102241  // 30 % : pluie d'arcanes
};

enum Texts
{
    SAY_INTRO           = 0,
    SAY_AGGRO           = 1,
    SAY_GUIDANCE_1      = 2,
    SAY_GUIDANCE_2      = 3,
    SAY_TEARS           = 4,
    SAY_MOONLANCE       = 5,
    SAY_SLAY            = 6,
    SAY_DEATH           = 7
};

enum Events
{
    EVENT_MOONBOLT = 1,
    EVENT_STARDUST,
    EVENT_MOONLANCE
};

enum Creatures
{
    NPC_MOONLANCE           = 54574,
    NPC_MOONLANCE_SPLIT_1   = 54580,
    NPC_MOONLANCE_SPLIT_2   = 54581,
    NPC_MOONLANCE_SPLIT_3   = 54582
};

struct boss_echo_of_tyrande : public BossAI
{
    boss_echo_of_tyrande(Creature* creature) : BossAI(creature, DATA_ECHO_OF_TYRANDE), _introDone(false), _phase(0) { }

    void JustAppeared() override
    {
        // L'événement des rayons de lune n'est pas scripté : Tyrande est attaquable directement
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
        _phase = 0;
        events.ScheduleEvent(EVENT_MOONBOLT, 1s);
        events.ScheduleEvent(EVENT_STARDUST, 7s);
        events.ScheduleEvent(EVENT_MOONLANCE, 12s);
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
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (_phase == 0 && me->HealthBelowPctDamaged(80, damage))
        {
            _phase = 1;
            Talk(SAY_GUIDANCE_1);
            DoCastSelf(SPELL_LUNAR_GUIDANCE, true);
        }
        else if (_phase == 1 && me->HealthBelowPctDamaged(55, damage))
        {
            _phase = 2;
            Talk(SAY_GUIDANCE_2);
            DoCastSelf(SPELL_LUNAR_GUIDANCE, true);
        }
        else if (_phase == 2 && me->HealthBelowPctDamaged(30, damage))
        {
            _phase = 3;
            Talk(SAY_TEARS);
            DoCastSelf(SPELL_TEARS_OF_ELUNE, true);
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
                case EVENT_MOONBOLT:
                    DoCastVictim(SPELL_MOONBOLT);
                    events.Repeat(3s);
                    break;
                case EVENT_STARDUST:
                    DoCastAOE(SPELL_STARDUST);
                    events.Repeat(18s);
                    break;
                case EVENT_MOONLANCE:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
                    {
                        Talk(SAY_MOONLANCE);
                        DoCast(target, SPELL_MOONLANCE);
                    }
                    events.Repeat(22s);
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
    bool _introDone;
    uint8 _phase;
};

// 54574 / 54580-54582 - Javelot de lune : avance en ligne droite en blessant et étourdissant au passage.
// La lance principale se divise en trois au bout de sa course.
struct npc_echo_of_tyrande_moonlance : public NullCreatureAI
{
    npc_echo_of_tyrande_moonlance(Creature* creature) : NullCreatureAI(creature), _split(false) { }

    void IsSummonedBy(Unit* summoner) override
    {
        float angle = summoner ? summoner->GetAngle(me) : me->GetOrientation();
        Launch(angle);
    }

    void Launch(float angle)
    {
        me->SetOrientation(angle);
        DoCastSelf(SPELL_MOONLANCE_AURA, true);
        float const dist = me->GetEntry() == NPC_MOONLANCE ? 12.0f : 25.0f;
        Position dest = me->GetPosition();
        dest.m_positionX += std::cos(angle) * dist;
        dest.m_positionY += std::sin(angle) * dist;
        me->UpdateGroundPositionZ(dest.m_positionX, dest.m_positionY, dest.m_positionZ);
        me->GetMotionMaster()->MovePoint(1, dest, false);
        me->DespawnOrUnsummon(me->GetEntry() == NPC_MOONLANCE ? 6s : 10s);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != 1)
            return;

        if (me->GetEntry() == NPC_MOONLANCE && !_split)
        {
            _split = true;
            // Trois lances en éventail
            float const base = me->GetOrientation();
            uint32 const entries[3] = { NPC_MOONLANCE_SPLIT_1, NPC_MOONLANCE_SPLIT_2, NPC_MOONLANCE_SPLIT_3 };
            for (uint8 i = 0; i < 3; ++i)
            {
                float const angle = Position::NormalizeOrientation(base + (float(i) - 1.0f) * float(M_PI) / 4.0f);
                Position pos = me->GetPosition();
                pos.SetOrientation(angle);
                if (TempSummon* split = me->SummonCreature(entries[i], pos, TEMPSUMMON_TIMED_DESPAWN, 10000))
                    if (npc_echo_of_tyrande_moonlance* ai = CAST_AI(npc_echo_of_tyrande_moonlance, split->AI()))
                        ai->Launch(angle);
            }
            me->DespawnOrUnsummon(500ms);
        }
        else
            me->DespawnOrUnsummon(500ms);
    }

private:
    bool _split;
};
}

// ============================================================================
// Azimov : Écho de Sylvanas (sanctuaire draconique rubis)
// ============================================================================
namespace EchoOfSylvanas
{
enum Spells
{
    SPELL_UNHOLY_SHOT           = 101411,
    SPELL_BLACK_ARROW           = 101404,
    SPELL_SHRIEK_OF_THE_HIGHBORNE = 101412,
    SPELL_CALLING_IMMUNITY      = 100686, // Appel des Bien-nés : insensible
    SPELL_SACRIFICE             = 101348, // goule arrivée près de Sylvanas
    SPELL_WRACKING_PAIN         = 100865  // utilisé pour le journal de combat (dégâts du lien)
};

enum Texts
{
    SAY_AGGRO   = 0,
    SAY_CALLING = 1,
    SAY_SLAY    = 2,
    SAY_RESET   = 3,
    SAY_DEATH   = 4
};

enum Events
{
    EVENT_UNHOLY_SHOT = 1,
    EVENT_BLACK_ARROW,
    EVENT_SHRIEK,
    EVENT_CALLING,
    EVENT_WRACKING_PAIN,
    EVENT_CALLING_END
};

enum Misc
{
    NPC_RISEN_GHOUL     = 54191,
    POINT_REACH_SYLVANAS = 1,
    GHOUL_COUNT         = 8
};

constexpr float GHOUL_RING_RADIUS   = 28.0f;
constexpr float GAP_HALF_ANGLE      = float(M_PI) / 8.0f; // 22,5° de chaque côté d'une goule tuée
constexpr uint32 WRACKING_PAIN_DAMAGE = 55000;

struct boss_echo_of_sylvanas : public BossAI
{
    boss_echo_of_sylvanas(Creature* creature) : BossAI(creature, DATA_ECHO_OF_SYLVANAS), _calling(false) { }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO, who);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);
        ScheduleCombatEvents();
        events.ScheduleEvent(EVENT_CALLING, 30s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        Talk(SAY_RESET);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        summons.DespawnAll();
        _ghouls.clear();
        _calling = false;
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

    // Une goule tuée ouvre une brèche dans le cercle
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        auto itr = _ghouls.find(summon->GetGUID());
        if (itr != _ghouls.end())
        {
            _gaps.push_back(itr->second);
            _ghouls.erase(itr);
        }
        summons.Despawn(summon);
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        _ghouls.erase(summon->GetGUID());
        BossAI::SummonedCreatureDespawn(summon);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (!_calling && me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_UNHOLY_SHOT:
                    DoCastVictim(SPELL_UNHOLY_SHOT);
                    events.Repeat(10s);
                    break;
                case EVENT_BLACK_ARROW:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 50.0f, true))
                        DoCast(target, SPELL_BLACK_ARROW);
                    events.Repeat(15s);
                    break;
                case EVENT_SHRIEK:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 50.0f, true))
                        DoCast(target, SPELL_SHRIEK_OF_THE_HIGHBORNE);
                    events.Repeat(16s);
                    break;
                case EVENT_CALLING:
                    StartCalling();
                    break;
                case EVENT_WRACKING_PAIN:
                    HandleWrackingPain();
                    if (_ghouls.empty())
                        events.ScheduleEvent(EVENT_CALLING_END, 1s);
                    else
                        events.Repeat(1s);
                    break;
                case EVENT_CALLING_END:
                    EndCalling();
                    break;
                default:
                    break;
            }

            if (!_calling && me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (!_calling)
            DoMeleeAttackIfReady();
    }

private:
    void ScheduleCombatEvents()
    {
        events.ScheduleEvent(EVENT_UNHOLY_SHOT, 4s);
        events.ScheduleEvent(EVENT_BLACK_ARROW, 9s);
        events.ScheduleEvent(EVENT_SHRIEK, 13s);
    }

    // Appel des Bien-nés : 8 goules en cercle marchent vers Sylvanas, reliées par des liens d'ombre
    void StartCalling()
    {
        _calling = true;
        events.CancelEvent(EVENT_UNHOLY_SHOT);
        events.CancelEvent(EVENT_BLACK_ARROW);
        events.CancelEvent(EVENT_SHRIEK);

        Talk(SAY_CALLING);
        me->InterruptNonMeleeSpells(false);
        me->AttackStop();
        me->SetReactState(REACT_PASSIVE);
        me->GetMotionMaster()->Clear();
        me->StopMoving();
        DoCastSelf(SPELL_CALLING_IMMUNITY, true);

        _ghouls.clear();
        _gaps.clear();
        for (uint8 i = 0; i < GHOUL_COUNT; ++i)
        {
            float const angle = Position::NormalizeOrientation(float(i) * 2.0f * float(M_PI) / float(GHOUL_COUNT));
            Position pos = me->GetPosition();
            pos.m_positionX += std::cos(angle) * GHOUL_RING_RADIUS;
            pos.m_positionY += std::sin(angle) * GHOUL_RING_RADIUS;
            me->UpdateGroundPositionZ(pos.m_positionX, pos.m_positionY, pos.m_positionZ);
            pos.SetOrientation(Position::NormalizeOrientation(angle + float(M_PI)));
            if (Creature* ghoul = me->SummonCreature(NPC_RISEN_GHOUL, pos, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 40000))
            {
                _ghouls[ghoul->GetGUID()] = angle;
                ghoul->SetReactState(REACT_PASSIVE);
                ghoul->SetWalk(true);
                ghoul->SetSpeedRate(MOVE_WALK, 0.6f);
                Position dest = me->GetPosition();
                dest.m_positionX += std::cos(angle) * 2.0f;
                dest.m_positionY += std::sin(angle) * 2.0f;
                ghoul->GetMotionMaster()->MovePoint(POINT_REACH_SYLVANAS, dest);
            }
        }

        events.ScheduleEvent(EVENT_WRACKING_PAIN, 2s);
        events.ScheduleEvent(EVENT_CALLING_END, 35s); // sécurité
    }

    // Lien d'ombre : sur le cercle des goules ou derrière, sauf dans la brèche d'une goule tuée
    void HandleWrackingPain()
    {
        if (_ghouls.empty())
            return;

        float ring = 0.0f;
        uint8 count = 0;
        for (auto const& [guid, angle] : _ghouls)
            if (Creature* ghoul = ObjectAccessor::GetCreature(*me, guid))
            {
                ring += me->GetExactDist2d(ghoul);
                ++count;
            }
        if (!count)
            return;
        ring /= float(count);

        SpellInfo const* painInfo = sSpellMgr->GetSpellInfo(SPELL_WRACKING_PAIN);
        for (MapReference const& ref : me->GetMap()->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (!player || !player->IsAlive() || player->IsGameMaster() || !player->IsWithinDist(me, 80.0f))
                continue;

            if (me->GetExactDist2d(player) < ring - 2.5f)
                continue; // à l'intérieur du cercle

            float const playerAngle = me->GetAngle(player);
            bool inGap = false;
            for (float gap : _gaps)
            {
                float diff = std::fabs(Position::NormalizeOrientation(playerAngle - gap));
                if (diff > float(M_PI))
                    diff = 2.0f * float(M_PI) - diff;
                if (diff <= GAP_HALF_ANGLE)
                {
                    inGap = true;
                    break;
                }
            }
            if (inGap)
                continue;

            me->SendSpellNonMeleeDamageLog(player, SPELL_WRACKING_PAIN, WRACKING_PAIN_DAMAGE, SPELL_SCHOOL_MASK_SHADOW, 0, 0, false, 0);
            Unit::DealDamage(me, player, WRACKING_PAIN_DAMAGE, 0, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW, painInfo, false);
        }
    }

    void EndCalling()
    {
        if (!_calling)
            return;

        _calling = false;
        events.CancelEvent(EVENT_WRACKING_PAIN);
        events.CancelEvent(EVENT_CALLING_END);
        for (auto const& [guid, angle] : _ghouls)
            if (Creature* ghoul = ObjectAccessor::GetCreature(*me, guid))
                ghoul->DespawnOrUnsummon();
        _ghouls.clear();
        _gaps.clear();

        me->RemoveAurasDueToSpell(SPELL_CALLING_IMMUNITY);
        me->SetReactState(REACT_AGGRESSIVE);
        if (Unit* victim = me->GetVictim())
            AttackStart(victim);

        ScheduleCombatEvents();
        events.ScheduleEvent(EVENT_CALLING, 50s);
    }

    bool _calling;
    std::unordered_map<ObjectGuid, float> _ghouls; // goule -> angle de départ
    std::vector<float> _gaps;
};

// 54191 - Goule ressuscitée : arrivée près de Sylvanas, elle se sacrifie
struct npc_echo_of_sylvanas_risen_ghoul : public ScriptedAI
{
    npc_echo_of_sylvanas_risen_ghoul(Creature* creature) : ScriptedAI(creature), _done(false) { }

    void AttackStart(Unit* /*who*/) override { } // elle ne fait que marcher
    void UpdateAI(uint32 /*diff*/) override { }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != POINT_REACH_SYLVANAS || _done)
            return;

        _done = true;
        DoCastSelf(SPELL_SACRIFICE, true);
        me->DespawnOrUnsummon(1s);
    }

private:
    bool _done;
};
}
}

void AddSC_end_time()
{
    using namespace EndTime;
    RegisterEndTimeCreatureAI(npc_end_time_nozdormu);
    RegisterGameObjectAI(go_end_time_time_transit_device);
    RegisterGameObjectAI(go_end_time_fragment_of_jainas_staff);

    // Azimov : Écho de Baine
    using namespace EndTime::EchoOfBaine;
    RegisterEndTimeCreatureAI(boss_echo_of_baine);
    RegisterEndTimeCreatureAI(npc_echo_of_baine_baines_totem);

    // Azimov : Écho de Tyrande
    using namespace EndTime::EchoOfTyrande;
    RegisterEndTimeCreatureAI(boss_echo_of_tyrande);
    RegisterEndTimeCreatureAI(npc_echo_of_tyrande_moonlance);

    // Azimov : Écho de Sylvanas
    using namespace EndTime::EchoOfSylvanas;
    RegisterEndTimeCreatureAI(boss_echo_of_sylvanas);
    RegisterEndTimeCreatureAI(npc_echo_of_sylvanas_risen_ghoul);
}
