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

// This is where scripts' loading functions should be declared:


// The name of this function should match:
// void Add${NameOfDirectory}Scripts()
void AddSC_CustomXPSystem();
void AddSC_azimov_commandscript();
void AddSC_universal_trainer();
void AddSC_npc_pet_teleporter();
void AddSC_custom_thunderfury();
void AddSC_npc_maitre_metiers();
void AddSC_compagnons();
void AddSC_quest_azimov_levels();
void AddSC_manastorm();
void AddSC_tri_commandscript();
void AddSC_custom_stats();
void AddSC_azimov_power();
void AddSC_azimov_level_scaling();
void AddSC_azimov_void_elf();
void AddSC_custom_echo_elements();
void AddSC_custom_garde_lumiere();
void AddSC_azimov_maghar();
void AddSC_azimov_account_mounts();

void AddCustomScripts()
{
    AddSC_CustomXPSystem();
    AddSC_azimov_commandscript();
    AddSC_universal_trainer();
    AddSC_npc_pet_teleporter();
	AddSC_custom_thunderfury();
	AddSC_npc_maitre_metiers();
	AddSC_compagnons();
	AddSC_quest_azimov_levels();
	AddSC_manastorm();
	AddSC_tri_commandscript();
	AddSC_custom_stats();
	AddSC_azimov_power();
	AddSC_azimov_level_scaling();
	AddSC_azimov_void_elf();
	AddSC_custom_echo_elements();
	AddSC_custom_garde_lumiere();
	AddSC_azimov_maghar();
	AddSC_azimov_account_mounts();
}
