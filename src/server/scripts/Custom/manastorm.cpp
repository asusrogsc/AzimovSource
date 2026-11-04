/*
 * Manastorm - mini-donjon à paliers inspiré de Project Ascension
 * Pour TrinityCore Cataclysm 4.3.4 (The-Cataclysm-Preservation-Project)
 *
 * Contenu :
 *  - PNJ "Portail du Manastorm" (gossip) : choix du palier, records, boutique
 *  - InstanceScript sur une instance existante (par défaut : Gouffre de Ragefeu, map 389)
 *  - Vagues de monstres + boss, mis à l'échelle selon le palier, le niveau et la taille du groupe
 *  - Récompenses : XP, or, monnaie, équipement adapté au niveau et à la classe
 *  - Records par mode (solo / duo / trio / groupe) stockés dans la base characters
 *  - Commande .manastorm (records) et .manastorm reload (admin)
 */

#include "ScriptMgr.h"
#include "CharacterDatabase.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "RBAC.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Manastorm
{
// ---------------------------------------------------------------------------
// Configuration (à adapter à ton serveur)
// ---------------------------------------------------------------------------
constexpr uint32 MAP_ID              = 389;      // Gouffre de Ragefeu. Doit correspondre au SQL (instance_template)
constexpr char const* INSTANCE_SCRIPT_NAME = "instance_manastorm";

// Position du centre de l'arène dans l'instance.
// Laisse USE_CUSTOM_ARENA à false pour utiliser le point d'entrée du donjon,
// puis remplace par les coordonnées données par .gps une fois en jeu.
constexpr bool  USE_CUSTOM_ARENA = false;
constexpr float ARENA_X = 0.0f, ARENA_Y = 0.0f, ARENA_Z = 0.0f, ARENA_O = 0.0f;

constexpr uint32 CURRENCY_ID         = 241;      // Monnaie existante dans le client 4.3.4 (241 = Sceau de champion)
constexpr uint32 MAX_TIER            = 100;
constexpr uint32 COUNTDOWN_MS        = 15 * IN_MILLISECONDS;
constexpr uint32 WAVE_PAUSE_MS       = 6 * IN_MILLISECONDS;
constexpr uint32 EXIT_DELAY_MS       = 20 * IN_MILLISECONDS;
constexpr uint32 TIME_LIMIT_SEC      = 20 * MINUTE;
constexpr uint32 PENDING_EXPIRE_SEC  = 2 * MINUTE;
constexpr float  GROUP_RANGE         = 40.0f;    // distance max du PNJ pour être emmené dans le run

// Entrées creature_template (voir manastorm_world.sql)
enum Npcs : uint32
{
    NPC_PORTAL      = 900100,
    NPC_ANCHOR      = 900101,
    NPC_WYRM        = 900110,
    NPC_WRAITH      = 900111,
    NPC_UNSTABLE    = 900112,
    NPC_GUARDIAN    = 900120,
    NPC_BOSS        = 900130,
};

enum Texts : uint32
{
    NPC_TEXT_PORTAL = 900100,
    NPC_TEXT_HELP   = 900101,
    NPC_TEXT_SHOP   = 900102,
};

enum DataIds : uint32
{
    DATA_DAMAGE_PCT = 1,    // CreatureAI::SetData : multiplicateur de dégâts x100
    DATA_MOB_DIED   = 100,  // InstanceScript::SetGuidData
    DATA_BOSS_ADDS  = 101,  // InstanceScript::SetData : nombre d'adds à invoquer
    DATA_STATE      = 102,
};

enum Mode : uint8
{
    MODE_SOLO  = 1,
    MODE_DUO   = 2,
    MODE_TRIO  = 3,
    MODE_GROUP = 4,
    MODE_MAX   = 5,
};

enum GossipActions : uint32
{
    ACTION_START_BASE = 1000,   // 1001..1100 = lancer le palier N
    ACTION_CODE       = 2000,
    ACTION_RECORDS    = 2001,
    ACTION_SHOP       = 2002,
    ACTION_HELP       = 2003,
    ACTION_BACK       = 2004,
    ACTION_BUY_BASE   = 3000,   // 3000+index = acheter l'article N
};

static char const* PREFIX = "|cff8a2be2[Manastorm]|r ";

char const* ModeName(uint8 mode)
{
    switch (mode)
    {
        case MODE_SOLO: return "Solo";
        case MODE_DUO:  return "Duo";
        case MODE_TRIO: return "Trio";
        default:        return "Groupe";
    }
}

uint8 ModeForCount(uint32 count)
{
    if (count <= 1) return MODE_SOLO;
    if (count == 2) return MODE_DUO;
    if (count == 3) return MODE_TRIO;
    return MODE_GROUP;
}

// Courbes de difficulté
float HealthMultiplier(uint32 tier, uint32 players)
{
    float tierMod = 1.0f + 0.15f * float(tier - 1);
    float groupMod = 1.0f + 0.75f * float(std::max<uint32>(players, 1) - 1);
    return tierMod * groupMod;
}

float DamageMultiplier(uint32 tier)
{
    return 1.0f + 0.10f * float(tier - 1);
}

uint32 WaveCount(uint32 tier)       { return std::min<uint32>(3 + tier / 5, 6); }

// Chaque monstre existe en 4 variantes (exp 0 à 3, entrée + 1000 * exp) pour que
// creature_classlevelstats donne des PV et dégâts cohérents à tous les niveaux.
uint32 EntryForLevel(uint32 entry, uint8 level)
{
    uint32 expansion = level <= 60 ? 0 : (level <= 70 ? 1 : (level <= 80 ? 2 : 3));
    return entry + 1000 * expansion;
}
uint32 MobsPerWave(uint32 tier, uint32 players) { return std::min<uint32>(3 + players + tier / 4, 14); }

void Say(Player* player, std::string const& text)
{
    if (player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage((std::string(PREFIX) + text).c_str());
}

void Notify(Player* player, std::string const& text)
{
    if (player && player->GetSession())
    {
        player->GetSession()->SendNotification("%s", text.c_str());
        Say(player, text);
    }
}

std::string FormatTime(uint32 seconds)
{
    return Trinity::StringFormat("%u:%02u", seconds / 60, seconds % 60);
}

// ---------------------------------------------------------------------------
// Manager : données partagées (runs en attente, records, boutique, butin)
// Les maps peuvent être mises à jour sur plusieurs threads : tout est protégé.
// ---------------------------------------------------------------------------
struct PendingRun
{
    uint32 tier = 1;
    uint8 mode = MODE_SOLO;
    time_t expires = 0;
};

struct Record
{
    uint32 bestTier = 0;
    uint32 bestTime = 0;
    uint32 runs = 0;
};

struct ShopEntry
{
    uint32 itemId = 0;
    uint32 count = 1;
    uint32 price = 0;
};

class Manager
{
public:
    static Manager* instance()
    {
        static Manager mgr;
        return &mgr;
    }

    // --- Runs en attente (clé = GUID du groupe, ou du joueur en solo)
    void SetPending(ObjectGuid key, PendingRun const& run)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _pending[key] = run;
    }

    bool TakePending(ObjectGuid key, PendingRun& out)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto itr = _pending.find(key);
        if (itr == _pending.end())
            return false;
        bool valid = itr->second.expires >= GameTime::GetGameTime();
        out = itr->second;
        _pending.erase(itr);
        return valid;
    }

    // --- Position d'origine pour le retour
    void SetOrigin(ObjectGuid guid, WorldLocation const& loc)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _origins[guid] = loc;
    }

    bool TakeOrigin(ObjectGuid guid, WorldLocation& out)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto itr = _origins.find(guid);
        if (itr == _origins.end())
            return false;
        out = itr->second;
        _origins.erase(itr);
        return true;
    }

    // --- Records
    Record GetRecord(ObjectGuid guid, uint8 mode)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return LoadRecordsLocked(guid)[std::min<uint8>(mode, MODE_GROUP)];
    }

    // Retourne true si c'est un nouveau record de palier
    bool SaveRun(ObjectGuid guid, uint8 mode, uint32 tier, uint32 seconds)
    {
        std::lock_guard<std::mutex> lock(_lock);
        Record& rec = LoadRecordsLocked(guid)[std::min<uint8>(mode, MODE_GROUP)];
        bool newBest = tier > rec.bestTier;
        ++rec.runs;
        if (newBest || (tier == rec.bestTier && (rec.bestTime == 0 || seconds < rec.bestTime)))
        {
            rec.bestTier = tier;
            rec.bestTime = seconds;
        }

        CharacterDatabase.PExecute("REPLACE INTO character_manastorm (guid, mode, best_tier, best_time, runs) VALUES (%u, %u, %u, %u, %u)",
            guid.GetCounter(), uint32(mode), rec.bestTier, rec.bestTime, rec.runs);
        return newBest;
    }

    void ForgetRecords(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _records.erase(guid.GetCounter());
    }

    // --- Boutique
    std::vector<ShopEntry> GetShop()
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (!_shopLoaded)
            LoadShopLocked();
        return _shop;
    }

    uint32 ReloadShop()
    {
        std::lock_guard<std::mutex> lock(_lock);
        LoadShopLocked();
        return uint32(_shop.size());
    }

    // --- Butin : choisit un objet équipable adapté au joueur
    uint32 PickGear(Player* player, uint32 quality)
    {
        {
            std::lock_guard<std::mutex> lock(_lock);
            if (!_gearIndexBuilt)
                BuildGearIndexLocked();
        }

        uint32 level = player->getLevel();
        uint32 minLevel = level > 6 ? level - 6 : 1;
        uint32 bestArmor = BestArmorSubclass(player);

        for (uint32 q = quality; q >= 2; --q)
        {
            std::vector<ItemTemplate const*> candidates;
            for (uint32 lvl = minLevel; lvl <= level; ++lvl)
            {
                auto itr = _gearByLevel.find(lvl);
                if (itr == _gearByLevel.end())
                    continue;

                for (ItemTemplate const* proto : itr->second)
                {
                    if (proto->GetQuality() != q)
                        continue;

                    if (proto->GetClass() == ITEM_CLASS_ARMOR && proto->GetInventoryType() != INVTYPE_CLOAK
                        && proto->GetSubClass() >= ITEM_SUBCLASS_ARMOR_CLOTH && proto->GetSubClass() <= ITEM_SUBCLASS_ARMOR_PLATE
                        && proto->GetSubClass() != bestArmor)
                        continue;

                    if (player->CanUseItem(proto) != EQUIP_ERR_OK)
                        continue;

                    candidates.push_back(proto);
                }
            }

            if (!candidates.empty())
                return Trinity::Containers::SelectRandomContainerElement(candidates)->GetId();
        }
        return 0;
    }

