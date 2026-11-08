/*
 * Azimov - Orc mag'har (race 15) : traits raciaux
 *   1001010 Appel ancestral          (actif, 2 min : un buff aléatoire de 400 pendant 15 s)
 *       1001011 Férocité des Loups-de-givre     score de critique
 *       1001012 Zèle de la Lame ardente         score de hâte
 *       1001013 Robustesse de l'Orbite-Sanglante score de maîtrise
 *       1001014 Puissance des Rochenoire        Polyvalence (CustomStats)
 *   1001015 Cieux ouverts            (passif, +10 % vitesse en monture terrestre et volante)
 *   1001016 Sang sauvage             (passif, -10 % durée des poisons, maladies et malédictions)
 *   1001017 Vigueur sympathique      (passif, toutes les 2 s donne 1001018 aux familiers : +10 % vie)
 *   1001019 Loup redoutable mag'har  (monture, apprise au niveau 20, PNJ 1000097)
 * Les sorts sont dans Spell.dbc (client + serveur). Ce script les apprend aux mag'har
 * à chaque connexion et retire les raciaux orc hérités du modèle.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Pet.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Random.h"
#include <array>

namespace
{
    constexpr uint8 RACE_AZIMOV_MAGHAR_ORC = 15;

    constexpr std::array<uint32, 4> MagharRacials =
    {
        1001010, // Appel ancestral
        1001015, // Cieux ouverts
        1001016, // Sang sauvage
        1001017, // Vigueur sympathique
    };

    // Raciaux orc copiés depuis la race 2 (playercreateinfo_spell / skills)
    constexpr std::array<uint32, 7> OrcRacials =
    {
        20572, 33697, 33702, // Fureur sanguinaire (AP / SP / hybride)
        20573,               // Robustesse
        21563,               // Commandement
        20574,               // Spécialisation Hache
        54562,               // Commandement (familiers)
    };

    constexpr std::array<uint32, 4> AncestralCallBuffs =
    {
        1001011, // Férocité des Loups-de-givre
        1001012, // Zèle de la Lame ardente
        1001013, // Robustesse de l'Orbite-Sanglante
        1001014, // Puissance des Rochenoire
    };

    constexpr uint32 SPELL_MAGHAR_MOUNT = 1001019;
    constexpr uint8  MAGHAR_MOUNT_LEVEL = 20;

    constexpr int32  ANCESTRAL_CALL_VERSATILITY = 400;
    constexpr uint32 SPELL_SYMPATHETIC_VIGOR_PET = 1001018;
}

class azimov_maghar_racials : public PlayerScript
{
public:
    azimov_maghar_racials() : PlayerScript("azimov_maghar_racials") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || player->getRace() != RACE_AZIMOV_MAGHAR_ORC)
            return;

        for (uint32 spellId : OrcRacials)
            if (player->HasSpell(spellId))
                player->RemoveSpell(spellId, false, false);

        for (uint32 spellId : MagharRacials)
            if (!player->HasSpell(spellId))
                player->LearnSpell(spellId, false);

        LearnMountIfReady(player);
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (player && player->getRace() == RACE_AZIMOV_MAGHAR_ORC)
            LearnMountIfReady(player);
    }

private:
    static void LearnMountIfReady(Player* player)
    {
        if (player->getLevel() >= MAGHAR_MOUNT_LEVEL && !player->HasSpell(SPELL_MAGHAR_MOUNT))
            player->LearnSpell(SPELL_MAGHAR_MOUNT, false);
    }
};

// 1001010 - Appel ancestral
class spell_azimov_ancestral_call : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ AncestralCallBuffs[0], AncestralCallBuffs[1], AncestralCallBuffs[2], AncestralCallBuffs[3] });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        uint32 buff = AncestralCallBuffs[urand(0, AncestralCallBuffs.size() - 1)];
        caster->CastSpell(caster, buff, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_azimov_ancestral_call::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 1001014 - Puissance des Rochenoire : +400 Polyvalence (CustomStats)
class spell_azimov_rochenoire_might : public AuraScript
{
    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
        {
            player->ApplyCustomVersatilityRating(ANCESTRAL_CALL_VERSATILITY, true);
            _applied = true;
        }
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (!_applied)
            return;

        if (Player* player = GetTarget()->ToPlayer())
            player->ApplyCustomVersatilityRating(ANCESTRAL_CALL_VERSATILITY, false);
        _applied = false;
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_azimov_rochenoire_might::HandleApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_azimov_rochenoire_might::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }

    bool _applied = false;
};

// 1001017 - Vigueur sympathique : +10 % vie aux familiers et serviteurs contrôlés
class spell_azimov_sympathetic_vigor : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SYMPATHETIC_VIGOR_PET });
    }

    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* owner = GetTarget();
        if (!owner)
            return;

        auto buff = [owner](Unit* unit)
        {
            if (unit && unit->IsAlive() && unit->GetOwnerGUID() == owner->GetGUID()
                && !unit->HasAura(SPELL_SYMPATHETIC_VIGOR_PET, owner->GetGUID()))
                owner->AddAura(SPELL_SYMPATHETIC_VIGOR_PET, unit);
        };

        if (Player* player = owner->ToPlayer())
            buff(player->GetPet());

        for (Unit* controlled : owner->m_Controlled)
            buff(controlled);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_azimov_sympathetic_vigor::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

void AddSC_azimov_maghar()
{
    new azimov_maghar_racials();
    RegisterSpellScript(spell_azimov_ancestral_call);
    RegisterSpellScript(spell_azimov_rochenoire_might);
    RegisterSpellScript(spell_azimov_sympathetic_vigor);
}
