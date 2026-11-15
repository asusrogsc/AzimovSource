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
#include "Creature.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "RBAC.h"
#include "TemporarySummon.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <cmath>
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

    bool Needed(Player* player, uint32 entry)
    {
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
        auto it = s_spawned.find(player->GetGUID().GetCounter());
        if (it == s_spawned.end())
            return;
        for (auto const& kv : it->second)
            Despawn(player, kv.second);
        s_spawned.erase(it);
    }

    void Tick(Player* player)
    {
        if (!player->IsInWorld() || player->IsBeingTeleported())
            return;

        auto& mine = s_spawned[player->GetGUID().GetCounter()];
        for (AzPnj const& p : s_pnjs)
        {
            auto it = mine.find(p.id);
            Creature* current = it != mine.end() ? ObjectAccessor::GetCreature(*player, it->second) : nullptr;

            bool sameMap = player->GetMapId() == p.map;
            float dist = sameMap ? player->GetExactDist2d(p.x, p.y) : 100000.0f;

            if (current)
            {
                if (dist > HIDE_RANGE || !current->IsAlive() || !Needed(player, p.entry))
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
                z = player->GetMapHeight(p.x, p.y, player->GetPositionZ() + 60.0f, true, 200.0f);
                if (z <= INVALID_HEIGHT)
                    continue;
            }
            if (TempSummon* c = player->SummonCreature(p.entry, Position(p.x, p.y, z, p.o), TEMPSUMMON_MANUAL_DESPAWN, 0, 0, player->GetGUID()))
            {
                c->SetReactState(REACT_PASSIVE);
                c->SetHomePosition(c->GetPosition());
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
