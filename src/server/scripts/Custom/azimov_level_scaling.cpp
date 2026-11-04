/*
 * Azimov - créatures au niveau de chaque joueur : chargement de la table et conversion des dégâts.
 * La logique est dans src/server/game/Custom/AzimovLevelScaling.cpp
 */

#include "ScriptMgr.h"
#include "AzimovLevelScaling.h"
#include "Unit.h"
#include <algorithm>

class azimov_level_scaling_world : public WorldScript
{
public:
    azimov_level_scaling_world() : WorldScript("azimov_level_scaling_world") { }

    void OnStartup() override
    {
        AzimovLevelScaling::Load();
    }
};

class azimov_level_scaling_unit : public UnitScript
{
public:
    azimov_level_scaling_unit() : UnitScript("azimov_level_scaling_unit") { }

    // Créature -> joueur : coups calibrés pour le niveau du joueur.
    // Appliqué avant l'affichage, donc le journal de combat montre les vrais chiffres.
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        AzimovLevelScaling::ScaleDamageFromCreature(attacker, target, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        if (damage <= 0)
            return;
        uint32 value = uint32(damage);
        AzimovLevelScaling::ScaleDamageFromCreature(attacker, target, value);
        damage = int32(std::min<uint32>(value, 0x7FFFFFFF));
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        AzimovLevelScaling::ScaleDamageFromCreature(attacker, target, damage);
    }

    // Joueur -> créature : converti au moment de retirer les PV.
    // Le joueur voit ses dégâts habituels, et sa barre (à son niveau) baisse d'autant.
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        AzimovLevelScaling::ScaleDamageToCreature(attacker, victim, damage);
    }
};

void AddSC_azimov_level_scaling()
{
    new azimov_level_scaling_world();
    new azimov_level_scaling_unit();
}
