/*
 * Azimov - Dragon Soul : Seigneur de guerre Corne-Noire (combat sur le Brûleciel)
 *
 *  - Capitaine Swayze au sommet (après Ultraxion) : téléporte sur le pont du Brûleciel.
 *  - Swayze sur le pont : « Rapprochez-nous » lance le combat.
 *  - Phase 1 : 3 vagues de 2 Drakes d'assaut du Crépuscule (Barrage crépusculaire sur le pont ;
 *    si personne n'intercepte, c'est le navire qui prend). Les harpons ramènent les drakes près du
 *    bastingage pour les tuer. Chaque drake dépose un Lame-d'effroi / Tueur d'élite.
 *    Goriona lance l'Assaut crépusculaire (à partager), en héroïque la Bordée sur le navire.
 *    Un Infiltré lâche régulièrement un Saboteur qui court vers la salle des machines.
 *  - Phase 2 (6 drakes morts) : Corne-Noire saute sur le pont. Vengeance, Dévaster, Rugissement
 *    perturbant, Onde de choc ; Goriona crache des Flammes du Crépuscule (héroïque : se pose à 90 %,
 *    Souffle crépusculaire, Voile dévorant) et s'enfuit à 20 %. Corne-Noire siphonne Goriona en héroïque.
 *  - Le Brûleciel détruit = wipe.
 *
 * Base : AshamaneCore (positions, sorts), réécrit pour ce core.
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureTextMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum BlackhornSpells
{
    // Corne-Noire
    SPELL_BERSERK                   = 26662,
    SPELL_VENGEANCE                 = 108045,
    SPELL_DEVASTATE                 = 108042,
    SPELL_DISRUPTING_ROAR           = 108044,
    SPELL_SHOCKWAVE                 = 108046,
    SPELL_SIPHON_VITALITY           = 110312,

    // Goriona
    SPELL_TWILIGHT_ONSLAUGHT_VISUAL = 107927,
    SPELL_TWILIGHT_ONSLAUGHT        = 107588,
    SPELL_TWILIGHT_ONSLAUGHT_DAMAGE = 106401,
    SPELL_BROADSIDE                 = 110153,
    SPELL_TWILIGHT_BREATH           = 110212,
    SPELL_CONSUMING_SHROUD          = 110214,
    SPELL_TWILIGHT_MISSILE          = 108050,

    // Drakes d'assaut
    SPELL_TWILIGHT_BARRAGE          = 107286,

    // Lame-d'effroi / Tueur
    SPELL_BLADE_RUSH                = 107594,
    SPELL_BRUTAL_STRIKE             = 107567,
    SPELL_DEGENERATION              = 107558,

    // Saboteur
    SPELL_DETONATE                  = 107518,
    SPELL_SMOKE_BOMB                = 107752,

    // Brûleciel
    SPELL_ENGINE_FIRE               = 107799,
    SPELL_MASSIVE_EXPLOSION         = 108132,
    SPELL_HARPOON                   = 108038
};

enum BlackhornNpcs
{
    NPC_SKY_CAPTAIN_SWAYZE          = 55870,
    NPC_THE_SKYFIRE                 = 56598,
    NPC_GORIONA                     = 56781,
    NPC_TWILIGHT_ASSAULT_DRAKE_1    = 56855, // côté tribord (y < -12130)
    NPC_TWILIGHT_ASSAULT_DRAKE_2    = 56587, // côté bâbord
    NPC_TWILIGHT_ELITE_DREADBLADE   = 56854,
    NPC_TWILIGHT_ELITE_SLAYER       = 56848,
    NPC_TWILIGHT_INFILTRATOR        = 56922,
    NPC_TWILIGHT_SAPPER             = 56923,
    NPC_ONSLAUGHT_TARGET            = 57238,
    NPC_ENGINE_STALKER              = 57190,
    NPC_TWILIGHT_FLAMES             = 57268, // flaques laissées par les Flammes du Crépuscule de Goriona
    NPC_SKYFIRE_HARPOON_GUN         = 56681
};

enum BlackhornActions
{
    ACTION_START_BATTLE     = 1,
    ACTION_RESET_BATTLE     = 2,
    ACTION_GORIONA_MOVE     = 3,
    ACTION_GORIONA_PHASE_2  = 4,
    ACTION_HARPOON          = 5,
    ACTION_HARPOONED        = 6,
    ACTION_HARPOON_END      = 7,
    ACTION_SAPPER           = 8,
    ACTION_SHIP_LOW         = 9,
    ACTION_SHIP_DESTROYED   = 10,
    ACTION_BLACKHORN_DIED   = 11,
    ACTION_INTRO_TALK       = 12,
    ACTION_ARMORED_DRAKE    = 13
};

enum BlackhornTexts
{
    // Corne-Noire
    SAY_BLACKHORN_INTRO     = 0,
    SAY_BLACKHORN_AGGRO     = 1,
    SAY_BLACKHORN_DEATH     = 2,
    SAY_BLACKHORN_GORIONA   = 3,
    SAY_BLACKHORN_PHASE_2   = 4,
    SAY_BLACKHORN_BERSERK   = 5,
    SAY_BLACKHORN_KILL      = 6,
    SAY_BLACKHORN_SHOCKWAVE = 7,
    SAY_BLACKHORN_ROAR      = 8,
    EMOTE_SAPPER            = 9,
    EMOTE_SIPHON            = 10,

    // Swayze
    SAY_SWAYZE_WELCOME      = 0,
    SAY_SWAYZE_ALL_AHEAD    = 1,
    SAY_SWAYZE_ENGINES_HIT  = 2,
    SAY_SWAYZE_STATIONS     = 3,
    SAY_SWAYZE_ARMORED      = 4,
    SAY_SWAYZE_SAPPER       = 5,
    SAY_SWAYZE_LOW_HEALTH   = 6,
    SAY_SWAYZE_GOING_DOWN   = 7,
    SAY_SWAYZE_VICTORY      = 8,

    // Goriona
    EMOTE_BROADSIDE         = 0,

    // Drakes
    EMOTE_TWILIGHT_BARRAGE  = 0
};

enum BlackhornEvents
{
    // Swayze
    EVENT_INTRO = 1,
    EVENT_CHECK_WIPE,
    EVENT_SHIP_EXPLOSION,
    EVENT_SUMMIT_VISIBILITY,

    // Corne-Noire
    EVENT_SUMMON_DRAKES,
    EVENT_HARPOON,
    EVENT_SUMMON_INFILTRATOR,
    EVENT_PHASE_2,
    EVENT_BERSERK,
    EVENT_DEVASTATE,
    EVENT_DISRUPTING_ROAR,
    EVENT_SHOCKWAVE,
    EVENT_SHOCKWAVE_END,
    EVENT_SIPHON_VITALITY,

    // Goriona
    EVENT_TWILIGHT_ONSLAUGHT,
    EVENT_BROADSIDE,
    EVENT_TWILIGHT_FLAMES,
    EVENT_TWILIGHT_BREATH,
    EVENT_CONSUMING_SHROUD,

    // Drakes / adds
    EVENT_DROP_ADD,
    EVENT_TWILIGHT_BARRAGE,
    EVENT_HARPOON_RELEASE,
    EVENT_BLADE_RUSH,
    EVENT_STACKING_STRIKE,
    EVENT_DROP_SAPPER,
    EVENT_DESPAWN,
    EVENT_SAPPER_RUN
};

enum BlackhornPoints
{
    POINT_DRAKE_HOLD        = 1,
    POINT_DRAKE_HARPOONED   = 2,
    POINT_GORIONA_HOVER     = 3,
    POINT_GORIONA_DECK      = 4,
    POINT_GORIONA_LAND      = 5,
    POINT_SAPPER_ENGINE     = 6,
    POINT_INFILTRATOR_END   = 7
};

// ---------------------------------------------------------------------------
// Positions (AshamaneCore)
// ---------------------------------------------------------------------------
static Position const SkyfireDeckPos     = { 13444.9f, -12133.3f, 151.21f, 0.0f };
static Position const SwayzeSummitPos    = { -1781.19f, -2375.12f, 341.35f, 4.43f }; // retour au sommet
static Position const SpineArrivalPos    = { -13854.67f, -13666.97f, 275.0f, 1.60f };  // dos d'Aile de mort
static Position const SpineBossPos       = { -13852.5f, -13665.38f, 297.3786f, 1.53589f };
static Position const MaelstromPos       = { -12081.39f, 12160.05f, 30.60f, 6.03f };  // plate-forme de la Folie
static Position const BlackhornDeckPos   = { 13465.208f, -12134.354f, 151.18f, 3.07f };
static Position const AddsPos[2]         =
{
    { 13460.484f, -12140.265f, 150.94f, 3.12f },
    { 13461.268f, -12125.924f, 150.91f, 3.12f }
};
static Position const GorionaPos[8]      =
{
    { 13535.675f, -12078.471f, 173.679f, 3.44f }, // apparition
    { 13462.571f, -12099.389f, 170.569f, 3.74f },
    { 13404.501f, -12078.250f, 171.447f, 2.79f },
    { 13384.118f, -12086.090f, 203.180f, 3.81f },
    { 13356.728f, -12137.280f, 190.198f, 5.80f },
    { 13404.381f, -12137.168f, 220.006f, 0.01f }, // au-dessus du pont (phase 1)
    { 13417.298f, -12131.918f, 158.683f, 0.01f }, // bout du pont (phase 2)
    { 13418.731f, -12132.062f, 151.18f,  0.01f }  // posée (héroïque)
};
static Position const EnginePos[8]       =
{
    { 13502.0f, -12177.0f, 134.298f, 3.095f },
    { 13373.6f, -12168.5f, 126.420f, 3.012f },
    { 13376.0f, -12092.4f, 126.500f, 3.421f },
    { 13505.7f, -12094.5f, 134.275f, 2.160f },
    { 13479.8f, -12094.7f, 143.790f, 3.100f },
    { 13397.9f, -12099.7f, 146.124f, 3.096f },
    { 13392.0f, -12162.1f, 146.087f, 3.060f },
    { 13473.4f, -12174.3f, 143.757f, 3.070f }
};
static Position const HarpoonGunPos[2]   =
{
    { 13440.0f, -12149.0f, 151.0f, 4.71f }, // tribord
    { 13440.0f, -12117.0f, 151.0f, 1.57f }  // bâbord
};
static Position const SapperPos[2]       =
{
    { 13418.580f, -12132.130f, 151.18f, 6.23f }, // largage
    { 13475.222f, -12134.426f, 150.81f, 6.21f }  // salle des machines
};
static Position const AssaultDrakePos[2][10] =
{
    {
        { 13577.011f, -12103.028f, 190.578f, 3.122f },
        { 13494.199f, -12099.243f, 176.920f, 3.094f },
        { 13454.508f, -12121.131f, 178.000f, 4.449f },
        { 13445.429f, -12160.474f, 175.161f, 4.622f },
        { 13447.944f, -12200.645f, 165.377f, 4.732f },
        { 13443.134f, -12226.259f, 158.178f, 3.782f },
        { 13426.954f, -12226.230f, 163.337f, 3.004f },
        { 13424.333f, -12218.577f, 161.056f, 1.575f }, // vague 1
        { 13434.357f, -12218.577f, 161.056f, 1.575f }, // vague 2
        { 13444.357f, -12218.577f, 161.056f, 1.575f }  // vague 3
    },
    {
        { 13575.193f, -12163.241f, 184.961f, 3.200f },
        { 13542.267f, -12165.166f, 178.630f, 3.197f },
        { 13466.173f, -12163.716f, 178.813f, 2.761f },
        { 13457.486f, -12137.352f, 176.080f, 1.618f },
        { 13456.574f, -12098.389f, 173.213f, 1.575f },
        { 13455.607f, -12063.203f, 167.213f, 1.689f },
        { 13440.849f, -12038.720f, 160.979f, 2.800f },
        { 13429.656f, -12051.177f, 171.689f, 4.701f }, // vague 1
        { 13439.656f, -12051.177f, 171.689f, 4.701f }, // vague 2
        { 13449.656f, -12051.177f, 171.689f, 4.701f }  // vague 3
    }
};
static Position const HarpoonedPos[2]    =
{
    { 13430.200f, -12159.500f, 157.083f, 1.575f },
    { 13432.300f, -12105.600f, 157.081f, 4.701f }
};
static Position const InfiltratorPath[12] =
{
    { 13541.889f, -12186.562f, 203.784f, 3.007f },
    { 13532.317f, -12185.049f, 201.008f, 2.980f },
    { 13498.614f, -12179.344f, 192.365f, 2.999f },
    { 13466.781f, -12173.522f, 185.991f, 2.858f },
    { 13442.281f, -12162.754f, 180.561f, 2.559f },
    { 13421.481f, -12148.216f, 184.845f, 2.375f },
    { 13421.511f, -12138.210f, 185.604f, 1.507f },
    { 13421.115f, -12129.624f, 183.585f, 1.487f },
    { 13422.139f, -12114.662f, 181.685f, 1.527f },
    { 13428.985f, -12071.629f, 173.243f, 1.106f },
    { 13440.401f, -12053.504f, 169.434f, 0.871f },
    { 13460.568f, -12036.537f, 164.934f, 0.474f }
};
// points du pont visés par le Barrage et l'Assaut
static Position const DeckPos[25] =
{
    { 13454.020f, -12145.916f, 150.848f, 0.0f }, { 13450.817f, -12145.892f, 150.836f, 0.0f },
    { 13438.058f, -12144.996f, 150.793f, 0.0f }, { 13427.595f, -12144.453f, 150.826f, 0.0f },
    { 13421.816f, -12139.198f, 150.870f, 0.0f }, { 13421.555f, -12133.157f, 150.904f, 0.0f },
    { 13425.701f, -12121.360f, 150.849f, 0.0f }, { 13432.726f, -12117.586f, 150.784f, 0.0f },
    { 13444.882f, -12116.272f, 150.797f, 0.0f }, { 13453.389f, -12117.736f, 150.838f, 0.0f },
    { 13458.964f, -12125.795f, 150.896f, 0.0f }, { 13459.809f, -12135.036f, 151.005f, 0.0f },
    { 13453.108f, -12139.578f, 150.848f, 0.0f }, { 13449.031f, -12138.921f, 150.838f, 0.0f },
    { 13439.471f, -12138.541f, 150.819f, 0.0f }, { 13437.735f, -12138.224f, 150.820f, 0.0f },
    { 13437.649f, -12130.649f, 150.835f, 0.0f }, { 13437.736f, -12125.138f, 150.807f, 0.0f },
    { 13442.063f, -12120.551f, 150.797f, 0.0f }, { 13446.127f, -12120.872f, 150.815f, 0.0f },
    { 13451.087f, -12122.585f, 150.835f, 0.0f }, { 13444.468f, -12131.013f, 150.840f, 0.0f },
    { 13444.048f, -12136.783f, 150.836f, 0.0f }, { 13444.695f, -12140.809f, 150.828f, 0.0f },
    { 13430.000f, -12133.000f, 150.850f, 0.0f }
};

static void ZoneTalk(Creature* creature, uint8 group)
{
    if (creature)
        sCreatureTextMgr->SendChat(creature, group, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
}

static bool IsOnSkyfire(WorldObject const* object)
{
    return object->GetPositionX() > 10000.0f;
}

static std::vector<Player*> GetDeckPlayers(Map* map)
{
    std::vector<Player*> players;
    for (auto const& ref : map->GetPlayers())
        if (Player* player = ref.GetSource())
            if (player->IsAlive() && !player->IsGameMaster() && player->GetExactDist2d(SkyfireDeckPos) < 120.0f)
                players.push_back(player);
    return players;
}

static Creature* GetShip(WorldObject* source)
{
    if (InstanceScript* instance = source->GetInstanceScript())
        if (Creature* ship = instance->GetCreature(DATA_THE_SKYFIRE))
            if (ship->IsAlive())
                return ship;
    return nullptr;
}

static void DamageShip(Unit* attacker, uint32 damage)
{
    if (Creature* ship = GetShip(attacker))
        if (damage)
            Unit::DealDamage(attacker, ship, damage, 0, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW, nullptr, false);
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

// ===========================================================================
// Capitaine Swayze : au sommet (transport) et sur le pont (lance / gère le combat)
// ===========================================================================
struct npc_ds_sky_captain_swayze : public ScriptedAI
{
    npc_ds_sky_captain_swayze(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()), _summons(creature) { }

    void Reset() override
    {
        _events.Reset();
        _summons.DespawnAll();
        _running = false;
        _sapperWarned = false;
        _lowWarned = false;
        _introStep = 0;
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);

        // au sommet, Swayze n'apparaît qu'une fois Ultraxion vaincu
        if (!IsOnSkyfire(me))
        {
            UpdateSummitVisibility();
            _events.ScheduleEvent(EVENT_SUMMIT_VISIBILITY, 2s);
        }
    }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (_instance && !player->IsInCombat())
        {
            bool ultraxionDone = _instance->GetBossState(DATA_ULTRAXION) == DONE;
            bool blackhornDone = _instance->GetBossState(DATA_WARMASTER_BLACKHORN) == DONE;

            if (!IsOnSkyfire(me))
            {
                if (ultraxionDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Emmenez-moi sur le pont du Brûleciel pour combattre Corne-Noire.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            }
            else
            {
                if (!blackhornDone && !_running)
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Rapprochez-nous.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                bool spineDone = _instance->GetBossState(DATA_SPINE_OF_DEATHWING) == DONE;
                bool spineRunning = _instance->GetBossState(DATA_SPINE_OF_DEATHWING) == IN_PROGRESS;
                if (blackhornDone && !spineDone && !spineRunning)
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Allons-y ! Sautons sur le dos d'Aile de mort.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
                if (spineDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Emmenez-nous au Maelström.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);
                if (!_running)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Retourner au sommet du Repos du ver.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
            }
        }
        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 action = GetGossipActionFor(player, gossipListId);
        CloseGossipMenuFor(player);
        if (!_instance || player->IsInCombat())
            return true;

        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (_instance->GetBossState(DATA_ULTRAXION) == DONE)
                    player->NearTeleportTo(SkyfireDeckPos.GetPositionX() + frand(-2.0f, 2.0f), SkyfireDeckPos.GetPositionY() + frand(-2.0f, 2.0f),
                        SkyfireDeckPos.GetPositionZ(), SkyfireDeckPos.GetOrientation());
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (!_running && _instance->GetBossState(DATA_WARMASTER_BLACKHORN) != DONE)
                    StartEvent();
                break;
            case GOSSIP_ACTION_INFO_DEF + 3:
                player->NearTeleportTo(SwayzeSummitPos.GetPositionX(), SwayzeSummitPos.GetPositionY(), SwayzeSummitPos.GetPositionZ(), SwayzeSummitPos.GetOrientation());
                break;
            case GOSSIP_ACTION_INFO_DEF + 4:
                StartSpine();
                break;
            case GOSSIP_ACTION_INFO_DEF + 5:
                if (_instance->GetBossState(DATA_SPINE_OF_DEATHWING) == DONE)
                    player->NearTeleportTo(MaelstromPos.GetPositionX(), MaelstromPos.GetPositionY(), MaelstromPos.GetPositionZ(), MaelstromPos.GetOrientation());
                break;
            default:
                break;
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

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_SAPPER:
                if (!_sapperWarned)
                {
                    _sapperWarned = true;
                    ZoneTalk(me, SAY_SWAYZE_SAPPER);
                }
                break;
            case ACTION_SHIP_LOW:
                if (!_lowWarned)
                {
                    _lowWarned = true;
                    ZoneTalk(me, SAY_SWAYZE_LOW_HEALTH);
                }
                break;
            case ACTION_ARMORED_DRAKE:
                ZoneTalk(me, SAY_SWAYZE_ARMORED);
                break;
            case ACTION_SHIP_DESTROYED:
                if (!_running)
                    break;
                ZoneTalk(me, SAY_SWAYZE_GOING_DOWN);
                _events.Reset();
                _events.ScheduleEvent(EVENT_SHIP_EXPLOSION, 4s);
                break;
            case ACTION_BLACKHORN_DIED:
                _events.Reset();
                _running = false;
                ZoneTalk(me, SAY_SWAYZE_VICTORY);
                if (_instance)
                    if (Creature* ship = _instance->GetCreature(DATA_THE_SKYFIRE))
                        _instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, ship);
                // on garde le corps de Corne-Noire (butin), on retire le reste
                for (uint32 entry : { uint32(NPC_THE_SKYFIRE), uint32(NPC_SKYFIRE_HARPOON_GUN), uint32(NPC_ENGINE_STALKER), uint32(NPC_GORIONA) })
                    _summons.DespawnEntry(entry);
                me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                break;
            case ACTION_RESET_BATTLE:
                FailEvent();
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
                case EVENT_SUMMIT_VISIBILITY:
                    UpdateSummitVisibility();
                    _events.Repeat(2s);
                    break;
                case EVENT_INTRO:
                    NextIntroStep();
                    break;
                case EVENT_CHECK_WIPE:
                    if (GetDeckPlayers(me->GetMap()).empty())
                    {
                        FailEvent();
                        break;
                    }
                    _events.Repeat(5s);
                    break;
                case EVENT_SHIP_EXPLOSION:
                    // le Brûleciel explose : tout le monde sur le pont meurt
                    DoCastSelf(SPELL_MASSIVE_EXPLOSION, true);
                    for (Player* player : GetDeckPlayers(me->GetMap()))
                        Unit::Kill(me, player, false);
                    FailEvent();
                    break;
                default:
                    break;
            }
        }
    }

private:
    // Échine d'Aile de mort : cinématique, tout le pont saute sur son dos, puis le combat démarre
    void StartSpine()
    {
        if (_instance->GetBossState(DATA_WARMASTER_BLACKHORN) != DONE)
            return;
        EncounterState spineState = _instance->GetBossState(DATA_SPINE_OF_DEATHWING);
        if (spineState == DONE || spineState == IN_PROGRESS)
            return;

        for (Player* player : GetDeckPlayers(me->GetMap()))
        {
            player->SendMovieStart(74);
            player->NearTeleportTo(SpineArrivalPos.GetPositionX() + frand(-3.0f, 3.0f), SpineArrivalPos.GetPositionY() + frand(-3.0f, 3.0f),
                SpineArrivalPos.GetPositionZ(), SpineArrivalPos.GetOrientation());
        }

        Creature* spine = _instance->GetCreature(DATA_SPINE_OF_DEATHWING);
        if (!spine || !spine->IsAlive())
            spine = me->SummonCreature(BOSS_SPINE_OF_DEATHWING, SpineBossPos, TEMPSUMMON_MANUAL_DESPAWN);
        if (spine)
            spine->AI()->DoAction(1); // ACTION_START_SPINE (après l'arrivée du raid)
    }

    void UpdateSummitVisibility()
    {
        bool visible = _instance && _instance->GetBossState(DATA_ULTRAXION) == DONE;
        if (me->IsVisible() != visible)
            me->SetVisible(visible);
    }

    void StartEvent()
    {
        _running = true;
        _introStep = 0;
        _sapperWarned = false;
        _lowWarned = false;
        me->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);

        // le navire (barre de vie), les moteurs en feu, les harpons
        if (Creature* ship = me->SummonCreature(NPC_THE_SKYFIRE, SkyfireDeckPos, TEMPSUMMON_MANUAL_DESPAWN))
        {
            uint32 health = 4000000;
            switch (me->GetMap()->GetDifficulty())
            {
                case RAID_DIFFICULTY_25MAN_NORMAL: health = 10000000; break;
                case RAID_DIFFICULTY_10MAN_HEROIC: health = 6000000;  break;
                case RAID_DIFFICULTY_25MAN_HEROIC: health = 15000000; break;
                default: break;
            }
            ship->SetMaxHealth(health);
            ship->SetHealth(health);
            if (_instance)
                _instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, ship);
        }

        for (Position const& pos : HarpoonGunPos)
            me->SummonCreature(NPC_SKYFIRE_HARPOON_GUN, pos, TEMPSUMMON_MANUAL_DESPAWN);

        me->SummonCreature(BOSS_WARMASTER_BLACKHORN, BlackhornDeckPos, TEMPSUMMON_MANUAL_DESPAWN);
        me->SummonCreature(NPC_GORIONA, GorionaPos[0], TEMPSUMMON_MANUAL_DESPAWN);

        ZoneTalk(me, SAY_SWAYZE_ALL_AHEAD);
        _events.ScheduleEvent(EVENT_INTRO, 7s);
        _events.ScheduleEvent(EVENT_CHECK_WIPE, 10s);
    }

    void NextIntroStep()
    {
        switch (_introStep++)
        {
            case 0:
                if (Creature* blackhorn = Blackhorn())
                    blackhorn->AI()->DoAction(ACTION_INTRO_TALK);
                _events.ScheduleEvent(EVENT_INTRO, 8s);
                break;
            case 1:
                ZoneTalk(me, SAY_SWAYZE_ENGINES_HIT);
                for (Position const& pos : EnginePos)
                    if (Creature* engine = me->SummonCreature(NPC_ENGINE_STALKER, pos, TEMPSUMMON_MANUAL_DESPAWN))
                        engine->CastSpell(engine, SPELL_ENGINE_FIRE, true);
                _events.ScheduleEvent(EVENT_INTRO, 5s);
                break;
            case 2:
                if (Creature* blackhorn = Blackhorn())
                    blackhorn->AI()->DoAction(ACTION_START_BATTLE);
                _events.ScheduleEvent(EVENT_INTRO, 7s);
                break;
            case 3:
                ZoneTalk(me, SAY_SWAYZE_STATIONS);
                break;
            default:
                break;
        }
    }

    void FailEvent()
    {
        if (!_running)
            return;

        _running = false;
        _events.Reset();
        if (Creature* blackhorn = Blackhorn())
            blackhorn->AI()->DoAction(ACTION_RESET_BATTLE);
        if (_instance)
        {
            if (Creature* ship = _instance->GetCreature(DATA_THE_SKYFIRE))
                _instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, ship);
            if (_instance->GetBossState(DATA_WARMASTER_BLACKHORN) != DONE)
                _instance->SetBossState(DATA_WARMASTER_BLACKHORN, FAIL);
        }
        _summons.DespawnAll();
        me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    Creature* Blackhorn() const
    {
        return _instance ? _instance->GetCreature(DATA_WARMASTER_BLACKHORN) : nullptr;
    }

    InstanceScript* _instance;
    SummonList _summons;
    EventMap _events;
    uint8 _introStep = 0;
    bool _running = false;
    bool _sapperWarned = false;
    bool _lowWarned = false;
};

// ===========================================================================
// Le Brûleciel : barre de vie du navire
// ===========================================================================
struct npc_ds_the_skyfire : public ScriptedAI
{
    npc_ds_the_skyfire(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        _lowHealth = false;
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!_lowHealth && me->HealthBelowPctDamaged(30, damage))
        {
            _lowHealth = true;
            if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
                swayze->AI()->DoAction(ACTION_SHIP_LOW);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
            swayze->AI()->DoAction(ACTION_SHIP_DESTROYED);
    }

    // le navire ne « revient » jamais à pleine vie
    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void UpdateAI(uint32 /*diff*/) override { }

