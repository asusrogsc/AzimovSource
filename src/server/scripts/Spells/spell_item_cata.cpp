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

/*
 * Scripts for spells with SPELLFAMILY_GENERIC spells used by items.
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "spell_item_".
 */

#include "ScriptMgr.h"
#include "Battleground.h"
#include "CommonPredicates.h"
#include "Containers.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "SkillDiscovery.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellScript.h"

namespace spell_item_cata
{
enum GenericData
{
    SPELL_ARCANITE_DRAGONLING           = 19804,
    SPELL_BATTLE_CHICKEN                = 13166,
    SPELL_MECHANICAL_DRAGONLING         = 4073,
    SPELL_MITHRIL_MECHANICAL_DRAGONLING = 12749,
};

enum AegisOfPreservation
{
    SPELL_AEGIS_HEAL   = 23781
};

enum LowerCityPrayerbook
{
    SPELL_BLESSING_OF_LOWER_CITY_DRUID      = 37878,
    SPELL_BLESSING_OF_LOWER_CITY_PALADIN    = 37879,
    SPELL_BLESSING_OF_LOWER_CITY_PRIEST     = 37880,
    SPELL_BLESSING_OF_LOWER_CITY_SHAMAN     = 37881
};

class spell_item_blessing_of_faith : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo
        ({
            SPELL_BLESSING_OF_LOWER_CITY_DRUID,
            SPELL_BLESSING_OF_LOWER_CITY_PALADIN,
            SPELL_BLESSING_OF_LOWER_CITY_PRIEST,
            SPELL_BLESSING_OF_LOWER_CITY_SHAMAN
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* unitTarget = GetHitUnit())
        {
            uint32 spellId = 0;
            switch (unitTarget->getClass())
            {
                case CLASS_DRUID:
                    spellId = SPELL_BLESSING_OF_LOWER_CITY_DRUID;
                    break;
                case CLASS_PALADIN:
                    spellId = SPELL_BLESSING_OF_LOWER_CITY_PALADIN;
                    break;
                case CLASS_PRIEST:
                    spellId = SPELL_BLESSING_OF_LOWER_CITY_PRIEST;
                    break;
                case CLASS_SHAMAN:
                    spellId = SPELL_BLESSING_OF_LOWER_CITY_SHAMAN;
                    break;
                default:
                    return; // ignore for non-healing classes
            }

            Unit* caster = GetCaster();
            caster->CastSpell(caster, spellId, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_blessing_of_faith::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum AlchemistStone
{
    SPELL_ALCHEMIST_STONE_EXTRA_HEAL       = 21399,
    SPELL_ALCHEMIST_STONE_EXTRA_MANA       = 21400
};

class spell_item_alchemist_stone : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_ALCHEMIST_STONE_EXTRA_HEAL,
            SPELL_ALCHEMIST_STONE_EXTRA_MANA
        });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        return eventInfo.GetDamageInfo()->GetSpellInfo()->SpellFamilyName == SPELLFAMILY_POTION;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        uint32 spellId = 0;
        int32 amount = int32(eventInfo.GetDamageInfo()->GetDamage() * 0.4f);

        if (eventInfo.GetDamageInfo()->GetSpellInfo()->HasEffect(SPELL_EFFECT_HEAL))
            spellId = SPELL_ALCHEMIST_STONE_EXTRA_HEAL;
        else if (eventInfo.GetDamageInfo()->GetSpellInfo()->HasEffect(SPELL_EFFECT_ENERGIZE))
            spellId = SPELL_ALCHEMIST_STONE_EXTRA_MANA;

        if (!spellId)
            return;

        Unit* caster = eventInfo.GetActionTarget();
        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(amount);
        caster->CastSpell(nullptr, spellId, args);
    }

    void Register() override
    {
        DoCheckProc.Register(&spell_item_alchemist_stone::CheckProc);
        OnEffectProc.Register(&spell_item_alchemist_stone::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

enum AngerCapacitor
{
    SPELL_MOTE_OF_ANGER             = 71432,
    SPELL_MANIFEST_ANGER_MAIN_HAND  = 71433,
    SPELL_MANIFEST_ANGER_OFF_HAND   = 71434
};

enum AuraOfMadness
{
    SPELL_SOCIOPATH         = 39511, // Sociopath: +35 strength(Paladin, Rogue, Druid, Warrior)
    SPELL_DELUSIONAL        = 40997, // Delusional: +70 attack power(Rogue, Hunter, Paladin, Warrior, Druid)
    SPELL_KLEPTOMANIA       = 40998, // Kleptomania: +35 agility(Warrior, Rogue, Paladin, Hunter, Druid)
    SPELL_MEGALOMANIA       = 40999, // Megalomania: +41 damage / healing(Druid, Shaman, Priest, Warlock, Mage, Paladin)
    SPELL_PARANOIA          = 41002, // Paranoia: +35 spell / melee / ranged crit strike rating(All classes)
    SPELL_MANIC             = 41005, // Manic: +35 haste(spell, melee and ranged) (All classes)
    SPELL_NARCISSISM        = 41009, // Narcissism: +35 intellect(Druid, Shaman, Priest, Warlock, Mage, Paladin, Hunter)
    SPELL_MARTYR_COMPLEX    = 41011, // Martyr Complex: +35 stamina(All classes)
    SPELL_DEMENTIA          = 41404, // Dementia: Every 5 seconds either gives you +5/-5%  damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)

    SPELL_DEMENTIA_POS      = 41406,
    SPELL_DEMENTIA_NEG      = 41409,

    SAY_MADNESS             = 21954
};

enum BrittleArmor
{
    SPELL_BRITTLE_ARMOR = 24575
};

class spell_item_brittle_armor : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BRITTLE_ARMOR });
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        GetHitUnit()->RemoveAuraFromStack(SPELL_BRITTLE_ARMOR);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_brittle_armor::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum BlessingOfAncientKings
{
    SPELL_PROTECTION_OF_ANCIENT_KINGS   = 64413
};

enum DeadlyPrecision
{
    SPELL_DEADLY_PRECISION = 71564
};

enum DeathbringersWill
{
    SPELL_STRENGTH_OF_THE_TAUNKA        = 71484, // +600 Strength
    SPELL_AGILITY_OF_THE_VRYKUL         = 71485, // +600 Agility
    SPELL_POWER_OF_THE_TAUNKA           = 71486, // +1200 Attack Power
    SPELL_AIM_OF_THE_IRON_DWARVES       = 71491, // +600 Critical
    SPELL_SPEED_OF_THE_VRYKUL           = 71492, // +600 Haste

