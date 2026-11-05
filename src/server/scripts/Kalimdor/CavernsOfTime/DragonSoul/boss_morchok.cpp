/*
 * Azimov - Dragon Soul : Morchok (+ Kohcrom en héroïque)
 *
 * IDs de sorts et mécaniques repris du script DS d'AshamaneCore (GPL, sorts identiques
 * entre 4.3.4 et Legion), nettoyé et adapté à l'API du fork Cataclysm Preservation :
 *  - Piétinement : dégâts partagés entre les joueurs touchés, double part pour les 2 plus proches
 *  - Armure écrasée : debuff empilable sur le tank
 *  - Cristal résonnant : explose après 12 s, touche les 3 (10) / 7 (25) joueurs les plus proches,
 *    marqués Danger / Prudence / Sûr selon leur distance
 *  - Vortex terrestre -> Sang noir de la terre : le sang se répand en anneau, les piliers
 *    (Vengeance de la terre) bloquent la ligne de vue
 *  - Furieux à 20 %, Berserk à 7 min
 *  - Héroïque : Kohcrom apparaît à 90 % et répète les sorts quelques secondes après.
 *    Les deux doivent mourir : Morchok reste à 1 PV tant que Kohcrom vit (comme sur retail).
 *  - Morchok rétrécit au fil des PV perdus
 */

#include <algorithm>
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "GameObject.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "dragon_soul.h"

namespace DragonSoul
{
enum MorchokSpells
{
    SPELL_BERSERK                       = 47008,
    SPELL_STOMP                         = 103414,
    SPELL_CRUSH_ARMOR                   = 103687,
    SPELL_RESONATING_CRYSTAL            = 103640, // invoque le cristal à une position
    SPELL_RESONATING_CRYSTAL_DMG        = 103545,
    SPELL_RESONATING_CRYSTAL_AURA       = 103494, // visuel du cristal
    SPELL_DANGER                        = 103534, // rouge : loin
    SPELL_WARNING                       = 103536, // jaune
    SPELL_SAFE                          = 103541, // vert : proche
    SPELL_FURIOUS                       = 103846,
    SPELL_BLACK_BLOOD_OF_THE_EARTH      = 103851,
    SPELL_BLACK_BLOOD_DUMMY             = 103180,
    SPELL_BLACK_BLOOD_DMG               = 103785,
    SPELL_EARTHEN_VORTEX                = 103821,
    SPELL_EARTHS_VENGEANCE              = 103176, // fait tomber les piliers
    SPELL_SUMMON_KOHCROM                = 109017,
    SPELL_MORCHOK_JUMP                  = 109070
};

enum MorchokNpcs
{
    NPC_RESONATING_CRYSTAL              = 55346
};

enum MorchokGameObjects
{
    GO_INNER_WALL                       = 209596  // piliers derrière lesquels se cacher
};

enum MorchokEvents
{
    EVENT_STOMP = 1,
    EVENT_CRUSH_ARMOR,
    EVENT_RESONATING_CRYSTAL,
    EVENT_EARTHEN_VORTEX,
    EVENT_BLACK_BLOOD,
    EVENT_END_BLACK_BLOOD,
    EVENT_BERSERK,
    EVENT_KOHCROM_RESUME,

    // cristal
    EVENT_CRYSTAL_MARK_TARGETS,
    EVENT_CRYSTAL_EXPLODE
};

enum MorchokActions
{
    ACTION_KOHCROM_STOMP = 1,
    ACTION_KOHCROM_CRYSTAL,
    ACTION_KOHCROM_VORTEX
};

// Groupes creature_text de Morchok : à aligner sur ta table (SELECT * FROM creature_text WHERE CreatureID = 55265)
enum MorchokTexts
{
    SAY_AGGRO       = 0,
    SAY_DEATH       = 1,
    SAY_VORTEX      = 6,
    SAY_BLACK_BLOOD = 7,
    SAY_CRYSTAL     = 9,
    SAY_KILL        = 10,
    SAY_KOHCROM     = 11
};

constexpr float BLACK_BLOOD_RING_STEP   = 4.0f;  // le sang avance de 4 m par tick
constexpr float MORCHOK_EVADE_RANGE     = 120.0f;

static void RemoveCrystalMarks(InstanceScript* instance)
{
    if (!instance)
        return;
    instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_DANGER);
    instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_WARNING);
    instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_SAFE);
}

