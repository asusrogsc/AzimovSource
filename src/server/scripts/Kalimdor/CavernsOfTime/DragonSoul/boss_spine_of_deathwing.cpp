/*
 * Azimov - Dragon Soul : l'Échine d'Aile de mort
 *
 *  - Swayze (pont du Brûleciel, après Corne-Noire) : « Allons-y ! » => cinématique, le raid saute sur le dos d'Aile de mort.
 *  - Corruptions (dans les trous de l'armure) : Plasma brûlant (absorbe les soins), Poigne ardente (étourdit,
 *    rompue en lui infligeant 20 % de sa vie). En mourant : un Amalgame hideux + un trou (Vrilles agrippantes).
 *  - Sang corrompu : sort régulièrement des trous. « Tué », il laisse un Résidu, puis repart vers un trou et revit.
 *  - Amalgame hideux : absorbe les Résidus à côté de lui (Sang absorbé). À 9 : Noyau surchauffé.
 *    Tué à 9 stacks près des tendons de la plaque en cours : Explosion nucléaire => la plaque se soulève,
 *    les Tendons ardents sont attaquables le temps qu'ils se referment (Sceller la brèche).
 *  - Tendons tués : la plaque s'arrache. 3 plaques => Aile de mort s'écroule, le raid part au Maelström.
 *  - Roulade : trop de monde d'un côté => Aile de mort roule. Seuls ceux qui sont dans un trou (Vrilles)
 *    ou sous Poigne ardente tiennent ; les autres sont projetés dans le vide.
 *
 * Base : AshamaneCore (positions, sorts), réécrit pour ce core.
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureTextMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum SpineSpells
{
    // Corruption
    SPELL_FIERY_GRIP                = 105490,
    SPELL_SEARING_PLASMA            = 105479,

    // Amalgame hideux
    SPELL_DEGRADATION               = 106005,
    SPELL_NUCLEAR_BLAST             = 105845,
    SPELL_SUPERHEATED_NUCLEUS       = 105834,
    SPELL_ABSORBED_BLOOD            = 105248,

    // Trou
    SPELL_GRASPING_TENDRILS         = 105510,
    SPELL_GRASPING_TENDRILS_HOLD    = 105563,

    // Sang corrompu
    SPELL_RESIDUE                   = 105223,
    SPELL_BURST                     = 105219,

    // Tendons ardents
    SPELL_SEAL_ARMOR_BREACH_LEFT    = 105847,
    SPELL_SEAL_ARMOR_BREACH_RIGHT   = 105848,
    SPELL_BREACH_ARMOR_LEFT         = 105363,
    SPELL_BREACH_ARMOR_RIGHT        = 105385,
    SPELL_PLATE_FLY_OFF_LEFT        = 105366,
    SPELL_PLATE_FLY_OFF_RIGHT       = 105384
};

enum SpineNpcs
{
    NPC_SPINE_OF_DEATHWING          = 53879,
    NPC_HIDEOUS_AMALGAMATION        = 53890,
    NPC_CORRUPTION                  = 53891,
    NPC_SPINE_SPAWNER               = 53888,
    NPC_CORRUPTED_BLOOD             = 53889,
    NPC_BURNING_TENDONS_LEFT        = 56341,
    NPC_BURNING_TENDONS_RIGHT       = 56575,
    NPC_SKY_CAPTAIN_SWAYZE_SPINE    = 55870
};

enum SpineTexts
{
    // Échine (Aile de mort)
    EMOTE_ROLL_LEFT_WARNING  = 0,
    EMOTE_ROLL_RIGHT_WARNING = 1,
    EMOTE_ROLL_LEFT          = 2,
    EMOTE_ROLL_RIGHT         = 3,
    SAY_PLATE                = 4,
    SAY_RANDOM               = 5,

    // Amalgame
    EMOTE_NUCLEAR_BLAST      = 0,
    EMOTE_NOT_ENOUGH_BLOOD   = 1,
    EMOTE_TOO_FAR            = 2,

    // Swayze
    SAY_SWAYZE_PLATES        = 9
};

enum SpineEvents
{
    EVENT_CHECK_ROLL = 1,
    EVENT_ROLL_WARNING_END,
    EVENT_ROLL,
    EVENT_FALL_CHECK,
    EVENT_SUMMON_BLOOD,
    EVENT_RANDOM_TALK,
    EVENT_CHECK_WIPE,
    EVENT_END_ENCOUNTER,
    EVENT_SEARING_PLASMA,
    EVENT_FIERY_GRIP,
    EVENT_ABSORB,
    EVENT_BLAST_FALLBACK,
    EVENT_BACK_TO_HOLE,
    EVENT_SEAL_CHECK
};

enum SpineActions
{
    ACTION_START_SPINE  = 1,
    ACTION_TENDONS_DIED = 2,
    ACTION_OPEN_TENDONS = 3
};

enum SpineMisc
{
    POINT_HOLE          = 1
};

// Positions (AshamaneCore)
static Position const SpinePos             = { -13852.5f,  -13665.38f, 297.3786f, 1.53589f };
static Position const SpineArrivalPos      = { -13854.67f, -13666.97f, 275.0f,    1.60f    };
static Position const MadnessArrivalPos    = { -12081.39f,  12160.05f,  30.60f,   6.03f    };
static Position const GreaterCachePos      = { -12075.2f,   12168.2f,   -2.56926f, 3.57793f };
static Position const CorruptionPos[8] =
{
    { -13868.6f, -13667.3f, 262.836f, 0.0698f }, // gauche 1
    { -13841.1f, -13666.9f, 262.795f, 3.0194f }, // droite 1
    { -13868.5f, -13654.1f, 262.946f, 0.0175f }, // gauche 2
    { -13842.8f, -13654.1f, 263.965f, 5.8469f }, // droite 2
    { -13867.1f, -13638.5f, 264.783f, 2.3387f }, // gauche 3
    { -13841.2f, -13635.2f, 265.261f, 0.7854f }, // droite 3
    { -13870.5f, -13614.4f, 266.88f,  1.9548f }, // gauche 4
    { -13839.3f, -13614.8f, 266.32f,  6.2308f }  // droite 4
};
static Position const TendonsPos[6] =
{
    { -13862.8f, -13645.0f, 265.752f, 1.5708f }, // plaque 1 gauche
    { -13846.8f, -13644.7f, 265.789f, 1.5708f }, // plaque 1 droite
    { -13862.6f, -13626.3f, 266.59f,  1.5708f }, // plaque 2 gauche
    { -13846.6f, -13626.0f, 266.638f, 1.5708f }, // plaque 2 droite
    { -13862.6f, -13604.9f, 269.227f, 1.5708f }, // plaque 3 gauche
    { -13846.6f, -13604.7f, 269.174f, 1.5708f }  // plaque 3 droite
};

static constexpr float SPINE_MIDDLE_X       = -13854.79f; // à gauche : x plus petit
static constexpr float SPINE_LEFT_LIMIT_X   = -13860.0f;
static constexpr float SPINE_RIGHT_LIMIT_X  = -13850.3f;
static constexpr float HOLE_HOLD_RANGE      = 6.0f;
static constexpr float BLAST_TENDONS_RANGE  = 20.0f;

static void ZoneTalk(Creature* creature, uint8 group)
{
    if (creature)
        sCreatureTextMgr->SendChat(creature, group, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_MAP);
}

static std::vector<Player*> GetSpinePlayers(Map* map)
{
    std::vector<Player*> players;
    for (auto const& ref : map->GetPlayers())
        if (Player* player = ref.GetSource())
            if (player->IsAlive() && !player->IsGameMaster() && player->GetExactDist2d(SpineArrivalPos) < 150.0f)
                players.push_back(player);
    return players;
}

static Creature* GetSpine(WorldObject* source)
{
    if (InstanceScript* instance = source->GetInstanceScript())
        return instance->GetCreature(DATA_SPINE_OF_DEATHWING);
    return nullptr;
}

// ===========================================================================
// L'Échine d'Aile de mort : gère tout le combat
// ===========================================================================
struct npc_ds_spine_of_deathwing : public ScriptedAI
{
    npc_ds_spine_of_deathwing(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()), _summons(creature)
    {
        SetCombatMovement(false);
    }

    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->setActive(true);
    }

    void Reset() override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void DamageTaken(Unit* /*attacker*/, uint32& damage) override { damage = 0; }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        _summons.Despawn(summon);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_START_SPINE:
                StartEncounter();
                break;
            case ACTION_TENDONS_DIED:
                PlateDestroyed();
                break;
            default:
                break;
        }
    }

    uint32 GetData(uint32 /*type*/) const override { return _plates; }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CHECK_ROLL:
                    CheckRoll();
                    break;
                case EVENT_ROLL_WARNING_END:
                    ZoneTalk(me, _rollSide == 1 ? EMOTE_ROLL_LEFT : EMOTE_ROLL_RIGHT);
                    _events.ScheduleEvent(EVENT_ROLL, 1s);
                    break;
                case EVENT_ROLL:
                    DoRoll();
                    _events.ScheduleEvent(EVENT_CHECK_ROLL, 10s);
                    break;
                case EVENT_FALL_CHECK:
                    // ceux qui ont été projetés et tombent dans le vide
                    for (Player* player : GetSpinePlayers(me->GetMap()))
                        if (player->GetPositionZ() < 250.0f)
                            Unit::Kill(me, player, false);
                    break;
                case EVENT_SUMMON_BLOOD:
                {
                    std::vector<Creature*> holes = Holes();
                    if (!holes.empty())
                    {
                        Creature* hole = Trinity::Containers::SelectRandomContainerElement(holes);
                        if (Creature* blood = me->SummonCreature(NPC_CORRUPTED_BLOOD, *hole, TEMPSUMMON_MANUAL_DESPAWN))
                            DoZoneInCombat(blood);
                    }
                    _events.Repeat(12s);
                    break;
                }
                case EVENT_RANDOM_TALK:
                    ZoneTalk(me, SAY_RANDOM);
                    _events.Repeat(35s, 45s);
                    break;
                case EVENT_CHECK_WIPE:
                    if (GetSpinePlayers(me->GetMap()).empty())
                    {
                        ResetEncounter();
                        return;
                    }
                    _events.Repeat(2s);
                    break;
                case EVENT_END_ENCOUNTER:
                    EndEncounter();
                    return;
                default:
                    break;
            }
        }
    }

    std::vector<Creature*> Holes() const
    {
        std::vector<Creature*> holes;
        for (ObjectGuid const& guid : _summons)
            if (Creature* summon = ObjectAccessor::GetCreature(*me, guid))
                if (summon->GetEntry() == NPC_SPINE_SPAWNER)
                    holes.push_back(summon);
        return holes;
    }

