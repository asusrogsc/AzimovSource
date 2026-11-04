#include "ScriptMgr.h"
#include "Player.h"
#include "QuestDef.h"

namespace
{
    struct AzimovLevelQuest
    {
        uint32 QuestId;
        uint8  RequiredLevel;
    };

    // Quête -> niveau requis pour la valider
    AzimovLevelQuest const AzimovLevelQuests[] =
    {
        { 1000005, 10 },
        { 1000006, 40 },   // À VÉRIFIER : ID de la quête niveau 40
        { 1000007, 60 },   // À VÉRIFIER : ID de la quête niveau 60
        { 1000008, 85 },   // À VÉRIFIER : ID de la quête niveau 85
    };
}

class player_azimov_level_quests : public PlayerScript
{
public:
    player_azimov_level_quests() : PlayerScript("player_azimov_level_quests") { }

    static void TryCompleteAll(Player* player)
    {
        for (AzimovLevelQuest const& q : AzimovLevelQuests)
            TryComplete(player, q);
    }

    static void TryComplete(Player* player, AzimovLevelQuest const& q)
    {
        if (player->getLevel() >= q.RequiredLevel
            && player->GetQuestStatus(q.QuestId) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(q.QuestId);
    }

    // Le joueur passe un niveau
    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        TryCompleteAll(player);
    }

    // Le joueur accepte une de ces quêtes alors qu'il a déjà le niveau
    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        for (AzimovLevelQuest const& q : AzimovLevelQuests)
            if (q.QuestId == questId)
                TryComplete(player, q);
    }

    // Sécurité à la connexion (niveau donné hors ligne, .levelup, etc.)
    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        TryCompleteAll(player);
    }
};

void AddSC_quest_azimov_levels()
{
    new player_azimov_level_quests();
}