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

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Containers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace hellfire_peninsula_cata
{
enum ExorcismSpells
{
    SPELL_JULES_GOES_PRONE     = 39283,
    SPELL_JULES_THREATENS_AURA = 39284,
    SPELL_JULES_GOES_UPRIGHT   = 39294,
    SPELL_JULES_VOMITS_AURA    = 39295,

    SPELL_BARADAS_COMMAND      = 39277,
    SPELL_BARADA_FALTERS       = 39278,
};

enum ExorcismTexts
{
    SAY_BARADA_1 = 0,
    SAY_BARADA_2 = 1,
    SAY_BARADA_3 = 2,
    SAY_BARADA_4 = 3,
    SAY_BARADA_5 = 4,
    SAY_BARADA_6 = 5,
    SAY_BARADA_7 = 6,
    SAY_BARADA_8 = 7,

    SAY_JULES_1  = 0,
    SAY_JULES_2  = 1,
    SAY_JULES_3  = 2,
    SAY_JULES_4  = 3,
    SAY_JULES_5  = 4,
};

Position const exorcismPos[11] =
{
    { -707.123f, 2751.686f, 101.592f, 4.577416f }, //Barada Waypoint-1      0
    { -710.731f, 2749.075f, 101.592f, 1.513286f }, //Barada Cast position   1
    { -710.332f, 2754.394f, 102.948f, 3.207566f }, //Jules                  2
    { -714.261f, 2747.754f, 103.391f, 0.0f },      //Jules Waypoint-1       3
    { -713.113f, 2750.194f, 103.391f, 0.0f },      //Jules Waypoint-2       4
    { -710.385f, 2750.896f, 103.391f, 0.0f },      //Jules Waypoint-3       5
    { -708.309f, 2750.062f, 103.391f, 0.0f },      //Jules Waypoint-4       6
    { -707.401f, 2747.696f, 103.391f, 0.0f },      //Jules Waypoint-5       7
    { -708.591f, 2745.266f, 103.391f, 0.0f },      //Jules Waypoint-6       8
    { -710.597f, 2744.035f, 103.391f, 0.0f },      //Jules Waypoint-7       9
    { -713.089f, 2745.302f, 103.391f, 0.0f },      //Jules Waypoint-8      10
};

enum ExorcismMisc
{
    NPC_DARKNESS_RELEASED               = 22507,
    NPC_FOUL_PURGE                      = 22506,
    NPC_COLONEL_JULES                   = 22432,

    BARADAS_GOSSIP_MESSAGE              = 10683,

    QUEST_THE_EXORCISM_OF_COLONEL_JULES = 10935,

    ACTION_START_EVENT                  = 1,
    ACTION_JULES_HOVER                  = 2,
    ACTION_JULES_FLIGHT                 = 3,
    ACTION_JULES_MOVE_HOME              = 4,
};

enum ExorcismEvents
{
    EVENT_BARADAS_TALK = 1,
    EVENT_RESET        = 2,

    //Colonel Jules
    EVENT_SUMMON_SKULL = 1,
};

enum Aledis
{
    SAY_CHALLENGE = 0,
    SAY_DEFEATED = 1,
    EVENT_TALK = 1,
    EVENT_ATTACK = 2,
    EVENT_EVADE = 3,
    EVENT_FIREBALL = 4,
    EVENT_FROSTNOVA = 5,
    SPELL_FIREBALL = 20823,
    SPELL_FROSTNOVA = 11831
};

enum WatchCommanderLeonus
{
    SAY_COVER = 0,

    EVENT_START = 1,
    EVENT_CAST  = 2,
    EVENT_END   = 3,

    GAME_EVENT_HELLFIRE = 85,

    NPC_INFERNAL_RAIN   = 18729,
    NPC_FEAR_CONTROLLER = 19393,
    SPELL_INFERNAL_RAIN = 33814,
    SPELL_FEAR          = 33815  // Serverside spell
};

enum FelSpirits
{
    SPELL_SEND_VENGEANCE_TO_PLAYER   = 39202,
    SPELL_SUMMON_FEL_SPIRIT          = 39206
};

class spell_hellfire_peninsula_send_vengeance : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SEND_VENGEANCE_TO_PLAYER });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (TempSummon* target = GetHitUnit()->ToTempSummon())
            if (Unit* summoner = target->GetSummoner())
                target->CastSpell(summoner, SPELL_SEND_VENGEANCE_TO_PLAYER, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_hellfire_peninsula_send_vengeance::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_hellfire_peninsula_send_vengeance_to_player : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_FEL_SPIRIT });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_SUMMON_FEL_SPIRIT, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_hellfire_peninsula_send_vengeance_to_player::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Translocation
{
    SPELL_TRANSLOCATION_FALCON_WATCH_TOWER_DOWN     = 30140,
    SPELL_TRANSLOCATION_FALCON_WATCH_TOWER_UP       = 30141
};