private:
    void StartEncounter()
    {
        if (_started)
            return;
        _started = true;
        _plates = 0;
        _rollSide = 0;

        if (_instance)
        {
            _instance->SetBossState(DATA_SPINE_OF_DEATHWING, IN_PROGRESS);
            if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE_SPINE, 50000.0f))
                ZoneTalk(swayze, SAY_SWAYZE_PLATES);
        }

        for (uint8 i = 0; i < 4; ++i)
            SummonCorruption(i);
        for (uint8 i = 0; i < 6; ++i)
            if (Creature* tendons = me->SummonCreature(i % 2 ? NPC_BURNING_TENDONS_RIGHT : NPC_BURNING_TENDONS_LEFT, TendonsPos[i], TEMPSUMMON_MANUAL_DESPAWN))
                tendons->AI()->SetData(0, i);

        _events.ScheduleEvent(EVENT_CHECK_ROLL, 5s);
        _events.ScheduleEvent(EVENT_SUMMON_BLOOD, 10s);
        _events.ScheduleEvent(EVENT_RANDOM_TALK, 3s, 5s);
        _events.ScheduleEvent(EVENT_CHECK_WIPE, 10s);
    }

public:
    void SummonCorruption(uint8 index)
    {
        if (Creature* corruption = me->SummonCreature(NPC_CORRUPTION, CorruptionPos[index], TEMPSUMMON_MANUAL_DESPAWN))
        {
            corruption->AI()->SetData(0, index);
            DoZoneInCombat(corruption);
        }
    }

    bool AnyCorruptionAlive() const
    {
        for (ObjectGuid const& guid : _summons)
            if (Creature* summon = ObjectAccessor::GetCreature(*me, guid))
                if (summon->GetEntry() == NPC_CORRUPTION && summon->IsAlive())
                    return true;
        return false;
    }

