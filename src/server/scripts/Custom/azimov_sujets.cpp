/*
 * Azimov Online - Donjons avec sujets (façon retail « Follower Dungeons »)
 *
 * Le joueur choisit un rôle et un donjon dans la section « Donjon avec sujets » de l'outil donjons (addon).
 * Il est téléporté seul à l'entrée du donjon (mode normal) et le serveur complète le groupe avec 4 sujets PNJ :
 *   - joueur tank      -> soigneur + 3 DPS
 *   - joueur soigneur  -> tank + 3 DPS
 *   - joueur DPS       -> tank + soigneur + 2 DPS
 * Les sujets suivent le joueur, se battent avec lui (le tank prend les monstres, le soigneur soigne le plus blessé),
 * reviennent après chaque combat s'ils sont morts, et disparaissent quand le joueur quitte le donjon.
 * Les monstres combattus par les sujets sont attribués au joueur (SetLootRecipient) : butin, expérience et quêtes pour lui.
 *
 * Protocole addon (préfixe AZSUJ, chuchotement à soi-même) :
 *   client -> serveur : "AZSUJ_Q" (liste des donjons), "AZSUJ_E:<donjon>:<rôle>" (entrer ; rôle 0 tank, 1 soigneur, 2 DPS)
 *   serveur -> client : "L:<donjon>~<niv min>~<niv max>~<nom>" (une ligne par donjon), "F" (fin de liste), "M:<texte>"
 *
 * Réglages worldserver.conf :
 *   Azimov.Sujets.Actif (1), Azimov.Sujets.Degats (100 = 100 %), Azimov.Sujets.Soins (100)
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellDefines.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    char const* ADDON_PREFIX = "AZSUJ";

    enum AzRole : uint8
    {
        ROLE_TANK = 0,
        ROLE_HEAL = 1,
        ROLE_DPS  = 2
    };

    enum AzSujetNpc : uint32
    {
        NPC_SUJET_TANK   = 9500001,   // Commandant Thorne
        NPC_SUJET_HEAL   = 9500002,   // Sœur Elyna
        NPC_SUJET_MELEE  = 9500003,   // Kael l'Ombre
        NPC_SUJET_CASTER = 9500004,   // Archimage Selvar
        NPC_SUJET_RANGED = 9500005    // Chaman Torvak
    };

    enum AzSujetSpell : uint32
    {
        SPELL_HEAL      = 2061,   // Soins rapides (montant imposé)
        SPELL_FROSTBOLT = 116,    // Éclair de givre (dégâts imposés)
        SPELL_LIGHTNING = 403,    // Éclair (dégâts imposés)
        SPELL_SMITE     = 585     // Châtiment (dégâts imposés)
    };

    struct AzSujetGroup
    {
        uint32 dungeon = 0;
        uint32 map = 0;
        uint8  role = ROLE_DPS;
        bool   pending = false;                   // téléportation en cours vers le donjon
        std::vector<uint32> entries;              // sujets voulus
        std::vector<ObjectGuid> guids;            // sujets en jeu (même ordre)
    };

    std::mutex s_lock;
    std::unordered_map<ObjectGuid::LowType, AzSujetGroup> s_groups;

    bool Enabled()
    {
        return sConfigMgr->GetBoolDefault("Azimov.Sujets.Actif", true);
    }

    void Send(Player* player, std::string const& text)
    {
        player->WhisperAddon(text, ADDON_PREFIX, player);
    }

    void Message(Player* player, std::string const& text)
    {
        Send(player, "M:" + text);
    }

    std::vector<uint32> EntriesFor(uint8 role)
    {
        switch (role)
        {
            case ROLE_TANK: return { NPC_SUJET_HEAL, NPC_SUJET_MELEE, NPC_SUJET_CASTER, NPC_SUJET_RANGED };
            case ROLE_HEAL: return { NPC_SUJET_TANK, NPC_SUJET_MELEE, NPC_SUJET_CASTER, NPC_SUJET_RANGED };
            default:        return { NPC_SUJET_TANK, NPC_SUJET_HEAL, NPC_SUJET_MELEE, NPC_SUJET_CASTER };
        }
    }

    float AngleFor(uint32 entry)
    {
        switch (entry)
        {
            case NPC_SUJET_TANK:   return 0.0f;           // devant
            case NPC_SUJET_HEAL:   return float(M_PI);    // derrière
            case NPC_SUJET_MELEE:  return float(M_PI) / 2.0f;
            case NPC_SUJET_CASTER: return float(M_PI) * 0.75f;
            default:               return float(M_PI) * 1.25f;
        }
    }

    // Donjons proposés au joueur : donjons normaux de l'outil, à son niveau, avec une entrée connue
    bool IsOffered(lfg::LFGDungeonData const* d, uint8 level)
    {
        return d && d->type == lfg::LFG_TYPE_DUNGEON && d->difficulty == DUNGEON_DIFFICULTY_NORMAL && !d->seasonal
            && (d->x != 0.0f || d->y != 0.0f) && level >= d->minlevel && level <= d->maxlevel
            && sMapStore.LookupEntry(d->map);
    }

    void SendList(Player* player)
    {
        uint8 level = player->getLevel();
        for (LFGDungeonEntry const* entry : sLFGDungeonStore)
            if (lfg::LFGDungeonData const* d = sLFGMgr->GetLFGDungeon(entry->ID))
                if (IsOffered(d, level))
                    Send(player, "L:" + std::to_string(d->id) + "~" + std::to_string(d->minlevel) + "~" + std::to_string(d->maxlevel) + "~" + d->name);
        Send(player, "F");
    }

    // Place les sujets manquants (ou morts) autour du joueur. Le joueur doit être dans le donjon du groupe.
    void EnsureFollowers(Player* player)
    {
        std::vector<std::pair<size_t, uint32>> missing;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_groups.find(player->GetGUID().GetCounter());
            if (itr == s_groups.end() || itr->second.map != player->GetMapId())
                return;
            AzSujetGroup& g = itr->second;
            g.guids.resize(g.entries.size());
            for (size_t i = 0; i < g.entries.size(); ++i)
            {
                Creature* c = g.guids[i].IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*player, g.guids[i]);
                if (!c || !c->IsAlive())
                {
                    if (c)
                        c->DespawnOrUnsummon();
                    missing.emplace_back(i, g.entries[i]);
                }
            }
        }

        for (auto const& m : missing)
        {
            Position pos = player->GetPosition();
            player->MovePositionToFirstCollision(pos, 2.5f, AngleFor(m.second));
            TempSummon* sujet = player->SummonCreature(m.second, pos, TEMPSUMMON_MANUAL_DESPAWN);
            if (!sujet)
            {
                TC_LOG_ERROR("server.azimov", "Sujets : impossible de faire apparaître le sujet %u (SQL des sujets passé ?)", m.second);
                continue;
            }
            // Pas de SetOwnerGUID / SetCreatorGUID : le client 4.3.4 plante sur un PNJ « invoqué par » le joueur qui n'est pas un familier
            sujet->SetFaction(player->GetFaction());
            sujet->SetLevel(player->getLevel());
            sujet->UpdateLevelDependantStats();
            sujet->SetFullHealth();
            sujet->SetPower(POWER_MANA, sujet->GetMaxPower(POWER_MANA));
            sujet->GetMotionMaster()->MoveFollow(player, 2.5f, AngleFor(m.second));

            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_groups.find(player->GetGUID().GetCounter());
            if (itr != s_groups.end() && m.first < itr->second.guids.size())
                itr->second.guids[m.first] = sujet->GetGUID();
        }
    }

    void Enter(Player* player, uint32 dungeonId, uint8 role)
    {
        if (!Enabled())
        {
            Message(player, "Les donjons avec sujets sont désactivés.");
            return;
        }
        if (role > ROLE_DPS)
            role = ROLE_DPS;

        lfg::LFGDungeonData const* d = sLFGMgr->GetLFGDungeon(dungeonId);
        if (!IsOffered(d, player->getLevel()))
        {
            Message(player, "Ce donjon n'est pas disponible à ton niveau.");
            return;
        }
        if (player->GetGroup())
        {
            Message(player, "Quitte ton groupe pour partir avec des sujets.");
            return;
        }
        if (player->IsInCombat() || !player->IsAlive() || player->IsInFlight())
        {
            Message(player, "Impossible pour le moment (combat, mort ou vol).");
            return;
        }

        {
            std::lock_guard<std::mutex> guard(s_lock);
            AzSujetGroup& g = s_groups[player->GetGUID().GetCounter()];
            g.dungeon = d->id;
            g.map = d->map;
            g.role = role;
            g.entries = EntriesFor(role);
            g.guids.clear();
            g.pending = player->GetMapId() != d->map;
        }

        if (player->GetMapId() == d->map)
        {
            // Déjà dans le donjon : on (re)fait seulement venir les sujets
            EnsureFollowers(player);
            Message(player, "Tes sujets te rejoignent.");
            return;
        }

        player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL);
        player->SendDungeonDifficulty(false);
        if (!player->TeleportTo(d->map, d->x, d->y, d->z, d->o))
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_groups.erase(player->GetGUID().GetCounter());
            Message(player, "La téléportation vers le donjon a échoué.");
            return;
        }
        Message(player, "En route pour " + d->name + " avec tes sujets !");
    }

    int32 ScaledAmount(Creature* me, float perLevelSquared, char const* configKey)
    {
        float level = float(me->getLevel());
        float pct = float(sConfigMgr->GetIntDefault(configKey, 100)) / 100.0f;
        return std::max(1, int32((level * level * perLevelSquared + 30.0f) * pct));
    }
}

// IA commune des sujets
struct npc_azimov_sujet : public ScriptedAI
{
    npc_azimov_sujet(Creature* creature) : ScriptedAI(creature), _thinkTimer(500), _summonTimer(5000) { }

    Player* Owner() const
    {
        TempSummon const* summon = me->ToTempSummon();
        return summon ? ObjectAccessor::GetPlayer(*me, summon->GetSummonerGUID()) : nullptr;
    }

    bool IsTank() const { return me->GetEntry() == NPC_SUJET_TANK; }
    bool IsHealer() const { return me->GetEntry() == NPC_SUJET_HEAL; }
    bool IsCaster() const { return me->GetEntry() == NPC_SUJET_CASTER || me->GetEntry() == NPC_SUJET_RANGED; }

    // Membres du groupe : le joueur et ses sujets vivants
    std::vector<Unit*> Party(Player* owner) const
    {
        std::vector<Unit*> party;
        party.push_back(owner);
        std::lock_guard<std::mutex> guard(s_lock);
        auto itr = s_groups.find(owner->GetGUID().GetCounter());
        if (itr != s_groups.end())
            for (ObjectGuid const& guid : itr->second.guids)
                if (!guid.IsEmpty())
                    if (Creature* c = ObjectAccessor::GetCreature(*me, guid))
                        if (c->IsAlive())
                            party.push_back(c);
        return party;
    }

    bool CanFight(Unit* target) const
    {
        return target && target->IsAlive() && me->IsValidAttackTarget(target) && target->GetTypeId() != TYPEID_PLAYER;
    }

    // Cible : celle du joueur, sinon un monstre qui attaque le groupe
    Unit* PickEnemy(Player* owner, std::vector<Unit*> const& party) const
    {
        if (Unit* v = owner->GetVictim())
            if (CanFight(v) && v->IsInCombat())
                return v;
        Unit* best = nullptr;
        float bestDist = 0.0f;
        for (Unit* member : party)
            for (Unit* attacker : member->getAttackers())
                if (CanFight(attacker))
                {
                    float dist = me->GetDistance(attacker);
                    if (!best || dist < bestDist)
                    {
                        best = attacker;
                        bestDist = dist;
                    }
                }
        if (!best && me->GetVictim() && CanFight(me->GetVictim()) && me->GetVictim()->IsInCombat())
            best = me->GetVictim();
        return best;
    }

    void FollowOwner(Player* owner)
    {
        if (me->GetVictim())
            me->AttackStop();
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            me->GetMotionMaster()->MoveFollow(owner, 2.5f, AngleFor(me->GetEntry()));
    }

    void Engage(Unit* enemy)
    {
        ClaimForOwner(enemy);
        if (me->GetVictim() == enemy)
            return;
        if (IsCaster() || IsHealer())
        {
            me->Attack(enemy, false);
            me->GetMotionMaster()->MoveChase(enemy, 20.0f);
        }
        else
            AttackStart(enemy);
    }

    // Le monstre revient au joueur (butin, expérience, crédit de quête) même si un sujet frappe en premier
    void ClaimForOwner(Unit* enemy)
    {
        if (Creature* c = enemy->ToCreature())
            if (!c->hasLootRecipient())
                if (Player* owner = Owner())
                    c->SetLootRecipient(owner);
    }

    // Le tank attire sur lui tout ce qui frappe le groupe
    void TauntAll(std::vector<Unit*> const& party)
    {
        for (Unit* member : party)
        {
            if (member == me)
                continue;
            for (Unit* attacker : member->getAttackers())
                if (CanFight(attacker) && attacker->GetVictim() != me && attacker->CanHaveThreatList())
                {
                    attacker->GetThreatManager().AddThreat(me, 0.0f, nullptr, true, true);
                    attacker->GetThreatManager().MatchUnitThreatToHighestThreat(me);
                    attacker->GetThreatManager().AddThreat(me, 5000.0f, nullptr, true, true);
                }
        }
    }

    bool HealSomeone(std::vector<Unit*> const& party, float threshold)
    {
        Unit* target = nullptr;
        for (Unit* member : party)
            if (member->IsAlive() && member->GetHealthPct() < threshold && me->IsWithinDistInMap(member, 40.0f)
                && (!target || member->GetHealthPct() < target->GetHealthPct()))
                target = member;
        if (!target)
            return false;
        me->CastSpell(target, SPELL_HEAL, CastSpellExtraArgs().AddSpellBP0(ScaledAmount(me, 4.0f, "Azimov.Sujets.Soins")));
        return true;
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(5000);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        // Pas de retour au point d'apparition : on retourne près du joueur
        me->CombatStop(true);
        if (Player* owner = Owner())
            me->GetMotionMaster()->MoveFollow(owner, 2.5f, AngleFor(me->GetEntry()));
    }

    void MoveInLineOfSight(Unit* /*who*/) override { }   // pas d'aggro spontanée

    void UpdateAI(uint32 diff) override
    {
        Player* owner = Owner();
        if (!owner)
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (_thinkTimer > diff)
        {
            _thinkTimer -= diff;
            if (me->GetVictim() && !IsCaster() && !IsHealer())
                DoMeleeAttackIfReady();
            return;
        }
        _thinkTimer = 500;

        if (!owner->IsAlive())
        {
            FollowOwner(owner);
            return;
        }

        std::vector<Unit*> party = Party(owner);
        Unit* enemy = PickEnemy(owner, party);

        if (!enemy)
        {
            FollowOwner(owner);
            if (IsHealer() && !me->HasUnitState(UNIT_STATE_CASTING))
                HealSomeone(party, 90.0f);

            // Hors combat : les sujets morts reviennent
            if (_summonTimer <= 500)
            {
                _summonTimer = 5000;
                if (!owner->IsInCombat())
                    EnsureFollowers(owner);
            }
            else
                _summonTimer -= 500;
            return;
        }

        if (IsTank())
            TauntAll(party);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (IsHealer())
        {
            ClaimForOwner(enemy);
            if (!HealSomeone(party, 85.0f) && me->IsWithinDistInMap(enemy, 30.0f))
                me->CastSpell(enemy, SPELL_SMITE, CastSpellExtraArgs().AddSpellBP0(ScaledAmount(me, 1.0f, "Azimov.Sujets.Degats")));
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 6.0f, AngleFor(me->GetEntry()));
            return;
        }

        Engage(enemy);

        if (IsCaster() && me->IsWithinDistInMap(enemy, 30.0f))
        {
            uint32 spell = me->GetEntry() == NPC_SUJET_CASTER ? SPELL_FROSTBOLT : SPELL_LIGHTNING;
            me->CastSpell(enemy, spell, CastSpellExtraArgs().AddSpellBP0(ScaledAmount(me, 2.5f, "Azimov.Sujets.Degats")));
        }
        else if (!IsCaster())
            DoMeleeAttackIfReady();
    }

