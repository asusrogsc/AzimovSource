/*
 * Azimov - Dragon Soul : sommet du Repos du ver, de l'arrivée du raid jusqu'à Ultraxion
 *
 *  1) Arrivée au sommet après les Gueules : Thrall et Kalecgos parlent du Dragon Soul,
 *     Kalecgos ouvre le passage vers l'Œil de l'Éternité (portail déjà en place).
 *  2) Retour après Hagara : conversation des Aspects, puis Thrall propose de lancer le rituel.
 *  3) Rituel : les Aspects canalisent sur l'Âme des dragons ; des Assaillants du Crépuscule
 *     tournent au-dessus du sommet et crachent leurs flammes. Ils se posent quand un tank
 *     les provoque (ou d'eux-mêmes au bout d'un moment). Quand tous sont morts, Ultraxion arrive.
 *  4) Ultraxion : Monstruosité instable (7e = éruption), Glissement crépusculaire / Volonté héroïque,
 *     Heure du Crépuscule, Lumière déclinante, Rafale crépusculaire, aides des Aspects
 *     (Dernier défenseur d'Azeroth, Don de vie, Essence des rêves, Source de magie, Boucle temporelle).
 *  5) Mort d'Ultraxion : Cache des Aspects selon la difficulté, fin du rituel.
 *
 * Textes : creature_text avec BroadcastTextId (voix + traduction du client).
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureTextMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "StringFormat.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum UltraxionSpells
{
    // Ultraxion
    SPELL_UNSTABLE_MONSTROSITY_1        = 106372,
    SPELL_UNSTABLE_MONSTROSITY_2        = 106376,
    SPELL_UNSTABLE_MONSTROSITY_3        = 106377,
    SPELL_UNSTABLE_MONSTROSITY_4        = 106378,
    SPELL_UNSTABLE_MONSTROSITY_5        = 106379,
    SPELL_UNSTABLE_MONSTROSITY_6        = 106380,
    SPELL_TWILIGHT_INSTABILITY_TARGET   = 106374,
    SPELL_TWILIGHT_INSTABILITY_DAMAGE   = 106375,
    SPELL_TWILIGHT_ERUPTION             = 106388,
    SPELL_TWILIGHT_SHIFT_AOE            = 106369,
    SPELL_TWILIGHT_SHIFT                = 106368,
    SPELL_HEROIC_WILL_AOE               = 105554,
    SPELL_HEROIC_WILL                   = 106108,
    SPELL_FADED_INTO_TWILIGHT           = 105927,
    SPELL_HOUR_OF_TWILIGHT              = 106371,
    SPELL_LOOMING_DARKNESS              = 106498,
    SPELL_LOOMING_DARKNESS_DAMAGE       = 109231,
    SPELL_FADING_LIGHT                  = 105925,
    SPELL_FADING_LIGHT_KILL             = 105926,
    SPELL_TWILIGHT_BURST                = 106415,
    SPELL_COSMETIC_LIGHTNING_1          = 105592,
    SPELL_COSMETIC_LIGHTNING_2          = 109405,
    SPELL_COSMETIC_LIGHTNING_3          = 109406,

    // Aspects
    SPELL_WARD_OF_TIME                  = 108160,
    SPELL_WARD_OF_EARTH                 = 108161,
    SPELL_WARD_OF_MAGIC                 = 108162,
    SPELL_WARD_OF_LIFE                  = 108163,
    SPELL_WARD_OF_DREAMS                = 108164,
    SPELL_LAST_DEFENDER_OF_AZEROTH      = 106182, // Thrall : visuel
    SPELL_LAST_DEFENDER_WARRIOR         = 106080,
    SPELL_LAST_DEFENDER_DRUID           = 106224,
    SPELL_LAST_DEFENDER_PALADIN         = 106226,
    SPELL_LAST_DEFENDER_DK              = 106227,
    SPELL_GIFT_OF_LIFE_AURA             = 105896,
    SPELL_ESSENCE_OF_DREAMS_AURA        = 105900,
    SPELL_SOURCE_OF_MAGIC_AURA          = 105903,
    SPELL_TIMELOOP                      = 105984,
    SPELL_TIMELOOP_HEAL                 = 105992,

    // Assaillant du Crépuscule : sorts crépusculaires déjà utilisés par des créatures de ce core
    SPELL_ASSAULTER_TWILIGHT_BREATH     = 78143,  // Souffle crépusculaire (cône frontal)
    SPELL_ASSAULTER_TWILIGHT_FLAMES     = 79564   // Boule de feu crépusculaire (lancée en vol sur un joueur)
};

enum UltraxionMisc
{
    NPC_TWILIGHT_ASSAULTER              = 57795,

    GO_GIFT_OF_LIFE                     = 209873,
    GO_ESSENCE_OF_DREAMS                = 209874,
    GO_SOURCE_OF_MAGIC                  = 209875,
    GO_LESSER_CACHE_10N                 = 210163,
    GO_LESSER_CACHE_25N                 = 210161,
    GO_LESSER_CACHE_10H                 = 210160,
    GO_LESSER_CACHE_25H                 = 210162,

    POINT_ULTRAXION_ARRIVED             = 1,
    POINT_ASSAULTER_CIRCLE              = 2,
    POINT_ASSAULTER_LAND                = 3,

    ACTION_START_RITUAL                 = 1, // Ultraxion (invisible) : réplique de début
    ACTION_ULTRAXION_ARRIVE             = 2, // Ultraxion : intro puis combat
    ACTION_ULTRAXION_ARRIVE_SHORT       = 3, // après un wipe sur Ultraxion
    ACTION_ULTRAXION_ERUPTION           = 4,
    ACTION_ULTRAXION_WIPE               = 10, // Thrall
    ACTION_ULTRAXION_DEAD               = 11  // Thrall
};

enum UltraxionTexts
{
    // Ultraxion
    // (groupes 0 et 1 : répliques déplacées vers Aile de mort)
    SAY_ULTRAXION_INTRO_1       = 2,
    SAY_ULTRAXION_INTRO_2       = 3,
    SAY_ULTRAXION_AGGRO         = 4,
    SAY_ULTRAXION_HOUR          = 5,
    SAY_ULTRAXION_KILL          = 6,
    SAY_ULTRAXION_UNSTABLE      = 7,
    SAY_ULTRAXION_ERUPTION      = 8,
    SAY_ULTRAXION_DEATH         = 9,

    // Thrall
    SAY_THRALL_SOUL_TOO_GREAT   = 0,
    SAY_THRALL_I_SEE            = 1,
    SAY_THRALL_NO_CHOICE        = 2,
    SAY_THRALL_BURDEN           = 3,
    SAY_THRALL_PROTECT_US       = 4,
    SAY_THRALL_SPEAK_TO_ME      = 5,
    SAY_THRALL_ASPECTS_AID      = 6,
    SAY_THRALL_LAST_DEFENDER    = 7,
    SAY_THRALL_ULTRAXION_DEAD   = 8,

    // Kalecgos
    SAY_KALEC_NOT_NECESSARILY   = 0,
    SAY_KALEC_FOCUSING_IRIS     = 1,
    SAY_KALEC_OPEN_THE_WAY      = 2,
    SAY_KALEC_BEGIN_RITUAL      = 3,
    SAY_KALEC_SOURCE_OF_MAGIC   = 4,

    // Alexstrasza
    SAY_ALEX_RITUAL_COST        = 0,
    SAY_ALEX_LOST               = 1,
    SAY_ALEX_MY_CLUTCH          = 2,
    SAY_ALEX_GIFT_OF_LIFE       = 3,
    SAY_ALEX_IT_IS_DONE         = 4,

    // Ysera
    SAY_YSERA_TRAP              = 0,
    SAY_YSERA_DISTURBANCE       = 1,
    SAY_YSERA_ESSENCE           = 2,

    // Aile de mort (survole le temple)
    SAY_DW_GOOD_TO_SEE_YOU      = 0, // début du rituel
    SAY_DW_TWISTING_WHELPS      = 1, // après « Ils ne sont plus ma couvée »
    SAY_DW_MERE_WHELPS          = 2, // tous les Assaillants morts
    SAY_DW_ULTIMATE_CREATION    = 3, // Ultraxion surgit

    // Nozdormu
    SAY_NOZ_NOT_UNEXPECTED      = 0,
    SAY_NOZ_TIMELOOP            = 1
};

enum UltraxionEvents
{
    // Thrall
    EVENT_CONVERSATION = 1,
    EVENT_GOSSIP_CHECK,
    EVENT_SPAWN_ASSAULTER,
    EVENT_CHECK_WIPE,
    EVENT_ULTRAXION_ARRIVE,
    EVENT_OUTRO,
    EVENT_DEATHWING_TWISTING,

    // Ultraxion
    EVENT_INTRO,
    EVENT_INTRO_LIGHTNING,
    EVENT_UNSTABLE_MONSTROSITY,
    EVENT_HOUR_OF_TWILIGHT,
    EVENT_FADING_LIGHT,
    EVENT_CHECK_MELEE,
    EVENT_CHECK_TWILIGHT,
    EVENT_THRALL_LAST_DEFENDER,
    EVENT_ALEX_GIFT_OF_LIFE,
    EVENT_YSERA_ESSENCE,
    EVENT_KALEC_SOURCE,
    EVENT_NOZ_TIMELOOP,

    // Assaillant
    EVENT_AIR_FLAMES,
    EVENT_AUTO_LAND,
    EVENT_BREATH,
    EVENT_GROUND_FLAMES
};

// Centre du sommet (l'Âme des dragons) et positions
static Position const SummitCenter          = { -1786.72f, -2393.23f, 341.44f, 0.0f };
static Position const UltraxionSpawnPos     = { -1699.47f, -2388.03f, 285.00f, 3.22f }; // sous le bord du sommet, côté est
static Position const UltraxionFightPos     = { -1699.47f, -2388.03f, 355.19f, 3.22f };
// au sud du sommet, loin du Brûleciel amarré (sa coque empêchait de cliquer sur la cache)
static Position const LootCachePos          = { -1786.00f, -2414.00f, 341.10f, 1.57f };
static Position const BuffOrbPos[2]         =
{
    { -1756.24f, -2385.01f, 340.807f, 0.0f },
    { -1755.99f, -2395.31f, 340.803f, 0.0f }
};

static Position const DeathwingCirclePos    = { -1586.72f, -2393.23f, 400.0f, 1.57f };
static constexpr float  DEATHWING_CIRCLE_RADIUS = 200.0f;
static constexpr float  DEATHWING_CIRCLE_Z      = 400.0f;

static constexpr float  ASSAULTER_CIRCLE_RADIUS = 34.0f;
static constexpr float  ASSAULTER_CIRCLE_Z      = 357.0f;
static constexpr float  ASSAULTER_LAND_RADIUS   = 16.0f;
static constexpr uint32 ASSAULTERS_TOTAL        = 15;

// Les répliques passent par la portée « zone » : tout le raid les entend,
// même quand Ultraxion est encore invisible sous le temple.
static void ZoneTalk(Creature* creature, uint8 group)
{
    if (creature)
        sCreatureTextMgr->SendChat(creature, group, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
}

static bool IsTankSpec(Player const* player)
{
    switch (player->GetPrimaryTalentTree(player->GetActiveSpec()))
    {
        case TALENT_TREE_WARRIOR_PROTECTION:
        case TALENT_TREE_PALADIN_PROTECTION:
        case TALENT_TREE_DEATH_KNIGHT_BLOOD:
            return true;
        case TALENT_TREE_DRUID_FERAL_COMBAT:
            return player->GetShapeshiftForm() == FORM_BEAR;
        default:
            return false;
    }
}

static bool IsHealerSpec(Player const* player)
{
    switch (player->GetPrimaryTalentTree(player->GetActiveSpec()))
    {
        case TALENT_TREE_PALADIN_HOLY:
        case TALENT_TREE_PRIEST_DISCIPLINE:
        case TALENT_TREE_PRIEST_HOLY:
        case TALENT_TREE_SHAMAN_RESTORATION:
        case TALENT_TREE_DRUID_RESTORATION:
            return true;
        default:
            return false;
    }
}

static std::vector<Player*> GetSummitPlayers(Map* map, float range = 150.0f)
{
    std::vector<Player*> players;
    for (auto const& ref : map->GetPlayers())
        if (Player* player = ref.GetSource())
            if (player->IsAlive() && !player->IsGameMaster() && player->GetExactDist2d(SummitCenter) < range)
                players.push_back(player);
    return players;
}

// ===========================================================================
// Thrall (sommet) : conversations, rituel, vagues d'Assaillants, arrivée d'Ultraxion
// ===========================================================================
struct npc_ds_thrall_summit : public ScriptedAI
{
    npc_ds_thrall_summit(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()), _summons(creature) { }

    enum ConversationStep : uint8
    {
        // avant Hagara
        CONV_PRE_HAGARA = 1,
        // après Hagara
        CONV_POST_HAGARA = 10
    };

    enum RitualState : uint8
    {
        RITUAL_IDLE,
        RITUAL_TRASH,
        RITUAL_ULTRAXION
    };

    void Reset() override
    {
        _events.Reset();
        _summons.DespawnAll();
        _state = RITUAL_IDLE;
        _spawned = 0;
        _killed = 0;
        me->SetReactState(REACT_PASSIVE);
        UpdateGossipFlag();
        // Hagara peut mourir après l'apparition de Thrall : on réévalue régulièrement le dialogue
        _events.ScheduleEvent(EVENT_GOSSIP_CHECK, 2s);

        if (_instance && _instance->GetBossState(DATA_ULTRAXION) != DONE)
            StopWards();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_instance || !who->IsPlayer() || who->ToPlayer()->IsGameMaster() || !me->IsWithinDist(who, 45.0f))
            return;

        bool mawsDone = _instance->GetBossState(DATA_WARLORD_ZONOZZ) == DONE && _instance->GetBossState(DATA_YORSAHJ_THE_UNSLEEPING) == DONE;
        bool hagaraDone = _instance->GetBossState(DATA_HAGARA_THE_STORMBINDER) == DONE;
        bool ultraxionDone = _instance->GetBossState(DATA_ULTRAXION) == DONE;

        if (!_preHagaraTalkDone && mawsDone && !hagaraDone)
        {
            _preHagaraTalkDone = true;
            StartConversation(CONV_PRE_HAGARA);
        }
        else if (!_postHagaraTalkDone && hagaraDone && !ultraxionDone && _state == RITUAL_IDLE
            && _instance->GetData(DATA_ULTRAXION_TRASH) != DONE)
        {
            _postHagaraTalkDone = true;
            StartConversation(CONV_POST_HAGARA);
        }
    }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (CanStartRitual())
        {
            if (_instance->GetData(DATA_ULTRAXION_TRASH) == DONE)
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Nous sommes prêts à affronter Ultraxion.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            else
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Vous pouvez commencer le rituel, nous vous défendrons.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        }
        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 action = GetGossipActionFor(player, gossipListId);
        CloseGossipMenuFor(player);

        if (!CanStartRitual())
            return true;

        _events.CancelEvent(EVENT_CONVERSATION);
        StartWards();
        SummonDeathwing();
        me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);

        if (action == GOSSIP_ACTION_INFO_DEF + 2)
        {
            // le trash est déjà fait : Ultraxion revient directement
            _state = RITUAL_ULTRAXION;
            if (Creature* ultraxion = SummonUltraxion())
                ultraxion->AI()->DoAction(ACTION_ULTRAXION_ARRIVE_SHORT);
        }
        else
        {
            _state = RITUAL_TRASH;
            AnnounceRemaining();
            if (Creature* ultraxion = SummonUltraxion())
                ultraxion->AI()->DoAction(ACTION_START_RITUAL);
            _events.ScheduleEvent(EVENT_SPAWN_ASSAULTER, 8s);
            _events.ScheduleEvent(EVENT_CHECK_WIPE, 10s);
        }
        return true;
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        _summons.Despawn(summon);
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() != NPC_TWILIGHT_ASSAULTER || _state != RITUAL_TRASH)
            return;

        ++_killed;
        AnnounceRemaining();
        if (_killed >= ASSAULTERS_TOTAL)
        {
            _state = RITUAL_ULTRAXION;
            _events.CancelEvent(EVENT_SPAWN_ASSAULTER);
            if (_instance)
                _instance->SetData(DATA_ULTRAXION_TRASH, DONE);
            _events.ScheduleEvent(EVENT_ULTRAXION_ARRIVE, 5s);
        }
        else if (_spawned < ASSAULTERS_TOTAL)
            _events.ScheduleEvent(EVENT_SPAWN_ASSAULTER, 6s, 10s);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_ULTRAXION_WIPE:
                _summons.DespawnEntry(NPC_DEATHWING_SUMMIT);
                _events.Reset();
                _events.ScheduleEvent(EVENT_GOSSIP_CHECK, 2s);
                _summons.DespawnEntry(NPC_TWILIGHT_ASSAULTER);
                _state = RITUAL_IDLE;
                StopWards();
                UpdateGossipFlag();
                break;
            case ACTION_ULTRAXION_DEAD:
                _summons.DespawnEntry(NPC_DEATHWING_SUMMIT);
                _events.Reset();
                _state = RITUAL_IDLE;
                StopWards();
                UpdateGossipFlag();
                _events.ScheduleEvent(EVENT_OUTRO, 10s);
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
                case EVENT_CONVERSATION:
                    NextConversationLine();
                    break;
                case EVENT_GOSSIP_CHECK:
                    if (_state == RITUAL_IDLE)
                        UpdateGossipFlag();
                    _events.Repeat(2s);
                    break;
                case EVENT_SPAWN_ASSAULTER:
                    SpawnAssaulters();
                    break;
                case EVENT_CHECK_WIPE:
                    if (_state == RITUAL_TRASH && GetSummitPlayers(me->GetMap()).empty())
                    {
                        // tout le raid est mort ou parti : on recommence la vague en cours
                        _spawned = _killed;
                        DoAction(ACTION_ULTRAXION_WIPE);
                        break;
                    }
                    _events.Repeat(5s);
                    break;
                case EVENT_ULTRAXION_ARRIVE:
                    if (Creature* ultraxion = _instance ? _instance->GetCreature(DATA_ULTRAXION) : nullptr)
                        ultraxion->AI()->DoAction(ACTION_ULTRAXION_ARRIVE);
                    else if (Creature* summoned = SummonUltraxion())
                        summoned->AI()->DoAction(ACTION_ULTRAXION_ARRIVE);
                    break;
                case EVENT_DEATHWING_TWISTING:
                    ZoneTalk(Aspect(DATA_DEATHWING_SUMMIT), SAY_DW_TWISTING_WHELPS);
                    break;
                case EVENT_OUTRO:
                    if (_outroStep == 0)
                    {
                        ZoneTalk(Aspect(DATA_ALEXSTRASZA_SUMMIT), SAY_ALEX_IT_IS_DONE);
                        _outroStep = 1;
                        _events.ScheduleEvent(EVENT_OUTRO, 12s);
                    }
                    else
                        ZoneTalk(me, SAY_THRALL_ULTRAXION_DEAD);
                    break;
                default:
                    break;
            }
        }
    }

private:
    bool CanStartRitual() const
    {
        return _instance && _state == RITUAL_IDLE
            && _instance->GetBossState(DATA_HAGARA_THE_STORMBINDER) == DONE
            && _instance->GetBossState(DATA_ULTRAXION) != DONE;
    }

    void UpdateGossipFlag()
    {
        if (CanStartRitual())
            me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        else
            me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    // décompte au centre de l'écran pour tout le raid
    void AnnounceRemaining()
    {
        uint32 remaining = _killed < ASSAULTERS_TOTAL ? ASSAULTERS_TOTAL - _killed : 0;
        for (auto const& ref : me->GetMap()->GetPlayers())
            if (Player* player = ref.GetSource())
            {
                if (remaining)
                    me->Whisper(Trinity::StringFormat("Assaillants du Crépuscule restants : %u / %u", remaining, ASSAULTERS_TOTAL), LANG_UNIVERSAL, player, true);
                else
                    me->Whisper("Tous les Assaillants du Crépuscule sont morts !", LANG_UNIVERSAL, player, true);
            }
    }

    Creature* Aspect(uint32 data) const
    {
        return _instance ? _instance->GetCreature(data) : nullptr;
    }

    void StartWards()
    {
        static std::pair<uint32, uint32> const wards[] =
        {
            { DATA_THRALL_SUMMIT,       SPELL_WARD_OF_EARTH  },
            { DATA_YSERA_SUMMIT,        SPELL_WARD_OF_DREAMS },
            { DATA_ALEXSTRASZA_SUMMIT,  SPELL_WARD_OF_LIFE   },
            { DATA_KALECGOS_SUMMIT,     SPELL_WARD_OF_MAGIC  },
            { DATA_NOZDORMU_SUMMIT,     SPELL_WARD_OF_TIME   }
        };
        for (auto const& ward : wards)
            if (Creature* aspect = Aspect(ward.first))
                aspect->CastSpell(aspect, ward.second, true);
    }

    void StopWards()
    {
        static std::pair<uint32, uint32> const wards[] =
        {
            { DATA_THRALL_SUMMIT,       SPELL_WARD_OF_EARTH  },
            { DATA_YSERA_SUMMIT,        SPELL_WARD_OF_DREAMS },
            { DATA_ALEXSTRASZA_SUMMIT,  SPELL_WARD_OF_LIFE   },
            { DATA_KALECGOS_SUMMIT,     SPELL_WARD_OF_MAGIC  },
            { DATA_NOZDORMU_SUMMIT,     SPELL_WARD_OF_TIME   }
        };
        for (auto const& ward : wards)
            if (Creature* aspect = Aspect(ward.first))
            {
                aspect->InterruptNonMeleeSpells(true);
                aspect->RemoveAurasDueToSpell(ward.second);
            }
    }

    // Aile de mort tourne autour du temple pendant tout le rituel
    void SummonDeathwing()
    {
        if (Creature* deathwing = Aspect(DATA_DEATHWING_SUMMIT))
            if (deathwing->IsAlive())
                return;
        me->SummonCreature(NPC_DEATHWING_SUMMIT, DeathwingCirclePos, TEMPSUMMON_MANUAL_DESPAWN);
    }

    Creature* SummonUltraxion()
    {
        if (_instance)
            if (Creature* existing = _instance->GetCreature(DATA_ULTRAXION))
                if (existing->IsAlive())
                    return existing;

        return me->SummonCreature(BOSS_ULTRAXION, UltraxionSpawnPos, TEMPSUMMON_MANUAL_DESPAWN);
    }

    void SpawnAssaulters()
    {
        if (_state != RITUAL_TRASH)
            return;

        uint32 maxActive = me->GetMap()->Is25ManRaid() ? 3 : 2;
        uint32 alive = 0;
        for (ObjectGuid const& guid : _summons)
            if (Creature* summon = ObjectAccessor::GetCreature(*me, guid))
                if (summon->GetEntry() == NPC_TWILIGHT_ASSAULTER && summon->IsAlive())
                    ++alive;

        bool firstWave = _spawned == 0;
        while (alive < maxActive && _spawned < ASSAULTERS_TOTAL)
        {
            float angle = frand(0.0f, 2.0f * float(M_PI));
            Position spawnPos;
            spawnPos.Relocate(SummitCenter.GetPositionX() + 110.0f * std::cos(angle),
                SummitCenter.GetPositionY() + 110.0f * std::sin(angle),
                ASSAULTER_CIRCLE_Z + 25.0f, angle + float(M_PI));
            if (me->SummonCreature(NPC_TWILIGHT_ASSAULTER, spawnPos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s))
            {
                ++alive;
                ++_spawned;
            }
            else
                break;
        }

        if (firstWave)
        {
            ZoneTalk(Aspect(DATA_ALEXSTRASZA_SUMMIT), SAY_ALEX_MY_CLUTCH);
            _events.ScheduleEvent(EVENT_DEATHWING_TWISTING, 6s);
        }
    }

    // ---- conversations -------------------------------------------------------
    void StartConversation(uint8 firstStep)
    {
        _convStep = firstStep;
        _events.ScheduleEvent(EVENT_CONVERSATION, 2s);
    }

    void NextConversationLine()
    {
        Milliseconds next = 0ms;
        switch (_convStep)
        {
            // Avant Hagara : le Dragon Soul est trop puissant, Kalecgos propose l'Iris de focalisation
            case CONV_PRE_HAGARA:     ZoneTalk(me, SAY_THRALL_SOUL_TOO_GREAT); next = 10s; break;
            case CONV_PRE_HAGARA + 1: ZoneTalk(Aspect(DATA_KALECGOS_SUMMIT), SAY_KALEC_NOT_NECESSARILY); next = 6s; break;
            case CONV_PRE_HAGARA + 2: ZoneTalk(Aspect(DATA_KALECGOS_SUMMIT), SAY_KALEC_FOCUSING_IRIS); next = 9s; break;
            case CONV_PRE_HAGARA + 3: ZoneTalk(me, SAY_THRALL_I_SEE); next = 6s; break;
            case CONV_PRE_HAGARA + 4: ZoneTalk(Aspect(DATA_KALECGOS_SUMMIT), SAY_KALEC_OPEN_THE_WAY); break;

            // Après Hagara : préparation du rituel
            case CONV_POST_HAGARA:     ZoneTalk(Aspect(DATA_KALECGOS_SUMMIT), SAY_KALEC_BEGIN_RITUAL); next = 5s; break;
            case CONV_POST_HAGARA + 1: ZoneTalk(Aspect(DATA_ALEXSTRASZA_SUMMIT), SAY_ALEX_RITUAL_COST); next = 8s; break;
            case CONV_POST_HAGARA + 2: ZoneTalk(Aspect(DATA_YSERA_SUMMIT), SAY_YSERA_TRAP); next = 7s; break;
            case CONV_POST_HAGARA + 3: ZoneTalk(Aspect(DATA_NOZDORMU_SUMMIT), SAY_NOZ_NOT_UNEXPECTED); next = 4s; break;
            case CONV_POST_HAGARA + 4: ZoneTalk(Aspect(DATA_ALEXSTRASZA_SUMMIT), SAY_ALEX_LOST); next = 4s; break;
            case CONV_POST_HAGARA + 5: ZoneTalk(me, SAY_THRALL_NO_CHOICE); next = 6s; break;
            case CONV_POST_HAGARA + 6: ZoneTalk(me, SAY_THRALL_BURDEN); next = 5s; break;
            case CONV_POST_HAGARA + 7: ZoneTalk(me, SAY_THRALL_PROTECT_US); next = 7s; break;
            case CONV_POST_HAGARA + 8: ZoneTalk(me, SAY_THRALL_SPEAK_TO_ME); break;
            default:
                break;
        }

        ++_convStep;
        if (next > 0ms)
            _events.ScheduleEvent(EVENT_CONVERSATION, next);
    }

    InstanceScript* _instance;
    SummonList _summons;
    EventMap _events;
    RitualState _state = RITUAL_IDLE;
    uint32 _spawned = 0;
    uint32 _killed = 0;
    uint8 _convStep = 0;
    uint8 _outroStep = 0;
    bool _preHagaraTalkDone = false;
    bool _postHagaraTalkDone = false;
};

// ===========================================================================
// Aile de mort : survole le temple en cercle, intouchable, et commente le rituel
// ===========================================================================
struct npc_ds_deathwing_summit : public ScriptedAI
{
    npc_ds_deathwing_summit(Creature* creature) : ScriptedAI(creature) { }

    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetAnimTier(AnimTier::Fly);
        me->setActive(true);
    }

    void Reset() override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        // dans la base il a 0 PV (multiplicateur de vie nul) : il apparaît comme un cadavre.
        // on lui donne de la vie et on le remet debout, en vol
        if (me->GetMaxHealth() <= 1)
            me->SetMaxHealth(100000000);
        me->SetFullHealth();
        if (me->isDead())
            me->setDeathState(ALIVE);
        me->SetStandState(UNIT_STAND_STATE_STAND);
        me->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
        me->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        me->RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);
        me->SetAnimTier(AnimTier::Fly);
        me->GetMotionMaster()->MoveCirclePath(SummitCenter.GetPositionX(), SummitCenter.GetPositionY(), DEATHWING_CIRCLE_Z,
            DEATHWING_CIRCLE_RADIUS, true, 16);
    }

    void UpdateAI(uint32 /*diff*/) override { }
};