private:
    // Roulade : trop de monde d'un côté
    void CheckRoll()
    {
        std::vector<Player*> players = GetSpinePlayers(me->GetMap());
        uint32 left = 0, right = 0;
        for (Player* player : players)
        {
            if (player->GetPositionX() <= SPINE_LEFT_LIMIT_X)
                ++left;
            else if (player->GetPositionX() >= SPINE_RIGHT_LIMIT_X)
                ++right;
        }

        uint32 minDiff = players.size() > 3 ? 2 : 1;
        _rollSide = 0;
        if (left > right && left - right >= minDiff)
            _rollSide = 1;
        else if (right > left && right - left >= minDiff)
            _rollSide = 2;

        if (!_rollSide)
        {
            _events.ScheduleEvent(EVENT_CHECK_ROLL, 2s);
            return;
        }

        ZoneTalk(me, _rollSide == 1 ? EMOTE_ROLL_LEFT_WARNING : EMOTE_ROLL_RIGHT_WARNING);
        _events.ScheduleEvent(EVENT_ROLL_WARNING_END, 5s);
    }

    bool IsHolding(Player* player) const
    {
        if (player->HasAura(SPELL_FIERY_GRIP) || player->HasAura(SPELL_GRASPING_TENDRILS_HOLD))
            return true;
        for (Creature* hole : Holes())
            if (player->GetExactDist2d(hole) <= HOLE_HOLD_RANGE)
                return true;
        return false;
    }

    void DoRoll()
    {
        float sideX = _rollSide == 1 ? -60.0f : 60.0f;
        for (Player* player : GetSpinePlayers(me->GetMap()))
            if (!IsHolding(player))
                player->KnockbackFrom(player->GetPositionX() - sideX, player->GetPositionY(), 35.0f, 12.0f);

        // les Amalgames et le sang non retenus tombent aussi
        for (ObjectGuid const& guid : _summons)
            if (Creature* summon = ObjectAccessor::GetCreature(*me, guid))
                if (summon->GetEntry() == NPC_HIDEOUS_AMALGAMATION)
                    summon->DespawnOrUnsummon();

        _events.ScheduleEvent(EVENT_FALL_CHECK, 4s);
        _rollSide = 0;
    }

    void PlateDestroyed()
    {
        ++_plates;
        ZoneTalk(me, SAY_PLATE);
        if (_instance)
            if (GameObject* plate = _instance->GetGameObject(DATA_BACK_PLATE_1 + _plates - 1))
                plate->UseDoorOrButton();

        switch (_plates)
        {
            case 1:
                SummonCorruption(4);
                SummonCorruption(5);
                break;
            case 2:
                SummonCorruption(6);
                SummonCorruption(7);
                break;
            default:
                _events.Reset();
                _events.ScheduleEvent(EVENT_END_ENCOUNTER, 2s);
                break;
        }
    }

    void CleanupAdds()
    {
        _summons.DespawnAll();
        if (_instance)
        {
            _instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_SEARING_PLASMA);
            _instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_FIERY_GRIP);
            _instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_DEGRADATION);
        }
    }

    void ResetEncounter()
    {
        _events.Reset();
        CleanupAdds();
        if (_instance && _instance->GetBossState(DATA_SPINE_OF_DEATHWING) != DONE)
            _instance->SetBossState(DATA_SPINE_OF_DEATHWING, FAIL);
        me->DespawnOrUnsummon();
    }

    void EndEncounter()
    {
        CleanupAdds();
        if (!_instance)
            return;

        _instance->SetBossState(DATA_SPINE_OF_DEATHWING, DONE);

        // Aile de mort s'écroule : cinématique, puis le raid se retrouve au Maelström
        for (auto const& ref : me->GetMap()->GetPlayers())
            if (Player* player = ref.GetSource())
                if (player->GetExactDist2d(SpineArrivalPos) < 300.0f)
                {
                    player->SendMovieStart(75);
                    player->NearTeleportTo(MadnessArrivalPos.GetPositionX(), MadnessArrivalPos.GetPositionY(), MadnessArrivalPos.GetPositionZ(), MadnessArrivalPos.GetOrientation());
                }

        // Grande cache des Aspects sur la plate-forme du Maelström
        uint32 cacheEntry = GO_GREATER_CACHE_10N;
        switch (me->GetMap()->GetDifficulty())
        {
            case RAID_DIFFICULTY_25MAN_NORMAL: cacheEntry = GO_GREATER_CACHE_25N; break;
            case RAID_DIFFICULTY_10MAN_HEROIC: cacheEntry = GO_GREATER_CACHE_10H; break;
            case RAID_DIFFICULTY_25MAN_HEROIC: cacheEntry = GO_GREATER_CACHE_25H; break;
            default: break;
        }
        if (GameObject* cache = me->SummonGameObject(cacheEntry, GreaterCachePos, QuaternionData::fromEulerAnglesZYX(GreaterCachePos.GetOrientation(), 0.0f, 0.0f), WEEK))
            cache->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED | GO_FLAG_NOT_SELECTABLE | GO_FLAG_INTERACT_COND | GO_FLAG_IN_USE);

        me->DespawnOrUnsummon(5s);
    }

    InstanceScript* _instance;
    SummonList _summons;
    EventMap _events;
    uint8 _plates = 0;
    uint8 _rollSide = 0; // 1 gauche, 2 droite
    bool _started = false;
};