class spell_hellfire_peninsula_translocation_falcon_watch : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), _triggeredSpellId);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_hellfire_peninsula_translocation_falcon_watch::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }

    uint32 _triggeredSpellId;

public:
    explicit spell_hellfire_peninsula_translocation_falcon_watch(Translocation triggeredSpellId) : _triggeredSpellId(triggeredSpellId) { }
};

enum HelboarTheOtherWhiteMeat
{
    SPELL_SUMMON_PURIFIED_HELBOAR_MEAT      = 29277,
    SPELL_SUMMON_TOXIC_HELBOAR_MEAT         = 29278
};

class spell_hellfire_peninsula_purify_helboar_meat : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_PURIFIED_HELBOAR_MEAT, SPELL_SUMMON_TOXIC_HELBOAR_MEAT });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), roll_chance_i(50) ? SPELL_SUMMON_PURIFIED_HELBOAR_MEAT : SPELL_SUMMON_TOXIC_HELBOAR_MEAT);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_hellfire_peninsula_purify_helboar_meat::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum TheEyesOfGrillok
{
    SPELL_EYE_OF_GRILLOK      = 38495
};

class spell_hellfire_peninsula_absorb_eye_of_grillok : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EYE_OF_GRILLOK });
    }

    void PeriodicTick(AuraEffect const* aurEff)
    {
        PreventDefaultAction();

        if (Unit* caster = GetCaster())
            GetTarget()->CastSpell(caster, SPELL_EYE_OF_GRILLOK, aurEff);

        if (Creature* target = GetTarget()->ToCreature())
        {
            /// @todo: This is a hack, in flight missiles of spells of despawned creatures get cancelled - delay despawning by the duration of SPELL_EYE_OF_GRILLOK aura
            target->SetVisible(false);
            target->DespawnOrUnsummon(5s);
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_hellfire_peninsula_absorb_eye_of_grillok::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum TheDemoniacScryer
{
    SPELL_SUMMON_DEMONAIC_VISITATION      = 38991
};

class spell_hellfire_peninsula_demonaic_visitation : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_DEMONAIC_VISITATION });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_SUMMON_DEMONAIC_VISITATION, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_hellfire_peninsula_demonaic_visitation::AfterRemove, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};
}

void AddSC_hellfire_peninsula_cata()
{
    using namespace hellfire_peninsula_cata;
    RegisterSpellScript(spell_hellfire_peninsula_send_vengeance);
    RegisterSpellScript(spell_hellfire_peninsula_send_vengeance_to_player);
    RegisterSpellScriptWithArgs(spell_hellfire_peninsula_translocation_falcon_watch, "spell_hellfire_peninsula_translocation_falcon_watch_tower_down", SPELL_TRANSLOCATION_FALCON_WATCH_TOWER_DOWN);
    RegisterSpellScriptWithArgs(spell_hellfire_peninsula_translocation_falcon_watch, "spell_hellfire_peninsula_translocation_falcon_watch_tower_up", SPELL_TRANSLOCATION_FALCON_WATCH_TOWER_UP);
    RegisterSpellScript(spell_hellfire_peninsula_purify_helboar_meat);
    RegisterSpellScript(spell_hellfire_peninsula_absorb_eye_of_grillok);
    RegisterSpellScript(spell_hellfire_peninsula_demonaic_visitation);
}
