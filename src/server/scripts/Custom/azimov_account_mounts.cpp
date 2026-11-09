/*
 * Azimov - Montures liées au compte (comme sur retail)
 *
 * Toute monture apprise par un personnage est enregistrée dans `account_mounts`
 * (base characters). À chaque connexion, le personnage apprend toutes les montures
 * du compte, y compris les montures de classe et de faction.
 *
 * Une monture = sort actif du grimoire qui applique l'aura MOUNTED (78)
 * ou qui appartient à la compétence Montures (777).
 */

#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "StringFormat.h"
#include <string>

namespace
{
    // Sorts « monture » à ne pas partager (changement de forme racial)
    constexpr uint32 SPELL_RUNNING_WILD = 87840; // Course sauvage (worgen)

    bool IsAccountMountSpell(uint32 spellId)
    {
        if (spellId == SPELL_RUNNING_WILD)
            return false;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive())
            return false;

        if (info->HasAura(SPELL_AURA_MOUNTED))
            return true;

        return IsPartOfSkillLine(SKILL_MOUNTS, spellId);
    }

    void SaveAccountMount(uint32 accountId, uint32 spellId)
    {
        CharacterDatabase.PExecute("INSERT IGNORE INTO account_mounts (account, spell) VALUES (%u, %u)", accountId, spellId);
    }

    void SaveCharacterMounts(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        std::string values;
        for (auto const& [spellId, spell] : player->GetSpellMap())
        {
            if (spell.state == PLAYERSPELL_REMOVED || spell.disabled)
                continue;

            if (!IsAccountMountSpell(spellId))
                continue;

            if (!values.empty())
                values += ',';
            values += Trinity::StringFormat("(%u,%u)", accountId, spellId);
        }

        // Une seule requête pour toutes les montures du personnage
        if (!values.empty())
            CharacterDatabase.Execute(("INSERT IGNORE INTO account_mounts (account, spell) VALUES " + values).c_str());
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

        // 1) Les montures de ce personnage rejoignent la collection du compte
        SaveCharacterMounts(player);

        // 2) Le personnage apprend toute la collection du compte
        QueryResult result = CharacterDatabase.PQuery("SELECT spell FROM account_mounts WHERE account = %u", player->GetSession()->GetAccountId());
        if (!result)
            return;

        do
        {
            uint32 spellId = (*result)[0].GetUInt32();
            if (!player->HasSpell(spellId) && IsAccountMountSpell(spellId))
                player->LearnSpell(spellId, false);
        } while (result->NextRow());
    }

    // Sauvegarde automatique et déconnexion : enregistre les montures apprises entre-temps
    void OnSave(Player* player) override
    {
        if (player)
            SaveCharacterMounts(player);
    }

    // Une monture invoquée est forcément connue : on l'ajoute tout de suite à la collection
    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!player || !spell || !spell->GetSpellInfo())
            return;

        uint32 spellId = spell->GetSpellInfo()->Id;
        if (player->HasSpell(spellId) && IsAccountMountSpell(spellId))
            SaveAccountMount(player->GetSession()->GetAccountId(), spellId);
    }
};

void AddSC_azimov_account_mounts()
{
    new azimov_account_mounts();
}
