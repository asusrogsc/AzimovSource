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
 *   serveur -> client : "L:<donjon>~<niv min>~<niv max>~<nom>" (une ligne par donjon), "F" (fin de liste), "M:<texte>",
 *                       "H:<entry>~<vie %>~<mana %>~<nom>" (état de chaque sujet, chaque seconde, pour les cadres de groupe)
 *
 * Réglages worldserver.conf :
 *   Azimov.Sujets.Actif (1), Azimov.Sujets.Degats (100 = 100 %), Azimov.Sujets.Soins (100)
 *   Par rôle (<Role> = Tank, Soigneur, Melee, Lanceur, Distance), en %, 100 par défaut :
 *     Azimov.Sujets.<Role>.Vie         : points de vie
 *     Azimov.Sujets.<Role>.Degats      : tous les dégâts infligés (mêlée, sorts, effets périodiques)
 *     Azimov.Sujets.<Role>.DegatsSubis : dégâts reçus
 *   Azimov.Sujets.Skada (0) : essai, sujets « créés par » le joueur pour apparaître dans Skada / Recount
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
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
#include <cmath>
#include <mutex>
#include <string>
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
        NPC_SUJET_TANK   = 990001,    // Commandant Thorne
        NPC_SUJET_HEAL   = 990002,    // Sœur Elyna
        NPC_SUJET_MELEE  = 990003,    // Kael l'Ombre
        NPC_SUJET_CASTER = 990004,    // Archimage Selvar
        NPC_SUJET_RANGED = 990005     // Chaman Torvak
    };

    enum AzSujetSpell : uint32
    {
        SPELL_HEAL      = 2061,   // Soins rapides (montant imposé)
        SPELL_FIREBALL  = 133,    // Boule de feu (dégâts imposés, effet 0). Avant : Éclair de givre 116, dont les dégâts
                                  // sont l'effet 1 -> la valeur imposée remplaçait le ralentissement et les dégâts restaient ~100
        SPELL_THUNDER_CLAP = 6343,  // Coup de tonnerre (tank, zone 8 m, dégâts = effet 0)
        SPELL_SHIELD_SLAM  = 23922, // Heurt de bouclier (tank, dégâts = effet 1)
        SPELL_HEROIC_STRIKE = 78,   // Frappe héroïque (Kael, dégâts = effet 0)
        SPELL_LIGHTNING = 403,    // Éclair (dégâts imposés)
        SPELL_SMITE     = 585,    // Châtiment (dégâts imposés)
        SPELL_DRINK     = 430,    // Boisson (visuel : le sujet s'assoit et boit)
        SPELL_REZ       = 2006    // Résurrection (le joueur reçoit la demande habituelle)
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

    // Nom du rôle d'un sujet dans worldserver.conf (Azimov.Sujets.<Role>.Vie / .Degats / .DegatsSubis)
    char const* RoleKey(uint32 entry)
    {
        switch (entry)
        {
            case NPC_SUJET_TANK:   return "Tank";
            case NPC_SUJET_HEAL:   return "Soigneur";
            case NPC_SUJET_MELEE:  return "Melee";
            case NPC_SUJET_CASTER: return "Lanceur";
            case NPC_SUJET_RANGED: return "Distance";
            default:               return nullptr;
        }
    }

    float RolePct(Unit const* unit, char const* setting)
    {
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
            return 1.0f;
        char const* role = RoleKey(unit->GetEntry());
        if (!role)
            return 1.0f;
        return float(std::max(0, sConfigMgr->GetIntDefault(std::string("Azimov.Sujets.") + role + "." + setting, 100))) / 100.0f;
    }

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
    // Niveau du joueur + statistiques de ce niveau + points de vie par rôle (Azimov.Sujets.<Role>.Vie)
    void ApplySujetLevel(Creature* sujet, uint8 level)
    {
        sujet->SetLevel(level);
        sujet->UpdateLevelDependantStats();
        if (float vie = RolePct(sujet, "Vie"); vie != 1.0f)
        {
            uint32 health = std::max<uint32>(1, uint32(float(sujet->GetCreateHealth()) * vie));
            sujet->SetCreateHealth(health);
            sujet->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
            sujet->UpdateMaxHealth();
        }
    }

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
            // Pas de SetOwnerGUID : le client 4.3.4 plante sur un PNJ « invoqué par » le joueur qui n'est pas un familier.
            // Option d'essai Azimov.Sujets.Skada = 1 : « créé par » le joueur seulement (comme un totem ou un gardien),
            // pour que le journal de combat marque le sujet comme « à moi » et que Skada / Recount compte ses dégâts et soins.
            if (sConfigMgr->GetBoolDefault("Azimov.Sujets.Skada", false))
                sujet->SetCreatorGUID(player->GetGUID());
            sujet->SetFaction(player->GetFaction());
            // Les modèles copiés (PNJ de ville) ont souvent IMMUNE_TO_PC / IMMUNE_TO_NPC dans unit_flags :
            // le joueur ne pouvait ni soigner ni améliorer ses sujets (IsValidAssistTarget refuse une cible immunisée aux joueurs)
            sujet->SetImmuneToPC(false);
            sujet->SetImmuneToNPC(false);
            ApplySujetLevel(sujet, player->getLevel());
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
    npc_azimov_sujet(Creature* creature) : ScriptedAI(creature), _thinkTimer(500), _summonTimer(5000), _drinking(false), _statusTimer(0)
    {
        // Les sujets ne doivent jamais passer sous le contrôle d'un monstre (Totem de charme du Contrôleur perdu
        // dans le temps, Salles des Sethekk : sujet « Inconnu », impossible à cibler ou à soigner) ni être rétrécis.
        for (uint32 aura : { SPELL_AURA_MOD_CHARM, SPELL_AURA_AOE_CHARM, SPELL_AURA_MOD_POSSESS, SPELL_AURA_MOD_SCALE })
            me->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, true);
        me->ApplySpellImmune(0, IMMUNITY_ID, 35013, true);   // Rétrécir (Contrôleur perdu dans le temps) : -dégâts
    }

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
    // Joueur prisonnier (Tombeau de givre, Entoilage, cocon, bloc de glace…) : ces prisons sont des PNJ
    // ennemis invoqués sur lui qu'il faut détruire. On cherche un PNJ invoqué attaquable à 4 m de lui, hors boss.
    Unit* FindPrison(Player* owner) const
    {
        if (!owner->IsAlive())
            return nullptr;
        bool prisonnier = owner->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT)
            || owner->HasAuraType(SPELL_AURA_MOD_STUN) || owner->HasAuraType(SPELL_AURA_MOD_ROOT)
            || owner->HasAuraType(SPELL_AURA_MOD_PACIFY) || owner->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE);
        if (!prisonnier)
            return nullptr;

        std::list<Unit*> proches;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(owner, me, 4.0f);
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(owner, proches, check);
        Cell::VisitAllObjects(owner, searcher, 4.0f);

        Unit* best = nullptr;
        for (Unit* u : proches)
        {
            Creature* c = u->ToCreature();
            if (!c || !CanFight(c) || c->IsDungeonBoss() || c->isWorldBoss())
                continue;
            // une prison est invoquée par un sort, ne bouge pas et n'attaque personne
            // (un monstre ordinaire qui dort à côté du joueur ne doit pas être tiré)
            if (!c->IsSummon() || c->GetVictim() || c->isMoving())
                continue;
            if (!best || owner->GetDistance(c) < owner->GetDistance(best))
                best = c;
        }
        return best;
    }

    Unit* PickEnemy(Player* owner, std::vector<Unit*> const& party) const
    {
        if (Unit* prison = FindPrison(owner))
            return prison;
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

    // Mana : jamais à sec. En combat +1 % par demi-seconde ; hors combat, si le joueur ne bouge pas,
    // le sujet s'assoit et boit (+8 % par demi-seconde) jusqu'à être plein. La boisson ne se consomme pas.
    void RegenMana(Player* owner, bool inCombat)
    {
        int32 maxMana = me->GetMaxPower(POWER_MANA);
        if (maxMana <= 0)
            return;

        if (inCombat)
        {
            StopDrinking();
            if (me->GetPower(POWER_MANA) < maxMana)
                me->ModifyPower(POWER_MANA, std::max(1, maxMana / 100));
            return;
        }

        bool needMana = me->GetPower(POWER_MANA) < maxMana;
        if (needMana && !owner->isMoving() && me->IsWithinDistInMap(owner, 8.0f))
        {
            if (!_drinking)
            {
                _drinking = true;
                me->GetMotionMaster()->Clear();
                me->StopMoving();
                me->CastSpell(me, SPELL_DRINK, true);
                me->SetStandState(UNIT_STAND_STATE_SIT);
            }
            me->ModifyPower(POWER_MANA, std::max(1, maxMana * 8 / 100));
        }
        else
            StopDrinking();
    }

    void StopDrinking()
    {
        if (!_drinking)
            return;
        _drinking = false;
        me->RemoveAurasDueToSpell(SPELL_DRINK);
        me->SetStandState(UNIT_STAND_STATE_STAND);
    }

    // Ascenseur, téléporteur, chute, sujet passé sous le sol : trop loin ou trop haut/bas -> il rejoint le joueur.
    // L'écart doit durer 3 s hors combat, 5 s en combat (un simple dénivelé ou un joueur qui court devant
    // ne doit pas le faire clignoter). Jamais pendant que le joueur est sur un transport en mouvement.
    // Appelé à chaque réflexion, en combat aussi (boss du Donjon d'Utgarde : sujets passés hors de la carte).
    bool RejoinIfStuck(Player* owner)
    {
        bool tooFar = false;
        if (!owner->GetTransport() && !_drinking)
        {
            float dz = std::fabs(me->GetPositionZ() - owner->GetPositionZ());
            float dist = me->GetDistance(owner);
            tooFar = dist > 60.0f || (dz > 15.0f && me->GetDistance2d(owner) < 25.0f) || !me->IsPositionValid();
        }
        _farTime = tooFar ? _farTime + 500 : 0;
        bool fight = me->IsInCombat() || owner->IsInCombat();
        if (_farTime < (fight ? 5000u : 3000u))
            return false;

        _farTime = 0;
        Position pos = owner->GetPosition();
        owner->MovePositionToFirstCollision(pos, 2.5f, AngleFor(me->GetEntry()));
        me->NearTeleportTo(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), owner->GetOrientation());
        me->GetMotionMaster()->Clear();
        return true;
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
        if (Player* owner = Owner())
            Send(owner, "H:" + std::to_string(me->GetEntry()) + "~0~0~" + me->GetName());
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

    // Fin d'un charme : pas d'attaque automatique du dernier « charmeur » (comportement par défaut de CreatureAI)
    void OnCharmed(bool /*isNew*/) override
    {
        if (!me->IsCharmed())
        {
            me->LastCharmerGUID.Clear();
            me->CombatStop(true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        Player* owner = Owner();
        if (!owner)
        {
            me->DespawnOrUnsummon();
            return;
        }

        // État pour les cadres de groupe de l'addon (vie, mana)
        if (_statusTimer <= diff)
        {
            _statusTimer = 1000;
            uint32 manaPct = me->GetMaxPower(POWER_MANA) > 0 ? uint32(100.0f * me->GetPower(POWER_MANA) / me->GetMaxPower(POWER_MANA)) : 0;
            Send(owner, "H:" + std::to_string(me->GetEntry()) + "~" + std::to_string(uint32(me->GetHealthPct())) + "~"
                + std::to_string(manaPct) + "~" + me->GetName());
        }
        else
            _statusTimer -= diff;

        if (_thinkTimer > diff)
        {
            _thinkTimer -= diff;
            if (me->GetVictim() && !IsCaster() && !IsHealer())
                DoMeleeAttackIfReady();
            return;
        }
        _thinkTimer = 500;

        // Charmé malgré tout (aura posée avant ce correctif) : on rend le sujet au joueur
        if (me->IsCharmed())
            me->RemoveCharmAuras();
        // Après un charme, le cœur remet la faction du gabarit : on reprend celle du joueur
        if (!me->IsCharmed() && me->GetFaction() != owner->GetFaction())
            me->SetFaction(owner->GetFaction());
        if (me->IsImmuneToPC())
            me->SetImmuneToPC(false);

        if (owner->IsAlive() && RejoinIfStuck(owner))
            return;

        // Le joueur a pris un niveau : hors combat, le sujet passe à son niveau sur place, vie au même pourcentage
        // (ne plus le faire disparaître : si les 4 disparaissaient ensemble, plus personne ne les rappelait)
        if (me->getLevel() != owner->getLevel() && !me->IsInCombat() && !owner->IsInCombat())
        {
            float pct = me->GetHealthPct();
            ApplySujetLevel(me, owner->getLevel());
            me->SetHealth(std::max<uint32>(1, uint32(me->GetMaxHealth() * pct / 100.0f)));
            me->SetPower(POWER_MANA, me->GetMaxPower(POWER_MANA));
        }

        std::vector<Unit*> party = Party(owner);
        Unit* enemy = PickEnemy(owner, party);

        // Joueur mort : tant que des monstres frappent le groupe, les sujets continuent le combat (plus bas) ;
        // une fois le combat fini, ils se regroupent près du corps et la soigneuse relève le joueur
        if (!owner->IsAlive() && !enemy)
        {
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;   // résurrection en cours : on ne bouge pas
            FollowOwner(owner);
            // Le groupe a péri mais la soigneuse est debout : elle relève le joueur une fois le combat fini
            if (IsHealer() && !me->IsInCombat() && !me->HasUnitState(UNIT_STATE_CASTING) && !owner->IsResurrectRequested()
                && me->IsWithinDistInMap(owner, 30.0f) && me->IsWithinLOSInMap(owner))
            {
                me->GetMotionMaster()->Clear();
                me->StopMoving();
                me->SetFacingToObject(owner);
                me->CastSpell(owner, SPELL_REZ, CastSpellExtraArgs(TriggerCastFlags(TRIGGERED_IGNORE_POWER_COST | TRIGGERED_IGNORE_REAGENT_COST)));
            }
            return;
        }

        RegenMana(owner, enemy != nullptr);

        if (!enemy)
        {
            if (!_drinking)
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
            uint32 spell = me->GetEntry() == NPC_SUJET_CASTER ? SPELL_FIREBALL : SPELL_LIGHTNING;
            me->CastSpell(enemy, spell, CastSpellExtraArgs().AddSpellBP0(ScaledAmount(me, 2.5f, "Azimov.Sujets.Degats")));
        }
        else if (!IsCaster())
        {
            UseMeleeAbilities(enemy);
            DoMeleeAttackIfReady();
        }
    }

    // Techniques de mêlée (sinon que des coups blancs) : tank = Coup de tonnerre / 8 s + Heurt de bouclier / 6 s,
    // Kael = Frappe héroïque / 4 s. Dégâts imposés comme pour les sorts (Azimov.Sujets.Degats).
    void UseMeleeAbilities(Unit* enemy)
    {
        _ability1 = _ability1 > 500 ? _ability1 - 500 : 0;
        _ability2 = _ability2 > 500 ? _ability2 - 500 : 0;
        if (!me->IsWithinMeleeRange(enemy))
            return;

        if (IsTank())
        {
            if (!_ability1)
            {
                me->CastSpell(me, SPELL_THUNDER_CLAP, CastSpellExtraArgs(true).AddSpellBP0(ScaledAmount(me, 0.8f, "Azimov.Sujets.Degats")));
                _ability1 = 8000;
            }
            else if (!_ability2)
            {
                me->CastSpell(enemy, SPELL_SHIELD_SLAM, CastSpellExtraArgs(true).AddSpellMod(SPELLVALUE_BASE_POINT1, ScaledAmount(me, 1.5f, "Azimov.Sujets.Degats")));
                _ability2 = 6000;
            }
        }
        else if (me->GetEntry() == NPC_SUJET_MELEE && !_ability1)
        {
            me->CastSpell(enemy, SPELL_HEROIC_STRIKE, CastSpellExtraArgs(true).AddSpellBP0(ScaledAmount(me, 1.5f, "Azimov.Sujets.Degats")));
            _ability1 = 4000;
        }
    }

private:
    uint32 _thinkTimer;
    uint32 _summonTimer;
    uint32 _ability1 = 0;  // délai des techniques de mêlée (ms)
    uint32 _ability2 = 3000;
    uint32 _farTime = 0;   // depuis combien de temps (ms) le sujet est trop loin du joueur
    bool _drinking;
    uint32 _statusTimer;
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
            else if (!itr->second.pending && player->IsAlive() && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                s_groups.erase(itr);   // sorti du donjon vivant : fin du groupe de sujets
            // Mort et esprit libéré (cimetière hors du donjon) : le groupe est gardé,
            // les sujets reviennent dès que le joueur rentre dans le donjon.
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

// Dégâts infligés / subis par rôle (Azimov.Sujets.<Role>.Degats / .DegatsSubis)
class azimov_sujets_damage : public UnitScript
{
public:
    azimov_sujets_damage() : UnitScript("azimov_sujets_damage") { }

    static float Factor(Unit* target, Unit* attacker)
    {
        return RolePct(attacker, "Degats") * RolePct(target, "DegatsSubis");
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (float f = Factor(target, attacker); f != 1.0f)
            damage = uint32(float(damage) * f);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        if (damage > 0)
            if (float f = Factor(target, attacker); f != 1.0f)
                damage = int32(float(damage) * f);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (float f = Factor(target, attacker); f != 1.0f)
            damage = uint32(float(damage) * f);
    }
};

void AddSC_azimov_sujets()
{
    new azimov_sujets_damage();
    RegisterCreatureAI(npc_azimov_sujet);
    new azimov_sujets_player();
}