private:
    std::array<Record, MODE_MAX>& LoadRecordsLocked(ObjectGuid guid)
    {
        auto itr = _records.find(guid.GetCounter());
        if (itr != _records.end())
            return itr->second;

        std::array<Record, MODE_MAX>& recs = _records[guid.GetCounter()];
        if (QueryResult result = CharacterDatabase.PQuery("SELECT mode, best_tier, best_time, runs FROM character_manastorm WHERE guid = %u", guid.GetCounter()))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 mode = fields[0].GetUInt8();
                if (mode == 0 || mode >= MODE_MAX)
                    continue;
                recs[mode].bestTier = fields[1].GetUInt32();
                recs[mode].bestTime = fields[2].GetUInt32();
                recs[mode].runs = fields[3].GetUInt32();
            } while (result->NextRow());
        }
        return recs;
    }

    void LoadShopLocked()
    {
        _shop.clear();
        _shopLoaded = true;
        QueryResult result = WorldDatabase.Query("SELECT item, count, price FROM manastorm_shop ORDER BY sort, item");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            ShopEntry entry;
            entry.itemId = fields[0].GetUInt32();
            entry.count = std::max<uint32>(fields[1].GetUInt32(), 1);
            entry.price = fields[2].GetUInt32();

            if (!sObjectMgr->GetItemTemplate(entry.itemId))
            {
                TC_LOG_ERROR("scripts", "Manastorm: l'objet %u de manastorm_shop n'existe pas dans le client, ignoré.", entry.itemId);
                continue;
            }
            _shop.push_back(entry);
        } while (result->NextRow() && _shop.size() < 25);
    }

    static bool IsBadName(std::string const& name)
    {
        static char const* const bad[] = { "Test", "TEST", "test", "Deprecated", "DEPRECATED", "Monster -", "QA ", "[PH]", "zz", "OLD", "Debug", "DEBUG", "Unused", "UNUSED" };
        for (char const* b : bad)
            if (name.find(b) != std::string::npos)
                return true;
        return false;
    }

    void BuildGearIndexLocked()
    {
        _gearIndexBuilt = true;
        uint32 count = 0;
        for (auto const& pair : *sObjectMgr->GetItemTemplateStore())
        {
            ItemTemplate const& proto = pair.second;
            if (proto.GetClass() != ITEM_CLASS_WEAPON && proto.GetClass() != ITEM_CLASS_ARMOR)
                continue;
            if (proto.GetQuality() < ITEM_QUALITY_UNCOMMON || proto.GetQuality() > ITEM_QUALITY_EPIC)
                continue;
            if (proto.GetRequiredLevel() <= 0)
                continue;
            if (proto.GetBonding() == BIND_QUEST)
                continue;

            switch (proto.GetInventoryType())
            {
                case INVTYPE_NON_EQUIP: case INVTYPE_BODY: case INVTYPE_TABARD: case INVTYPE_BAG:
                case INVTYPE_AMMO: case INVTYPE_QUIVER: case INVTYPE_RELIC: case INVTYPE_THROWN:
                    continue;
                default:
                    break;
            }

            char const* name = proto.GetName(LOCALE_enUS);
            if (!name || IsBadName(name))
                continue;

            _gearByLevel[uint32(proto.GetRequiredLevel())].push_back(&proto);
            ++count;
        }
        TC_LOG_INFO("scripts", "Manastorm: index de butin construit (%u objets).", count);
    }

    static uint32 BestArmorSubclass(Player* player)
    {
        if (player->HasSkill(SKILL_PLATE_MAIL)) return ITEM_SUBCLASS_ARMOR_PLATE;
        if (player->HasSkill(SKILL_MAIL))       return ITEM_SUBCLASS_ARMOR_MAIL;
        if (player->HasSkill(SKILL_LEATHER))    return ITEM_SUBCLASS_ARMOR_LEATHER;
        return ITEM_SUBCLASS_ARMOR_CLOTH;
    }

    std::mutex _lock;
    std::unordered_map<ObjectGuid, PendingRun> _pending;
    std::unordered_map<ObjectGuid, WorldLocation> _origins;
    std::unordered_map<ObjectGuid::LowType, std::array<Record, MODE_MAX>> _records;
    std::vector<ShopEntry> _shop;
    bool _shopLoaded = false;
    std::unordered_map<uint32, std::vector<ItemTemplate const*>> _gearByLevel;
    bool _gearIndexBuilt = false;
};

