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
#include "CombatAI.h"
#include "CreatureAIImpl.h"
#include "DB2Stores.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace dragonblight_cata
{
enum CommanderEligorDawnbringer
{
    MODEL_IMAGE_OF_KELTHUZAD           = 24787, // Image of Kel'Thuzad
    MODEL_IMAGE_OF_SAPPHIRON           = 24788, // Image of Sapphiron
    MODEL_IMAGE_OF_RAZUVIOUS           = 24799, // Image of Razuvious
    MODEL_IMAGE_OF_GOTHIK              = 24804, // Image of Gothik
    MODEL_IMAGE_OF_THANE               = 24802, // Image of Thane Korth'azz
    MODEL_IMAGE_OF_BLAUMEUX            = 24794, // Image of Lady Blaumeux
    MODEL_IMAGE_OF_ZELIEK              = 24800, // Image of Sir Zeliek
    MODEL_IMAGE_OF_PATCHWERK           = 24798, // Image of Patchwerk
    MODEL_IMAGE_OF_GROBBULUS           = 24792, // Image of Grobbulus
    MODEL_IMAGE_OF_THADDIUS            = 24801, // Image of Thaddius
    MODEL_IMAGE_OF_GLUTH               = 24803, // Image of Gluth
    MODEL_IMAGE_OF_ANUBREKHAN          = 24789, // Image of Anub'rekhan
    MODEL_IMAGE_OF_FAERLINA            = 24790, // Image of Faerlina
    MODEL_IMAGE_OF_MAEXXNA             = 24796, // Image of Maexxna
    MODEL_IMAGE_OF_NOTH                = 24797, // Image of Noth
    MODEL_IMAGE_OF_HEIGAN              = 24793, // Image of Heigan
    MODEL_IMAGE_OF_LOATHEB             = 24795, // Image of Loatheb

    NPC_IMAGE_OF_KELTHUZAD             = 27766, // Image of Kel'Thuzad
    NPC_IMAGE_OF_SAPPHIRON             = 27767, // Image of Sapphiron
    NPC_IMAGE_OF_RAZUVIOUS             = 27768, // Image of Razuvious
    NPC_IMAGE_OF_GOTHIK                = 27769, // Image of Gothik
    NPC_IMAGE_OF_THANE                 = 27770, // Image of Thane Korth'azz
    NPC_IMAGE_OF_BLAUMEUX              = 27771, // Image of Lady Blaumeux
    NPC_IMAGE_OF_ZELIEK                = 27772, // Image of Sir Zeliek
    NPC_IMAGE_OF_PATCHWERK             = 27773, // Image of Patchwerk
    NPC_IMAGE_OF_GROBBULUS             = 27774, // Image of Grobbulus
    NPC_IMAGE_OF_THADDIUS              = 27775, // Image of Thaddius
    NPC_IMAGE_OF_GLUTH                 = 27782, // Image of Gluth
    NPC_IMAGE_OF_ANUBREKHAN            = 27776, // Image of Anub'rekhan
    NPC_IMAGE_OF_FAERLINA              = 27777, // Image of Faerlina
    NPC_IMAGE_OF_MAEXXNA               = 27778, // Image of Maexxna
    NPC_IMAGE_OF_NOTH                  = 27779, // Image of Noth
    NPC_IMAGE_OF_HEIGAN                = 27780, // Image of Heigan
    NPC_IMAGE_OF_LOATHEB               = 27781, // Image of Loatheb

    NPC_INFANTRYMAN                    = 27160, // Add in case I randomize the spawning
    NPC_SENTINAL                       = 27162,
    NPC_BATTLE_MAGE                    = 27164,

    // Five platforms to choose from
    SAY_PINNACLE                       = 0,
    SAY_DEATH_KNIGHT_WING              = 1,
    SAY_ABOMINATION_WING               = 2,
    SAY_SPIDER_WING                    = 3,
    SAY_PLAGUE_WING                    = 4,
    // Used in all talks
    SAY_TALK_COMPLETE                  = 5,
    // Pinnacle of Naxxramas
    SAY_SAPPHIRON                      = 6,
    SAY_KELTHUZAD_1                    = 7,
    SAY_KELTHUZAD_2                    = 8,
    SAY_KELTHUZAD_3                    = 9,
    // Death knight wing of Naxxramas
    SAY_RAZUVIOUS                      = 10,
    SAY_GOTHIK                         = 11,
    SAY_DEATH_KNIGHTS_1                = 12,
    SAY_DEATH_KNIGHTS_2                = 13,
    SAY_DEATH_KNIGHTS_3                = 14,
    SAY_DEATH_KNIGHTS_4                = 15,
    // Blighted abomination wing of Naxxramas
    SAY_PATCHWERK                      = 16,
    SAY_GROBBULUS                      = 17,
    SAY_GLUTH                          = 18,
    SAY_THADDIUS                       = 19,
    // Accursed spider wing of Naxxramas
    SAY_ANUBREKHAN                     = 20,
    SAY_FAERLINA                       = 21,
    SAY_MAEXXNA                        = 22,
    // Dread plague wing of Naxxramas
    SAY_NOTH                           = 23,
    SAY_HEIGAN_1                       = 24,
    SAY_HEIGAN_2                       = 25,
    SAY_LOATHEB                        = 26,

    SPELL_HEROIC_IMAGE_CHANNEL         = 49519,

    EVENT_START_RANDOM                 = 1,
    EVENT_MOVE_TO_POINT                = 2,
    EVENT_TALK_COMPLETE                = 3,
    EVENT_GET_TARGETS                  = 4,
    EVENT_KELTHUZAD_2                  = 5,
    EVENT_KELTHUZAD_3                  = 6,
    EVENT_DEATH_KNIGHTS_2              = 7,
    EVENT_DEATH_KNIGHTS_3              = 8,
    EVENT_DEATH_KNIGHTS_4              = 9,
    EVENT_HEIGAN_2                     = 10
};

uint32 const AudienceMobs[3] = { NPC_INFANTRYMAN, NPC_SENTINAL, NPC_BATTLE_MAGE };

Position const PosTalkLocations[6] =
{
    { 3805.453f, -682.9075f, 222.2917f, 2.793398f }, // Pinnacle of Naxxramas
    { 3807.508f, -691.0882f, 221.9688f, 2.094395f }, // Death knight wing of Naxxramas
    { 3797.228f, -690.3555f, 222.5019f, 1.134464f }, // Blighted abomination wing of Naxxramas
    { 3804.038f, -672.3098f, 222.5019f, 4.578917f }, // Accursed spider wing of Naxxramas
    { 3815.097f, -680.2596f, 221.9777f, 2.86234f  }, // Dread plague wing of Naxxramas
    { 3798.05f,  -680.611f,  222.9825f, 6.038839f }, // Home
};

enum AlexstraszaWrGate
{
    // Quest
    QUEST_RETURN_TO_AG_A    = 12499,
    QUEST_RETURN_TO_AG_H    = 12500,

    // Movie
    MOVIE_ID_GATES          = 14
};

enum DefendingWyrmrestTemple
{
    // Quest data
    QUEST_DEFENDING_WYRMREST_TEMPLE          = 12372,
    GOSSIP_OPTION_ID                         = 0,
    MENU_ID                                  = 9568,

    // Spells data
    SPELL_CHARACTER_SCRIPT                   = 49213,
    SPELL_DEFENDER_ON_LOW_HEALTH_EMOTE       = 52421, // ID - 52421 Wyrmrest Defender: On Low Health Boss Emote to Controller - Random /self/
    SPELL_RENEW                              = 49263, // cast to heal drakes
    SPELL_WYRMREST_DEFENDER_MOUNT            = 49256,

    SPELL_SUMMON_WYRMREST_DEFENDER           = 49207,

    // Texts data
    WHISPER_MOUNTED                          = 0,
    BOSS_EMOTE_ON_LOW_HEALTH                 = 2,

    NPC_WYRMREST_TEMPLE_CREDIT               = 27698
};

class spell_dragonblight_defending_wyrmrest_temple_cast_from_gossip : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_WYRMREST_DEFENDER });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
       GetHitUnit()->CastSpell(GetHitUnit(), SPELL_SUMMON_WYRMREST_DEFENDER, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_dragonblight_defending_wyrmrest_temple_cast_from_gossip::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_dragonblight_defending_wyrmrest_temple_dummy : public SpellScript
{
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (GetHitCreature())
            if (Unit* caster = GetOriginalCaster())
                if (Vehicle* vehicle = caster->GetVehicleKit())
                    if (Unit* passenger = vehicle->GetPassenger(0))
                        if (Player* player = passenger->ToPlayer())
                            player->KilledMonsterCredit(NPC_WYRMREST_TEMPLE_CREDIT);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_dragonblight_defending_wyrmrest_temple_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum WarsongBattleStandard
{
    TEXT_TAUNT_1    = 25888,
    TEXT_TAUNT_2    = 25889,
    TEXT_TAUNT_3    = 25890,
    TEXT_TAUNT_4    = 25891,
    TEXT_TAUNT_5    = 25892,
    TEXT_TAUNT_6    = 25893,
    TEXT_TAUNT_7    = 25894
};

class spell_dragonblight_warsong_battle_standard : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return (sObjectMgr->GetBroadcastText(TEXT_TAUNT_1) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_2) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_3) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_4) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_5) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_6) != nullptr) &&
            (sObjectMgr->GetBroadcastText(TEXT_TAUNT_7) != nullptr);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->Unit::Say(RAND(TEXT_TAUNT_1, TEXT_TAUNT_2, TEXT_TAUNT_3, TEXT_TAUNT_4, TEXT_TAUNT_5, TEXT_TAUNT_6, TEXT_TAUNT_7), caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_warsong_battle_standard::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum MysteryOfTheInfinite
{
    SPELL_MIRROR_IMAGE_AURA         = 49889
};

class spell_dragonblight_moti_mirror_image_script_effect : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MIRROR_IMAGE_AURA });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_MIRROR_IMAGE_AURA);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_dragonblight_moti_mirror_image_script_effect::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_dragonblight_moti_hourglass_cast_see_invis_on_master : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (TempSummon* casterSummon = GetCaster()->ToTempSummon())
            if (Unit* summoner = casterSummon->GetSummoner())
                summoner->CastSpell(summoner, uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_moti_hourglass_cast_see_invis_on_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheChainGunAndYou
{
    TEXT_CALL_OUT_1    = 27083,
    TEXT_CALL_OUT_2    = 27084
};

class spell_dragonblight_call_out_injured_soldier : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return (sObjectMgr->GetBroadcastText(TEXT_CALL_OUT_1) != nullptr) && (sObjectMgr->GetBroadcastText(TEXT_CALL_OUT_2) != nullptr);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Vehicle* vehicle = GetCaster()->GetVehicleKit())
            if (Unit* passenger = vehicle->GetPassenger(0))
                passenger->Unit::Say(RAND(TEXT_CALL_OUT_1, TEXT_CALL_OUT_2), passenger);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_call_out_injured_soldier::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TortureTheTorturer
{
    WHISPER_TORTURE_1             = 1,
    WHISPER_TORTURE_2             = 2,
    WHISPER_TORTURE_3             = 3,
    WHISPER_TORTURE_4             = 4,
    WHISPER_TORTURE_5             = 5,
    WHISPER_TORTURE_RANDOM        = 6,

    SPELL_TORTURER_KILL_CREDIT    = 48607,
    SPELL_BRANDING_IRON_IMPACT    = 48614
};