    SPELL_AGILITY_OF_THE_VRYKUL_HERO    = 71556, // +700 Agility
    SPELL_POWER_OF_THE_TAUNKA_HERO      = 71558, // +1400 Attack Power
    SPELL_AIM_OF_THE_IRON_DWARVES_HERO  = 71559, // +700 Critical
    SPELL_SPEED_OF_THE_VRYKUL_HERO      = 71560, // +700 Haste
    SPELL_STRENGTH_OF_THE_TAUNKA_HERO   = 71561  // +700 Strength
};

enum GoblinBombDispenser
{
    SPELL_SUMMON_GOBLIN_BOMB       = 13258,
    SPELL_MALFUNCTION_EXPLOSION    = 13261
};

class spell_item_goblin_bomb_dispenser : public SpellScript
{
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_GOBLIN_BOMB, SPELL_MALFUNCTION_EXPLOSION });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Item* item = GetCastItem())
            GetCaster()->CastSpell(GetCaster(), roll_chance_i(95) ? SPELL_SUMMON_GOBLIN_BOMB : SPELL_MALFUNCTION_EXPLOSION, item);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_item_goblin_bomb_dispenser::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum GoblinWeatherMachine
{
    SPELL_PERSONALIZED_WEATHER1 = 46740,
    SPELL_PERSONALIZED_WEATHER2 = 46739,
    SPELL_PERSONALIZED_WEATHER3 = 46738,
    SPELL_PERSONALIZED_WEATHER4 = 46736
};

class spell_item_goblin_weather_machine : public SpellScript
{
    void HandleScript(SpellEffIndex /* effIndex */)
    {
        Unit* target = GetHitUnit();

        uint32 spellId = RAND(SPELL_PERSONALIZED_WEATHER1, SPELL_PERSONALIZED_WEATHER2, SPELL_PERSONALIZED_WEATHER3,
                              SPELL_PERSONALIZED_WEATHER4);
        target->CastSpell(target, spellId, GetSpell());
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_goblin_weather_machine::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Defibrillate
{
    SPELL_GOBLIN_JUMPER_CABLES_FAIL     = 8338,
    SPELL_GOBLIN_JUMPER_CABLES_XL_FAIL  = 23055
};

enum DesperateDefense
{
    SPELL_DESPERATE_RAGE    = 33898
};

enum DeviateFishSpells
{
    SPELL_SLEEPY            = 8064,
    SPELL_INVIGORATE        = 8065,
    SPELL_SHRINK            = 8066,
    SPELL_PARTY_TIME        = 8067,
    SPELL_HEALTHY_SPIRIT    = 8068,
    SPELL_REJUVENATION      = 8070
};

class PartyTimeEmoteEvent : public BasicEvent
{
public:
    PartyTimeEmoteEvent(Player* player) : _player(player) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        if (!_player->HasAura(SPELL_PARTY_TIME))
            return true;

        if (_player->isMoving())
            _player->HandleEmoteCommand(RAND(EMOTE_ONESHOT_APPLAUD, EMOTE_ONESHOT_LAUGH, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_CHICKEN));
        else
            _player->HandleEmoteCommand(RAND(EMOTE_ONESHOT_APPLAUD, EMOTE_ONESHOT_DANCESPECIAL, EMOTE_ONESHOT_LAUGH, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_CHICKEN));

        _player->m_Events.AddEventAtOffset(this, RAND(5s, 10s, 15s));

        return false; // do not delete re-added event in EventProcessor::Update
    }

private:
    Player* _player;
};

class spell_item_party_time : public AuraScript
{
    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetOwner()->ToPlayer();
        if (!player)
            return;

        player->m_Events.AddEventAtOffset(new PartyTimeEmoteEvent(player), RAND(5s, 10s, 15s));
    }

