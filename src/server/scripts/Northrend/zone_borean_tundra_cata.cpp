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
#include "ObjectMgr.h"
#include "Group.h"
#include <algorithm>
#include <array>
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedFollowerAI.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

namespace borean_tundra_cata
{
enum CaribouTrap
{
    EVENT_FUR_SPAWN        = 1,
    EVENT_SPAWN_TRAPPER,
    EVENT_TRAPPER_MOVE,
    EVENT_TRAPPER_TEXT,
    EVENT_TRAPPER_LOOT,
    EVENT_FUR_DESPAWN,
    EVENT_TRAPPER_DIE,
    EVENT_DESPAWN_ALL,

    GO_HIGH_QUALITY_FUR    = 187983,

    NPC_NESINGWARY_TRAPPER = 25835,

    SAY_NESINGWARY_1       = 0,

    SPELL_PLACE_FAKE_FUR   = 46085,
    SPELL_TRAPPED          = 46104,
};

struct go_caribou_trap : public GameObjectAI
{
    go_caribou_trap(GameObject* go) : GameObjectAI(go), _placedFur(false) { }

    void Reset() override
    {
        me->SetGoState(GO_STATE_READY);
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (_placedFur)
            return;

        Player* playerCaster = caster->ToPlayer();
        if (!playerCaster)
            return;

        if (spellInfo->Id == SPELL_PLACE_FAKE_FUR)
        {
            _playerGUID = caster->GetGUID();
            _placedFur = true;
            _events.ScheduleEvent(EVENT_FUR_SPAWN, 1s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_placedFur)
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FUR_SPAWN:
                    if (GameObject* fur = me->SummonGameObject(GO_HIGH_QUALITY_FUR, me->GetPosition(), QuaternionData(0.0f, 0.0f, 0.77162457f, 0.63607824f), 20))
                        _goFurGUID = fur->GetGUID();
                    _events.ScheduleEvent(EVENT_SPAWN_TRAPPER, 1s);
                    break;
                case EVENT_SPAWN_TRAPPER:
                    if (TempSummon* trapper = me->SummonCreature(NPC_NESINGWARY_TRAPPER, me->GetFirstCollisionPosition(21.0f, 0), TEMPSUMMON_DEAD_DESPAWN, 6s))
                    {
                        trapper->SetFacingToObject(me);
                        _trapperGUID = trapper->GetGUID();
                    }
                    _events.ScheduleEvent(EVENT_TRAPPER_MOVE, 1s);
                    break;
                case EVENT_TRAPPER_MOVE:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->GetMotionMaster()->MovePoint(0, trapper->GetFirstCollisionPosition(20.0f, 0));
                    _events.ScheduleEvent(EVENT_TRAPPER_TEXT, 5s);
                    break;
                case EVENT_TRAPPER_TEXT:
                {
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                    {
                        if (trapper->IsAIEnabled())
                            trapper->AI()->Talk(SAY_NESINGWARY_1);
                    }
                    _events.ScheduleEvent(EVENT_TRAPPER_LOOT, 2s);
                    break;
                }
                case EVENT_TRAPPER_LOOT:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->HandleEmoteCommand(EMOTE_ONESHOT_LOOT);
                    _events.ScheduleEvent(EVENT_FUR_DESPAWN, 1s);
                    break;
                case EVENT_FUR_DESPAWN:
                    if (GameObject* fur = ObjectAccessor::GetGameObject(*me, _goFurGUID))
                        fur->Delete();
                    _events.ScheduleEvent(EVENT_TRAPPER_DIE, 1s);
                    break;
                case EVENT_TRAPPER_DIE:
                    me->SetGoState(GO_STATE_ACTIVE);
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                    {
                        if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                            player->KilledMonsterCredit(trapper->GetEntry(), trapper->GetGUID());
                        trapper->CastSpell(trapper, SPELL_TRAPPED);
                    }
                    _events.ScheduleEvent(EVENT_DESPAWN_ALL, 1s);
                    break;
                case EVENT_DESPAWN_ALL:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->DespawnOrUnsummon();
                    me->DespawnOrUnsummon(0s, 50s);
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
    bool _placedFur;
    ObjectGuid _goFurGUID;
    ObjectGuid _playerGUID;
    ObjectGuid _trapperGUID;
};

enum MammothTrap
{
    EVENT_FIND_MAMMOTH     = 1,
    EVENT_FACE_PLAYER,
    EVENT_QUEST_CREDIT,
    EVENT_MAMMOTH_TEXT,
    EVENT_MAMMOTH_MOVE,
    EVENT_MAMMOTH_DESPAWN,
    EVENT_TRAP_RESET,
    EVENT_MAMMOTH_RESPAWN,

    NPC_TRAPPED_MAMMOTH    = 25850,

    SAY_MAMMOTH            = 0,

    SPELL_SMASH_TRAP       = 46201
};

struct go_mammoth_trap : public GameObjectAI
{
    go_mammoth_trap(GameObject* go) : GameObjectAI(go), _trapSmashed(true) { }

    void Reset() override
    {
        me->SetGoState(GO_STATE_ACTIVE);
        _events.ScheduleEvent(EVENT_FIND_MAMMOTH, 1s);
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Player* playerCaster = caster->ToPlayer();
        if (!playerCaster)
            return;

        if (me->GetGoState() == GO_STATE_READY)
            return;

        if (spellInfo->Id == SPELL_SMASH_TRAP)
        {
            _playerGUID = caster->GetGUID();
            _trapSmashed = true;
            me->SetGoState(GO_STATE_READY);
            _events.ScheduleEvent(EVENT_FACE_PLAYER, 1s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_trapSmashed)
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FIND_MAMMOTH:
                    if (Creature* mammoth = me->FindNearestCreature(NPC_TRAPPED_MAMMOTH, 1.0f, true))
                    {
                        _mammothGUID = mammoth->GetGUID();
                        _trapSmashed = false;
                    }
                    break;
                case EVENT_FACE_PLAYER:
                    if (Creature* mammoth = ObjectAccessor::GetCreature(*me, _mammothGUID))
                        if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                        {
                            mammoth->SetStandState(UNIT_STAND_STATE_STAND);
                            mammoth->SetFacingToObject(player);
                        }
                    _events.ScheduleEvent(EVENT_QUEST_CREDIT, 1s);
                    break;
                case EVENT_QUEST_CREDIT:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                        player->KilledMonsterCredit(NPC_TRAPPED_MAMMOTH);
                    _events.ScheduleEvent(EVENT_MAMMOTH_TEXT, 1s);
                    break;
                case EVENT_MAMMOTH_TEXT:
                    if (Creature* mammoth = ObjectAccessor::GetCreature(*me, _mammothGUID))
                        if (mammoth->IsAIEnabled())
                            mammoth->AI()->Talk(SAY_MAMMOTH);
                    _events.ScheduleEvent(EVENT_MAMMOTH_MOVE, 2s);
                    break;
                case EVENT_MAMMOTH_MOVE:
                    if (Creature* mammoth = ObjectAccessor::GetCreature(*me, _mammothGUID))
                        mammoth->GetMotionMaster()->MovePoint(0, mammoth->GetFirstCollisionPosition(50.0f, me->GetOrientation()));
                    _events.ScheduleEvent(EVENT_MAMMOTH_DESPAWN, 9s);
                    break;
                case EVENT_MAMMOTH_DESPAWN:
                    if (Creature* mammoth = ObjectAccessor::GetCreature(*me, _mammothGUID))
                        mammoth->DespawnOrUnsummon(0s, 120s);
                    _events.ScheduleEvent(EVENT_MAMMOTH_RESPAWN, 5s);
                    break;
                case EVENT_MAMMOTH_RESPAWN:
                    if (me->FindNearestCreature(NPC_TRAPPED_MAMMOTH, 1.0f, true))
                        Reset();
                    else
                        _events.ScheduleEvent(EVENT_MAMMOTH_RESPAWN, 5s);
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
    bool _trapSmashed;
    ObjectGuid _playerGUID;
    ObjectGuid _mammothGUID;
};

enum Valiancekeepcannons
{
    GO_VALIANCE_KEEP_CANNON_1                     = 187560,
    GO_VALIANCE_KEEP_CANNON_2                     = 188692
};

enum Thassarian
{
    EVENT_THASSARIAN_SCRIPT_1  = 1,
    EVENT_THASSARIAN_SCRIPT_2,
    EVENT_THASSARIAN_SCRIPT_3,
    EVENT_THASSARIAN_SCRIPT_4,
    EVENT_THASSARIAN_SCRIPT_5,
    EVENT_THASSARIAN_SCRIPT_6,
    EVENT_THASSARIAN_SCRIPT_7,
    EVENT_THASSARIAN_SCRIPT_8,
    EVENT_THASSARIAN_SCRIPT_9,
    EVENT_THASSARIAN_SCRIPT_10,
    EVENT_THASSARIAN_SCRIPT_11,
    EVENT_THASSARIAN_SCRIPT_12,
    EVENT_THASSARIAN_SCRIPT_13,
    EVENT_THASSARIAN_SCRIPT_14,
    EVENT_THASSARIAN_SCRIPT_15,
    EVENT_THASSARIAN_SCRIPT_16,
    EVENT_THASSARIAN_SCRIPT_17,
    EVENT_THASSARIAN_SCRIPT_18,
    EVENT_THASSARIAN_SCRIPT_19,
    EVENT_THASSARIAN_SCRIPT_20,
    EVENT_THASSARIAN_SCRIPT_21,
    EVENT_THASSARIAN_SCRIPT_22,
    EVENT_THASSARIAN_SCRIPT_23,
    EVENT_THASSARIAN_SCRIPT_24,
    EVENT_THASSARIAN_SCRIPT_25,
    EVENT_THASSARIAN_SCRIPT_26,
    EVENT_THASSARIAN_SCRIPT_27,
    EVENT_THASSARIAN_SCRIPT_28,
    EVENT_THASSARIAN_SCRIPT_29,

    FACTION_VALANAR_COMBAT      = 1988,

    NPC_IMAGE_LICH_KING         = 26203,
    NPC_COUNSELOR_TALBOT        = 25301,
    NPC_PRINCE_VALANAR          = 28189,
    NPC_GENERAL_ARLOS           = 25250,
    NPC_LERYSSA                 = 25251,

    SPELL_TRANSFORM_VALANAR     = 46753,
    SPELL_STUN                  = 46957,

    SAY_THASSARIAN_1            = 0,
    SAY_THASSARIAN_2            = 1,
    SAY_THASSARIAN_3            = 2,
    SAY_THASSARIAN_4            = 3,
    SAY_THASSARIAN_5            = 4,
    SAY_THASSARIAN_6            = 5,
    SAY_THASSARIAN_7            = 6,
    SAY_TALBOT_1                = 0,
    SAY_TALBOT_2                = 1,
    SAY_TALBOT_3                = 2,
    SAY_TALBOT_4                = 3,
    SAY_LICH_1                  = 0,
    SAY_LICH_2                  = 1,
    SAY_LICH_3                  = 2,
    SAY_ARLOS_1                 = 0,
    SAY_ARLOS_2                 = 1,
    SAY_LERYSSA_1               = 0,
    SAY_LERYSSA_2               = 1,
    SAY_LERYSSA_3               = 2,
    SAY_LERYSSA_4               = 3,

    PATH_THASSARIAN             = 8104240,
    PATH_ARTHAS                 = 8104248,
    PATH_TALBOT                 = 8104256,
    PATH_ARLOS                  = 8104264,
    PATH_LERYSSA                = 8104272
};

enum CounselorTalbot
{
    AREA_LAST_RITES     = 4128,

    EVENT_DEFLECTION    = 1,
    EVENT_SOUL_BLAST,
    EVENT_VAMPIRIC_BOLT,

    SPELL_DEFLECTION    = 51009,
    SPELL_SOUL_BLAST    = 50992,
    SPELL_VAMPIRIC_BOLT = 51016
};

enum BloodsporeRuination
{
    NPC_BLOODMAGE_LAURITH   = 25381,
    SAY_BLOODMAGE_LAURITH   = 0,
    EVENT_TALK              = 1,
    EVENT_RESET_ORIENTATION,

    DATA_FACING_PLAYER_GUID = 0
};

enum HahYoureNotSoBigNow
{
    SPELL_BIGGER_1                 = 45674,
    SPELL_SHRUNK_1                 = 45675,
    SPELL_YELLOW_1                 = 45678,
    SPELL_GHOST_1                  = 45682,
    SPELL_POLYMORPH_1              = 45684,

    SPELL_BIGGER_2                 = 45673,
    SPELL_SHRUNK_2                 = 45672,
    SPELL_YELLOW_2                 = 45677,
    SPELL_GHOST_2                  = 45681,
    SPELL_POLYMORPH_2              = 45683,

    SPELL_MAGNATAUR_KILL_CREDIT    = 45686,
    SPELL_MAGNATAUR_ON_DEATH_2     = 45685
};

static constexpr std::array<uint32, 5> BlasterCasterSpells = { SPELL_BIGGER_1, SPELL_SHRUNK_1, SPELL_YELLOW_1, SPELL_GHOST_1, SPELL_POLYMORPH_1 };

static constexpr std::array<uint32, 5> BlasterTargetSpells = { SPELL_BIGGER_2, SPELL_SHRUNK_2, SPELL_YELLOW_2, SPELL_GHOST_2, SPELL_POLYMORPH_2 };

class spell_borean_tundra_shortening_blaster : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(BlasterCasterSpells) && ValidateSpellInfo(BlasterTargetSpells);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(BlasterCasterSpells));
        target->CastSpell(target, Trinity::Containers::SelectRandomContainerElement(BlasterTargetSpells));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_shortening_blaster::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_magnataur_on_death_1 : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(BlasterTargetSpells) && ValidateSpellInfo({ SPELL_MAGNATAUR_KILL_CREDIT, SPELL_MAGNATAUR_ON_DEATH_2 });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* caster = GetCaster()->ToCreature();
        if (!caster)
            return;

        if (!caster->hasLootRecipient())
            return;

        if (std::none_of(BlasterTargetSpells.begin(), BlasterTargetSpells.end(), [caster](uint32 spell) { return caster->HasAura(spell); }))
            return;

        if (Group* group = caster->GetLootRecipientGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                if (Player* player = itr->GetSource())
                    if (player->IsInMap(caster) && player->IsAtGroupRewardDistance(caster))
                        player->CastSpell(player, SPELL_MAGNATAUR_KILL_CREDIT);
        }
        else if (Player* player = caster->GetLootRecipient())
            player->CastSpell(player, SPELL_MAGNATAUR_KILL_CREDIT);

        caster->CastSpell(caster, SPELL_MAGNATAUR_ON_DEATH_2);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_magnataur_on_death_1::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_magnataur_on_death_2 : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(BlasterTargetSpells);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        for (uint32 spell : BlasterTargetSpells)
            GetCaster()->RemoveAurasDueToSpell(spell);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_magnataur_on_death_2::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum TakenByTheScourge
{
    SPELL_FREED_WARSONG_MAGE        = 45526,
    SPELL_FREED_WARSONG_SHAMAN      = 45527,
    SPELL_FREED_WARSONG_WARRIOR     = 45514,
    SPELL_FREED_WARSONG_PEON        = 45532,
    SPELL_FREED_SOLDIER_DEBUFF      = 45523
};

class spell_borean_tundra_nerubar_web_random_unit_not_on_quest : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return true && ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_nerubar_web_random_unit_not_on_quest::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

std::array<uint32, 3> const CocoonSummonSpells =
{
    SPELL_FREED_WARSONG_MAGE, SPELL_FREED_WARSONG_SHAMAN, SPELL_FREED_WARSONG_WARRIOR
};

class spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREED_SOLDIER_DEBUFF });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        // Do nothing if has 3 soldiers
        Aura* aura = caster->GetAura(SPELL_FREED_SOLDIER_DEBUFF);
        if (!aura || aura->GetStackAmount() < 3)
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREED_SOLDIER_DEBUFF, SPELL_FREED_WARSONG_PEON });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        // Always summon peon if has 3 soldiers
        Aura* aura = caster->GetAura(SPELL_FREED_SOLDIER_DEBUFF);
        if ((!aura || aura->GetStackAmount() < 3) && roll_chance_i(75))
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            caster->CastSpell(nullptr, SPELL_FREED_WARSONG_PEON, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_borean_tundra_dispel_freed_soldier_debuff : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return true && ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Aura* aura = GetHitUnit()->GetAura(uint32(GetEffectValue())))
            aura->ModStackAmount(-1);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_dispel_freed_soldier_debuff::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BringEmBackAlive
{
    SPELL_KODO_DELIVERED   = 48203,

    TEXT_DELIVERED_1       = 24881,
    TEXT_DELIVERED_2       = 24882,
    TEXT_DELIVERED_3       = 26284,
    TEXT_DELIVERED_4       = 26285,
    TEXT_DELIVERED_5       = 26286
};