// ===========================================================================
// Assaillant du Crépuscule : tourne au-dessus du sommet, se pose quand on le provoque
// ===========================================================================
struct npc_ds_twilight_assaulter : public ScriptedAI
{
    npc_ds_twilight_assaulter(Creature* creature) : ScriptedAI(creature) { }

    void InitializeAI() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
        me->SetReactState(REACT_PASSIVE);
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        _airborne = true;
        _landing = false;
    }

    void Reset() override { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        // approche puis cercle autour du sommet
        float angle = SummitCenter.GetAngle(me);
        Position entry;
        entry.Relocate(SummitCenter.GetPositionX() + ASSAULTER_CIRCLE_RADIUS * std::cos(angle),
            SummitCenter.GetPositionY() + ASSAULTER_CIRCLE_RADIUS * std::sin(angle), ASSAULTER_CIRCLE_Z);
        me->GetMotionMaster()->MovePoint(POINT_ASSAULTER_CIRCLE, entry, false);

        _events.ScheduleEvent(EVENT_AIR_FLAMES, 4s, 6s);
        _events.ScheduleEvent(EVENT_AUTO_LAND, 50s, 60s);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        // en l'air : on ne revient pas « au point de spawn », on continue de tourner
        if (_airborne)
            return;
        ScriptedAI::EnterEvadeMode(why);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;

        if (pointId == POINT_ASSAULTER_CIRCLE && _airborne && !_landing)
            me->GetMotionMaster()->MoveCirclePath(SummitCenter.GetPositionX(), SummitCenter.GetPositionY(), ASSAULTER_CIRCLE_Z,
                ASSAULTER_CIRCLE_RADIUS, urand(0, 1) != 0, 12);
        else if (pointId == POINT_ASSAULTER_LAND)
            _events.ScheduleEvent(EVENT_GROUND_FLAMES, 0ms); // atterri : on passe au sol au prochain tick
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        // provocation d'un tank : le drake descend sur le sommet
        if (!_airborne || _landing || !caster || !caster->IsPlayer())
            return;
        if (spellInfo->HasEffect(SPELL_EFFECT_ATTACK_ME) || spellInfo->HasAura(SPELL_AURA_MOD_TAUNT))
            Land(caster->ToUnit());
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (_airborne)
        {
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_AIR_FLAMES:
                        if (!_landing)
                            if (Player* target = RandomSummitPlayer())
                                me->CastSpell(target, SPELL_ASSAULTER_TWILIGHT_FLAMES, true);
                        _events.Repeat(7s, 10s);
                        break;
                    case EVENT_AUTO_LAND:
                        Land(nullptr);
                        break;
                    case EVENT_GROUND_FLAMES:
                        OnLanded();
                        break;
                    default:
                        break;
                }
            }
            return;
        }

        if (!UpdateVictim())
            return;

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BREATH:
                    DoCastVictim(SPELL_ASSAULTER_TWILIGHT_BREATH);
                    _events.Repeat(9s, 12s);
                    break;
                case EVENT_GROUND_FLAMES:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 1, 40.0f, true))
                        DoCast(target, SPELL_ASSAULTER_TWILIGHT_FLAMES);
                    _events.Repeat(10s, 14s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    Player* RandomSummitPlayer() const
    {
        std::vector<Player*> players = GetSummitPlayers(me->GetMap(), 60.0f);
        if (players.empty())
            return nullptr;
        return Trinity::Containers::SelectRandomContainerElement(players);
    }

    void Land(Unit* taunter)
    {
        if (_landing)
            return;
        _landing = true;
        _taunter = taunter ? taunter->GetGUID() : ObjectGuid::Empty;
        _events.CancelEvent(EVENT_AIR_FLAMES);
        _events.CancelEvent(EVENT_AUTO_LAND);

        // atterrissage sur le bord du sommet, du côté où se trouve le drake
        float angle = SummitCenter.GetAngle(me);
        Position landPos;
        landPos.Relocate(SummitCenter.GetPositionX() + ASSAULTER_LAND_RADIUS * std::cos(angle),
            SummitCenter.GetPositionY() + ASSAULTER_LAND_RADIUS * std::sin(angle), SummitCenter.GetPositionZ(), angle + float(M_PI));
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveLand(POINT_ASSAULTER_LAND, landPos);
    }

    void OnLanded()
    {
        _airborne = false;
        me->SetDisableGravity(false);
        me->SetCanFly(false);
        me->SetReactState(REACT_AGGRESSIVE);
        _events.Reset();
        _events.ScheduleEvent(EVENT_BREATH, 2s, 4s);
        _events.ScheduleEvent(EVENT_GROUND_FLAMES, 5s, 7s);

        Unit* target = _taunter.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*me, _taunter);
        if (!target || !target->IsAlive())
            target = RandomSummitPlayer();
        if (target)
        {
            AddThreat(target, 100000.0f);
            AttackStart(target);
        }
        DoZoneInCombat();
    }

    EventMap _events;
    ObjectGuid _taunter;
    bool _airborne = true;
    bool _landing = false;
};