    void Register() override
    {
        OnEffectApply.Register(&spell_item_party_time::HandleEffectApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum DireBrew
{
    MODEL_CLASS_CLOTH_MALE         = 25229,
    MODEL_CLASS_CLOTH_FEMALE       = 25233,
    MODEL_CLASS_LEATHER_MALE       = 25230,
    MODEL_CLASS_LEATHER_FEMALE     = 25234,
    MODEL_CLASS_MAIL_MALE          = 25231,
    MODEL_CLASS_MAIL_FEMALE        = 25235,
    MODEL_CLASS_PLATE_MALE         = 25232,
    MODEL_CLASS_PLATE_FEMALE       = 25236
};

class spell_item_dire_brew : public AuraScript
{
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();

        uint32 model = 0;
        uint8 gender = target->getGender();
        // 4.3.4 : pas de ArmorTypeMask dans ChrClasses.dbc, armure de la classe au niveau maximum
        switch (target->getClass())
        {
            case CLASS_WARRIOR: case CLASS_PALADIN: case CLASS_DEATH_KNIGHT:
                model = gender == GENDER_MALE ? MODEL_CLASS_PLATE_MALE : MODEL_CLASS_PLATE_FEMALE;
                break;
            case CLASS_HUNTER: case CLASS_SHAMAN:
                model = gender == GENDER_MALE ? MODEL_CLASS_MAIL_MALE : MODEL_CLASS_MAIL_FEMALE;
                break;
            case CLASS_ROGUE: case CLASS_DRUID:
                model = gender == GENDER_MALE ? MODEL_CLASS_LEATHER_MALE : MODEL_CLASS_LEATHER_FEMALE;
                break;
            default:
                model = gender == GENDER_MALE ? MODEL_CLASS_CLOTH_MALE : MODEL_CLASS_CLOTH_FEMALE;
                break;
        }

        if (model)
            target->SetDisplayId(model);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_item_dire_brew::AfterApply, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};

enum DiscerningEyeBeastMisc
{
    SPELL_DISCERNING_EYE_BEAST = 59914
};

class spell_item_discerning_eye_beast_dummy : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DISCERNING_EYE_BEAST });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        eventInfo.GetActor()->CastSpell(nullptr, SPELL_DISCERNING_EYE_BEAST, aurEff);
    }

    void Register() override
    {
        OnEffectProc.Register(&spell_item_discerning_eye_beast_dummy::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

class spell_item_extract_gas : public AuraScript
{
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();

        // move loot to player inventory and despawn target
        if (GetCaster() && GetCaster()->GetTypeId() == TYPEID_PLAYER &&
            GetTarget()->GetTypeId() == TYPEID_UNIT &&
            GetTarget()->ToCreature()->GetCreatureTemplate()->type == CREATURE_TYPE_GAS_CLOUD)
        {
            Player* player = GetCaster()->ToPlayer();
            Creature* creature = GetTarget()->ToCreature();
            uint32 const skinLootId = creature->GetCreatureTemplate()->SkinLootId;
            // missing lootid has been reported on startup - just return
            if (!skinLootId)
                return;

            player->AutoStoreLoot(skinLootId, LootTemplates_Skinning, true);
            creature->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_item_extract_gas::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

enum FateRuneOfUnsurpassedVigor
{
    SPELL_UNSURPASSED_VIGOR = 25733
};

enum Feast
{
    TEXT_GREAT_FEAST        = 31843,
    TEXT_FISH_FEAST         = 31844,
    TEXT_GIGANTIC_FEAST     = 31846,
    TEXT_SMALL_FEAST        = 31845,
    TEXT_BOUNTIFUL_FEAST    = 35153
};

enum FlaskOfTheNorthSpells
{
    SPELL_FLASK_OF_THE_NORTH_SP = 67016,
    SPELL_FLASK_OF_THE_NORTH_AP = 67017,
    SPELL_FLASK_OF_THE_NORTH_STR = 67018,
};

enum FrozenShadoweave
{
    SPELL_SHADOWMEND    = 39373
};

enum GnomishDeathRay
{
    SPELL_GNOMISH_DEATH_RAY_SELF = 13493,
    SPELL_GNOMISH_DEATH_RAY_TARGET = 13279,
};

enum HarmPreventionBelt
{
    SPELL_FORCEFIELD_COLLAPSE = 13235
};

enum Heartpierce
{
    SPELL_INVIGORATION_MANA         = 71881,
    SPELL_INVIGORATION_ENERGY       = 71882,
    SPELL_INVIGORATION_RAGE         = 71883,
    SPELL_INVIGORATION_RP           = 71884,

    SPELL_INVIGORATION_RP_HERO      = 71885,
    SPELL_INVIGORATION_RAGE_HERO    = 71886,
    SPELL_INVIGORATION_ENERGY_HERO  = 71887,
    SPELL_INVIGORATION_MANA_HERO    = 71888
};

enum HourglassSand
{
    SPELL_BROOD_AFFLICTION_BRONZE    = 23170
};

class spell_item_hourglass_sand : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BROOD_AFFLICTION_BRONZE });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_BROOD_AFFLICTION_BRONZE);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_item_hourglass_sand::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum MakeAWish
{
    SPELL_MR_PINCHYS_BLESSING       = 33053,
    SPELL_SUMMON_MIGHTY_MR_PINCHY   = 33057,
    SPELL_SUMMON_FURIOUS_MR_PINCHY  = 33059,
    SPELL_TINY_MAGICAL_CRAWDAD      = 33062,
    SPELL_MR_PINCHYS_GIFT           = 33064,
};

enum MarkOfConquest
{
    SPELL_MARK_OF_CONQUEST_ENERGIZE     = 39599
};

enum MercurialShield
{
    SPELL_MERCURIAL_SHIELD = 26464
};

class spell_item_mercurial_shield : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MERCURIAL_SHIELD });
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        GetHitUnit()->RemoveAuraFromStack(SPELL_MERCURIAL_SHIELD);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_mercurial_shield::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum MingoFortune
{
    SPELL_CREATE_FORTUNE_1       = 40804,
    SPELL_CREATE_FORTUNE_2       = 40805,
    SPELL_CREATE_FORTUNE_3       = 40806,
    SPELL_CREATE_FORTUNE_4       = 40807,
    SPELL_CREATE_FORTUNE_5       = 40808,
    SPELL_CREATE_FORTUNE_6       = 40809,
    SPELL_CREATE_FORTUNE_7       = 40908,
    SPELL_CREATE_FORTUNE_8       = 40910,
    SPELL_CREATE_FORTUNE_9       = 40911,
    SPELL_CREATE_FORTUNE_10      = 40912,
    SPELL_CREATE_FORTUNE_11      = 40913,
    SPELL_CREATE_FORTUNE_12      = 40914,
    SPELL_CREATE_FORTUNE_13      = 40915,
    SPELL_CREATE_FORTUNE_14      = 40916,
    SPELL_CREATE_FORTUNE_15      = 40918,
    SPELL_CREATE_FORTUNE_16      = 40919,
    SPELL_CREATE_FORTUNE_17      = 40920,
    SPELL_CREATE_FORTUNE_18      = 40921,
    SPELL_CREATE_FORTUNE_19      = 40922,
    SPELL_CREATE_FORTUNE_20      = 40923
};