class spell_dragonblight_high_executor_branding_iron : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TORTURER_KILL_CREDIT, SPELL_BRANDING_IRON_IMPACT });
    }

    void HandleWhisper()
    {
        Player* caster = GetCaster()->ToPlayer();
        Creature* target = GetHitCreature();
        if (!caster || !target)
            return;

        target->CastSpell(target, SPELL_BRANDING_IRON_IMPACT);

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
                    target->CastSpell(caster, SPELL_TORTURER_KILL_CREDIT);
                    break;
                case 6:
                    target->AI()->Talk(WHISPER_TORTURE_RANDOM, caster);
                    break;
                default:
                    return;
            }
        }
    }

    void Register() override
    {
        AfterHit.Register(&spell_dragonblight_high_executor_branding_iron::HandleWhisper);
    }
};

enum ThePerfectDissemblance
{
    SPELL_BANSHEES_MAGIC_MIRROR     = 48648
};

class spell_dragonblight_cancel_banshees_magic_mirror : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BANSHEES_MAGIC_MIRROR });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_BANSHEES_MAGIC_MIRROR);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_cancel_banshees_magic_mirror::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum AFallFromGrace
{
    SPELL_PRIEST_IMAGE_FEMALE     = 48761,
    SPELL_PRIEST_IMAGE_MALE       = 48763
};

