/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "CreatureAIImpl.h"
#include "ScriptMgr.h"
#include "CharmInfo.h"
#include "CombatAI.h"
#include "CreatureTextMgr.h"
#include "G3DPosition.hpp"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace chapter1_cata
{
enum UnworthyInitiatePhase
{
    PHASE_CHAINED,
    PHASE_TO_EQUIP,
    PHASE_EQUIPING,
    PHASE_TO_ATTACK,
    PHASE_ATTACKING,
};

enum UnworthyInitiateData
{
    DATA_PRISONER_GUID = 0
};

class spell_death_knight_initiate_visual : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        Creature* target = GetHitCreature();
        if (!target)
            return;

        uint32 spellId;
        switch (target->GetDisplayId())
        {
            case 25369: spellId = 51552; break; // bloodelf female
            case 25373: spellId = 51551; break; // bloodelf male
            case 25363: spellId = 51542; break; // draenei female
            case 25357: spellId = 51541; break; // draenei male
            case 25361: spellId = 51537; break; // dwarf female
            case 25356: spellId = 51538; break; // dwarf male
            case 25372: spellId = 51550; break; // forsaken female
            case 25367: spellId = 51549; break; // forsaken male
            case 25362: spellId = 51540; break; // gnome female
            case 25359: spellId = 51539; break; // gnome male
            case 25355: spellId = 51534; break; // human female
            case 25354: spellId = 51520; break; // human male
            case 25360: spellId = 51536; break; // nightelf female
            case 25358: spellId = 51535; break; // nightelf male
            case 25368: spellId = 51544; break; // orc female
            case 25364: spellId = 51543; break; // orc male
            case 25371: spellId = 51548; break; // tauren female
            case 25366: spellId = 51547; break; // tauren male
            case 25370: spellId = 51545; break; // troll female
            case 25365: spellId = 51546; break; // troll male
            default: return;
        }

        target->CastSpell(target, spellId, GetSpell());
        target->LoadEquipment();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_death_knight_initiate_visual::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum EyeOfAcherusMisc
{
    SPELL_THE_EYE_OF_ACHERUS                = 51852,
    SPELL_EYE_OF_ACHERUS_VISUAL             = 51892,
    SPELL_EYE_OF_ACHERUS_FLIGHT_BOOST       = 51923,
    SPELL_EYE_OF_ACHERUS_FLIGHT             = 51890,
    SPELL_ROOT_SELF                         = 51860,

    EVENT_ANNOUNCE_LAUNCH_TO_DESTINATION    = 1,
    EVENT_UNROOT                            = 2,
    EVENT_LAUNCH_TOWARDS_DESTINATION        = 3,
    EVENT_GRANT_CONTROL                     = 4,

    SAY_LAUNCH_TOWARDS_DESTINATION          = 0,
    SAY_EYE_UNDER_CONTROL                   = 1,

    POINT_NEW_AVALON                        = 1
};

enum DeathComesFromOnHigh
{
    SPELL_FORGE_CREDIT                  = 51974,
    SPELL_TOWN_HALL_CREDIT              = 51977,
    SPELL_SCARLET_HOLD_CREDIT           = 51980,
    SPELL_CHAPEL_CREDIT                 = 51982,

    NPC_NEW_AVALON_FORGE                = 28525,
    NPC_NEW_AVALON_TOWN_HALL            = 28543,
    NPC_SCARLET_HOLD                    = 28542,
    NPC_CHAPEL_OF_THE_CRIMSON_FLAME     = 28544
};

class spell_chapter1_siphon_of_acherus : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_FORGE_CREDIT,
            SPELL_TOWN_HALL_CREDIT,
            SPELL_SCARLET_HOLD_CREDIT,
            SPELL_CHAPEL_CREDIT
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        uint32 spellId = 0;

        switch (GetHitCreature()->GetEntry())
        {
            case NPC_NEW_AVALON_FORGE:
                spellId = SPELL_FORGE_CREDIT;
                break;
            case NPC_NEW_AVALON_TOWN_HALL:
                spellId = SPELL_TOWN_HALL_CREDIT;
                break;
            case NPC_SCARLET_HOLD:
                spellId = SPELL_SCARLET_HOLD_CREDIT;
                break;
            case NPC_CHAPEL_OF_THE_CRIMSON_FLAME:
                spellId = SPELL_CHAPEL_CREDIT;
                break;
            default:
                return;
        }

        GetCaster()->CastSpell(nullptr, spellId, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_chapter1_siphon_of_acherus::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_chapter1_recall_eye_of_acherus : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_THE_EYE_OF_ACHERUS });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = Object::ToPlayer(GetCaster()->GetCharmerOrOwner()))
        {
            player->StopCastingCharm();
            player->StopCastingBindSight();
            player->RemoveAura(SPELL_THE_EYE_OF_ACHERUS);
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_chapter1_recall_eye_of_acherus::HandleDummy, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Spells_DKI
{
    SPELL_DUEL                  = 52996,
    //SPELL_DUEL_TRIGGERED        = 52990,
    SPELL_DUEL_VICTORY          = 52994,
    SPELL_DUEL_FLAG             = 52991,
    SPELL_GROVEL                = 7267,
};

enum Says_VBM
{
    SAY_DUEL                    = 0,
};

enum Misc_VBN
{
    QUEST_DEATH_CHALLENGE = 12733
};

enum Paths_VBN
{
    PATH_DEATH_KNIGHT_INITIATE      = 10361360,
    PATH_DEATH_KNIGHT_INITIATE2     = 10361440,
    PATH_DEATH_KNIGHT_INITIATE3     = 10362320,
    PATH_DEATH_KNIGHT_INITIATE4     = 10362400,
    PATH_DEATH_KNIGHT_INITIATE5     = 10362480,
    PATH_DEATH_KNIGHT_INITIATE6     = 10363520,
    PATH_DEATH_KNIGHT_INITIATE7     = 10363680,
    PATH_DEATH_KNIGHT_INITIATE8     = 10363760,
};

enum DarkRiderOfAcherus
{
    SAY_DARK_RIDER              = 0,

    EVENT_START_MOVING          = 1,
    EVENT_DESPAWN_HORSE         = 2,
    EVENT_END_SCRIPT            = 3,

    SPELL_DESPAWN_HORSE         = 52267
};

enum SalanarTheHorseman
{
    SALANAR_SAY                       = 0,
    QUEST_INTO_REALM_OF_SHADOWS       = 12687,
    SPELL_EFFECT_STOLEN_HORSE         = 52263,
    SPELL_DELIVER_STOLEN_HORSE        = 52264,
    SPELL_CALL_DARK_RIDER             = 52266,
    SPELL_EFFECT_OVERTAKE             = 52349,
    SPELL_REALM_OF_SHADOWS            = 52693
};

enum HorseSeats
{
    SEAT_ID_0   = 0
};

class spell_stable_master_repo : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Creature* creature = GetTarget()->ToCreature();
        if (!creature)
            return;

        if (Vehicle* vehicleKit = creature->GetVehicleKit())
            if (Unit* passenger = vehicleKit->GetPassenger(SEAT_ID_0))
                GetCaster()->EngageWithTarget(passenger);

        creature->DespawnOrUnsummon(1s);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_stable_master_repo::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_deliver_stolen_horse : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DELIVER_STOLEN_HORSE, SPELL_EFFECT_STOLEN_HORSE });
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->RemoveAurasDueToSpell(SPELL_EFFECT_STOLEN_HORSE);

        Unit* caster = GetCaster();
        caster->RemoveAurasDueToSpell(SPELL_EFFECT_STOLEN_HORSE);
        caster->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
        caster->SetFaction(FACTION_FRIENDLY);

        caster->CastSpell(caster, SPELL_CALL_DARK_RIDER, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_deliver_stolen_horse::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_KILL_CREDIT2);
    }
};