// ===========================================================================
// Ultraxion
// ===========================================================================
struct boss_ultraxion : public BossAI
{
    boss_ultraxion(Creature* creature) : BossAI(creature, DATA_ULTRAXION)
    {
        SetCombatMovement(false);
    }

    void InitializeAI() override
    {
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->SetVisible(false);
        AddTwilightPhase();
        _unstableCount = 0;
    }

    void Reset() override { }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_START_RITUAL:
                ZoneTalk(Deathwing(), SAY_DW_GOOD_TO_SEE_YOU);
                break;
            case ACTION_ULTRAXION_ARRIVE:
                if (_arriving)
                    break;
                _arriving = true;
                ZoneTalk(Deathwing(), SAY_DW_MERE_WHELPS);
                ShowAtSpawn();
                _introStep = 0;
                events.ScheduleEvent(EVENT_INTRO, 8s);
                break;
            case ACTION_ULTRAXION_ARRIVE_SHORT:
                if (_arriving)
                    break;
                _arriving = true;
                _introStep = 1;
                ShowAtSpawn();
                events.ScheduleEvent(EVENT_INTRO, 2s);
                break;
            case ACTION_ULTRAXION_ERUPTION:
                _unstableCount = 6;
                events.RescheduleEvent(EVENT_UNSTABLE_MONSTROSITY, 1s);
                break;
            default:
                break;
        }
    }

    void AttackStart(Unit* victim) override
    {
        if (victim && me->Attack(victim, true))
            DoStartNoMovement(victim);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_ULTRAXION_KILL);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (!_fightStarted)
            return;

        CleanupEncounter();
        if (instance)
            instance->SetBossState(DATA_ULTRAXION, FAIL);
        if (Creature* thrall = instance ? instance->GetCreature(DATA_THRALL_SUMMIT) : nullptr)
            thrall->AI()->DoAction(ACTION_ULTRAXION_WIPE);
        me->DespawnOrUnsummon();
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_ULTRAXION_DEATH);
        CleanupEncounter();
        // son corps resterait suspendu en l'air : il disparaît, le butin est dans la cache
        me->DespawnOrUnsummon(4s);

        uint32 cacheEntry = GO_LESSER_CACHE_10N;
        switch (me->GetMap()->GetDifficulty())
        {
            case RAID_DIFFICULTY_25MAN_NORMAL: cacheEntry = GO_LESSER_CACHE_25N; break;
            case RAID_DIFFICULTY_10MAN_HEROIC: cacheEntry = GO_LESSER_CACHE_10H; break;
            case RAID_DIFFICULTY_25MAN_HEROIC: cacheEntry = GO_LESSER_CACHE_25H; break;
            default: break;
        }
        if (GameObject* cache = me->SummonGameObject(cacheEntry, LootCachePos, QuaternionData::fromEulerAnglesZYX(LootCachePos.GetOrientation(), 0.0f, 0.0f), WEEK))
        {
            // la cache doit être ouvrable par le raid (le sniff lui donne des drapeaux « verrouillé / non sélectionnable »)
            cache->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED | GO_FLAG_NOT_SELECTABLE | GO_FLAG_INTERACT_COND | GO_FLAG_IN_USE);
        }

        if (Creature* thrall = instance->GetCreature(DATA_THRALL_SUMMIT))
            thrall->AI()->DoAction(ACTION_ULTRAXION_DEAD);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if ((type == POINT_MOTION_TYPE || type == EFFECT_MOTION_TYPE) && pointId == POINT_ULTRAXION_ARRIVED)
        {
            _arrived = true;
            me->SetFacingTo(UltraxionFightPos.GetOrientation());
            // les éclairs cosmétiques seulement une fois en place (un sort lancé coupe le vol)
            if (!_fightStarted)
                events.ScheduleEvent(EVENT_INTRO_LIGHTNING, 500ms);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (!_fightStarted)
        {
            while (uint32 eventId = events.ExecuteEvent())
            {
                if (eventId == EVENT_INTRO)
                    NextIntroStep();
                else if (eventId == EVENT_INTRO_LIGHTNING)
                {
                    static uint32 const lightning[] = { SPELL_COSMETIC_LIGHTNING_1, SPELL_COSMETIC_LIGHTNING_2, SPELL_COSMETIC_LIGHTNING_3 };
                    DoCastSelf(lightning[urand(0, 2)], true);
                    events.Repeat(2s, 4s);
                }
            }
            return;
        }

        if (!UpdateVictim())
            return;

        // la cible qui utilise Volonté héroïque sort du crépuscule : on frappe quelqu'un d'autre
        if (me->GetVictim() && me->GetVictim()->HasAura(SPELL_HEROIC_WILL))
            if (Unit* target = SelectTarget(SELECT_TARGET_MAXTHREAT, 0, 0.0f, true, true, -int32(SPELL_HEROIC_WILL)))
                AttackStart(target);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_UNSTABLE_MONSTROSITY:
                    UnstableMonstrosity();
                    break;
                case EVENT_HOUR_OF_TWILIGHT:
                    Talk(SAY_ULTRAXION_HOUR);
                    DoCastAOE(SPELL_HOUR_OF_TWILIGHT);
                    events.Repeat(45s);
                    events.RescheduleEvent(EVENT_FADING_LIGHT, 13s);
                    break;
                case EVENT_FADING_LIGHT:
                    FadingLight();
                    events.Repeat(IsHeroic() ? 10s : 15s);
                    break;
                case EVENT_CHECK_MELEE:
                    if (me->GetVictim() && !me->IsWithinMeleeRange(me->GetVictim()))
                        DoCastAOE(SPELL_TWILIGHT_BURST);
                    events.Repeat(4s);
                    break;
                case EVENT_CHECK_TWILIGHT:
                    KeepPlayersInTwilight();
                    events.Repeat(2s);
                    break;
                case EVENT_THRALL_LAST_DEFENDER:
                    LastDefenderOfAzeroth();
                    break;
                case EVENT_ALEX_GIFT_OF_LIFE:
                    if (Creature* alex = instance->GetCreature(DATA_ALEXSTRASZA_SUMMIT))
                    {
                        ZoneTalk(alex, SAY_ALEX_GIFT_OF_LIFE);
                        SpawnBuffOrbs(alex, GO_GIFT_OF_LIFE);
                    }
                    break;
                case EVENT_YSERA_ESSENCE:
                    if (Creature* ysera = instance->GetCreature(DATA_YSERA_SUMMIT))
                    {
                        ZoneTalk(ysera, SAY_YSERA_ESSENCE);
                        SpawnBuffOrbs(ysera, GO_ESSENCE_OF_DREAMS);
                    }
                    break;
                case EVENT_KALEC_SOURCE:
                    if (Creature* kalec = instance->GetCreature(DATA_KALECGOS_SUMMIT))
                    {
                        ZoneTalk(kalec, SAY_KALEC_SOURCE_OF_MAGIC);
                        SpawnBuffOrbs(kalec, GO_SOURCE_OF_MAGIC);
                    }
                    break;
                case EVENT_NOZ_TIMELOOP:
                    if (Creature* noz = instance->GetCreature(DATA_NOZDORMU_SUMMIT))
                    {
                        ZoneTalk(noz, SAY_NOZ_TIMELOOP);
                        for (Player* player : GetSummitPlayers(me->GetMap()))
                            noz->AddAura(SPELL_TIMELOOP, player);
                    }
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
    void NextIntroStep()
    {
        switch (_introStep)
        {
            case 0:
                if (Creature* ysera = instance->GetCreature(DATA_YSERA_SUMMIT))
                    ZoneTalk(ysera, SAY_YSERA_DISTURBANCE);
                events.ScheduleEvent(EVENT_INTRO, 7s);
                break;
            case 1:
                // Ultraxion surgit de sous le temple et monte en volant jusqu'à sa place (~7 s)
                ShowAtSpawn();
                me->GetMotionMaster()->MovePoint(POINT_ULTRAXION_ARRIVED, UltraxionFightPos, false, 10.0f);
                ZoneTalk(Deathwing(), SAY_DW_ULTIMATE_CREATION);
                events.ScheduleEvent(EVENT_INTRO, 8s);
                break;
            case 2:
                ZoneTalk(me, SAY_ULTRAXION_INTRO_1);
                events.ScheduleEvent(EVENT_INTRO, 12s);
                break;
            case 3:
                ZoneTalk(me, SAY_ULTRAXION_INTRO_2);
                events.ScheduleEvent(EVENT_INTRO, 10s);
                break;
            case 4:
                StartFight();
                break;
            default:
                break;
        }
        ++_introStep;
    }

    // visible et en vol sous le bord du sommet avant de monter (le client voit ainsi tout le trajet)
    void ShowAtSpawn()
    {
        if (!me->IsVisible())
            me->SetVisible(true);
        // la base le marque « immobile » (enraciné) : sans ça il ne peut pas monter et finit téléporté
        me->SetControlled(false, UNIT_STATE_ROOT);
        me->ClearUnitState(UNIT_STATE_ROOT);
        KeepFlying();
    }

    Creature* Deathwing() const
    {
        return instance ? instance->GetCreature(DATA_DEATHWING_SUMMIT) : nullptr;
    }

    void StartFight()
    {
        events.Reset();
        _fightStarted = true;

        // sécurité : s'il n'est pas arrivé à sa place, on l'y met avant le combat (il ne bouge plus ensuite)
        // on se fie à sa position réelle, pas seulement à l'avis du générateur de mouvement
        if (!_arrived || me->GetExactDist(UltraxionFightPos) > 5.0f)
        {
            me->StopMoving();
            me->NearTeleportTo(UltraxionFightPos.GetPositionX(), UltraxionFightPos.GetPositionY(), UltraxionFightPos.GetPositionZ(), UltraxionFightPos.GetOrientation());
            _arrived = true;
        }
        KeepFlying();
        _unstableCount = 0;
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->SetReactState(REACT_AGGRESSIVE);

        instance->SetBossState(DATA_ULTRAXION, IN_PROGRESS);
        me->setActive(true);
        Talk(SAY_ULTRAXION_AGGRO);
        if (Creature* thrall = instance->GetCreature(DATA_THRALL_SUMMIT))
            ZoneTalk(thrall, SAY_THRALL_ASPECTS_AID);

        // tout le raid passe dans le royaume du Crépuscule et reçoit Volonté héroïque
        DoCastAOE(SPELL_TWILIGHT_SHIFT_AOE, true);
        DoCastAOE(SPELL_HEROIC_WILL_AOE, true);
        KeepPlayersInTwilight();
        DoZoneInCombat();

        events.ScheduleEvent(EVENT_UNSTABLE_MONSTROSITY, 1s);
        events.ScheduleEvent(EVENT_HOUR_OF_TWILIGHT, 45s);
        events.ScheduleEvent(EVENT_FADING_LIGHT, 13s);
        events.ScheduleEvent(EVENT_CHECK_MELEE, 6s);
        events.ScheduleEvent(EVENT_CHECK_TWILIGHT, 2s);
        events.ScheduleEvent(EVENT_THRALL_LAST_DEFENDER, 5s);
        events.ScheduleEvent(EVENT_ALEX_GIFT_OF_LIFE, 80s);
        events.ScheduleEvent(EVENT_YSERA_ESSENCE, 155s);
        events.ScheduleEvent(EVENT_KALEC_SOURCE, 215s);
        events.ScheduleEvent(EVENT_NOZ_TIMELOOP, 5min);
    }

    // il plane en permanence (animation de vol, pas de « sol invisible »)
    void KeepFlying()
    {
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetAnimTier(AnimTier::Fly);
        me->SetPlayHoverAnim(true);
    }

    // Ultraxion reste visible pour les joueurs passés dans le royaume du Crépuscule
    void AddTwilightPhase()
    {
        for (uint32 spellId : { uint32(SPELL_TWILIGHT_SHIFT), uint32(SPELL_TWILIGHT_SHIFT_AOE) })
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
                for (SpellEffectInfo const& effect : info->Effects)
                {
                    if (effect.ApplyAuraName == SPELL_AURA_PHASE)
                        PhasingHandler::AddPhase(me, uint32(effect.MiscValueB), true);
                    else if (effect.ApplyAuraName == SPELL_AURA_PHASE_GROUP)
                        PhasingHandler::AddPhaseGroup(me, uint32(effect.MiscValueB), true);
                }
    }

    void KeepPlayersInTwilight()
    {
        for (Player* player : GetSummitPlayers(me->GetMap()))
        {
            if (!player->HasAura(SPELL_HEROIC_WILL) && !player->HasAura(SPELL_TWILIGHT_SHIFT))
                me->AddAura(SPELL_TWILIGHT_SHIFT, player);
            if (!player->HasAura(SPELL_HEROIC_WILL_AOE) && !player->HasSpell(SPELL_HEROIC_WILL))
                me->AddAura(SPELL_HEROIC_WILL_AOE, player);
        }
    }

    void UnstableMonstrosity()
    {
        static uint32 const stacks[] =
        {
            SPELL_UNSTABLE_MONSTROSITY_1, SPELL_UNSTABLE_MONSTROSITY_2, SPELL_UNSTABLE_MONSTROSITY_3,
            SPELL_UNSTABLE_MONSTROSITY_4, SPELL_UNSTABLE_MONSTROSITY_5, SPELL_UNSTABLE_MONSTROSITY_6
        };

        if (_unstableCount > 0 && _unstableCount <= 6)
            me->RemoveAurasDueToSpell(stacks[_unstableCount - 1]);

        ++_unstableCount;
        if (_unstableCount <= 6)
        {
            if (_unstableCount == 4)
                Talk(SAY_ULTRAXION_UNSTABLE);
            DoCastSelf(stacks[_unstableCount - 1], true);
            events.ScheduleEvent(EVENT_UNSTABLE_MONSTROSITY, 1min);
        }
        else
        {
            // 6 minutes : Éruption crépusculaire, le raid meurt
            Talk(SAY_ULTRAXION_ERUPTION);
            DoCastAOE(SPELL_TWILIGHT_ERUPTION);
        }
    }

    // Lumière déclinante : le tank + 1/2/3/6 DPS (10N/10H/25N/25H)
    void FadingLight()
    {
        Unit* tank = me->GetVictim();
        if (tank)
            me->AddAura(SPELL_FADING_LIGHT, tank);

        uint32 count = 1;
        switch (me->GetMap()->GetDifficulty())
        {
            case RAID_DIFFICULTY_10MAN_HEROIC: count = 2; break;
            case RAID_DIFFICULTY_25MAN_NORMAL: count = 3; break;
            case RAID_DIFFICULTY_25MAN_HEROIC: count = 6; break;
            default: break;
        }

        std::vector<Player*> dps;
        for (Player* player : GetSummitPlayers(me->GetMap()))
            if (player != tank && !IsTankSpec(player) && !IsHealerSpec(player) && !player->HasAura(SPELL_FADING_LIGHT))
                dps.push_back(player);

        Trinity::Containers::RandomShuffle(dps);
        for (uint32 i = 0; i < count && i < dps.size(); ++i)
            me->AddAura(SPELL_FADING_LIGHT, dps[i]);
    }

    // Thrall : Dernier défenseur d'Azeroth sur les tanks
    void LastDefenderOfAzeroth()
    {
        Creature* thrall = instance->GetCreature(DATA_THRALL_SUMMIT);
        if (!thrall)
            return;

        ZoneTalk(thrall, SAY_THRALL_LAST_DEFENDER);
        thrall->CastSpell(thrall, SPELL_LAST_DEFENDER_OF_AZEROTH, true);

        for (Player* player : GetSummitPlayers(me->GetMap()))
        {
            if (!IsTankSpec(player))
                continue;
            uint32 spellId = 0;
            switch (player->getClass())
            {
                case CLASS_WARRIOR:      spellId = SPELL_LAST_DEFENDER_WARRIOR; break;
                case CLASS_PALADIN:      spellId = SPELL_LAST_DEFENDER_PALADIN; break;
                case CLASS_DEATH_KNIGHT: spellId = SPELL_LAST_DEFENDER_DK;      break;
                case CLASS_DRUID:        spellId = SPELL_LAST_DEFENDER_DRUID;   break;
                default: break;
            }
            if (spellId)
                player->CastSpell(player, spellId, true);
        }
    }

    // Don de vie / Essence des rêves / Source de magie : 1 orbe en 10, 2 en 25
    void SpawnBuffOrbs(Creature* aspect, uint32 goEntry)
    {
        auto spawn = [&](Position const& pos)
        {
            if (GameObject* go = aspect->SummonGameObject(goEntry, pos, QuaternionData::fromEulerAnglesZYX(0.0f, 0.0f, 0.0f), 600))
                _orbs.push_back(go->GetGUID());
        };

        if (me->GetMap()->Is25ManRaid())
        {
            spawn(BuffOrbPos[0]);
            spawn(BuffOrbPos[1]);
        }
        else
            spawn(BuffOrbPos[urand(0, 1)]);
    }

    void CleanupEncounter()
    {
        events.Reset();
        for (ObjectGuid const& guid : _orbs)
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, guid))
                go->Delete();
        _orbs.clear();

        static uint32 const encounterAuras[] =
        {
            SPELL_TWILIGHT_SHIFT, SPELL_HEROIC_WILL_AOE, SPELL_HEROIC_WILL, SPELL_FADING_LIGHT, SPELL_FADED_INTO_TWILIGHT,
            SPELL_LOOMING_DARKNESS, SPELL_GIFT_OF_LIFE_AURA, SPELL_ESSENCE_OF_DREAMS_AURA, SPELL_SOURCE_OF_MAGIC_AURA,
            SPELL_TIMELOOP, SPELL_LAST_DEFENDER_WARRIOR, SPELL_LAST_DEFENDER_PALADIN, SPELL_LAST_DEFENDER_DK, SPELL_LAST_DEFENDER_DRUID
        };
        if (instance)
            for (uint32 spellId : encounterAuras)
                instance->DoRemoveAurasDueToSpellOnPlayers(spellId);
    }

    std::vector<ObjectGuid> _orbs;
    uint8 _unstableCount = 0;
    uint8 _introStep = 0;
    bool _arriving = false;
    bool _arrived = false;
    bool _fightStarted = false;
};