#define sManastorm Manastorm::Manager::instance()

// Donne un objet, ou l'envoie par courrier si les sacs sont pleins
void GiveItem(Player* player, uint32 itemId, uint32 count)
{
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
    {
        if (Item* item = player->StoreNewItem(dest, itemId, true, GenerateItemRandomPropertyId(itemId)))
        {
            player->SendNewItem(item, count, true, false);
            return;
        }
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft draft("Butin du Manastorm", "Tes sacs étaient pleins : voici ta récompense du Manastorm.");
    if (Item* item = Item::CreateItem(itemId, count, player))
    {
        item->SaveToDB(trans);
        draft.AddItem(item);
    }
    draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_CREATURE, NPC_PORTAL, MAIL_STATIONERY_DEFAULT));
    CharacterDatabase.CommitTransaction(trans);
    Say(player, "Sacs pleins : l'objet t'a été envoyé par courrier.");
}

std::string ItemLink(uint32 itemId, LocaleConstant locale)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return "objet inconnu";

    static char const* const colors[] = { "ff9d9d9d", "ffffffff", "ff1eff00", "ff0070dd", "ffa335ee", "ffff8000", "ffe6cc80", "ffe6cc80" };
    char const* name = proto->GetName(locale);
    if (!name || !*name)
        name = proto->GetName(LOCALE_enUS);
    return Trinity::StringFormat("|c%s|Hitem:%u:0:0:0:0:0:0:0:0:0|h[%s]|h|r", colors[std::min<uint32>(proto->GetQuality(), 7)], itemId, name ? name : "?");
}

// ---------------------------------------------------------------------------
// InstanceScript : déroulement d'un run
// ---------------------------------------------------------------------------
enum RunState : uint32
{
    STATE_IDLE,
    STATE_COUNTDOWN,
    STATE_WAVE,
    STATE_WAVE_PAUSE,
    STATE_BOSS,
    STATE_COMPLETE,
    STATE_FAILED,
};

class instance_manastorm : public InstanceMapScript
{
public:
    instance_manastorm() : InstanceMapScript(INSTANCE_SCRIPT_NAME, MAP_ID) { }

    struct instance_manastorm_InstanceScript : public InstanceScript
    {
        instance_manastorm_InstanceScript(InstanceMap* map) : InstanceScript(map) { }

