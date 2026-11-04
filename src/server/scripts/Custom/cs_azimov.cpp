#include "ScriptMgr.h"
#include "Chat.h"
#include "Player.h"

class azimov_commandscript : public CommandScript
{
public:
    azimov_commandscript() : CommandScript("azimov_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable =
        {
            { "azimov",  SEC_ADMINISTRATOR, false, &HandleAzimovCommand,  "" },
            { "azimov2", SEC_ADMINISTRATOR, false, &HandleAzimov2Command, "" }
        };

        return commandTable;
    }

    // Vérifications communes aux deux commandes
    static bool CanTeleport(ChatHandler* handler, Player* player)
    {
        if (player->IsInCombat() || !player->IsAlive())
        {
            handler->SendSysMessage("Vous ne pouvez pas utiliser cette commande en combat ou si vous êtes mort.");
            return false;
        }
        return true;
    }

    static bool HandleAzimovCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!CanTeleport(handler, player))
            return true;

        player->TeleportTo(1, -8506.349609f, 2020.459961f, 104.750000f, 3.514660f);
        handler->SendSysMessage("Téléportation Azimov vers Ahn'Qiraj réussie !");
        return true;
    }

    static bool HandleAzimov2Command(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!CanTeleport(handler, player))
            return true;

        // Map 1400 - Vallée d'Ombrelune (Draenor)
        player->TeleportTo(1400, -1770.511353f, -1800.242676f, 17.774578f, 6.275275f);
        handler->SendSysMessage("Téléportation Azimov vers la Vallée d'Ombrelune réussie !");
        return true;
    }
};

void AddSC_azimov_commandscript()
{
    new azimov_commandscript();
}