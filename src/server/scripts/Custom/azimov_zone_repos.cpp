/*
 * Azimov - Zones de repos posées en jeu
 *
 * PNJ invisible 990030 « Zone de repos Azimov » : tout joueur à moins de Azimov.ZoneRepos.Rayon mètres
 * (25 par défaut) est « au repos » comme dans une auberge (icône Zzz, expérience reposée, déconnexion
 * immédiate et repos complet hors ligne). On en pose autant qu'on veut : .npc add 990030
 *
 * Le repos d'auberge du cœur (REST_FLAG_IN_TAVERN) est recalculé chaque seconde depuis les déclencheurs
 * d'AreaTrigger.dbc : il ne peut pas servir ici. On utilise un drapeau à part, REST_FLAG_AZIMOV (ajouté au cœur).
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include <algorithm>
#include <list>
#include <vector>

namespace
{
    float Rayon()
    {
        return std::clamp(sConfigMgr->GetFloatDefault("Azimov.ZoneRepos.Rayon", 25.0f), 5.0f, 100.0f);
    }
}

struct npc_azimov_zone_repos : public ScriptedAI
{
    npc_azimov_zone_repos(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
    }

    void AttackStart(Unit* /*who*/) override { }
    void MoveInLineOfSight(Unit* /*who*/) override { }

    void UpdateAI(uint32 diff) override
    {
        if (_timer > diff)
        {
            _timer -= diff;
            return;
        }
        _timer = 1000;

        float rayon = Rayon();
        std::list<Player*> proches;
        Trinity::AnyPlayerInObjectRangeCheck check(me, rayon);
        Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(me, proches, check);
        Cell::VisitWorldObjects(me, searcher, rayon);

        std::vector<ObjectGuid> dedans;
        for (Player* player : proches)
        {
            if (!player->IsInWorld())
                continue;
            dedans.push_back(player->GetGUID());
            if (!player->HasRestFlag(REST_FLAG_AZIMOV))
                player->SetRestFlag(REST_FLAG_AZIMOV);
        }

        // sortis du rayon (sur la même carte) : fin du repos donné par cette zone
        for (ObjectGuid const& guid : _joueurs)
            if (std::find(dedans.begin(), dedans.end(), guid) == dedans.end())
                if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
                    if (!EncoreDansUneZone(player))
                        player->RemoveRestFlag(REST_FLAG_AZIMOV);

        _joueurs.swap(dedans);
    }

    // une autre zone de repos proche couvre peut-être encore le joueur
    bool EncoreDansUneZone(Player* player) const
    {
        std::list<Creature*> zones;
        player->GetCreatureListWithEntryInGrid(zones, me->GetEntry(), Rayon());
        for (Creature* zone : zones)
            if (zone != me && zone->IsAlive())
                return true;
        return false;
    }

    void JustDied(Unit* /*killer*/) override { Liberer(); }

    void Liberer()
    {
        for (ObjectGuid const& guid : _joueurs)
            if (Player* player = ObjectAccessor::GetPlayer(*me, guid))
                player->RemoveRestFlag(REST_FLAG_AZIMOV);
        _joueurs.clear();
    }

private:
    uint32 _timer = 1000;
    std::vector<ObjectGuid> _joueurs;
};

// Changement de carte ou connexion : le repos donné par une zone ne suit pas le joueur
class azimov_zone_repos_player : public PlayerScript
{
public:
    azimov_zone_repos_player() : PlayerScript("azimov_zone_repos_player") { }

    void OnMapChanged(Player* player) override
    {
        if (player->HasRestFlag(REST_FLAG_AZIMOV))
            player->RemoveRestFlag(REST_FLAG_AZIMOV);
    }
};

void AddSC_azimov_zone_repos()
{
    RegisterCreatureAI(npc_azimov_zone_repos);
    new azimov_zone_repos_player();
}