        // --- Les créatures d'origine du donjon sont retirées pour libérer la place
        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);
            if (creature->GetSpawnId() && creature->GetEntry() != NPC_PORTAL)
                _dbCreatures.push_back(creature->GetGUID());
        }

        void OnPlayerEnter(Player* player) override
        {
            if (_state != STATE_IDLE)
            {
                if (_state == STATE_COUNTDOWN || _state == STATE_WAVE || _state == STATE_WAVE_PAUSE || _state == STATE_BOSS)
                    Say(player, Trinity::StringFormat("Run en cours : palier %u (%s).", _tier, ModeName(_mode)));
                return;
            }

            ObjectGuid key = player->GetGroup() ? player->GetGroup()->GetGUID() : player->GetGUID();
            PendingRun run;
            if (!sManastorm->TakePending(key, run))
            {
                // Entrée sans passer par le portail : on renvoie le joueur
                Say(player, "Aucun Manastorm n'est actif ici. Passe par un Portail du Manastorm en ville.");
                _kickList.push_back(player->GetGUID());
                _kickTimer = 3 * IN_MILLISECONDS;
                return;
            }

            _tier = std::max<uint32>(1, std::min<uint32>(run.tier, MAX_TIER));
            _mode = run.mode;
            _state = STATE_COUNTDOWN;
            _timer = COUNTDOWN_MS;
            _lastAnnounce = COUNTDOWN_MS / IN_MILLISECONDS + 1;
            _wave = 0;
            Broadcast(Trinity::StringFormat("Palier %u (%s). Le Manastorm commence dans %u secondes...", _tier, ModeName(_mode), COUNTDOWN_MS / IN_MILLISECONDS), true);
        }

        void SetGuidData(uint32 type, ObjectGuid guid) override
        {
            if (type != DATA_MOB_DIED)
                return;

            _alive.erase(guid);
            if (_state == STATE_BOSS && guid == _bossGuid)
            {
                CompleteRun();
                return;
            }

            if (_state == STATE_WAVE && _alive.empty())
            {
                if (_wave >= _totalWaves)
                {
                    _state = STATE_WAVE_PAUSE;
                    _timer = WAVE_PAUSE_MS;
                    Broadcast("Le gardien du Manastorm arrive !", true);
                }
                else
                {
                    _state = STATE_WAVE_PAUSE;
                    _timer = WAVE_PAUSE_MS;
                    Broadcast(Trinity::StringFormat("Vague %u/%u terminée.", _wave, _totalWaves), true);
                }
            }
        }

        void SetData(uint32 type, uint32 value) override
        {
            if (type == DATA_BOSS_ADDS && _state == STATE_BOSS)
                for (uint32 i = 0; i < value; ++i)
                    SpawnMob(RandomTrash());
        }

        uint32 GetData(uint32 type) const override
        {
            return type == DATA_STATE ? uint32(_state) : 0;
        }

        void Update(uint32 diff) override
        {
            // Les grilles se chargent au fur et à mesure : on retire les créatures d'origine dès leur apparition
            if (!_dbCreatures.empty())
            {
                for (ObjectGuid const& guid : _dbCreatures)
                    if (Creature* c = instance->GetCreature(guid))
                        c->DespawnOrUnsummon(0, Seconds(7 * DAY));
                _dbCreatures.clear();
            }

            if (!_kickList.empty())
            {
                if (_kickTimer <= diff)
                {
                    for (ObjectGuid const& guid : _kickList)
                        if (Player* p = ObjectAccessor::GetPlayer(instance, guid))
                            SendHome(p);
                    _kickList.clear();
                }
                else
                    _kickTimer -= diff;
            }

            switch (_state)
            {
                case STATE_IDLE:
                    return;
                case STATE_COUNTDOWN:
                {
                    uint32 secondsLeft = _timer / IN_MILLISECONDS;
                    if ((secondsLeft == 10 || secondsLeft <= 3) && secondsLeft < _lastAnnounce && secondsLeft > 0)
                    {
                        _lastAnnounce = secondsLeft;
                        Broadcast(Trinity::StringFormat("%u...", secondsLeft), true);
                    }
                    if (_timer <= diff)
                        StartRun();
                    else
                        _timer -= diff;
                    return;
                }
                case STATE_WAVE:
                case STATE_BOSS:
                    CheckFailure();
                    return;
                case STATE_WAVE_PAUSE:
                    if (CheckFailure())
                        return;
                    if (_timer <= diff)
                    {
                        if (_wave >= _totalWaves)
                            StartBoss();
                        else
                            StartWave();
                    }
                    else
                        _timer -= diff;
                    return;
                case STATE_COMPLETE:
                case STATE_FAILED:
                    if (_timer <= diff)
                        EndRun();
                    else
                        _timer -= diff;
                    return;
            }
        }

    private:
        void Broadcast(std::string const& text, bool notify = false)
        {
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* p = ref.GetSource())
                    notify ? Notify(p, text) : Say(p, text);
        }

        uint32 CountPlayers(bool aliveOnly) const
        {
            uint32 count = 0;
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* p = ref.GetSource())
                    if (!p->IsGameMaster() && (!aliveOnly || p->IsAlive()))
                        ++count;
            return count;
        }

        bool CheckFailure()
        {
            if (CountPlayers(true) == 0)
            {
                FailRun("Tous les participants sont tombés.");
                return true;
            }
            if (GameTime::GetGameTime() - _startTime > time_t(TIME_LIMIT_SEC))
            {
                FailRun("Le temps est écoulé.");
                return true;
            }
            return false;
        }

        void StartRun()
        {
            _players = std::max<uint32>(CountPlayers(false), 1);

            uint32 totalLevel = 0, n = 0;
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* p = ref.GetSource())
                    if (!p->IsGameMaster())
                    {
                        totalLevel += p->getLevel();
                        ++n;
                    }
            _mobLevel = uint8(n ? (totalLevel + n / 2) / n : 1);

            if (!SpawnAnchor())
            {
                FailRun("Impossible d'ouvrir l'arène (point d'ancrage introuvable).");
                return;
            }

            _totalWaves = WaveCount(_tier);
            _startTime = GameTime::GetGameTime();
            Broadcast(Trinity::StringFormat("C'est parti ! %u vagues puis le gardien. Limite : %s.", _totalWaves, FormatTime(TIME_LIMIT_SEC).c_str()), true);
            StartWave();
        }

        bool SpawnAnchor()
        {
            Position pos;
            if (USE_CUSTOM_ARENA)
                pos.Relocate(ARENA_X, ARENA_Y, ARENA_Z, ARENA_O);
            else if (AreaTriggerStruct const* entrance = sObjectMgr->GetMapEntranceTrigger(MAP_ID))
                pos.Relocate(entrance->target_X, entrance->target_Y, entrance->target_Z, entrance->target_Orientation);
            else
            {
                Player* first = nullptr;
                for (MapReference const& ref : instance->GetPlayers())
                    if ((first = ref.GetSource()))
                        break;
                if (!first)
                    return false;
                pos = first->GetPosition();
            }

            SummonCreatureExtraArgs args;
            Creature* anchor = instance->SummonCreature(NPC_ANCHOR, pos, args);
            if (!anchor)
            {
                TC_LOG_ERROR("scripts", "Manastorm: impossible d'invoquer l'ancre %u (SQL appliqué ?).", NPC_ANCHOR);
                return false;
            }
            _anchorGuid = anchor->GetGUID();
            _summons.push_back(anchor->GetGUID());
            return true;
        }

        Creature* GetAnchor() const { return instance->GetCreature(_anchorGuid); }

        uint32 RandomTrash() const
        {
            static uint32 const trash[] = { NPC_WYRM, NPC_WYRM, NPC_WRAITH, NPC_WRAITH, NPC_UNSTABLE };
            return trash[urand(0, 4)];
        }

        Position RandomSpawnPosition(Creature* anchor) const
        {
            Position pos = anchor->GetPosition();
            float angle = frand(0.0f, float(2 * M_PI));
            anchor->MovePositionToFirstCollision(pos, frand(8.0f, 16.0f), angle - anchor->GetOrientation());
            return pos;
        }

        Creature* SpawnMob(uint32 entry)
        {
            Creature* anchor = GetAnchor();
            if (!anchor)
                return nullptr;

            Position pos = RandomSpawnPosition(anchor);
            entry = EntryForLevel(entry, _mobLevel);
            TempSummon* mob = anchor->SummonCreature(entry, pos, TEMPSUMMON_MANUAL_DESPAWN);
            if (!mob)
            {
                TC_LOG_ERROR("scripts", "Manastorm: impossible d'invoquer la créature %u (SQL appliqué ?).", entry);
                return nullptr;
            }

            mob->SetLevel(_mobLevel);
            mob->UpdateLevelDependantStats();
            float hp = HealthMultiplier(_tier, _players);
            if (hp > 1.0f)
                mob->ApplyStatPctModifier(UNIT_MOD_HEALTH, TOTAL_PCT, (hp - 1.0f) * 100.0f);
            mob->UpdateMaxHealth();
            mob->SetFullHealth();

            if (mob->AI())
            {
                mob->AI()->SetData(DATA_DAMAGE_PCT, uint32(DamageMultiplier(_tier) * 100.0f));
                mob->AI()->DoZoneInCombat();
            }

            _alive.insert(mob->GetGUID());
            _summons.push_back(mob->GetGUID());
            return mob;
        }

        void StartWave()
        {
            ++_wave;
            _state = STATE_WAVE;
            uint32 count = MobsPerWave(_tier, _players);
            for (uint32 i = 0; i < count; ++i)
                SpawnMob(RandomTrash());

            // Un élite par vague à partir de la 2e, deux à haut palier
            if (_wave >= 2)
                SpawnMob(NPC_GUARDIAN);
            if (_wave >= 2 && _tier >= 15)
                SpawnMob(NPC_GUARDIAN);

            Broadcast(Trinity::StringFormat("Vague %u/%u !", _wave, _totalWaves), true);
            if (_alive.empty())
                FailRun("Aucune créature n'a pu apparaître (SQL appliqué ?).");
        }

        void StartBoss()
        {
            _state = STATE_BOSS;
            if (Creature* boss = SpawnMob(NPC_BOSS))
            {
                _bossGuid = boss->GetGUID();
                boss->Yell("Encore des curieux attirés par la tempête ? Elle va vous dévorer !", LANG_UNIVERSAL);
            }
            else
                FailRun("Le gardien n'a pas pu apparaître (SQL appliqué ?).");
        }

        void CompleteRun()
        {
            _state = STATE_COMPLETE;
            _timer = EXIT_DELAY_MS;
            uint32 seconds = uint32(GameTime::GetGameTime() - _startTime);
            DespawnSummons(false);

            Broadcast(Trinity::StringFormat("Palier %u terminé en %s !", _tier, FormatTime(seconds).c_str()), true);

            for (MapReference const& ref : instance->GetPlayers())
                if (Player* p = ref.GetSource())
                    if (!p->IsGameMaster())
                        GiveRewards(p, seconds);

            Broadcast(Trinity::StringFormat("Retour en ville dans %u secondes.", EXIT_DELAY_MS / IN_MILLISECONDS));
        }

        void GiveRewards(Player* player, uint32 seconds)
        {
            uint32 level = player->getLevel();

            // XP : pourcentage du niveau en cours
            if (level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                float pct = std::min(0.05f + 0.01f * float(_tier), 0.35f);
                uint32 xp = uint32(float(sObjectMgr->GetXPForLevel(level)) * pct);
                if (xp)
                    player->GiveXP(xp, nullptr);
            }

            // Or
            int64 copper = int64(100) * level * (2 + _tier);
            player->ModifyMoney(copper);

            // Monnaie
            int32 currency = int32(2 + _tier / 2 + (_mode == MODE_SOLO ? 1 : 0));
            player->ModifyCurrency(CURRENCY_ID, currency);

            // Équipement
            uint32 quality = _tier >= 15 ? ITEM_QUALITY_EPIC : (_tier >= 5 ? ITEM_QUALITY_RARE : ITEM_QUALITY_UNCOMMON);
            uint32 pieces = 1 + (_tier >= 10 ? 1 : 0) + (roll_chance_i(int32(std::min<uint32>(_tier * 2, 50))) ? 1 : 0);
            std::string loot;
            for (uint32 i = 0; i < pieces; ++i)
            {
                uint32 q = quality;
                if (q == ITEM_QUALITY_RARE && roll_chance_i(int32(std::min<uint32>(_tier, 25))))
                    q = ITEM_QUALITY_EPIC;
                if (uint32 itemId = sManastorm->PickGear(player, q))
                {
                    GiveItem(player, itemId, 1);
                    loot += " " + ItemLink(itemId, player->GetSession()->GetSessionDbLocaleIndex());
                }
            }

            Say(player, Trinity::StringFormat("Récompenses : %u po %u pa, %d unités de monnaie.%s%s",
                uint32(copper / 10000), uint32((copper / 100) % 100), currency, loot.empty() ? "" : " Butin :", loot.c_str()));

            if (sManastorm->SaveRun(player->GetGUID(), _mode, _tier, seconds))
                Say(player, Trinity::StringFormat("Nouveau record %s : palier %u ! Le palier %u est débloqué.", ModeName(_mode), _tier, std::min(_tier + 1, MAX_TIER)));
        }

        void FailRun(std::string const& reason)
        {
            if (_state == STATE_COMPLETE || _state == STATE_FAILED)
                return;
            _state = STATE_FAILED;
            _timer = 10 * IN_MILLISECONDS;
            DespawnSummons(true);
            Broadcast("Échec du Manastorm : " + reason, true);
            Broadcast("Retour en ville dans 10 secondes.");
        }

        void EndRun()
        {
            std::vector<Player*> players;
            for (MapReference const& ref : instance->GetPlayers())
                if (Player* p = ref.GetSource())
                    players.push_back(p);

            for (Player* p : players)
                SendHome(p);

            DespawnSummons(true);
            _state = STATE_IDLE;
            _wave = 0;
            _alive.clear();
            _bossGuid.Clear();
            _anchorGuid.Clear();
        }

        void SendHome(Player* player)
        {
            if (!player->IsAlive())
            {
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }

            WorldLocation origin;
            if (sManastorm->TakeOrigin(player->GetGUID(), origin) && origin.GetMapId() != MAP_ID)
                player->TeleportTo(origin);
            else
                player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
        }

        void DespawnSummons(bool includeAnchor)
        {
            GuidVector keep;
            for (ObjectGuid const& guid : _summons)
            {
                if (!includeAnchor && guid == _anchorGuid)
                {
                    keep.push_back(guid);
                    continue;
                }
                if (Creature* c = instance->GetCreature(guid))
                    c->DespawnOrUnsummon();
            }
            _summons = keep;
            _alive.clear();
        }

        RunState _state = STATE_IDLE;
        uint32 _timer = 0;
        uint32 _lastAnnounce = 0;
        uint32 _tier = 1;
        uint8 _mode = MODE_SOLO;
        uint32 _players = 1;
        uint8 _mobLevel = 1;
        uint32 _wave = 0;
        uint32 _totalWaves = 0;
        time_t _startTime = 0;
        ObjectGuid _anchorGuid;
        ObjectGuid _bossGuid;
        GuidSet _alive;
        GuidVector _summons;
        GuidVector _dbCreatures;
        GuidVector _kickList;
        uint32 _kickTimer = 0;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_manastorm_InstanceScript(map);
    }
};

