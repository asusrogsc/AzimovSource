/*
 * Azimov Online - Foire de Sombrelune (île, carte 974) : mini-jeux.
 * Porté de Project SkyFire (SkyFire_548, src/server/scripts/Events/darkmoonfaire, licence GPL)
 * et adapté à l'API du fork (ObjectGuid, IA de gossip, hooks .Register, etc.).
 * Non portés (contenu MoP) : Croc-de-Lune, manège, chapeau haut-de-forme.
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Containers.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "Vehicle.h"
#include "WorldSession.h"
#include <algorithm>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Données communes
// ---------------------------------------------------------------------------------------------
namespace AzDarkmoon
{
    uint32 const IslandMapId = 974;

    // Identifiants d'événement réglables dans worldserver.conf
    uint16 FaireEventId()       { return uint16(sConfigMgr->GetIntDefault("Azimov.Sombrelune.Evenement", 75)); }
    uint16 DeathmatchAnnounce() { return uint16(sConfigMgr->GetIntDefault("Azimov.Sombrelune.CombatAMort.Annonce", 82)); }
    uint16 DeathmatchRun()      { return uint16(sConfigMgr->GetIntDefault("Azimov.Sombrelune.CombatAMort.Course", 83)); }

    uint32 const ItemGameToken = 71083;
    char const* const MissingTokenText = "Il vous faut un jeton de jeu de Sombrelune pour jouer.";

    inline uint32 Min(uint32 a, uint32 b) { return a < b ? a : b; }
}

enum AzDarkmoonSpells
{
    // Tape-taupe gnoll
    SPELL_WHACK_OVERRIDE_ACTION             = 101612,
    SPELL_WHACK_ENABLE_POWERBAR             = 110230,
    SPELL_WHACK_KILL_CREDIT                 = 101835,
    SPELL_WHACK_DOLL_STUN                   = 101679,
    SPELL_WHACK_SPAWN_VISUAL                = 102136,
    SPELL_WHACK_OK_TO_HIT                   = 101996,
    SPELL_WHACK_SUMMON_GNOLL                = 102036,
    SPELL_WHACK_SUMMON_DOLL                 = 102043,
    SPELL_WHACK_SUMMON_BOSS                 = 102044,
    SPELL_WHACK_FORBIDDEN_ZONE              = 109977,
    // Stand de tir
    SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION  = 101871,
    SPELL_SHOOTING_GALLERY_TARGET_INDICATOR = 102341,
    // Lancer d'anneaux
    SPELL_RING_TOSS_ACTION                  = 102058,
    // Tonk
    SPELL_DARKMOON_TONK_CONTROLLER          = 100752,
    SPELL_DARKMOON_TONK_CANNON_BLAST        = 102121,
    SPELL_DARKMOON_TONK_ACTION              = 102178,
    SPELL_DARKMOON_TONK_SHOT                = 102292,
    SPELL_DARKMOON_TONK_TARGET_TRACKER      = 62265,
    SPELL_DARKMOON_TONK_TARGET_PARTICIPANT  = 62280,
    // Canon
    SPELL_DARKMOON_CANNON_PREPARATION       = 102112,
    SPELL_DARKMOON_CANNON_MAGIC_WINGS       = 102116,
    SPELL_DARKMOON_CANNON_LAUNCH            = 102121,
    SPELL_DARKMOON_CANNON_TARGET_CENTER     = 62173,
    SPELL_DARKMOON_CANNON_TARGET_NEAR       = 62175,
    SPELL_DARKMOON_CANNON_TARGET_MISS       = 62179,
    SPELL_DARKMOON_CANNON_ROOT              = 97936,
    // Combat à mort
    SPELL_ENTER_DEATHMATCH                  = 108919,
    SPELL_EXIT_DEATHMATCH                   = 108923
};

enum AzDarkmoonCreatures
{
    NPC_WHACK_BARREL                = 54546,
    NPC_WHACK_GNOLL                 = 54444,
    NPC_WHACK_DOLL                  = 54466,
    NPC_WHACK_BOSS                  = 54549,
    NPC_WHACK_GNOLL_CREDIT          = 54505,
    NPC_DARKMOON_RINLING            = 14841,
    NPC_SHOOTING_GALLERY_TARGET     = 54231,
    NPC_DARKMOON_JESSICA_ROGERS     = 54485,
    NPC_DARKMOON_DUBENKO            = 54490,
    NPC_RING_TOSS_CREDIT            = 54495,
    NPC_DARKMOON_FINLAY_COOLSHOT    = 54605,
    NPC_DARKMOON_STEAM_TONK         = 54588,
    NPC_TONK_CANNON_TARGET          = 33081,
    NPC_DARKMOON_CANNON_TARGET_CREDIT = 54224,
    NPC_DARKMOON_DEATHMATCH_ANNOUNCER = 55402,
    GO_DARKMOON_DEATHMATCH_CHEST    = 209620
};

enum AzDarkmoonQuests
{
    QUEST_WHACK_A_GNOLL         = 29463,
    QUEST_SHOOTING_GALLERY      = 29438,
    QUEST_TARGET_TURTLE         = 29455,
    QUEST_TONK_COMMANDER        = 29434,
    QUEST_HUMANOID_CANNONBALL   = 29436
};

enum AzDarkmoonMisc
{
    ACHIEVEMENT_QUICK_SHOT              = 6022,
    ACHIEVEMENT_BLASTENHEIMER_BULLSEYE  = 6021,

    MAX_WHACK_BARRELS                   = 9,
    SHOOTING_GALLERY_TARGET_COUNT       = 25,
    SHOOTING_GALLERY_QUICK_SHOT_MS      = 1000,
    SHOOTING_GALLERY_MAX_HITS_PER_TARGET = 2,
    RING_TOSS_TARGET_COUNT              = 3,
    RING_TOSS_MAX_THROWS                = 10,
    TONK_CHALLENGE_TARGET_COUNT         = 30,
    DARKMOON_CANNON_TARGET_POINTS       = 5,

    // données / actions d'IA
    DATA_ACTIVE_PLAYER                  = 1,
    DATA_SPAWN_TIME                     = 2,
    DATA_HIT_COUNT                      = 3,
    DATA_ACTIVE_TARGET                  = 4,
    ACTION_END_GAME                     = 1,
    ACTION_COMPLETE_GAME                = 2,
    ACTION_CANCEL_GAME                  = 3,

    // texte du combat à mort
    SAY_DEATHMATCH_ANNOUNCE             = 0,
    SAY_DEATHMATCH_RUN                  = 1,
    SAY_DEATHMATCH_END                  = 2
};

namespace
{
    bool HasAnyDarkmoonGameAura(Player const* player)
    {
        if (!player)
            return false;

        static uint32 const auras[] =
        {
            SPELL_WHACK_OVERRIDE_ACTION, SPELL_WHACK_ENABLE_POWERBAR, SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION, SPELL_RING_TOSS_ACTION,
            SPELL_DARKMOON_TONK_CONTROLLER, SPELL_DARKMOON_TONK_CANNON_BLAST, SPELL_DARKMOON_TONK_ACTION, SPELL_DARKMOON_TONK_TARGET_PARTICIPANT,
            SPELL_DARKMOON_CANNON_PREPARATION, SPELL_DARKMOON_CANNON_MAGIC_WINGS, SPELL_DARKMOON_CANNON_ROOT
        };

        for (uint32 aura : auras)
            if (player->HasAura(aura))
                return true;
        return false;
    }

    void CompleteAchievement(Player* player, uint32 achievementId)
    {
        if (!player || player->HasAchieved(achievementId))
            return;

        if (AchievementEntry const* achievement = sAchievementStore.LookupEntry(achievementId))
            player->CompletedAchievement(achievement);
    }

    // Remet à zéro l'objectif « tuer » d'une quête (nouvelle partie)
    void ResetGameScore(Player* player, uint32 questId, uint32 creditEntry)
    {
        if (!player)
            return;

        player->ResetQuestObjectiveCount(questId, int32(creditEntry));
        player->SetPower(POWER_ALTERNATE_POWER, 0);
    }

    bool TakeGameToken(Player* player)
    {
        if (!player->HasItemCount(AzDarkmoon::ItemGameToken, 1))
        {
            player->GetSession()->SendNotification("%s", AzDarkmoon::MissingTokenText);
            return false;
        }

        player->DestroyItemCount(AzDarkmoon::ItemGameToken, 1, true);
        return true;
    }

    template<class Pred>
    Creature* ClosestTo(WorldObject const* source, std::list<Creature*>& list, Pred keepIf)
    {
        list.remove_if([&](Creature* c) { return !c || !keepIf(c); });
        if (list.empty())
            return nullptr;

        return *std::min_element(list.begin(), list.end(), [source](Creature* l, Creature* r)
        {
            return source->GetExactDist2d(l) < source->GetExactDist2d(r);
        });
    }
}

// ---------------------------------------------------------------------------------------------
// Fin de foire : les joueurs restés sur l'île sont renvoyés à Elwynn / Mulgore
// ---------------------------------------------------------------------------------------------
namespace
{
    void SendHomeFromIsland(Player* player)
    {
        if (!player || player->GetMapId() != AzDarkmoon::IslandMapId)
            return;

        if (player->GetVehicle())
            player->ExitVehicle();

        if (player->GetTeam() == HORDE)
            player->TeleportTo(1, -1460.82f, 170.258f, -7.44726f, 3.12414f);
        else
            player->TeleportTo(0, -9547.84f, 85.0556f, 60.1021f, 6.23083f);
    }

    bool FaireActive()
    {
        return sGameEventMgr->IsActiveEvent(AzDarkmoon::FaireEventId());
    }
}

class world_azimov_darkmoon_faire : public WorldScript
{
public:
    world_azimov_darkmoon_faire() : WorldScript("world_azimov_darkmoon_faire"), _timer(30 * IN_MILLISECONDS) { }

    void OnUpdate(uint32 diff) override
    {
        if (_timer > diff)
        {
            _timer -= diff;
            return;
        }
        _timer = 30 * IN_MILLISECONDS;

        if (FaireActive())
            return;

        std::vector<ObjectGuid> guids;
        {
            std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
            for (auto const& pair : ObjectAccessor::GetPlayers())
                if (pair.second && pair.second->IsInWorld() && pair.second->GetMapId() == AzDarkmoon::IslandMapId)
                    guids.push_back(pair.first);
        }

        for (ObjectGuid const& guid : guids)
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                SendHomeFromIsland(player);
    }

private:
    uint32 _timer;
};

class player_azimov_darkmoon_faire : public PlayerScript
{
public:
    player_azimov_darkmoon_faire() : PlayerScript("player_azimov_darkmoon_faire") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (player->GetMapId() == AzDarkmoon::IslandMapId && !FaireActive())
            SendHomeFromIsland(player);
    }

    void OnMapChanged(Player* player) override
    {
        if (player->GetMapId() == AzDarkmoon::IslandMapId && !FaireActive())
            SendHomeFromIsland(player);
    }
};

// ---------------------------------------------------------------------------------------------
// Tape-taupe gnoll (quête 29463)
// ---------------------------------------------------------------------------------------------
namespace WhackAGnoll
{
    Position const PlayPosition = { -3994.28f, 6283.58f, 13.12f, 0.727784f };
    uint32 const GnollTimer = 3 * IN_MILLISECONDS;
    uint32 const DollTimer = 6 * IN_MILLISECONDS;
    uint32 const BossTimer = 10 * IN_MILLISECONDS;
    uint32 const PositionTimer = IN_MILLISECONDS;
    float const ArenaRadius = 15.0f;
    float const PlayerScanRange = 30.0f;
    float const BarrelSearchRange = 50.0f;
    float const HammerRange = 3.0f;
    float const HammerArc = 1.5707963f;
    float const BarrelReleaseRange = 5.0f;

    bool IsTarget(uint32 entry) { return entry == NPC_WHACK_GNOLL || entry == NPC_WHACK_DOLL || entry == NPC_WHACK_BOSS; }

    void Cleanup(Player* player, bool castForbiddenZone)
    {
        if (!player)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        player->RemoveAurasDueToSpell(SPELL_WHACK_ENABLE_POWERBAR);
        player->RemoveAurasDueToSpell(SPELL_WHACK_DOLL_STUN);
        if (castForbiddenZone)
            player->CastSpell(player, SPELL_WHACK_FORBIDDEN_ZONE, true);
    }

    void RefreshScore(Player* player)
    {
        if (player)
            player->SetPower(POWER_ALTERNATE_POWER, player->GetReqKillOrCastCurrentCount(QUEST_WHACK_A_GNOLL, NPC_WHACK_GNOLL_CREDIT));
    }

    void ReleaseNearestBarrel(Creature* target)
    {
        std::list<Creature*> barrels;
        target->GetCreatureListWithEntryInGrid(barrels, NPC_WHACK_BARREL, BarrelReleaseRange);
        if (Creature* barrel = ClosestTo(target, barrels, [](Creature*) { return true; }))
            barrel->RemoveAurasDueToSpell(SPELL_WHACK_OK_TO_HIT);
    }

    enum Events { EVENT_SUMMON_GNOLL = 1, EVENT_SUMMON_DOLL, EVENT_SUMMON_BOSS, EVENT_CHECK_PLAYERS };
}

struct npc_whack_gnoll_bunny : public ScriptedAI
{
    npc_whack_gnoll_bunny(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        RefreshBarrels();
        _events.Reset();
        _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_GNOLL, WhackAGnoll::GnollTimer);
        _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_DOLL, WhackAGnoll::DollTimer);
        _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_BOSS, WhackAGnoll::BossTimer);
        _events.ScheduleEvent(WhackAGnoll::EVENT_CHECK_PLAYERS, WhackAGnoll::PositionTimer);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (_barrelGuids.size() < MAX_WHACK_BARRELS)
            RefreshBarrels();

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case WhackAGnoll::EVENT_SUMMON_GNOLL:
                    SummonFromBarrel(SPELL_WHACK_SUMMON_GNOLL);
                    _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_GNOLL, WhackAGnoll::GnollTimer);
                    break;
                case WhackAGnoll::EVENT_SUMMON_DOLL:
                    SummonFromBarrel(SPELL_WHACK_SUMMON_DOLL);
                    _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_DOLL, WhackAGnoll::DollTimer);
                    break;
                case WhackAGnoll::EVENT_SUMMON_BOSS:
                    SummonFromBarrel(SPELL_WHACK_SUMMON_BOSS);
                    _events.ScheduleEvent(WhackAGnoll::EVENT_SUMMON_BOSS, WhackAGnoll::BossTimer);
                    break;
                case WhackAGnoll::EVENT_CHECK_PLAYERS:
                    CheckPlayers();
                    _events.ScheduleEvent(WhackAGnoll::EVENT_CHECK_PLAYERS, WhackAGnoll::PositionTimer);
                    break;
                default:
                    break;
            }
        }
    }

private:
    void RefreshBarrels()
    {
        _barrelGuids.clear();
        std::list<Creature*> barrels;
        me->GetCreatureListWithEntryInGrid(barrels, NPC_WHACK_BARREL, WhackAGnoll::BarrelSearchRange);
        for (Creature* barrel : barrels)
            _barrelGuids.push_back(barrel->GetGUID());
    }

    Creature* GetRandomAvailableBarrel()
    {
        std::list<ObjectGuid> candidates(_barrelGuids);
        while (!candidates.empty())
        {
            ObjectGuid barrelGuid = Trinity::Containers::SelectRandomContainerElement(candidates);
            Creature* barrel = ObjectAccessor::GetCreature(*me, barrelGuid);
            if (barrel && barrel->IsAlive() && !barrel->HasAura(SPELL_WHACK_OK_TO_HIT))
                return barrel;
            candidates.remove(barrelGuid);
        }
        return nullptr;
    }

    void SummonFromBarrel(uint32 spellId)
    {
        Creature* barrel = GetRandomAvailableBarrel();
        if (!barrel)
            return;

        barrel->CastSpell(barrel, spellId, false);
        barrel->CastSpell(barrel, SPELL_WHACK_SPAWN_VISUAL, false);
        barrel->AddAura(SPELL_WHACK_OK_TO_HIT, barrel);
    }

    void CheckPlayers()
    {
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, WhackAGnoll::PlayerScanRange);
        for (Player* player : players)
        {
            if (player->HasAura(SPELL_WHACK_ENABLE_POWERBAR))
            {
                if (player->GetExactDist2d(me) >= WhackAGnoll::ArenaRadius)
                    player->NearTeleportTo(WhackAGnoll::PlayPosition, true);
            }
            else if (player->GetExactDist2d(me) < WhackAGnoll::ArenaRadius)
                player->CastSpell(player, SPELL_WHACK_FORBIDDEN_ZONE, true);
        }
    }

    EventMap _events;
    std::list<ObjectGuid> _barrelGuids;
};

struct npc_whack_gnoll_mola : public ScriptedAI
{
    npc_whack_gnoll_mola(Creature* creature) : ScriptedAI(creature) { }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (me->IsQuestGiver())
            player->PrepareQuestMenu(me->GetGUID());

        if (player->GetQuestStatus(QUEST_WHACK_A_GNOLL) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Je veux jouer au tape-taupe gnoll ! |cFF0000FF(jeton de jeu de Sombrelune)|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        CloseGossipMenuFor(player);

        if (action != GOSSIP_ACTION_INFO_DEF + 1 || !TakeGameToken(player))
            return true;

        WhackAGnoll::Cleanup(player, false);
        player->NearTeleportTo(WhackAGnoll::PlayPosition, true);
        me->AddAura(SPELL_WHACK_OVERRIDE_ACTION, player);
        me->AddAura(SPELL_WHACK_ENABLE_POWERBAR, player);
        ResetGameScore(player, QUEST_WHACK_A_GNOLL, NPC_WHACK_GNOLL_CREDIT);
        return true;
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() == QUEST_WHACK_A_GNOLL)
            WhackAGnoll::Cleanup(player, false);
    }
};

struct npc_whack_gnoll_barrel : public ScriptedAI
{
    npc_whack_gnoll_barrel(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override { me->SetReactState(REACT_PASSIVE); }
    void UpdateAI(uint32 /*diff*/) override { }
};

