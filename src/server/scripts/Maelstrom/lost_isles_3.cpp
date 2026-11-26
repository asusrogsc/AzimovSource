/*
 * Azimov - Îles Perdues, partie 3 (lot 25)
 *
 *  - 24671 « Cluster Cluck » : clic sur une Poule sauvage 38111 (71170 Feu d'artifice télécommandé)
 *    -> crédit 38117, feu d'artifice, la poule disparaît (réapparition 30 s).
 *  - 24925 « Free the Captives » : à l'approche d'un Captif gobelin 38643, un Chaman Oomlot 38644
 *    apparaît et attaque ; le tuer donne le crédit (objectif « tuer 38644 »), le captif s'enfuit.
 *  - 24942 « Zombies vs. Super Booster Rocket Boots » : objet 52013 (72891) -> aura 72885 (90 s)
 *    qui déclenche 72886 autour du joueur, filtré sur les Zombies gobelins 38753 (crédit 38807 par
 *    KillCredit1, voir SQL). Aura retirée quand la quête est terminée.
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace LostIsles3
{
enum Data
{
    QUEST_CLUSTER_CLUCK         = 24671,
    QUEST_FREE_THE_CAPTIVES     = 24925,
    QUEST_ZOMBIES_VS_BOOTS      = 24942,

    NPC_WILD_CLUCKER            = 38111,
    NPC_CLUSTER_CLUCK_CREDIT    = 38117,
    NPC_OOMLOT_SHAMAN_QUEST     = 38644,
    NPC_GOBLIN_ZOMBIE           = 38753,

    SPELL_FIREWORKS_VISUAL      = 74177,
    SPELL_CLUCKER_LOOT_FX       = 66727,
    SPELL_BOOTS_DAMAGE_TRIGGER  = 72885,
};

// 71170 - Feu d'artifice télécommandé (clic droit sur 38111)
class spell_lost_isles_remote_fireworks : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Creature* clucker = GetHitUnit() ? GetHitUnit()->ToCreature() : nullptr;
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!clucker || !player || clucker->GetEntry() != NPC_WILD_CLUCKER || !clucker->IsAlive())
            return;

        if (player->GetQuestStatus(QUEST_CLUSTER_CLUCK) != QUEST_STATUS_INCOMPLETE)
            return;

        player->KilledMonsterCredit(NPC_CLUSTER_CLUCK_CREDIT);
        clucker->CastSpell(clucker, SPELL_FIREWORKS_VISUAL, true);
        clucker->CastSpell(clucker, SPELL_CLUCKER_LOOT_FX, true);
        clucker->DespawnOrUnsummon(2000, 30s);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_lost_isles_remote_fireworks::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 38643 - Goblin Captive
struct npc_lost_isles_goblin_captive : public ScriptedAI
{
    npc_lost_isles_goblin_captive(Creature* creature) : ScriptedAI(creature), _checkTimer(1000), _freed(false) { }

    void Reset() override
    {
        _checkTimer = 1000;
        _freed = false;
        _shamanGUID.Clear();
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() != NPC_OOMLOT_SHAMAN_QUEST || _freed)
            return;

        _freed = true;
        _shamanGUID.Clear();
        me->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
        Position dest = me->GetPosition();
        me->MovePosition(dest, 15.0f, frand(0.0f, float(2 * M_PI)));
        me->SetWalk(false);
        me->GetMotionMaster()->MovePoint(1, dest);
        me->DespawnOrUnsummon(4000, 60s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (_freed)
            return;

        if (_checkTimer > diff)
        {
            _checkTimer -= diff;
            return;
        }
        _checkTimer = 1000;

        if (!_shamanGUID.IsEmpty())
        {
            Creature* shaman = ObjectAccessor::GetCreature(*me, _shamanGUID);
            if (shaman && shaman->IsAlive())
                return;
            _shamanGUID.Clear();
        }

        Player* player = me->SelectNearestPlayer(15.0f);
        if (!player || player->GetQuestStatus(QUEST_FREE_THE_CAPTIVES) != QUEST_STATUS_INCOMPLETE)
            return;

        Position pos = me->GetPosition();
        me->MovePosition(pos, 3.0f, me->GetAngle(player) + float(M_PI));
        if (TempSummon* shaman = me->SummonCreature(NPC_OOMLOT_SHAMAN_QUEST, pos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30000))
        {
            _shamanGUID = shaman->GetGUID();
            shaman->AI()->AttackStart(player);
        }
    }

private:
    uint32 _checkTimer;
    bool _freed;
    ObjectGuid _shamanGUID;
};

// 72891 - Super-bottes à fusée d'appoint (objet 52013)
class spell_lost_isles_super_booster_boots : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(QUEST_ZOMBIES_VS_BOOTS) != QUEST_STATUS_INCOMPLETE)
            return;

        player->RemoveAurasDueToSpell(SPELL_BOOTS_DAMAGE_TRIGGER);
        if (Aura* aura = player->AddAura(SPELL_BOOTS_DAMAGE_TRIGGER, player))
        {
            aura->SetMaxDuration(90000);
            aura->SetDuration(90000);
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_lost_isles_super_booster_boots::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 72886 - Zombies vs. Super Booster Rocket Boots: Damage - Effect
class spell_lost_isles_boots_damage : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(QUEST_ZOMBIES_VS_BOOTS) != QUEST_STATUS_INCOMPLETE)
        {
            if (Unit* caster = GetCaster())
                caster->RemoveAurasDueToSpell(SPELL_BOOTS_DAMAGE_TRIGGER);
            targets.clear();
            return;
        }

        targets.remove_if([](WorldObject* object)
        {
            Unit* unit = object->ToUnit();
            return !unit || !unit->IsAlive() || unit->GetEntry() != NPC_GOBLIN_ZOMBIE;
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_lost_isles_boots_damage::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};
}

void AddSC_lost_isles_3()
{
    using namespace LostIsles3;
    RegisterSpellScript(spell_lost_isles_remote_fireworks);
    RegisterCreatureAI(npc_lost_isles_goblin_captive);
    RegisterSpellScript(spell_lost_isles_super_booster_boots);
    RegisterSpellScript(spell_lost_isles_boots_damage);
}
