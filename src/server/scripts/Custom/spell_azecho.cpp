/*
 * Écho des éléments (Azimov)
 *
 * 15 % de chance qu'un sort de dégâts ou de soin "résonne" :
 * la cible reçoit une seconde fois 50 % des dégâts / soins réellement infligés.
 *
 * L'écho est appliqué directement (pas de recast) : pas de coût, pas de GCD,
 * pas de temps d'incantation, pas de re-scaling par la puissance des sorts,
 * et aucun risque d'écho en chaîne.
 *
 * Lier le script aux sorts voulus via spell_script_names
 * (ScriptName = 'spell_custom_echo_elements').
 */

#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "Unit.h"

namespace
{
    constexpr int32 ECHO_CHANCE  = 15;    // % de chance
    constexpr float ECHO_PERCENT = 0.50f; // part des dégâts / soins renvoyée
}

class spell_custom_echo_elements : public SpellScript
{
    // OnHit est appelé APRÈS le calcul des effets : GetHitDamage()/GetHitHeal()
    // contiennent les vraies valeurs (en OnEffectHitTarget elles valent encore 0).
    void HandleEcho()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !target->IsAlive())
            return;

        // Pas d'écho sur les sorts déclenchés (procs, tics, etc.)
        if (GetSpell()->IsTriggered())
            return;

        if (!roll_chance_i(ECHO_CHANCE))
            return;

        SpellInfo const* spellInfo = GetSpellInfo();

        if (int32 heal = GetHitHeal(); heal > 0)
        {
            uint32 echoHeal = uint32(heal * ECHO_PERCENT);
            if (!echoHeal)
                return;

            HealInfo healInfo(caster, target, echoHeal, spellInfo, spellInfo->GetSchoolMask());
            caster->HealBySpell(healInfo);
            return;
        }

        if (int32 damage = GetHitDamage(); damage > 0)
        {
            uint32 echoDamage = uint32(damage * ECHO_PERCENT);
            if (!echoDamage)
                return;

            SpellNonMeleeDamage damageInfo(caster, target, spellInfo->Id, spellInfo->GetSchoolMask());
            damageInfo.damage = echoDamage;
            caster->SendSpellNonMeleeDamageLog(&damageInfo);
            caster->DealSpellDamage(&damageInfo, false);
        }
    }

    void Register() override
    {
        OnHit.Register(&spell_custom_echo_elements::HandleEcho);
    }
};

void AddSC_custom_echo_elements()
{
    RegisterSpellScript(spell_custom_echo_elements);
}
