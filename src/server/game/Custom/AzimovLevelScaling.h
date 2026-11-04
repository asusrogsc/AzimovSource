/*
 * Azimov - créatures au niveau de chaque joueur (comme les rares "à votre niveau" de retail)
 *
 * Une créature listée dans `creature_level_scaling` (base world) :
 *  - est vue par chaque joueur à SON niveau (borné par MinLevel / MaxLevel) ;
 *  - garde une seule barre de vie, affichée à chaque joueur à l'échelle de son niveau
 *    (même pourcentage pour tout le monde) ;
 *  - convertit les dégâts : chaque joueur entame la barre comme s'il frappait un monstre
 *    de son niveau, et reçoit des coups calibrés pour son niveau ;
 *  - donne l'XP, les chances de toucher et l'armure correspondant au niveau du joueur.
 *
 * En interne, la créature garde le niveau de son creature_template (niveau de référence).
 */

#ifndef AZIMOV_LEVEL_SCALING_H
#define AZIMOV_LEVEL_SCALING_H

#include "Define.h"

class Creature;
class Player;
class Unit;
class WorldObject;

namespace AzimovLevelScaling
{
    // Chargement de la table creature_level_scaling (au démarrage)
    TC_GAME_API void Load();

    // La créature est-elle concernée ?
    TC_GAME_API bool IsScaled(Creature const* creature);

    // Joueur derrière une unité (lui-même, ou maître d'un familier / totem / véhicule)
    TC_GAME_API Player const* GetPlayerFor(WorldObject const* object);

    // Niveau sous lequel `viewer` voit la créature. 0 = pas de mise à l'échelle.
    TC_GAME_API uint8 ScaledLevelFor(Creature const* creature, WorldObject const* viewer);

    // Valeur à envoyer à un joueur pour UNIT_FIELD_LEVEL / HEALTH / MAXHEALTH
    TC_GAME_API uint32 FieldValueForViewer(Creature const* creature, uint16 index, Player const* viewer, uint32 rawValue);

    // Rapports (niveau vu / niveau de référence)
    TC_GAME_API float HealthRatio(Creature const* creature, uint8 viewLevel);
    TC_GAME_API float DamageRatio(Creature const* creature, uint8 viewLevel);
    TC_GAME_API float ArmorRatio(Creature const* creature, uint8 viewLevel);

    // Armure : facteur appliqué à l'armure de la victime, et niveau d'attaquant à utiliser
    TC_GAME_API float ArmorFactor(Unit const* attacker, Unit const* victim);
    TC_GAME_API uint8 AttackerLevelForArmor(Unit const* attacker, Unit const* victim, uint8 defaultLevel);

    // Conversion des dégâts
    TC_GAME_API void ScaleDamageToCreature(Unit const* attacker, Unit const* victim, uint32& damage);   // joueur -> créature
    TC_GAME_API void ScaleDamageFromCreature(Unit const* attacker, Unit const* victim, uint32& damage); // créature -> joueur
}

#endif
