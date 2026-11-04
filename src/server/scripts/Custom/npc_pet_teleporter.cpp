#include "ScriptMgr.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "WorldSession.h"
#include "Unit.h"

// IA minimale : force le flag gossip à chaque apparition, car le système de
// summon "companion pet" écrase le npcflag défini en base à chaque invocation
class npc_pet_teleporter_ai : public CreatureAI
{
public:
    explicit npc_pet_teleporter_ai(Creature* creature) : CreatureAI(creature) { }

    void JustAppeared() override
    {
        me->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        // Pas de comportement de combat/mouvement nécessaire pour cette mascotte
    }
};

struct TeleportLocation {
    uint32 mapId;
    float x, y, z, o;
    std::string name;
};

const TeleportLocation locations[] = {
    { 0, -8949.71f, -132.49f, 5.50f, 0.0f, "Hurlevent (Alliance)" },
    { 1, 1559.04f, -4413.71f, 8.05f, 0.0f, "Orgrimmar (Horde)" },
    { 571, 5808.31f, 588.93f, 647.47f, 0.0f, "Dalaran (Norfendre)" },
    { 640, -11915.1f, -1213.9f, -1.9f, 0.0f, "Mont Hyjal (Cataclysm)" }
};

class npc_pet_teleporter : public CreatureScript
{
public:
    npc_pet_teleporter() : CreatureScript("npc_pet_teleporter") { }

    bool OnGossipHello(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return false;

        if (creature->GetOwnerGUID() != player->GetGUID())
        {
            player->GetSession()->SendNotification("Ce Lil' Deathwing ne vous appartient pas !");
            return true;
        }

        player->PlayerTalkClass->ClearMenus();

        for (size_t i = 0; i < sizeof(locations) / sizeof(TeleportLocation); ++i)
        {
            player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, GOSSIP_ICON_TAXI, locations[i].name, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + i, "", 0, false);
        }

        player->PlayerTalkClass->SendGossipMenu(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action)
    {
        if (!player)
            return false;

        uint32 index = action - GOSSIP_ACTION_INFO_DEF;
        player->PlayerTalkClass->ClearMenus();

        if (index < sizeof(locations) / sizeof(TeleportLocation))
        {
            player->TeleportTo(locations[index].mapId, locations[index].x, locations[index].y, locations[index].z, locations[index].o);
        }

        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    // Fournit notre IA custom (au lieu de nullptr) pour forcer le flag gossip à chaque apparition
    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_pet_teleporter_ai(creature);
    }
};

void AddSC_npc_pet_teleporter()
{
    new npc_pet_teleporter();
}