class spell_dragonblight_scarlet_raven_priest_image_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PRIEST_IMAGE_FEMALE, SPELL_PRIEST_IMAGE_MALE });
    }

    void HandleAfterHit()
    {
        if (Player* target = GetHitUnit()->ToPlayer())
            target->CastSpell(target, target->getGender() == GENDER_FEMALE ? SPELL_PRIEST_IMAGE_FEMALE : SPELL_PRIEST_IMAGE_MALE);
    }

    void Register() override
    {
        AfterHit.Register(&spell_dragonblight_scarlet_raven_priest_image_master::HandleAfterHit);
    }
};

class spell_dragonblight_cancel_scarlet_raven_priest_image : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PRIEST_IMAGE_FEMALE, SPELL_PRIEST_IMAGE_MALE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_PRIEST_IMAGE_FEMALE);
        GetCaster()->RemoveAurasDueToSpell(SPELL_PRIEST_IMAGE_MALE);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_cancel_scarlet_raven_priest_image::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BombardTheBallistae
{
    SPELL_BALLISTA_BOW               = 48351,
    SPELL_BALLISTA_FRAME             = 48352,
    SPELL_BALLISTA_MISSILE           = 48353,
    SPELL_BALLISTA_WHEEL             = 48354,
    SPELL_BALLISTA_KNOCKBACK         = 52687
};