class spell_borean_tundra_deliver_kodo : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_KODO_DELIVERED });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_KODO_DELIVERED, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_deliver_kodo::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_kodo_delivered : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return (sObjectMgr->GetBroadcastText(TEXT_DELIVERED_1) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_DELIVERED_2) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_DELIVERED_3) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_DELIVERED_4) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_DELIVERED_5) != nullptr);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->Unit::Say(RAND(TEXT_DELIVERED_1, TEXT_DELIVERED_2, TEXT_DELIVERED_3, TEXT_DELIVERED_4, TEXT_DELIVERED_5), caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_kodo_delivered::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheArtOfPersuasion
{
    WHISPER_TORTURE_1                      = 0,
    WHISPER_TORTURE_2                      = 1,
    WHISPER_TORTURE_3                      = 2,
    WHISPER_TORTURE_4                      = 3,
    WHISPER_TORTURE_5                      = 4,
    WHISPER_TORTURE_RANDOM_1               = 5,
    WHISPER_TORTURE_RANDOM_2               = 6,
    WHISPER_TORTURE_RANDOM_3               = 7,

    WHISPER_TORTURE_PROTO_1                = 8,
    WHISPER_TORTURE_PROTO_2                = 9,
    WHISPER_TORTURE_PROTO_3                = 10,
    WHISPER_TORTURE_PROTO_4                = 11,
    WHISPER_TORTURE_PROTO_5                = 12,
    WHISPER_TORTURE_PROTO_6                = 13,
    WHISPER_TORTURE_PROTO_7                = 14,
    WHISPER_TORTURE_PROTO_8                = 15,
    WHISPER_TORTURE_PROTO_9                = 16,
    WHISPER_TORTURE_PROTO_10               = 17,

    SPELL_NEURAL_NEEDLE_IMPACT             = 45702,
    SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT   = 48254
};

class spell_borean_tundra_neural_needle : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_NEURAL_NEEDLE_IMPACT });
    }

    void HandleWhisper()
    {
        Player* caster = GetCaster()->ToPlayer();
        Creature* target = GetHitCreature();
        if (!caster || !target)
            return;

        target->CastSpell(target, SPELL_NEURAL_NEEDLE_IMPACT);

        if (Aura* aura = caster->GetAura(GetSpellInfo()->Id))
        {
            switch (aura->GetStackAmount())
            {
                case 1:
                    target->AI()->Talk(WHISPER_TORTURE_1, caster);
                    break;
                case 2:
                    target->AI()->Talk(WHISPER_TORTURE_2, caster);
                    break;
                case 3:
                    target->AI()->Talk(WHISPER_TORTURE_3, caster);
                    break;
                case 4:
                    target->AI()->Talk(WHISPER_TORTURE_4, caster);
                    break;
                case 5:
                    target->AI()->Talk(WHISPER_TORTURE_5, caster);
                    caster->KilledMonsterCredit(target->GetEntry());
                    break;
                case 6:
                    target->AI()->Talk(RAND(WHISPER_TORTURE_RANDOM_1, WHISPER_TORTURE_RANDOM_2, WHISPER_TORTURE_RANDOM_3), caster);
                    break;
                default:
                    return;
            }
        }
    }

    void Register() override
    {
        AfterHit.Register(&spell_borean_tundra_neural_needle::HandleWhisper);
    }
};

