/*
 * Azimov - créatures au niveau de chaque joueur. Voir AzimovLevelScaling.h
 */

#include "AzimovLevelScaling.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Timer.h"
#include "UpdateFields.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    struct ScalingInfo
    {
        uint8 MinLevel = 1;
        uint8 MaxLevel = 85;
    };

    // Rempli au démarrage puis lu seulement : pas besoin de verrou
    std::unordered_map<uint32, ScalingInfo> s_scaling;

    // Extension des statistiques de base selon le niveau (comme le jeu d'origine)
    uint8 ExpansionForLevel(uint8 level)
    {
        if (level <= 60) return 0;
        if (level <= 70) return 1;
        if (level <= 80) return 2;
        return 3;
    }

    ScalingInfo const* GetInfo(Creature const* creature)
    {
        if (!creature)
            return nullptr;
        auto itr = s_scaling.find(creature->GetEntry());
        return itr != s_scaling.end() ? &itr->second : nullptr;
    }

    CreatureBaseStats const* BaseStatsFor(Creature const* creature, uint8 level)
    {
        return CreatureBaseStats::GetBaseStats(level, creature->GetCreatureTemplate()->unit_class);
    }

    float SafeRatio(float view, float ref)
    {
        if (view <= 0.0f || ref <= 0.0f)
            return 1.0f;
        return view / ref;
    }
}

namespace AzimovLevelScaling
{
    void Load()
    {
        uint32 oldMSTime = getMSTime();
        s_scaling.clear();

        QueryResult result = WorldDatabase.Query("SELECT Entry, MinLevel, MaxLevel FROM creature_level_scaling");
        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Azimov : 0 créature à niveau variable (table creature_level_scaling vide ou absente)");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            ScalingInfo info;
            uint32 entry = fields[0].GetUInt32();
            info.MinLevel = std::max<uint8>(fields[1].GetUInt8(), 1);
            info.MaxLevel = std::max<uint8>(fields[2].GetUInt8(), info.MinLevel);
            s_scaling[entry] = info;
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Azimov : %u créature(s) à niveau variable chargée(s) en %u ms", uint32(s_scaling.size()), GetMSTimeDiffToNow(oldMSTime));
    }

    bool IsScaled(Creature const* creature)
    {
        return GetInfo(creature) != nullptr;
    }

    Player const* GetPlayerFor(WorldObject const* object)
    {
        if (!object)
            return nullptr;
        return object->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    uint8 ScaledLevelFor(Creature const* creature, WorldObject const* viewer)
    {
        ScalingInfo const* info = GetInfo(creature);
        if (!info)
            return 0;

        Player const* player = GetPlayerFor(viewer);
        if (!player)
            return 0;

        return std::min<uint8>(std::max<uint8>(player->getLevel(), info->MinLevel), info->MaxLevel);
    }

    float HealthRatio(Creature const* creature, uint8 viewLevel)
    {
        uint8 refLevel = creature->getLevel();
        CreatureBaseStats const* view = BaseStatsFor(creature, viewLevel);
        CreatureBaseStats const* ref = BaseStatsFor(creature, refLevel);
        if (!view || !ref)
            return 1.0f;
        return SafeRatio(float(view->BaseHealth[ExpansionForLevel(viewLevel)]),
                         float(ref->BaseHealth[creature->GetCreatureTemplate()->GetHealthScalingExpansion()]));
    }

    float DamageRatio(Creature const* creature, uint8 viewLevel)
    {
        uint8 refLevel = creature->getLevel();
        CreatureBaseStats const* view = BaseStatsFor(creature, viewLevel);
        CreatureBaseStats const* ref = BaseStatsFor(creature, refLevel);
        if (!view || !ref)
            return 1.0f;
        return SafeRatio(view->BaseDamage[ExpansionForLevel(viewLevel)],
                         ref->BaseDamage[creature->GetCreatureTemplate()->GetHealthScalingExpansion()]);
    }

    float ArmorRatio(Creature const* creature, uint8 viewLevel)
    {
        CreatureBaseStats const* view = BaseStatsFor(creature, viewLevel);
        CreatureBaseStats const* ref = BaseStatsFor(creature, creature->getLevel());
        if (!view || !ref)
            return 1.0f;
        return SafeRatio(float(view->BaseArmor), float(ref->BaseArmor));
    }

    uint32 FieldValueForViewer(Creature const* creature, uint16 index, Player const* viewer, uint32 rawValue)
    {
        uint8 viewLevel = ScaledLevelFor(creature, viewer);
        if (!viewLevel || viewLevel == creature->getLevel())
            return rawValue;

        if (index == UNIT_FIELD_LEVEL)
            return viewLevel;

        uint64 maxHealth = creature->GetMaxHealth();
        if (!maxHealth)
            return rawValue;

        uint64 viewMax = std::max<uint64>(uint64(double(maxHealth) * HealthRatio(creature, viewLevel)), 1);
        if (index == UNIT_FIELD_MAXHEALTH)
            return uint32(std::min<uint64>(viewMax, 0xFFFFFFFF));

        // UNIT_FIELD_HEALTH : même pourcentage que la vraie barre
        uint64 health = rawValue;
        if (!health)
            return 0;
        uint64 viewHealth = std::max<uint64>(health * viewMax / maxHealth, 1);
        return uint32(std::min<uint64>(viewHealth, viewMax));
    }

    float ArmorFactor(Unit const* attacker, Unit const* victim)
    {
        if (!attacker || !victim)
            return 1.0f;
        Creature const* creature = victim->ToCreature();
        uint8 viewLevel = ScaledLevelFor(creature, attacker);
        if (!viewLevel)
            return 1.0f;
        return ArmorRatio(creature, viewLevel);
    }

    uint8 AttackerLevelForArmor(Unit const* attacker, Unit const* victim, uint8 defaultLevel)
    {
        if (!attacker)
            return defaultLevel;
        if (uint8 viewLevel = ScaledLevelFor(attacker->ToCreature(), victim))
            return viewLevel;
        return attacker->getLevel();
    }

    void ScaleDamageToCreature(Unit const* attacker, Unit const* victim, uint32& damage)
    {
        if (!damage || !attacker || !victim)
            return;
        Creature const* creature = victim->ToCreature();
        uint8 viewLevel = ScaledLevelFor(creature, attacker);
        if (!viewLevel || viewLevel == creature->getLevel())
            return;

        // Le joueur voit une barre de PV à son niveau : ses dégâts sont convertis dans la vraie barre
        float ratio = HealthRatio(creature, viewLevel);
        if (ratio <= 0.0f)
            return;
        double scaled = double(damage) / ratio;
        damage = uint32(std::min<double>(scaled, double(0xFFFFFFFF)));
    }

    void ScaleDamageFromCreature(Unit const* attacker, Unit const* victim, uint32& damage)
    {
        if (!damage || !attacker || !victim)
            return;
        Creature const* creature = attacker->ToCreature();
        uint8 viewLevel = ScaledLevelFor(creature, victim);
        if (!viewLevel || viewLevel == creature->getLevel())
            return;

        float ratio = DamageRatio(creature, viewLevel);
        damage = std::max<uint32>(uint32(double(damage) * ratio), 1);
    }
}
