/*
 * Azimov - Dragon Soul : trash de Morchok (Siège du temple du Repos du Ver)
 *
 * Remplace l'ancienne SmartAI de ces PNJ (sql/old/4.3.4/world/09_2015_07_28/2015_03_27_00_world.sql).
 * Les IDs de sorts viennent de cette SmartAI ; les timers aussi, ajustés pour coller au comportement retail.
 * Chaque mort prévient l'instance (DATA_MORCHOK_TRASH) qui déverrouille Morchok quand tout est mort.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "InstanceScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum DSTrashSpells
{
    // Earthen Destroyer
    SPELL_BOULDER_SMASH         = 107597, // rochers sur un joueur au hasard, stun de zone dissipable
    SPELL_DUST_STORM            = 107675, // dégâts de proximité, évitables dans l'eau pure

    // Ancient Water Lord
    SPELL_FLOOD                 = 107791, // canalisation en cône frontal
    SPELL_DRENCHED              = 107801,

    // Earthen Soldier
    SPELL_SHADOW_BOLT           = 95440,
    SPELL_TWILIGHT_CORRUPTION   = 107852, // interruptible, remplit l'énergie
    SPELL_TWILIGHT_RAGE         = 107872, // +25 % dégâts, se cumule

    // Cosmétique hors combat (canalisation sur le temple)
    SPELL_COSMETIC_CHANNEL_OMNI = 51835
};

enum DSTrashEvents
{
    EVENT_BOULDER_SMASH = 1,
    EVENT_DUST_STORM,
    EVENT_FLOOD,
    EVENT_DRENCHED,
    EVENT_SHADOW_BOLT,
    EVENT_TWILIGHT_CORRUPTION,
    EVENT_COSMETIC_CHANNEL
};

// Base commune : canalisation cosmétique hors combat + notification de mort à l'instance
struct npc_ds_morchok_trash_base : public ScriptedAI
{
    npc_ds_morchok_trash_base(Creature* creature, bool cosmeticChannel) : ScriptedAI(creature), _cosmeticChannel(cosmeticChannel) { }

    void Reset() override
    {
        _events.Reset();
        if (_cosmeticChannel && !me->IsSummon())
            _events.ScheduleEvent(EVENT_COSMETIC_CHANNEL, 1s);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.Reset();
        me->InterruptNonMeleeSpells(false, SPELL_COSMETIC_CHANNEL_OMNI);
        me->RemoveAurasDueToSpell(SPELL_COSMETIC_CHANNEL_OMNI);
        ScheduleCombatEvents();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (me->GetSpawnId() && !me->IsSummon())
            if (InstanceScript* instance = me->GetInstanceScript())
                instance->SetData(DATA_MORCHOK_TRASH, uint32(me->GetSpawnId()));
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsInCombat())
        {
            _events.Update(diff);
            if (_events.ExecuteEvent() == EVENT_COSMETIC_CHANNEL && !me->HasUnitState(UNIT_STATE_CASTING))
                DoCastSelf(SPELL_COSMETIC_CHANNEL_OMNI);
            return;
        }

        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
            ExecuteCombatEvent(eventId);

        DoMeleeAttackIfReady();
    }

    virtual void ScheduleCombatEvents() = 0;
    virtual void ExecuteCombatEvent(uint32 eventId) = 0;

protected:
    EventMap _events;

private:
    bool _cosmeticChannel;
};

// ---------------------------------------------------------------------------
struct npc_ds_earthen_destroyer : public npc_ds_morchok_trash_base
{
    npc_ds_earthen_destroyer(Creature* creature) : npc_ds_morchok_trash_base(creature, true) { }

    void ScheduleCombatEvents() override
    {
        _events.ScheduleEvent(EVENT_BOULDER_SMASH, 2s, 5s);
        _events.ScheduleEvent(EVENT_DUST_STORM, 7s, 9s);
    }

    void ExecuteCombatEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_BOULDER_SMASH:
                // retail : un joueur au hasard, pas forcément le tank
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 100.0f, true))
                    DoCast(target, SPELL_BOULDER_SMASH);
                _events.Repeat(12s, 15s);
                break;
            case EVENT_DUST_STORM:
                DoCastSelf(SPELL_DUST_STORM);
                _events.Repeat(22s, 26s);
                break;
            default:
                break;
        }
    }
};

// ---------------------------------------------------------------------------
struct npc_ds_ancient_water_lord : public npc_ds_morchok_trash_base
{
    npc_ds_ancient_water_lord(Creature* creature) : npc_ds_morchok_trash_base(creature, true) { }

    void ScheduleCombatEvents() override
    {
        _events.ScheduleEvent(EVENT_FLOOD, 4s, 6s);
        _events.ScheduleEvent(EVENT_DRENCHED, 7s, 9s);
    }

    void ExecuteCombatEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_FLOOD:
                // retail : se tourne parfois vers un joueur au hasard pendant Flood
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                {
                    me->SetFacingToObject(target);
                    DoCast(target, SPELL_FLOOD);
                }
                _events.Repeat(13s, 16s);
                break;
            case EVENT_DRENCHED:
                DoCastVictim(SPELL_DRENCHED);
                _events.Repeat(25s, 27s);
                break;
            default:
                break;
        }
    }
};

// ---------------------------------------------------------------------------
// Earthen Soldier : chaque Twilight Corruption réussi remplit l'énergie ;
// à 100 elle se vide et il gagne un stack de Twilight Rage (+25 % dégâts).
// Un kick empêche donc la montée.
// ---------------------------------------------------------------------------
struct npc_ds_earthen_soldier : public npc_ds_morchok_trash_base
{
    npc_ds_earthen_soldier(Creature* creature) : npc_ds_morchok_trash_base(creature, false) { }

    static constexpr int32 ENERGY_PER_CORRUPTION = 25; // 4 corruptions = 1 stack

    void Reset() override
    {
        npc_ds_morchok_trash_base::Reset();
        me->SetPowerType(POWER_ENERGY);
        me->SetMaxPower(POWER_ENERGY, 100);
        me->SetPower(POWER_ENERGY, 0);
        me->RemoveAurasDueToSpell(SPELL_TWILIGHT_RAGE);
    }

    void ScheduleCombatEvents() override
    {
        _events.ScheduleEvent(EVENT_SHADOW_BOLT, 1s);
        _events.ScheduleEvent(EVENT_TWILIGHT_CORRUPTION, 5s, 7s);
    }

    void OnSpellCastFinished(SpellInfo const* spell, SpellFinishReason reason) override
    {
        if (spell->Id != SPELL_TWILIGHT_CORRUPTION || reason != SPELL_FINISHED_SUCCESSFUL_CAST)
            return;

        int32 energy = me->GetPower(POWER_ENERGY) + ENERGY_PER_CORRUPTION;
        if (energy >= 100)
        {
            me->SetPower(POWER_ENERGY, 0);
            DoCastSelf(SPELL_TWILIGHT_RAGE, true);
        }
        else
            me->SetPower(POWER_ENERGY, energy);
    }

    void ExecuteCombatEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_SHADOW_BOLT:
                DoCastVictim(SPELL_SHADOW_BOLT);
                _events.Repeat(3400ms, 4700ms);
                break;
            case EVENT_TWILIGHT_CORRUPTION:
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                    DoCast(target, SPELL_TWILIGHT_CORRUPTION);
                _events.Repeat(6s, 8s);
                break;
            default:
                break;
        }
    }
};
}

void AddSC_dragon_soul_trash()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(npc_ds_earthen_destroyer);
    RegisterDragonSoulCreatureAI(npc_ds_ancient_water_lord);
    RegisterDragonSoulCreatureAI(npc_ds_earthen_soldier);
}
