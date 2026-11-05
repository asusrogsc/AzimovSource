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

#include "InstanceScript.h"
#include "CreatureTextMgr.h"
#include "Player.h"
#include <sstream>
#include <unordered_map>
#include <vector>
#include "ScriptMgr.h"
#include "dragon_soul.h"

namespace DragonSoul
{
ObjectData const creatureData[] =
{
    { BOSS_MORCHOK,                         DATA_MORCHOK                            },
    { BOSS_WARLORD_ZONOZZ,                  DATA_WARLORD_ZONOZZ                     },
    { BOSS_MADNESS_OF_DEATHWING,            DATA_MADNESS_OF_DEATHWING               },
    { NPC_DEATHWING_MADNESS_OF_DEATHWING,   DATA_DEATHWING_MADNESS_OF_DEATHWING     },
    { NPC_THRALL_MADNESS_OF_DEATHWING,      DATA_THRALL_MADNESS_OF_DEATHWING        },
    { NPC_YSERA_MADNESS_OF_DEATHWING,       DATA_YSERA_MADNESS_OF_DEATHWING         },
    { NPC_ALEXSTRASZA_MADNESS_OF_DEATHWING, DATA_ALEXSTRASZA_MADNESS_OF_DEATHWING   },
    { NPC_NOZDORMU_MADNESS_OF_DEATHWING,    DATA_NOZDORMU_MADNESS_OF_DEATHWING      },
    { NPC_KALECGOS_MADNESS_OF_DEATHWING,    DATA_KALECGOS_MADNESS_OF_DEATHWING      },
    { NPC_TAIL_TENTACLE,                    DATA_TAIL_TENTACLE_MADNESS_OF_DEATHWING },
    { 0,                                    0                                       } // END
};

ObjectData const gameobjectData[] =
{
    { 0, 0 } // END
};

DoorData const doorData[] =
{
    { 0, 0, DOOR_TYPE_ROOM } // END
};

class instance_dragon_soul : public InstanceMapScript
{
public:
    instance_dragon_soul() : InstanceMapScript(DSScriptName, 967) { }

    struct instance_dragon_soul_InstanceMapScript : public InstanceScript
    {
        instance_dragon_soul_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadDoorData(doorData);
            LoadObjectData(creatureData, gameobjectData);
            _morchokTrashCleared = false;
            _morchokPacksCleared = 0;
            _morchokIntroDone = false;
        }

        static bool IsMorchokTrash(Creature const* creature)
        {
            switch (creature->GetEntry())
            {
                case NPC_EARTHEN_DESTROYER:
                case NPC_EARTHEN_SOLDIER:
                case NPC_ANCIENT_WATER_LORD:
                    // seulement les élémentaires posés en base, pas ceux des portails
                    return creature->GetSpawnId() != 0 && !creature->IsSummon();
                default:
                    return false;
            }
        }

        // Morchok est visible et frappe le temple, mais intouchable tant que le trash vit
        void SetMorchokLocked(Creature* morchok, bool locked)
        {
            morchok->SetImmuneToPC(locked);
            if (locked)
            {
                morchok->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                morchok->SetReactState(REACT_PASSIVE);
            }
            else
            {
                morchok->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                morchok->SetReactState(REACT_AGGRESSIVE);
            }
        }

        // Regroupe les élémentaires en packs selon leur position de spawn
        void RegisterMorchokTrash(Creature* creature)
        {
            ObjectGuid::LowType spawnId = creature->GetSpawnId();
            if (_trashPackOf.count(spawnId))
                return; // grille rechargée, déjà connu

            uint32 packId = uint32(_packAnchors.size());
            for (uint32 i = 0; i < _packAnchors.size(); ++i)
                if (creature->GetHomePosition().GetExactDist2d(_packAnchors[i]) <= 25.0f)
                {
                    packId = i;
                    break;
                }

            if (packId == _packAnchors.size())
            {
                _packAnchors.push_back(creature->GetHomePosition());
                _packAlive.push_back(0);
            }

            _trashPackOf[spawnId] = packId;
            ++_packAlive[packId];
        }

        void OnMorchokTrashDied(ObjectGuid::LowType spawnId)
        {
            auto itr = _trashPackOf.find(spawnId);
            if (itr == _trashPackOf.end())
                return;

            uint32 packId = itr->second;
            _trashPackOf.erase(itr);

            Creature* morchok = GetCreature(DATA_MORCHOK);

            if (_packAlive[packId] && --_packAlive[packId] == 0)
            {
                ++_morchokPacksCleared;
                if (morchok)
                {
                    if (_morchokPacksCleared == 1)
                        sCreatureTextMgr->SendChat(morchok, SAY_MORCHOK_TRASH_PACK_1, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
                    else if (_morchokPacksCleared == 2)
                        sCreatureTextMgr->SendChat(morchok, SAY_MORCHOK_TRASH_PACK_2, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
                }
            }

            if (_trashPackOf.empty())
            {
                _morchokTrashCleared = true;
                SaveToDB();
                if (morchok)
                    SetMorchokLocked(morchok, false);
            }
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_MORCHOK_TRASH:
                    OnMorchokTrashDied(ObjectGuid::LowType(data));
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            if (type == DATA_MORCHOK_TRASH)
                return _morchokTrashCleared ? 1 : 0;
            return 0;
        }

        void WriteSaveDataMore(std::ostringstream& data) override
        {
            data << uint32(_morchokTrashCleared ? 1 : 0) << ' ';
        }

        void ReadSaveDataMore(std::istringstream& data) override
        {
            uint32 cleared = 0;
            data >> cleared;
            _morchokTrashCleared = cleared != 0;
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            if (IsMorchokTrash(creature) && creature->IsAlive() && !_morchokTrashCleared)
                RegisterMorchokTrash(creature);

            switch (creature->GetEntry())
            {
                case BOSS_MORCHOK:
                    SetMorchokLocked(creature, !_morchokTrashCleared && GetBossState(DATA_MORCHOK) != DONE);
                    break;
                case NPC_YSERA_MADNESS_OF_DEATHWING:
                case NPC_ALEXSTRASZA_MADNESS_OF_DEATHWING:
                case NPC_NOZDORMU_MADNESS_OF_DEATHWING:
                case NPC_KALECGOS_MADNESS_OF_DEATHWING:
                    creature->setActive(true); // Ugly as fuck but the boss area is just too big...
                    break;
                default:
                    break;
            }
        }

        void OnPlayerEnter(Player* player) override
        {
            if (GetBossState(DATA_MADNESS_OF_DEATHWING) == DONE)
                player->CastSpell(player, SPELL_CALM_MAELSTROM_SKYBOX);

            // Morchok accueille le raid une fois, tant que le siège n'est pas brisé
            if (!_morchokIntroDone && !_morchokTrashCleared && GetBossState(DATA_MORCHOK) != DONE)
                if (Creature* morchok = GetCreature(DATA_MORCHOK))
                {
                    _morchokIntroDone = true;
                    sCreatureTextMgr->SendChat(morchok, SAY_MORCHOK_INTRO, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
                }
        }

    private:
        bool _morchokTrashCleared;
        bool _morchokIntroDone;
        uint32 _morchokPacksCleared;
        std::unordered_map<ObjectGuid::LowType, uint32> _trashPackOf;
        std::vector<Position> _packAnchors;
        std::vector<uint32> _packAlive;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_dragon_soul_InstanceMapScript(map);
    }
};
}

void AddSC_instance_dragon_soul()
{
    using namespace DragonSoul;
    new instance_dragon_soul();
}
