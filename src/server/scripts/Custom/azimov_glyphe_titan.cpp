/*
 * azimov_glyphe_titan.cpp - Glyphe de poigne du titan (glyphe prime, toutes classes et races)
 *
 *   Objet 1200025 « Glyphe de poigne du titan » -> apprend 1001031 (effet 74, GlyphProperties 962)
 *   Glyphe inscrite -> aura passive 1001030 (ce script) :
 *     - Poigne du titan (armes à deux mains dans chaque main, pénalité Blizzard 49152)
 *     - Ambidextrie
 *     - Haches, Masses et Épées à deux mains
 *   Glyphe retirée -> tout est repris, sauf ce que le personnage possède par lui-même
 *   (talent Poigne du titan 46917, ambidextrie de classe, sorts d'armes 197 / 199 / 202).
 *   Les armes devenues interdites sont rangées dans les sacs (ou envoyées par courrier si sacs pleins).
 *
 * SQL (azworld2) : INSERT INTO spell_script_names (spell_id, ScriptName) VALUES (1001030, 'spell_azimov_glyphe_titan');
 */

#include "ScriptMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "WorldSession.h"
#include <array>

namespace
{
    constexpr uint32 SPELL_TITAN_GRIP_TALENT  = 46917;
    constexpr uint32 SPELL_TITAN_GRIP_PENALTY = 49152;

    struct WeaponSkill { uint32 skill; uint32 teachSpell; uint32 subclass; };

    constexpr std::array<WeaponSkill, 3> TwoHandSkills =
    { {
        { SKILL_2H_AXES,   197, ITEM_SUBCLASS_WEAPON_AXE2  },
        { SKILL_2H_MACES,  199, ITEM_SUBCLASS_WEAPON_MACE2 },
        { SKILL_2H_SWORDS, 202, ITEM_SUBCLASS_WEAPON_SWORD2 },
    } };

    // Le personnage sait-il manier deux armes sans la glyphe (sort passif « Ambidextrie » appris) ?
    bool HasOwnDualWield(Player* player)
    {
        for (auto const& kv : player->GetSpellMap())
        {
            if (kv.second.state == PLAYERSPELL_REMOVED || kv.second.disabled)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(kv.first);
            if (!info || !info->IsPassive())
                continue;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (info->Effects[i].Effect == SPELL_EFFECT_DUAL_WIELD)
                    return true;
        }
        return false;
    }

    // Range l'arme d'un emplacement dans les sacs (rien si les sacs sont pleins : elle reste équipée)
    void StoreEquipped(Player* player, uint8 slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            return;
        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(dest, item, true);
        }
    }
}

// 1001030 - Glyphe de poigne du titan (aura passive)
class spell_azimov_glyphe_titan : public AuraScript
{
    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        if (!player->CanTitanGrip())
            player->SetCanTitanGrip(true, SPELL_TITAN_GRIP_PENALTY);

        if (!player->CanDualWield())
            player->SetCanDualWield(true);

        uint16 value = uint16(player->GetMaxSkillValueForLevel());
        uint32 profMask = 0;
        for (WeaponSkill const& ws : TwoHandSkills)
        {
            if (!player->HasSkill(ws.skill))
                player->SetSkill(ws.skill, 0, value, value);
            profMask |= 1 << ws.subclass;
        }
        player->AddWeaponProficiency(profMask);
        player->SendProficiency(ITEM_CLASS_WEAPON, player->GetWeaponProficiency());

        player->CheckTitanGripPenalty();
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        // déconnexion : la glyphe sera réappliquée à la prochaine connexion, on ne touche à rien
        if (!player->IsInWorld() || player->GetSession()->PlayerLogout())
            return;

        if (!player->HasSpell(SPELL_TITAN_GRIP_TALENT))
        {
            player->RemoveAurasDueToSpell(SPELL_TITAN_GRIP_PENALTY);
            player->SetCanTitanGrip(false);
        }

        if (!HasOwnDualWield(player))
            player->SetCanDualWield(false);

        for (WeaponSkill const& ws : TwoHandSkills)
            if (!player->HasSpell(ws.teachSpell) && player->HasSkill(ws.skill))
                player->SetSkill(ws.skill, 0, 0, 0);

        // arme à deux mains en main droite dont la compétence vient d'être retirée
        if (Item* main = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
            if (main->GetSkill() && !player->HasSkill(main->GetSkill()))
                StoreEquipped(player, EQUIPMENT_SLOT_MAINHAND);

        if (Item* off = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            if (off->GetSkill() && !player->HasSkill(off->GetSkill()))
                StoreEquipped(player, EQUIPMENT_SLOT_OFFHAND);

        // main gauche : ambidextrie / Poigne du titan perdues (sacs pleins : envoi par courrier)
        player->AutoUnequipOffhandIfNeed();
        player->CheckTitanGripPenalty();
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_azimov_glyphe_titan::HandleApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_azimov_glyphe_titan::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_azimov_glyphe_titan()
{
    RegisterSpellScript(spell_azimov_glyphe_titan);
}
