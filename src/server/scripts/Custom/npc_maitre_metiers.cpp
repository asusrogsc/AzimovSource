/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include <string>

// ---------------------------------------------------------------------------
// PNJ custom 1001050 : Azimov <Maitre des metiers>
// ScriptName dans creature_template : npc_maitre_metiers
// Enseigne tous les metiers (principaux + secondaires) jusqu'au rang Illustre.
// L'ouverture des metiers non affiches par le grimoire se fait cote client avec l'addon AzimovMetiers (/metiers).
// Les accents sont ecrits en sequences UTF-8 (\xC3\xA9 = e accent aigu) pour
// que le texte s'affiche correctement en jeu quel que soit l'encodage du fichier.
// ---------------------------------------------------------------------------
namespace
{
    struct MetierInfo
    {
        char const* nom;
        uint16 competence;  // ID de la competence (SkillLine)
        uint32 rangs[7];    // Apprenti -> Illustre (verifie dans spell_ranks)
        uint32 sorts[2];    // sorts de metier normalement achetes au maitre (0 = aucun)
    };

    MetierInfo const Metiers[] =
    {
        { "Alchimie",                       171, {  2259,  3101,  3464, 11611, 28596, 51304, 80731 } , { 0, 0 } },
        { "Forge",                          164, {  2018,  3100,  3538,  9785, 29844, 51300, 76666 } , { 0, 0 } },
        { "Enchantement",                   333, {  7411,  7412,  7413, 13920, 28029, 51313, 74258 } , { 13262, 0 } },
        { "Ing\xC3\xA9" "nierie",           202, {  4036,  4037,  4038, 12656, 30350, 51306, 82774 } , { 0, 0 } },
        { "Herboristerie",                  182, {  2366,  2368,  3570, 11993, 28695, 50300, 74519 } , { 2383, 0 } },
        { "Calligraphie",                   773, { 45357, 45358, 45359, 45360, 45361, 45363, 86008 } , { 51005, 0 } },
        { "Joaillerie",                     755, { 25229, 25230, 28894, 28895, 28897, 51311, 73318 } , { 31252, 0 } },
        { "Travail du cuir",                165, {  2108,  3104,  3811, 10662, 32549, 51302, 81199 } , { 0, 0 } },
        { "Minage",                         186, {  2575,  2576,  3564, 10248, 29354, 50310, 74517 } , { 2580, 2656 } },
        { "D\xC3\xA9" "pe\xC3\xA7" "age",   393, {  8613,  8617,  8618, 10768, 32678, 50305, 74522 } , { 0, 0 } },
        { "Couture",                        197, {  3908,  3909,  3910, 12180, 26790, 51309, 75156 } , { 0, 0 } },
        { "Cuisine",                        185, {  2550,  3102,  3413, 18260, 33359, 51296, 88053 } , { 0, 0 } },
        { "Secourisme",                     129, {  3273,  3274,  7924, 10846, 27028, 45542, 74559 } , { 0, 0 } },
        { "P\xC3\xAA" "che",                356, {  7620,  7731,  7732, 18248, 33095, 51294, 88868 } , { 0, 0 } },
        { "Arch\xC3\xA9" "ologie",          794, { 78670, 88961, 89718, 89719, 89720, 89721, 89722 } , { 0, 0 } }
    };

    uint32 const NombreMetiers = sizeof(Metiers) / sizeof(Metiers[0]);
    uint32 const ACTION_APPRENDRE      = GOSSIP_ACTION_INFO_DEF;          // + index du metier
    uint32 const ACTION_TOUT_APPRENDRE = GOSSIP_ACTION_INFO_DEF + 100;

    uint32 const QUETE_PARLER_PNJ = 1000004; // quete custom : objectif "parler au Maitre des metiers"

    uint16 const RANG_MAX        = 7;       // Illustre
    uint16 const COMPETENCE_MAX  = 525;

    bool ConnaitMetier(Player const* player, MetierInfo const& metier)
    {
        return player->HasSpell(metier.rangs[6]) && player->HasSkill(metier.competence);
    }