// ---------------------------------------------------------------------------
// IA commune des créatures du Manastorm
// ---------------------------------------------------------------------------
struct npc_manastorm_mobAI : public ScriptedAI
{
    npc_manastorm_mobAI(Creature* creature) : ScriptedAI(creature) { }

    void SetData(uint32 id, uint32 value) override
    {
        if (id == DATA_DAMAGE_PCT)
            _damagePct = std::max<uint32>(value, 1);
    }

    void DamageDealt(Unit* /*victim*/, uint32& damage, DamageEffectType /*type*/) override
    {
        damage = uint32(uint64(damage) * _damagePct / 100);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetGuidData(DATA_MOB_DIED, me->GetGUID());
        me->DespawnOrUnsummon(5 * IN_MILLISECONDS);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        // Pas de retour au point d'apparition : on cherche une nouvelle cible
        DoZoneInCombat();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;
        UpdateSpecial(diff);
        DoMeleeAttackIfReady();
    }

    virtual void UpdateSpecial(uint32 /*diff*/) { }

    // Dégâts de zone basés sur les PV max des joueurs (mécaniques à esquiver)
    void PulseAoe(float radius, float pctOfMaxHealth, SpellSchoolMask school)
    {
        std::vector<Player*> targets;
        for (MapReference const& ref : me->GetMap()->GetPlayers())
            if (Player* p = ref.GetSource())
                if (p->IsAlive() && !p->IsGameMaster() && me->IsWithinDistInMap(p, radius))
                    targets.push_back(p);

        for (Player* p : targets)
        {
            uint32 dmg = uint32(float(p->GetMaxHealth()) * pctOfMaxHealth);
            Unit::DealDamage(me, p, dmg, 0, SPELL_DIRECT_DAMAGE, school, nullptr, false);
        }
    }

protected:
    uint32 _damagePct = 100;
};