struct npc_whack_gnoll_target : public ScriptedAI
{
    npc_whack_gnoll_target(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    void UpdateAI(uint32 /*diff*/) override { }
};

// 101604 - Coup de maillet
class spell_whack_gnoll_whack : public SpellScript
{
    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return SPELL_CAST_OK;

        std::list<Creature*> targets;
        player->GetCreatureListWithEntryInGrid(targets, NPC_WHACK_GNOLL, WhackAGnoll::HammerRange);
        player->GetCreatureListWithEntryInGrid(targets, NPC_WHACK_DOLL, WhackAGnoll::HammerRange);
        player->GetCreatureListWithEntryInGrid(targets, NPC_WHACK_BOSS, WhackAGnoll::HammerRange);

        Creature* target = ClosestTo(player, targets, [player](Creature* c)
        {
            return c->IsAlive() && WhackAGnoll::IsTarget(c->GetEntry()) && player->isInFront(c, WhackAGnoll::HammerArc);
        });
        if (!target)
            return SPELL_CAST_OK;

        switch (target->GetEntry())
        {
            case NPC_WHACK_GNOLL:
                player->CastSpell(player, SPELL_WHACK_KILL_CREDIT, true);
                break;
            case NPC_WHACK_DOLL:
                player->CastSpell(player, SPELL_WHACK_DOLL_STUN, true);
                break;
            case NPC_WHACK_BOSS:
                for (uint8 i = 0; i < 3; ++i)
                    player->CastSpell(player, SPELL_WHACK_KILL_CREDIT, true);
                break;
            default:
                break;
        }

