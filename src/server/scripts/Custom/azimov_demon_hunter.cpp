/*
 * Azimov - Chasseur de démons (classe 10), étape 2 : kit Dévastation.
 * Les sorts (1002000-1002012) sont dans Spell.dbc / SpellEffect.dbc ; ce script les apprend
 * automatiquement selon le niveau (connexion + passage de niveau), comme les techniques de classe.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "SharedDefines.h"

namespace AzimovDemonHunter
{
struct ClassSpell { uint8 level; uint32 spell; };

ClassSpell const Spells[] =
{
    {  1, 1002000 }, // Morsure du démon
    {  3, 1002001 }, // Frappe du chaos
    {  5, 1002002 }, // Vol fel
    {  7, 1002004 }, // Lancer de glaive
    { 10, 1002005 }, // Danse des lames
    { 10, 1002007 }, // Plané
    { 12, 1002008 }, // Rayon accablant
    { 14, 1002010 }, // Retraite vengeresse
    { 18, 1002011 }, // Vue spectrale
    { 20, 1002012 }, // Métamorphose
};

void LearnClassSpells(Player* player)
{
    if (player->getClass() != CLASS_DEMON_HUNTER)
        return;

    for (ClassSpell const& entry : Spells)
        if (player->getLevel() >= entry.level && !player->HasSpell(entry.spell))
            player->LearnSpell(entry.spell, false);
}

class player_azimov_demon_hunter : public PlayerScript
{
public:
    player_azimov_demon_hunter() : PlayerScript("player_azimov_demon_hunter") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        LearnClassSpells(player);
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        LearnClassSpells(player);
    }
};
}

void AddSC_azimov_demon_hunter()
{
    new AzimovDemonHunter::player_azimov_demon_hunter();
}