// Anomalie instable : explose quand elle passe sous 30 % de vie
struct npc_manastorm_unstableAI : public npc_manastorm_mobAI
{
    npc_manastorm_unstableAI(Creature* creature) : npc_manastorm_mobAI(creature) { }

    void UpdateSpecial(uint32 diff) override
    {
        if (!_armed && HealthBelowPct(30))
        {
            _armed = true;
            _timer = 3 * IN_MILLISECONDS;
            me->SetControlled(true, UNIT_STATE_ROOT);
            me->TextEmote("se met à vibrer violemment ! Éloignez-vous !", nullptr, true);
        }

        if (_armed)
        {
            if (_timer <= diff)
            {
                _armed = false;
                _timer = 0;
                PulseAoe(8.0f, 0.20f, SPELL_SCHOOL_MASK_ARCANE);
                me->KillSelf();
            }
            else
                _timer -= diff;
        }
    }

private:
    bool _armed = false;
    uint32 _timer = 0;
};

// Gardien du Manastorm (boss)
struct npc_manastorm_bossAI : public npc_manastorm_mobAI
{
    npc_manastorm_bossAI(Creature* creature) : npc_manastorm_mobAI(creature) { }

    void UpdateSpecial(uint32 diff) override
    {
        // Invocations à 66 % et 33 %
        if (_summonPhase == 0 && HealthBelowPct(66)) { _summonPhase = 1; CallAdds(); }
        else if (_summonPhase == 1 && HealthBelowPct(33)) { _summonPhase = 2; CallAdds(); }

        // Enrager à 20 %
        if (!_enraged && HealthBelowPct(20))
        {
            _enraged = true;
            _damagePct = _damagePct * 130 / 100;
            me->Yell("La tempête ne peut être contenue !", LANG_UNIVERSAL);
            me->TextEmote("devient enragé !", nullptr, true);
        }

        // Nova arcanique : 3 s d'incantation, à fuir
        if (_novaCasting)
        {
            if (_novaTimer <= diff)
            {
                _novaCasting = false;
                PulseAoe(12.0f, 0.35f, SPELL_SCHOOL_MASK_ARCANE);
                me->SetControlled(false, UNIT_STATE_ROOT);
                _novaTimer = 18 * IN_MILLISECONDS;
            }
            else
                _novaTimer -= diff;
            return;
        }

        if (_novaTimer <= diff)
        {
            _novaCasting = true;
            _novaTimer = 3 * IN_MILLISECONDS;
            me->SetControlled(true, UNIT_STATE_ROOT);
            me->TextEmote("concentre une Nova arcanique ! Éloignez-vous à plus de 12 mètres !", nullptr, true);
        }
        else
            _novaTimer -= diff;
    }