private:
    uint32 _thinkTimer;
    uint32 _summonTimer;
};

class azimov_sujets_player : public PlayerScript
{
public:
    azimov_sujets_player() : PlayerScript("azimov_sujets_player") { }

    void OnChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || receiver != player || msg.compare(0, 6, "AZSUJ_") != 0)
            return;

        if (msg == "AZSUJ_Q")
            SendList(player);
        else if (msg.compare(0, 8, "AZSUJ_E:") == 0)
        {
            char* end = nullptr;
            uint32 dungeon = uint32(strtoul(msg.c_str() + 8, &end, 10));
            uint8 role = (end && *end == ':') ? uint8(strtoul(end + 1, nullptr, 10)) : uint8(ROLE_DPS);
            Enter(player, dungeon, role);
        }
    }

    void OnMapChanged(Player* player) override
    {
        bool summon = false;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_groups.find(player->GetGUID().GetCounter());
            if (itr == s_groups.end())
                return;
            if (itr->second.map == player->GetMapId())
            {
                itr->second.pending = false;
                summon = true;
            }
            else if (!itr->second.pending)
                s_groups.erase(itr);   // sorti du donjon : fin du groupe de sujets
        }
        if (summon)
            EnsureFollowers(player);
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(s_lock);
        s_groups.erase(player->GetGUID().GetCounter());
    }
};

void AddSC_azimov_sujets()
{
    RegisterCreatureAI(npc_azimov_sujet);
    new azimov_sujets_player();
}