std::array<uint32, 20> const CreateFortuneSpells =
{
    SPELL_CREATE_FORTUNE_1, SPELL_CREATE_FORTUNE_2, SPELL_CREATE_FORTUNE_3, SPELL_CREATE_FORTUNE_4, SPELL_CREATE_FORTUNE_5,
    SPELL_CREATE_FORTUNE_6, SPELL_CREATE_FORTUNE_7, SPELL_CREATE_FORTUNE_8, SPELL_CREATE_FORTUNE_9, SPELL_CREATE_FORTUNE_10,
    SPELL_CREATE_FORTUNE_11, SPELL_CREATE_FORTUNE_12, SPELL_CREATE_FORTUNE_13, SPELL_CREATE_FORTUNE_14, SPELL_CREATE_FORTUNE_15,
    SPELL_CREATE_FORTUNE_16, SPELL_CREATE_FORTUNE_17, SPELL_CREATE_FORTUNE_18, SPELL_CREATE_FORTUNE_19, SPELL_CREATE_FORTUNE_20
};

enum NecroticTouch
{
    SPELL_ITEM_NECROTIC_TOUCH_PROC  = 71879
};

enum NetOMaticSpells
{
    SPELL_NET_O_MATIC_TRIGGERED1 = 16566,
    SPELL_NET_O_MATIC_TRIGGERED2 = 13119,
    SPELL_NET_O_MATIC_TRIGGERED3 = 13099,
};

enum NoggenfoggerElixirSpells
{
    SPELL_NOGGENFOGGER_ELIXIR_TRIGGERED1 = 16595,
    SPELL_NOGGENFOGGER_ELIXIR_TRIGGERED2 = 16593,
    SPELL_NOGGENFOGGER_ELIXIR_TRIGGERED3 = 16591,
};

enum PersistentShieldMisc
{
    SPELL_PERSISTENT_SHIELD_TRIGGERED = 26470
};

enum PetHealing
{
    SPELL_HEALTH_LINK   = 37382
};

enum PowerCircle
{
    SPELL_LIMITLESS_POWER = 45044
};

enum SavoryDeviateDelight
{
    SPELL_FLIP_OUT_MALE     = 8219,
    SPELL_FLIP_OUT_FEMALE   = 8220,
    SPELL_YAAARRRR_MALE     = 8221,
    SPELL_YAAARRRR_FEMALE   = 8222,
};

enum ScrollOfRecall
{
    SPELL_SCROLL_OF_RECALL_I                = 48129,
    SPELL_SCROLL_OF_RECALL_II               = 60320,
    SPELL_SCROLL_OF_RECALL_III              = 60321,
    SPELL_LOST                              = 60444,
    SPELL_SCROLL_OF_RECALL_FAIL_ALLIANCE_1  = 60323,
    SPELL_SCROLL_OF_RECALL_FAIL_HORDE_1     = 60328,
};

enum TransporterSpells
{
    SPELL_EVIL_TWIN                         = 23445,
    SPELL_TRANSPORTER_MALFUNCTION_FIRE      = 23449,
    SPELL_TRANSPORTER_MALFUNCTION_SMALLER   = 36893,
    SPELL_TRANSPORTER_MALFUNCTION_BIGGER    = 36895,
    SPELL_TRANSPORTER_MALFUNCTION_CHICKEN   = 36940,
    SPELL_TRANSFORM_HORDE                   = 36897,
    SPELL_TRANSFORM_ALLIANCE                = 36899,
    SPELL_SOUL_SPLIT_EVIL                   = 36900,
    SPELL_SOUL_SPLIT_GOOD                   = 36901
};

class spell_item_dimensional_ripper_everlook : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRANSPORTER_MALFUNCTION_FIRE, SPELL_EVIL_TWIN });
    }

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        int32 r = irand(0, 119);
        if (r <= 70)                               // 7/12 success
            return;

        Unit* caster = GetCaster();

        if (r < 100)                              // 4/12 evil twin
            caster->CastSpell(caster, SPELL_EVIL_TWIN, true);
        else                                      // 1/12 fire
            caster->CastSpell(caster, SPELL_TRANSPORTER_MALFUNCTION_FIRE, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_dimensional_ripper_everlook::HandleScript, EFFECT_0, SPELL_EFFECT_TELEPORT_UNITS);
    }
};

