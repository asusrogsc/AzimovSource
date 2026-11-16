/*
 * azimov_quest_fix.cpp - Filet de sécurité pour les quêtes cassées
 *
 * Certaines quêtes (escortes, défenses, scènes, crédits donnés par un PNJ invisible...)
 * ne peuvent jamais se valider sur ce serveur : le script ou le sort qui donne le crédit manque.
 * Ce script les rend faisables, en attendant (ou à la place) d'un script blizzlike.
 *
 * Tout se règle dans la table WORLD `azimov_quest_fix` (lue au démarrage du serveur) :
 *   mode 0 : fin de quête en arrivant près du PNJ qui rend la quête (radius), au moins `delay` s après l'avoir prise
 *   mode 1 : fin en arrivant près du PNJ `npc` (radius), au moins `delay` s après l'avoir prise
 *   mode 2 : fin `delay` s après avoir pris la quête (en vie, dans la zone si `zone` est rempli)
 *   mode 3 : chaque monstre tué dans la zone `zone` (0 = zone de la quête) fait progresser
 *   mode 4 : chaque PNJ `npc` tué fait progresser
 *   mode 5 : objectif « aller à un endroit » rempli en arrivant à moins de `radius` m du point (map, x, y)
 *            (comme une zone de déclenchement : les autres objectifs de la quête restent à faire ;
 *            avec `credit` / `item` : donne ce crédit / cet objet à cet endroit précis)
 * Ce qui est donné quand la condition est remplie :
 *   credit > 0 : crédit du PNJ `credit` (credit < 0 : objet du monde -credit)
 *   item > 0   : l'objet de quête `item` (mode 3/4 : 1 par mort, avec `chance` %)
 *   les deux à 0 : la quête entière est validée
 * Les membres du groupe à moins de 60 m profitent aussi des morts (modes 3 et 4).
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "World.h"
#include "WorldSession.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    enum AzQuestFixMode : uint8
    {
        AZQF_NEAR_ENDER = 0,
        AZQF_NEAR_NPC   = 1,
        AZQF_DELAY      = 2,
        AZQF_ZONE_KILL  = 3,
        AZQF_NPC_KILL   = 4,
        AZQF_POINT      = 5
    };

    struct AzQuestFix
    {
        uint32 quest = 0;
        uint8 mode = 0;
        int32 npc = 0;
        float radius = 20.0f;
        uint32 delay = 30;
        uint32 zone = 0;
        int32 credit = 0;
        uint32 item = 0;
        uint8 chance = 100;
        uint32 map = 0;
        float x = 0.f, y = 0.f;
    };

    std::vector<AzQuestFix> s_fixes;                       // toutes les lignes
    std::unordered_map<uint32, std::vector<size_t>> s_byQuest;
    std::mutex s_lock;                                      // protège s_accept
    std::unordered_map<ObjectGuid::LowType, std::unordered_map<uint32, time_t>> s_accept;

    uint32 ZoneOf(AzQuestFix const& f)
    {
        if (f.zone)
            return f.zone;
        if (Quest const* q = sObjectMgr->GetQuestTemplate(f.quest))
            if (q->GetZoneOrSort() > 0)
                return uint32(q->GetZoneOrSort());
        return 0;
    }

    void GiveItem(Player* player, uint32 itemId, uint32 count)
    {
        if (!count)
            return;
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
            if (Item* item = player->StoreNewItem(dest, itemId, true, GenerateItemRandomPropertyId(itemId)))
                player->SendNewItem(item, count, true, false);
    }

    uint32 RequiredItemCount(Quest const* q, uint32 itemId)
    {
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (q->RequiredItemId[i] == itemId)
                return q->RequiredItemCount[i];
        return 0;
    }

    uint32 RequiredCreditCount(Quest const* q, int32 credit)
    {
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (q->RequiredNpcOrGo[i] == credit)
                return q->RequiredNpcOrGoCount[i];
        return 0;
    }

    void GiveOneCredit(Player* player, int32 credit)
    {
        if (credit > 0)
            player->KilledMonsterCredit(uint32(credit));
        else if (credit < 0)
            player->KillCreditGO(uint32(-credit));
    }

    // Même logique que la commande MJ .quest complete
    void CompleteWholeQuest(Player* player, Quest const* q)
    {
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 id = q->RequiredItemId[i];
            uint32 count = q->RequiredItemCount[i];
            if (!id || !count)
                continue;
            uint32 have = player->GetItemCount(id, true);
            if (have < count)
                GiveItem(player, id, count - have);
        }

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 target = q->RequiredNpcOrGo[i];
            uint32 need = q->RequiredNpcOrGoCount[i];
            if (!target || !need)
                continue;
            uint16 have = player->GetReqKillOrCastCurrentCount(q->GetQuestId(), target);
            for (uint32 n = have; n < need; ++n)
            {
                if (target > 0)
                {
                    if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(uint32(target)))
                        player->KilledMonster(ct, ObjectGuid::Empty);
                }
                else
                    player->KillCreditGO(uint32(-target));
            }
        }

        if (player->GetQuestStatus(q->GetQuestId()) == QUEST_STATUS_INCOMPLETE)
            player->CompleteQuest(q->GetQuestId());
    }

    // Condition des modes 0/1/2 remplie : on donne tout ce qui manque pour cette ligne
    void ApplyFull(Player* player, AzQuestFix const& f)
    {
        Quest const* q = sObjectMgr->GetQuestTemplate(f.quest);
        if (!q || player->GetQuestStatus(f.quest) != QUEST_STATUS_INCOMPLETE)
            return;

        if (f.credit)
        {
            uint32 need = RequiredCreditCount(q, f.credit);
            uint16 have = player->GetReqKillOrCastCurrentCount(f.quest, f.credit);
            for (uint32 n = have; n < need; ++n)
                GiveOneCredit(player, f.credit);
        }
        if (f.item)
        {
            uint32 need = RequiredItemCount(q, f.item);
            uint32 have = player->GetItemCount(f.item, true);
            if (have < need)
                GiveItem(player, f.item, need - have);
        }
        if (!f.credit && !f.item)
            CompleteWholeQuest(player, q);
    }

    // Une mort (modes 3/4) : une progression
    void ApplyOne(Player* player, AzQuestFix const& f)
    {
        Quest const* q = sObjectMgr->GetQuestTemplate(f.quest);
        if (!q || player->GetQuestStatus(f.quest) != QUEST_STATUS_INCOMPLETE)
            return;

        if (f.credit)
        {
            if (player->GetReqKillOrCastCurrentCount(f.quest, f.credit) < RequiredCreditCount(q, f.credit))
                GiveOneCredit(player, f.credit);
        }
        if (f.item && roll_chance_i(f.chance))
        {
            if (player->GetItemCount(f.item, true) < RequiredItemCount(q, f.item))
                GiveItem(player, f.item, 1);
        }
        if (!f.credit && !f.item)
            CompleteWholeQuest(player, q);
    }

    time_t AcceptTime(Player* player, uint32 quest)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        auto& m = s_accept[player->GetGUID().GetCounter()];
        auto it = m.find(quest);
        if (it == m.end())
            it = m.emplace(quest, GameTime::GetGameTime()).first;   // après une reconnexion : on repart de maintenant
        return it->second;
    }

    bool NearEntry(Player* player, uint32 entry, float radius)
    {
        return entry && player->FindNearestCreature(entry, radius) != nullptr;
    }

    bool NearEnder(Player* player, uint32 quest, float radius)
    {
        for (uint32 entry : sObjectMgr->GetCreatureQuestInvolvedRelationsReverse(quest))
            if (NearEntry(player, entry, radius))
                return true;
        return false;
    }

    void CheckPlayer(Player* player)
    {
        if (!player->IsInWorld() || !player->IsAlive() || player->IsBeingTeleported())
            return;

        time_t now = GameTime::GetGameTime();
        for (AzQuestFix const& f : s_fixes)
        {
            if (f.mode == AZQF_POINT)
            {
                if (player->GetMapId() == f.map && player->GetExactDist2d(f.x, f.y) < f.radius
                    && player->GetQuestStatus(f.quest) == QUEST_STATUS_INCOMPLETE)
                {
                    if (f.credit || f.item)
                        ApplyFull(player, f);       // crédit / objet de cet endroit précis
                    else
                        player->AreaExploredOrEventHappens(f.quest);
                }
                continue;
            }
            if (f.mode > AZQF_DELAY)
                continue;
            if (player->GetQuestStatus(f.quest) != QUEST_STATUS_INCOMPLETE)
                continue;
            if (now - AcceptTime(player, f.quest) < time_t(f.delay))
                continue;

            bool ok = false;
            switch (f.mode)
            {
                case AZQF_NEAR_ENDER: ok = NearEnder(player, f.quest, f.radius); break;
                case AZQF_NEAR_NPC:   ok = f.npc > 0 && NearEntry(player, uint32(f.npc), f.radius); break;
                case AZQF_DELAY:      ok = !f.zone || player->GetZoneId() == f.zone; break;
                default: break;
            }
            if (ok)
                ApplyFull(player, f);
        }
    }

    void OnKillFor(Player* player, Creature* killed)
    {
        uint32 entry = killed->GetEntry();
        for (AzQuestFix const& f : s_fixes)
        {
            if (f.mode != AZQF_ZONE_KILL && f.mode != AZQF_NPC_KILL)
                continue;
            if (player->GetQuestStatus(f.quest) != QUEST_STATUS_INCOMPLETE)
                continue;

            if (f.mode == AZQF_NPC_KILL)
            {
                if (f.npc <= 0 || entry != uint32(f.npc))
                    continue;
            }
            else
            {
                uint32 zone = ZoneOf(f);
                if (zone && killed->GetZoneId() != zone)
                    continue;
                if (killed->GetCreatureType() == CREATURE_TYPE_CRITTER || killed->IsTotem() || killed->IsPet())
                    continue;
                if (f.npc > 0 && entry != uint32(f.npc))
                    continue;
            }
            ApplyOne(player, f);
        }
    }
}

class azimov_quest_fix_world : public WorldScript
{
public:
    azimov_quest_fix_world() : WorldScript("azimov_quest_fix_world"), _timer(0) { }

    void OnStartup() override
    {
        s_fixes.clear();
        s_byQuest.clear();

        QueryResult res = WorldDatabase.Query("SELECT quest, mode, npc, radius, delay, zone, credit, item, chance, map, x, y FROM azimov_quest_fix");
        if (!res)
        {
            TC_LOG_INFO("server.loading", ">> azimov_quest_fix : aucune ligne (table vide ou absente)");
            return;
        }
        do
        {
            Field* fl = res->Fetch();
            AzQuestFix f;
            f.quest  = fl[0].GetUInt32();
            f.mode   = fl[1].GetUInt8();
            f.npc    = fl[2].GetInt32();
            f.radius = fl[3].GetFloat();
            f.delay  = fl[4].GetUInt32();
            f.zone   = fl[5].GetUInt32();
            f.credit = fl[6].GetInt32();
            f.item   = fl[7].GetUInt32();
            f.chance = fl[8].GetUInt8();
            f.map    = fl[9].GetUInt32();
            f.x      = fl[10].GetFloat();
            f.y      = fl[11].GetFloat();
            if (!sObjectMgr->GetQuestTemplate(f.quest) || f.mode > AZQF_POINT)
            {
                TC_LOG_ERROR("sql.sql", "azimov_quest_fix : quête %u ou mode %u invalide, ligne ignorée", f.quest, f.mode);
                continue;
            }
            if (f.radius <= 0.0f)
                f.radius = 20.0f;
            s_byQuest[f.quest].push_back(s_fixes.size());
            s_fixes.push_back(f);
        } while (res->NextRow());

        TC_LOG_INFO("server.loading", ">> azimov_quest_fix : %u corrections chargées pour %u quêtes",
            uint32(s_fixes.size()), uint32(s_byQuest.size()));
    }

    void OnUpdate(uint32 diff) override
    {
        if (s_fixes.empty())
            return;
        _timer += diff;
        if (_timer < 2000)
            return;
        _timer = 0;

        for (auto const& kv : sWorld->GetAllSessions())
            if (WorldSession* session = kv.second)
                if (Player* player = session->GetPlayer())
                    CheckPlayer(player);
    }

private:
    uint32 _timer;
};

class azimov_quest_fix_player : public PlayerScript
{
public:
    azimov_quest_fix_player() : PlayerScript("azimov_quest_fix_player") { }

    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        if (s_byQuest.find(questId) == s_byQuest.end())
            return;

        std::lock_guard<std::mutex> guard(s_lock);
        auto& m = s_accept[player->GetGUID().GetCounter()];
        if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            m[questId] = GameTime::GetGameTime();
        else
            m.erase(questId);
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(s_lock);
        s_accept.erase(player->GetGUID().GetCounter());
    }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (s_fixes.empty() || !killer || !killed)
            return;

        if (Group* group = killer->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member == killer || (member->IsInMap(killer) && member->IsWithinDistInMap(killed, 60.0f)))
                        OnKillFor(member, killed);
        }
        else
            OnKillFor(killer, killed);
    }
};

void AddSC_azimov_quest_fix()
{
    new azimov_quest_fix_world();
    new azimov_quest_fix_player();
}