class spell_dragonblight_bombard_the_ballistae_fx_master : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BALLISTA_BOW,
            SPELL_BALLISTA_FRAME,
            SPELL_BALLISTA_MISSILE,
            SPELL_BALLISTA_WHEEL,
            SPELL_BALLISTA_KNOCKBACK
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_BALLISTA_BOW);
        caster->CastSpell(caster, SPELL_BALLISTA_FRAME);
        caster->CastSpell(caster, SPELL_BALLISTA_MISSILE);
        caster->CastSpell(caster, SPELL_BALLISTA_WHEEL);
        caster->CastSpell(caster, SPELL_BALLISTA_WHEEL);
        caster->CastSpell(caster, SPELL_BALLISTA_WHEEL);
        caster->CastSpell(caster, SPELL_BALLISTA_WHEEL);
        caster->CastSpell(caster, SPELL_BALLISTA_KNOCKBACK);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_bombard_the_ballistae_fx_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ProjectionsAndPlans
{
    SPELL_TELE_MOONREST_GARDENS    = 47324,
    SPELL_TELE_SURGE_NEEDLE        = 47325,

    AREA_SURGE_NEEDLE              = 4156,
    AREA_MOONREST_GARDENS          = 4157
};

class spell_dragonblight_surge_needle_teleporter : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TELE_MOONREST_GARDENS, SPELL_TELE_SURGE_NEEDLE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        switch (caster->GetAreaId())
        {
            case AREA_SURGE_NEEDLE:
                caster->CastSpell(caster, SPELL_TELE_MOONREST_GARDENS);
                break;
            case AREA_MOONREST_GARDENS:
                caster->CastSpell(caster, SPELL_TELE_SURGE_NEEDLE);
                break;
            default:
                break;
        }
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_surge_needle_teleporter::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_dragonblight_fill_blood_unholy_frost_gem : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), uint32(GetEffectValue()));
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_fill_blood_unholy_frost_gem::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum MessyBusiness
{
    SPELL_CORROSIVE_SPIT     = 47447
};