class spell_item_ultrasafe_transporter : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
            {
                SPELL_TRANSPORTER_MALFUNCTION_SMALLER,
                SPELL_TRANSPORTER_MALFUNCTION_BIGGER,
                SPELL_SOUL_SPLIT_EVIL,
                SPELL_SOUL_SPLIT_GOOD,
                SPELL_TRANSFORM_HORDE,
                SPELL_TRANSFORM_ALLIANCE,
                SPELL_TRANSPORTER_MALFUNCTION_CHICKEN,
                SPELL_EVIL_TWIN,
            });
    }

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        if (!roll_chance_i(50)) // 50% success
            return;

        Unit* caster = GetCaster();

        uint32 spellId = 0;
        switch (urand(0, 6))
        {
            case 0:
                spellId = SPELL_TRANSPORTER_MALFUNCTION_SMALLER;
                break;
            case 1:
                spellId = SPELL_TRANSPORTER_MALFUNCTION_BIGGER;
                break;
            case 2:
                spellId = SPELL_SOUL_SPLIT_EVIL;
                break;
            case 3:
                spellId = SPELL_SOUL_SPLIT_GOOD;
                break;
            case 4:
                if (caster->ToPlayer()->GetTeamId() == TEAM_ALLIANCE)
                    spellId = SPELL_TRANSFORM_HORDE;
                else
                    spellId = SPELL_TRANSFORM_ALLIANCE;
                break;
            case 5:
                spellId = SPELL_TRANSPORTER_MALFUNCTION_CHICKEN;
                break;
            case 6:
                spellId = SPELL_EVIL_TWIN;
                break;
            default:
                break;
        }

        caster->CastSpell(caster, spellId, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_ultrasafe_transporter::HandleScript, EFFECT_0, SPELL_EFFECT_TELEPORT_UNITS);
    }
};

class spell_item_dimensional_ripper_area52 : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
            {
                SPELL_TRANSPORTER_MALFUNCTION_BIGGER,
                SPELL_SOUL_SPLIT_EVIL,
                SPELL_SOUL_SPLIT_GOOD,
                SPELL_TRANSFORM_HORDE,
                SPELL_TRANSFORM_ALLIANCE
            });
    }

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        if (!roll_chance_i(50)) // 50% success
            return;

        Unit* caster = GetCaster();

        uint32 spellId = 0;
        switch (urand(0, 3))
        {
            case 0:
                spellId = SPELL_TRANSPORTER_MALFUNCTION_BIGGER;
                break;
            case 1:
                spellId = SPELL_SOUL_SPLIT_EVIL;
                break;
            case 2:
                spellId = SPELL_SOUL_SPLIT_GOOD;
                break;
            case 3:
                if (caster->ToPlayer()->GetTeamId() == TEAM_ALLIANCE)
                    spellId = SPELL_TRANSFORM_HORDE;
                else
                    spellId = SPELL_TRANSFORM_ALLIANCE;
                break;
            default:
                break;
        }

        caster->CastSpell(caster, spellId, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_dimensional_ripper_area52::HandleScript, EFFECT_0, SPELL_EFFECT_TELEPORT_UNITS);
    }
};

enum ShadowsFate
{
    SPELL_SOUL_FEAST        = 71203,
    NPC_SINDRAGOSA          = 36853
};

enum Shadowmourne
{
    SPELL_SHADOWMOURNE_CHAOS_BANE_DAMAGE    = 71904,
    SPELL_SHADOWMOURNE_SOUL_FRAGMENT        = 71905,
    SPELL_SHADOWMOURNE_VISUAL_LOW           = 72521,
    SPELL_SHADOWMOURNE_VISUAL_HIGH          = 72523,
    SPELL_SHADOWMOURNE_CHAOS_BANE_BUFF      = 73422,
};

enum SixDemonBagSpells
{
    SPELL_FROSTBOLT                 = 11538,
    SPELL_POLYMORPH                 = 14621,
    SPELL_SUMMON_FELHOUND_MINION    = 14642,
    SPELL_FIREBALL                  = 15662,
    SPELL_CHAIN_LIGHTNING           = 21179,
    SPELL_ENVELOPING_WINDS          = 25189,
};

enum SwiftHandJusticeMisc
{
    SPELL_SWIFT_HAND_OF_JUSTICE_HEAL = 59913
};

enum UnderbellyElixirSpells
{
    SPELL_UNDERBELLY_ELIXIR_TRIGGERED1 = 59645,
    SPELL_UNDERBELLY_ELIXIR_TRIGGERED2 = 59831,
    SPELL_UNDERBELLY_ELIXIR_TRIGGERED3 = 59843,
};

enum WormholeGeneratorPandariaSpell
{
    SPELL_WORMHOLE_PANDARIA_ISLE_OF_RECKONING   = 126756,
    SPELL_WORMHOLE_PANDARIA_KUNLAI_UNDERWATER   = 126757,
    SPELL_WORMHOLE_PANDARIA_SRA_VESS            = 126758,
    SPELL_WORMHOLE_PANDARIA_RIKKITUN_VILLAGE    = 126759,
    SPELL_WORMHOLE_PANDARIA_ZANVESS_TREE        = 126760,
    SPELL_WORMHOLE_PANDARIA_ANGLERS_WHARF       = 126761,
    SPELL_WORMHOLE_PANDARIA_CRANE_STATUE        = 126762,
    SPELL_WORMHOLE_PANDARIA_EMPERORS_OMEN       = 126763,
    SPELL_WORMHOLE_PANDARIA_WHITEPETAL_LAKE     = 126764,
};

uint32 const WormholeTargetLocations[] =
{
    SPELL_WORMHOLE_PANDARIA_ISLE_OF_RECKONING,
    SPELL_WORMHOLE_PANDARIA_KUNLAI_UNDERWATER,
    SPELL_WORMHOLE_PANDARIA_SRA_VESS,
    SPELL_WORMHOLE_PANDARIA_RIKKITUN_VILLAGE,
    SPELL_WORMHOLE_PANDARIA_ZANVESS_TREE,
    SPELL_WORMHOLE_PANDARIA_ANGLERS_WHARF,
    SPELL_WORMHOLE_PANDARIA_CRANE_STATUE,
    SPELL_WORMHOLE_PANDARIA_EMPERORS_OMEN,
    SPELL_WORMHOLE_PANDARIA_WHITEPETAL_LAKE
};

