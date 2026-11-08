/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DEF_DRAGONSOUL_H
#define DEF_DRAGONSOUL_H

#include "Define.h"
#include "Creature.h"
#include "CreatureAIImpl.h"

namespace DragonSoul
{
constexpr char const* DataHeader = "DS";
#define DSScriptName "instance_dragon_soul"

uint32 const EncounterCount = 8;

enum DSDataTypes
{
    // Bosses
    DATA_MORCHOK                = 0,
    DATA_WARLORD_ZONOZZ         = 1,
    DATA_YORSAHJ_THE_UNSLEEPING = 2,
    DATA_HAGARA_THE_STORMBINDER = 3,
    DATA_ULTRAXION              = 4,
    DATA_WARMASTER_BLACKHORN    = 5,
    DATA_SPINE_OF_DEATHWING     = 6,
    DATA_MADNESS_OF_DEATHWING   = 7,

    // Additional Data
    DATA_DEATHWING_MADNESS_OF_DEATHWING,
    DATA_THRALL_MADNESS_OF_DEATHWING,
    DATA_YSERA_MADNESS_OF_DEATHWING,
    DATA_ALEXSTRASZA_MADNESS_OF_DEATHWING,
    DATA_NOZDORMU_MADNESS_OF_DEATHWING,
    DATA_KALECGOS_MADNESS_OF_DEATHWING,
    DATA_TAIL_TENTACLE_MADNESS_OF_DEATHWING,

    // Siege of Wyrmrest Temple
    DATA_MORCHOK_TRASH,         // SetData(DATA_MORCHOK_TRASH, spawnId) quand un trash meurt
    DATA_HAGARA_INTRO_EVENT,    // vagues de trash d'Hagara terminées (DONE)

    // Sommet du Repos du ver (Ultraxion)
    DATA_ULTRAXION_TRASH,       // Assaillants du Crépuscule tués (DONE)
    DATA_THRALL_SUMMIT,
    DATA_ALEXSTRASZA_SUMMIT,
    DATA_YSERA_SUMMIT,
    DATA_KALECGOS_SUMMIT,
    DATA_NOZDORMU_SUMMIT,
    DATA_DEATHWING_SUMMIT,

    // Brûleciel (Corne-Noire)
    DATA_THE_SKYFIRE,
    DATA_GORIONA,

    // Échine d'Aile de mort
    DATA_BACK_PLATE_1,
    DATA_BACK_PLATE_2,
    DATA_BACK_PLATE_3
};

enum DSCreatures
{
    // Bosses
    BOSS_MORCHOK                                = 55265,
    BOSS_WARLORD_ZONOZZ                         = 55308,
    BOSS_YORSAHJ_THE_UNSLEEPING                 = 55312,
    BOSS_HAGARA_THE_STORMBINDER                 = 55689,
    BOSS_ULTRAXION                              = 55294,
    BOSS_WARMASTER_BLACKHORN                    = 56427,
    BOSS_SPINE_OF_DEATHWING                     = 53879,
    BOSS_MADNESS_OF_DEATHWING                   = 56173,
    NPC_KOHCROM                                 = 57773,

    /*Morchok trash*/
    NPC_EARTHEN_DESTROYER                       = 57158,
    NPC_EARTHEN_SOLDIER                         = 57159,
    NPC_ANCIENT_WATER_LORD                      = 57160,
    NPC_TWILIGHT_SIEGE_CAPTAIN                  = 57280,

    /*Wyrmrest Summit*/
    NPC_ALEXSTRASZA_SUMMIT                      = 56630,
    NPC_KALECGOS_SUMMIT                         = 56664,
    NPC_YSERA_SUMMIT                            = 56665,
    NPC_NOZDORMU_SUMMIT                         = 56666,
    NPC_THRALL_SUMMIT                           = 56667,
    NPC_DEATHWING_SUMMIT                        = 55971, // survole le temple pendant le rituel

    /*The Skyfire*/
    NPC_THE_SKYFIRE_SHIP                        = 56598,
    NPC_GORIONA_DRAKE                           = 56781,

