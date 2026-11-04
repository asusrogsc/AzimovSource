#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Unit.h"

class spell_custom_echo_elements : public SpellScriptLoader
{
public:
    spell_custom_echo_elements() : SpellScriptLoader("spell_custom_echo_elements") { }

    class spell_custom_echo_elements_SpellScript : public SpellScript
    {
    public:
        void HandleEcho(SpellEffIndex /*effIndex*/)
        {
            Unit* caster = GetCaster();
            Unit* target = GetHitUnit();
            if (!caster || !target || !target->IsAlive())
                return;

            if (GetSpell() && GetSpell()->IsTriggered())
                return;

            if (GetSpellInfo()->SpellFamilyName == SPELLFAMILY_GENERIC && GetSpellInfo()->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
                return;

            if (!roll_chance_i(15))
                return;

            uint32 originalSpellId = GetSpellInfo()->Id;

            // Écho Soin
            int32 heal = GetHitHeal();
            if (heal > 0)
            {
                int32 echoHeal = int32(heal * 0.50f);
                CastSpellExtraArgs args;
                args.AddSpellBP0(echoHeal);
                caster->CastSpell(target, originalSpellId, args);
                return;
            }

            // Écho Dégâts
            int32 damage = GetHitDamage();
            if (damage > 0)
            {
                int32 echoDamage = int32(damage * 0.50f);
                CastSpellExtraArgs args;
                args.AddSpellBP0(echoDamage);
                caster->CastSpell(target, originalSpellId, args);
                return;
            }
        }

        void Register() override
        {
            OnEffectHitTarget.Register<spell_custom_echo_elements_SpellScript>(
                &spell_custom_echo_elements_SpellScript::HandleEcho, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE
            );
            OnEffectHitTarget.Register<spell_custom_echo_elements_SpellScript>(
                &spell_custom_echo_elements_SpellScript::HandleEcho, EFFECT_0, SPELL_EFFECT_HEAL
            );
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_echo_elements_SpellScript();
    }
};