enum AirRifleSpells
{
    SPELL_AIR_RIFLE_HOLD_VISUAL = 65582,
    SPELL_AIR_RIFLE_SHOOT       = 67532,
    SPELL_AIR_RIFLE_SHOOT_SELF  = 65577,
};

enum GiftOfTheHarvester
{
    NPC_GHOUL   = 28845,
    MAX_GHOULS  = 5,
};

enum Sinkholes
{
    NPC_SOUTH_SINKHOLE      = 25664,
    NPC_NORTHEAST_SINKHOLE  = 25665,
    NPC_NORTHWEST_SINKHOLE  = 25666,
};

enum VanquishedClutchesSpells
{
    SPELL_CRUSHER       = 64982,
    SPELL_CONSTRICTOR   = 64983,
    SPELL_CORRUPTOR     = 64984,
};

enum AshbringerSounds
{
    SOUND_ASHBRINGER_1  = 8906,                             // "I was pure once"
    SOUND_ASHBRINGER_2  = 8907,                             // "Fought for righteousness"
    SOUND_ASHBRINGER_3  = 8908,                             // "I was once called Ashbringer"
    SOUND_ASHBRINGER_4  = 8920,                             // "Betrayed by my order"
    SOUND_ASHBRINGER_5  = 8921,                             // "Destroyed by Kel'Thuzad"
    SOUND_ASHBRINGER_6  = 8922,                             // "Made to serve"
    SOUND_ASHBRINGER_7  = 8923,                             // "My son watched me die"
    SOUND_ASHBRINGER_8  = 8924,                             // "Crusades fed his rage"
    SOUND_ASHBRINGER_9  = 8925,                             // "Truth is unknown to him"
    SOUND_ASHBRINGER_10 = 8926,                             // "Scarlet Crusade  is pure no longer"
    SOUND_ASHBRINGER_11 = 8927,                             // "Balnazzar's crusade corrupted my son"
    SOUND_ASHBRINGER_12 = 8928,                             // "Kill them all!"
};

enum MagicEater
{
    SPELL_WILD_MAGIC                             = 58891,
    SPELL_WELL_FED_1                             = 57288,
    SPELL_WELL_FED_2                             = 57139,
    SPELL_WELL_FED_3                             = 57111,
    SPELL_WELL_FED_4                             = 57286,
    SPELL_WELL_FED_5                             = 57291,
};

enum NighInvulnerability
{
    SPELL_NIGH_INVULNERABILITY                  = 30456,
    SPELL_COMPLETE_VULNERABILITY                = 30457,
};

enum Poultryzer
{
    SPELL_POULTRYIZER_SUCCESS    = 30501,
    SPELL_POULTRYIZER_BACKFIRE   = 30504,
};

enum DemonBroiledSurprise
{
    QUEST_SUPER_HOT_STEW                    = 11379,
    SPELL_CREATE_DEMON_BROILED_SURPRISE     = 43753,
    NPC_ABYSSAL_FLAMEBRINGER                = 19973,
};

enum CompleteRaptorCapture
{
    SPELL_RAPTOR_CAPTURE_CREDIT     = 42337,
};

enum ImpaleLeviroth
{
    NPC_LEVIROTH                = 26452,
    SPELL_LEVIROTH_SELF_IMPALE  = 49882
};

enum LifegivingGem
{
    SPELL_GIFT_OF_LIFE_1    = 23782,
    SPELL_GIFT_OF_LIFE_2    = 23783
};

class spell_item_lifegiving_gem : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_GIFT_OF_LIFE_1, SPELL_GIFT_OF_LIFE_2 });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_GIFT_OF_LIFE_1, true);
        caster->CastSpell(caster, SPELL_GIFT_OF_LIFE_2, true);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_item_lifegiving_gem::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

enum NitroBoosts
{
    SPELL_NITRO_BOOSTS_SUCCESS       = 54861,
    SPELL_NITRO_BOOSTS_BACKFIRE      = 54621,
    SPELL_NITRO_BOOSTS_PARACHUTE     = 54649,
};

enum RocketBoots
{
    SPELL_ROCKET_BOOTS_PROC      = 30452,
};

class spell_item_runic_healing_injector : public SpellScript
{
    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    void HandleHeal(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            if (caster->HasSkill(SKILL_ENGINEERING))
                SetHitHeal(GetHitHeal() * 1.25f);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_runic_healing_injector::HandleHeal, EFFECT_0, SPELL_EFFECT_HEAL);
    }
};

enum PygmyOil
{
    SPELL_PYGMY_OIL_PYGMY_AURA      = 53806,
    SPELL_PYGMY_OIL_SMALLER_AURA    = 53805,
};

enum ChickenCover
{
    SPELL_CHICKEN_NET               = 51959,
    SPELL_CAPTURE_CHICKEN_ESCAPE    = 51037,
    QUEST_CHICKEN_PARTY             = 12702,
    QUEST_FLOWN_THE_COOP            = 12532,
};

enum GreatmothersSoulcather
{
    SPELL_FORCE_CAST_SUMMON_GNOME_SOUL = 46486,
};

enum ShardOfTheScale
{
    SPELL_PURIFIED_CAUTERIZING_HEAL = 69733,
    SPELL_PURIFIED_SEARING_FLAMES   = 69729,

    SPELL_SHINY_CAUTERIZING_HEAL    = 69734,
    SPELL_SHINY_SEARING_FLAMES      = 69730
};

enum SoulPreserver
{
    SPELL_SOUL_PRESERVER_DRUID       = 60512,
    SPELL_SOUL_PRESERVER_PALADIN     = 60513,
    SPELL_SOUL_PRESERVER_PRIEST      = 60514,
    SPELL_SOUL_PRESERVER_SHAMAN      = 60515,
};

