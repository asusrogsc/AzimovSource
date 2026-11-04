/*
 * CustomStats - Polyvalence & Raffinage
 *
 * Deux statistiques "à la retail" pour TrinityCore Cataclysm 4.3.4 :
 *
 *   POLYVALENCE (score) :
 *     + X % dégâts infligés, soins prodigués et boucliers d'absorption
 *     - X * FacteurDegatsSubis % dégâts subis
 *
 *   RAFFINAGE (score) :
 *     + Y % vitesse de récolte (herboristerie, minage, dépeçage) et d'artisanat
 *     + Z % taux de récolte : à 100 % -> x2 garanti, à 150 % -> x2 garanti + 50 % de chance de x3, etc.
 *     + (optionnel) taux de production en artisanat, même principe
 *
 * TOUS les réglages sont dans worldserver.conf (section "CUSTOM STATS")
 * et se rechargent en jeu avec la commande  .reload config
 *
 * Stats d'objet utilisées (le client 4.3.4 sait déjà les afficher) :
 *   stat_type 12 (ancien "score de défense")         -> Polyvalence
 *   stat_type 44 (ancienne "pénétration d'armure")   -> Raffinage
 */

#ifndef TRINITY_CUSTOM_STATS_H
#define TRINITY_CUSTOM_STATS_H

#include "Define.h"

class GameObject;
class Player;
class SpellInfo;
class Unit;
struct Loot;

namespace CustomStats
{
    struct VersatilitySettings
    {
        bool  Enabled               = true;
        float RatingPerPercent      = 130.0f; // score nécessaire pour 1 % (0 = même courbe que la Hâte, qui varie avec le niveau)
        float DamageDoneFactor      = 1.0f;   // 1.0 = 1 % de Polyvalence -> +1 % dégâts
        float HealingDoneFactor     = 1.0f;   // idem pour les soins
        float AbsorbDoneFactor      = 1.0f;   // idem pour les boucliers d'absorption
        float DamageTakenFactor     = 0.5f;   // 0.5 = 1 % de Polyvalence -> -0.5 % dégâts subis (comme retail)
        float PvPFactor             = 1.0f;   // multiplicateur quand l'autre camp est un joueur (1.0 = pas de différence)
        float MaxDonePct            = 50.0f;  // plafond du bonus dégâts/soins en %
        float MaxTakenReductionPct  = 25.0f;  // plafond de la réduction de dégâts subis en %
        bool  PetsInherit           = true;   // familiers/totems/gardiens utilisent la Polyvalence de leur maître
    };

    struct RefiningSettings
    {
        bool   Enabled              = true;
        float  RatingPerPercent     = 50.0f;  // score nécessaire pour 1 % de Raffinage
        float  SpeedFactor          = 1.0f;   // 1 % Raffinage -> +1 % vitesse de récolte/craft
        float  MaxSpeedPct          = 100.0f; // plafond vitesse (100 % = temps d'incantation divisé par 2)
        uint32 MinCastTimeMs        = 250;    // temps d'incantation minimum en millisecondes
        bool   SpeedOnCrafting      = true;   // la vitesse s'applique aussi à l'artisanat
        float  YieldFactor          = 1.0f;   // 1 % Raffinage -> +1 % taux de récolte
        float  MaxYieldPct          = 200.0f; // plafond taux de récolte (200 % = x3 garanti)
        bool   Herbalism            = true;
        bool   Mining               = true;
        bool   Skinning             = true;
        float  CraftYieldFactor     = 0.0f;   // 0 = désactivé. 0.5 = 1 % Raffinage -> +0.5 % d'objets en plus en artisanat
        float  MaxCraftYieldPct     = 100.0f;
        bool   AnnounceBonus        = true;   // message "Récolte x2 !" dans le chat
    };

    struct Settings
    {
        VersatilitySettings Versatility;
        RefiningSettings    Refining;
    };

    enum class AmountKind : uint8
    {
        Damage,
        Healing,
        Absorb
    };

    enum class GatherSource : uint8
    {
        Node,     // herbe / minerai
        Skinning  // dépeçage
    };

    TC_GAME_API Settings const& Get();
    TC_GAME_API void LoadConfig();

    // ---- Polyvalence ----
    TC_GAME_API float GetVersatilityPct(Player const* player);
    // Bonus "infligé" : appelé dans les calculs de dégâts / soins / absorption
    TC_GAME_API float ApplyVersatilityDone(Unit const* doer, Unit const* target, float amount, AmountKind kind);
    // Réduction "subie"
    TC_GAME_API float ApplyVersatilityTaken(Unit const* victim, Unit const* attacker, float amount);

    // Les scripts de boucliers (Mot de pouvoir : Bouclier...) appellent parfois SpellHealingBonusDone :
    // ce verrou évite d'appliquer la Polyvalence deux fois sur un bouclier.
    TC_GAME_API void SetInAbsorbCalculation(bool value);
    TC_GAME_API bool IsInAbsorbCalculation();

    // ---- Raffinage ----
    TC_GAME_API float GetRefiningPct(Player const* player);
    TC_GAME_API int32 ModifyCastTime(Player const* player, SpellInfo const* spellInfo, int32 castTime);
    TC_GAME_API bool IsGatheringNode(GameObject const* go);
    TC_GAME_API void ApplyGatheringYield(Player const* player, Loot* loot, GatherSource source);
    TC_GAME_API uint32 RollCraftMultiplier(Player const* player, SpellInfo const* spellInfo);

    // Message pour l'addon client "CustomStats" (feuille de personnage)
    TC_GAME_API void SendStatsToClient(Player* player);
}

#endif