private:
    bool _lowHealth = false;
};

// ===========================================================================
// Seigneur de guerre Corne-Noire
// ===========================================================================
struct boss_warmaster_blackhorn : public BossAI
{
    boss_warmaster_blackhorn(Creature* creature) : BossAI(creature, DATA_WARMASTER_BLACKHORN) { }

    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
        me->SetVisible(false);
        me->setActive(true);
        _phase = 0;
        _drakesKilled = 0;
        _wave = 0;
    }

    void Reset() override { }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_INTRO_TALK:
                ZoneTalk(me, SAY_BLACKHORN_INTRO);
                break;
            case ACTION_START_BATTLE:
                if (_phase != 0)
                    break;
                _phase = 1;
                ZoneTalk(me, SAY_BLACKHORN_AGGRO);
                instance->SetBossState(DATA_WARMASTER_BLACKHORN, IN_PROGRESS);
                if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
                    goriona->AI()->DoAction(ACTION_GORIONA_MOVE);
                events.ScheduleEvent(EVENT_SUMMON_DRAKES, 10s);
                events.ScheduleEvent(EVENT_HARPOON, 40s);
                events.ScheduleEvent(EVENT_SUMMON_INFILTRATOR, 58s);
                break;
            case ACTION_RESET_BATTLE:
                Cleanup();
                me->DespawnOrUnsummon();
                break;
            default:
                break;
        }
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (_phase < 2)
            return; // phase 1 : c'est Swayze qui détecte le wipe

        if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
            swayze->AI()->DoAction(ACTION_RESET_BATTLE);
        else
        {
            Cleanup();
            instance->SetBossState(DATA_WARMASTER_BLACKHORN, FAIL);
            me->DespawnOrUnsummon();
        }
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        switch (summon->GetEntry())
        {
            case NPC_TWILIGHT_ELITE_DREADBLADE:
            case NPC_TWILIGHT_ELITE_SLAYER:
            case NPC_TWILIGHT_SAPPER:
                DoZoneInCombat(summon);
                break;
            default:
                break;
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() != NPC_TWILIGHT_ASSAULT_DRAKE_1 && summon->GetEntry() != NPC_TWILIGHT_ASSAULT_DRAKE_2)
            return;

        if (++_drakesKilled >= 6 && _phase == 1)
        {
            _phase = 2;
            events.CancelEvent(EVENT_SUMMON_DRAKES);
            events.CancelEvent(EVENT_HARPOON);
            events.CancelEvent(EVENT_SUMMON_INFILTRATOR);
            _phase2Step = 0;
            events.ScheduleEvent(EVENT_PHASE_2, 3s);
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_BLACKHORN_KILL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_BLACKHORN_DEATH);
        DespawnFlames();
        if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
            goriona->DespawnOrUnsummon();
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_CONSUMING_SHROUD);
        if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
            swayze->AI()->DoAction(ACTION_BLACKHORN_DIED);
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        // phase 1 : Corne-Noire est sur le dos de Goriona, il dirige l'attaque sans combattre
        if (_phase < 3)
        {
            while (uint32 eventId = events.ExecuteEvent())
                ExecutePhase1Event(eventId);
            return;
        }

        if (!UpdateVictim())
            return;

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DEVASTATE:
                    DoCastVictim(SPELL_DEVASTATE);
                    events.Repeat(8500ms);
                    break;
                case EVENT_DISRUPTING_ROAR:
                    Talk(SAY_BLACKHORN_ROAR);
                    DoCastAOE(SPELL_DISRUPTING_ROAR);
                    events.Repeat(18500ms, 23s);
                    break;
                case EVENT_SHOCKWAVE:
                    Shockwave();
                    events.Repeat(23s);
                    break;
                case EVENT_SHOCKWAVE_END:
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (Unit* victim = me->GetVictim())
                        AttackStart(victim);
                    break;
                case EVENT_SIPHON_VITALITY:
                    if (me->HealthBelowPct(60))
                        if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
                            if (goriona->IsAlive() && goriona->GetHealthPct() >= 30.0f)
                            {
                                Talk(EMOTE_SIPHON);
                                DoCast(goriona, SPELL_SIPHON_VITALITY);
                            }
                    events.Repeat(10s);
                    break;
                case EVENT_BERSERK:
                    Talk(SAY_BLACKHORN_BERSERK);
                    DoCastSelf(SPELL_BERSERK, true);
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
    void ExecutePhase1Event(uint32 eventId)
    {
        switch (eventId)
        {
            case EVENT_SUMMON_DRAKES:
                if (_wave < 3)
                {
                    ++_wave;
                    for (uint8 side = 0; side < 2; ++side)
                        if (Creature* drake = me->SummonCreature(side == 0 ? NPC_TWILIGHT_ASSAULT_DRAKE_1 : NPC_TWILIGHT_ASSAULT_DRAKE_2,
                            AssaultDrakePos[side][0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10s))
                            drake->AI()->SetData(side, _wave);
                    if (_wave < 3)
                        events.ScheduleEvent(EVENT_SUMMON_DRAKES, 60s);
                }
                break;
            case EVENT_HARPOON:
                if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
                    for (Creature* gun : GunsOf(swayze))
                        gun->AI()->DoAction(ACTION_HARPOON);
                events.Repeat(35s);
                break;
            case EVENT_SUMMON_INFILTRATOR:
                me->SummonCreature(NPC_TWILIGHT_INFILTRATOR, InfiltratorPath[0], TEMPSUMMON_TIMED_DESPAWN, 25s);
                events.Repeat(40s);
                break;
            case EVENT_PHASE_2:
                NextPhase2Step();
                break;
            default:
                break;
        }
    }

    static std::vector<Creature*> GunsOf(Creature* swayze)
    {
        std::vector<Creature*> guns;
        std::list<Creature*> found;
        swayze->GetCreatureListWithEntryInGrid(found, NPC_SKYFIRE_HARPOON_GUN, 100.0f);
        for (Creature* gun : found)
            if (gun->IsAlive())
                guns.push_back(gun);
        return guns;
    }

    void NextPhase2Step()
    {
        switch (_phase2Step++)
        {
            case 0:
                if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
                    swayze->AI()->DoAction(ACTION_ARMORED_DRAKE);
                events.ScheduleEvent(EVENT_PHASE_2, 4s);
                break;
            case 1:
                ZoneTalk(me, SAY_BLACKHORN_GORIONA);
                if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
                    goriona->AI()->DoAction(ACTION_GORIONA_PHASE_2);
                events.ScheduleEvent(EVENT_PHASE_2, 4s);
                break;
            case 2:
            {
                // Corne-Noire saute du dos de Goriona sur le pont
                _phase = 3;
                me->SetVisible(true);
                if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
                    me->NearTeleportTo(goriona->GetPositionX(), goriona->GetPositionY(), goriona->GetPositionZ(), me->GetOrientation());
                me->GetMotionMaster()->MoveJump(BlackhornDeckPos, 20.0f, 15.0f);
                me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
                me->SetReactState(REACT_AGGRESSIVE);
                me->SetUnkillable(false); // la base le marque « intuable » (resterait bloqué à 1 PV)
                Talk(SAY_BLACKHORN_PHASE_2);
                DoZoneInCombat();
                DoCastSelf(SPELL_VENGEANCE, true);

                events.ScheduleEvent(EVENT_DEVASTATE, 6s, 8500ms);
                events.ScheduleEvent(EVENT_DISRUPTING_ROAR, 10s);
                events.ScheduleEvent(EVENT_SHOCKWAVE, 13s, 16s);
                events.ScheduleEvent(EVENT_BERSERK, 4min);
                if (IsHeroic())
                    events.ScheduleEvent(EVENT_SIPHON_VITALITY, 5s);
                break;
            }
            default:
                break;
        }
    }

    // Onde de choc : de préférence sur un joueur à 15 m ou plus, Corne-Noire reste figé dans cette direction
    void Shockwave()
    {
        std::vector<Player*> distant, everyone;
        for (Player* player : GetDeckPlayers(me->GetMap()))
        {
            everyone.push_back(player);
            if (me->GetDistance(player) >= 15.0f)
                distant.push_back(player);
        }
        std::vector<Player*>& pool = distant.size() >= 2 ? distant : everyone;
        if (pool.empty())
            return;

        Player* target = Trinity::Containers::SelectRandomContainerElement(pool);
        Talk(SAY_BLACKHORN_SHOCKWAVE);
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetFacingToObject(target);
        me->CastSpell(target->GetPosition(), SPELL_SHOCKWAVE, CastSpellExtraArgs(false));
        events.ScheduleEvent(EVENT_SHOCKWAVE_END, 3s);
    }

    void DespawnFlames()
    {
        std::list<Creature*> flames;
        me->GetCreatureListWithEntryInGrid(flames, NPC_TWILIGHT_FLAMES, 200.0f);
        for (Creature* flame : flames)
            flame->DespawnOrUnsummon();
    }

    void Cleanup()
    {
        events.Reset();
        summons.DespawnAll();
        DespawnFlames();
        if (Creature* goriona = instance->GetCreature(DATA_GORIONA))
            goriona->DespawnOrUnsummon();
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_CONSUMING_SHROUD);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_DEGENERATION);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BRUTAL_STRIKE);
    }

    uint8 _phase = 0;        // 0 attente, 1 drakes, 2 transition, 3 Corne-Noire sur le pont
    uint8 _phase2Step = 0;
    uint8 _drakesKilled = 0;
    uint8 _wave = 0;
};