// ===========================================================================
// Corruption : dans un trou de l'armure
// ===========================================================================
struct npc_ds_spine_corruption : public ScriptedAI
{
    npc_ds_spine_corruption(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void SetData(uint32 /*type*/, uint32 data) override { _index = uint8(data); }

    void Reset() override
    {
        _events.Reset();
        _gripDamageLeft = 0;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_SEARING_PLASMA, 1s, 8s);
        _events.ScheduleEvent(EVENT_FIERY_GRIP, 31s, 33s);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    // Poigne ardente : rompue quand la Corruption perd 20 % de sa vie pendant la canalisation
    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!_gripDamageLeft)
            return;
        if (damage >= _gripDamageLeft)
        {
            _gripDamageLeft = 0;
            me->InterruptNonMeleeSpells(true);
        }
        else
            _gripDamageLeft -= damage;
    }

    void JustDied(Unit* /*killer*/) override
    {
        _events.Reset();
        Creature* spine = GetSpine(me);
        if (!spine)
            return;

        // à sa place : un trou (Vrilles agrippantes) et un Amalgame hideux
        if (Creature* hole = spine->SummonCreature(NPC_SPINE_SPAWNER, *me, TEMPSUMMON_MANUAL_DESPAWN))
            hole->CastSpell(hole, SPELL_GRASPING_TENDRILS, true);
        if (Creature* amalgamation = spine->SummonCreature(NPC_HIDEOUS_AMALGAMATION, *me, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s))
            DoZoneInCombat(amalgamation);

        // il reste toujours au moins une Corruption sur le dos
        if (auto* spineAI = dynamic_cast<npc_ds_spine_of_deathwing*>(spine->AI()))
            if (!spineAI->AnyCorruptionAlive())
                spineAI->SummonCorruption(_index % 2 ? _index - 1 : _index + 1);

        me->DespawnOrUnsummon(2s);
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
                case EVENT_SEARING_PLASMA:
                    // Plasma brûlant : absorbe les soins, se cumule
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 200.0f, true))
                    {
                        if (Aura* plasma = target->GetAura(SPELL_SEARING_PLASMA))
                            plasma->ModStackAmount(1);
                        else
                            me->CastSpell(target, SPELL_SEARING_PLASMA, true);
                    }
                    _events.Repeat(15s, 20s);
                    break;
                case EVENT_FIERY_GRIP:
                {
                    Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 200.0f, true, true, -int32(SPELL_FIERY_GRIP));
                    if (!target)
                        target = SelectTarget(SELECT_TARGET_RANDOM, 0, 200.0f, true);
                    if (target)
                    {
                        _gripDamageLeft = me->CountPctFromMaxHealth(20);
                        CastSpellExtraArgs args;
                        args.AddSpellMod(SPELLVALUE_MAX_TARGETS, me->GetMap()->Is25ManRaid() ? 3 : 1);
                        me->CastSpell(target, SPELL_FIERY_GRIP, args);
                    }
                    _events.Repeat(31s, 33s);
                    break;
                }
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
    }

