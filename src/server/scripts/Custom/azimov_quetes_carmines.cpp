/*
 * azimov_quetes_carmines.cpp - Les Carmines : la barque de Keeshan (26616 It's Never Over)
 *
 * Blizzard : le joueur monte dans la barque (43450) au ponton du lac Placide, Messner la propulse
 * jusqu'au camp Placide. La barque n'a pas de véhicule dans la base : ici, en arrivant au ponton avec
 * la quête, une barque personnelle apparaît avec Keeshan, la scène se joue, puis le joueur est déposé
 * au camp Placide et la quête est validée.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include <unordered_map>

namespace
{
    enum : uint32
    {
        QUEST_ITS_NEVER_OVER = 26616,
        NPC_RIVERBOAT        = 43450,
        NPC_KEESHAN_BOAT     = 43449
    };

    // Ponton du lac Placide (position Blizzard du sort 81243 « Invocation du bateau fluvial de Keeshan »)
    Position const PosDock    = { -9300.98f, -2363.67f, 55.58f, 1.57f };
    Position const PosKeeshan = { -9299.50f, -2361.50f, 56.00f, 4.70f };
    // Arrivée au camp Placide, sur l'autre rive (à quelques mètres de Keeshan)
    Position const PosCamp    = { -9439.3f, -2815.9f, 0.0f, 4.70f };

    struct BoatState
    {
        uint8 step = 0;
        uint32 nextMs = 0;
        ObjectGuid boat, keeshan;
        bool done = false;
    };

    std::unordered_map<ObjectGuid::LowType, BoatState> s_boat;

    void Say(Player* p, ObjectGuid const& g, char const* text)
    {
        if (Creature* c = ObjectAccessor::GetCreature(*p, g))
            c->Say(text, LANG_UNIVERSAL);
        else
            ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100%s|r", text);
    }

    void Cleanup(Player* p, BoatState& st)
    {
        for (ObjectGuid* g : { &st.boat, &st.keeshan })
        {
            if (Creature* c = ObjectAccessor::GetCreature(*p, *g))
                c->DespawnOrUnsummon();
            g->Clear();
        }
    }

    void Tick(Player* p)
    {
        if (!p->IsInWorld() || p->GetMapId() != 0 || p->IsBeingTeleported())
            return;

        auto it = s_boat.find(p->GetGUID().GetCounter());
        QuestStatus qs = p->GetQuestStatus(QUEST_ITS_NEVER_OVER);
        bool hasQuest = qs == QUEST_STATUS_INCOMPLETE || qs == QUEST_STATUS_COMPLETE;
        float dist = p->GetExactDist2d(&PosDock);

        if (it == s_boat.end())
        {
            if (!hasQuest || dist > 12.0f)
                return;
            it = s_boat.emplace(p->GetGUID().GetCounter(), BoatState()).first;
        }

        BoatState& st = it->second;
        if (st.done || !p->IsAlive() || (st.step < 5 && dist > 40.0f))
        {
            Cleanup(p, st);
            if (!st.done || dist > 40.0f)
                s_boat.erase(it);
            return;
        }

        uint32 now = GameTime::GetGameTimeMS();
        if (now < st.nextMs)
            return;

        switch (st.step)
        {
            case 0:
                if (TempSummon* b = p->SummonCreature(NPC_RIVERBOAT, PosDock, TEMPSUMMON_MANUAL_DESPAWN, 0, 0, p->GetGUID()))
                {
                    b->SetReactState(REACT_PASSIVE);
                    st.boat = b->GetGUID();
                }
                if (TempSummon* k = p->SummonCreature(NPC_KEESHAN_BOAT, PosKeeshan, TEMPSUMMON_MANUAL_DESPAWN, 0, 0, p->GetGUID()))
                {
                    k->SetReactState(REACT_PASSIVE);
                    k->SetFaction(35);
                    st.keeshan = k->GetGUID();
                }
                st.nextMs = now + 2000;
                break;
            case 1:
                Say(p, st.keeshan, "Tout le monde à bord ! Emmène-nous au large, Messner.");
                st.nextMs = now + 3500;
                break;
            case 2:
                ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100Messner : C'est parti, Johnny !|r");
                st.nextMs = now + 2500;
                break;
            case 3:
                ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100Messner déclenche une gerbe de flammes à l'arrière de la barque, qui file à toute allure sur le lac Placide...|r");
                st.nextMs = now + 4000;
                break;
            case 4:
            {
                Cleanup(p, st);
                float z = p->GetMapHeight(PosCamp.GetPositionX(), PosCamp.GetPositionY(), 120.0f, true, 200.0f);
                if (z <= INVALID_HEIGHT)
                    z = 60.0f;
                p->NearTeleportTo(PosCamp.GetPositionX(), PosCamp.GetPositionY(), z + 0.5f, PosCamp.GetOrientation());
                st.nextMs = now + 2500;
                break;
            }
            case 5:
                ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100John J. Keeshan : On y est, tout le monde descend !|r");
                if (p->GetQuestStatus(QUEST_ITS_NEVER_OVER) == QUEST_STATUS_INCOMPLETE)
                {
                    p->AreaExploredOrEventHappens(QUEST_ITS_NEVER_OVER);
                    if (p->GetQuestStatus(QUEST_ITS_NEVER_OVER) == QUEST_STATUS_INCOMPLETE)
                        p->CompleteQuest(QUEST_ITS_NEVER_OVER);
                }
                st.done = true;
                break;
        }
        ++st.step;
    }
}

class azimov_carmines_world : public WorldScript
{
public:
    azimov_carmines_world() : WorldScript("azimov_carmines_world"), _timer(0) { }

    void OnUpdate(uint32 diff) override
    {
        _timer += diff;
        if (_timer < 1000)
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

class azimov_carmines_player : public PlayerScript
{
public:
    azimov_carmines_player() : PlayerScript("azimov_carmines_player") { }

    void OnLogout(Player* player) override
    {
        auto it = s_boat.find(player->GetGUID().GetCounter());
        if (it == s_boat.end())
            return;
        Cleanup(player, it->second);
        s_boat.erase(it);
    }
};

void AddSC_azimov_quetes_carmines()
{
    new azimov_carmines_world();
    new azimov_carmines_player();
}