class spell_borean_tundra_prototype_neural_needle : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT });
    }

    void HandleWhisper()
    {
        Player* caster = GetCaster()->ToPlayer();
        Creature* target = GetHitCreature();
        if (!caster || !target)
            return;

        target->CastSpell(target, SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT);

        uint32 text = 0;
        if (Aura* aura = caster->GetAura(GetSpellInfo()->Id))
        {
            switch (aura->GetStackAmount())
            {
                case 1: text = WHISPER_TORTURE_PROTO_1; break;
                case 2: text = WHISPER_TORTURE_PROTO_2; break;
                case 3: text = WHISPER_TORTURE_PROTO_3; break;
                case 4: text = WHISPER_TORTURE_PROTO_4; break;
                case 5: text = WHISPER_TORTURE_PROTO_5; break;
                case 6: text = WHISPER_TORTURE_PROTO_6; break;
                case 7: text = WHISPER_TORTURE_PROTO_7; break;
                case 8: text = WHISPER_TORTURE_PROTO_8; break;
                case 9: text = WHISPER_TORTURE_PROTO_9; break;
                case 10: text = WHISPER_TORTURE_PROTO_10; break;
                default: return;
            }
        }

        if (text)
            target->AI()->Talk(text, caster);
    }

    void Register() override
    {
        AfterHit.Register(&spell_borean_tundra_prototype_neural_needle::HandleWhisper);
    }
};