private:
    EventMap _events;
    uint32 _gripDamageLeft = 0;
    uint8 _index = 0;
};

// ===========================================================================
// Trou (Vrilles agrippantes) : immobile, tient les joueurs pendant les roulades
// ===========================================================================
struct npc_ds_spine_spawner : public ScriptedAI
{
    npc_ds_spine_spawner(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void UpdateAI(uint32 /*diff*/) override { }
};

// ===========================================================================
// Sang corrompu : « tué », il devient un Résidu qui rampe vers un trou puis revit
// ===========================================================================
struct npc_ds_spine_corrupted_blood : public ScriptedAI
{
    npc_ds_spine_corrupted_blood(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_AGGRESSIVE);
        _events.Reset();
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (damage < me->GetHealth())
            return;

        damage = 0;
        if (_residue)
            return;

        _residue = true;
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        DoCastAOE(SPELL_BURST, true);
        DoCastSelf(SPELL_RESIDUE, true);
        me->GetMotionMaster()->Clear();
        me->StopMoving();
        me->SetSpeedRate(MOVE_RUN, 0.3f);
        me->SetSpeedRate(MOVE_WALK, 0.3f);
        _events.ScheduleEvent(EVENT_BACK_TO_HOLE, 5s);
    }

    bool IsResidue() const { return _residue; }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE || pointId != POINT_HOLE || !_residue)
            return;

        // revenu dans un trou : il reprend vie
        _residue = false;
        me->RemoveAurasDueToSpell(SPELL_RESIDUE);
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        me->SetSpeedRate(MOVE_RUN, 1.0f);
        me->SetSpeedRate(MOVE_WALK, 1.0f);
        me->SetFullHealth();
        me->SetReactState(REACT_AGGRESSIVE);
        DoZoneInCombat();
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() == EVENT_BACK_TO_HOLE && _residue)
        {
            Creature* nearest = nullptr;
            if (Creature* spine = GetSpine(me))
                if (auto* spineAI = dynamic_cast<npc_ds_spine_of_deathwing*>(spine->AI()))
                    for (Creature* hole : spineAI->Holes())
                        if (!nearest || me->GetExactDist2d(hole) < me->GetExactDist2d(nearest))
                            nearest = hole;
            if (nearest)
                me->GetMotionMaster()->MovePoint(POINT_HOLE, *nearest);
        }

        if (_residue || !UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    bool _residue = false;
};