    void ApprendreMetier(Player* player, MetierInfo const& metier)
    {
        for (uint32 sort : metier.rangs)
            if (!player->HasSpell(sort))
                player->LearnSpell(sort, false);

        // Sur ce fork, LearnSpell seul laisse la competence a 0 (donc metier inutilisable) :
        // on fixe la competence nous-memes, comme le fait Spell::EffectSkill.
        for (uint32 sort : metier.sorts)
            if (sort && !player->HasSpell(sort))
                player->LearnSpell(sort, false);

        uint16 valeur = player->GetPureSkillValue(metier.competence);
        if (valeur < 1)
            valeur = 1;

        player->SetSkill(metier.competence, RANG_MAX, valeur, COMPETENCE_MAX);
    }
}

struct npc_maitre_metiers : public ScriptedAI
{
    npc_maitre_metiers(Creature* creature) : ScriptedAI(creature) { }

    bool GossipHello(Player* player) override
    {
        // Objectif de la quete 1000004 : parler a ce PNJ (compteur plafonne a 1, aucun risque de double credit)
        if (player->GetQuestStatus(QUETE_PARLER_PNJ) == QUEST_STATUS_INCOMPLETE)
            player->KilledMonsterCredit(me->GetEntry());

        ClearGossipMenuFor(player);

        // Rattrapage : donne les sorts de metier manquants (Prospection, Mouture...)
        // aux joueurs qui ont appris leurs metiers avant cette mise a jour
        for (uint32 i = 0; i < NombreMetiers; ++i)
            if (player->HasSkill(Metiers[i].competence))
                for (uint32 sort : Metiers[i].sorts)
                    if (sort && !player->HasSpell(sort))
                        player->LearnSpell(sort, false);

        uint32 restants = 0;
        for (uint32 i = 0; i < NombreMetiers; ++i)
            if (!ConnaitMetier(player, Metiers[i]))
                ++restants;

        if (restants > 1)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "Apprendre tous les m\xC3\xA9" "tiers", GOSSIP_SENDER_MAIN, ACTION_TOUT_APPRENDRE);

        for (uint32 i = 0; i < NombreMetiers; ++i)
            if (!ConnaitMetier(player, Metiers[i]))
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, std::string("Apprendre : ") + Metiers[i].nom, GOSSIP_SENDER_MAIN, ACTION_APPRENDRE + i);

        if (!restants)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Vous connaissez d\xC3\xA9" "j\xC3\xA0" " tous les m\xC3\xA9" "tiers. Tapez /metiers pour les ouvrir.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 999);

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);

        if (action == ACTION_TOUT_APPRENDRE)
        {
            for (uint32 i = 0; i < NombreMetiers; ++i)
                ApprendreMetier(player, Metiers[i]);

            ChatHandler(player->GetSession()).SendSysMessage("Vous connaissez maintenant tous les m\xC3\xA9" "tiers.");
            return GossipHello(player);
        }

        if (action >= ACTION_APPRENDRE && action < ACTION_APPRENDRE + NombreMetiers)
        {
            MetierInfo const& metier = Metiers[action - ACTION_APPRENDRE];
            ApprendreMetier(player, metier);

            std::string const message = std::string("Vous avez appris : ") + metier.nom + ".";
            ChatHandler(player->GetSession()).SendSysMessage(message.c_str());

            // on rouvre le menu pour pouvoir choisir un autre metier
            return GossipHello(player);
        }

        CloseGossipMenuFor(player);
        return true;
    }
};

struct npc_maitre_classe_azimov : public ScriptedAI
{
    npc_maitre_classe_azimov(Creature* creature) : ScriptedAI(creature) { }

    bool GossipHello(Player* player) override
    {
        if (player->GetQuestStatus(1000004) == QUEST_STATUS_INCOMPLETE)
            player->KilledMonsterCredit(me->GetEntry());

        return false;   // laisse le serveur ouvrir la fenetre des sorts normalement
    }
};

void AddSC_npc_maitre_metiers()
{
    RegisterCreatureAI(npc_maitre_metiers);
	RegisterCreatureAI(npc_maitre_classe_azimov);
}