/*
 * Thunderfury custom à 3 rangs, selon l'épée équipée.
 *
 * Le spell 21992 (Thunderfury) contient, dans le client 4.3.4 :
 *   - un effet APPLY_AURA (résistance Nature -25) qui saute en chaîne sur 5 cibles
 *   - un effet SCHOOL_DAMAGE (300 Nature) sur la cible principale uniquement
 *   - un effet TRIGGER_SPELL (27648, ralentissement) sur la cible principale
 *
 * Ce script :
 *   - remplace les 300 dégâts de la cible principale par la valeur du rang
 *   - inflige les mêmes dégâts à chaque cible touchée par le saut en chaîne
 *   - limite le nombre de cibles selon le rang (5 / 6 / 7)
 *
 * Avec une arme qui n'est pas dans la table (ex. le vrai Thunderfury 19019),
 * le sort garde son comportement d'origine.
 */

#include "ScriptMgr.h"
#include "Item.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "Unit.h"

namespace
{
    constexpr uint32 SPELL_THUNDERFURY = 21992;
    constexpr uint32 THUNDERFURY_MAX_CHAIN_TARGETS = 7; // le plus haut rang

    struct ThunderfuryRank
    {
        uint32 ItemEntry;
        int32  Damage;      // dégâts par cible
        uint32 MaxTargets;  // cible principale incluse
    };

    // >>> Remplace 100001 / 100002 / 100003 par les entry de tes 3 épées <<<
    constexpr ThunderfuryRank ThunderfuryRanks[] =
    {
        { 1000032, 2500, 5 }, // Rang 1
        { 1000033, 3500, 6 }, // Rang 2
        { 1000034, 4000, 7 }, // Rang 3
    };

    ThunderfuryRank const* GetThunderfuryRank(Item const* item)
    {
        if (!item)
            return nullptr;

        for (ThunderfuryRank const& rank : ThunderfuryRanks)
            if (rank.ItemEntry == item->GetEntry())
                return &rank;

        return nullptr;
    }
}

class spell_custom_thunderfury : public SpellScript
{
    ThunderfuryRank const* _rank = nullptr;
    uint32 _secondaryHits = 0;

    bool Load() override
    {
        _rank = GetThunderfuryRank(GetCastItem());

        // DEBUG : visible dans la console du worldserver, à retirer une fois validé
        TC_LOG_INFO("scripts", "[Thunderfury] proc - item %u - rang %s",
            GetCastItem() ? GetCastItem()->GetEntry() : 0, _rank ? "trouve" : "AUCUN (entry absente de la table)");
        return true;
    }

    // Cible principale : on remplace les 300 dégâts d'origine
    void HandlePrimaryDamage(SpellEffIndex /*effIndex*/)
    {
        if (!_rank)
            return;

        TC_LOG_INFO("scripts", "[Thunderfury] cible principale : %i -> %i degats", GetHitDamage(), _rank->Damage);
        SetHitDamage(_rank->Damage);
    }

    // Cibles du saut en chaîne : elles ne reçoivent normalement que l'aura,
    // on leur inflige les dégâts du rang ici
    void HandleChainTarget(SpellEffIndex /*effIndex*/)
    {
        if (!_rank)
            return;

        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !target->IsAlive())
            return;

        // la cible principale est déjà gérée par HandlePrimaryDamage
        if (target == GetExplTargetUnit())
            return;

        // respecte le nombre de cibles du rang
        if (_secondaryHits + 1 >= _rank->MaxTargets)
            return;
        ++_secondaryHits;
        TC_LOG_INFO("scripts", "[Thunderfury] cible secondaire %u : %i degats", _secondaryHits, _rank->Damage);

        SpellNonMeleeDamage damageInfo(caster, target, GetSpellInfo()->Id, SPELL_SCHOOL_MASK_NATURE);
        caster->CalculateSpellDamageTaken(&damageInfo, _rank->Damage, GetSpellInfo());
        Unit::DealDamageMods(damageInfo.target, damageInfo.damage, &damageInfo.absorb);
        caster->SendSpellNonMeleeDamageLog(&damageInfo);
        caster->DealSpellDamage(&damageInfo, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_custom_thunderfury::HandlePrimaryDamage, EFFECT_ALL, SPELL_EFFECT_SCHOOL_DAMAGE);
        OnEffectHitTarget.Register(&spell_custom_thunderfury::HandleChainTarget, EFFECT_ALL, SPELL_EFFECT_APPLY_AURA);
    }
};

void AddSC_custom_thunderfury()
{
    // Le saut en chaîne d'origine est limité à 5 cibles par le DBC.
    // On monte la limite à 7 pour permettre le rang 3 ; le script réduit
    // ensuite le nombre de cibles qui prennent des dégâts selon le rang.
    if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_THUNDERFURY))
    {
        SpellInfo* fix = const_cast<SpellInfo*>(spellInfo);
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (fix->Effects[i].ChainTarget > 1)
                fix->Effects[i].ChainTarget = THUNDERFURY_MAX_CHAIN_TARGETS;
    }

    RegisterSpellScript(spell_custom_thunderfury);
    TC_LOG_INFO("server.loading", "[Thunderfury] script spell_custom_thunderfury charge");
}