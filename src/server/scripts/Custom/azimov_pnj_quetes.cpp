/*
 * azimov_pnj_quetes.cpp - PNJ de quête absents du monde
 *
 * Beaucoup de PNJ qui donnent / rendent des quêtes Cataclysm ne sont placés nulle part dans la base
 * (Keeshan au camp Sombrelac, Troteman, Eitrigg, Sylvanas aux Pins-Argentés...). Chez Blizzard, ils
 * apparaissent par phases selon l'avancement du joueur.
 *
 * Ici : chaque ligne de la table WORLD `azimov_pnj_quete` place un PNJ. Il apparaît EN PERSONNEL
 * (visible seulement par ce joueur) quand le joueur est à moins de 120 m ET qu'il en a besoin :
 *   - il peut prendre une quête que ce PNJ donne, ou
 *   - il a en cours une quête que ce PNJ donne ou rend.
 * Sinon il disparaît. Résultat : chaque version d'un PNJ (Keeshan 43458, 43611...) se montre au bon moment.
 *
 * Commandes MJ :
 *   .azpnj ici <entry> [commentaire]  ajoute un point à votre position (z et orientation compris)
 *   .azpnj proche                     liste les points à moins de 100 m (id, entry, nom, distance)
 *   .azpnj suppr <id>                 supprime un point
 *   .azpnj recharger                  relit la table
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "RBAC.h"
#include "SpellAuraDefines.h"
#include "TemporarySummon.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    struct AzPnj
    {
        uint32 id = 0;
        uint32 entry = 0;
        uint32 map = 0;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;   // z = 0 : posé au sol automatiquement
    };

    std::vector<AzPnj> s_pnjs;
    // joueur -> (id du point -> PNJ personnel)
    std::unordered_map<ObjectGuid::LowType, std::unordered_map<uint32, ObjectGuid>> s_spawned;
    // PNJ de quête présents mais INVISIBLES pour le joueur (aura d'invisibilité / détection manquante) :
    // vrai PNJ -> copie personnelle visible
    std::unordered_map<ObjectGuid::LowType, std::unordered_map<ObjectGuid, ObjectGuid>> s_copies;
    float const HIDDEN_RANGE = 60.0f;
    // Valideurs de quêtes terminées : copie au point de spawn si le vrai est hors phase / invisible / pas là
    std::unordered_map<ObjectGuid::LowType, std::unordered_map<uint32, ObjectGuid>> s_enders;
    struct SpawnPt { uint32 map; Position pos; };
    std::unordered_map<uint32, std::vector<SpawnPt>> s_spawnIndex;   // entrée -> points de spawn de la base
    bool s_spawnIndexBuilt = false;

    void BuildSpawnIndex()
    {
        s_spawnIndexBuilt = true;
        for (auto const& kv : sObjectMgr->GetAllCreatureData())
            s_spawnIndex[kv.second.id].push_back({ kv.second.mapId, kv.second.spawnPoint });
    }

    float const SHOW_RANGE = 120.0f;
    float const HIDE_RANGE = 150.0f;

    void Load()
    {
        s_pnjs.clear();
        QueryResult res = WorldDatabase.Query("SELECT id, entry, map, x, y, z, o FROM azimov_pnj_quete");
        if (!res)
            return;
        do
        {
            Field* f = res->Fetch();
            AzPnj p;
            p.id = f[0].GetUInt32();
            p.entry = f[1].GetUInt32();
            p.map = f[2].GetUInt32();
            p.x = f[3].GetFloat();
            p.y = f[4].GetFloat();
            p.z = f[5].GetFloat();
            p.o = f[6].GetFloat();
            if (!sObjectMgr->GetCreatureTemplate(p.entry))
            {
                TC_LOG_ERROR("sql.sql", "azimov_pnj_quete : id %u, PNJ %u inexistant, ligne ignorée", p.id, p.entry);
                continue;
            }
            s_pnjs.push_back(p);
        } while (res->NextRow());
    }

    // Cible à tuer d'une quête en cours (objectif pas encore rempli) : boss/nommés absents de la base.
    bool IsKillTarget(Player* player, uint32 entry)
    {
        CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry);
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = player->GetQuestSlotQuestId(slot);
            if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                continue;
            Quest const* q = sObjectMgr->GetQuestTemplate(questId);
            if (!q)
                continue;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 req = q->RequiredNpcOrGo[i];
                if (req <= 0 || !q->RequiredNpcOrGoCount[i])
                    continue;
                bool match = uint32(req) == entry;
                if (!match && ct)
                    for (uint32 kc : ct->KillCredit)
                        if (kc && kc == uint32(req))
                            match = true;
                if (match && player->GetReqKillOrCastCurrentCount(questId, req) < q->RequiredNpcOrGoCount[i])
                    return true;
            }
        }
        return false;
    }

    // Donne le crédit de quête pour cette cible (entrée directe ou via KillCredit), objectifs pas encore remplis.
    void GiveKillCredit(Player* player, Creature* c)
    {
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = player->GetQuestSlotQuestId(slot);
            if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                continue;
            Quest const* q = sObjectMgr->GetQuestTemplate(questId);
            if (!q)
                continue;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 req = q->RequiredNpcOrGo[i];
                if (req <= 0 || player->GetReqKillOrCastCurrentCount(questId, req) >= q->RequiredNpcOrGoCount[i])
                    continue;
                bool match = uint32(req) == c->GetEntry();
                for (uint32 kc : ct->KillCredit)
                    if (kc && kc == uint32(req))
                        match = true;
                if (match)
                    player->KilledMonsterCredit(uint32(req), c->GetGUID());
            }
        }
    }

    bool Needed(Player* player, uint32 entry)
    {
        if (IsKillTarget(player, entry))
            return true;
        for (uint32 questId : sObjectMgr->GetCreatureQuestRelations(entry))
        {
            QuestStatus st = player->GetQuestStatus(questId);
            if (st == QUEST_STATUS_INCOMPLETE || st == QUEST_STATUS_COMPLETE)
                return true;
            if (st == QUEST_STATUS_NONE && !player->GetQuestRewardStatus(questId))
                if (Quest const* q = sObjectMgr->GetQuestTemplate(questId))
                    if (player->CanTakeQuest(q, false))
                        return true;
        }
        for (uint32 questId : sObjectMgr->GetCreatureQuestInvolvedRelations(entry))
        {
            QuestStatus st = player->GetQuestStatus(questId);
            if (st == QUEST_STATUS_INCOMPLETE || st == QUEST_STATUS_COMPLETE)
                return true;
        }
        return false;
    }

    void Despawn(Player* player, ObjectGuid const& guid)
    {
        if (Creature* c = ObjectAccessor::GetCreature(*player, guid))
            c->DespawnOrUnsummon();
    }

    void DespawnAll(Player* player)
    {
        auto en = s_enders.find(player->GetGUID().GetCounter());
        if (en != s_enders.end())
        {
            for (auto const& kv : en->second)
                Despawn(player, kv.second);
            s_enders.erase(en);
        }
        auto ct = s_copies.find(player->GetGUID().GetCounter());
        if (ct != s_copies.end())
        {
            for (auto const& kv : ct->second)
                Despawn(player, kv.second);
            s_copies.erase(ct);
        }
        auto it = s_spawned.find(player->GetGUID().GetCounter());
        if (it == s_spawned.end())
            return;
        for (auto const& kv : it->second)
            Despawn(player, kv.second);
        s_spawned.erase(it);
    }

    struct HiddenQuestNpcCheck
    {
        Player* player;
        bool operator()(Creature* c) const
        {
            if (!c->IsAlive() || c->IsSummon() || c->IsInCombat() || player->GetExactDist(c) > HIDDEN_RANGE)
                return false;
            QuestRelationResult gives = sObjectMgr->GetCreatureQuestRelations(c->GetEntry());
            QuestRelationResult ends = sObjectMgr->GetCreatureQuestInvolvedRelations(c->GetEntry());
            if (gives.begin() == gives.end() && ends.begin() == ends.end())
                return false;
            return !player->CanSeeOrDetect(c, false, false, false);
        }
    };

    // Tout PNJ donneur/valideur de quête invisible pour ce joueur alors qu'il en a besoin : copie personnelle visible.
    void TickHidden(Player* player)
    {
        auto& copies = s_copies[player->GetGUID().GetCounter()];

        // nettoyage des copies devenues inutiles
        for (auto it = copies.begin(); it != copies.end();)
        {
            Creature* real = ObjectAccessor::GetCreature(*player, it->first);
            Creature* copy = ObjectAccessor::GetCreature(*player, it->second);
            bool keep = real && copy && copy->IsAlive() && player->GetExactDist(real) <= HIDDEN_RANGE + 20.0f
                && !player->CanSeeOrDetect(real, false, false, false) && Needed(player, real->GetEntry());
            if (!keep)
            {
                if (copy)
                    copy->DespawnOrUnsummon();
                it = copies.erase(it);
            }
            else
                ++it;
        }

        std::list<Creature*> found;
        HiddenQuestNpcCheck check{ player };
        Trinity::CreatureListSearcher<HiddenQuestNpcCheck> searcher(player, found, check);
        Cell::VisitGridObjects(player, searcher, HIDDEN_RANGE);

        for (Creature* real : found)
        {
            if (copies.count(real->GetGUID()) || !Needed(player, real->GetEntry()))
                continue;
            if (TempSummon* c = player->SummonCreature(real->GetEntry(), real->GetPosition(), TEMPSUMMON_MANUAL_DESPAWN, 0, 0, player->GetGUID()))
            {
                c->SetReactState(REACT_PASSIVE);
                c->SetHomePosition(c->GetPosition());
                c->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
                c->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
                c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                copies[real->GetGUID()] = c->GetGUID();
            }
        }
        if (copies.empty())
            s_copies.erase(player->GetGUID().GetCounter());
    }

    bool SeesEntryNearby(Player* player, uint32 entry, float range)
    {
        std::list<Creature*> lst;
        player->GetCreatureListWithEntryInGrid(lst, entry, range);
        for (Creature* c : lst)
            if (c->IsAlive() && player->CanSeeOrDetect(c, false, false, false))
                return true;
        return false;
    }

    // Quête TERMINÉE mais son valideur n'est pas visible (autre phase, invisible, non posé à cet endroit) :
    // copie personnelle à son point de spawn le plus proche.
    void TickEnders(Player* player)
    {
        if (!s_spawnIndexBuilt)
            BuildSpawnIndex();
        auto& mine = s_enders[player->GetGUID().GetCounter()];

        for (auto it = mine.begin(); it != mine.end();)
        {
            Creature* copy = ObjectAccessor::GetCreature(*player, it->second);
            if (!copy || !copy->IsAlive() || player->GetExactDist(copy) > 150.0f || !Needed(player, it->first))
            {
                if (copy)
                    copy->DespawnOrUnsummon();
                it = mine.erase(it);
            }
            else
                ++it;
        }

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = player->GetQuestSlotQuestId(slot);
            if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
                continue;
            for (uint32 entry : sObjectMgr->GetCreatureQuestInvolvedRelationsReverse(questId))
            {
                if (mine.count(entry))
                    continue;
                auto idx = s_spawnIndex.find(entry);
                if (idx == s_spawnIndex.end())
                    continue;
                SpawnPt const* best = nullptr;
                float bestDist = 100.0f;
                for (SpawnPt const& sp : idx->second)
                {
                    if (sp.map != player->GetMapId())
                        continue;
                    float d = player->GetExactDist(sp.pos);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        best = &sp;
                    }
                }
                Position where;
                if (best)
                    where = best->pos;
                else
                {
                    // pas de spawn proche : point de rendu affiché sur la carte (quest_poi, objectif -1)
                    QuestPOIData const* poi = sObjectMgr->GetQuestPOIData(questId);
                    if (!poi)
                        continue;
                    bool found = false;
                    for (QuestPOIBlobData const& blob : poi->Blobs)
                    {
                        if (blob.ObjectiveIndex != -1 || uint32(blob.MapID) != player->GetMapId() || blob.Points.empty())
                            continue;
                        float sx = 0.0f, sy = 0.0f;
                        for (QuestPOIBlobPoint const& pt : blob.Points)
                        {
                            sx += float(pt.X);
                            sy += float(pt.Y);
                        }
                        sx /= float(blob.Points.size());
                        sy /= float(blob.Points.size());
                        if (player->GetExactDist2d(sx, sy) > 60.0f)
                            continue;
                        float z = player->GetMapHeight(sx, sy, player->GetPositionZ() + 5.0f, true, 50.0f);
                        if (z <= INVALID_HEIGHT)
                            continue;
                        where.Relocate(sx, sy, z, player->GetAngle(sx, sy) + float(M_PI));
                        found = true;
                        break;
                    }
                    if (!found)
                        continue;
                }
                if (SeesEntryNearby(player, entry, 120.0f))
                    continue;
                if (TempSummon* c = player->SummonCreature(entry, where, TEMPSUMMON_MANUAL_DESPAWN, 0, 0, player->GetGUID()))
                {
                    c->SetReactState(REACT_PASSIVE);
                    c->SetHomePosition(c->GetPosition());
                    c->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
                    c->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
                    c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                    mine[entry] = c->GetGUID();
                }
            }
        }
        if (mine.empty())
            s_enders.erase(player->GetGUID().GetCounter());
    }

    void Tick(Player* player)
    {
        if (!player->IsInWorld() || player->IsBeingTeleported())
            return;

        TickEnders(player);

        TickHidden(player);

        auto& mine = s_spawned[player->GetGUID().GetCounter()];
        for (AzPnj const& p : s_pnjs)
        {
            auto it = mine.find(p.id);
            Creature* current = it != mine.end() ? ObjectAccessor::GetCreature(*player, it->second) : nullptr;

            bool sameMap = player->GetMapId() == p.map;
            float dist = sameMap ? player->GetExactDist2d(p.x, p.y) : 100000.0f;

            if (current)
            {
                if (!current->IsAlive())
                {
                    // tué mais objectif toujours vide (crédit refusé par le cœur) : on le donne
                    if (player->GetExactDist(current) < 100.0f && IsKillTarget(player, p.entry))
                        GiveKillCredit(player, current);
                    continue;                       // cadavre : il reste pour le butin puis disparaît seul
                }
                // cible bloquée à 1 PV (invincibilité prévue pour une scène Blizzard absente) : on l'achève
                if (current->IsInCombat() && current->GetHealthPct() <= 2.0f && IsKillTarget(player, p.entry))
                {
                    Unit::Kill(player, current);
                    continue;
                }
                if (dist > HIDE_RANGE || !Needed(player, p.entry))
                {
                    current->DespawnOrUnsummon();
                    mine.erase(it);
                }
                continue;
            }
            if (it != mine.end())
                mine.erase(it);

            if (dist > SHOW_RANGE || !Needed(player, p.entry))
                continue;

            float z = p.z;
            if (z == 0.0f)
            {
                z = player->GetMapHeight(p.x, p.y, player->GetPositionZ() + 3.0f, true, 100.0f);
                if (z <= INVALID_HEIGHT)
                    continue;
            }
            bool cible = IsKillTarget(player, p.entry);
            if (TempSummon* c = player->SummonCreature(p.entry, Position(p.x, p.y, z, p.o),
                cible ? TEMPSUMMON_CORPSE_TIMED_DESPAWN : TEMPSUMMON_MANUAL_DESPAWN, cible ? 60000 : 0, 0, player->GetGUID()))
            {
                if (!cible)
                    c->SetReactState(REACT_PASSIVE);   // donneur de quête : reste calme
                else
                {
                    c->SetReactState(REACT_AGGRESSIVE);
                    c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_PACIFIED);
                    // insensible / immunisé (attente d'un événement scripté) : on retire ces protections
                    c->RemoveAurasByType(SPELL_AURA_SCHOOL_IMMUNITY);
                    c->RemoveAurasByType(SPELL_AURA_DAMAGE_IMMUNITY);
                    c->RemoveAurasByType(SPELL_AURA_MOD_STUN);
                    c->m_PlayerDamageReq = 0;      // PNJ personnel : tout kill du joueur compte (butin + crédit)
                    // élite prévu pour un groupe ou l'escouade Bravo (absente ici) : vie plafonnée à 1,5x celle du joueur
                    if (c->isElite())
                    {
                        uint32 cap = std::max<uint32>(1, player->GetMaxHealth() * 3 / 2);
                        if (c->GetMaxHealth() > cap)
                        {
                            c->SetMaxHealth(cap);
                            c->SetFullHealth();
                        }
                        c->m_PlayerDamageReq = 0;
                    }
                }
                c->SetHomePosition(c->GetPosition());
                // ces PNJ portent souvent une invisibilité de quête (phase Blizzard) : inutile ici,
                // le PNJ est déjà personnel, on la retire pour que le joueur le voie
                c->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
                mine[p.id] = c->GetGUID();
            }
        }
        if (mine.empty())
            s_spawned.erase(player->GetGUID().GetCounter());
    }
}

class azimov_pnj_quetes_world : public WorldScript
{
public:
    azimov_pnj_quetes_world() : WorldScript("azimov_pnj_quetes_world"), _timer(0) { }

    void OnStartup() override
    {
        Load();
        TC_LOG_INFO("server.loading", ">> azimov_pnj_quete : %u PNJ de quête placés", uint32(s_pnjs.size()));
    }

    void OnUpdate(uint32 diff) override
    {
        if (s_pnjs.empty())
            return;
        _timer += diff;
        if (_timer < 1500)
            return;
        _timer = 0;

        for (auto const& kv : sWorld->GetAllSessions())
            if (WorldSession* s = kv.second)
                if (Player* p = s->GetPlayer())
                    Tick(p);
    }

private:
    uint32 _timer;
};

class azimov_pnj_quetes_player : public PlayerScript
{
public:
    azimov_pnj_quetes_player() : PlayerScript("azimov_pnj_quetes_player") { }

    void OnLogout(Player* player) override { DespawnAll(player); }
};

class azimov_pnj_quetes_commands : public CommandScript
{
public:
    azimov_pnj_quetes_commands() : CommandScript("azimov_pnj_quetes_commands") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> sub =
        {
            { "ici",       SEC_GAMEMASTER, false, &HandleIci,       "" },
            { "proche",    SEC_GAMEMASTER, false, &HandleProche,    "" },
            { "suppr",     SEC_GAMEMASTER, false, &HandleSuppr,     "" },
            { "recharger", SEC_GAMEMASTER, false, &HandleRecharger, "" },
        };
        static std::vector<ChatCommand> root =
        {
            { "azpnj", SEC_GAMEMASTER, false, nullptr, "", sub },
        };
        return root;
    }

    static void ReloadAndRespawn()
    {
        for (auto const& kv : sWorld->GetAllSessions())
            if (WorldSession* s = kv.second)
                if (Player* p = s->GetPlayer())
                    DespawnAll(p);
        Load();
    }

    static bool HandleIci(ChatHandler* handler, char const* args)
    {
        Player* gm = handler->GetSession()->GetPlayer();
        if (!gm || !args || !*args)
        {
            handler->SendSysMessage("Usage : .azpnj ici <entry> [commentaire]");
            return true;
        }
        std::string a = args;
        size_t sp = a.find(' ');
        uint32 entry = uint32(atoul(a.substr(0, sp).c_str()));
        std::string comment = sp == std::string::npos ? "" : a.substr(sp + 1);
        CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry);
        if (!ct)
        {
            handler->PSendSysMessage("PNJ %u inconnu.", entry);
            return true;
        }
        if (comment.empty())
            comment = ct->Name;
        WorldDatabase.EscapeString(comment);

        WorldDatabase.DirectPExecute("INSERT INTO azimov_pnj_quete (entry, map, x, y, z, o, comment) VALUES (%u, %u, %f, %f, %f, %f, '%s')",
            entry, gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ(), gm->GetOrientation(), comment.c_str());
        ReloadAndRespawn();
        handler->PSendSysMessage("Point ajouté : %s (%u) ici. Il apparaîtra pour les joueurs qui en ont besoin.", ct->Name.c_str(), entry);
        return true;
    }

    static bool HandleProche(ChatHandler* handler, char const* /*args*/)
    {
        Player* gm = handler->GetSession()->GetPlayer();
        if (!gm)
            return false;
        uint32 n = 0;
        for (AzPnj const& p : s_pnjs)
        {
            if (p.map != gm->GetMapId())
                continue;
            float d = gm->GetExactDist2d(p.x, p.y);
            if (d > 100.0f)
                continue;
            CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(p.entry);
            handler->PSendSysMessage("id %u : %s (%u) à %.0f m%s", p.id, ct ? ct->Name.c_str() : "?", p.entry, d,
                Needed(gm, p.entry) ? "" : " (non requis pour vous)");
            ++n;
        }
        if (!n)
            handler->SendSysMessage("Aucun point azpnj à moins de 100 m.");
        return true;
    }

    static bool HandleSuppr(ChatHandler* handler, char const* args)
    {
        uint32 id = args ? uint32(atoul(args)) : 0;
        if (!id)
        {
            handler->SendSysMessage("Usage : .azpnj suppr <id>   (voir .azpnj proche)");
            return true;
        }
        WorldDatabase.DirectPExecute("DELETE FROM azimov_pnj_quete WHERE id = %u", id);
        ReloadAndRespawn();
        handler->PSendSysMessage("Point %u supprimé.", id);
        return true;
    }

    static bool HandleRecharger(ChatHandler* handler, char const* /*args*/)
    {
        ReloadAndRespawn();
        handler->PSendSysMessage("azimov_pnj_quete rechargée : %u points.", uint32(s_pnjs.size()));
        return true;
    }
};

void AddSC_azimov_pnj_quetes()
{
    new azimov_pnj_quetes_world();
    new azimov_pnj_quetes_player();
    new azimov_pnj_quetes_commands();
}