// ===========================================================================
// Orbes des Aspects : un seul joueur par orbe, un seul bonus d'Aspect à la fois
// ===========================================================================
struct go_ds_ultraxion_buff_orb : public GameObjectAI
{
    go_ds_ultraxion_buff_orb(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* player) override
    {
        uint32 aura = 0;
        switch (me->GetEntry())
        {
            case GO_GIFT_OF_LIFE:       aura = SPELL_GIFT_OF_LIFE_AURA; break;
            case GO_ESSENCE_OF_DREAMS:  aura = SPELL_ESSENCE_OF_DREAMS_AURA; break;
            case GO_SOURCE_OF_MAGIC:    aura = SPELL_SOURCE_OF_MAGIC_AURA; break;
            default: return true;
        }

        for (uint32 other : { uint32(SPELL_GIFT_OF_LIFE_AURA), uint32(SPELL_ESSENCE_OF_DREAMS_AURA), uint32(SPELL_SOURCE_OF_MAGIC_AURA) })
            if (other != aura)
                player->RemoveAurasDueToSpell(other);

        player->CastSpell(player, aura, true);
        me->Delete();
        return true;
    }
};

// ===========================================================================
// Sorts
// ===========================================================================

// 106374 - Twilight Instability : ignore ceux qui sont sous Volonté héroïque
class spell_ultraxion_twilight_instability : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target)
        {
            Unit* unit = target->ToUnit();
            return !unit || unit->HasAura(SPELL_HEROIC_WILL);
        });
    }

    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            GetCaster()->CastSpell(target, SPELL_TWILIGHT_INSTABILITY_DAMAGE, true);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ultraxion_twilight_instability::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget.Register(&spell_ultraxion_twilight_instability::HandleHit, EFFECT_0, SPELL_EFFECT_ANY);
    }
};

