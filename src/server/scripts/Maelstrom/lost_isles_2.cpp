/*
 * Azimov - Îles Perdues, partie 2 (lot 23)
 * Mécaniques inspirées d'AshamaneCore (lost_isles.cpp, scripts « gob »), réécrites pour le fork.
 *
 *  - 24864 « Irresistible Pool Pony » : bébés nagas 38412 / 44578 / 44579 / 44580.
 *    Joueur avec l'aura 71914 (déguisement) et la quête en cours à moins de 10 m :
 *    un bébé naga 44589 le suit (71917), crédit 38413, le bébé d'origine disparaît (réapparition 30 s).
 *  - 24901 « Town-In-A-Box: Under Attack » : 72207 Déflagration de canon (déclenchée par 72206 Tir du canon)
 *    qui touche un Guerrier Oomlot 38531 -> crédit 38536 au joueur qui tire.
 *  - 25207 « Good-bye, Sweet Oil » : 73892 Gros bouton rouge (objet 205061) -> crédit 39393 + explosions.
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace LostIsles2
{
enum Data
{
    QUEST_POOL_PONY             = 24864,
    QUEST_TIAB_UNDER_ATTACK     = 24901,
    QUEST_GOODBYE_SWEET_OIL     = 25207,

    NPC_NAGA_FOLLOWER           = 44589,
    NPC_POOL_PONY_CREDIT        = 38413,
    NPC_OOMLOT_WARRIOR          = 38531,
    NPC_TIAB_CREDIT             = 38536,
    NPC_SWEET_OIL_CREDIT        = 39393,
    NPC_CAULDRON_TRIGGER        = 41505,

    SPELL_POOL_PONY_DISGUISE    = 71914,
    SPELL_NAGA_FOLLOW_VISUAL    = 71917,
    SPELL_EXPLOSION_VISUAL_1    = 93569,
    SPELL_EXPLOSION_VISUAL_2    = 71094,
};

// Joueur derrière un sort lancé par un véhicule (canon) ou par le joueur lui-même
Player* GetShooter(Unit* caster)
{
    if (!caster)
        return nullptr;
    if (Player* player = caster->GetCharmerOrOwnerPlayerOrPlayerItself())
        return player;
    if (Vehicle* vehicle = caster->GetVehicleKit())
        for (int8 seat = 0; seat < MAX_VEHICLE_SEATS; ++seat)
            if (Unit* passenger = vehicle->GetPassenger(seat))
                if (Player* player = passenger->ToPlayer())
                    return player;
    return nullptr;
}

// 38412, 44578, 44579, 44580 - Naga Hatchling
struct npc_lost_isles_naga_hatchling : public ScriptedAI
{
    npc_lost_isles_naga_hatchling(Creature* creature) : ScriptedAI(creature), _checkTimer(1000) { }

    void Reset() override
    {
        _checkTimer = 1000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_checkTimer <= diff)
        {
            _checkTimer = 1000;
            if (!me->IsInCombat())
                if (Player* player = me->SelectNearestPlayer(10.0f))
                    if (player->HasAura(SPELL_POOL_PONY_DISGUISE) && player->GetQuestStatus(QUEST_POOL_PONY) == QUEST_STATUS_INCOMPLETE)
                    {
                        if (TempSummon* follower = player->SummonCreature(NPC_NAGA_FOLLOWER, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 300000))
                        {
                            follower->CastSpell(follower, SPELL_NAGA_FOLLOW_VISUAL, true);
                            follower->GetMotionMaster()->MoveFollow(player, frand(1.0f, 4.0f), frand(0.0f, float(2 * M_PI)));
                        }
                        player->KilledMonsterCredit(NPC_POOL_PONY_CREDIT);
                        me->DespawnOrUnsummon(0, 30s);
                        return;
                    }
        }
        else
            _checkTimer -= diff;

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    uint32 _checkTimer;
};

// 72207 - Déflagration de canon
class spell_lost_isles_cannon_blast : public SpellScript
{
    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        if (!target || target->GetEntry() != NPC_OOMLOT_WARRIOR)
            return;

        if (Player* player = GetShooter(GetOriginalCaster() ? GetOriginalCaster() : GetCaster()))
            if (player->GetQuestStatus(QUEST_TIAB_UNDER_ATTACK) == QUEST_STATUS_INCOMPLETE)
                player->KilledMonsterCredit(NPC_TIAB_CREDIT);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_lost_isles_cannon_blast::HandleHit, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// 73892 - Gros bouton rouge (objet 205061)
class spell_lost_isles_big_red_button : public SpellScript
{
    void HandleAfterCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(QUEST_GOODBYE_SWEET_OIL) != QUEST_STATUS_INCOMPLETE)
            return;

        player->KilledMonsterCredit(NPC_SWEET_OIL_CREDIT);

        // Explosions sur la plateforme pétrolière (positions d'AshamaneCore)
        static Position const ExplosionPos[] =
        {
            { 2477.0f,  2082.0f,  14.0f, 0.0f },
            { 2499.28f, 2091.48f, 17.0f, 0.0f },
            { 2450.424f, 2068.89f, 28.0f, 0.0f },
        };
        for (Position const& pos : ExplosionPos)
            if (TempSummon* trigger = player->SummonCreature(NPC_CAULDRON_TRIGGER, pos, TEMPSUMMON_TIMED_DESPAWN, 10000))
            {
                trigger->CastSpell(trigger, SPELL_EXPLOSION_VISUAL_1, true);
                trigger->CastSpell(trigger, SPELL_EXPLOSION_VISUAL_2, true);
            }
    }

    void Register() override
    {
        AfterCast.Register(&spell_lost_isles_big_red_button::HandleAfterCast);
    }
};
}

void AddSC_lost_isles_2()
{
    using namespace LostIsles2;
    RegisterCreatureAI(npc_lost_isles_naga_hatchling);
    RegisterSpellScript(spell_lost_isles_cannon_blast);
    RegisterSpellScript(spell_lost_isles_big_red_button);
}
