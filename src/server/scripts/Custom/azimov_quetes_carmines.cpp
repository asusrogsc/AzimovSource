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
#include "Random.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <list>
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

    // 26708 AHHHHHHHHHHHH! : le char de siège de la compagnie Bravo (43734) attend près de Keeshan (position Blizzard du sort 81808).
    // Le joueur en PREND LE CONTRÔLE (possession, barre d'actions du char : Charge fracassante) ; des vagues d'envahisseurs
    // rochenoirs arrivent (aucun n'est placé dans la base aux Carmines). Les orcs tués par le char comptent pour le joueur.
    Position const PosSiegeTank = { -9646.64f, -3454.94f, 117.49f, 1.94f };

    struct TankState
    {
        ObjectGuid tank;
        std::list<ObjectGuid> invaders;
        uint32 nextWaveMs = 0;
    };
    std::unordered_map<ObjectGuid::LowType, TankState> s_tanks;

    void ReleaseTank(Player* p, TankState& st)
    {
        if (Creature* tank = ObjectAccessor::GetCreature(*p, st.tank))
        {
            if (tank->GetCharmerGUID() == p->GetGUID())
                tank->RemoveCharmedBy(p);
            tank->DespawnOrUnsummon(1s);
        }
        for (ObjectGuid const& g : st.invaders)
            if (Creature* c = ObjectAccessor::GetCreature(*p, g))
                c->DespawnOrUnsummon();
        st.invaders.clear();
        st.tank.Clear();
    }

    void SpawnWave(Player* p, Creature* tank, TankState& st)
    {
        st.invaders.remove_if([p](ObjectGuid const& g)
        {
            Creature* c = ObjectAccessor::GetCreature(*p, g);
            return !c || !c->IsAlive();
        });
        if (st.invaders.size() >= 15 || tank->GetExactDist2d(&PosSiegeTank) > 700.0f)
            return;
        for (int i = 0; i < 5; ++i)
        {
            float angle = tank->GetOrientation() + frand(-1.3f, 1.3f);
            float dist = frand(15.0f, 28.0f);
            float x = tank->GetPositionX() + std::cos(angle) * dist;
            float y = tank->GetPositionY() + std::sin(angle) * dist;
            float z = tank->GetMapHeight(x, y, tank->GetPositionZ() + 10.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - tank->GetPositionZ()) > 15.0f)
                continue;
            if (TempSummon* orc = tank->SummonCreature(43775, x, y, z, angle + float(M_PI), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 90000, p->GetGUID()))
            {
                orc->SetFaction(2313);
                orc->SetReactState(REACT_AGGRESSIVE);
                orc->SetMaxHealth(std::max<uint32>(1, orc->GetMaxHealth() / 2));   // piétaille : le char les balaie
                orc->SetFullHealth();
                if (orc->AI())
                    orc->AI()->AttackStart(tank);
                st.invaders.push_back(orc->GetGUID());
            }
        }
    }

    void TickSiegeTank(Player* p)
    {
        ObjectGuid::LowType key = p->GetGUID().GetCounter();
        bool onQuest = p->GetQuestStatus(26708) == QUEST_STATUS_INCOMPLETE;
        auto it = s_tanks.find(key);

        if (it != s_tanks.end())
        {
            TankState& st = it->second;
            Creature* tank = ObjectAccessor::GetCreature(*p, st.tank);
            if (!onQuest || !tank || !tank->IsAlive() || !p->IsAlive())
            {
                if (!onQuest && p->GetQuestStatus(26708) == QUEST_STATUS_COMPLETE)
                    ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100200 Rochenoires écrasés ! Allez retrouver Keeshan de l'autre côté de la vallée.|r");
                ReleaseTank(p, st);
                s_tanks.erase(it);
                return;
            }
            uint32 now = GameTime::GetGameTimeMS();
            if (now >= st.nextWaveMs)
            {
                st.nextWaveMs = now + 5000;
                SpawnWave(p, tank, st);
            }
            return;
        }

        if (!onQuest || p->GetExactDist2d(&PosSiegeTank) > 12.0f || p->IsMounted() || p->IsInCombat())
            return;

        TempSummon* tank = p->SummonCreature(43734, PosSiegeTank, TEMPSUMMON_TIMED_DESPAWN, 1800000, 0, p->GetGUID());
        if (!tank)
            return;
        tank->SetFaction(p->GetFaction());
        tank->SetLevel(p->GetLevel());
        tank->SetMaxHealth(p->GetMaxHealth() * 6);
        tank->SetFullHealth();
        tank->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(p->GetLevel() * 12));
        tank->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(p->GetLevel() * 18));
        tank->UpdateDamagePhysical(BASE_ATTACK);
        tank->SetSpeedRate(MOVE_RUN, 1.5f);

        TankState& st = s_tanks[key];
        st.tank = tank->GetGUID();
        st.nextWaveMs = GameTime::GetGameTimeMS() + 3000;
        if (tank->SetCharmedBy(p, CHARM_TYPE_POSSESS))
            ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100Vous prenez les commandes du char de siège de la compagnie Bravo ! Utilisez Charge fracassante (barre du char) pour écraser 200 Rochenoires.|r");
    }

    void Tick(Player* p)
    {
        if (!p->IsInWorld() || p->GetMapId() != 0 || p->IsBeingTeleported())
            return;

        TickSiegeTank(p);

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
        auto tk = s_tanks.find(player->GetGUID().GetCounter());
        if (tk != s_tanks.end())
        {
            ReleaseTank(player, tk->second);
            s_tanks.erase(tk);
        }

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
        // seulement dans le Val de Rend (l'exigence de sous-zone du sort est retirée côté serveur)
        if (player->GetMapId() != 0 || player->GetExactDist2d(-9600.0f, -3400.0f) > 400.0f)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cffff2020Il faut poser la charge dans la Tour ou la Hutte à munitions rochenoire, au Val de Rend.|r");
            return;
        }

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