enum TheGiftThatKeepsOnGiving
{
    SAY_LINE_0  = 0,

    NPC_GHOULS  = 28845,
    NPC_GHOSTS  = 28846,
};

enum GiftOfTheHarvester
{
    SPELL_GHOUL_TRANFORM    = 52490,
    SPELL_GHOST_TRANSFORM   = 52505
};

enum Runeforging
{
    SPELL_RUNEFORGING_CREDIT     = 54586,
    QUEST_RUNEFORGING            = 12842
};

class spell_chapter1_runeforging_credit : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_RUNEFORGING_CREDIT }) &&
            sObjectMgr->GetQuestTemplate(QUEST_RUNEFORGING);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            if (caster->GetQuestStatus(QUEST_RUNEFORGING) == QUEST_STATUS_INCOMPLETE)
                caster->CastSpell(caster, SPELL_RUNEFORGING_CREDIT);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_chapter1_runeforging_credit::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

enum SkyDarkenerAssault
{
    SPELL_SKY_DARKENER_ASSAULT     = 52125
};

class spell_chapter1_sky_darkener_assault : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SKY_DARKENER_ASSAULT });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_SKY_DARKENER_ASSAULT);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_chapter1_sky_darkener_assault::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_chapter1_emblazon_runeblade : public AuraScript
{
    void HandleEffectPeriodic(AuraEffect const* aurEff)
    {
        PreventDefaultAction();
        if (Unit* caster = GetCaster())
            caster->CastSpell(caster, aurEff->GetSpellInfo()->Effects[aurEff->GetEffIndex()].TriggerSpell, aurEff);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_chapter1_emblazon_runeblade::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

class spell_chapter1_emblazon_runeblade_effect : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), uint32(GetEffectValue()), false);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_chapter1_emblazon_runeblade_effect::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum HearthglenCrusaderPaths : uint32
{
    PATH_HEARTHGLEN_CRUSADER_DESPAWN    = 10445360,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN2   = 10445600,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN3   = 10448640,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN4   = 10449200,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN5   = 10452240,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN6   = 10452880,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN7   = 10452960,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN8   = 10453040,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN9   = 10453520,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN10  = 10453680,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN11  = 10454000,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN12  = 10454080,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN13  = 10454160,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN14  = 10454320,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN15  = 10454560,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN16  = 10459440,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN17  = 10460320,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN18  = 10463040,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN19  = 10463120,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN20  = 10463280,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN21  = 10463360,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN22  = 10463520,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN23  = 10463680,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN24  = 10463840,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN25  = 10464080,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN26  = 10464160,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN27  = 10464240,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN28  = 10464320,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN29  = 10464400,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN30  = 10464480,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN31  = 10464720,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN32  = 10464800,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN33  = 10464880,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN34  = 10464960,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN35  = 10465040,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN36  = 10465520,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN37  = 10465600,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN38  = 10466000,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN39  = 10466160,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN40  = 10466320,
    PATH_HEARTHGLEN_CRUSADER_DESPAWN41  = 10466400,
};

