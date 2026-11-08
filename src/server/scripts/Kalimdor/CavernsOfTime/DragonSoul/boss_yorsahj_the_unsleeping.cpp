/*
 * Azimov - Dragon Soul : Yor'sahj l'Insomniaque (Gueule de Shu'ma)
 *
 * IDs de sorts et positions repris d'AshamaneCore (GPL), réécrit pour l'API du fork.
 *  - Trait du Vide sur le tank (toutes les ~9 s)
 *  - Appel du sang de Shu'ma (~22 s puis toutes les ~62 s) : il va au centre et invoque
 *    3 globules (4 en héroïque) selon une des 6 combinaisons de couleurs de retail.
 *    Les globules rampent vers lui ; chaque globule qui l'atteint lui donne son pouvoir,
 *    ceux qu'on tue avant ne comptent pas. Le combat reprend quand il n'en reste plus.
 *      Cramoisi : Sang brûlant (3 / 8 joueurs)
 *      Acide    : Acide digestif (rebonds sur 4 / 8 joueurs hors tank)
 *      Lumineux : accélère tous les autres pouvoirs, Trait du Vide devient une AoE
 *      Noir     : Sbires corrompus (Oubliés)
 *      Ombreux  : Corruption profonde (explose à 5 stacks)
 *      Cobalt   : Vide de mana (draine le mana, le rend quand on le tue)
 *  - Berserk à 10 min
 *  - Il parle en shath'yar et chuchote la traduction à chaque joueur, comme sur retail
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "InstanceScript.h"
#include "CreatureTextMgr.h"
#include "Containers.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "dragon_soul.h"
#include <set>

namespace DragonSoul
{
enum YorsahjSpells
{
    SPELL_YORSAHJ_BERSERK           = 26662,
    SPELL_VOID_BOLT                 = 104849,
    SPELL_VOID_BOLT_AOE             = 105416,
    SPELL_SEARING_BLOOD             = 105033,
    SPELL_DEEP_CORRUPTION           = 105171,
    SPELL_DEEP_CORRUPTION_AURA      = 103628,
    SPELL_DEEP_CORRUPTION_DMG       = 105173,
    SPELL_DIGESTIVE_ACID            = 105031,
    SPELL_DIGESTIVE_ACID_DUMMY      = 105562,
    SPELL_DIGESTIVE_ACID_DMG        = 105573,
    SPELL_MANA_VOID_SUMMON          = 105034,
    SPELL_MANA_VOID                 = 105530,
    SPELL_MANA_DIFFUSION            = 108228,
    SPELL_CORRUPTED_MINIONS_AURA    = 105636,
    SPELL_PSYCHIC_SLICE             = 105671,

    SPELL_CRIMSON_BLOOD_OF_SHUMA    = 104897,
    SPELL_ACIDIC_BLOOD_OF_SHUMA     = 104898,
    SPELL_GLOWING_BLOOD_OF_SHUMA    = 104901,
    SPELL_BLACK_BLOOD_OF_SHUMA      = 104894,
    SPELL_SHADOWED_BLOOD_OF_SHUMA   = 104896,
    SPELL_COBALT_BLOOD_OF_SHUMA     = 105027,

    SPELL_FUSING_VAPORS             = 103968,
    SPELL_FUSING_VAPORS_IMMUNE      = 105904,

    SPELL_COLOR_COMBINATION_1       = 105420, // ombreux, acide, cobalt (+ noir)
    SPELL_COLOR_COMBINATION_2       = 105435, // acide, cramoisi, noir (+ cobalt)
    SPELL_COLOR_COMBINATION_3       = 105436, // acide, lumineux, cramoisi (+ noir)
    SPELL_COLOR_COMBINATION_4       = 105437, // cobalt, ombreux, lumineux (+ acide)
    SPELL_COLOR_COMBINATION_5       = 105439, // cobalt, noir, lumineux (+ ombreux)
    SPELL_COLOR_COMBINATION_6       = 105440, // ombreux, cramoisi, noir (+ lumineux)

    SPELL_AURA_TALL_BLUE            = 105473,
    SPELL_AURA_TALL_RED             = 105474,
    SPELL_AURA_TALL_GREEN           = 105475,
    SPELL_AURA_TALL_YELLOW          = 105476,
    SPELL_AURA_TALL_PURPLE          = 105477,
    SPELL_AURA_TALL_BLACK           = 105478,

    SPELL_SPAWNING_POOL_1           = 105600,
    SPELL_SPAWNING_POOL_2           = 105601,
    SPELL_SPAWNING_POOL_3           = 105603
};

enum YorsahjNpcs
{
    NPC_MAW_OF_SHUMA        = 55544,
    NPC_MANA_VOID           = 56231,
    NPC_FORGOTTEN_ONE       = 56265,
    NPC_ACIDIC_GLOBULE      = 55862,
    NPC_SHADOWED_GLOBULE    = 55863,
    NPC_GLOWING_GLOBULE     = 55864,
    NPC_CRIMSON_GLOBULE     = 55865,
    NPC_COBALT_GLOBULE      = 55866,
    NPC_DARK_GLOBULE        = 55867
};

enum YorsahjEvents
{
    EVENT_Y_BERSERK = 1,
    EVENT_Y_VOID_BOLT,
    EVENT_Y_CALL_BLOOD,
    EVENT_Y_SUMMON_GLOBULES,
    EVENT_Y_SEARING_BLOOD,
    EVENT_Y_DIGESTIVE_ACID,
    EVENT_Y_CORRUPTED_MINIONS,
    EVENT_Y_DEEP_CORRUPTION,
    EVENT_Y_MANA_VOID,
    EVENT_Y_MANA_VOID_SUMMON,
    EVENT_Y_BLOOD_TIMEOUT,

    EVENT_G_MOVE,
    EVENT_G_CHECK,

    EVENT_F_ATTACK,
    EVENT_F_PSYCHIC_SLICE
};

enum YorsahjMisc
{
    GUID_GLOBULE_ARRIVED = 1
};

// Cri en shath'yar + chuchotement de la traduction (groupe + 1)
enum YorsahjTexts
{
    SAY_Y_INTRO     = 0,
    SAY_Y_AGGRO     = 2,
    SAY_Y_DEATH     = 4,
    SAY_Y_KILL_1    = 6,  // 6, 8
    SAY_Y_BLOOD_1   = 10  // 10, 12, 14, 16
};

static Position const GlobulePos[6] =
{
    { -1662.959961f, -2992.280029f, -173.52f, 3.63f }, // cramoisi
    { -1723.760010f, -2935.330078f, -174.03f, 4.32f }, // acide
    { -1863.989990f, -2993.090088f, -174.12f, 5.86f }, // lumineux
    { -1808.229980f, -3136.739990f, -173.48f, 1.09f }, // noir
    { -1663.890015f, -3077.129883f, -174.48f, 2.72f }, // ombreux
    { -1722.599976f, -3137.159912f, -173.39f, 1.93f }  // cobalt
};

static Position const MawOfShumaPos = { -1762.56f, -3036.65f, -116.44f, 0.0f };
static Position const YorsahjCenter = { -1765.66f, -3034.35f, -182.38f, 3.52f };

static void YorsahjSpeak(Creature* me, uint8 group)
{
    sCreatureTextMgr->SendChat(me, group, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
    Map::PlayerList const& players = me->GetMap()->GetPlayers();
    for (auto itr = players.begin(); itr != players.end(); ++itr)
        if (Player* player = itr->GetSource())
            sCreatureTextMgr->SendChat(me, group + 1, player, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_NORMAL);
}

static int32 GlobuleIndex(uint32 entry)
{
    switch (entry)
    {
        case NPC_CRIMSON_GLOBULE:  return 0;
        case NPC_ACIDIC_GLOBULE:   return 1;
        case NPC_GLOWING_GLOBULE:  return 2;
        case NPC_DARK_GLOBULE:     return 3;
        case NPC_SHADOWED_GLOBULE: return 4;
        case NPC_COBALT_GLOBULE:   return 5;
        default:                   return -1;
    }
}

// ---------------------------------------------------------------------------
// Yor'sahj
// ---------------------------------------------------------------------------
struct boss_yorsahj_the_unsleeping : public BossAI
{
    boss_yorsahj_the_unsleeping(Creature* creature) : BossAI(creature, DATA_YORSAHJ_THE_UNSLEEPING), _introDone(false) { }

    void Reset() override
    {
        _Reset();
        _globules.clear();
        _inBloodPhase = false;
        _glowing = false;
        me->SetReactState(REACT_AGGRESSIVE);
        RemoveBloodAuras();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_introDone && who->IsPlayer() && me->IsWithinDistInMap(who, 70.0f))
        {
            _introDone = true;
            YorsahjSpeak(me, SAY_Y_INTRO);
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
        YorsahjSpeak(me, SAY_Y_AGGRO);
        DoZoneInCombat();

        events.ScheduleEvent(EVENT_Y_VOID_BOLT, 6s);
        events.ScheduleEvent(EVENT_Y_CALL_BLOOD, 22s);
        events.ScheduleEvent(EVENT_Y_BERSERK, 10min);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            YorsahjSpeak(me, uint8(SAY_Y_KILL_1 + 2 * urand(0, 1)));
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        YorsahjSpeak(me, SAY_Y_DEATH);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_DEEP_CORRUPTION_AURA);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        summons.DespawnAll();
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_DEEP_CORRUPTION_AURA);
        BossAI::EnterEvadeMode(why);
    }

    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);

        if (GlobuleIndex(summon->GetEntry()) >= 0)
            _globules.insert(summon->GetGUID());
        else if (summon->GetEntry() == NPC_MANA_VOID)
            summon->GetMotionMaster()->MoveRandom(25.0f);
        else if (summon->GetEntry() == NPC_FORGOTTEN_ONE)
            DoZoneInCombat(summon);
    }

    void SummonedCreatureDies(Creature* summon, Unit* killer) override
    {
        BossAI::SummonedCreatureDies(summon, killer);

        // un globule tué avant d'arriver ne donne rien
        if (_globules.erase(summon->GetGUID()))
            TryEndBloodPhase();
    }

    // un globule a atteint Yor'sahj : il absorbe son pouvoir
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id != GUID_GLOBULE_ARRIVED || !_globules.erase(guid))
            return;

        if (Creature* globule = ObjectAccessor::GetCreature(*me, guid))
        {
            Absorb(globule->GetEntry());
            globule->DespawnOrUnsummon();
        }
        TryEndBloodPhase();
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
                case EVENT_Y_BERSERK:
                    DoCastSelf(SPELL_YORSAHJ_BERSERK, true);
                    break;
                case EVENT_Y_VOID_BOLT:
                    if (_glowing)
                        DoCastAOE(SPELL_VOID_BOLT_AOE);
                    else
                        DoCastVictim(SPELL_VOID_BOLT);
                    events.Repeat(_glowing ? 5500ms : 9s);
                    break;
                case EVENT_Y_CALL_BLOOD:
                    StartBloodPhase();
                    break;
                case EVENT_Y_SUMMON_GLOBULES:
                    SummonGlobules();
                    break;
                case EVENT_Y_BLOOD_TIMEOUT:
                    // sécurité : si un globule reste coincé, il est absorbé d'office
                    for (ObjectGuid const& guid : std::set<ObjectGuid>(_globules))
                        SetGUID(guid, GUID_GLOBULE_ARRIVED);
                    break;
                case EVENT_Y_SEARING_BLOOD:
                {
                    CastSpellExtraArgs args;
                    args.AddSpellMod(SPELLVALUE_MAX_TARGETS, Is25ManRaid() ? 8 : 3);
                    me->CastSpell(me, SPELL_SEARING_BLOOD, args);
                    events.Repeat(_glowing ? 3500ms : 6s);
                    break;
                }
                case EVENT_Y_DIGESTIVE_ACID:
                    DoCastSelf(SPELL_DIGESTIVE_ACID);
                    events.Repeat(_glowing ? 3500ms : 8300ms);
                    break;
                case EVENT_Y_CORRUPTED_MINIONS:
                    DoCastSelf(SPELL_SPAWNING_POOL_1, true);
                    DoCastSelf(SPELL_SPAWNING_POOL_2, true);
                    DoCastSelf(SPELL_SPAWNING_POOL_3, true);
                    DoCastSelf(SPELL_CORRUPTED_MINIONS_AURA);
                    // avec le lumineux, une seconde vague
                    if (_glowing && !_secondMinionWave)
                    {
                        _secondMinionWave = true;
                        events.Repeat(30s);
                    }
                    break;
                case EVENT_Y_DEEP_CORRUPTION:
                    DoCastAOE(SPELL_DEEP_CORRUPTION);
                    events.Repeat(25s);
                    break;
                case EVENT_Y_MANA_VOID:
                    DoCastAOE(SPELL_MANA_VOID);
                    events.ScheduleEvent(EVENT_Y_MANA_VOID_SUMMON, 5s);
                    break;
                case EVENT_Y_MANA_VOID_SUMMON:
                    DoCastSelf(SPELL_MANA_VOID_SUMMON);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (!_inBloodPhase)
            DoMeleeAttackIfReady();
    }

private:
    void RemoveBloodAuras()
    {
        for (uint32 spellId : { uint32(SPELL_CRIMSON_BLOOD_OF_SHUMA), uint32(SPELL_ACIDIC_BLOOD_OF_SHUMA), uint32(SPELL_GLOWING_BLOOD_OF_SHUMA),
                                uint32(SPELL_BLACK_BLOOD_OF_SHUMA), uint32(SPELL_SHADOWED_BLOOD_OF_SHUMA), uint32(SPELL_COBALT_BLOOD_OF_SHUMA),
                                uint32(SPELL_SPAWNING_POOL_1), uint32(SPELL_SPAWNING_POOL_2), uint32(SPELL_SPAWNING_POOL_3) })
            me->RemoveAurasDueToSpell(spellId);
    }

    void StartBloodPhase()
    {
        for (uint32 eventId : { EVENT_Y_VOID_BOLT, EVENT_Y_SEARING_BLOOD, EVENT_Y_DIGESTIVE_ACID, EVENT_Y_CORRUPTED_MINIONS,
                                EVENT_Y_DEEP_CORRUPTION, EVENT_Y_MANA_VOID, EVENT_Y_MANA_VOID_SUMMON })
            events.CancelEvent(eventId);

        RemoveBloodAuras();
        _glowing = false;
        _secondMinionWave = false;
        _inBloodPhase = true;

        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->NearTeleportTo(YorsahjCenter.GetPositionX(), YorsahjCenter.GetPositionY(), YorsahjCenter.GetPositionZ(), YorsahjCenter.GetOrientation());
        events.ScheduleEvent(EVENT_Y_SUMMON_GLOBULES, 1s);
    }

    void SummonGlobules()
    {
        YorsahjSpeak(me, uint8(SAY_Y_BLOOD_1 + 2 * urand(0, 3)));

        uint32 spellId = 0;
        std::vector<uint32> entries;
        bool heroic = IsHeroic();
        switch (urand(0, 5))
        {
            case 0: entries = { NPC_SHADOWED_GLOBULE, NPC_ACIDIC_GLOBULE, NPC_COBALT_GLOBULE };  if (heroic) entries.push_back(NPC_DARK_GLOBULE);     spellId = SPELL_COLOR_COMBINATION_1; break;
            case 1: entries = { NPC_ACIDIC_GLOBULE, NPC_CRIMSON_GLOBULE, NPC_DARK_GLOBULE };     if (heroic) entries.push_back(NPC_COBALT_GLOBULE);   spellId = SPELL_COLOR_COMBINATION_2; break;
            case 2: entries = { NPC_ACIDIC_GLOBULE, NPC_GLOWING_GLOBULE, NPC_CRIMSON_GLOBULE };  if (heroic) entries.push_back(NPC_DARK_GLOBULE);     spellId = SPELL_COLOR_COMBINATION_3; break;
            case 3: entries = { NPC_COBALT_GLOBULE, NPC_SHADOWED_GLOBULE, NPC_GLOWING_GLOBULE }; if (heroic) entries.push_back(NPC_ACIDIC_GLOBULE);   spellId = SPELL_COLOR_COMBINATION_4; break;
            case 4: entries = { NPC_COBALT_GLOBULE, NPC_DARK_GLOBULE, NPC_GLOWING_GLOBULE };     if (heroic) entries.push_back(NPC_SHADOWED_GLOBULE); spellId = SPELL_COLOR_COMBINATION_5; break;
            default: entries = { NPC_SHADOWED_GLOBULE, NPC_CRIMSON_GLOBULE, NPC_DARK_GLOBULE };  if (heroic) entries.push_back(NPC_GLOWING_GLOBULE);  spellId = SPELL_COLOR_COMBINATION_6; break;
        }

        for (uint32 entry : entries)
            me->SummonCreature(entry, GlobulePos[GlobuleIndex(entry)], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);

        DoCastSelf(spellId); // visuel : il appelle le sang
        events.ScheduleEvent(EVENT_Y_BLOOD_TIMEOUT, 50s);
    }

    void Absorb(uint32 entry)
    {
        switch (entry)
        {
            case NPC_CRIMSON_GLOBULE:
                DoCastSelf(SPELL_CRIMSON_BLOOD_OF_SHUMA, true);
                break;
            case NPC_ACIDIC_GLOBULE:
                me->SummonCreature(NPC_MAW_OF_SHUMA, MawOfShumaPos, TEMPSUMMON_TIMED_DESPAWN, 60s);
                DoCastSelf(SPELL_ACIDIC_BLOOD_OF_SHUMA, true);
                DoCastSelf(SPELL_DIGESTIVE_ACID_DUMMY, true);
                break;
            case NPC_GLOWING_GLOBULE:
                _glowing = true;
                DoCastSelf(SPELL_GLOWING_BLOOD_OF_SHUMA, true);
                break;
            case NPC_DARK_GLOBULE:
                DoCastSelf(SPELL_BLACK_BLOOD_OF_SHUMA, true);
                break;
            case NPC_SHADOWED_GLOBULE:
                DoCastSelf(SPELL_SHADOWED_BLOOD_OF_SHUMA, true);
                break;
            case NPC_COBALT_GLOBULE:
                DoCastSelf(SPELL_COBALT_BLOOD_OF_SHUMA, true);
                break;
            default:
                break;
        }
    }

    // plus aucun globule vivant : Yor'sahj reprend le combat avec les pouvoirs absorbés
    void TryEndBloodPhase()
    {
        if (!_inBloodPhase || !_globules.empty())
            return;

        _inBloodPhase = false;
        events.CancelEvent(EVENT_Y_BLOOD_TIMEOUT);

        if (me->HasAura(SPELL_CRIMSON_BLOOD_OF_SHUMA))
            events.ScheduleEvent(EVENT_Y_SEARING_BLOOD, 5s, 7s);
        if (me->HasAura(SPELL_ACIDIC_BLOOD_OF_SHUMA))
            events.ScheduleEvent(EVENT_Y_DIGESTIVE_ACID, 7s, 9s);
        if (me->HasAura(SPELL_BLACK_BLOOD_OF_SHUMA))
            events.ScheduleEvent(EVENT_Y_CORRUPTED_MINIONS, 1s);
        if (me->HasAura(SPELL_SHADOWED_BLOOD_OF_SHUMA))
            events.ScheduleEvent(EVENT_Y_DEEP_CORRUPTION, 3s, 4s);
        if (me->HasAura(SPELL_COBALT_BLOOD_OF_SHUMA))
            events.ScheduleEvent(EVENT_Y_MANA_VOID, 2s, 3s);

        me->SetReactState(REACT_AGGRESSIVE);
        if (Unit* victim = me->GetVictim())
            AttackStart(victim);

        events.ScheduleEvent(EVENT_Y_VOID_BOLT, 6s, 7s);
        events.ScheduleEvent(EVENT_Y_CALL_BLOOD, 62s);
    }

    std::set<ObjectGuid> _globules;
    bool _introDone;
    bool _inBloodPhase = false;
    bool _glowing = false;
    bool _secondMinionWave = false;
};

// ---------------------------------------------------------------------------
// Globules : apparaissent sur leur bassin puis rampent lentement vers Yor'sahj
// ---------------------------------------------------------------------------
struct npc_yorsahj_globule : public ScriptedAI
{
    npc_yorsahj_globule(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
    }

    void IsSummonedBy(Unit* summoner) override
    {
        _yorsahjGUID = summoner->GetGUID();
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);

        uint32 aura = 0;
        switch (me->GetEntry())
        {
            case NPC_CRIMSON_GLOBULE:  aura = SPELL_AURA_TALL_RED;    break;
            case NPC_ACIDIC_GLOBULE:   aura = SPELL_AURA_TALL_GREEN;  break;
            case NPC_GLOWING_GLOBULE:  aura = SPELL_AURA_TALL_YELLOW; break;
            case NPC_DARK_GLOBULE:     aura = SPELL_AURA_TALL_BLACK;  break;
            case NPC_SHADOWED_GLOBULE: aura = SPELL_AURA_TALL_PURPLE; break;
            case NPC_COBALT_GLOBULE:   aura = SPELL_AURA_TALL_BLUE;   break;
            default: break;
        }
        if (aura)
            DoCastSelf(aura, true);

        DoZoneInCombat();
        _events.ScheduleEvent(EVENT_G_MOVE, 3s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoCastAOE(SPELL_FUSING_VAPORS_IMMUNE, true);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_G_MOVE:
                    me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                    DoCastSelf(SPELL_FUSING_VAPORS, true);
                    me->SetSpeedRate(MOVE_RUN, 0.47142876f);
                    me->SetSpeedRate(MOVE_WALK, 0.47142876f);
                    if (Creature* yorsahj = ObjectAccessor::GetCreature(*me, _yorsahjGUID))
                        me->GetMotionMaster()->MoveFollow(yorsahj, 0.0f, 0.0f);
                    _events.ScheduleEvent(EVENT_G_CHECK, 500ms);
                    break;
                case EVENT_G_CHECK:
                    if (Creature* yorsahj = ObjectAccessor::GetCreature(*me, _yorsahjGUID))
                    {
                        if (me->GetExactDist2d(yorsahj) <= 4.0f)
                        {
                            yorsahj->AI()->SetGUID(me->GetGUID(), GUID_GLOBULE_ARRIVED);
                            break;
                        }
                    }
                    _events.Repeat(500ms);
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _yorsahjGUID;
};

// ---------------------------------------------------------------------------
// Oublié (Sbires corrompus) : fixe un joueur au hasard
// ---------------------------------------------------------------------------
struct npc_yorsahj_forgotten_one : public ScriptedAI
{
    npc_yorsahj_forgotten_one(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        _events.ScheduleEvent(EVENT_F_ATTACK, 1s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(3s);
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
                case EVENT_F_ATTACK:
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                    {
                        AddThreat(target, 1000000.0f);
                        AttackStart(target);
                    }
                    _events.ScheduleEvent(EVENT_F_PSYCHIC_SLICE, 6s, 20s);
                    break;
                case EVENT_F_PSYCHIC_SLICE:
                    DoCastVictim(SPELL_PSYCHIC_SLICE);
                    _events.Repeat(15s, 20s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
};

// ---------------------------------------------------------------------------
// Vide de mana : quand on le tue, il rend le mana aspiré
// ---------------------------------------------------------------------------
struct npc_yorsahj_mana_void : public ScriptedAI
{
    npc_yorsahj_mana_void(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoCastAOE(SPELL_MANA_DIFFUSION, true);
        me->DespawnOrUnsummon(1s);
    }

    void UpdateAI(uint32 /*diff*/) override { }
};