// ===========================================================================
// Goriona
// ===========================================================================
struct npc_ds_goriona : public ScriptedAI
{
    npc_ds_goriona(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void InitializeAI() override
    {
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        me->setActive(true);
        _phase = 0;
    }

    void Reset() override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { } // géré par Corne-Noire / Swayze

    void DoAction(int32 action) override
    {
        if (action == ACTION_GORIONA_MOVE)
        {
            _phase = 1;
            me->GetMotionMaster()->MoveSmoothPath(POINT_GORIONA_HOVER, &GorionaPos[1], 5, false, true);
            _events.ScheduleEvent(EVENT_TWILIGHT_ONSLAUGHT, 46s);
            if (IsHeroic())
                _events.ScheduleEvent(EVENT_BROADSIDE, 57s);
        }
        else if (action == ACTION_GORIONA_PHASE_2)
        {
            _phase = 2;
            me->InterruptNonMeleeSpells(false);
            _events.CancelEvent(EVENT_TWILIGHT_ONSLAUGHT);
            _events.CancelEvent(EVENT_BROADSIDE);
            me->GetMotionMaster()->MovePoint(POINT_GORIONA_DECK, GorionaPos[6], false);
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
            me->SetReactState(REACT_AGGRESSIVE);
            DoZoneInCombat();
            _events.ScheduleEvent(EVENT_TWILIGHT_FLAMES, 15s);
            if (IsHeroic())
                _events.ScheduleEvent(EVENT_CONSUMING_SHROUD, 45s);
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if ((type == POINT_MOTION_TYPE || type == EFFECT_MOTION_TYPE) && pointId == POINT_GORIONA_LAND)
        {
            me->SetCanFly(false);
            me->SetDisableGravity(false);
            _events.ScheduleEvent(EVENT_TWILIGHT_BREATH, 10s, 20s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (_phase >= 2 && _phase < 4)
        {
            // héroïque : se pose sur le pont à 90 %
            if (_phase == 2 && IsHeroic() && me->HealthBelowPct(90))
            {
                _phase = 3;
                me->GetMotionMaster()->MoveLand(POINT_GORIONA_LAND, GorionaPos[7]);
            }
            // à 20 % elle s'enfuit
            if (me->HealthBelowPct(20))
            {
                _phase = 4;
                _events.Reset();
                me->InterruptNonMeleeSpells(true);
                me->SetReactState(REACT_PASSIVE);
                me->AttackStop();
                me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                me->SetCanFly(true);
                me->SetDisableGravity(true);
                me->GetMotionMaster()->MovePoint(0, GorionaPos[4], false);
                me->DespawnOrUnsummon(5s);
                return;
            }
        }

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TWILIGHT_ONSLAUGHT:
                {
                    Position const& pos = Trinity::Containers::SelectRandomContainerElement(DeckPos);
                    if (Creature* target = me->SummonCreature(NPC_ONSLAUGHT_TARGET, pos, TEMPSUMMON_TIMED_DESPAWN, 10s))
                    {
                        target->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                        target->CastSpell(target, SPELL_TWILIGHT_ONSLAUGHT_VISUAL, true);
                    }
                    _onslaughtPos = pos;
                    me->CastSpell(pos, SPELL_TWILIGHT_ONSLAUGHT, CastSpellExtraArgs(false));
                    _events.Repeat(35s);
                    break;
                }
                case EVENT_BROADSIDE:
                    Talk(EMOTE_BROADSIDE);
                    DoCastAOE(SPELL_BROADSIDE);
                    _events.Repeat(90s);
                    break;
                case EVENT_TWILIGHT_FLAMES:
                {
                    // sur un joueur loin de Corne-Noire
                    std::vector<Player*> targets;
                    Creature* blackhorn = me->GetInstanceScript() ? me->GetInstanceScript()->GetCreature(DATA_WARMASTER_BLACKHORN) : nullptr;
                    for (Player* player : GetDeckPlayers(me->GetMap()))
                        if (!blackhorn || player->GetDistance(blackhorn) > 14.0f)
                            targets.push_back(player);
                    if (targets.empty())
                        targets = GetDeckPlayers(me->GetMap());
                    if (!targets.empty())
                        me->CastSpell(Trinity::Containers::SelectRandomContainerElement(targets)->GetPosition(), SPELL_TWILIGHT_MISSILE, CastSpellExtraArgs(true));
                    _events.Repeat(9s);
                    break;
                }
                case EVENT_TWILIGHT_BREATH:
                    DoCastVictim(SPELL_TWILIGHT_BREATH);
                    _events.Repeat(20500ms);
                    break;
                case EVENT_CONSUMING_SHROUD:
                    DoCastAOE(SPELL_CONSUMING_SHROUD);
                    _events.Repeat(30s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (_phase >= 2 && UpdateVictim())
            DoMeleeAttackIfReady();
    }

    // Assaut crépusculaire : les dégâts sont partagés entre les joueurs dans la zone (7 m) et le Brûleciel.
    // Calcul fait ici pour ne pas dépendre du ciblage du sort de dégâts.
    void OnSpellCastFinished(SpellInfo const* spell, SpellFinishReason reason) override
    {
        if (spell->Id != SPELL_TWILIGHT_ONSLAUGHT || reason != SPELL_FINISHED_SUCCESSFUL_CAST)
            return;

        uint32 total = 800000;
        switch (me->GetMap()->GetDifficulty())
        {
            case RAID_DIFFICULTY_25MAN_NORMAL: total = 2000000; break;
            case RAID_DIFFICULTY_10MAN_HEROIC: total = 1200000; break;
            case RAID_DIFFICULTY_25MAN_HEROIC: total = 3000000; break;
            default: break;
        }

        std::vector<Player*> soakers;
        for (Player* player : GetDeckPlayers(me->GetMap()))
            if (player->GetExactDist2d(_onslaughtPos) <= 7.0f)
                soakers.push_back(player);

        uint32 share = total / uint32(soakers.size() + 1);
        for (Player* player : soakers)
        {
            me->SendSpellNonMeleeDamageLog(player, SPELL_TWILIGHT_ONSLAUGHT_DAMAGE, share, SPELL_SCHOOL_MASK_SHADOW, 0, 0, false, 0);
            Unit::DealDamage(me, player, share, 0, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW, nullptr, false);
        }
        if (Creature* ship = GetShip(me))
            me->SendSpellNonMeleeDamageLog(ship, SPELL_TWILIGHT_ONSLAUGHT_DAMAGE, share, SPELL_SCHOOL_MASK_SHADOW, 0, 0, false, 0);
        DamageShip(me, share);
    }

private:
    EventMap _events;
    Position _onslaughtPos;
    uint8 _phase = 0; // 1 survol, 2 bout du pont, 3 posée (héroïque), 4 fuite
};

// ===========================================================================
// Drakes d'assaut du Crépuscule
// ===========================================================================
struct npc_ds_twilight_assault_drake : public ScriptedAI
{
    npc_ds_twilight_assault_drake(Creature* creature) : ScriptedAI(creature) { }

    void InitializeAI() override
    {
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetReactState(REACT_PASSIVE);
        me->setActive(true);
        _side = me->GetEntry() == NPC_TWILIGHT_ASSAULT_DRAKE_2 ? 1 : 0;
    }

    void Reset() override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void SetData(uint32 side, uint32 wave) override
    {
        _side = std::min<uint32>(side, 1);
        _wave = std::clamp<uint32>(wave, 1, 3);

        Position path[7];
        for (uint8 i = 0; i < 6; ++i)
            path[i] = AssaultDrakePos[_side][i + 1];
        path[6] = HoldPos();
        me->GetMotionMaster()->MoveSmoothPath(POINT_DRAKE_HOLD, path, 7, false, true);

        _events.ScheduleEvent(EVENT_DROP_ADD, 9s);
        _events.ScheduleEvent(EVENT_TWILIGHT_BARRAGE, 23s, 28s);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_HARPOONED:
                _harpooned = true;
                me->InterruptNonMeleeSpells(false);
                me->GetMotionMaster()->MovePoint(POINT_DRAKE_HARPOONED, HarpoonedPos[_side], false);
                break;
            case ACTION_HARPOON_END:
                _harpooned = false;
                me->RemoveAurasDueToSpell(SPELL_HARPOON);
                me->GetMotionMaster()->MovePoint(POINT_DRAKE_HOLD, HoldPos(), false);
                break;
            default:
                break;
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if ((type == POINT_MOTION_TYPE || type == EFFECT_MOTION_TYPE) && pointId == POINT_DRAKE_HOLD)
            _ready = true;
    }

    bool IsReady() const { return _ready && !_harpooned && me->IsAlive(); }
    uint8 GetSide() const { return _side; }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DROP_ADD:
                    if (InstanceScript* instance = me->GetInstanceScript())
                        if (Creature* blackhorn = instance->GetCreature(DATA_WARMASTER_BLACKHORN))
                            blackhorn->SummonCreature(_side == 1 ? NPC_TWILIGHT_ELITE_DREADBLADE : NPC_TWILIGHT_ELITE_SLAYER,
                                AddsPos[_side], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60s);
                    break;
                case EVENT_TWILIGHT_BARRAGE:
                    if (!me->isMoving())
                    {
                        if (!_barrageWarned)
                        {
                            _barrageWarned = true;
                            Talk(EMOTE_TWILIGHT_BARRAGE);
                        }
                        me->CastSpell(Trinity::Containers::SelectRandomContainerElement(DeckPos), SPELL_TWILIGHT_BARRAGE, CastSpellExtraArgs(false));
                    }
                    _events.Repeat(10s, 15s);
                    break;
                default:
                    break;
            }
        }
    }

private:
    Position const& HoldPos() const { return AssaultDrakePos[_side][6 + _wave]; }

    EventMap _events;
    uint32 _side = 0;
    uint32 _wave = 1;
    bool _ready = false;
    bool _harpooned = false;
    bool _barrageWarned = false;
};

// ===========================================================================
// Harpons du Brûleciel : ramènent un drake au bastingage pendant 25 s (20 s en héroïque)
// ===========================================================================
struct npc_ds_skyfire_harpoon_gun : public ScriptedAI
{
    npc_ds_skyfire_harpoon_gun(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        _drakeEntry = me->GetPositionY() < -12130.0f ? NPC_TWILIGHT_ASSAULT_DRAKE_1 : NPC_TWILIGHT_ASSAULT_DRAKE_2;
        _events.Reset();
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_HARPOON || !_drake.IsEmpty())
            return;

        std::list<Creature*> drakes;
        me->GetCreatureListWithEntryInGrid(drakes, _drakeEntry, 200.0f);
        for (Creature* drake : drakes)
        {
            auto* ai = dynamic_cast<npc_ds_twilight_assault_drake*>(drake->AI());
            if (!ai || !ai->IsReady())
                continue;

            _drake = drake->GetGUID();
            me->CastSpell(drake, SPELL_HARPOON, true);
            drake->AI()->DoAction(ACTION_HARPOONED);
            _events.ScheduleEvent(EVENT_HARPOON_RELEASE, IsHeroic() ? 20s : 25s);
            break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() == EVENT_HARPOON_RELEASE)
        {
            me->InterruptNonMeleeSpells(true);
            if (Creature* drake = ObjectAccessor::GetCreature(*me, _drake))
                if (drake->IsAlive())
                    drake->AI()->DoAction(ACTION_HARPOON_END);
            _drake.Clear();
        }
    }

private:
    EventMap _events;
    ObjectGuid _drake;
    uint32 _drakeEntry = NPC_TWILIGHT_ASSAULT_DRAKE_1;
};

// ===========================================================================
// Lame-d'effroi d'élite / Tueur d'élite du Crépuscule
// ===========================================================================
struct npc_ds_twilight_elite_add : public ScriptedAI
{
    npc_ds_twilight_elite_add(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        _events.ScheduleEvent(EVENT_STACKING_STRIKE, 8500ms, 9500ms);
        _events.ScheduleEvent(EVENT_BLADE_RUSH, 20s);
        DoZoneInCombat();
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
                case EVENT_STACKING_STRIKE:
                    // Lame-d'effroi : Dégénérescence ; Tueur : Frappe brutale (cumulables, changement de tank)
                    DoCastVictim(me->GetEntry() == NPC_TWILIGHT_ELITE_DREADBLADE ? SPELL_DEGENERATION : SPELL_BRUTAL_STRIKE);
                    _events.Repeat(8500ms, 9500ms);
                    break;
                case EVENT_BLADE_RUSH:
                    if (Unit* target = SelectTarget(SELECT_TARGET_MAXDISTANCE, 0, 30.0f, true))
                        DoCast(target, SPELL_BLADE_RUSH);
                    _events.Repeat(20s);
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

// ===========================================================================
// Infiltré du Crépuscule : survole le pont et lâche un Saboteur
// ===========================================================================
struct npc_ds_twilight_infiltrator : public ScriptedAI
{
    npc_ds_twilight_infiltrator(Creature* creature) : ScriptedAI(creature) { }

    void InitializeAI() override
    {
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
    }

    void Reset() override { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        me->GetMotionMaster()->MoveSmoothPath(POINT_INFILTRATOR_END, &InfiltratorPath[1], 11, false, true);
        _events.ScheduleEvent(EVENT_DROP_SAPPER, 9s);
        _events.ScheduleEvent(EVENT_DESPAWN, 19s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            if (eventId == EVENT_DROP_SAPPER)
            {
                if (InstanceScript* instance = me->GetInstanceScript())
                    if (Creature* blackhorn = instance->GetCreature(DATA_WARMASTER_BLACKHORN))
                    {
                        ZoneTalk(blackhorn, EMOTE_SAPPER);
                        blackhorn->SummonCreature(NPC_TWILIGHT_SAPPER, SapperPos[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                    }
            }
            else if (eventId == EVENT_DESPAWN)
                me->DespawnOrUnsummon(1s);
        }
    }

private:
    EventMap _events;
};

// ===========================================================================
// Saboteur du Crépuscule : court vers la salle des machines et explose
// ===========================================================================
struct npc_ds_twilight_sapper : public ScriptedAI
{
    npc_ds_twilight_sapper(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
    }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        DoCastSelf(SPELL_SMOKE_BOMB, true);
        _events.ScheduleEvent(EVENT_SAPPER_RUN, 1500ms);
    }

    void AttackStart(Unit* /*who*/) override { } // il ignore les joueurs, seule la salle des machines compte
    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE || pointId != POINT_SAPPER_ENGINE || !me->IsAlive())
            return;

        DoCastSelf(SPELL_DETONATE, true);
        if (Creature* ship = GetShip(me))
            DamageShip(me, ship->CountPctFromMaxHealth(15));
        if (Creature* swayze = me->FindNearestCreature(NPC_SKY_CAPTAIN_SWAYZE, 200.0f))
            swayze->AI()->DoAction(ACTION_SAPPER);
        Unit::Kill(me, me, false);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        if (_events.ExecuteEvent() == EVENT_SAPPER_RUN)
        {
            me->RemoveAurasDueToSpell(SPELL_SMOKE_BOMB);
            me->GetMotionMaster()->MovePoint(POINT_SAPPER_ENGINE, SapperPos[1]);
        }
    }

private:
    EventMap _events;
};

// ===========================================================================
// Sorts
// ===========================================================================

// 107439 - Twilight Barrage (dégâts) : si personne n'intercepte, le Brûleciel encaisse (x1,5)
class spell_ds_twilight_barrage_damage : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target) { return !target->IsPlayer(); });
        if (targets.empty())
            if (Unit* caster = GetCaster())
                DamageShip(caster, uint32(GetSpellInfo()->Effects[EFFECT_0].CalcValue(caster) * 3 / 2));
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ds_twilight_barrage_damage::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// 106401 - Twilight Onslaught (dégâts) : n'est plus utilisé, Goriona calcule le partage elle-même
class [[maybe_unused]] spell_ds_twilight_onslaught_damage : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target) { return !target->IsPlayer(); });
        if (Unit* caster = GetCaster())
            DamageShip(caster, uint32(GetSpellInfo()->Effects[EFFECT_0].CalcValue(caster) / int32(targets.size() + 1)));
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ds_twilight_onslaught_damage::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// 110153 - Broadside (héroïque) : 20 % de la vie actuelle du Brûleciel
class spell_ds_broadside : public SpellScript
{
    void HandleAfterCast()
    {
        if (Unit* caster = GetCaster())
            if (Creature* ship = GetShip(caster))
                DamageShip(caster, ship->CountPctFromCurHealth(20));
    }

