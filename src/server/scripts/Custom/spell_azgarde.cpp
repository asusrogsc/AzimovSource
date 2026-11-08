/*
 * Gardé par la Lumière (Azimov) - sort 1000112, version toutes classes
 *
 * Copie de 85646 (talent paladin) sans la restriction à Mot de gloire :
 * tout excédent de soin de vos sorts (soins directs ET soins sur la durée)
 * crée sur la cible soignée le bouclier 88063 "Gardé par la Lumière" (6 s).
 *
 * Les excédents s'additionnent au bouclier restant, plafonné à
 * SHIELD_CAP_PCT % de la vie max de la cible (sinon un petit tic de HoT
 * écraserait un gros bouclier).
 */

#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "Unit.h"

namespace AzGarde
{
    constexpr uint32 SPELL_SHIELD   = 88063; // Gardé par la Lumière (absorption, 6 s)
    constexpr int32  SHIELD_CAP_PCT = 50;    // plafond : % de la vie max de la cible
}

class spell_custom_garde_lumiere : public AuraScript
{
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        HealInfo* healInfo = eventInfo.GetHealInfo();
        if (!healInfo || !healInfo->GetTarget())
            return false;

        // pas de proc sur le bouclier lui-même ni sur des soins sans excédent
        if (healInfo->GetSpellInfo() && healInfo->GetSpellInfo()->Id == AzGarde::SPELL_SHIELD)
            return false;

        return healInfo->GetHeal() > healInfo->GetEffectiveHeal();
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        HealInfo* healInfo = eventInfo.GetHealInfo();
        Unit* target = healInfo->GetTarget();
        int32 overheal = int32(healInfo->GetHeal() - healInfo->GetEffectiveHeal());
        if (overheal <= 0 || !target->IsAlive())
            return;

        Unit* caster = GetTarget();
        int32 amount = overheal;

        // On additionne au bouclier encore actif posé par ce joueur
        if (AuraEffect const* shield = target->GetAuraEffect(AzGarde::SPELL_SHIELD, EFFECT_0, caster->GetGUID()))
            amount += shield->GetAmount();

        int32 cap = int32(target->CountPctFromMaxHealth(AzGarde::SHIELD_CAP_PCT));
        if (amount > cap)
            amount = cap;

        caster->CastSpell(target, AzGarde::SPELL_SHIELD, CastSpellExtraArgs(aurEff).AddSpellBP0(amount));
    }

    void Register() override
    {
        DoCheckProc.Register(&spell_custom_garde_lumiere::CheckProc);
        OnEffectProc.Register(&spell_custom_garde_lumiere::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

void AddSC_custom_garde_lumiere()
{
    RegisterSpellScript(spell_custom_garde_lumiere);
}