// ---------------------------------------------------------------- 26694 The Grand Magus Doane (combat retail)
// Tour d'Ilgalar : Doane (397) lance Boule de feu, appelle son serviteur, et à 20 % de vie il se téléporte
// en évoquant Darkblaze : « Grand magus Doane affronté » (sort 81791 Doane Credit) pour les joueurs présents.
enum GrandMagusDoane : uint32
{
    SPELL_DOANE_FIREBALL = 9053,
    SPELL_DOANE_MINION   = 3611,    // Serviteur de Doane
    SPELL_DOANE_CREDIT   = 81791,   // Doane Credit
    NPC_DOANE            = 397,
    QUEST_GRAND_MAGUS    = 26694,

    EVENT_DOANE_FIREBALL = 1,
    EVENT_DOANE_MINION,
    EVENT_DOANE_VANISH,
    EVENT_DOANE_GONE
};

struct npc_azimov_grand_magus_doane : public ScriptedAI
{
    npc_azimov_grand_magus_doane(Creature* creature) : ScriptedAI(creature), _escaped(false) { }

    void Reset() override
    {
        _events.Reset();
        _escaped = false;
        me->SetReactState(REACT_AGGRESSIVE);
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->Say("Le chemin de la damnation vous attend...", LANG_UNIVERSAL);
        _events.ScheduleEvent(EVENT_DOANE_FIREBALL, 2s);
        _events.ScheduleEvent(EVENT_DOANE_MINION, 6s);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (_escaped)
        {
            damage = 0;
            return;
        }
        // il ne meurt pas : sous 20 % il s'échappe
        if (damage >= me->GetHealth() || me->HealthBelowPctDamaged(20, damage))
        {
            damage = std::min<uint32>(damage, me->GetHealth() > 1 ? me->GetHealth() - 1 : 0);
            Escape();
        }
    }

    void Escape()
    {
        _escaped = true;
        _events.Reset();
        me->InterruptNonMeleeSpells(true);
        me->AttackStop();
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
        me->Say("Assez ! Vous croyez m'avoir vaincu ? Darkblaze se rit de vos efforts. Nous nous reverrons, mortels !", LANG_UNIVERSAL);
        _events.ScheduleEvent(EVENT_DOANE_VANISH, 3s);
    }