    void Register() override
    {
        AfterCast.Register(&spell_ds_broadside::HandleAfterCast);
    }
};

// 110214 - Consuming Shroud : une seule cible, pas les tanks
class spell_ds_consuming_shroud : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* target)
        {
            Player* player = target->ToPlayer();
            return !player || IsTankSpec(player);
        });
        if (targets.size() > 1)
            Trinity::Containers::RandomResize(targets, 1);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_ds_consuming_shroud::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

// 108045 - Vengeance : +1 % de dégâts par % de vie perdue
class spell_ds_blackhorn_vengeance : public AuraScript
{
    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        if (Unit* owner = GetUnitOwner())
            if (AuraEffect* damageEffect = GetAura()->GetEffect(EFFECT_0))
                damageEffect->ChangeAmount(int32(100.0f - owner->GetHealthPct()));
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_ds_blackhorn_vengeance::HandlePeriodic, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
    }
};
}

void AddSC_boss_warmaster_blackhorn()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(npc_ds_sky_captain_swayze);
    RegisterDragonSoulCreatureAI(npc_ds_the_skyfire);
    RegisterDragonSoulCreatureAI(boss_warmaster_blackhorn);
    RegisterDragonSoulCreatureAI(npc_ds_goriona);
    RegisterDragonSoulCreatureAI(npc_ds_twilight_assault_drake);
    RegisterDragonSoulCreatureAI(npc_ds_skyfire_harpoon_gun);
    RegisterDragonSoulCreatureAI(npc_ds_twilight_elite_add);
    RegisterDragonSoulCreatureAI(npc_ds_twilight_infiltrator);
    RegisterDragonSoulCreatureAI(npc_ds_twilight_sapper);
    RegisterSpellScript(spell_ds_twilight_barrage_damage);
    RegisterSpellScript(spell_ds_broadside);
    RegisterSpellScript(spell_ds_consuming_shroud);
    RegisterSpellScript(spell_ds_blackhorn_vengeance);
}