static void CastCrystalNear(Creature* caster)
{
    Position pos = caster->GetNearPosition(frand(25.0f, 45.0f), frand(0.0f, float(2 * M_PI)));
    caster->CastSpell(pos, SPELL_RESONATING_CRYSTAL, true);
}

// ---------------------------------------------------------------------------
// Morchok
// ---------------------------------------------------------------------------
struct boss_morchok : public BossAI
{
    boss_morchok(Creature* creature) : BossAI(creature, DATA_MORCHOK), _furious(false), _kohcromSummoned(false) { }

    void Reset() override
    {
        _Reset();
        _furious = false;
        _kohcromSummoned = false;
        me->SetObjectScale(1.0f);
        // Héroïque : comme sur retail, Morchok ne peut pas mourir avant Kohcrom
        me->SetUnkillable(IsHeroic());
        RemoveCrystalMarks(instance);
        DespawnPillars();

        // reste passif tant que le trash du siège n'est pas nettoyé (géré par l'instance)
        if (instance->GetData(DATA_MORCHOK_TRASH))
            me->SetReactState(REACT_AGGRESSIVE);
    }

    void JustEngagedWith(Unit* who) override
    {
        _JustEngagedWith(who);
        Talk(SAY_AGGRO);
        DoZoneInCombat();

        events.ScheduleEvent(EVENT_STOMP, 12s, 14s);
        events.ScheduleEvent(EVENT_CRUSH_ARMOR, 6s, 12s);
        events.ScheduleEvent(EVENT_RESONATING_CRYSTAL, 16s, 19s);
        events.ScheduleEvent(EVENT_EARTHEN_VORTEX, 54s, 56s);
        events.ScheduleEvent(EVENT_BERSERK, 7min);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            Talk(SAY_KILL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
        RemoveCrystalMarks(instance);
        DespawnPillars();
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        RemoveCrystalMarks(instance);
        DespawnPillars();
        summons.DespawnAll();
        me->SetObjectScale(1.0f);
        BossAI::EnterEvadeMode(why);
    }

    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);

        if (summon->GetEntry() == NPC_KOHCROM)
        {
            // Kohcrom arrive avec la vie actuelle de Morchok
            summon->SetMaxHealth(me->GetMaxHealth());
            summon->SetHealth(me->GetHealth());
            summon->SetObjectScale(me->GetObjectScale());
            if (Unit* victim = me->GetVictim())
                summon->AI()->AttackStart(victim);
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() != NPC_KOHCROM)
            return;

        // Kohcrom est mort : Morchok peut mourir ; s'il attendait à 1 PV, il tombe tout de suite
        me->SetUnkillable(false);
        if (me->GetHealth() <= 1)
            me->KillSelf();
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        // rétrécit de 100 % à 50 % de taille selon la vie restante
        float pct = me->GetHealthPct() - (100.0f * damage / std::max<uint32>(1, me->GetMaxHealth()));
        me->SetObjectScale(std::max(0.5f, 0.5f + 0.5f * std::max(0.0f, pct) / 100.0f));

        if (IsHeroic() && !_kohcromSummoned && pct <= 90.0f)
        {
            _kohcromSummoned = true;
            events.ScheduleEvent(EVENT_KOHCROM_RESUME, 0s); // traité dans UpdateAI
        }