enum ExaltedSunwellNeck
{
    SPELL_LIGHTS_WRATH      = 45479, // Light's Wrath if Exalted by Aldor
    SPELL_ARCANE_BOLT       = 45429, // Arcane Bolt if Exalted by Scryers

    SPELL_LIGHTS_STRENGTH   = 45480, // Light's Strength if Exalted by Aldor
    SPELL_ARCANE_STRIKE     = 45428, // Arcane Strike if Exalted by Scryers

    SPELL_LIGHTS_WARD       = 45432, // Light's Ward if Exalted by Aldor
    SPELL_ARCANE_INSIGHT    = 45431, // Arcane Insight if Exalted by Scryers

    SPELL_LIGHTS_SALVATION  = 45478, // Light's Salvation if Exalted by Aldor
    SPELL_ARCANE_SURGE      = 45430, // Arcane Surge if Exalted by Scryers

    FACTION_ALDOR           = 932,
    FACTION_SCRYERS         = 934
};

enum DeathChoiceSpells
{
    SPELL_DEATH_CHOICE_NORMAL_AURA      = 67702,
    SPELL_DEATH_CHOICE_NORMAL_AGILITY   = 67703,
    SPELL_DEATH_CHOICE_NORMAL_STRENGTH  = 67708,
    SPELL_DEATH_CHOICE_HEROIC_AURA      = 67771,
    SPELL_DEATH_CHOICE_HEROIC_AGILITY   = 67772,
    SPELL_DEATH_CHOICE_HEROIC_STRENGTH  = 67773
};

enum TrinketStackSpells
{
    SPELL_LIGHTNING_CAPACITOR_AURA              = 37657,  // Lightning Capacitor
    SPELL_LIGHTNING_CAPACITOR_STACK             = 37658,
    SPELL_LIGHTNING_CAPACITOR_TRIGGER           = 37661,
    SPELL_THUNDER_CAPACITOR_AURA                = 54841,  // Thunder Capacitor
    SPELL_THUNDER_CAPACITOR_STACK               = 54842,
    SPELL_THUNDER_CAPACITOR_TRIGGER             = 54843,
    SPELL_TOC25_CASTER_TRINKET_NORMAL_AURA      = 67712,  // Item - Coliseum 25 Normal Caster Trinket
    SPELL_TOC25_CASTER_TRINKET_NORMAL_STACK     = 67713,
    SPELL_TOC25_CASTER_TRINKET_NORMAL_TRIGGER   = 67714,
    SPELL_TOC25_CASTER_TRINKET_HEROIC_AURA      = 67758,  // Item - Coliseum 25 Heroic Caster Trinket
    SPELL_TOC25_CASTER_TRINKET_HEROIC_STACK     = 67759,
    SPELL_TOC25_CASTER_TRINKET_HEROIC_TRIGGER   = 67760,
};

enum DarkmoonCardSpells
{
    SPELL_DARKMOON_CARD_STRENGTH        = 60229,
    SPELL_DARKMOON_CARD_AGILITY         = 60233,
    SPELL_DARKMOON_CARD_INTELLECT       = 60234,
    SPELL_DARKMOON_CARD_VERSATILITY     = 60235,
};

enum ManaDrainSpells
{
    SPELL_MANA_DRAIN_ENERGIZE = 29471,
    SPELL_MANA_DRAIN_LEECH    = 27526
};

enum TauntFlag
{
    SPELL_TAUNT_FLAG    = 51657,

    EMOTE_PLANTS_FLAG   = 28008
};

enum MirrensDrinkingHat
{
    SPELL_LOCH_MODAN_LAGER      = 29827,
    SPELL_STOUTHAMMER_LITE      = 29828,
    SPELL_AERIE_PEAK_PALE_ALE   = 29829
};

class spell_item_mirrens_drinking_hat : public SpellScript
{
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        uint32 spellId = 0;
        switch (urand(1, 6))
        {
            case 1:
            case 2:
            case 3:
                spellId = SPELL_LOCH_MODAN_LAGER; break;
            case 4:
            case 5:
                spellId = SPELL_STOUTHAMMER_LITE; break;
            case 6:
                spellId = SPELL_AERIE_PEAK_PALE_ALE; break;
            default:
                return;
        }

        Unit* caster = GetCaster();
        caster->CastSpell(caster, spellId, GetSpell());
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_mirrens_drinking_hat::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum MindControlCap
{
    ROLL_CHANCE_DULLARD             = 32,
    ROLL_CHANCE_NO_BACKFIRE         = 95,
    SPELL_GNOMISH_MIND_CONTROL_CAP  = 13181,
    SPELL_DULLARD                   = 67809
};

enum UniversalRemote
{
    SPELL_CONTROL_MACHINE       = 8345,
    SPELL_MOBILITY_MALFUNCTION  = 8346,
    SPELL_TARGET_LOCK           = 8347
};

enum ZandalarianCharms
{
    SPELL_UNSTABLE_POWER_AURA_STACK     = 24659,
    SPELL_RESTLESS_STRENGTH_AURA_STACK  = 24662
};

enum AuraProcRemoveSpells
{
    SPELL_TALISMAN_OF_ASCENDANCE    = 28200,
    SPELL_JOM_GABBAR                = 29602,
    SPELL_BATTLE_TRANCE             = 45040,
    SPELL_WORLD_QUELLER_FOCUS       = 90900,
    SPELL_BRUTAL_KINSHIP_1          = 144671,
    SPELL_BRUTAL_KINSHIP_2          = 145738
};

enum TitaniumSealOfDalaranTexts
{
    TEXT_TSOD_COIN_TOSS     = 32638,
    TEXT_TSOD_FLIPPED_HEADS = 32663,
    TEXT_TSOD_FLIPPED_TAILS = 32664
};

class spell_item_titanium_seal_of_dalaran_toss : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return (sObjectMgr->GetBroadcastText(TEXT_TSOD_COIN_TOSS) != nullptr);
    }

    void RelocateHeight(SpellDestination& dest)
    {
        dest.RelocateOffset({ 0.0f, 0.0f, 20.0f });
    }

    void TriggerEmote(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->TextEmote(TEXT_TSOD_COIN_TOSS, caster);
    }

    void Register() override
    {
        OnDestinationTargetSelect.Register(&spell_item_titanium_seal_of_dalaran_toss::RelocateHeight, EFFECT_0, TARGET_DEST_CASTER);
        OnEffectLaunch.Register(&spell_item_titanium_seal_of_dalaran_toss::TriggerEmote, EFFECT_0, SPELL_EFFECT_TRIGGER_MISSILE);
    }
};

