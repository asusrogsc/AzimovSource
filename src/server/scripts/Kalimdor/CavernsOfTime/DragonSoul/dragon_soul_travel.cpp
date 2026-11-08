/*
 * Azimov - Dragon Soul : déplacements dans le raid
 *
 * Valeera     -> Gueule de Go'rath (Zon'ozz)            après Morchok
 * Eiendormi   -> Gueule de Shu'ma (Yor'sahj)            après Morchok
 * Nethestrasz -> Sommet du Repos du ver (Thrall)        après Zon'ozz et Yor'sahj
 * Portail du sommet -> Œil de l'Éternité (Hagara), portail de retour dans l'Œil
 * Eiendormi / Valeera dans les Gueules, "Travel to Wyrmrest Base" au sommet -> retour au temple
 *
 * Positions d'arrivée reprises d'AshamaneCore. Sur retail les dragons font un vol ;
 * ici c'est une téléportation, le vol en véhicule viendra plus tard.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "InstanceScript.h"
#include "Player.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum TravelNpcs
{
    NPC_NETHESTRASZ                 = 57287,
    NPC_EIENDORMI                   = 57288,
    NPC_VALEERA                     = 57289,
    NPC_TRAVEL_TO_WYRMREST_TEMPLE   = 57328,
    NPC_TRAVEL_TO_WYRMREST_BASE     = 57882,
    NPC_TRAVEL_TO_EYE_OF_ETERNITY   = 57377, // portail au sommet, près de Thrall
    NPC_TRAVEL_TO_WYRMREST_SUMMIT   = 57379  // portail de retour dans l'Œil de l'Éternité
};

enum TravelDestinations
{
    DEST_TEMPLE = 1,
    DEST_YORSAHJ,
    DEST_ZONOZZ,
    DEST_SUMMIT,
    DEST_HAGARA
};

static Position const TravelPos[] =
{
    { 0.0f,          0.0f,          0.0f,    0.0f  }, // (inutilisé)
    { -1779.503662f, -2393.439941f,  45.61f, 3.20f  }, // DEST_TEMPLE  : rez-de-chaussée du temple
    { -1854.233154f, -3068.658691f, -178.34f, 0.46f }, // DEST_YORSAHJ : Gueule de Shu'ma
    { -1743.647827f, -1835.132568f, -220.51f, 4.53f }, // DEST_ZONOZZ  : Gueule de Go'rath
    { -1781.188477f, -2375.122559f, 341.35f, 4.43f  }, // DEST_SUMMIT  : sommet
    { 13629.356445f, 13612.099609f, 123.49f, 3.14f  }  // DEST_HAGARA  : Œil de l'Éternité
};

struct npc_ds_travel : public ScriptedAI
{
    npc_ds_travel(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    // les portails du sniff sont « non sélectionnables » : on les rend cliquables
    void Reset() override
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        me->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        me->SetReactState(REACT_PASSIVE);
    }

    void UpdateAI(uint32 /*diff*/) override { }

    bool GossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);

        bool morchokDone  = Done(DATA_MORCHOK);
        bool mawsDone     = Done(DATA_WARLORD_ZONOZZ) && Done(DATA_YORSAHJ_THE_UNSLEEPING);

        switch (me->GetEntry())
        {
            case NPC_EIENDORMI:
                if (InMaw())
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Retourner au temple du Repos du ver.", GOSSIP_SENDER_MAIN, DEST_TEMPLE);
                else if (morchokDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Emmenez-moi à la Gueule de Shu'ma.", GOSSIP_SENDER_MAIN, DEST_YORSAHJ);
                break;
            case NPC_VALEERA:
                if (InMaw())
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Retourner au temple du Repos du ver.", GOSSIP_SENDER_MAIN, DEST_TEMPLE);
                else if (morchokDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Emmenez-moi à la Gueule de Go'rath.", GOSSIP_SENDER_MAIN, DEST_ZONOZZ);
                break;
            case NPC_NETHESTRASZ:
                // comme sur retail : Nethestrasz dépose le raid au sommet, près de Thrall
                if (mawsDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Emmenez-moi au sommet du temple.", GOSSIP_SENDER_MAIN, DEST_SUMMIT);
                break;
            case NPC_TRAVEL_TO_EYE_OF_ETERNITY:
                if (mawsDone)
                    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Entrer dans l'Œil de l'Éternité.", GOSSIP_SENDER_MAIN, DEST_HAGARA);
                break;
            case NPC_TRAVEL_TO_WYRMREST_SUMMIT:
                AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Retourner au sommet du temple.", GOSSIP_SENDER_MAIN, DEST_SUMMIT);
                break;
            case NPC_TRAVEL_TO_WYRMREST_TEMPLE:
            case NPC_TRAVEL_TO_WYRMREST_BASE:
                AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Retourner au temple du Repos du ver.", GOSSIP_SENDER_MAIN, DEST_TEMPLE);
                break;
            default:
                break;
        }

        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 dest = GetGossipActionFor(player, gossipListId);
        CloseGossipMenuFor(player);

        if (dest >= DEST_TEMPLE && dest <= DEST_HAGARA && !player->IsInCombat())
        {
            Position const& pos = TravelPos[dest];
            player->NearTeleportTo(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation());
        }
        return true;
    }

private:
    // une seconde Eiendormi / Valeera attend dans chaque Gueule pour le retour
    bool InMaw() const { return me->GetPositionZ() < 0.0f; }
    bool Done(uint32 bossId) const { return _instance && _instance->GetBossState(bossId) == DONE; }

    InstanceScript* _instance;
};
}

void AddSC_dragon_soul_travel()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(npc_ds_travel);
}