        WhackAGnoll::ReleaseNearestBarrel(target);
        Unit::Kill(player, target);
        target->DespawnOrUnsummon(100);
        WhackAGnoll::RefreshScore(player);
        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_whack_gnoll_whack::CheckCast);
    }
};

// 101612 - Tape-taupe gnoll (barre d'action)
class spell_whack_gnoll_override_action : public AuraScript
{
    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
            WhackAGnoll::Cleanup(player, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_whack_gnoll_override_action::HandleEffectRemove, EFFECT_0, SPELL_AURA_OVERRIDE_SPELLS, AURA_EFFECT_HANDLE_REAL);
    }
};

// 7344 - entrée de l'enclos
class at_whack_a_gnoll_entrance : public AreaTriggerScript
{
public:
    at_whack_a_gnoll_entrance() : AreaTriggerScript("at_whack_a_gnoll_entrance") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
    {
        if (!player->HasAura(SPELL_WHACK_ENABLE_POWERBAR))
            player->CastSpell(player, SPELL_WHACK_FORBIDDEN_ZONE, true);
        return false;
    }
};

// ---------------------------------------------------------------------------------------------
// Jeux « à un joueur » avec un forain : stand de tir, lancer d'anneaux, tonk
// ---------------------------------------------------------------------------------------------
namespace
{
    // Menu commun : explication + « jouer »
    void SendBoothMenu(Creature* me, Player* player, uint32 questId, char const* howText, char const* playText)
    {
        ClearGossipMenuFor(player);
        if (me->IsQuestGiver())
            player->PrepareQuestMenu(me->GetGUID());

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, howText, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE && !HasAnyDarkmoonGameAura(player))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, playText, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
    }

    // Renvoie true si le joueur a demandé à jouer et a payé le jeton
    bool HandleBoothSelect(Creature* me, Player* player, uint32 gossipListId, uint32 questId, char const* howText, char const* playText)
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);

        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "D'accord.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return false;
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 3)
        {
            SendBoothMenu(me, player, questId, howText, playText);
            return false;
        }

        CloseGossipMenuFor(player);
        return action == GOSSIP_ACTION_INFO_DEF + 2 && player->HasItemCount(AzDarkmoon::ItemGameToken, 1);
    }

    void PrepareForGame(Player* player)
    {
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->RemoveAurasDueToSpell(SPELL_WHACK_OVERRIDE_ACTION);
        player->RemoveAurasDueToSpell(SPELL_WHACK_ENABLE_POWERBAR);
        player->RemoveAurasDueToSpell(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION);
        player->RemoveAurasDueToSpell(SPELL_RING_TOSS_ACTION);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_ACTION);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_CANNON_BLAST);
    }

    uint32 const GameDuration = 60 * IN_MILLISECONDS;
    uint32 const PlayerCheckInterval = IN_MILLISECONDS;
    enum BoothEvents { EVENT_BOOTH_FINISH = 1, EVENT_BOOTH_CHECK, EVENT_BOOTH_WAVE, EVENT_BOOTH_PLACE_TONK, EVENT_BOOTH_CANCEL };
}

// --- Stand de tir (quête 29438) ---
namespace ShootingGallery
{
    float const ShotRange = 60.0f;
    float const ShotArc = 0.87266463f;
    float const BoothRange = 70.0f;
    float const TargetSearchRange = 20.0f;
    uint32 const WaveInterval = 5 * IN_MILLISECONDS;

    Position const Spots[3] =
    {
        { -4068.08f, 6353.26f, 13.4977f, 4.04916f },
        { -4069.91f, 6355.00f, 13.4849f, 4.13643f },
        { -4072.09f, 6356.73f, 13.4903f, 4.31096f }
    };

    char const* const HowText = "Comment fonctionne le stand de tir ?";
    char const* const PlayText = "Je suis prêt à tirer ! |cFF0000FF(jeton de jeu de Sombrelune)|r";

    float Dist2(Creature const* c, Position const& p)
    {
        float const dx = c->GetPositionX() - p.GetPositionX();
        float const dy = c->GetPositionY() - p.GetPositionY();
        return dx * dx + dy * dy;
    }

    uint32 SpotIndex(Creature const* c)
    {
        uint32 best = 0;
        for (uint32 i = 1; i < 3; ++i)
            if (Dist2(c, Spots[i]) < Dist2(c, Spots[best]))
                best = i;
        return best;
    }

    bool IsOnSpot(Creature const* c)
    {
        for (Position const& p : Spots)
            if (Dist2(c, p) <= 1.0f)
                return true;
        return false;
    }

    void ClearTarget(Creature* target)
    {
        target->RemoveAurasDueToSpell(SPELL_SHOOTING_GALLERY_TARGET_INDICATOR);
        if (target->AI())
        {
            target->AI()->SetGUID(ObjectGuid::Empty, DATA_ACTIVE_PLAYER);
            target->AI()->SetData(DATA_ACTIVE_TARGET, 0);
        }
    }

    void GetTargets(Creature* rinling, std::list<Creature*>& targets)
    {
        rinling->GetCreatureListWithEntryInGrid(targets, NPC_SHOOTING_GALLERY_TARGET, TargetSearchRange);
        targets.remove_if([](Creature* t) { return !t || !t->IsAlive() || !IsOnSpot(t); });
        targets.sort([](Creature const* l, Creature const* r) { return SpotIndex(l) < SpotIndex(r); });
    }

    void ClearTargets(Creature* rinling)
    {
        std::list<Creature*> targets;
        GetTargets(rinling, targets);
        for (Creature* t : targets)
            ClearTarget(t);
    }

    uint32 Score(Player* player)
    {
        return AzDarkmoon::Min(player->GetReqKillOrCastCurrentCount(QUEST_SHOOTING_GALLERY, NPC_SHOOTING_GALLERY_TARGET), SHOOTING_GALLERY_TARGET_COUNT);
    }
}

struct npc_darkmoon_rinling : public ScriptedAI
{
    npc_darkmoon_rinling(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        ShootingGallery::ClearTargets(me);
        _player.Clear();
    }

