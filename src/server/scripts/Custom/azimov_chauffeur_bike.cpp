/*
 * Azimov - Bécane avec chauffeur (comme la monture héritage du retail)
 *
 * - Tous les personnages connaissent les deux bécanes dès leur première connexion
 *   (personnages existants compris) : utilisables dès le niveau 1, sans compétence de monte.
 *   1001023 : Mécabécane avec chauffeur (PNJ véhicule 1000099, chauffeur 1000101)
 *   1001024 : Bécane de mekgénieur avec chauffeur (PNJ véhicule 1000100, chauffeur 1000102)
 * - C'est une vraie monture (aura 78, vitesse +60 % par l'aura 32) qui crée un kit de véhicule
 *   (VehicleId copié de la Mécabécane / Bécane de mekgénieur). Le joueur reste le conducteur
 *   « technique » : il dirige et garde ses sorts.
 * - Le modèle de la moto (MotorcycleVehicleChauffeur.m2) a ses points d'attache échangés :
 *   le joueur apparaît dans le side-car et le chauffeur, assis à la place du passager, au guidon.
 * - Le chauffeur est installé ici plutôt que par vehicle_template_accessory : à la connexion,
 *   l'aura de monture est rechargée avant que le joueur soit dans le monde, et l'invocation
 *   d'un accessoire échouerait (ASSERT dans Vehicle::InstallAccessory).
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace
{
    struct ChauffeurBike
    {
        uint32 Spell;
        uint32 Chauffeur;
    };

    ChauffeurBike const ChauffeurBikes[] =
    {
        { 1001023, 1000101 },   // Mécabécane avec chauffeur (Horde) : Gazlowe
        { 1001024, 1000102 },   // Bécane de mekgénieur avec chauffeur (Alliance) : Mekkanivelle
    };

    void InstallChauffeur(Player* player)
    {
        if (!player || !player->IsInWorld() || !player->IsAlive())
            return;

        uint32 chauffeurEntry = 0;
        for (ChauffeurBike const& bike : ChauffeurBikes)
            if (player->HasAura(bike.Spell))
                chauffeurEntry = bike.Chauffeur;

        if (!chauffeurEntry)
            return;

        Vehicle* kit = player->GetVehicleKit();
        if (!kit)
            return;

        // déjà un chauffeur (ou une place prise) : rien à faire
        for (auto const& seat : kit->Seats)
            if (!seat.second.IsEmpty())
                return;

        TempSummon* chauffeur = player->SummonCreature(chauffeurEntry, *player, TEMPSUMMON_MANUAL_DESPAWN);
        if (!chauffeur)
            return;

        // accessoire : disparaît quand il quitte la moto (démonter, mourir, téléportation...)
        chauffeur->AddUnitTypeMask(UNIT_MASK_ACCESSORY);
        chauffeur->EnterVehicle(player, -1);
    }
}

// 1001023 / 1001024 : installe le chauffeur quand on monte
class spell_azimov_chauffeur_bike : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
            InstallChauffeur(player);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_azimov_chauffeur_bike::AfterApply, EFFECT_0, SPELL_AURA_MOUNTED, AURA_EFFECT_HANDLE_REAL);
    }
};

class azimov_chauffeur_bike_player : public PlayerScript
{
public:
    azimov_chauffeur_bike_player() : PlayerScript("azimov_chauffeur_bike_player") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        // tout le monde a les bécanes, y compris les nouveaux personnages
        for (ChauffeurBike const& bike : ChauffeurBikes)
            if (!player->HasSpell(bike.Spell))
                player->LearnSpell(bike.Spell, false);

        // connecté sur la moto : l'aura a été rechargée avant l'entrée dans le monde
        InstallChauffeur(player);
    }

    void OnMapChanged(Player* player) override
    {
        InstallChauffeur(player);
    }
};

void AddSC_azimov_chauffeur_bike()
{
    RegisterSpellScript(spell_azimov_chauffeur_bike);
    new azimov_chauffeur_bike_player();
}
