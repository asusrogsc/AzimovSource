/*
 * Azimov - Chasseur de démons (classe 10), étape 2 : kit Dévastation.
 * Les sorts (1002000-1002012) sont dans Spell.dbc / SpellEffect.dbc ; ce script les apprend
 * automatiquement selon le niveau (connexion + passage de niveau), comme les techniques de classe.
 * Règle : 2 effets maximum par sort custom (3 effets = plantage aléatoire du client au chargement).
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellScript.h"

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

// 1002005 - Danse des lames : +100 % d'esquive pendant 1 s (1002006)
// (lancé par script : le client 4.3.4 plante avec des sorts custom à 3 effets)
class spell_azimov_dh_blade_dance : public SpellScript
{
    void HandleAfterCast()
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(caster, 1002006, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_azimov_dh_blade_dance::HandleAfterCast);
    }
};

// 1002012 - Métamorphose : +20 % de hâte (1002013) pendant la même durée
class spell_azimov_dh_metamorphosis : public SpellScript
{
    void HandleAfterCast()
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(caster, 1002013, true);
    }

    void Register() override
    {
        AfterCast.Register(&spell_azimov_dh_metamorphosis::HandleAfterCast);
    }
};

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
    using namespace AzimovDemonHunter;
    new player_azimov_demon_hunter();
    RegisterSpellScript(spell_azimov_dh_blade_dance);
    RegisterSpellScript(spell_azimov_dh_metamorphosis);
}