// ===========================================================================
// Amalgame hideux : absorbe les Résidus ; à 9, son explosion soulève une plaque
// ===========================================================================
struct npc_ds_spine_hideous_amalgamation : public ScriptedAI
{
    npc_ds_spine_hideous_amalgamation(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        _absorbed = 0;
        _blasting = false;
        me->SetReactState(REACT_AGGRESSIVE);
    }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        DoZoneInCombat();
        _events.ScheduleEvent(EVENT_ABSORB, 1s);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (damage < me->GetHealth())
            return;

        if (_absorbed < 9)
        {
            Talk(EMOTE_NOT_ENOUGH_BLOOD);
            return; // il meurt sans exploser
        }

        damage = 0;
        if (_blasting)
            return;

        // Explosion nucléaire (incantation), puis il meurt
        _blasting = true;
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        Talk(EMOTE_NUCLEAR_BLAST);
        DoCastAOE(SPELL_NUCLEAR_BLAST);
        _events.Reset();
        _events.ScheduleEvent(EVENT_BLAST_FALLBACK, 6s);
    }

    void OnSpellCastFinished(SpellInfo const* spell, SpellFinishReason /*reason*/) override
    {
        if (spell->Id == SPELL_NUCLEAR_BLAST && _blasting)
            ResolveBlast();
    }

    void JustDied(Unit* /*killer*/) override
    {
        // héroïque : Dégradation sur le raid
        if (IsHeroic())
            for (Player* player : GetSpinePlayers(me->GetMap()))
                me->AddAura(SPELL_DEGRADATION, player);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            if (eventId == EVENT_ABSORB)
            {
                AbsorbResidues();
                _events.Repeat(1s);
            }
            else if (eventId == EVENT_BLAST_FALLBACK)
                ResolveBlast();
        }

        if (_blasting || !UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    void AbsorbResidues()
    {
        if (_absorbed >= 9)
            return;

        std::list<Creature*> bloods;
        me->GetCreatureListWithEntryInGrid(bloods, NPC_CORRUPTED_BLOOD, 6.0f);
        for (Creature* blood : bloods)
        {
            auto* bloodAI = dynamic_cast<npc_ds_spine_corrupted_blood*>(blood->AI());
            if (!bloodAI || !bloodAI->IsResidue() || _absorbed >= 9)
                continue;

            blood->DespawnOrUnsummon();
            ++_absorbed;
            if (Aura* stacks = me->GetAura(SPELL_ABSORBED_BLOOD))
                stacks->SetStackAmount(_absorbed);
            else if (Aura* aura = me->AddAura(SPELL_ABSORBED_BLOOD, me))
                aura->SetStackAmount(_absorbed);

            if (_absorbed >= 9 && !me->HasAura(SPELL_SUPERHEATED_NUCLEUS))
                me->AddAura(SPELL_SUPERHEATED_NUCLEUS, me);
        }
    }

    // l'explosion soulève la plaque en cours si l'Amalgame est près des bons tendons
    void ResolveBlast()
    {
        if (!_blasting || _resolved)
            return;
        _resolved = true;
        _events.Reset();

        bool opened = false;
        if (Creature* spine = GetSpine(me))
        {
            uint32 plate = spine->AI()->GetData(0);
            if (plate < 3)
            {
                bool left = me->GetPositionX() <= SPINE_MIDDLE_X;
                uint8 index = uint8(plate * 2 + (left ? 0 : 1));
                std::list<Creature*> tendons;
                me->GetCreatureListWithEntryInGrid(tendons, left ? NPC_BURNING_TENDONS_LEFT : NPC_BURNING_TENDONS_RIGHT, BLAST_TENDONS_RANGE);
                for (Creature* tendon : tendons)
                    if (tendon->IsAlive() && tendon->AI()->GetData(0) == index)
                    {
                        tendon->AI()->DoAction(ACTION_OPEN_TENDONS);
                        opened = true;
                        break;
                    }
            }
        }
        if (!opened)
            Talk(EMOTE_TOO_FAR);

        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        Unit::Kill(me, me, false);
    }

    EventMap _events;
    uint8 _absorbed = 0;
    bool _blasting = false;
    bool _resolved = false;
};