        if (!_furious && pct <= 20.0f)
        {
            _furious = true;
            DoCastSelf(SPELL_FURIOUS, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (me->GetDistance(me->GetHomePosition()) > MORCHOK_EVADE_RANGE)
        {
            EnterEvadeMode(EVADE_REASON_BOUNDARY);
            return;
        }

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_STOMP:
                    // pas de Piétinement juste avant le vortex (comme sur retail)
                    if (events.GetTimeUntilEvent(EVENT_EARTHEN_VORTEX) > 7000)
                    {
                        DoCastSelf(SPELL_STOMP);
                        TellKohcrom(ACTION_KOHCROM_STOMP);
                    }
                    events.Repeat(12s, 14s);
                    break;
                case EVENT_CRUSH_ARMOR:
                    DoCastVictim(SPELL_CRUSH_ARMOR);
                    events.Repeat(6s, 9s);
                    break;
                case EVENT_RESONATING_CRYSTAL:
                    if (events.GetTimeUntilEvent(EVENT_EARTHEN_VORTEX) > 13000)
                    {
                        Talk(SAY_CRYSTAL);
                        CastCrystalNear(me);
                        TellKohcrom(ACTION_KOHCROM_CRYSTAL);
                    }
                    events.Repeat(15s);
                    break;
                case EVENT_EARTHEN_VORTEX:
                    Talk(SAY_VORTEX);
                    StopForVortex();
                    TellKohcrom(ACTION_KOHCROM_VORTEX);
                    events.ScheduleEvent(EVENT_EARTHEN_VORTEX, 94s, 97s);
                    break;
                case EVENT_BLACK_BLOOD:
                    Talk(SAY_BLACK_BLOOD);
                    DoCastSelf(SPELL_BLACK_BLOOD_OF_THE_EARTH);
                    break;
                case EVENT_END_BLACK_BLOOD:
                    DespawnPillars();
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (Unit* victim = me->GetVictim())
                        AttackStart(victim);
                    events.ScheduleEvent(EVENT_STOMP, 15s);
                    events.ScheduleEvent(EVENT_CRUSH_ARMOR, 6s, 12s);
                    events.ScheduleEvent(EVENT_RESONATING_CRYSTAL, 18s);
                    break;
                case EVENT_KOHCROM_RESUME:
                    Talk(SAY_KOHCROM);
                    DoCastSelf(SPELL_MORCHOK_JUMP, true);
                    DoCastSelf(SPELL_SUMMON_KOHCROM, true);
                    break;
                case EVENT_BERSERK:
                    DoCastSelf(SPELL_BERSERK, true);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    void StopForVortex()
    {
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        events.CancelEvent(EVENT_STOMP);
        events.CancelEvent(EVENT_CRUSH_ARMOR);
        events.CancelEvent(EVENT_RESONATING_CRYSTAL);
        DoCastAOE(SPELL_EARTHEN_VORTEX, true);
        DoCastSelf(SPELL_EARTHS_VENGEANCE);
        events.ScheduleEvent(EVENT_BLACK_BLOOD, 5s);
        events.ScheduleEvent(EVENT_END_BLACK_BLOOD, 22s);
    }

    void TellKohcrom(int32 action)
    {
        for (ObjectGuid const& guid : summons)
            if (Creature* summon = ObjectAccessor::GetCreature(*me, guid))
                if (summon->GetEntry() == NPC_KOHCROM && summon->IsAlive())
                    summon->AI()->DoAction(action);
    }

    void DespawnPillars()
    {
        std::list<GameObject*> pillars;
        me->GetGameObjectListWithEntryInGrid(pillars, GO_INNER_WALL, 150.0f);
        for (GameObject* pillar : pillars)
            pillar->Delete();
    }

    bool _furious;
    bool _kohcromSummoned;
};

// ---------------------------------------------------------------------------
// Kohcrom (héroïque) : répète Piétinement / Cristal / Vortex de Morchok avec un décalage
// ---------------------------------------------------------------------------
struct npc_morchok_kohcrom : public ScriptedAI
{
    npc_morchok_kohcrom(Creature* creature) : ScriptedAI(creature), _furious(false) { }

    void Reset() override
    {
        _events.Reset();
        _furious = false;
    }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        DoCastSelf(SPELL_MORCHOK_JUMP, true);
        DoZoneInCombat();
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_KOHCROM_STOMP:
                _events.ScheduleEvent(EVENT_STOMP, 4s, 6s);
                break;
            case ACTION_KOHCROM_CRYSTAL:
                _events.ScheduleEvent(EVENT_RESONATING_CRYSTAL, 4s, 6s);
                break;
            case ACTION_KOHCROM_VORTEX:
                _events.Reset();
                me->SetReactState(REACT_PASSIVE);
                me->AttackStop();
                DoCastAOE(SPELL_EARTHEN_VORTEX, true);
                _events.ScheduleEvent(EVENT_BLACK_BLOOD, 5s);
                _events.ScheduleEvent(EVENT_END_BLACK_BLOOD, 22s);
                break;
            default:
                break;
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        float pct = me->GetHealthPct() - (100.0f * damage / std::max<uint32>(1, me->GetMaxHealth()));
        me->SetObjectScale(std::max(0.5f, 0.5f + 0.5f * std::max(0.0f, pct) / 100.0f));

        if (!_furious && pct <= 20.0f)
        {
            _furious = true;
            DoCastSelf(SPELL_FURIOUS, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_STOMP:
                    DoCastSelf(SPELL_STOMP);
                    break;
                case EVENT_RESONATING_CRYSTAL:
                    CastCrystalNear(me);
                    break;
                case EVENT_BLACK_BLOOD:
                    DoCastSelf(SPELL_BLACK_BLOOD_OF_THE_EARTH);
                    break;
                case EVENT_END_BLACK_BLOOD:
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (Unit* victim = me->GetVictim())
                        AttackStart(victim);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    bool _furious;
};

// ---------------------------------------------------------------------------
// Cristal résonnant : marque les joueurs les plus proches puis explose après 12 s
// ---------------------------------------------------------------------------
struct npc_morchok_resonating_crystal : public ScriptedAI
{
    npc_morchok_resonating_crystal(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
    }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        DoCastSelf(SPELL_RESONATING_CRYSTAL_AURA, true);
        _events.ScheduleEvent(EVENT_CRYSTAL_MARK_TARGETS, 500ms);
        _events.ScheduleEvent(EVENT_CRYSTAL_EXPLODE, 12s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CRYSTAL_MARK_TARGETS:
                    MarkTargets();
                    _events.Repeat(1s);
                    break;
                case EVENT_CRYSTAL_EXPLODE:
                    _events.Reset();
                    MarkTargets();
                    me->RemoveAurasDueToSpell(SPELL_RESONATING_CRYSTAL_AURA);
                    DoCastAOE(SPELL_RESONATING_CRYSTAL_DMG, true);
                    RemoveCrystalMarks(me->GetInstanceScript());
                    me->DespawnOrUnsummon(500ms);
                    break;
                default:
                    break;
            }
        }
    }

private:
    // les 3 (10) / 7 (25) joueurs vivants les plus proches, couleur selon la distance
    void MarkTargets()
    {
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, 200.0f);
        players.remove_if([](Player* p) { return !p->IsAlive() || p->IsGameMaster(); });
        players.sort([this](Player* a, Player* b) { return me->GetExactDist2d(a) < me->GetExactDist2d(b); });

        uint32 maxTargets = me->GetMap()->Is25ManRaid() ? 7 : 3;
        uint32 count = 0;
        for (Player* player : players)
        {
            bool targeted = count++ < maxTargets;
            float dist = me->GetExactDist2d(player);
            uint32 wanted = !targeted ? 0 : (dist > 30.0f ? SPELL_DANGER : (dist > 15.0f ? SPELL_WARNING : SPELL_SAFE));

            for (uint32 spellId : { uint32(SPELL_DANGER), uint32(SPELL_WARNING), uint32(SPELL_SAFE) })
                if (spellId != wanted)
                    player->RemoveAurasDueToSpell(spellId);

            if (wanted && !player->HasAura(wanted))
                me->CastSpell(player, wanted, true);
        }
    }

    EventMap _events;
};

// ---------------------------------------------------------------------------
// 103414 - Piétinement : dégâts partagés, double part pour les 2 plus proches
// ---------------------------------------------------------------------------
class spell_morchok_stomp : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        _targetCount = uint32(targets.size());
        if (targets.empty())
            return;

