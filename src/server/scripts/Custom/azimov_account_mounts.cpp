/*
 * Azimov - Montures et mascottes liées au compte (comme sur retail)
 *
 * Toute monture ou mascotte apprise par un personnage est enregistrée dans
 * `account_mounts` / `account_pets` (base characters). À chaque connexion, le
 * personnage apprend toute la collection du compte (montures de classe et de
 * faction comprises).
 *
 * Monture  = sort actif du grimoire avec l'aura MOUNTED (78) ou de la compétence Montures (777).
 * Mascotte = sort actif de la compétence Mascottes (778) ou qui invoque un mini-familier
 *            (effet SUMMON avec SummonProperties 41, utilisé aussi par les mascottes custom).
 */

#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <string>

namespace
{
    // Sorts « monture » à ne pas partager (changement de forme racial)
    constexpr uint32 SPELL_RUNNING_WILD = 87840; // Course sauvage (worgen)
    constexpr int32  SUMMON_PROPERTIES_MINIPET = 41;

    enum class CollectionType { None, Mount, Pet };

    bool IsAccountMountSpell(SpellInfo const* info)
    {
        if (info->Id == SPELL_RUNNING_WILD)
            return false;

        if (info->HasAura(SPELL_AURA_MOUNTED))
            return true;

        return IsPartOfSkillLine(SKILL_MOUNTS, info->Id);
    }

    bool IsAccountPetSpell(SpellInfo const* info)
    {
        if (IsPartOfSkillLine(SKILL_COMPANIONS, info->Id))
            return true;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (info->Effects[i].Effect == SPELL_EFFECT_SUMMON && info->Effects[i].MiscValueB == SUMMON_PROPERTIES_MINIPET)
                return true;

        return false;
    }

    CollectionType GetCollectionType(uint32 spellId)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive())
            return CollectionType::None;

        if (IsAccountMountSpell(info))
            return CollectionType::Mount;

        if (IsAccountPetSpell(info))
            return CollectionType::Pet;

        return CollectionType::None;
    }

    char const* TableFor(CollectionType type)
    {
        return type == CollectionType::Mount ? "account_mounts" : "account_pets";
    }

    void SaveCollectionSpell(uint32 accountId, uint32 spellId, CollectionType type)
    {
        CharacterDatabase.PExecute("INSERT IGNORE INTO %s (account, spell) VALUES (%u, %u)", TableFor(type), accountId, spellId);
    }

    void SaveCharacterCollection(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        std::string mounts, pets;
        for (auto const& [spellId, spell] : player->GetSpellMap())
        {
            if (spell.state == PLAYERSPELL_REMOVED || spell.disabled)
                continue;

            CollectionType type = GetCollectionType(spellId);
            if (type == CollectionType::None)
                continue;

            std::string& values = type == CollectionType::Mount ? mounts : pets;
            if (!values.empty())
                values += ',';
            values += Trinity::StringFormat("(%u,%u)", accountId, spellId);
        }

        // Une seule requête par table pour tout le personnage
        if (!mounts.empty())
            CharacterDatabase.Execute(("INSERT IGNORE INTO account_mounts (account, spell) VALUES " + mounts).c_str());
        if (!pets.empty())
            CharacterDatabase.Execute(("INSERT IGNORE INTO account_pets (account, spell) VALUES " + pets).c_str());
    }

    void LearnAccountCollection(Player* player, char const* table)
    {
        QueryResult result = CharacterDatabase.PQuery("SELECT spell FROM %s WHERE account = %u", table, player->GetSession()->GetAccountId());
        if (!result)
            return;

        do
        {
            uint32 spellId = (*result)[0].GetUInt32();
            if (!player->HasSpell(spellId) && GetCollectionType(spellId) != CollectionType::None)
                player->LearnSpell(spellId, false);
        } while (result->NextRow());
    }
}

class azimov_account_mounts : public PlayerScript
{
public:
    azimov_account_mounts() : PlayerScript("azimov_account_mounts") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        // 1) La collection de ce personnage rejoint celle du compte
        SaveCharacterCollection(player);

        // 2) Le personnage apprend toute la collection du compte
        LearnAccountCollection(player, "account_mounts");
        LearnAccountCollection(player, "account_pets");
    }

    // Sauvegarde automatique et déconnexion : enregistre ce qui a été appris entre-temps
    void OnSave(Player* player) override
    {
        if (player)
            SaveCharacterCollection(player);
    }

    // Une monture ou une mascotte invoquée est forcément connue : ajout immédiat à la collection
    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!player || !spell || !spell->GetSpellInfo())
            return;

        uint32 spellId = spell->GetSpellInfo()->Id;
        if (!player->HasSpell(spellId))
            return;

        CollectionType type = GetCollectionType(spellId);
        if (type != CollectionType::None)
            SaveCollectionSpell(player->GetSession()->GetAccountId(), spellId, type);
    }
};

void AddSC_azimov_account_mounts()
{
    new azimov_account_mounts();
}