// ===========================================================================
// Tendons ardents : attaquables tant que la brèche n'est pas refermée
// ===========================================================================
struct npc_ds_spine_burning_tendons : public ScriptedAI
{
    npc_ds_spine_burning_tendons(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void SetData(uint32 /*type*/, uint32 data) override { _index = uint8(data); }
    uint32 GetData(uint32 /*type*/) const override { return _index; }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        Close();
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!_open)
            damage = 0;
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_OPEN_TENDONS || _open)
            return;

        _open = true;
        bool right = _index % 2;
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
        DoCastSelf(right ? SPELL_BREACH_ARMOR_RIGHT : SPELL_BREACH_ARMOR_LEFT, true);
        // il referme la brèche : il faut le tuer avant la fin de cette canalisation
        DoCastSelf(right ? SPELL_SEAL_ARMOR_BREACH_RIGHT : SPELL_SEAL_ARMOR_BREACH_LEFT);
        _events.ScheduleEvent(EVENT_SEAL_CHECK, 1s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoCastSelf(_index % 2 ? SPELL_PLATE_FLY_OFF_RIGHT : SPELL_PLATE_FLY_OFF_LEFT, true);
        if (Creature* spine = GetSpine(me))
            spine->AI()->DoAction(ACTION_TENDONS_DIED);

        // l'autre tendon de la même plaque disparaît avec elle
        uint8 pair = _index % 2 ? _index - 1 : _index + 1;
        std::list<Creature*> others;
        me->GetCreatureListWithEntryInGrid(others, pair % 2 ? NPC_BURNING_TENDONS_RIGHT : NPC_BURNING_TENDONS_LEFT, 40.0f);
        for (Creature* other : others)
            if (other->AI()->GetData(0) == pair)
                other->DespawnOrUnsummon();

        me->DespawnOrUnsummon(1s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() == EVENT_SEAL_CHECK && _open)
        {
            // canalisation terminée (ou interrompue) : la brèche est refermée
            if (!me->HasUnitState(UNIT_STATE_CASTING))
                Close();
            else
                _events.ScheduleEvent(EVENT_SEAL_CHECK, 1s);
        }
    }

private:
    void Close()
    {
        _open = false;
        _events.Reset();
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
    }

    EventMap _events;
    uint8 _index = 0;
    bool _open = false;
};
}

void AddSC_boss_spine_of_deathwing()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(npc_ds_spine_of_deathwing);
    RegisterDragonSoulCreatureAI(npc_ds_spine_corruption);
    RegisterDragonSoulCreatureAI(npc_ds_spine_spawner);
    RegisterDragonSoulCreatureAI(npc_ds_spine_corrupted_blood);
    RegisterDragonSoulCreatureAI(npc_ds_spine_hideous_amalgamation);
    RegisterDragonSoulCreatureAI(npc_ds_spine_burning_tendons);
}