class spell_dragonblight_scrape_corrosive_spit : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CORROSIVE_SPIT });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_CORROSIVE_SPIT);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_scrape_corrosive_spit::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_dragonblight_corrosive_spit : public AuraScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->Effects[EFFECT_0].CalcValue()) });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTarget()->HasAura(GetSpellInfo()->Effects[EFFECT_0].CalcValue()))
            GetAura()->Remove();
    }

    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        if (GetTarget()->HasAura(GetSpellInfo()->Effects[EFFECT_0].CalcValue()))
        {
            PreventDefaultAction();
            GetAura()->Remove();
        }
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_dragonblight_corrosive_spit::AfterApply, EFFECT_1, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        OnEffectPeriodic.Register(&spell_dragonblight_corrosive_spit::PeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

enum TheFocusOnTheBeach
{
    SPELL_LEY_LINE_INFORMATION_01     = 47391
};

class spell_dragonblight_focus_on_the_beach_quest_completion_script : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEY_LINE_INFORMATION_01 });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_LEY_LINE_INFORMATION_01);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_focus_on_the_beach_quest_completion_script::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum AtopTheWoodlands
{
    SPELL_LEY_LINE_INFORMATION_02     = 47473
};

class spell_dragonblight_atop_the_woodlands_quest_completion_script : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEY_LINE_INFORMATION_02 });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_LEY_LINE_INFORMATION_02);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_atop_the_woodlands_quest_completion_script::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum TheEndOfTheLine
{
    SPELL_LEY_LINE_INFORMATION_03     = 47636
};

class spell_dragonblight_end_of_the_line_quest_completion_script : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEY_LINE_INFORMATION_03 });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_LEY_LINE_INFORMATION_03);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_dragonblight_end_of_the_line_quest_completion_script::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum SpiritualInsight
{
    SPELL_SPIRITUAL_INSIGHT     = 47189
};

class spell_dragonblight_toaluus_spiritual_incense : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SPIRITUAL_INSIGHT });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_SPIRITUAL_INSIGHT);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_dragonblight_toaluus_spiritual_incense::AfterApply, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum TheSacredAndTheCorrupt
{
    SPELL_KNOCKDOWN     = 13360
};

class spell_dragonblight_lich_king_zap_player : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_KNOCKDOWN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_KNOCKDOWN, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_dragonblight_lich_king_zap_player::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};
}

void AddSC_dragonblight_cata()
{
    using namespace dragonblight_cata;
    RegisterSpellScript(spell_dragonblight_defending_wyrmrest_temple_cast_from_gossip);
    RegisterSpellScript(spell_dragonblight_defending_wyrmrest_temple_dummy);
    RegisterSpellScript(spell_dragonblight_warsong_battle_standard);
    RegisterSpellScript(spell_dragonblight_moti_mirror_image_script_effect);
    RegisterSpellScript(spell_dragonblight_moti_hourglass_cast_see_invis_on_master);
    RegisterSpellScript(spell_dragonblight_call_out_injured_soldier);
    RegisterSpellScript(spell_dragonblight_high_executor_branding_iron);
    RegisterSpellScript(spell_dragonblight_cancel_banshees_magic_mirror);
    RegisterSpellScript(spell_dragonblight_scarlet_raven_priest_image_master);
    RegisterSpellScript(spell_dragonblight_cancel_scarlet_raven_priest_image);
    RegisterSpellScript(spell_dragonblight_bombard_the_ballistae_fx_master);
    RegisterSpellScript(spell_dragonblight_surge_needle_teleporter);
    RegisterSpellScript(spell_dragonblight_fill_blood_unholy_frost_gem);
    RegisterSpellScript(spell_dragonblight_scrape_corrosive_spit);
    RegisterSpellScript(spell_dragonblight_corrosive_spit);
    RegisterSpellScript(spell_dragonblight_focus_on_the_beach_quest_completion_script);
    RegisterSpellScript(spell_dragonblight_atop_the_woodlands_quest_completion_script);
    RegisterSpellScript(spell_dragonblight_end_of_the_line_quest_completion_script);
    RegisterSpellScript(spell_dragonblight_toaluus_spiritual_incense);
    RegisterSpellScript(spell_dragonblight_lich_king_zap_player);
}