// 106415 - Twilight Burst : pas sur ceux qui sont sortis du Crépuscule
class spell_ultraxion_twilight_burst : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target)
        {
            Unit* unit = target->ToUnit();
            return !unit || unit->HasAura(SPELL_HEROIC_WILL);
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ultraxion_twilight_burst::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// 103327 - Hour of Twilight : les dégâts sont partagés par ceux qui restent dans le Crépuscule.
// Pas assez de joueurs pour absorber (1 / 2 / 3 / 5) => Éruption crépusculaire.
class spell_ultraxion_hour_of_twilight_damage : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target)
        {
            Unit* unit = target->ToUnit();
            return !unit || !unit->IsPlayer() || unit->HasAura(SPELL_HEROIC_WILL);
        });

        Unit* caster = GetCaster();
        uint32 needed = 1;
        switch (caster->GetMap()->GetDifficulty())
        {
            case RAID_DIFFICULTY_10MAN_HEROIC: needed = 2; break;
            case RAID_DIFFICULTY_25MAN_NORMAL: needed = 3; break;
            case RAID_DIFFICULTY_25MAN_HEROIC: needed = 5; break;
            default: break;
        }

        if (targets.size() < needed)
            if (Creature* ultraxion = caster->ToCreature())
                if (ultraxion->IsAIEnabled())
                    ultraxion->AI()->DoAction(ACTION_ULTRAXION_ERUPTION);
    }

    void HandleLoomingDarkness(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        if (!target || !GetCaster()->GetMap()->IsHeroic())
            return;

        // héroïque : chaque Heure du Crépuscule encaissée ajoute Ténèbres menaçantes
        if (target->HasAura(SPELL_LOOMING_DARKNESS))
            target->CastSpell(target, SPELL_LOOMING_DARKNESS_DAMAGE, true);
        GetCaster()->AddAura(SPELL_LOOMING_DARKNESS, target);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ultraxion_hour_of_twilight_damage::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
        OnEffectHitTarget.Register(&spell_ultraxion_hour_of_twilight_damage::HandleLoomingDarkness, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// 105925 - Fading Light : 5 à 9 s (3 à 9 s en héroïque). À l'expiration :
// encore dans le Crépuscule => mort ; sorti grâce à Volonté héroïque => sauvé.
class spell_ultraxion_fading_light : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* owner = GetUnitOwner();
        int32 duration = int32(urand(owner->GetMap()->IsHeroic() ? 3000 : 5000, 9000));
        GetAura()->SetMaxDuration(duration);
        GetAura()->SetDuration(duration);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        auto removeMode = GetTargetApplication()->GetRemoveMode();
        if (!removeMode.HasFlag(AuraRemoveFlags::Expired))
            return;

        Unit* target = GetTarget();
        if (target->HasAura(SPELL_HEROIC_WILL))
        {
            target->CastSpell(target, SPELL_FADED_INTO_TWILIGHT, true);
            return;
        }
        if (target->HasAura(SPELL_TWILIGHT_SHIFT))
            target->CastSpell(target, SPELL_FADING_LIGHT_KILL, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_ultraxion_fading_light::AfterApply, EFFECT_0, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_ultraxion_fading_light::AfterRemove, EFFECT_0, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 105984 - Timeloop : un coup qui devrait tuer est annulé et le joueur est soigné entièrement (une fois)
class spell_ultraxion_timeloop : public AuraScript
{
    void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& /*canBeRecalculated*/)
    {
        amount = -1;
    }

    void Absorb(AuraEffect* aurEff, DamageInfo& dmgInfo, uint32& absorbAmount)
    {
        Unit* victim = GetTarget();
        if (dmgInfo.GetDamage() < victim->GetHealth())
        {
            absorbAmount = 0;
            return;
        }

        absorbAmount = dmgInfo.GetDamage();
        victim->CastSpell(victim, SPELL_TIMELOOP_HEAL, CastSpellExtraArgs(aurEff).AddSpellBP0(int32(victim->GetMaxHealth())));
        aurEff->GetBase()->Remove();
    }

    void Register() override
    {
        DoEffectCalcAmount.Register(&spell_ultraxion_timeloop::CalculateAmount, EFFECT_ALL, SPELL_AURA_SCHOOL_ABSORB);
        OnEffectAbsorb.Register(&spell_ultraxion_timeloop::Absorb, EFFECT_ALL);
    }
};
}

void AddSC_boss_ultraxion()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(npc_ds_thrall_summit);
    RegisterDragonSoulCreatureAI(npc_ds_twilight_assaulter);
    RegisterDragonSoulCreatureAI(npc_ds_deathwing_summit);
    RegisterDragonSoulCreatureAI(boss_ultraxion);
    RegisterGameObjectAI(go_ds_ultraxion_buff_orb);
    RegisterSpellScript(spell_ultraxion_twilight_instability);
    RegisterSpellScript(spell_ultraxion_twilight_burst);
    RegisterSpellScript(spell_ultraxion_hour_of_twilight_damage);
    RegisterSpellScript(spell_ultraxion_fading_light);
    RegisterSpellScript(spell_ultraxion_timeloop);
}
