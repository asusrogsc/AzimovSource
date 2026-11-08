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

#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
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
    { BOSS_YORSAHJ_THE_UNSLEEPING,          DATA_YORSAHJ_THE_UNSLEEPING             },
    { BOSS_HAGARA_THE_STORMBINDER,          DATA_HAGARA_THE_STORMBINDER             },
    { BOSS_ULTRAXION,                       DATA_ULTRAXION                          },
    { NPC_THRALL_SUMMIT,                    DATA_THRALL_SUMMIT                      },
    { NPC_ALEXSTRASZA_SUMMIT,               DATA_ALEXSTRASZA_SUMMIT                 },
    { NPC_YSERA_SUMMIT,                     DATA_YSERA_SUMMIT                       },
    { NPC_KALECGOS_SUMMIT,                  DATA_KALECGOS_SUMMIT                    },
    { NPC_NOZDORMU_SUMMIT,                  DATA_NOZDORMU_SUMMIT                    },
    { BOSS_WARMASTER_BLACKHORN,             DATA_WARMASTER_BLACKHORN                },
    { NPC_THE_SKYFIRE_SHIP,                 DATA_THE_SKYFIRE                        },
    { NPC_GORIONA_DRAKE,                    DATA_GORIONA                            },
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
            _trashHome[spawnId] = creature->GetHomePosition();
            ++_packAlive[packId];
        }

        // Seuls les packs autour de Morchok comptent (comme sur retail) ; ceux de l'entrée
        // et des portails font partie du décor du siège.
        bool IsNearMorchok(Position const& pos) const
        {
            return pos.GetExactDist2d(_morchokHome) <= MORCHOK_TRASH_RANGE;
        }

        uint32 CountNearTrashAlive() const
        {
            uint32 count = 0;
            for (auto const& pair : _trashHome)
                if (_trashPackOf.count(pair.first) && IsNearMorchok(pair.second))
                    ++count;
            return count;
        }

        void OnMorchokTrashDied(ObjectGuid::LowType spawnId)
        {
            auto itr = _trashPackOf.find(spawnId);
            if (itr == _trashPackOf.end())
                return;

            uint32 packId = itr->second;
            _trashPackOf.erase(itr);

            Creature* morchok = GetCreature(DATA_MORCHOK);

            if (_packAlive[packId] && --_packAlive[packId] == 0 && IsNearMorchok(_packAnchors[packId]))
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

            if (!_morchokTrashCleared && CountNearTrashAlive() == 0)
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
                case DATA_HAGARA_INTRO_EVENT:
                    _hagaraIntroDone = data == DONE;
                    SaveToDB();
                    break;
                case DATA_ULTRAXION_TRASH:
                    _ultraxionTrashDone = data == DONE;
                    SaveToDB();
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            if (type == DATA_MORCHOK_TRASH)
                return _morchokTrashCleared ? 1 : 0;
            if (type == DATA_HAGARA_INTRO_EVENT)
                return _hagaraIntroDone ? DONE : NOT_STARTED;
            if (type == DATA_ULTRAXION_TRASH)
                return _ultraxionTrashDone ? DONE : NOT_STARTED;
            return 0;
        }

        void WriteSaveDataMore(std::ostringstream& data) override
        {
            data << uint32(_morchokTrashCleared ? 1 : 0) << ' ' << uint32(_hagaraIntroDone ? 1 : 0) << ' '
                 << uint32(_ultraxionTrashDone ? 1 : 0) << ' ';
        }

        void ReadSaveDataMore(std::istringstream& data) override
        {
            uint32 cleared = 0;
            data >> cleared;
            _morchokTrashCleared = cleared != 0;
            uint32 hagaraIntro = 0;
            data >> hagaraIntro;
            _hagaraIntroDone = hagaraIntro != 0;
            uint32 ultraxionTrash = 0;
            data >> ultraxionTrash;
            _ultraxionTrashDone = ultraxionTrash != 0;
        }

        // PNJ du temple blessés pendant le siège (couchés, aura de mort feinte) : debout après Morchok
        static bool IsWoundedTempleNpc(uint32 entry)
        {
            switch (entry)
            {
                case 27935: // Ferithos
                case 27938: // Trizormu
                case 27940: // Lethecus
                case 27943: // Dalormi
                case 27953: // Protecteur du Repos du ver
                case 57682: // Tyrygosa
                case 58036: // Demestrasz
                    return true;
                default:
                    return false;
            }
        }

        static void WakeTempleNpc(Creature* creature)
        {
            creature->RemoveAurasDueToSpell(SPELL_TEMPLE_NPC_FEIGN_DEATH);
            creature->SetStandState(UNIT_STAND_STATE_STAND);
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            // le Brûleciel n'arrive au sommet qu'après la mort d'Ultraxion (sinon Ultraxion le traverse)
            if (type == DATA_ULTRAXION && state == DONE)
                if (GameObject* ship = instance->GetGameObject(_skyfireDockedGUID))
                    SetSkyfireVisible(ship, true);

            if (type == DATA_MORCHOK && state == DONE)
                for (ObjectGuid const& guid : _woundedTempleNpcs)
                    if (Creature* npc = instance->GetCreature(guid))
                        WakeTempleNpc(npc);

            return true;
        }

        static void SetSkyfireVisible(GameObject* ship, bool visible)
        {
            ship->m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GM, visible ? SEC_PLAYER : SEC_GAMEMASTER);
            ship->UpdateObjectVisibility();
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            if (go->GetEntry() == GO_THE_SKYFIRE_DOCKED)
            {
                _skyfireDockedGUID = go->GetGUID();
                if (GetBossState(DATA_ULTRAXION) != DONE)
                    go->m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GM, SEC_GAMEMASTER);
            }
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            if (IsWoundedTempleNpc(creature->GetEntry()))
            {
                _woundedTempleNpcs.push_back(creature->GetGUID());
                if (GetBossState(DATA_MORCHOK) == DONE)
                    WakeTempleNpc(creature);
            }

            if (IsMorchokTrash(creature) && creature->IsAlive() && !_morchokTrashCleared)
                RegisterMorchokTrash(creature);

            switch (creature->GetEntry())
            {
                case BOSS_MORCHOK:
                    _morchokHome = creature->GetHomePosition();
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
        bool _hagaraIntroDone = false;
        ObjectGuid _skyfireDockedGUID;
        bool _ultraxionTrashDone = false;
        uint32 _morchokPacksCleared;
        std::unordered_map<ObjectGuid::LowType, uint32> _trashPackOf;
        std::unordered_map<ObjectGuid::LowType, Position> _trashHome;
        std::vector<ObjectGuid> _woundedTempleNpcs;
        static constexpr uint32 SPELL_TEMPLE_NPC_FEIGN_DEATH = 58951;
        std::vector<Position> _packAnchors;
        Position _morchokHome = { -1997.53f, -2408.27f, 70.18f, 0.0f };
        static constexpr float MORCHOK_TRASH_RANGE = 115.0f;
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