    bool GossipHello(Player* player) override
    {
        SendBoothMenu(me, player, QUEST_SHOOTING_GALLERY, ShootingGallery::HowText, ShootingGallery::PlayText);
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        bool const wantsToPlay = GetGossipActionFor(player, gossipListId) == GOSSIP_ACTION_INFO_DEF + 2;
        if (!HandleBoothSelect(me, player, gossipListId, QUEST_SHOOTING_GALLERY, ShootingGallery::HowText, ShootingGallery::PlayText))
        {
            if (wantsToPlay)
                player->GetSession()->SendNotification("%s", AzDarkmoon::MissingTokenText);
            return true;
        }

        if (!StartGame(player))
        {
            me->Whisper("Le stand de tir est déjà occupé.", LANG_UNIVERSAL, player);
            return true;
        }

        TakeGameToken(player);
        return true;
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_SHOOTING_GALLERY)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        player->RemoveAurasDueToSpell(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION);
        if (_player == player->GetGUID())
            EndGame(false);
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_END_GAME)
            EndGame(false);
        else if (action == ACTION_COMPLETE_GAME)
            EndGame(true);
    }

    ObjectGuid GetGUID(int32 id) const override
    {
        return id == DATA_ACTIVE_PLAYER ? _player : ObjectGuid::Empty;
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BOOTH_WAVE:
                    SpawnWave();
                    _events.ScheduleEvent(EVENT_BOOTH_WAVE, ShootingGallery::WaveInterval);
                    break;
                case EVENT_BOOTH_FINISH:
                    EndGame(true);
                    break;
                case EVENT_BOOTH_CHECK:
                    CheckPlayer();
                    if (!_player.IsEmpty())
                        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
                    break;
                default:
                    break;
            }
        }
    }

private:
    bool StartGame(Player* player)
    {
        if (!_player.IsEmpty())
            return false;

        _player = player->GetGUID();
        _events.Reset();
        ShootingGallery::ClearTargets(me);

        PrepareForGame(player);
        player->AddAura(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION, player);
        ResetGameScore(player, QUEST_SHOOTING_GALLERY, NPC_SHOOTING_GALLERY_TARGET);

        _events.ScheduleEvent(EVENT_BOOTH_WAVE, 500);
        _events.ScheduleEvent(EVENT_BOOTH_FINISH, GameDuration);
        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
        return true;
    }

    void EndGame(bool removeAura)
    {
        ObjectGuid const guid = _player;
        _player.Clear();
        _events.Reset();
        ShootingGallery::ClearTargets(me);

        if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
        {
            player->SetPower(POWER_ALTERNATE_POWER, 0);
            if (removeAura)
                player->RemoveAurasDueToSpell(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION);
        }
    }

    void CheckPlayer()
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _player);
        if (!player || !player->IsAlive() || !player->HasAura(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION) || player->GetExactDist2d(me) > ShootingGallery::BoothRange)
        {
            EndGame(false);
            return;
        }

        uint32 const score = ShootingGallery::Score(player);
        player->SetPower(POWER_ALTERNATE_POWER, score);
        if (score >= SHOOTING_GALLERY_TARGET_COUNT)
            EndGame(true);
    }

    void SpawnWave()
    {
        if (_player.IsEmpty())
            return;

        std::list<Creature*> targets;
        ShootingGallery::GetTargets(me, targets);
        if (targets.empty())
            return;

        uint32 const active = urand(0, uint32(targets.size()) - 1);
        uint32 index = 0;
        for (Creature* target : targets)
        {
            ShootingGallery::ClearTarget(target);
            if (index++ == active && target->AI())
            {
                target->AI()->SetGUID(_player, DATA_ACTIVE_PLAYER);
                target->AI()->SetData(DATA_ACTIVE_TARGET, 1);
                target->CastSpell(target, SPELL_SHOOTING_GALLERY_TARGET_INDICATOR, false);
            }
        }
    }

    EventMap _events;
    ObjectGuid _player;
};

struct npc_darkmoon_shooting_gallery_target : public ScriptedAI
{
    npc_darkmoon_shooting_gallery_target(Creature* creature) : ScriptedAI(creature), _spawnTime(getMSTime()), _hitCount(0), _active(false) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->RemoveAurasDueToSpell(SPELL_SHOOTING_GALLERY_TARGET_INDICATOR);
        _player.Clear();
        _spawnTime = getMSTime();
        _hitCount = 0;
        _active = false;
    }

    void UpdateAI(uint32 /*diff*/) override { }

    uint32 GetData(uint32 id) const override
    {
        switch (id)
        {
            case DATA_SPAWN_TIME:    return _spawnTime;
            case DATA_HIT_COUNT:     return _hitCount;
            case DATA_ACTIVE_TARGET: return _active ? 1 : 0;
            default:                 return 0;
        }
    }

    void SetData(uint32 id, uint32 value) override
    {
        if (id == DATA_HIT_COUNT)
            _hitCount = value;
        else if (id == DATA_ACTIVE_TARGET)
        {
            _active = value != 0;
            _spawnTime = getMSTime();
            _hitCount = 0;
            if (!_active)
                _player.Clear();
        }
    }

    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_ACTIVE_PLAYER)
            _player = guid;
    }

    ObjectGuid GetGUID(int32 id) const override
    {
        return id == DATA_ACTIVE_PLAYER ? _player : ObjectGuid::Empty;
    }

private:
    ObjectGuid _player;
    uint32 _spawnTime;
    uint32 _hitCount;
    bool _active;
};

// 101872 - Tir
class spell_darkmoon_shooting_gallery_shoot : public SpellScript
{
    void HandleAfterCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player || !player->HasAura(SPELL_SHOOTING_GALLERY_OVERRIDE_ACTION))
            return;

        std::list<Creature*> targets;
        player->GetCreatureListWithEntryInGrid(targets, NPC_SHOOTING_GALLERY_TARGET, ShootingGallery::ShotRange);
        Creature* target = ClosestTo(player, targets, [player](Creature* c)
        {
            return c->IsAlive() && c->AI() && c->AI()->GetGUID(DATA_ACTIVE_PLAYER) == player->GetGUID()
                && c->AI()->GetData(DATA_ACTIVE_TARGET) != 0 && player->isInFront(c, ShootingGallery::ShotArc);
        });
        if (!target)
            return;

        uint32 const current = ShootingGallery::Score(player);
        if (current >= SHOOTING_GALLERY_TARGET_COUNT)
        {
            ShootingGallery::ClearTarget(target);
            return;
        }

        uint32 const elapsed = getMSTimeDiff(target->AI()->GetData(DATA_SPAWN_TIME), getMSTime());
        uint32 const credit = elapsed <= SHOOTING_GALLERY_QUICK_SHOT_MS ? 2 : 1;
        uint32 const newScore = AzDarkmoon::Min(current + credit, SHOOTING_GALLERY_TARGET_COUNT);
        for (uint32 i = current; i < newScore; ++i)
            player->KilledMonsterCredit(NPC_SHOOTING_GALLERY_TARGET);
        player->SetPower(POWER_ALTERNATE_POWER, newScore);

        if (credit > 1)
            CompleteAchievement(player, ACHIEVEMENT_QUICK_SHOT);

        uint32 const hits = target->AI()->GetData(DATA_HIT_COUNT) + 1;
        target->AI()->SetData(DATA_HIT_COUNT, hits);
        if (hits >= SHOOTING_GALLERY_MAX_HITS_PER_TARGET || newScore >= SHOOTING_GALLERY_TARGET_COUNT)
            ShootingGallery::ClearTarget(target);

        if (newScore >= SHOOTING_GALLERY_TARGET_COUNT)
            if (Creature* rinling = GetClosestCreatureWithEntry(player, NPC_DARKMOON_RINLING, ShootingGallery::BoothRange))
                if (rinling->AI() && rinling->AI()->GetGUID(DATA_ACTIVE_PLAYER) == player->GetGUID())
                    rinling->AI()->DoAction(ACTION_COMPLETE_GAME);
    }

    void Register() override
    {
        AfterCast.Register(&spell_darkmoon_shooting_gallery_shoot::HandleAfterCast);
    }
};

// 101871 - Stand de tir (barre d'action)
class spell_darkmoon_shooting_gallery_override_action : public AuraScript
{
    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        if (Creature* rinling = GetClosestCreatureWithEntry(player, NPC_DARKMOON_RINLING, ShootingGallery::BoothRange))
            if (rinling->AI() && rinling->AI()->GetGUID(DATA_ACTIVE_PLAYER) == player->GetGUID())
                rinling->AI()->DoAction(ACTION_END_GAME);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_darkmoon_shooting_gallery_override_action::HandleEffectRemove, EFFECT_0, SPELL_AURA_OVERRIDE_SPELLS, AURA_EFFECT_HANDLE_REAL);
    }
};

// --- Lancer d'anneaux (quête 29455) ---
namespace RingToss
{
    float const BoothRange = 65.0f;
    float const TurtleSearchRange = 45.0f;
    float const HitRadius = 2.75f;
    float const VisualSpeed = 12.0f;
    uint32 const DefaultVisual = 21471;