    void GiveCredit()
    {
        me->CastSpell(me, SPELL_DOANE_CREDIT, true);
        // au cas où le sort ne touche pas tout le monde : crédit à chaque joueur proche qui a la quête
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, 60.0f);
        for (Player* p : players)
            if (p->GetQuestStatus(QUEST_GRAND_MAGUS) == QUEST_STATUS_INCOMPLETE)
                p->KilledMonsterCredit(NPC_DOANE);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DOANE_FIREBALL:
                    if (Unit* victim = me->GetVictim())
                        DoCast(victim, SPELL_DOANE_FIREBALL);
                    _events.ScheduleEvent(EVENT_DOANE_FIREBALL, 4s, 6s);
                    break;
                case EVENT_DOANE_MINION:
                    me->Yell("SERVITEUR, à moi !", LANG_UNIVERSAL);
                    DoCastSelf(SPELL_DOANE_MINION, true);
                    _events.ScheduleEvent(EVENT_DOANE_MINION, 45s);
                    break;
                case EVENT_DOANE_VANISH:
                    GiveCredit();
                    me->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST_OMNI);
                    _events.ScheduleEvent(EVENT_DOANE_GONE, 1500ms);
                    break;
                case EVENT_DOANE_GONE:
                    me->DespawnOrUnsummon(0, 30s);   // disparaît (« téléporté »), revient 30 s plus tard
                    break;
            }
        }

        if (!_escaped && UpdateVictim())
            DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    bool _escaped;
};

// Char de siège : suit le joueur, fonce dans les envahisseurs rochenoirs proches (Charge fracassante 81828)
// et chaque orc qu'il tue compte pour la quête du joueur.
struct npc_azimov_bravo_siege_tank : public ScriptedAI
{
    npc_azimov_bravo_siege_tank(Creature* creature) : ScriptedAI(creature), _scan(1000), _charge(4000), _wave(3000) { }

    Player* Owner()
    {
        TempSummon* self = me->ToTempSummon();
        Unit* u = self ? self->GetSummoner() : nullptr;
        return u ? u->ToPlayer() : nullptr;
    }

    static bool IsInvader(Creature* c)
    {
        switch (c->GetEntry())
        {
            case 42937: case 43775: case 43786: case 48414: case 48432:
                return true;
            default:
                return false;
        }
    }

    void IsSummonedBy(Unit* summoner) override
    {
        if (Player* p = summoner ? summoner->ToPlayer() : nullptr)
        {
            me->SetFaction(p->GetFaction());
            me->SetReactState(REACT_AGGRESSIVE);
            me->SetSpeedRate(MOVE_RUN, 1.6f);
            me->GetMotionMaster()->MoveFollow(p, 4.0f, float(M_PI));
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (me->IsCharmed())
            return;   // possédé : le jeu crédite déjà le joueur qui le pilote
        Creature* c = victim ? victim->ToCreature() : nullptr;
        if (Player* p = Owner())
            if (c && IsInvader(c) && p->GetQuestStatus(26708) == QUEST_STATUS_INCOMPLETE)
                p->KilledMonsterCredit(43775);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        me->CombatStop(true);
        if (Player* p = Owner())
            me->GetMotionMaster()->MoveFollow(p, 4.0f, float(M_PI));
    }

    void UpdateAI(uint32 diff) override
    {
        Player* p = Owner();
        if (!p)
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (_scan <= diff)
        {
            _scan = 1000;
            if (!me->GetVictim())
            {
                Creature* target = nullptr;
                float best = 30.0f;
                for (uint32 entry : { 42937u, 43775u, 43786u, 48414u, 48432u })
                    if (Creature* c = me->FindNearestCreature(entry, best))
                    {
                        best = me->GetDistance(c);
                        target = c;
                    }
                if (target && target->IsAlive())
                    AttackStart(target);
            }
        }
        else
            _scan -= diff;

        if (!UpdateVictim())
            return;

        if (_charge <= diff)
        {
            _charge = 5000;
            if (me->GetVictim() && me->IsWithinDist(me->GetVictim(), 10.0f))
                DoCastSelf(81828, true);   // Charge fracassante : dégâts + projection autour du char
        }
        else
            _charge -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 _scan;
    uint32 _charge;
    uint32 _wave;
};

void AddSC_azimov_quetes_carmines()
{
    RegisterCreatureAI(npc_azimov_bravo_siege_tank);
    RegisterCreatureAI(npc_azimov_grand_magus_doane);
    RegisterSpellScript(spell_azimov_place_hydroglycerin);
    RegisterSpellScript(spell_azimov_ettin_control);
    RegisterCreatureAI(npc_azimov_controlled_ettin);
    new go_azimov_chain_lever();
    new azimov_carmines_world();
    new azimov_carmines_player();
}