enum PrisonBreak
{
    SPELL_SUMMON_ARCANE_PRISONER_1    = 45446,
    SPELL_SUMMON_ARCANE_PRISONER_2    = 45448
};

class spell_borean_tundra_arcane_prisoner_rescue : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_ARCANE_PRISONER_1, SPELL_SUMMON_ARCANE_PRISONER_2 });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_ARCANE_PRISONER_1, SPELL_SUMMON_ARCANE_PRISONER_2));
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_arcane_prisoner_rescue::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum WeaknessToLightning
{
    SPELL_POWER_OF_THE_STORM_ITEM      = 46432,
    SPELL_POWER_OF_THE_STORM           = 46424
};

class spell_borean_tundra_weakness_to_lightning_cast_on_master : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_weakness_to_lightning_cast_on_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_weakness_to_lightning_cancel_aura : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_POWER_OF_THE_STORM_ITEM });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_POWER_OF_THE_STORM_ITEM);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_weakness_to_lightning_cancel_aura::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_weakness_to_lightning_on_quest_complete : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_POWER_OF_THE_STORM });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_POWER_OF_THE_STORM);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_weakness_to_lightning_on_quest_complete::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_signal_alliance : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo(
        {
            uint32(spellInfo->Effects[EFFECT_0].CalcValue()),
            uint32(spellInfo->Effects[EFFECT_1].CalcValue())
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (caster->HasAura(uint32(GetSpellInfo()->Effects[EFFECT_0].CalcValue())))
            caster->CastSpell(caster, uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_signal_alliance::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

enum MasterAndServant
{
    SPELL_SUMMON_SCAVENGEBOT_004A8  = 46063,
    SPELL_SUMMON_SENTRYBOT_57K      = 46068,
    SPELL_SUMMON_DEFENDOTANK_66D    = 46058,
    SPELL_SUMMON_SCAVENGEBOT_005B6  = 46066,
    SPELL_SUMMON_55D_COLLECTATRON   = 46034,
    SPELL_ROBOT_KILL_CREDIT         = 46027,
    NPC_SCAVENGEBOT_004A8           = 25752,
    NPC_SENTRYBOT_57K               = 25753,
    NPC_DEFENDOTANK_66D             = 25758,
    NPC_SCAVENGEBOT_005B6           = 25792,
    NPC_55D_COLLECTATRON            = 25793
};

class spell_borean_tundra_ultrasonic_screwdriver : public SpellScript
{
    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER && GetCastItem();
    }

    bool Validate(SpellInfo const* /*spellEntry*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_SCAVENGEBOT_004A8,
            SPELL_SUMMON_SENTRYBOT_57K,
            SPELL_SUMMON_DEFENDOTANK_66D,
            SPELL_SUMMON_SCAVENGEBOT_005B6,
            SPELL_SUMMON_55D_COLLECTATRON,
            SPELL_ROBOT_KILL_CREDIT
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Item* castItem = GetCastItem();
        Unit* caster = GetCaster();
        if (Creature* target = GetHitCreature())
        {
            uint32 spellId = 0;
            switch (target->GetEntry())
            {
                case NPC_SCAVENGEBOT_004A8: spellId = SPELL_SUMMON_SCAVENGEBOT_004A8;    break;
                case NPC_SENTRYBOT_57K:     spellId = SPELL_SUMMON_SENTRYBOT_57K;        break;
                case NPC_DEFENDOTANK_66D:   spellId = SPELL_SUMMON_DEFENDOTANK_66D;      break;
                case NPC_SCAVENGEBOT_005B6: spellId = SPELL_SUMMON_SCAVENGEBOT_005B6;    break;
                case NPC_55D_COLLECTATRON:  spellId = SPELL_SUMMON_55D_COLLECTATRON;     break;
                default:
                    return;
            }
            caster->CastSpell(caster, spellId, castItem);
            caster->CastSpell(caster, SPELL_ROBOT_KILL_CREDIT, true);
            target->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_ultrasonic_screwdriver::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum ThePlainsOfNasam
{
    SPELL_DROP_WARSONG_LAND_MINE_1     = 45751,
    SPELL_DROP_WARSONG_LAND_MINE_2     = 45752,
    SPELL_DROP_WARSONG_LAND_MINE_3     = 45753,
    SPELL_DROP_WARSONG_LAND_MINE_4     = 45754,
    SPELL_DROP_WARSONG_LAND_MINE_5     = 45755,
    SPELL_DROP_WARSONG_LAND_MINE_6     = 45756,
    SPELL_DROP_WARSONG_LAND_MINE_7     = 47839,
    SPELL_DROP_WARSONG_LAND_MINE_8     = 45749
};

static constexpr std::array<uint32, 8> DropLandMineSpells =
{
    SPELL_DROP_WARSONG_LAND_MINE_1, SPELL_DROP_WARSONG_LAND_MINE_2, SPELL_DROP_WARSONG_LAND_MINE_3, SPELL_DROP_WARSONG_LAND_MINE_4,
    SPELL_DROP_WARSONG_LAND_MINE_5, SPELL_DROP_WARSONG_LAND_MINE_6, SPELL_DROP_WARSONG_LAND_MINE_7, SPELL_DROP_WARSONG_LAND_MINE_8
};

class spell_borean_tundra_land_mine_barrier : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(DropLandMineSpells);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        for (uint32 spells : DropLandMineSpells)
            caster->CastSpell(caster, spells);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_borean_tundra_land_mine_barrier::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum RescuingEvanor
{
    SPELL_AMBER_LEDGE_TO_BERYL_POINT     = 45883
};

class spell_borean_tundra_taxi_amber_ledge_to_beryl_point_platform : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_AMBER_LEDGE_TO_BERYL_POINT });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_AMBER_LEDGE_TO_BERYL_POINT);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_borean_tundra_taxi_amber_ledge_to_beryl_point_platform::AfterRemove, EFFECT_0, SPELL_AURA_MECHANIC_IMMUNITY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum SpringingTheTrap
{
    SPELL_COLDARRA_TO_TRANSITUS     = 46814
};

class spell_borean_tundra_taxi_coldarra_ledge_to_transitus_shield : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_COLDARRA_TO_TRANSITUS });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_COLDARRA_TO_TRANSITUS);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_borean_tundra_taxi_coldarra_ledge_to_transitus_shield::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum ReCursive
{
    SPELL_SUMMON_FIZZCRANK_SURVIVOR     = 46022
};

class spell_borean_tundra_re_cursive_transmatter_injection : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_FIZZCRANK_SURVIVOR });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_SUMMON_FIZZCRANK_SURVIVOR);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_re_cursive_transmatter_injection::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum DrakeHunt
{
    SPELL_RED_DRAGONBLOOD             = 46620,
    SPELL_CAPTURE_TRIGGER             = 46673,
    SPELL_DRAKE_TURN_IN               = 46696,
    SPELL_DRAKE_HATCHLING_SUBDUED     = 46691,
    SPELL_ROPE_BEAM                   = 46674
};

class spell_borean_tundra_drake_harpoon : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_RED_DRAGONBLOOD });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(GetTarget(), SPELL_RED_DRAGONBLOOD, true);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_borean_tundra_drake_harpoon::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_borean_tundra_red_dragonblood : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CAPTURE_TRIGGER });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (!GetTargetApplication()->GetRemoveMode().HasFlag(AuraRemoveFlags::Expired))
            return;

        if (Unit* caster = GetCaster())
            caster->CastSpell(GetTarget(), SPELL_CAPTURE_TRIGGER, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_borean_tundra_red_dragonblood::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_borean_tundra_drake_hatchling_subdued : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DRAKE_TURN_IN });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        EnumFlag<AuraRemoveFlags> mode = GetTargetApplication()->GetRemoveMode();
        if (mode.HasFlag(AuraRemoveFlags::Expired) || mode.HasFlag(AuraRemoveFlags::ByDeath))
        {
            if (Creature* creature = Object::ToCreature(GetCaster()))
                creature->DespawnOrUnsummon();
        }
        else if (mode.HasFlag(AuraRemoveFlags::ByDefault))
        {
            if (Unit* caster = GetCaster())
                GetTarget()->CastSpell(caster, SPELL_DRAKE_TURN_IN, true);
        }
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_borean_tundra_drake_hatchling_subdued::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_borean_tundra_strip_auras : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DRAKE_HATCHLING_SUBDUED, SPELL_ROPE_BEAM });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_DRAKE_HATCHLING_SUBDUED);
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_ROPE_BEAM);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_strip_auras::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_arcane_chains_character_force_cast : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_arcane_chains_character_force_cast::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_borean_tundra_abduction_quest_completion : public SpellScript
{
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Creature* creature = GetHitUnit()->ToCreature())
            creature->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_borean_tundra_abduction_quest_completion::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};
}

void AddSC_borean_tundra_cata()
{
    using namespace borean_tundra_cata;
    RegisterGameObjectAI(go_caribou_trap);
    RegisterGameObjectAI(go_mammoth_trap);
    RegisterSpellScript(spell_borean_tundra_shortening_blaster);
    RegisterSpellScript(spell_borean_tundra_magnataur_on_death_1);
    RegisterSpellScript(spell_borean_tundra_magnataur_on_death_2);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy);
    RegisterSpellScript(spell_borean_tundra_dispel_freed_soldier_debuff);
    RegisterSpellScript(spell_borean_tundra_deliver_kodo);
    RegisterSpellScript(spell_borean_tundra_kodo_delivered);
    RegisterSpellScript(spell_borean_tundra_neural_needle);
    RegisterSpellScript(spell_borean_tundra_prototype_neural_needle);
    RegisterSpellScript(spell_borean_tundra_arcane_prisoner_rescue);
    RegisterSpellScript(spell_borean_tundra_weakness_to_lightning_cast_on_master);
    RegisterSpellScript(spell_borean_tundra_weakness_to_lightning_cancel_aura);
    RegisterSpellScript(spell_borean_tundra_weakness_to_lightning_on_quest_complete);
    RegisterSpellScript(spell_borean_tundra_signal_alliance);
    RegisterSpellScript(spell_borean_tundra_ultrasonic_screwdriver);
    RegisterSpellScript(spell_borean_tundra_land_mine_barrier);
    RegisterSpellScript(spell_borean_tundra_taxi_amber_ledge_to_beryl_point_platform);
    RegisterSpellScript(spell_borean_tundra_taxi_coldarra_ledge_to_transitus_shield);
    RegisterSpellScript(spell_borean_tundra_re_cursive_transmatter_injection);
    RegisterSpellScript(spell_borean_tundra_drake_harpoon);
    RegisterSpellScript(spell_borean_tundra_red_dragonblood);
    RegisterSpellScript(spell_borean_tundra_drake_hatchling_subdued);
    RegisterSpellScript(spell_borean_tundra_strip_auras);
    RegisterSpellScript(spell_borean_tundra_arcane_chains_character_force_cast);
    RegisterSpellScript(spell_borean_tundra_abduction_quest_completion);
}
