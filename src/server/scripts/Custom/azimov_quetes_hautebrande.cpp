/*
 * azimov_quetes_hautebrande.cpp - Chaîne des Contreforts de Hautebrande (Horde), refaite à la main
 *
 * Blizzard joue ces quêtes en cinématiques (caméra + acteurs) dont la base n'a ni trajets ni textes,
 * et Kingslayer Orkus n'y est placé nulle part. Ce script :
 *   - fait apparaître un Orkus PERSONNEL (visible seulement par le joueur concerné) selon l'avancement :
 *       dans l'eau aux Ruines d'Austrivage (28345), sur la rive (28345 rendue -> 28348, 28375),
 *       sur l'île de la Purification (28375 / 28400) ;
 *   - joue des scènes parlées en français et valide les quêtes :
 *       28192 No One Here Gets Out Alive (Stillwater), 28345 *Gurgle* HELP! *Gurgle*,
 *       28348 Stormpike Rendezvous, 28375 The Road to Purgation (vol sur Kasha),
 *       28400 Heroes of the Horde! (combat contre Vanndar et Balinda, retour à Moulin-de-Tarren),
 *       28506 March of the Stormpike, 28600 Matters of Loyalty (Drek'Thar), 28616 Stormpike Apocalypse.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    enum HbQuests : uint32
    {
        Q_SLUDGE      = 28192,
        Q_GURGLE      = 28345,
        Q_RENDEZVOUS  = 28348,
        Q_PURGATION   = 28375,
        Q_HEROES      = 28400,
        Q_MARCH       = 28506,
        Q_LOYALTY     = 28600,
        Q_APOCALYPSE  = 28616
    };

    enum HbNpcs : uint32
    {
        NPC_STILLWATER    = 47781,
        NPC_ORKUS_SHORE   = 48218,
        NPC_ORKUS_ISLE    = 48470,
        NPC_ORKUS_HEROES  = 48503,
        NPC_KASHA         = 48467,
        NPC_VANNDAR       = 48403,
        NPC_BALINDA       = 48407,
        NPC_CROMUSH       = 48545,
        NPC_VOGGAH        = 13817,
        NPC_DREKTHAR      = 11946
    };

    Position const PosWater      = { -969.75f, -551.89f, -0.5f, 0.01f };
    Position const PosShore      = { -944.74f, -509.23f, 1.16f, 4.67f };
    Position const PosKashaShore = { -950.50f, -515.00f, 1.50f, 4.67f };
    Position const PosIslePlayer = { -1226.73f, 412.26f, 2.82f, 1.96f };
    Position const PosIsleOrkus  = { -1228.20f, 415.80f, 2.90f, 5.10f };
    Position const PosVanndar    = { -1232.40f, 426.20f, 3.00f, 5.10f };
    Position const PosBalinda    = { -1236.00f, 422.00f, 3.00f, 5.10f };
    Position const PosStillwater = { -500.72f, 90.80f, 56.40f, 4.64f };
    Position const PosCromush    = { -17.72f, -925.55f, 54.88f, 1.64f };
    Position const PosTarrenDrop = { -15.50f, -920.00f, 55.00f, 4.78f };
    Position const PosVoggah     = { 371.20f, -1004.14f, 109.34f, 1.59f };
    Position const PosDrekThar   = { 373.50f, -1000.50f, 109.40f, 4.70f };

    enum OrkusSpot : uint8 { SPOT_NONE, SPOT_WATER, SPOT_SHORE, SPOT_ISLE };

    enum Speaker : uint8 { SPK_NARRATOR, SPK_GIVER, SPK_ORKUS, SPK_KASHA, SPK_DREKTHAR, SPK_VANNDAR };

    enum Action : uint8
    {
        ACT_SAY,
        ACT_SUMMON_KASHA,
        ACT_TELE_ISLE,
        ACT_SPAWN_FIGHT,
        ACT_WAIT_FIGHT,
        ACT_SUMMON_DREKTHAR,
        ACT_TELE_TARREN,
        ACT_COMPLETE
    };

    struct Line
    {
        Action act;
        Speaker who;
        char const* text;
        uint32 waitMs;      // pause après cette ligne
    };

    struct Scene
    {
        uint32 quest;
        uint32 giver;       // PNJ « giver » qui parle (0 = aucun)
        std::vector<Line> lines;
    };

    std::vector<Scene> const& Scenes()
    {
        static std::vector<Scene> const s =
        {
            { Q_SLUDGE, NPC_STILLWATER, {
                { ACT_SAY, SPK_GIVER,    "Allez-y, jetez un œil. Et surtout, ne touchez à RIEN.", 5000 },
                { ACT_SAY, SPK_NARRATOR, "Vous observez les Champs Fangeux : des ouvriers aux yeux vides titubent entre les cuves de boue.", 6000 },
                { ACT_SAY, SPK_NARRATOR, "Un corps se redresse dans la fange... puis un autre. Des fioles de la Société royale d'apothicaires traînent près des cuves.", 6000 },
                { ACT_SAY, SPK_GIVER,    "Vous voyez ? Les champignons ! Je l'avais bien dit ! ...Comment ça, « des fioles » ?", 4000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 0 } } },

            { Q_RENDEZVOUS, NPC_ORKUS_SHORE, {
                { ACT_SAY, SPK_ORKUS, "Écoute bien, $N. Orkus a retrouvé ce vieux nécromancien, Helcular, pour savoir où se cachait l'Alliance.", 6000 },
                { ACT_SAY, SPK_ORKUS, "Il m'a demandé de chasser des worgens. Facile... sauf que j'avais avalé trois portions des côtes de chimérok de Dirge.", 6000 },
                { ACT_SAY, SPK_ORKUS, "Mes entrailles hurlaient. Les worgens m'ont surpris, Kasha a été blessée et je suis tombé à l'eau. Fin de l'histoire. Ou presque.", 7000 },
                { ACT_SAY, SPK_ORKUS, "Avant de couler, j'ai entendu les Foudrepiques : Vanndar et Balinda se retrouvent sur l'île de la Purification !", 6000 },
                { ACT_SAY, SPK_ORKUS, "C'est notre chance. Quand Kasha pourra voler de nouveau, nous irons leur rendre visite.", 3000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 0 } } },

            { Q_PURGATION, NPC_ORKUS_SHORE, {
                { ACT_SAY, SPK_ORKUS, "KASHA ! À moi !", 2500 },
                { ACT_SUMMON_KASHA, SPK_NARRATOR, nullptr, 2500 },
                { ACT_SAY, SPK_ORKUS, "Grimpe, $N, et accroche-toi. Kasha vole vite quand elle a soif de vengeance.", 4000 },
                { ACT_SAY, SPK_NARRATOR, "Kasha s'élance au-dessus de la baie et pique vers l'île de la Purification...", 3000 },
                { ACT_TELE_ISLE, SPK_NARRATOR, nullptr, 0 } } },

            { Q_HEROES, NPC_ORKUS_HEROES, {
                { ACT_SAY, SPK_ORKUS,   "Les voilà ! Le général Vanndar et sa capitaine. Leurs plans de bataille sont sur eux !", 3000 },
                { ACT_SPAWN_FIGHT, SPK_NARRATOR, nullptr, 1500 },
                { ACT_SAY, SPK_VANNDAR, "Le Roi-tueur Orkus ? On te croyait au fond de la baie ! Balinda, finissons-en.", 1000 },
                { ACT_WAIT_FIGHT, SPK_NARRATOR, nullptr, 2000 },
                { ACT_SAY, SPK_ORKUS,   "Les plans de bataille de l'Alliance ! Prends-les, $N. Le seigneur de guerre Cromush doit les voir.", 5000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 2000 },
                { ACT_SAY, SPK_ORKUS,   "Kasha va te déposer à Moulin-de-Tarren. Nous sommes des héros de la Horde, $N !", 4000 },
                { ACT_TELE_TARREN, SPK_NARRATOR, nullptr, 0 } } },

            { Q_MARCH, NPC_CROMUSH, {
                { ACT_SAY, SPK_GIVER,    "Regarde vers le nord, $N. Ces plans disent vrai.", 4000 },
                { ACT_SAY, SPK_NARRATOR, "Au loin, des colonnes de Foudrepiques descendent de la vallée d'Alterac, bannières au vent, vers les « quatre doigts » de Hautebrande.", 7000 },
                { ACT_SAY, SPK_NARRATOR, "Des béliers de guerre, des canons, des centaines de soldats... Une semaine de plus, et Moulin-de-Tarren aurait été balayé.", 6000 },
                { ACT_SAY, SPK_GIVER,    "Nous ne tiendrons pas seuls. Il nous faut l'aide des Loups-de-givre.", 3000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 0 } } },

            { Q_LOYALTY, NPC_VOGGAH, {
                { ACT_SAY, SPK_GIVER,    "Le chef arrive. Incline-toi devant Drek'Thar.", 3000 },
                { ACT_SUMMON_DREKTHAR, SPK_NARRATOR, nullptr, 2500 },
                { ACT_SAY, SPK_DREKTHAR, "Les esprits m'ont parlé de toi, $N. Tu as rendu à Orkus son honneur et volé les plans des Foudrepiques.", 6000 },
                { ACT_SAY, SPK_DREKTHAR, "Les Loups-de-givre n'oublient jamais leurs dettes. Nos guerriers marcheront aux côtés de Moulin-de-Tarren.", 6000 },
                { ACT_SAY, SPK_DREKTHAR, "Retourne auprès de Cromush. Dis-lui que le clan répond à l'appel.", 3000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 0 } } },

            { Q_APOCALYPSE, NPC_CROMUSH, {
                { ACT_SAY, SPK_GIVER,    "Les bombes nucléaires de poche gobelines sont en place. À toi l'honneur, $N.", 4000 },
                { ACT_SAY, SPK_NARRATOR, "Vous enfoncez le détonateur. Un silence... puis le ciel du nord s'embrase.", 4000 },
                { ACT_SAY, SPK_NARRATOR, "Quatre champignons de feu s'élèvent au-dessus des campements foudrepiques. La terre tremble jusqu'à Moulin-de-Tarren.", 6000 },
                { ACT_SAY, SPK_GIVER,    "POUR LA HORDE ! Et pour Orkus, notre frère tombé !", 3000 },
                { ACT_COMPLETE, SPK_NARRATOR, nullptr, 0 } } },
        };
        return s;
    }

    Scene const* FindScene(uint32 quest)
    {
        for (Scene const& sc : Scenes())
            if (sc.quest == quest)
                return &sc;
        return nullptr;
    }

    struct HbState
    {
        ObjectGuid orkus;
        uint32 orkusEntry = 0;
        uint8 orkusSpot = SPOT_NONE;
        ObjectGuid kasha, drekthar, vanndar, balinda;

        uint32 sceneQuest = 0;
        size_t step = 0;
        uint32 nextMs = 0;
        std::vector<uint32> played;     // scènes déjà jouées depuis la connexion
    };

    std::unordered_map<ObjectGuid::LowType, HbState> s_states;

    Creature* Get(Player* p, ObjectGuid const& g)
    {
        return g.IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*p, g);
    }

    void Unsummon(Player* p, ObjectGuid& g)
    {
        if (Creature* c = Get(p, g))
            c->DespawnOrUnsummon();
        g.Clear();
    }

    Creature* SummonPrivate(Player* p, uint32 entry, Position const& pos, bool passive)
    {
        Position at = pos;
        float z = p->GetMapHeight(at.GetPositionX(), at.GetPositionY(), at.GetPositionZ() + 5.0f);
        if (z > INVALID_HEIGHT && std::fabs(z - at.GetPositionZ()) < 8.0f)
            at.m_positionZ = z;
        TempSummon* c = p->SummonCreature(entry, at, TEMPSUMMON_MANUAL_DESPAWN, 0, 0, p->GetGUID());
        if (c && passive)
            c->SetReactState(REACT_PASSIVE);
        return c;
    }

    std::string Fill(Player* p, char const* text)
    {
        std::string s = text;
        size_t pos;
        while ((pos = s.find("$N")) != std::string::npos)
            s.replace(pos, 2, p->GetName());
        return s;
    }

    void GiveItem(Player* player, uint32 itemId, uint32 count)
    {
        ItemPosCountVec dest;
        if (count && player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
            if (Item* item = player->StoreNewItem(dest, itemId, true, GenerateItemRandomPropertyId(itemId)))
                player->SendNewItem(item, count, true, false);
    }

    void CompleteQuestFull(Player* player, uint32 questId)
    {
        Quest const* q = sObjectMgr->GetQuestTemplate(questId);
        if (!q || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            return;
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (q->RequiredItemId[i] && q->RequiredItemCount[i])
            {
                uint32 have = player->GetItemCount(q->RequiredItemId[i], true);
                if (have < q->RequiredItemCount[i])
                    GiveItem(player, q->RequiredItemId[i], q->RequiredItemCount[i] - have);
            }
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 t = q->RequiredNpcOrGo[i];
            if (!t)
                continue;
            for (uint32 n = player->GetReqKillOrCastCurrentCount(questId, t); n < q->RequiredNpcOrGoCount[i]; ++n)
            {
                if (t > 0)
                {
                    if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(uint32(t)))
                        player->KilledMonster(ct, ObjectGuid::Empty);
                }
                else
                    player->KillCreditGO(uint32(-t));
            }
        }
        if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(questId);
        if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            player->CompleteQuest(questId);
    }

    bool Rewarded(Player* p, uint32 q) { return p->GetQuestRewardStatus(q); }
    bool Active(Player* p, uint32 q)
    {
        QuestStatus st = p->GetQuestStatus(q);
        return st == QUEST_STATUS_INCOMPLETE || st == QUEST_STATUS_COMPLETE;
    }

    // ---------------------------------------------------------------- Orkus personnel
    void UpdateOrkus(Player* p, HbState& st)
    {
        uint32 entry = 0;
        uint8 spot = SPOT_NONE;
        Position const* pos = nullptr;

        if (p->GetExactDist2d(&PosShore) < 200.0f)
        {
            QuestStatus g = p->GetQuestStatus(Q_GURGLE);
            Quest const* qg = sObjectMgr->GetQuestTemplate(Q_GURGLE);
            bool canTake = qg && g == QUEST_STATUS_NONE && !Rewarded(p, Q_GURGLE) && p->CanTakeQuest(qg, false);
            if (canTake || g == QUEST_STATUS_INCOMPLETE)
            {
                entry = NPC_ORKUS_SHORE; spot = SPOT_WATER; pos = &PosWater;
            }
            else if ((g == QUEST_STATUS_COMPLETE || Rewarded(p, Q_GURGLE)) && !Rewarded(p, Q_PURGATION))
            {
                entry = NPC_ORKUS_SHORE; spot = SPOT_SHORE; pos = &PosShore;
            }
        }
        else if (p->GetExactDist2d(&PosIslePlayer) < 200.0f)
        {
            if (Active(p, Q_PURGATION))
            {
                entry = NPC_ORKUS_ISLE; spot = SPOT_ISLE; pos = &PosIsleOrkus;
            }
            else if (Rewarded(p, Q_PURGATION) && !Rewarded(p, Q_HEROES))
            {
                entry = NPC_ORKUS_HEROES; spot = SPOT_ISLE; pos = &PosIsleOrkus;
            }
        }

        Creature* orkus = Get(p, st.orkus);
        if (orkus && !orkus->IsAlive())
        {
            orkus->DespawnOrUnsummon();
            orkus = nullptr;
            st.orkus.Clear();
        }

        if (!entry)
        {
            if (orkus && !st.sceneQuest)
                Unsummon(p, st.orkus);
            st.orkusEntry = 0;
            st.orkusSpot = SPOT_NONE;
            return;
        }

        // sorti de l'eau : on le fait nager jusqu'à la rive au lieu de le faire réapparaître
        if (orkus && st.orkusEntry == entry && st.orkusSpot == SPOT_WATER && spot == SPOT_SHORE)
        {
            orkus->SetDisableGravity(false);
            orkus->GetMotionMaster()->MovePoint(1, PosShore.GetPositionX(), PosShore.GetPositionY(), PosShore.GetPositionZ(), false);
            orkus->SetHomePosition(PosShore);
            st.orkusSpot = SPOT_SHORE;
            return;
        }

        if (orkus && st.orkusEntry == entry && st.orkusSpot == spot)
            return;

        Unsummon(p, st.orkus);
        if (Creature* c = SummonPrivate(p, entry, *pos, true))
        {
            if (spot == SPOT_WATER)
            {
                c->SetDisableGravity(true);
                c->NearTeleportTo(PosWater);
            }
            st.orkus = c->GetGUID();
            st.orkusEntry = entry;
            st.orkusSpot = spot;
        }
    }

    // ---------------------------------------------------------------- scènes
    void Speak(Player* p, HbState& st, Scene const& sc, Speaker who, char const* text)
    {
        std::string msg = Fill(p, text);
        Creature* c = nullptr;
        switch (who)
        {
            case SPK_GIVER:    c = sc.giver ? p->FindNearestCreature(sc.giver, 60.0f) : nullptr; break;
            case SPK_ORKUS:    c = Get(p, st.orkus); break;
            case SPK_KASHA:    c = Get(p, st.kasha); break;
            case SPK_DREKTHAR: c = Get(p, st.drekthar); break;
            case SPK_VANNDAR:  c = Get(p, st.vanndar); break;
            default: break;
        }
        if (c)
            c->Say(msg, LANG_UNIVERSAL);
        else
            ChatHandler(p->GetSession()).PSendSysMessage("|cffffd100%s|r", msg.c_str());
    }

    void EndScene(HbState& st)
    {
        st.played.push_back(st.sceneQuest);
        st.sceneQuest = 0;
        st.step = 0;
    }

    void AbortScene(Player* p, HbState& st)
    {
        Unsummon(p, st.vanndar);
        Unsummon(p, st.balinda);
        Unsummon(p, st.kasha);
        Unsummon(p, st.drekthar);
        st.sceneQuest = 0;
        st.step = 0;        // pas ajoutée à « played » : elle se rejouera
    }

    bool FightOver(Player* p, HbState& st)
    {
        Creature* v = Get(p, st.vanndar);
        Creature* b = Get(p, st.balinda);
        return (!v || !v->IsAlive()) && (!b || !b->IsAlive());
    }

    void RunScene(Player* p, HbState& st, uint32 now)
    {
        Scene const* sc = FindScene(st.sceneQuest);
        if (!sc || st.step >= sc->lines.size())
        {
            EndScene(st);
            return;
        }
        if (!p->IsAlive())
        {
            AbortScene(p, st);
            return;
        }
        if (now < st.nextMs)
            return;

        Line const& l = sc->lines[st.step];
        switch (l.act)
        {
            case ACT_SAY:
                Speak(p, st, *sc, l.who, l.text);
                break;
            case ACT_SUMMON_KASHA:
                Unsummon(p, st.kasha);
                if (Creature* k = SummonPrivate(p, NPC_KASHA, PosKashaShore, true))
                    st.kasha = k->GetGUID();
                break;
            case ACT_TELE_ISLE:
                Unsummon(p, st.kasha);
                Unsummon(p, st.orkus);
                st.orkusEntry = 0;
                st.orkusSpot = SPOT_NONE;
                p->NearTeleportTo(PosIslePlayer);
                break;
            case ACT_SPAWN_FIGHT:
            {
                Unsummon(p, st.vanndar);
                Unsummon(p, st.balinda);
                Creature* v = SummonPrivate(p, NPC_VANNDAR, PosVanndar, false);
                Creature* b = SummonPrivate(p, NPC_BALINDA, PosBalinda, false);
                Creature* o = Get(p, st.orkus);
                if (v) { st.vanndar = v->GetGUID(); v->SetReactState(REACT_AGGRESSIVE); if (v->AI()) v->AI()->AttackStart(p); }
                if (b) { st.balinda = b->GetGUID(); b->SetReactState(REACT_AGGRESSIVE); if (b->AI()) b->AI()->AttackStart(p); }
                if (o && v)
                {
                    o->SetReactState(REACT_AGGRESSIVE);
                    if (o->AI())
                        o->AI()->AttackStart(v);
                }
                break;
            }
            case ACT_WAIT_FIGHT:
                if (!FightOver(p, st))
                {
                    st.nextMs = now + 1000;
                    return;             // on reste sur cette ligne
                }
                Unsummon(p, st.vanndar);
                Unsummon(p, st.balinda);
                if (Creature* o = Get(p, st.orkus))
                {
                    o->SetReactState(REACT_PASSIVE);
                    o->GetMotionMaster()->MoveTargetedHome();
                }
                break;
            case ACT_SUMMON_DREKTHAR:
                Unsummon(p, st.drekthar);
                if (Creature* d = SummonPrivate(p, NPC_DREKTHAR, PosDrekThar, true))
                {
                    d->SetFaction(35);
                    st.drekthar = d->GetGUID();
                }
                break;
            case ACT_TELE_TARREN:
                Unsummon(p, st.orkus);
                st.orkusEntry = 0;
                st.orkusSpot = SPOT_NONE;
                p->NearTeleportTo(PosTarrenDrop);
                break;
            case ACT_COMPLETE:
                CompleteQuestFull(p, st.sceneQuest);
                break;
        }

        ++st.step;
        st.nextMs = now + l.waitMs;
        if (st.step >= sc->lines.size())
        {
            Unsummon(p, st.drekthar);
            Unsummon(p, st.kasha);
            EndScene(st);
        }
    }

    bool Played(HbState const& st, uint32 q)
    {
        for (uint32 x : st.played)
            if (x == q)
                return true;
        return false;
    }

    void TryStart(Player* p, HbState& st, uint32 q, bool where, uint32 now)
    {
        if (st.sceneQuest || !where || Played(st, q) || p->GetQuestStatus(q) != QUEST_STATUS_INCOMPLETE)
            return;
        st.sceneQuest = q;
        st.step = 0;
        st.nextMs = now + 1500;
    }

    void Tick(Player* p)
    {
        if (!p->IsInWorld() || p->GetMapId() != 0 || p->IsBeingTeleported())
            return;

        bool proche = p->GetExactDist2d(&PosShore) < 300.0f || p->GetExactDist2d(&PosIslePlayer) < 300.0f
            || p->GetExactDist2d(&PosStillwater) < 200.0f || p->GetExactDist2d(&PosCromush) < 200.0f
            || p->GetExactDist2d(&PosVoggah) < 200.0f;

        auto it = s_states.find(p->GetGUID().GetCounter());
        if (!proche)
        {
            if (it != s_states.end())
            {
                AbortScene(p, it->second);
                Unsummon(p, it->second.orkus);
                s_states.erase(it);
            }
            return;
        }

        HbState& st = s_states[p->GetGUID().GetCounter()];
        uint32 now = GameTime::GetGameTimeMS();

        if (!st.sceneQuest)
            UpdateOrkus(p, st);

        // 28345 : nager jusqu'à Orkus le sort de l'eau
        if (st.orkusSpot == SPOT_WATER && p->GetQuestStatus(Q_GURGLE) == QUEST_STATUS_INCOMPLETE)
            if (Creature* o = Get(p, st.orkus))
                if (p->IsWithinDist(o, 8.0f))
                {
                    o->Say("*Glouglou*... Orkus... te doit... la vie ! Vite, à la rive !", LANG_UNIVERSAL);
                    CompleteQuestFull(p, Q_GURGLE);
                }

        // 28375 : arrivée sur l'île
        if (p->GetQuestStatus(Q_PURGATION) == QUEST_STATUS_INCOMPLETE && p->GetExactDist2d(&PosIslePlayer) < 80.0f)
            CompleteQuestFull(p, Q_PURGATION);

        Creature* orkus = Get(p, st.orkus);
        TryStart(p, st, Q_RENDEZVOUS, orkus && st.orkusSpot == SPOT_SHORE && p->IsWithinDist(orkus, 20.0f), now);
        TryStart(p, st, Q_PURGATION,  orkus && st.orkusSpot == SPOT_SHORE && p->IsWithinDist(orkus, 20.0f), now);
        TryStart(p, st, Q_HEROES,     orkus && st.orkusSpot == SPOT_ISLE && p->IsWithinDist(orkus, 30.0f), now);
        TryStart(p, st, Q_SLUDGE,     p->GetExactDist2d(&PosStillwater) < 25.0f, now);
        TryStart(p, st, Q_MARCH,      p->GetExactDist2d(&PosCromush) < 25.0f, now);
        TryStart(p, st, Q_APOCALYPSE, p->GetExactDist2d(&PosCromush) < 25.0f, now);
        TryStart(p, st, Q_LOYALTY,    p->GetExactDist2d(&PosVoggah) < 25.0f, now);

        if (st.sceneQuest)
            RunScene(p, st, now);
    }
}

class azimov_hautebrande_world : public WorldScript
{
public:
    azimov_hautebrande_world() : WorldScript("azimov_hautebrande_world"), _timer(0) { }

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

class azimov_hautebrande_player : public PlayerScript
{
public:
    azimov_hautebrande_player() : PlayerScript("azimov_hautebrande_player") { }

    void OnLogout(Player* player) override
    {
        auto it = s_states.find(player->GetGUID().GetCounter());
        if (it == s_states.end())
            return;
        AbortScene(player, it->second);
        Unsummon(player, it->second.orkus);
        s_states.erase(it);
    }
};

void AddSC_azimov_quetes_hautebrande()
{
    new azimov_hautebrande_world();
    new azimov_hautebrande_player();
}