    char const* const HowText = "Comment fonctionne le lancer d'anneaux ?";
    char const* const PlayText = "Je suis prêt à lancer des anneaux ! |cFF0000FF(jeton de jeu de Sombrelune)|r";

    uint32 Score(Player* player)
    {
        return AzDarkmoon::Min(player->GetReqKillOrCastCurrentCount(QUEST_TARGET_TURTLE, NPC_RING_TOSS_CREDIT), RING_TOSS_TARGET_COUNT);
    }

    void SetRemaining(Player* player, uint32 throws)
    {
        player->SetPower(POWER_ALTERNATE_POWER, throws >= RING_TOSS_MAX_THROWS ? 0 : RING_TOSS_MAX_THROWS - throws);
    }

    float Dist2(Creature const* c, WorldLocation const* dest)
    {
        float const dx = c->GetPositionX() - dest->GetPositionX();
        float const dy = c->GetPositionY() - dest->GetPositionY();
        return dx * dx + dy * dy;
    }
}

struct npc_darkmoon_jessica_rogers : public ScriptedAI
{
    npc_darkmoon_jessica_rogers(Creature* creature) : ScriptedAI(creature), _throws(0) { }

    void Reset() override
    {
        _events.Reset();
        _player.Clear();
        _throws = 0;
    }

    bool GossipHello(Player* player) override
    {
        SendBoothMenu(me, player, QUEST_TARGET_TURTLE, RingToss::HowText, RingToss::PlayText);
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        bool const wantsToPlay = GetGossipActionFor(player, gossipListId) == GOSSIP_ACTION_INFO_DEF + 2;
        if (!HandleBoothSelect(me, player, gossipListId, QUEST_TARGET_TURTLE, RingToss::HowText, RingToss::PlayText))
        {
            if (wantsToPlay)
                player->GetSession()->SendNotification("%s", AzDarkmoon::MissingTokenText);
            return true;
        }

        if (!StartGame(player))
        {
            me->Whisper("Le lancer d'anneaux est déjà occupé.", LANG_UNIVERSAL, player);
            return true;
        }

        TakeGameToken(player);
        return true;
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_TARGET_TURTLE)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        player->RemoveAurasDueToSpell(SPELL_RING_TOSS_ACTION);
        if (_player == player->GetGUID())
            EndGame(false);
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_END_GAME)
            EndGame(false);
        else if (action == ACTION_COMPLETE_GAME)
            EndGame(true);
    }

    ObjectGuid GetGUID(int32 id) const override
    {
        return id == DATA_ACTIVE_PLAYER ? _player : ObjectGuid::Empty;
    }

    bool RegisterThrow(Player* player)
    {
        if (_player != player->GetGUID())
            return false;

        ++_throws;
        RingToss::SetRemaining(player, _throws);
        return true;
    }

    void CheckThrowLimit(Player* player)
    {
        if (_player == player->GetGUID() && _throws >= RING_TOSS_MAX_THROWS && RingToss::Score(player) < RING_TOSS_TARGET_COUNT)
            EndGame(true);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BOOTH_FINISH:
                    EndGame(true);
                    break;
                case EVENT_BOOTH_CHECK:
                    CheckPlayer();
                    if (!_player.IsEmpty())
                        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
                    break;
                default:
                    break;
            }
        }
    }

private:
    bool StartGame(Player* player)
    {
        if (!_player.IsEmpty())
            return false;

        PrepareForGame(player);
        _player = player->GetGUID();
        _throws = 0;
        _events.Reset();

        player->AddAura(SPELL_RING_TOSS_ACTION, player);
        ResetGameScore(player, QUEST_TARGET_TURTLE, NPC_RING_TOSS_CREDIT);
        RingToss::SetRemaining(player, 0);

        _events.ScheduleEvent(EVENT_BOOTH_FINISH, GameDuration);
        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
        return true;
    }

    void EndGame(bool removeAura)
    {
        ObjectGuid const guid = _player;
        _player.Clear();
        _throws = 0;
        _events.Reset();

        if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
        {
            player->SetPower(POWER_ALTERNATE_POWER, 0);
            if (removeAura)
                player->RemoveAurasDueToSpell(SPELL_RING_TOSS_ACTION);
        }
    }

    void CheckPlayer()
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _player);
        if (!player || !player->IsAlive() || !player->HasAura(SPELL_RING_TOSS_ACTION) || player->GetExactDist2d(me) > RingToss::BoothRange)
        {
            EndGame(false);
            return;
        }

        RingToss::SetRemaining(player, _throws);
        if (RingToss::Score(player) >= RING_TOSS_TARGET_COUNT)
            EndGame(true);
    }

    EventMap _events;
    ObjectGuid _player;
    uint32 _throws;
};

// 101695 - Lancer d'anneau
class spell_darkmoon_ring_toss_throw : public SpellScript
{
    void HandleAfterCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player || !player->HasAura(SPELL_RING_TOSS_ACTION))
            return;

        WorldLocation const* dest = GetExplTargetDest();
        if (!dest)
            return;

        Creature* jessica = GetClosestCreatureWithEntry(player, NPC_DARKMOON_JESSICA_ROGERS, RingToss::BoothRange);
        npc_darkmoon_jessica_rogers* ai = jessica ? CAST_AI(npc_darkmoon_jessica_rogers, jessica->AI()) : nullptr;
        if (!ai || !ai->RegisterThrow(player))
            return;

        std::list<Creature*> turtles;
        player->GetCreatureListWithEntryInGrid(turtles, NPC_DARKMOON_DUBENKO, RingToss::TurtleSearchRange);
        turtles.remove_if([dest](Creature* t) { return !t || !t->IsAlive() || RingToss::Dist2(t, dest) > RingToss::HitRadius * RingToss::HitRadius; });
        Creature* turtle = nullptr;
        if (!turtles.empty())
            turtle = *std::min_element(turtles.begin(), turtles.end(), [dest](Creature* l, Creature* r) { return RingToss::Dist2(l, dest) < RingToss::Dist2(r, dest); });

        uint32 const visual = GetSpellInfo()->SpellVisual[0] ? GetSpellInfo()->SpellVisual[0] : RingToss::DefaultVisual;
        if (turtle)
            player->SendPlaySpellVisual(visual, turtle, {}, RingToss::VisualSpeed);
        else
            player->SendPlaySpellVisual(visual, nullptr, Position(dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ() + 1.25f), RingToss::VisualSpeed);

        if (turtle && RingToss::Score(player) < RING_TOSS_TARGET_COUNT)
        {
            player->KilledMonsterCredit(NPC_RING_TOSS_CREDIT);
            if (RingToss::Score(player) >= RING_TOSS_TARGET_COUNT)
            {
                ai->DoAction(ACTION_COMPLETE_GAME);
                return;
            }
        }

        ai->CheckThrowLimit(player);
    }

    void Register() override
    {
        AfterCast.Register(&spell_darkmoon_ring_toss_throw::HandleAfterCast);
    }
};

// 102058 - Lancer d'anneaux (barre d'action)
class spell_darkmoon_ring_toss_action : public AuraScript
{
    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        if (Creature* jessica = GetClosestCreatureWithEntry(player, NPC_DARKMOON_JESSICA_ROGERS, RingToss::BoothRange))
            if (jessica->AI() && jessica->AI()->GetGUID(DATA_ACTIVE_PLAYER) == player->GetGUID())
                jessica->AI()->DoAction(ACTION_END_GAME);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_darkmoon_ring_toss_action::HandleEffectRemove, EFFECT_0, SPELL_AURA_OVERRIDE_SPELLS, AURA_EFFECT_HANDLE_REAL);
    }
};

// --- Défi des tonks (quête 29434) ---
namespace Tonk
{
    uint32 const VehicleId = 1734;
    uint32 const WaveInterval = 30 * IN_MILLISECONDS;
    uint32 const TargetDespawn = 25 * IN_MILLISECONDS;
    uint32 const PlaceDelay = 250;
    float const BoothRange = 110.0f;
    float const TonkSearchRange = 45.0f;
    float const TargetSearchRange = 65.0f;