        std::vector<WorldObject*> sorted(targets.begin(), targets.end());
        Unit* caster = GetCaster();
        std::sort(sorted.begin(), sorted.end(), [caster](WorldObject* a, WorldObject* b)
        {
            return caster->GetExactDist(a) < caster->GetExactDist(b);
        });

        for (size_t i = 0; i < sorted.size() && i < 2; ++i)
            _closest[i] = sorted[i]->GetGUID();
    }

    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        if (!_targetCount)
            return;

        // 2 parts pour les deux plus proches, 1 part pour les autres
        uint32 shares = _targetCount + std::min<uint32>(_targetCount, 2);
        int32 share = GetHitDamage() / int32(shares);
        bool closest = GetHitUnit()->GetGUID() == _closest[0] || GetHitUnit()->GetGUID() == _closest[1];
        SetHitDamage(closest ? share * 2 : share);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_morchok_stomp::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnEffectHitTarget.Register(&spell_morchok_stomp::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }

    uint32 _targetCount = 0;
    ObjectGuid _closest[2];
};

// ---------------------------------------------------------------------------
// 103545 - Explosion du cristal : ne touche que les joueurs marqués
// ---------------------------------------------------------------------------
class spell_morchok_resonating_crystal_dmg : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* obj)
        {
            Unit* unit = obj->ToUnit();
            return !unit || !(unit->HasAura(SPELL_DANGER) || unit->HasAura(SPELL_WARNING) || unit->HasAura(SPELL_SAFE));
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_morchok_resonating_crystal_dmg::FilterTargets, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// ---------------------------------------------------------------------------
// 103851 - Sang noir de la terre : l'anneau s'élargit à chaque tick
// ---------------------------------------------------------------------------
class spell_morchok_black_blood_of_the_earth : public AuraScript
{
    void HandlePeriodic(AuraEffect const* aurEff)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        float radius = aurEff->GetTickNumber() * BLACK_BLOOD_RING_STEP;
        for (float angle = 0.0f; angle < float(2 * M_PI); angle += 0.3f)
        {
            Position pos = caster->GetNearPosition(radius, angle);
            caster->CastSpell(pos, SPELL_BLACK_BLOOD_DUMMY, true);
        }
    }

    void Register() override
    {
        OnEffectPeriodic.Register(&spell_morchok_black_blood_of_the_earth::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

// ---------------------------------------------------------------------------
// 103785 - Dégâts du sang noir : seulement dans l'anneau atteint, et pas derrière un pilier
// ---------------------------------------------------------------------------
class spell_morchok_black_blood_of_the_earth_dmg : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (AuraEffect const* aurEff = caster->GetAuraEffect(SPELL_BLACK_BLOOD_OF_THE_EARTH, EFFECT_0))
        {
            float reach = (aurEff->GetTickNumber() + 1) * BLACK_BLOOD_RING_STEP;
            targets.remove_if([caster, reach](WorldObject* obj) { return caster->GetExactDist2d(obj) > reach; });
        }

        std::list<GameObject*> pillars;
        caster->GetGameObjectListWithEntryInGrid(pillars, GO_INNER_WALL, 100.0f);
        for (GameObject* pillar : pillars)
            targets.remove_if([caster, pillar](WorldObject* obj) { return pillar->IsInBetween(caster, obj, 3.0f); });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect.Register(&spell_morchok_black_blood_of_the_earth_dmg::FilterTargets, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};
}

void AddSC_boss_morchok()
{
    using namespace DragonSoul;
    RegisterDragonSoulCreatureAI(boss_morchok);
    RegisterDragonSoulCreatureAI(npc_morchok_kohcrom);
    RegisterDragonSoulCreatureAI(npc_morchok_resonating_crystal);
    RegisterSpellScript(spell_morchok_stomp);
    RegisterSpellScript(spell_morchok_resonating_crystal_dmg);
    RegisterSpellScript(spell_morchok_black_blood_of_the_earth);
    RegisterSpellScript(spell_morchok_black_blood_of_the_earth_dmg);
}