    void JustDied(Unit* killer) override
    {
        me->Yell("Impossible... la tempête... se dissipe...", LANG_UNIVERSAL);
        npc_manastorm_mobAI::JustDied(killer);
    }

private:
    void CallAdds()
    {
        me->Yell("À moi, enfants de la tempête !", LANG_UNIVERSAL);
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(DATA_BOSS_ADDS, 2 + std::min<uint32>(_damagePct / 100, 4));
    }

    uint32 _novaTimer = 10 * IN_MILLISECONDS;
    bool _novaCasting = false;
    uint8 _summonPhase = 0;
    bool _enraged = false;
};

// ---------------------------------------------------------------------------
// PNJ : Portail du Manastorm
// ---------------------------------------------------------------------------
struct npc_manastorm_portalAI : public ScriptedAI
{
    npc_manastorm_portalAI(Creature* creature) : ScriptedAI(creature) { }

    // Joueurs qui partiront avec le chef
    std::vector<Player*> GetParty(Player* player) const
    {
        std::vector<Player*> party;
        Group* group = player->GetGroup();
        if (!group)
        {
            party.push_back(player);
            return party;
        }

        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member || !member->IsInWorld() || !member->IsAlive())
                continue;
            if (member != player && (member->GetMap() != me->GetMap() || !member->IsWithinDistInMap(me, GROUP_RANGE)))
                continue;
            party.push_back(member);
        }
        return party;
    }

    bool GossipHello(Player* player) override
    {
        ShowMain(player);
        return true;
    }

    void ShowMain(Player* player)
    {
        ClearGossipMenuFor(player);
        std::vector<Player*> party = GetParty(player);
        uint8 mode = ModeForCount(uint32(party.size()));
        Record rec = sManastorm->GetRecord(player->GetGUID(), mode);
        uint32 maxTier = std::min(rec.bestTier + 1, MAX_TIER);

        bool canStart = !player->GetGroup() || player->GetGroup()->IsLeader(player->GetGUID());
        if (canStart)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Trinity::StringFormat("Entrer dans le Manastorm : palier %u (%s, %u joueur%s)", maxTier, ModeName(mode), uint32(party.size()), party.size() > 1 ? "s" : ""), GOSSIP_SENDER_MAIN, ACTION_START_BASE + maxTier);
            if (maxTier > 2)
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Trinity::StringFormat("Entrer au palier %u", maxTier - 1), GOSSIP_SENDER_MAIN, ACTION_START_BASE + maxTier - 1);
            if (maxTier > 1)
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Entrer au palier 1", GOSSIP_SENDER_MAIN, ACTION_START_BASE + 1);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Trinity::StringFormat("Choisir un palier (1 à %u)...", maxTier), GOSSIP_SENDER_MAIN, ACTION_CODE, "Entre le numéro du palier :", 0, true);
        }
        else
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Seul le chef du groupe peut ouvrir le Manastorm.", GOSSIP_SENDER_MAIN, ACTION_BACK);

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "Mes records", GOSSIP_SENDER_MAIN, ACTION_RECORDS);
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, Trinity::StringFormat("Boutique du Manastorm (%u unités)", player->GetCurrency(CURRENCY_ID, false)), GOSSIP_SENDER_MAIN, ACTION_SHOP);
        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Comment ça marche ?", GOSSIP_SENDER_MAIN, ACTION_HELP);
        SendGossipMenuFor(player, NPC_TEXT_PORTAL, me->GetGUID());
    }

    void ShowShop(Player* player)
    {
        ClearGossipMenuFor(player);
        std::vector<ShopEntry> shop = sManastorm->GetShop();
        LocaleConstant locale = player->GetSession()->GetSessionDbLocaleIndex();
        for (uint32 i = 0; i < shop.size(); ++i)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(shop[i].itemId);
            char const* name = proto ? proto->GetName(locale) : nullptr;
            if (!name || !*name)
                name = proto ? proto->GetName(LOCALE_enUS) : "?";
            std::string label = shop[i].count > 1
                ? Trinity::StringFormat("%s x%u - %u unités", name, shop[i].count, shop[i].price)
                : Trinity::StringFormat("%s - %u unités", name, shop[i].price);
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, label, GOSSIP_SENDER_MAIN, ACTION_BUY_BASE + i,
                Trinity::StringFormat("Acheter %s pour %u unités ?", name, shop[i].price), 0, false);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Retour", GOSSIP_SENDER_MAIN, ACTION_BACK);
        SendGossipMenuFor(player, NPC_TEXT_SHOP, me->GetGUID());
    }

    void ShowRecords(Player* player)
    {
        Say(player, "Tes records :");
        for (uint8 mode = MODE_SOLO; mode <= MODE_GROUP; ++mode)
        {
            Record rec = sManastorm->GetRecord(player->GetGUID(), mode);
            if (rec.bestTier)
                Say(player, Trinity::StringFormat("  %s : palier %u en %s (%u run%s)", ModeName(mode), rec.bestTier, FormatTime(rec.bestTime).c_str(), rec.runs, rec.runs > 1 ? "s" : ""));
            else
                Say(player, Trinity::StringFormat("  %s : aucun palier terminé", ModeName(mode)));
        }
    }

    void Buy(Player* player, uint32 index)
    {
        std::vector<ShopEntry> shop = sManastorm->GetShop();
        if (index >= shop.size())
            return;

        ShopEntry const& entry = shop[index];
        if (player->GetCurrency(CURRENCY_ID, false) < entry.price)
        {
            Say(player, "Tu n'as pas assez de monnaie du Manastorm.");
            return;
        }

        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry.itemId, entry.count) != EQUIP_ERR_OK)
        {
            Say(player, "Tes sacs sont pleins.");
            return;
        }

        player->ModifyCurrency(CURRENCY_ID, -int32(entry.price));
        GiveItem(player, entry.itemId, entry.count);
    }

    void StartRun(Player* player, uint32 tier)
    {
        if (!player->GetGroup() || player->GetGroup()->IsLeader(player->GetGUID()))
        {
            if (player->IsInCombat())
            {
                Say(player, "Impossible d'entrer en combat.");
                return;
            }

            std::vector<Player*> party = GetParty(player);
            uint8 mode = ModeForCount(uint32(party.size()));
            uint32 maxTier = std::min(sManastorm->GetRecord(player->GetGUID(), mode).bestTier + 1, MAX_TIER);
            if (tier < 1 || tier > maxTier)
            {
                Say(player, Trinity::StringFormat("Palier invalide. Tu peux choisir de 1 à %u en %s.", maxTier, ModeName(mode)));
                return;
            }

            AreaTriggerStruct const* entrance = sObjectMgr->GetMapEntranceTrigger(MAP_ID);
            if (!entrance)
            {
                Say(player, "Erreur : point d'entrée de l'instance introuvable (areatrigger_teleport).");
                return;
            }

            PendingRun run;
            run.tier = tier;
            run.mode = mode;
            run.expires = GameTime::GetGameTime() + PENDING_EXPIRE_SEC;
            ObjectGuid key = player->GetGroup() ? player->GetGroup()->GetGUID() : player->GetGUID();
            sManastorm->SetPending(key, run);

            for (Player* member : party)
            {
                if (member->IsInCombat())
                {
                    Say(member, "Tu es en combat : tu ne rejoins pas ce Manastorm.");
                    continue;
                }
                sManastorm->SetOrigin(member->GetGUID(), WorldLocation(member->GetMapId(), member->GetPosition()));
                Say(member, Trinity::StringFormat("Ouverture du Manastorm, palier %u (%s)...", tier, ModeName(mode)));
                member->TeleportTo(entrance->target_mapId, entrance->target_X, entrance->target_Y, entrance->target_Z, entrance->target_Orientation);
            }
        }
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 action = GetGossipActionFor(player, gossipListId);

        if (action > ACTION_START_BASE && action <= ACTION_START_BASE + MAX_TIER)
        {
            CloseGossipMenuFor(player);
            StartRun(player, action - ACTION_START_BASE);
            return true;
        }

        if (action >= ACTION_BUY_BASE)
        {
            Buy(player, action - ACTION_BUY_BASE);
            ShowShop(player);
            return true;
        }

        switch (action)
        {
            case ACTION_RECORDS:
                CloseGossipMenuFor(player);
                ShowRecords(player);
                break;
            case ACTION_SHOP:
                ShowShop(player);
                break;
            case ACTION_HELP:
                ClearGossipMenuFor(player);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Retour", GOSSIP_SENDER_MAIN, ACTION_BACK);
                SendGossipMenuFor(player, NPC_TEXT_HELP, me->GetGUID());
                break;
            default:
                ShowMain(player);
                break;
        }
        return true;
    }

    bool GossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* code) override
    {
        uint32 action = GetGossipActionFor(player, gossipListId);
        CloseGossipMenuFor(player);
        if (action != ACTION_CODE || !code)
            return true;

        uint32 tier = uint32(atoi(code));
        StartRun(player, tier);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Commandes : .manastorm  /  .manastorm reload
// ---------------------------------------------------------------------------
class manastorm_commandscript : public CommandScript
{
public:
    manastorm_commandscript() : CommandScript("manastorm_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> subTable =
        {
            { "reload",  rbac::RBAC_PERM_COMMAND_RELOAD,      true,  &HandleReload,  "" },
            { "",        rbac::RBAC_PERM_COMMAND_SERVER_INFO, false, &HandleRecords, "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "manastorm", rbac::RBAC_PERM_COMMAND_SERVER_INFO, true, nullptr, "", subTable },
        };
        return commandTable;
    }

    static bool HandleRecords(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;

        handler->SendSysMessage("|cff8a2be2[Manastorm]|r Tes records :");
        for (uint8 mode = MODE_SOLO; mode <= MODE_GROUP; ++mode)
        {
            Record rec = sManastorm->GetRecord(player->GetGUID(), mode);
            handler->PSendSysMessage("  %s : palier %u, meilleur temps %s, %u run(s)", ModeName(mode), rec.bestTier, FormatTime(rec.bestTime).c_str(), rec.runs);
        }
        return true;
    }

    static bool HandleReload(ChatHandler* handler, char const* /*args*/)
    {
        uint32 count = sManastorm->ReloadShop();
        handler->PSendSysMessage("Manastorm : boutique rechargée (%u articles).", count);
        return true;
    }
};

// Libère le cache des records à la déconnexion
class manastorm_playerscript : public PlayerScript
{
public:
    manastorm_playerscript() : PlayerScript("manastorm_playerscript") { }

    void OnLogout(Player* player) override
    {
        sManastorm->ForgetRecords(player->GetGUID());
    }
};

} // namespace Manastorm

void AddSC_manastorm()
{
    using namespace Manastorm;
    new instance_manastorm();
    RegisterCreatureAI(npc_manastorm_portalAI);
    RegisterCreatureAI(npc_manastorm_mobAI);
    RegisterCreatureAI(npc_manastorm_unstableAI);
    RegisterCreatureAI(npc_manastorm_bossAI);
    new manastorm_commandscript();
    new manastorm_playerscript();
}