    Position const Start = { -4129.767578f, 6321.413086f, 13.116933f, 4.30912f };
    Position const Return = { -4128.246582f, 6324.775879f, 13.117409f, 4.30912f };
    Position const Targets[18] =
    {
        { -4140.77f, 6315.62f, 13.1177f, 0.26241f }, { -4148.82f, 6303.68f, 13.1177f, 0.36451f }, { -4143.79f, 6299.65f, 13.1177f, 0.32523f },
        { -4142.58f, 6309.06f, 13.1177f, 0.64332f }, { -4134.22f, 6303.87f, 13.1177f, 1.51511f }, { -4134.84f, 6293.24f, 13.1169f, 1.60936f },
        { -4130.75f, 6299.12f, 13.1169f, 2.49686f }, { -4129.41f, 6307.05f, 13.1169f, 2.14343f }, { -4126.29f, 6314.47f, 13.1175f, 2.36158f },
        { -4129.05f, 6302.49f, 13.1166f, 2.30239f }, { -4131.39f, 6296.11f, 13.1165f, 2.38578f }, { -4140.39f, 6298.14f, 13.1165f, 1.21451f },
        { -4148.82f, 6306.59f, 13.1165f, 0.34664f }, { -4139.23f, 6312.75f, 13.1165f, 0.99067f }, { -4140.27f, 6307.18f, 13.1165f, 1.17917f },
        { -4137.38f, 6303.69f, 13.1165f, 1.47371f }, { -4128.18f, 6308.92f, 13.1165f, 2.96988f }, { -4127.59f, 6313.02f, 13.1165f, 2.84422f }
    };

    char const* const HowText = "Comment fonctionne le défi des tonks ?";
    char const* const PlayText = "Je suis prêt à commander un tonk ! |cFF0000FF(jeton de jeu de Sombrelune)|r";

    uint32 Score(Player* player)
    {
        return AzDarkmoon::Min(player->GetReqKillOrCastCurrentCount(QUEST_TONK_COMMANDER, NPC_TONK_CANNON_TARGET), TONK_CHALLENGE_TARGET_COUNT);
    }

    bool IsPlayerTonk(Creature const* tonk, Player const* player)
    {
        if (!tonk || !player || tonk->GetEntry() != NPC_DARKMOON_STEAM_TONK)
            return false;

        ObjectGuid const guid = player->GetGUID();
        if (tonk->GetCharmerGUID() == guid || tonk->GetOwnerGUID() == guid || tonk->GetCreatorGUID() == guid)
            return true;

        if (TempSummon const* summon = tonk->ToTempSummon())
            return summon->GetSummonerGUID() == guid;
        return false;
    }

    Creature* FindPlayerTonk(Player* player)
    {
        if (Unit* vehicle = player->GetVehicleBase())
            if (Creature* tonk = vehicle->ToCreature())
                if (IsPlayerTonk(tonk, player))
                    return tonk;

        if (Unit* charm = player->GetCharmed())
            if (Creature* tonk = charm->ToCreature())
                if (IsPlayerTonk(tonk, player))
                    return tonk;

        std::list<Creature*> tonks;
        player->GetCreatureListWithEntryInGrid(tonks, NPC_DARKMOON_STEAM_TONK, TonkSearchRange);
        return ClosestTo(player, tonks, [player](Creature* t) { return IsPlayerTonk(t, player); });
    }

    void RemoveAuras(Player* player)
    {
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_TARGET_TRACKER);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_TARGET_PARTICIPANT);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_CANNON_BLAST);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_ACTION);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_TONK_CONTROLLER);
    }

    void DespawnTonk(Player* player)
    {
        Creature* tonk = FindPlayerTonk(player);
        if (player->GetVehicle())
            player->ExitVehicle();
        if (tonk)
            tonk->DespawnOrUnsummon(1);
    }

    void ClearTargets(WorldObject* source)
    {
        std::list<Creature*> targets;
        source->GetCreatureListWithEntryInGrid(targets, NPC_TONK_CANNON_TARGET, TargetSearchRange);
        for (Creature* target : targets)
            if (target->IsSummon())
                target->DespawnOrUnsummon(1);
    }

    bool EnsureVehicleKit(Creature* tonk)
    {
        return tonk->IsVehicle() || tonk->CreateVehicleKit(VehicleId, NPC_DARKMOON_STEAM_TONK);
    }

    void CancelChallenge(Player* player, WorldObject* source)
    {
        Creature* finlay = GetClosestCreatureWithEntry(source, NPC_DARKMOON_FINLAY_COOLSHOT, BoothRange);
        if (finlay && finlay->AI() && finlay->AI()->GetGUID(DATA_ACTIVE_PLAYER) == player->GetGUID())
            finlay->AI()->DoAction(ACTION_CANCEL_GAME);
    }
}

struct npc_darkmoon_finlay_coolshot : public ScriptedAI
{
    npc_darkmoon_finlay_coolshot(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        Tonk::ClearTargets(me);
        _player.Clear();
    }

    bool GossipHello(Player* player) override
    {
        SendBoothMenu(me, player, QUEST_TONK_COMMANDER, Tonk::HowText, Tonk::PlayText);
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        bool const wantsToPlay = GetGossipActionFor(player, gossipListId) == GOSSIP_ACTION_INFO_DEF + 2;
        if (!HandleBoothSelect(me, player, gossipListId, QUEST_TONK_COMMANDER, Tonk::HowText, Tonk::PlayText))
        {
            if (wantsToPlay)
                player->GetSession()->SendNotification("%s", AzDarkmoon::MissingTokenText);
            return true;
        }

        if (!StartGame(player))
        {
            me->Whisper("Le défi des tonks est déjà occupé.", LANG_UNIVERSAL, player);
            return true;
        }

        TakeGameToken(player);
        return true;
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() != QUEST_TONK_COMMANDER)
            return;

        player->SetPower(POWER_ALTERNATE_POWER, 0);
        Tonk::RemoveAuras(player);
        Tonk::DespawnTonk(player);
        if (_player == player->GetGUID())
            EndGame(false);
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_END_GAME)
            EndGame(false);
        else if (action == ACTION_COMPLETE_GAME)
            EndGame(true);
        else if (action == ACTION_CANCEL_GAME && !_player.IsEmpty())
            _events.ScheduleEvent(EVENT_BOOTH_CANCEL, 1);
    }

    ObjectGuid GetGUID(int32 id) const override
    {
        return id == DATA_ACTIVE_PLAYER ? _player : ObjectGuid::Empty;
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BOOTH_PLACE_TONK:
                    if (!PlaceTonk() && !_player.IsEmpty())
                        _events.ScheduleEvent(EVENT_BOOTH_PLACE_TONK, Tonk::PlaceDelay);
                    break;
                case EVENT_BOOTH_WAVE:
                    SpawnWave();
                    if (!_player.IsEmpty())
                        _events.ScheduleEvent(EVENT_BOOTH_WAVE, Tonk::WaveInterval);
                    break;
                case EVENT_BOOTH_FINISH:
                case EVENT_BOOTH_CANCEL:
                    EndGame(true);
                    break;
                case EVENT_BOOTH_CHECK:
                    CheckPlayer();
                    if (!_player.IsEmpty())
                        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
                    break;
                default:
                    break;
            }
        }
    }

private:
    bool StartGame(Player* player)
    {
        if (!_player.IsEmpty())
            return false;

        _player = player->GetGUID();
        _events.Reset();
        Tonk::ClearTargets(me);

        PrepareForGame(player);
        Tonk::RemoveAuras(player);
        ResetGameScore(player, QUEST_TONK_COMMANDER, NPC_TONK_CANNON_TARGET);
        player->CastSpell(player, SPELL_DARKMOON_TONK_CONTROLLER, true);

        _events.ScheduleEvent(EVENT_BOOTH_PLACE_TONK, Tonk::PlaceDelay);
        _events.ScheduleEvent(EVENT_BOOTH_WAVE, IN_MILLISECONDS);
        _events.ScheduleEvent(EVENT_BOOTH_FINISH, GameDuration);
        _events.ScheduleEvent(EVENT_BOOTH_CHECK, PlayerCheckInterval);
        return true;
    }

    void EndGame(bool removeAuras)
    {
        ObjectGuid const guid = _player;
        _player.Clear();
        _events.Reset();
        Tonk::ClearTargets(me);

        if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
        {
            player->SetPower(POWER_ALTERNATE_POWER, 0);
            if (removeAuras)
                Tonk::RemoveAuras(player);
            Tonk::DespawnTonk(player);
            if (player->GetMapId() == AzDarkmoon::IslandMapId)
                player->NearTeleportTo(Tonk::Return, true);
        }
    }

    bool PlaceTonk()
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _player);
        if (!player)
            return false;

        Creature* tonk = Tonk::FindPlayerTonk(player);
        if (!tonk)
            return false;

        if (player->GetVehicleBase() != tonk)
        {
            tonk->SetReactState(REACT_PASSIVE);
            tonk->SetHomePosition(Tonk::Start);
            tonk->NearTeleportTo(Tonk::Start, true);
        }

        if (!Tonk::EnsureVehicleKit(tonk) || player->GetVehicleBase() == tonk || tonk->HasAuraTypeWithCaster(SPELL_AURA_CONTROL_VEHICLE, player->GetGUID()))
            return true;

        tonk->SendSetVehicleRecId(Tonk::VehicleId);
        if (player->GetVehicle())
            player->ExitVehicle();
        player->EnterVehicle(tonk, 0);
        return true;
    }

    void CheckPlayer()
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _player);
        if (!player || !player->IsAlive() || player->GetQuestStatus(QUEST_TONK_COMMANDER) != QUEST_STATUS_INCOMPLETE || player->GetExactDist2d(me) > Tonk::BoothRange)
        {
            EndGame(true);
            return;
        }

        uint32 const score = Tonk::Score(player);
        player->SetPower(POWER_ALTERNATE_POWER, score);
        if (score >= TONK_CHALLENGE_TARGET_COUNT)
            EndGame(true);
    }

    void SpawnWave()
    {
        if (_player.IsEmpty())
            return;

        Tonk::ClearTargets(me);
        for (Position const& pos : Tonk::Targets)
        {
            Creature* target = me->SummonCreature(NPC_TONK_CANNON_TARGET, pos, TEMPSUMMON_TIMED_DESPAWN, Tonk::TargetDespawn);
            if (!target)
                continue;

            target->SetReactState(REACT_PASSIVE);
            target->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
            if (target->AI())
                target->AI()->SetGUID(_player, DATA_ACTIVE_PLAYER);
        }
    }

    EventMap _events;
    ObjectGuid _player;
};

