/*
 * Azimov - Dragon Soul : Seigneur de guerre Zon'ozz (Gueule de Go'rath)
 *
 * IDs de sorts et positions repris d'AshamaneCore (GPL), réécrit pour l'API du fork :
 *  - Colère focalisée : buff empilable toutes les ~6 s
 *  - Drain psychique : cône frontal sur le tank, soigne Zon'ozz
 *  - Ombres perturbatrices : 3 (10) / 8 (25) joueurs hors tank ; une dissipation fait exploser l'ombre
 *  - Vide de l'anéantissement : la sphère part en ligne droite vers un joueur. Chaque fois qu'elle
 *    touche un joueur, les dégâts sont partagés entre les joueurs proches, elle gagne un stack et
 *    repart en sens inverse. Si elle revient sur Zon'ozz, il prend autant de stacks de Diffusion
 *    du Vide (+dégâts subis) et lance la phase Sang noir de Go'rath. Si elle sort de la salle :
 *    Éruption de sang noir sur tout le raid.
 *  - Sang noir de Go'rath (30 s) : Colère, Ténèbres, tentacules (Yeux ; + Fléaux et Griffes en
 *    héroïque, chaque tentacule vivante ajoute un stack de Sang noir aux joueurs)
 *  - Berserk à 6 min
 *  - Il parle en shath'yar et chuchote la traduction à chaque joueur, comme sur retail
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "CreatureTextMgr.h"
#include "Containers.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum ZonozzSpells
{
    SPELL_ZONOZZ_BERSERK                = 26662,
    SPELL_FOCUSED_ANGER                 = 104543,
    SPELL_PSYCHIC_DRAIN                 = 104323,
    SPELL_DISRUPTING_SHADOWS            = 103434,
    SPELL_DISRUPTING_SHADOWS_DMG        = 103948, // à la dissipation
    SPELL_VOID_OF_THE_UNMAKING_SUMMON   = 103571,
    SPELL_VOID_OF_THE_UNMAKING_VISUAL   = 109187,
    SPELL_VOID_OF_THE_UNMAKING_DUMMY    = 103946, // rayon de Zon'ozz vers la sphère
    SPELL_VOID_DIFFUSION_DMG            = 103527,
    SPELL_VOID_DIFFUSION_BUFF           = 106836, // stacks de la sphère
    SPELL_VOID_DIFFUSION_DEBUFF         = 104031, // stacks sur Zon'ozz
    SPELL_BLACK_BLOOD_ERUPTION          = 108799,
    SPELL_TANTRUM                       = 103953,
    SPELL_DARKNESS                      = 109413,
    SPELL_BLACK_BLOOD_OF_GORATH_10      = 104377, // héroïque : un stack par tentacule
    SPELL_BLACK_BLOOD_OF_GORATH_25      = 110306,
    SPELL_BLACK_BLOOD_OF_GORATH_SELF    = 104378, // normal

    // tentacules
    SPELL_SHADOW_GAZE                   = 104347, // Œil
    SPELL_SLUDGE_SPEW                   = 110297, // Fléau
    SPELL_WILD_FLAIL                    = 109199, // Fléau
    SPELL_OOZE_SPIT                     = 109396  // Griffe
};

enum ZonozzNpcs
{
    NPC_VOID_OF_THE_UNMAKING            = 55334,
    NPC_EYE_OF_GORATH                   = 55416,
    NPC_FLAIL_OF_GORATH                 = 55417,
    NPC_CLAW_OF_GORATH                  = 55418,

    // trash de la Gueule de Go'rath (mêmes sorts que les adds du combat)
    NPC_EYE_OF_GORATH_TRASH             = 57875,
    NPC_FLAIL_OF_GORATH_TRASH           = 57877,
    NPC_CLAW_OF_GORATH_TRASH            = 57890
};

enum ZonozzEvents
{
    EVENT_Z_BERSERK = 1,
    EVENT_Z_FOCUSED_ANGER,
    EVENT_Z_PSYCHIC_DRAIN,
    EVENT_Z_DISRUPTING_SHADOWS,
    EVENT_Z_VOID,
    EVENT_Z_TANTRUM_MOVE,
    EVENT_Z_TANTRUM,
    EVENT_Z_TANTRUM_FIGHT,
    EVENT_Z_TANTRUM_END,

    // sphère
    EVENT_V_LAUNCH,
    EVENT_V_CHECK,

    // tentacules
    EVENT_T_SHADOW_GAZE,
    EVENT_T_SLUDGE_SPEW,
    EVENT_T_WILD_FLAIL,
    EVENT_T_OOZE_SPIT
};

enum ZonozzMisc
{
    DATA_VOID_RETURNED  = 1
};

// Chaque réplique : un cri en shath'yar + un chuchotement de traduction (groupe + 1)
enum ZonozzTexts
{
    SAY_Z_INTRO     = 0,
    SAY_Z_AGGRO     = 2,
    SAY_Z_DEATH     = 4,
    SAY_Z_VOID      = 6,
    SAY_Z_SHADOWS   = 8,
    SAY_Z_KILL_1    = 10, // 10, 12, 14
    SAY_Z_BLOOD_1   = 16  // 16, 18, 20
};

static Position const ZonozzCenter = { -1769.329956f, -1916.869995f, -226.28f, 0.0f };
constexpr float ZONOZZ_ROOM_RADIUS = 95.0f;

static Position const TentaclePos[14] =
{
    // Yeux de Go'rath (bord de la salle)
    { -1702.57f, -1884.71f, -221.513f, 3.63029f }, { -1801.84f, -1851.69f, -221.436f, 5.2709f  },
    { -1792.20f, -1988.63f, -221.373f, 1.41372f }, { -1834.55f, -1952.28f, -221.380f, 0.628318f },
    { -1734.35f, -1983.18f, -221.445f, 2.14675f }, { -1745.46f, -1847.31f, -221.437f, 4.43314f },
    { -1839.37f, -1895.09f, -221.381f, 5.98648f }, { -1696.95f, -1941.09f, -221.292f, 1.90241f },
    // Fléaux de Go'rath (héroïque)
    { -1739.25f, -1885.62f, -226.28f, 4.44f }, { -1791.32f, -1885.34f, -226.06f, 4.94f },
    { -1801.41f, -1939.77f, -226.13f, 0.84f }, { -1759.52f, -1957.95f, -226.00f, 1.67f },
    // Griffes de Go'rath (héroïque)
    { -1775.00f, -1937.96f, -226.35f, 1.30f }, { -1748.31f, -1901.35f, -226.17f, 3.87f }
};

// Le sol de la salle est une cuvette : plat au centre, remonte vers les bords
static float BowlHeight(float x, float y)
{
    float d = ZonozzCenter.GetExactDist2d(x, y);
    return d > 40.0f ? -225.0f + (d - 40.0f) * 0.1333f : -225.0f;
}

// cri en shath'yar + traduction chuchotée à chaque joueur de l'instance
static void ZonozzSpeak(Creature* me, uint8 group)
{
    sCreatureTextMgr->SendChat(me, group, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
    Map::PlayerList const& players = me->GetMap()->GetPlayers();
    for (auto itr = players.begin(); itr != players.end(); ++itr)
        if (Player* player = itr->GetSource())
            sCreatureTextMgr->SendChat(me, group + 1, player, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_NORMAL);
}

static uint32 BlackBloodSpell(Unit const* unit)
{
    return unit->GetMap()->Is25ManRaid() ? SPELL_BLACK_BLOOD_OF_GORATH_25 : SPELL_BLACK_BLOOD_OF_GORATH_10;
}

// ---------------------------------------------------------------------------
// Zon'ozz
// ---------------------------------------------------------------------------
struct boss_warlord_zonozz : public BossAI
{
    boss_warlord_zonozz(Creature* creature) : BossAI(creature, DATA_WARLORD_ZONOZZ), _introDone(false) { }

    void Reset() override
    {
        _Reset();
        me->SetReactState(REACT_AGGRESSIVE);
        me->RemoveAurasDueToSpell(SPELL_VOID_DIFFUSION_DEBUFF);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_10);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_25);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_introDone && who->IsPlayer() && me->IsWithinDistInMap(who, 70.0f))
        {
            _introDone = true;
            ZonozzSpeak(me, SAY_Z_INTRO);
        }
        BossAI::MoveInLineOfSight(who);
    }

    void JustEngagedWith(Unit* who) override
    {
        if (instance->GetBossState(DATA_MORCHOK) != DONE)
        {
            EnterEvadeMode(EVADE_REASON_SEQUENCE_BREAK);
            return;
        }

        _JustEngagedWith(who);
        ZonozzSpeak(me, SAY_Z_AGGRO);
        DoZoneInCombat();

        events.ScheduleEvent(EVENT_Z_VOID, 5500ms);
        events.ScheduleEvent(EVENT_Z_FOCUSED_ANGER, 10500ms);
        events.ScheduleEvent(EVENT_Z_PSYCHIC_DRAIN, 13s);
        events.ScheduleEvent(EVENT_Z_DISRUPTING_SHADOWS, 25s, 30s);
        events.ScheduleEvent(EVENT_Z_BERSERK, 6min);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            ZonozzSpeak(me, uint8(SAY_Z_KILL_1 + 2 * urand(0, 2)));
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        ZonozzSpeak(me, SAY_Z_DEATH);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_10);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_25);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_10);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLACK_BLOOD_OF_GORATH_25);
        summons.DespawnAll();
        BossAI::EnterEvadeMode(why);
    }

    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);

        switch (summon->GetEntry())
        {
            case NPC_VOID_OF_THE_UNMAKING:
                DoCast(summon, SPELL_VOID_OF_THE_UNMAKING_DUMMY, true);
                break;
            case NPC_EYE_OF_GORATH:
            case NPC_FLAIL_OF_GORATH:
            case NPC_CLAW_OF_GORATH:
                // héroïque : chaque tentacule vivante = un stack de Sang noir sur le raid
                if (IsHeroic())
                    DoCastAOE(BlackBloodSpell(me), true);
                DoZoneInCombat(summon);
                break;
            default:
                break;
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* killer) override
    {
        BossAI::SummonedCreatureDies(summon, killer);

        if (!IsHeroic())
            return;

        switch (summon->GetEntry())
        {
            case NPC_EYE_OF_GORATH:
            case NPC_FLAIL_OF_GORATH:
            case NPC_CLAW_OF_GORATH:
            {
                // une tentacule en moins = un stack en moins
                uint32 spellId = BlackBloodSpell(me);
                Map::PlayerList const& players = me->GetMap()->GetPlayers();
                for (auto itr = players.begin(); itr != players.end(); ++itr)
                    if (Player* player = itr->GetSource())
                        if (Aura* aura = player->GetAura(spellId))
                            aura->ModStackAmount(-1);
                break;
            }
            default:
                break;
        }
    }

    // la sphère est revenue sur Zon'ozz avec `stacks` stacks
    void SetData(uint32 type, uint32 stacks) override
    {
        if (type != DATA_VOID_RETURNED || _inTantrum)
            return;

        _inTantrum = true;
        CastSpellExtraArgs args(true);
        args.AddSpellMod(SPELLVALUE_AURA_STACK, int32(std::max<uint32>(1, stacks)));
        me->CastSpell(me, SPELL_VOID_DIFFUSION_DEBUFF, args);

        me->RemoveAurasDueToSpell(SPELL_FOCUSED_ANGER);
        events.CancelEvent(EVENT_Z_DISRUPTING_SHADOWS);
        events.CancelEvent(EVENT_Z_PSYCHIC_DRAIN);
        events.CancelEvent(EVENT_Z_FOCUSED_ANGER);
        events.CancelEvent(EVENT_Z_VOID);
        events.ScheduleEvent(EVENT_Z_TANTRUM_MOVE, 1500ms);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (me->GetDistance(me->GetHomePosition()) > 150.0f)
        {
            EnterEvadeMode(EVADE_REASON_BOUNDARY);
            return;
        }

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_Z_BERSERK:
                    DoCastSelf(SPELL_ZONOZZ_BERSERK, true);
                    break;
                case EVENT_Z_FOCUSED_ANGER:
                    DoCastSelf(SPELL_FOCUSED_ANGER, true);
                    events.Repeat(6500ms);
                    break;
                case EVENT_Z_PSYCHIC_DRAIN:
                    DoCastVictim(SPELL_PSYCHIC_DRAIN);
                    events.Repeat(20s, 25s);
                    break;
                case EVENT_Z_DISRUPTING_SHADOWS:
                {
                    ZonozzSpeak(me, SAY_Z_SHADOWS);
                    CastSpellExtraArgs args;
                    args.AddSpellMod(SPELLVALUE_MAX_TARGETS, Is25ManRaid() ? 8 : 3);
                    me->CastSpell(me, SPELL_DISRUPTING_SHADOWS, args);
                    events.Repeat(25s, 30s);
                    break;
                }
                case EVENT_Z_VOID:
                    summons.DespawnEntry(NPC_VOID_OF_THE_UNMAKING);
                    ZonozzSpeak(me, SAY_Z_VOID);
                    DoCastSelf(SPELL_VOID_OF_THE_UNMAKING_SUMMON);
                    events.Repeat(90s);
                    break;
                case EVENT_Z_TANTRUM_MOVE:
                    me->SetReactState(REACT_PASSIVE);
                    me->AttackStop();
                    me->NearTeleportTo(ZonozzCenter.GetPositionX(), ZonozzCenter.GetPositionY(), ZonozzCenter.GetPositionZ(), me->GetOrientation());
                    events.ScheduleEvent(EVENT_Z_TANTRUM, 3s);
                    break;
                case EVENT_Z_TANTRUM:
                    ZonozzSpeak(me, uint8(SAY_Z_BLOOD_1 + 2 * urand(0, 2)));
                    DoCastSelf(SPELL_DARKNESS, true);
                    if (!IsHeroic())
                        DoCastSelf(SPELL_BLACK_BLOOD_OF_GORATH_SELF, true);
                    DoCastSelf(SPELL_TANTRUM);
                    SpawnTentacles();
                    events.ScheduleEvent(EVENT_Z_TANTRUM_FIGHT, 11s);
                    events.ScheduleEvent(EVENT_Z_TANTRUM_END, 30s);
                    break;
                case EVENT_Z_TANTRUM_FIGHT:
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (Unit* victim = me->GetVictim())
                        AttackStart(victim);
                    break;
                case EVENT_Z_TANTRUM_END:
                    _inTantrum = false;
                    // en normal les Yeux disparaissent à la fin de la phase
                    if (!IsHeroic())
                        summons.DespawnEntry(NPC_EYE_OF_GORATH);
                    events.ScheduleEvent(EVENT_Z_VOID, 13s, 14s);
                    events.ScheduleEvent(EVENT_Z_FOCUSED_ANGER, 6s);
                    events.ScheduleEvent(EVENT_Z_DISRUPTING_SHADOWS, 6s);
                    events.ScheduleEvent(EVENT_Z_PSYCHIC_DRAIN, 21s);
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
    void SpawnTentacles()
    {
        uint32 eyes   = Is25ManRaid() ? 8 : 4;
        uint32 flails = IsHeroic() ? (Is25ManRaid() ? 4 : 2) : 0;
        uint32 claws  = IsHeroic() ? (Is25ManRaid() ? 2 : 1) : 0;

        for (uint32 i = 0; i < eyes; ++i)
            me->SummonCreature(NPC_EYE_OF_GORATH, TentaclePos[i], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);
        for (uint32 i = 0; i < flails; ++i)
            me->SummonCreature(NPC_FLAIL_OF_GORATH, TentaclePos[8 + i], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);
        for (uint32 i = 0; i < claws; ++i)
            me->SummonCreature(NPC_CLAW_OF_GORATH, TentaclePos[12 + i], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);
    }

    bool _introDone;
    bool _inTantrum = false;
};

// ---------------------------------------------------------------------------
// Vide de l'anéantissement : sphère qui fait du ping-pong entre les joueurs et Zon'ozz
// ---------------------------------------------------------------------------
struct npc_zonozz_void_of_the_unmaking : public ScriptedAI
{
    npc_zonozz_void_of_the_unmaking(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
    }

    void IsSummonedBy(Unit* summoner) override
    {
        _summonerGUID = summoner->GetGUID();
        me->SetDisableGravity(true);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        _events.ScheduleEvent(EVENT_V_LAUNCH, 5s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_V_LAUNCH:
                {
                    DoCastSelf(SPELL_VOID_OF_THE_UNMAKING_VISUAL, true);
                    // part vers un joueur au hasard, puis continue tout droit
                    std::list<Player*> players;
                    me->GetPlayerListInGrid(players, 150.0f);
                    players.remove_if([](Player* p) { return !p->IsAlive() || p->IsGameMaster(); });
                    float angle = me->GetOrientation();
                    if (!players.empty())
                        angle = me->GetAngle(Trinity::Containers::SelectRandomContainerElement(players));
                    MoveStraight(angle);
                    _events.ScheduleEvent(EVENT_V_CHECK, 250ms);
                    break;
                }
                case EVENT_V_CHECK:
                    if (CheckPosition())
                        _events.Repeat(250ms);
                    break;
                default:
                    break;
            }
        }

        if (_bounceCooldown > diff)
            _bounceCooldown -= diff;
        else
            _bounceCooldown = 0;
    }

private:
    // retourne false si la sphère a fini son trajet
    bool CheckPosition()
    {
        // sortie de la salle : éruption sur tout le raid
        if (ZonozzCenter.GetExactDist2d(me) > ZONOZZ_ROOM_RADIUS)
        {
            me->StopMoving();
            DoCastAOE(SPELL_BLACK_BLOOD_ERUPTION, true);
            me->DespawnOrUnsummon(500ms);
            return false;
        }

        if (_bounceCooldown)
            return true;

        // retour sur Zon'ozz après au moins un rebond
        if (_bounced)
            if (Creature* zonozz = ObjectAccessor::GetCreature(*me, _summonerGUID))
                if (zonozz->IsAlive() && me->GetExactDist2d(zonozz) < 4.0f)
                {
                    uint32 stacks = me->GetAuraCount(SPELL_VOID_DIFFUSION_BUFF);
                    zonozz->AI()->SetData(DATA_VOID_RETURNED, stacks);
                    me->StopMoving();
                    me->DespawnOrUnsummon(500ms);
                    return false;
                }

        // un joueur bloque la sphère : dégâts partagés, +1 stack, demi-tour
        if (Player* player = me->SelectNearestPlayer(3.0f))
        {
            if (player->IsAlive() && !player->IsGameMaster())
            {
                DoCastAOE(SPELL_VOID_DIFFUSION_DMG, true);
                DoCastSelf(SPELL_VOID_DIFFUSION_BUFF, true);
                _bounced = true;
                _bounceCooldown = 1500;
                MoveStraight(Position::NormalizeOrientation(_angle + float(M_PI)));
            }
        }
        return true;
    }

    void MoveStraight(float angle)
    {
        _angle = angle;
        float x = me->GetPositionX() + 200.0f * std::cos(angle);
        float y = me->GetPositionY() + 200.0f * std::sin(angle);
        float z = BowlHeight(x, y) + 2.0f;
        me->SetFacingTo(angle);
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MovePoint(1, x, y, z, false, 5.0f);
    }

    EventMap _events;
    ObjectGuid _summonerGUID;
    float _angle = 0.0f;
    bool _bounced = false;
    uint32 _bounceCooldown = 0;
};

// ---------------------------------------------------------------------------
// Tentacules de Go'rath (Œil, Fléau, Griffe) : immobiles
// ---------------------------------------------------------------------------
struct npc_zonozz_tentacle : public ScriptedAI
{
    npc_zonozz_tentacle(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void Reset() override
    {
        _events.Reset();
    }

    // le trash utilise les mêmes sorts que les tentacules du combat
    static uint32 Role(uint32 entry)
    {
        switch (entry)
        {
            case NPC_EYE_OF_GORATH_TRASH:   return NPC_EYE_OF_GORATH;
            case NPC_FLAIL_OF_GORATH_TRASH: return NPC_FLAIL_OF_GORATH;
            case NPC_CLAW_OF_GORATH_TRASH:  return NPC_CLAW_OF_GORATH;
            default:                        return entry;
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        switch (Role(me->GetEntry()))
        {
            case NPC_EYE_OF_GORATH:
                _events.ScheduleEvent(EVENT_T_SHADOW_GAZE, 3s, 8s);
                break;
            case NPC_FLAIL_OF_GORATH:
                _events.ScheduleEvent(EVENT_T_SLUDGE_SPEW, 10s, 15s);
                _events.ScheduleEvent(EVENT_T_WILD_FLAIL, 15s);
                break;
            case NPC_CLAW_OF_GORATH:
                _events.ScheduleEvent(EVENT_T_OOZE_SPIT, 8s);
                break;
            default:
                break;
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
                case EVENT_T_SHADOW_GAZE:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                        DoCast(target, SPELL_SHADOW_GAZE);
                    _events.Repeat(8s, 15s);
                    break;
                case EVENT_T_SLUDGE_SPEW:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                        DoCast(target, SPELL_SLUDGE_SPEW);
                    _events.Repeat(12s, 20s);
                    break;
                case EVENT_T_WILD_FLAIL:
                    DoCastAOE(SPELL_WILD_FLAIL);
                    _events.Repeat(7s, 10s);
                    break;
                case EVENT_T_OOZE_SPIT:
                    // crache seulement si personne n'est au corps à corps
                    if (!me->IsWithinMeleeRange(me->GetVictim()))
                        if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                            DoCast(target, SPELL_OOZE_SPIT);
                    _events.Repeat(6s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (Role(me->GetEntry()) != NPC_EYE_OF_GORATH)
            DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
};

// ---------------------------------------------------------------------------
// 103434 - Ombres perturbatrices : jamais sur le tank
// ---------------------------------------------------------------------------
class spell_zonozz_disrupting_shadows : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (targets.size() <= 1)
            return;

        if (Creature* caster = GetCaster()->ToCreature())
            if (Unit* tank = caster->GetVictim())
                targets.remove(tank);

        Trinity::Containers::RandomResize(targets, GetCaster()->GetMap()->Is25ManRaid() ? 8 : 3);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_zonozz_disrupting_shadows::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// 103434 (aura) - dissipée : l'ombre explose sur le joueur (dégâts + projection)
class spell_zonozz_disrupting_shadows_aura : public AuraScript
{
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode().HasFlag(AuraRemoveFlags::ByEnemySpell))
            GetTarget()->CastSpell(GetTarget(), SPELL_DISRUPTING_SHADOWS_DMG, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_zonozz_disrupting_shadows_aura::HandleRemove, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

// 103527 - Diffusion du Vide : dégâts partagés entre les joueurs touchés
class spell_zonozz_void_diffusion_dmg : public SpellScript
{
    void CountTargets(std::list<WorldObject*>& targets)
    {
        _targetCount = uint32(targets.size());
    }

    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        if (_targetCount > 1)
            SetHitDamage(GetHitDamage() / int32(_targetCount));
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_zonozz_void_diffusion_dmg::CountTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget.Register(&spell_zonozz_void_diffusion_dmg::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }

    uint32 _targetCount = 0;
};
}

void AddSC_boss_warlord_zonozz()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(boss_warlord_zonozz);
    RegisterDragonSoulCreatureAI(npc_zonozz_void_of_the_unmaking);
    RegisterDragonSoulCreatureAI(npc_zonozz_tentacle);
    RegisterSpellScript(spell_zonozz_disrupting_shadows);
    RegisterSpellScript(spell_zonozz_disrupting_shadows_aura);
    RegisterSpellScript(spell_zonozz_void_diffusion_dmg);
}