class spell_item_titanium_seal_of_dalaran_catch : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return (sObjectMgr->GetBroadcastText(TEXT_TSOD_FLIPPED_HEADS) != nullptr) && (sObjectMgr->GetBroadcastText(TEXT_TSOD_FLIPPED_TAILS) != nullptr);
    }

    void TriggerEmote(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->TextEmote(RAND(TEXT_TSOD_FLIPPED_HEADS, TEXT_TSOD_FLIPPED_TAILS), caster);
    }

    void Register() override
    {
        OnEffectHit.Register(&spell_item_titanium_seal_of_dalaran_catch::TriggerEmote, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum RunescrollOfFortitude
{
    SPELL_FORTITUDE     = 72590
};

class spell_item_runescroll_of_fortitude : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FORTITUDE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_FORTITUDE, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_runescroll_of_fortitude::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum DrumsOfForgottenKings
{
    SPELL_BLESSING_OF_FORGOTTEN_KINGS     = 72586
};

class spell_item_drums_of_forgotten_kings : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BLESSING_OF_FORGOTTEN_KINGS });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_BLESSING_OF_FORGOTTEN_KINGS, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_drums_of_forgotten_kings::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum ThrallmarAndHonorHoldFavor
{
    SPELL_BUFFBOT_BUFF_EFFECT     = 32172
};

class spell_item_thrallmar_and_honor_hold_favor : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BUFFBOT_BUFF_EFFECT });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BUFFBOT_BUFF_EFFECT);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_BUFFBOT_BUFF_EFFECT);
    }

    void Register() override
    {
        AfterEffectApply.Register(&spell_item_thrallmar_and_honor_hold_favor::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove.Register(&spell_item_thrallmar_and_honor_hold_favor::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum DarkmoonCardIllusion
{
    SPELL_DARKMOON_CARD_ILLUSION       = 60242
};

class spell_item_darkmoon_card_illusion : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DARKMOON_CARD_ILLUSION });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_DARKMOON_CARD_ILLUSION, true);
    }

    void Register() override
    {
        AfterEffectRemove.Register(&spell_item_darkmoon_card_illusion::AfterRemove, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB, AURA_EFFECT_HANDLE_REAL);
    }
};

enum DiscoBall
{
    SPELL_LISTENING_TO_MUSIC_CHECK     = 50492,
    SPELL_LISTENING_TO_MUSIC           = 50493
};

class spell_item_disco_ball_listening_to_music_periodic : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LISTENING_TO_MUSIC_CHECK });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_LISTENING_TO_MUSIC_CHECK, true);
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_item_disco_ball_listening_to_music_periodic::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_item_disco_ball_listening_to_music_parent : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LISTENING_TO_MUSIC });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_LISTENING_TO_MUSIC, true);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_item_disco_ball_listening_to_music_parent::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};
}

void AddSC_spell_item_cata()
{
    using namespace spell_item_cata;
    RegisterSpellScript(spell_item_blessing_of_faith);
    RegisterSpellScript(spell_item_alchemist_stone);
    RegisterSpellScript(spell_item_brittle_armor);
    RegisterSpellScript(spell_item_goblin_bomb_dispenser);
    RegisterSpellScript(spell_item_goblin_weather_machine);
    RegisterSpellScript(spell_item_party_time);
    RegisterSpellScript(spell_item_dire_brew);
    RegisterSpellScript(spell_item_discerning_eye_beast_dummy);
    RegisterSpellScript(spell_item_extract_gas);
    RegisterSpellScript(spell_item_hourglass_sand);
    RegisterSpellScript(spell_item_mercurial_shield);
    RegisterSpellScript(spell_item_dimensional_ripper_everlook);
    RegisterSpellScript(spell_item_ultrasafe_transporter);
    RegisterSpellScript(spell_item_dimensional_ripper_area52);
    RegisterSpellScript(spell_item_lifegiving_gem);
    RegisterSpellScript(spell_item_runic_healing_injector);
    RegisterSpellScript(spell_item_mirrens_drinking_hat);
    RegisterSpellScript(spell_item_titanium_seal_of_dalaran_toss);
    RegisterSpellScript(spell_item_titanium_seal_of_dalaran_catch);
    RegisterSpellScript(spell_item_runescroll_of_fortitude);
    RegisterSpellScript(spell_item_drums_of_forgotten_kings);
    RegisterSpellScript(spell_item_thrallmar_and_honor_hold_favor);
    RegisterSpellScript(spell_item_darkmoon_card_illusion);
    RegisterSpellScript(spell_item_disco_ball_listening_to_music_periodic);
    RegisterSpellScript(spell_item_disco_ball_listening_to_music_parent);
}
