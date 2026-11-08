/*
 * Système d'XP des héritages : bonus cumulables + une seule icône avec le total exact.
 *
 * Principe :
 *   - Les 4 sorts posés sur les objets (1000000 à 1000003) ne donnent plus d'XP
 *     eux-mêmes : ce sont des marqueurs cachés (aura DUMMY passive, sans icône).
 *     Leur ligne « Équipé : » reste visible dans l'infobulle de l'objet.
 *   - Un sort récapitulatif (1000004) est l'unique aura visible. Son montant est
 *     recalculé à partir des objets réellement équipés, à chaque fois qu'un
 *     marqueur apparaît ou disparaît (équiper, retirer, posture, forme, login).
 *   - Comme le total est calculé à partir des objets et non des auras, un doublon
 *     d'aura (posture/forme) ne peut plus fausser le bonus.
 *
 * XP appliquée :
 *   - kills : par le core, via SPELL_AURA_MOD_XP_PCT de l'aura récapitulative
 *   - quêtes, exploration, récolte… : par OnGiveXP, avec le même pourcentage
 *   => le bonus est exactement le même partout (somme simple des objets).
 */

#include "ScriptMgr.h"
#include "DBCStores.h"
#include "EventProcessor.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include <unordered_map>

namespace
{
    // Aura visible qui affiche le total (à créer côté client ET serveur)
    constexpr uint32 SPELL_XP_SUMMARY = 1000040;

    // Sort posé sur l'objet -> % d'XP donné par cet objet
    std::unordered_map<uint32, int32> const XpBonusBySpell =
    {
        { 1000000, 20 },
        { 1000001, 30 }, // Collier
        { 1000002, 40 },
        { 1000003, 50 },
    };

    // Somme des bonus de tous les objets équipés (chaque objet compte une fois)
    int32 ComputeTotalXpBonus(Player* player)
    {
        int32 total = 0;

        // Equipement (0-18) + emplacements de sacs (19-22) : le core applique
        // aussi les sorts "Equipe :" des sacs, ils doivent compter dans le total.
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || item->IsBroken())
                continue;

            for (ItemEffect const& effect : item->GetTemplate()->Effects)
            {
                if (effect.Trigger != ITEM_SPELLTRIGGER_ON_EQUIP)
                    continue;

                auto itr = XpBonusBySpell.find(effect.SpellID);
                if (itr != XpBonusBySpell.end())
                    total += itr->second;
            }
        }

        return total;
    }

    // Met l'aura récapitulative à jour (ou la retire si aucun bonus)
    void UpdateXpSummaryAura(Player* player)
    {
        if (!player || !player->IsInWorld())
            return;

        int32 total = ComputeTotalXpBonus(player);

        if (total <= 0)
        {
            player->RemoveAurasDueToSpell(SPELL_XP_SUMMARY);
            return;
        }

        Aura* aura = player->GetAura(SPELL_XP_SUMMARY);
        if (!aura)
            aura = player->AddAura(SPELL_XP_SUMMARY, player);

        if (aura)
            if (AuraEffect* effect = aura->GetEffect(EFFECT_0))
                effect->ChangeAmount(total);
    }

    // Recalcul différé au prochain tick : au moment où l'aura d'un objet est
    // retirée, l'objet est encore dans son emplacement, il faut attendre.
    class XpSummaryRecalcEvent : public BasicEvent
    {
    public:
        explicit XpSummaryRecalcEvent(Player* player) : _player(player) { }

        bool Execute(uint64 /*time*/, uint32 /*diff*/) override
        {
            UpdateXpSummaryAura(_player);
            return true;
        }

    private:
        Player* _player;
    };

    void ScheduleXpSummaryRecalc(Unit* unit)
    {
        if (Player* player = unit ? unit->ToPlayer() : nullptr)
            player->m_Events.AddEventAtOffset(new XpSummaryRecalcEvent(player), 0ms);
    }
}

// Marqueurs posés sur les objets (1000000 à 1000003)
class spell_custom_xp_item_marker : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        ScheduleXpSummaryRecalc(GetTarget());
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        ScheduleXpSummaryRecalc(GetTarget());
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_custom_xp_item_marker::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_custom_xp_item_marker::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class CustomXPSystem : public PlayerScript
{
public:
    CustomXPSystem() : PlayerScript("CustomXPSystem") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        UpdateXpSummaryAura(player);
    }

    // Les kills sont déjà gérés par le core via l'aura récapitulative.
    // Ici : tout ce qui n'a pas de victime (quêtes, exploration, récolte…).
    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
    {
        if (!player || amount == 0 || victim)
            return;

        Aura* aura = player->GetAura(SPELL_XP_SUMMARY);
        if (!aura)
            return;

        if (AuraEffect* effect = aura->GetEffect(EFFECT_0))
            if (effect->GetAmount() > 0)
                amount = uint32(uint64(amount) * (100 + effect->GetAmount()) / 100);
    }
};

void AddSC_CustomXPSystem()
{
    // Les 4 sorts d'objets deviennent des marqueurs cachés sans effet d'XP
    for (auto const& [spellId, pct] : XpBonusBySpell)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
        {
            TC_LOG_ERROR("server.loading", "[XP] sort %u introuvable", spellId);
            continue;
        }

        SpellInfo* fix = const_cast<SpellInfo*>(spellInfo);
        fix->Effects[EFFECT_0].Effect = SPELL_EFFECT_APPLY_AURA;
        fix->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
        fix->Effects[EFFECT_0].BasePoints = pct;
        fix->Effects[EFFECT_0].TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        fix->Attributes |= SPELL_ATTR0_PASSIVE; // pas d'icône, pas sauvegardé
    }

    // L'aura récapitulative : permanente, non annulable, garde son % à la mort,
    // et envoie son montant au client pour que l'infobulle affiche $w1
    if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_XP_SUMMARY))
    {
        SpellInfo* fix = const_cast<SpellInfo*>(spellInfo);
        fix->Effects[EFFECT_0].Effect = SPELL_EFFECT_APPLY_AURA;
        fix->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_XP_PCT;
        fix->Effects[EFFECT_0].BasePoints = 0;
        fix->Effects[EFFECT_0].TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        fix->DurationEntry = sSpellDurationStore.LookupEntry(21); // durée infinie
        fix->Attributes |= SPELL_ATTR0_NO_AURA_CANCEL;
        fix->AttributesEx3 |= SPELL_ATTR3_ALLOW_AURA_WHILE_DEAD;
        fix->AttributesEx8 |= SPELL_ATTR8_AURA_POINTS_ON_CLIENT;
    }
    else
        TC_LOG_ERROR("server.loading", "[XP] sort recapitulatif %u introuvable : cree-le cote serveur", SPELL_XP_SUMMARY);

    RegisterSpellScript(spell_custom_xp_item_marker);
    new CustomXPSystem();
}