// ---------------------------------------------------------------------------
// 103628 - Corruption profonde : à 5 stacks, explosion et retrait
// ---------------------------------------------------------------------------
class spell_yorsahj_deep_corruption : public AuraScript
{
    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        if (GetStackAmount() >= 5)
        {
            GetTarget()->CastSpell(GetTarget(), SPELL_DEEP_CORRUPTION_DMG, true);
            Remove();
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_yorsahj_deep_corruption::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 105171 - quand l'aura de Corruption profonde se termine, les stacks partent avec
class spell_yorsahj_deep_corruption_aoe : public AuraScript
{
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_DEEP_CORRUPTION_AURA);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_yorsahj_deep_corruption_aoe::HandleRemove, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
    }
};

// 105571 - Acide digestif : 4 (10) / 8 (25) joueurs, jamais le tank
class spell_yorsahj_digestive_acid_aoe : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (Creature* yorsahj = GetCaster()->FindNearestCreature(BOSS_YORSAHJ_THE_UNSLEEPING, 200.0f))
            if (Unit* tank = yorsahj->GetVictim())
                targets.remove(tank);

        Trinity::Containers::RandomResize(targets, GetCaster()->GetMap()->Is25ManRaid() ? 8 : 4);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            GetCaster()->CastSpell(target, SPELL_DIGESTIVE_ACID_DMG, true);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_yorsahj_digestive_acid_aoe::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget.Register(&spell_yorsahj_digestive_acid_aoe::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 105530 - Vide de mana : ne touche que les joueurs à mana
class spell_yorsahj_mana_void : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* obj)
        {
            Player* player = obj->ToPlayer();
            return !player || player->GetPowerType() != POWER_MANA;
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_yorsahj_mana_void::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnObjectAreaTargetSelect.Register(&spell_yorsahj_mana_void::FilterTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// ---------------------------------------------------------------------------
// Trash de la Gueule de Shu'ma : globules qui patrouillent en anneau autour de Yor'sahj
// (rayon ~60 m, relevé en jeu). Chaque globule tourne autour du centre en partant de
// sa position de spawn ; deux globules côte à côte forment un pack.
// ---------------------------------------------------------------------------
enum ShumaTrash
{
    NPC_ACIDIC_GLOBULE_TRASH    = 57333,
    NPC_COBALT_GLOBULE_TRASH    = 57384,
    NPC_GLOWING_GLOBULE_TRASH   = 57387,
    NPC_SHADOWED_GLOBULE_TRASH  = 57388,

    // seul sort de trash confirmé (guide Tauri-Veins). Cobalt / Lumineux / Acide : mêlée
    // pour l'instant, à compléter avec leurs IDs de Spell.dbc.
    SPELL_TRASH_DEEP_CORRUPTION = 108220,

    EVENT_T_DEEP_CORRUPTION     = 1,
    POINT_PATROL                = 1
};

constexpr float SHUMA_PATROL_RADIUS = 60.0f;
constexpr float SHUMA_PATROL_Z      = -180.6f;
constexpr float SHUMA_PATROL_STEP   = 0.25f; // ~15 m entre deux points

struct npc_yorsahj_trash_globule : public ScriptedAI
{
    npc_yorsahj_trash_globule(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        _angle = std::atan2(me->GetPositionY() - YorsahjCenter.GetPositionY(), me->GetPositionX() - YorsahjCenter.GetPositionX());
        NextPoint();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (me->GetEntry() == NPC_SHADOWED_GLOBULE_TRASH)
            _events.ScheduleEvent(EVENT_T_DEEP_CORRUPTION, 5s, 8s);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_PATROL && !me->IsInCombat())
            NextPoint();
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
            if (eventId == EVENT_T_DEEP_CORRUPTION)
            {
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                    DoCast(target, SPELL_TRASH_DEEP_CORRUPTION);
                _events.Repeat(12s, 16s);
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    void NextPoint()
    {
        _angle -= SHUMA_PATROL_STEP; // sens horaire
        float x = YorsahjCenter.GetPositionX() + SHUMA_PATROL_RADIUS * std::cos(_angle);
        float y = YorsahjCenter.GetPositionY() + SHUMA_PATROL_RADIUS * std::sin(_angle);
        me->SetWalk(true);
        me->GetMotionMaster()->MovePoint(POINT_PATROL, x, y, SHUMA_PATROL_Z);
    }

    EventMap _events;
    float _angle = 0.0f;
};
}

void AddSC_boss_yorsahj_the_unsleeping()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(boss_yorsahj_the_unsleeping);
    RegisterDragonSoulCreatureAI(npc_yorsahj_globule);
    RegisterDragonSoulCreatureAI(npc_yorsahj_forgotten_one);
    RegisterDragonSoulCreatureAI(npc_yorsahj_mana_void);
    RegisterDragonSoulCreatureAI(npc_yorsahj_trash_globule);
    RegisterSpellScript(spell_yorsahj_deep_corruption);
    RegisterSpellScript(spell_yorsahj_deep_corruption_aoe);
    RegisterSpellScript(spell_yorsahj_digestive_acid_aoe);
    RegisterSpellScript(spell_yorsahj_mana_void);
}
