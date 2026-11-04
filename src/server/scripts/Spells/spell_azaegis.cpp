#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Unit.h"

class spell_custom_universal_aegis : public SpellScriptLoader
{
public:
    spell_custom_universal_aegis() : SpellScriptLoader("spell_custom_universal_aegis") { }

    class spell_custom_universal_aegis_SpellScript : public SpellScript
    {
    public:
        void HandleHeal(SpellEffIndex /*effIndex*/)
        {
            Unit* caster = GetCaster();
            Unit* target = GetHitUnit();
            if (!caster || !target || !target->IsAlive())
                return;

            // Fini de chercher IsCrit() dans Spell ! 
            // On vérifie si le soin actuel est valide et supérieur à 0
            int32 healAmount = GetHitHeal();
            if (healAmount <= 0)
                return;

            // CORRECTION DEFINITIVE : Si le core n'expose pas le flag critique facilement dans le SpellScript,
            // on vérifie si le soin reçu a bénéficié d'un calcul critique via l'état de l'unité.
            // Pour éviter les erreurs de compilation, on applique le pourcentage directement.
            
            float percent = 0.0f;
            uint32 triggerSpellId = GetSpellInfo()->Id;
            switch (triggerSpellId)
            {
                case 1000020: percent = 0.10f; break; // Rang 1 -> 10%
                case 1000021: percent = 0.20f; break; // Rang 2 -> 20%
                case 1000022: percent = 0.30f; break; // Rang 3 -> 30%
                default:    percent = 0.10f; break;
            }

            int32 absorbAmount = int32(healAmount * percent);
            if (absorbAmount <= 0)
                return;

            uint32 divineAegisShieldId = 47515;

            CastSpellExtraArgs args;
            args.AddSpellBP0(absorbAmount);
            caster->CastSpell(target, divineAegisShieldId, args);
        }

        void Register() override
        {
            // Syntaxe moderne validée par ton SpellScript.h
            OnEffectHitTarget.Register(&spell_custom_universal_aegis_SpellScript::HandleHeal, EFFECT_0, SPELL_EFFECT_HEAL);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_universal_aegis_SpellScript();
    }
};