struct npc_darkmoon_steam_tonk : public ScriptedAI
{
    npc_darkmoon_steam_tonk(Creature* creature) : ScriptedAI(creature), _ending(false) { }

    void Reset() override
    {
        _player.Clear();
        _ending = false;
        Tonk::EnsureVehicleKit(me);
        me->SetReactState(REACT_PASSIVE);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (damage && damage >= me->GetHealth())
            EndOnLoss();
    }

    void JustDied(Unit* /*killer*/) override
    {
        EndOnLoss();
    }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
        Player* player = passenger ? passenger->ToPlayer() : nullptr;
        if (!player)
            return;

        _player = player->GetGUID();
        if (apply || _ending)
            return;

        _ending = true;
        Tonk::CancelChallenge(player, me);
    }

    void UpdateAI(uint32 /*diff*/) override { }

private:
    void EndOnLoss()
    {
        if (_ending)
            return;

        Player* player = !_player.IsEmpty() ? ObjectAccessor::GetPlayer(*me, _player) : nullptr;
        if (!player)
            player = me->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player || !Tonk::IsPlayerTonk(me, player))
            return;

        _ending = true;
        Tonk::CancelChallenge(player, me);
    }

    ObjectGuid _player;
    bool _ending;
};

struct npc_darkmoon_tonk_cannon_target : public ScriptedAI
{
    npc_darkmoon_tonk_cannon_target(Creature* creature) : ScriptedAI(creature), _credited(false) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        _credited = false;
    }

    void UpdateAI(uint32 /*diff*/) override { }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override { damage = 0; }

    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_ACTIVE_PLAYER)
            _player = guid;
    }

    ObjectGuid GetGUID(int32 id) const override
    {
        return id == DATA_ACTIVE_PLAYER ? _player : ObjectGuid::Empty;
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (_credited || !caster || !spellInfo || spellInfo->Id != SPELL_DARKMOON_TONK_SHOT)
            return;

        Player* player = caster->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player || player->GetGUID() != _player || player->GetQuestStatus(QUEST_TONK_COMMANDER) != QUEST_STATUS_INCOMPLETE)
            return;

        Creature* finlay = GetClosestCreatureWithEntry(me, NPC_DARKMOON_FINLAY_COOLSHOT, Tonk::BoothRange);
        if (!finlay || !finlay->AI() || finlay->AI()->GetGUID(DATA_ACTIVE_PLAYER) != player->GetGUID())
            return;

        _credited = true;
        if (Tonk::Score(player) < TONK_CHALLENGE_TARGET_COUNT)
            player->KilledMonsterCredit(NPC_TONK_CANNON_TARGET);

        uint32 const score = Tonk::Score(player);
        player->SetPower(POWER_ALTERNATE_POWER, score);
        if (score >= TONK_CHALLENGE_TARGET_COUNT)
            finlay->AI()->DoAction(ACTION_COMPLETE_GAME);

        me->DespawnOrUnsummon(1);
    }

private:
    ObjectGuid _player;
    bool _credited;
};

// 100752 - Contrôleur de tonk : le tonk apparaît au départ de l'arène
class spell_darkmoon_tonk_controller : public SpellScript
{
    void HandleSummonPosition(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster || caster->GetMapId() != AzDarkmoon::IslandMapId)
            return;

        WorldLocation const pos(AzDarkmoon::IslandMapId, Tonk::Start);
        SetExplTargetDest(pos);
        if (WorldLocation* hitDest = GetHitDest())
            hitDest->WorldRelocate(pos);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_darkmoon_tonk_controller::HandleSummonPosition, EFFECT_ALL, SPELL_EFFECT_SUMMON);
    }
};

// ---------------------------------------------------------------------------------------------
// Canon humain (quête 29436)
// ---------------------------------------------------------------------------------------------
namespace Cannon
{
    uint32 const CheckInterval = 250;
    uint32 const AmbientInterval = 30 * IN_MILLISECONDS;
    uint32 const ReturnCost = 3000;
    float const LandingSearchRange = 90.0f;
    float const LandingReadyZ = 0.75f;
    float const BullseyeDistance = 3.0f;
    float const NearDistance = 28.0f;
    float const SpeedXY = 55.0f;
    float const SpeedZ = 48.2f;

    Position const Launch = { -4018.15f, 6299.57f, 13.11f, 3.30732f };
    Position const Source = { -4017.1637f, 6299.7350f, 13.11f, 3.30732f };
    Position const Booth = { -4019.0f, 6286.58f, 12.49f, 1.39f };

    uint32 Score(Player* player)
    {
        return AzDarkmoon::Min(player->GetReqKillOrCastCurrentCount(QUEST_HUMANOID_CANNONBALL, NPC_DARKMOON_CANNON_TARGET_CREDIT), DARKMOON_CANNON_TARGET_POINTS);
    }

    void RemoveAuras(Player* player)
    {
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_CANNON_PREPARATION);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_CANNON_MAGIC_WINGS);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_CANNON_LAUNCH);
        player->RemoveAurasDueToSpell(SPELL_DARKMOON_CANNON_ROOT);
    }

    enum Events { EVENT_CHECK_PLAYERS = 1, EVENT_AMBIENT };
}

struct npc_darkmoon_maxima_blastenheimer : public ScriptedAI
{
    npc_darkmoon_maxima_blastenheimer(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(Cannon::EVENT_AMBIENT, Cannon::AmbientInterval);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            if (eventId == Cannon::EVENT_AMBIENT)
            {
                Talk(1);
                _events.ScheduleEvent(Cannon::EVENT_AMBIENT, Cannon::AmbientInterval);
            }
        }
    }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (me->IsQuestGiver())
            player->PrepareQuestMenu(me->GetGUID());

        Talk(0, player);

        if (player->GetQuestStatus(QUEST_HUMANOID_CANNONBALL) == QUEST_STATUS_INCOMPLETE && Cannon::Score(player) < DARKMOON_CANNON_TARGET_POINTS && !HasAnyDarkmoonGameAura(player))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Lancez-moi ! |cFF0000FF(jeton de jeu de Sombrelune)|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        CloseGossipMenuFor(player);

        if (action != GOSSIP_ACTION_INFO_DEF + 1 || !TakeGameToken(player))
            return true;

        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        PrepareForGame(player);
        Cannon::RemoveAuras(player);
        player->NearTeleportTo(Cannon::Launch, true);
        player->CastSpell(player, SPELL_DARKMOON_CANNON_ROOT, true);
        player->CastSpell(player, SPELL_DARKMOON_CANNON_PREPARATION, true);
        return true;
    }

    void QuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() == QUEST_HUMANOID_CANNONBALL)
            Cannon::RemoveAuras(player);
    }

private:
    EventMap _events;
};

struct npc_darkmoon_cannon_target : public ScriptedAI
{
    npc_darkmoon_cannon_target(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        me->SetReactState(REACT_PASSIVE);
        _events.ScheduleEvent(Cannon::EVENT_CHECK_PLAYERS, Cannon::CheckInterval);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            if (eventId == Cannon::EVENT_CHECK_PLAYERS)
            {
                CheckPlayers();
                _events.ScheduleEvent(Cannon::EVENT_CHECK_PLAYERS, Cannon::CheckInterval);
            }
        }
    }

