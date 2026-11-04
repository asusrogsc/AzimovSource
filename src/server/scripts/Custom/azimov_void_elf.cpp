/*
 * Azimov - Elfe du Vide (race 12) : traits raciaux
 *   1001000 Faille spatiale      (actif, téléportation vers l'avant)
 *   1001001 Étreinte entropique  (passif, déclenche 1001002 : +5 % dégâts et soins, 12 s)
 *   1001003 Connexion éthérée    (passif, +10 Enchantement)
 *   1001004 Froid de la nuit     (passif, résistance à l'Ombre)
 *   1001005 Trotteur du Vide     (monture, apprise au niveau 20)
 * Quête 1001001 « Faire ses preuves » : chaque autre quête rendue compte pour l'objectif
 *   (crédit sur l'entrée d'Umbric 1000093, 3 requis).
 * Les sorts sont dans Spell.dbc (client + serveur). Ce script les apprend aux elfes du Vide
 * à chaque connexion et limite Étreinte entropique à une fois toutes les 45 s.
 */

#include "ScriptMgr.h"
#include "GameTime.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include <array>

namespace
{
    constexpr uint8 RACE_AZIMOV_VOID_ELF = 12;

    constexpr std::array<uint32, 4> VoidElfRacials =
    {
        1001000, // Faille spatiale
        1001001, // Étreinte entropique
        1001003, // Connexion éthérée
        1001004, // Froid de la nuit
    };

    constexpr uint32 SPELL_VOID_ELF_MOUNT = 1001005;
    constexpr uint8 VOID_ELF_MOUNT_LEVEL = 20;

    constexpr uint32 QUEST_UMBRIC_INTRO  = 1001000;
    constexpr uint32 QUEST_UMBRIC_PROOF  = 1001001;
    constexpr uint32 NPC_UMBRIC_CREDIT   = 1000093;

    constexpr uint32 ENTROPIC_EMBRACE_COOLDOWN_MS = 45 * IN_MILLISECONDS;
}

class azimov_void_elf_racials : public PlayerScript
{
public:
    azimov_void_elf_racials() : PlayerScript("azimov_void_elf_racials") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || player->getRace() != RACE_AZIMOV_VOID_ELF)
            return;

        for (uint32 spellId : VoidElfRacials)
            if (!player->HasSpell(spellId))
                player->LearnSpell(spellId, false);

        LearnMountIfReady(player);
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (player && player->getRace() == RACE_AZIMOV_VOID_ELF)
            LearnMountIfReady(player);
    }

    // Toute quête rendue (hors chaîne d'Umbric) compte pour « Faire ses preuves »
    void OnQuestStatusChange(Player* player, uint32 questId) override
    {
        if (!player || questId == QUEST_UMBRIC_INTRO || questId == QUEST_UMBRIC_PROOF)
            return;

        if (player->GetQuestStatus(QUEST_UMBRIC_PROOF) != QUEST_STATUS_INCOMPLETE)
            return;

        if (player->GetQuestStatus(questId) != QUEST_STATUS_REWARDED)
            return;

        player->KilledMonsterCredit(NPC_UMBRIC_CREDIT);
    }

private:
    static void LearnMountIfReady(Player* player)
    {
        if (player->getLevel() >= VOID_ELF_MOUNT_LEVEL && !player->HasSpell(SPELL_VOID_ELF_MOUNT))
            player->LearnSpell(SPELL_VOID_ELF_MOUNT, false);
    }
};

// 1001001 - Étreinte entropique
class spell_azimov_entropic_embrace : public AuraScript
{
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        uint32 now = GameTime::GetGameTimeMS();
        if (_nextProc && int32(now - _nextProc) < 0)
            return false;

        _nextProc = now + ENTROPIC_EMBRACE_COOLDOWN_MS;
        return true;
    }

    void Register() override
    {
        DoCheckProc.Register(&spell_azimov_entropic_embrace::CheckProc);
    }

    uint32 _nextProc = 0;
};

void AddSC_azimov_void_elf()
{
    new azimov_void_elf_racials();
    RegisterSpellScript(spell_azimov_entropic_embrace);
}
