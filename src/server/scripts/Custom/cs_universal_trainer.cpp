#include "ScriptMgr.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"
#include "Creature.h"
#include "CreatureAI.h"

struct universal_trainerAI : public CreatureAI
{
    universal_trainerAI(Creature* creature) : CreatureAI(creature) { }

    bool GossipHello(Player* player) override
    {
        if (!player)
            return false;

        player->PlayerTalkClass->ClearMenus();

        player->PlayerTalkClass->GetGossipMenu().AddMenuItem(0, GOSSIP_ICON_TRAINER, "Enseigne-moi tous mes sorts disponibles", 1, 1001, "", 0, false);
        player->PlayerTalkClass->GetGossipMenu().AddMenuItem(1, GOSSIP_ICON_TRAINER, "Je souhaite m'entraîner normalement", 1, 1002, "", 0, false);

        player->PlayerTalkClass->SendGossipMenu(player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool GossipSelect(Player* player, uint32 /*sender*/, uint32 action) override
    {
        if (!player)
            return false;

        player->PlayerTalkClass->ClearMenus();

        if (action == 1001 || action == 1002)
        {
            if (player->GetSession())
            {
                // ??? LIAISON RÉSEAU : ID officiel pour forcer l'affichage sur Cataclysm
                player->GetSession()->SendTrainerList(me, 46714);
            }
        }

        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    void UpdateAI(uint32 const /*diff*/) override { }
};

class universal_trainer : public CreatureScript
{
public:
    universal_trainer() : CreatureScript("universal_trainer") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new universal_trainerAI(creature);
    }
};

void AddSC_universal_trainer() { new universal_trainer(); }