private:
    void CheckPlayers()
    {
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, Cannon::LandingSearchRange);
        for (Player* player : players)
        {
            if (player->GetMapId() != AzDarkmoon::IslandMapId || player->GetQuestStatus(QUEST_HUMANOID_CANNONBALL) != QUEST_STATUS_INCOMPLETE)
                continue;

            if (!player->HasAura(SPELL_DARKMOON_CANNON_LAUNCH) || !(player->IsInWater() || player->GetPositionZ() <= Cannon::LandingReadyZ))
                continue;

            float const distance = player->GetExactDist2d(me);
            uint32 credit = 0;
            uint32 resultSpell = SPELL_DARKMOON_CANNON_TARGET_MISS;
            if (distance <= Cannon::BullseyeDistance)
            {
                credit = 3;
                resultSpell = SPELL_DARKMOON_CANNON_TARGET_CENTER;
                CompleteAchievement(player, ACHIEVEMENT_BLASTENHEIMER_BULLSEYE);
            }
            else if (distance <= Cannon::NearDistance)
            {
                credit = 1;
                resultSpell = SPELL_DARKMOON_CANNON_TARGET_NEAR;
            }

            Cannon::RemoveAuras(player);
            me->CastSpell(player, resultSpell, true);

            uint32 const current = Cannon::Score(player);
            uint32 const target = AzDarkmoon::Min(current + credit, DARKMOON_CANNON_TARGET_POINTS);
            for (uint32 i = current; i < target; ++i)
                player->KilledMonsterCredit(NPC_DARKMOON_CANNON_TARGET_CREDIT);
        }
    }

    EventMap _events;
};

struct npc_darkmoon_fozlebub : public ScriptedAI
{
    npc_darkmoon_fozlebub(Creature* creature) : ScriptedAI(creature) { }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        QuestStatus const status = player->GetQuestStatus(QUEST_HUMANOID_CANNONBALL);
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Ramenez-moi au canon.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        CloseGossipMenuFor(player);

        if (action != GOSSIP_ACTION_INFO_DEF + 1)
            return true;

        if (!player->HasEnoughMoney(uint64(Cannon::ReturnCost)))
        {
            me->Whisper("Il vous faut 30 pièces d'argent pour retourner au canon.", LANG_UNIVERSAL, player);
            return true;
        }

        player->ModifyMoney(-int64(Cannon::ReturnCost));
        Cannon::RemoveAuras(player);
        player->TeleportTo(AzDarkmoon::IslandMapId, Cannon::Booth.GetPositionX(), Cannon::Booth.GetPositionY(), Cannon::Booth.GetPositionZ(), Cannon::Booth.GetOrientation());
        return true;
    }
};

// 102112 - Préparation au tir de canon
class spell_darkmoon_cannon_preparation : public AuraScript
{
    void HandleLaunch(AuraEffect const* /*aurEff*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player || player->HasAura(SPELL_DARKMOON_CANNON_LAUNCH))
            return;

        player->RemoveAurasDueToSpell(SPELL_DARKMOON_CANNON_ROOT);
        player->SetFacingTo(Cannon::Launch.GetOrientation());
        player->AddAura(SPELL_DARKMOON_CANNON_LAUNCH, player);
        player->AddAura(SPELL_DARKMOON_CANNON_MAGIC_WINGS, player);
        player->KnockbackFrom(Cannon::Source.GetPositionX(), Cannon::Source.GetPositionY(), Cannon::SpeedXY, Cannon::SpeedZ);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_darkmoon_cannon_preparation::HandleLaunch, EFFECT_2, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

// ---------------------------------------------------------------------------------------------
// Combat à mort (pavillon) : annonce, coffre, entrée/sortie
// ---------------------------------------------------------------------------------------------
struct npc_darkmoon_deathmatch_announcer : public ScriptedAI
{
    npc_darkmoon_deathmatch_announcer(Creature* creature) : ScriptedAI(creature), _chestNeedsRespawn(false) { }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!_chestNeedsRespawn || !_chest.IsEmpty())
            return;

        if (GameObject* chest = GetClosestGameObjectWithEntry(me, GO_DARKMOON_DEATHMATCH_CHEST, 100.0f))
            PrepareChest(chest);
    }

    void OnGameEvent(bool start, uint16 eventId) override
    {
        if (!start)
            return;

        if (eventId == AzDarkmoon::DeathmatchAnnounce())
            Talk(SAY_DEATHMATCH_ANNOUNCE);
        else if (eventId == AzDarkmoon::DeathmatchRun())
        {
            Talk(SAY_DEATHMATCH_RUN);
            if (GameObject* chest = ObjectAccessor::GetGameObject(*me, _chest))
                PrepareChest(chest);
            else
            {
                _chest.Clear();
                _chestNeedsRespawn = true;
            }
        }
    }

    void SetGUID(ObjectGuid const& winner, int32 /*id*/) override
    {
        if (Player* player = ObjectAccessor::GetPlayer(*me, winner))
            Talk(SAY_DEATHMATCH_END, player);

        sGameEventMgr->StopEvent(AzDarkmoon::DeathmatchRun());
    }

private:
    void PrepareChest(GameObject* chest)
    {
        _chest = chest->GetGUID();
        _chestNeedsRespawn = false;
        chest->loot.clear();
        chest->SetLootState(GO_READY);
        chest->Respawn();
    }

    ObjectGuid _chest;
    bool _chestNeedsRespawn;
};

struct go_darkmoon_treasure_chest : public GameObjectAI
{
    go_darkmoon_treasure_chest(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* /*player*/) override
    {
        // Le coffre ne s'ouvre que pendant le combat à mort
        return !sGameEventMgr->IsActiveEvent(AzDarkmoon::DeathmatchRun());
    }

    void OnLootStateChanged(uint32 state, Unit* unit) override
    {
        if (state == GO_ACTIVATED)
        {
            if (!sGameEventMgr->IsActiveEvent(AzDarkmoon::DeathmatchRun()))
            {
                _lastLooter.Clear();
                me->loot.clear();
                me->SetLootState(GO_READY);
                return;
            }

            if (Player* player = unit ? unit->ToPlayer() : nullptr)
                _lastLooter = player->GetGUID();
            else
                _lastLooter.Clear();
            return;
        }

        if (state != GO_JUST_DEACTIVATED || _lastLooter.IsEmpty())
            return;

        if (Creature* announcer = GetClosestCreatureWithEntry(me, NPC_DARKMOON_DEATHMATCH_ANNOUNCER, 100.0f))
            if (announcer->AI())
                announcer->AI()->SetGUID(_lastLooter, 0);

        _lastLooter.Clear();
    }

private:
    ObjectGuid _lastLooter;
};

// 108941 - Combat à mort : entrée / sortie du pavillon
class spell_darkmoon_deathmatch : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        caster->CastSpell(caster, caster->GetPositionY() < 6402.79f ? SPELL_ENTER_DEATHMATCH : SPELL_EXIT_DEATHMATCH, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_darkmoon_deathmatch::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_event_darkmoon_faire_jeux()
{
    new world_azimov_darkmoon_faire();
    new player_azimov_darkmoon_faire();

    RegisterCreatureAI(npc_whack_gnoll_bunny);
    RegisterCreatureAI(npc_whack_gnoll_mola);
    RegisterCreatureAI(npc_whack_gnoll_barrel);
    RegisterCreatureAI(npc_whack_gnoll_target);
    RegisterSpellScript(spell_whack_gnoll_whack);
    RegisterSpellScript(spell_whack_gnoll_override_action);
    new at_whack_a_gnoll_entrance();

    RegisterCreatureAI(npc_darkmoon_rinling);
    RegisterCreatureAI(npc_darkmoon_shooting_gallery_target);
    RegisterSpellScript(spell_darkmoon_shooting_gallery_shoot);
    RegisterSpellScript(spell_darkmoon_shooting_gallery_override_action);

    RegisterCreatureAI(npc_darkmoon_jessica_rogers);
    RegisterSpellScript(spell_darkmoon_ring_toss_throw);
    RegisterSpellScript(spell_darkmoon_ring_toss_action);

    RegisterCreatureAI(npc_darkmoon_finlay_coolshot);
    RegisterCreatureAI(npc_darkmoon_steam_tonk);
    RegisterCreatureAI(npc_darkmoon_tonk_cannon_target);
    RegisterSpellScript(spell_darkmoon_tonk_controller);

    RegisterCreatureAI(npc_darkmoon_maxima_blastenheimer);
    RegisterCreatureAI(npc_darkmoon_cannon_target);
    RegisterCreatureAI(npc_darkmoon_fozlebub);
    RegisterSpellScript(spell_darkmoon_cannon_preparation);

    RegisterCreatureAI(npc_darkmoon_deathmatch_announcer);
    RegisterGameObjectAI(go_darkmoon_treasure_chest);
    RegisterSpellScript(spell_darkmoon_deathmatch);
}