struct npc_hearthglen_crusader : public ScriptedAI
{
    npc_hearthglen_crusader(Creature* creature) : ScriptedAI(creature), _minimumRange(0)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(creature->m_spells[0]);
        if (!spellInfo)
            return;

        float minRange = spellInfo->GetMinRange(false);
        float maxRange = spellInfo->GetMaxRange(false);
        _minimumRange = minRange;

        if (!_minimumRange)
            _minimumRange = MELEE_RANGE;
        creature->m_CombatDistance = maxRange;
        creature->m_SightDistance = creature->m_CombatDistance;
    }

    void AttackStart(Unit* who) override
    {
        if (!who)
            return;

        if (me->IsWithinCombatRange(who, _minimumRange))
        {
            if (me->Attack(who, true) && !who->IsFlying())
                me->GetMotionMaster()->MoveChase(who);
        }
        else
        {
            if (me->Attack(who, false) && !who->IsFlying())
                me->GetMotionMaster()->MoveChase(who, me->m_CombatDistance);
        }

        if (who->IsFlying())
            me->GetMotionMaster()->MoveIdle();
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        if (!me->IsWithinCombatRange(me->GetVictim(), _minimumRange))
            DoSpellAttackIfReady(me->m_spells[0]);
    }

    void WaypointPathEnded(uint32 /*nodeId*/, uint32 pathId) override
    {
        switch (pathId)
        {
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN2:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN3:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN4:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN5:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN6:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN7:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN8:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN9:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN10:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN11:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN12:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN13:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN14:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN15:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN16:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN17:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN18:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN19:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN20:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN21:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN22:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN23:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN24:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN25:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN26:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN27:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN28:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN29:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN30:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN31:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN32:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN33:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN34:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN35:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN36:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN37:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN38:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN39:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN40:
            case PATH_HEARTHGLEN_CRUSADER_DESPAWN41:
                me->DespawnOrUnsummon(1s);
                break;
            default:
                break;
        }
    }

private:
    float _minimumRange;
};
}

void AddSC_chapter1_cata()
{
    using namespace chapter1_cata;
    RegisterSpellScript(spell_death_knight_initiate_visual);
    RegisterSpellScript(spell_chapter1_siphon_of_acherus);
    RegisterSpellScript(spell_chapter1_recall_eye_of_acherus);
    RegisterSpellScript(spell_stable_master_repo);
    RegisterSpellScript(spell_deliver_stolen_horse);
    RegisterSpellScript(spell_chapter1_runeforging_credit);
    RegisterSpellScript(spell_chapter1_sky_darkener_assault);
    RegisterSpellScript(spell_chapter1_emblazon_runeblade);
    RegisterSpellScript(spell_chapter1_emblazon_runeblade_effect);
    RegisterCreatureAI(npc_hearthglen_crusader);
}
