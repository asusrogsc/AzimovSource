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
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
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

    // 26512 Tuning the Gnomecorder : tester le gnomotophone au cimetière de Comté-du-Lac (carte [32, 39])
    Position const PosGraveyard = { -9182.3f, -2301.7f, 0.0f, 0.0f };

    void Tick(Player* p)
    {
        if (!p->IsInWorld() || p->GetMapId() != 0 || p->IsBeingTeleported())
            return;

        if (p->GetQuestStatus(26512) == QUEST_STATUS_INCOMPLETE && p->GetExactDist2d(&PosGraveyard) < 30.0f)
        {
            ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100Le gnomotophone grésille et crépite... Magistrat Solomon : « Je vous reçois cinq sur cinq ! »|r");
            p->AreaExploredOrEventHappens(26512);
            if (p->GetQuestStatus(26512) == QUEST_STATUS_INCOMPLETE)
                p->CompleteQuest(26512);
        }

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

// Prisoners of War (26646) : le levier à chaînes (avec la Clé du levier rochenoire) libère les prisonniers
class go_azimov_chain_lever : public GameObjectScript
{
public:
    go_azimov_chain_lever() : GameObjectScript("go_azimov_chain_lever") { }

    struct go_azimov_chain_leverAI : public GameObjectAI
    {
        go_azimov_chain_leverAI(GameObject* go) : GameObjectAI(go) { }

        bool GossipHello(Player* player) override
        {
            if (player && player->GetQuestStatus(26646) == QUEST_STATUS_INCOMPLETE)
            {
                player->KilledMonsterCredit(43574);
                ChatHandler(player->GetSession()).PSendSysMessage("|cffffd100Les chaînes cèdent dans un grand fracas : la porte de la cage des prisonniers s'ouvre ! Retournez voir Keeshan.|r");
            }
            return false;   // le levier s'actionne normalement
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_azimov_chain_leverAI(go);
    }
};

// ---------------------------------------------------------------- Orbe de contrôle d'ettin (objet 58895, sort 80704)
// « He Who Controls the Ettins » / « Saving Foreman Oslow » : l'orbe soumet un ettin du canyon,
// l'ettin soumis (43197) suit le joueur ; près du contremaître Oslow (341) il soulève les rochers.
enum EttinControl : uint32
{
    NPC_CONTROLLED_ETTIN = 43197,
    NPC_FOREMAN_OSLOW    = 341
};

class spell_azimov_ettin_control : public SpellScript
{
    // L'orbe (objet 58895) lance 80704 sur soi : on prend l'ettin du canyon ciblé par le joueur
    Creature* FindEttin(Player* player)
    {
        if (Unit* sel = player->GetSelectedUnit())
            if (Creature* c = sel->ToCreature())
                if (c->IsAlive() && !c->IsSummon() && player->IsWithinDistInMap(c, 20.0f)
                    && c->GetCreatureTemplate()->Name.find("Ettin") != std::string::npos)
                    return c;
        return nullptr;
    }

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || !FindEttin(player))
            return SPELL_FAILED_BAD_TARGETS;
        return SPELL_CAST_OK;
    }

    void HandleScript(SpellEffIndex /*eff*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return;
        Creature* ettin = FindEttin(player);
        if (!ettin)
            return;

        Position pos = ettin->GetPosition();
        ettin->DespawnOrUnsummon(0, 30s);
        if (TempSummon* tamed = player->SummonCreature(NPC_CONTROLLED_ETTIN, pos, TEMPSUMMON_TIMED_DESPAWN, 300000, 0, player->GetGUID()))
        {
            tamed->SetFaction(player->GetFaction());
            tamed->SetReactState(REACT_PASSIVE);
            tamed->GetMotionMaster()->MoveFollow(player, 3.0f, float(M_PI));
            tamed->Say("GRAAAH... Ettin obéir...", LANG_UNIVERSAL);
        }
        player->KilledMonsterCredit(NPC_CONTROLLED_ETTIN);
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_azimov_ettin_control::CheckCast);
        OnEffectHit.Register(&spell_azimov_ettin_control::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

struct npc_azimov_controlled_ettin : public ScriptedAI
{
    npc_azimov_controlled_ettin(Creature* creature) : ScriptedAI(creature), _timer(1000), _done(false) { }

    void UpdateAI(uint32 diff) override
    {
        if (_done || _timer > diff)
        {
            if (!_done)
                _timer -= diff;
            return;
        }
        _timer = 1000;

        TempSummon* self = me->ToTempSummon();
        Unit* owner = self ? self->GetSummoner() : nullptr;
        Player* player = owner ? owner->ToPlayer() : nullptr;
        if (!player)
        {
            me->DespawnOrUnsummon();
            return;
        }
        if (Creature* oslow = me->FindNearestCreature(NPC_FOREMAN_OSLOW, 15.0f))
        {
            _done = true;
            me->GetMotionMaster()->Clear();
            me->SetFacingToObject(oslow);
            me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK2HTIGHT);
            me->Say("RAAAH ! Ettin soulever cailloux !", LANG_UNIVERSAL);
            player->KilledMonsterCredit(NPC_FOREMAN_OSLOW);
            oslow->Say("Je suis libre ! Merci, merci mille fois !", LANG_UNIVERSAL);
            me->DespawnOrUnsummon(5s);
        }
    }

private:
    uint32 _timer;
    bool _done;
};

// ---------------------------------------------------------------- 26651 To Win a War, You Gotta Become War
// Le paquetage Bravo lance « Poser l'hydroglycérine » (81365 -> 81342 / 82585) : le sort ne trouve aucune cible
// dans la base. On donne le crédit de l'emplacement le plus proche (tour / hutte à munitions), sinon le premier manquant.
class spell_azimov_place_hydroglycerin : public SpellScript
{
    void HandleAfterCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(26651) != QUEST_STATUS_INCOMPLETE)
            return;
        Quest const* q = sObjectMgr->GetQuestTemplate(26651);
        if (!q)
            return;

        int32 chosen = 0;
        float best = 40.0f;
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 t = q->RequiredNpcOrGo[i];
            if (t <= 0 || player->GetReqKillOrCastCurrentCount(26651, t) >= q->RequiredNpcOrGoCount[i])
                continue;
            if (Creature* bunny = player->FindNearestCreature(uint32(t), best))
            {
                best = player->GetDistance(bunny);
                chosen = t;
            }
        }
        if (!chosen)   // repères absents de la base : premier objectif manquant
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT && !chosen; ++i)
            {
                int32 t = q->RequiredNpcOrGo[i];
                if (t > 0 && player->GetReqKillOrCastCurrentCount(26651, t) < q->RequiredNpcOrGoCount[i])
                    chosen = t;
            }
        if (chosen)
        {
            player->KilledMonsterCredit(uint32(chosen));
            ChatHandler(player->GetSession()).PSendSysMessage("|cffffd100Charge d'hydroglycérine en place !|r");
        }
    }

    void Register() override
    {
        AfterCast.Register(&spell_azimov_place_hydroglycerin::HandleAfterCast);
    }
};

void AddSC_azimov_quetes_carmines()
{
    RegisterSpellScript(spell_azimov_place_hydroglycerin);
    RegisterSpellScript(spell_azimov_ettin_control);
    RegisterCreatureAI(npc_azimov_controlled_ettin);
    new go_azimov_chain_lever();
    new azimov_carmines_world();
    new azimov_carmines_player();
}