    /*Madness of Deathwing*/
    NPC_DEATHWING_MADNESS_OF_DEATHWING          = 57962,
    NPC_ARM_TENTACLE_1                          = 56167,
    NPC_ARM_TENTACLE_2                          = 56846,
    NPC_WING_TENTACLE                           = 56168,
    NPC_TAIL_TENTACLE                           = 56844,
    NPC_MUTATED_CORRUPTION                      = 56471,
    NPC_CRUSH_TARGET                            = 56581,
    NPC_PLATFORM                                = 56307,
    NPC_HEMORRHAGE_TARGET                       = 56359,
    NPC_ELEMENTIUM_BOLT                         = 56262,
    NPC_BLISTERING_TENTACLE                     = 56188,
    NPC_TIME_ZONE_TARGET                        = 56332,
    NPC_TIME_ZONE                               = 56311,
    NPC_COSMETIC_TENTACLE                       = 57693,
    NPC_ELEMENTIUM_FRAGMENT                     = 56724,
    NPC_ELEMENTIUM_TERROR                       = 56710,
    NPC_JUMP_PAD                                = 56699,
    NPC_THRALL_MADNESS_OF_DEATHWING             = 56103,
    NPC_YSERA_MADNESS_OF_DEATHWING              = 56100,
    NPC_ALEXSTRASZA_MADNESS_OF_DEATHWING        = 56099,
    NPC_NOZDORMU_MADNESS_OF_DEATHWING           = 56102,
    NPC_KALECGOS_MADNESS_OF_DEATHWING           = 56101
};

enum DSGameObjectIds
{
    GO_THE_FOCUSING_IRIS                        = 210132,
    GO_THE_SKYFIRE_DOCKED                       = 210210, // le navire amarré au sommet
    GO_DEATHWING_BACK_PLATE_1                   = 209623,
    GO_DEATHWING_BACK_PLATE_2                   = 209631,
    GO_DEATHWING_BACK_PLATE_3                   = 209632,
    GO_GREATER_CACHE_10N                        = 209894,
    GO_GREATER_CACHE_25N                        = 209895,
    GO_GREATER_CACHE_10H                        = 209896,
    GO_GREATER_CACHE_25H                        = 209897,
    GO_ELEMENTIUM_FRAGMENT_10_NORMAL = 210079,
    GO_ELEMENTIUM_FRAGMENT_25_NORMAL = 210218,
    GO_ELEMENTIUM_FRAGMENT_25_LFR    = 210220,
    GO_ELEMENTIUM_FRAGMENT_10_HEROIC = 210219,
    GO_ELEMENTIUM_FRAGMENT_25_HEROIC = 210217
};

enum DSTexts
{
    // Morchok : répliques pendant le nettoyage du trash (groupes 20+ pour ne pas
    // entrer en conflit avec les textes du combat)
    SAY_MORCHOK_TRASH_PACK_1    = 20,
    SAY_MORCHOK_TRASH_PACK_2    = 21,
    SAY_MORCHOK_INTRO           = 22  // "No mortal shall turn me from my task."
};

enum DSSpells
{
    SPELL_CALM_MAELSTROM_SKYBOX = 109480
};

enum DSMapObjIds
{
    /*
    * Data Values:
    * 1, 0, 60,  0, 0, 0  -- Talk
    * 1, 0, 213, 0, 0, 0  -- Scream in Agony
    */
    MAP_OBJ_ID_SPINE_OF_DEATHWING_HEAD  = 6574436,
    /*
    * Data Values:
    * 1, 1, [(0 - 100)], 0, 0, 0 -- Skybox cloud speed
    */
    MAP_OBJ_ID_SKYFIRE_SKYBOX           = 6858573
};

template<class AI>
AI* GetDragonSoulAI(Creature* creature)
{
    return GetInstanceAI<AI>(creature, DSScriptName);
}

#define RegisterDragonSoulCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetDragonSoulAI)
}

#endif // DEF_DRAGONSOUL_H
