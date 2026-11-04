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
#include "Chat.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "WorldSession.h"
#include <string>
#include "CharacterDatabase.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Random.h"
#include "RBAC.h"
#include "SmartScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

// ===========================================================================
// COMPAGNONS (ajout du 12 octobre 2026)
// Capturer n'importe quel monstre (sauf les boss) avec le Lasso du dompteur,
// puis l'invoquer comme compagnon : roles attaque / tank / soin, comportements
// agressif / defensif / passif, niveaux 1 a 90 avec evolution, sorts du monstre.
// Donnees : table characters.character_compagnon. Commandes : .compagnon ...
// L'addon Compagnons (cote jeu) se sert des commandes et lit les lignes "#COMP#".
// ===========================================================================
namespace Compagnons
{
    uint32 const SORT_LASSO            = 1000900;   // sort ajoute a Spell.dbc (jeu + serveur)
    uint32 const PROPRIETES_INVOCATION = 61;        // SummonProperties : allie, gardien, sans emplacement
    uint8  const NB_MAX                = 5;
    uint8  const NIVEAU_MAX            = 90;
    uint32 const DELAI_RAPPEL          = 120;       // secondes apres la mort
    float  const AFFAIBLI_PCT          = 20.0f;     // vie maximum pour capturer
    float  const FACTEUR_AVEC_FAMILIER = 0.4f;      // si un familier de chasseur/demoniste est sorti

    // sorts donnes par le role (en plus de ceux du monstre)
    uint32 const SORT_FRAPPE      = 103002;   // Frappe mortelle (150 % de l'arme)
    uint32 const SORT_PROVOCATION = 355;      // Provocation
    uint32 const SORT_TONNERRE    = 23931;    // Coup de tonnerre
    uint32 const SORT_SOIN        = 41372;    // Soins (sur un allie)

    uint32 const SORTS_RESURRECTION[] = { 2006, 7328, 2008, 50769, 20484, 61999, 982 };

    enum Role : uint8 { ROLE_ATTAQUE = 0, ROLE_TANK = 1, ROLE_SOIN = 2 };

    // sorts que le compagnon peut apprendre, selon son role (niveau requis)
    struct SortAppris { uint32 id; uint8 niveau; };
    std::vector<SortAppris> const CATALOGUE[3] =
    {
        {   // attaque
            { 16509, 1 },  // Pourfendre
            { 9080, 10 },  // Brise-genou
            { 15496, 20 }, // Enchainement
            { 15618, 30 }, // Coup de pied rapide
            { 29426, 40 }, // Frappe heroique
            { 6253, 50 },  // Revers
            { 7160, 60 },  // Execution
            { 8599, 70 },  // Enrager
            { 31408, 80 }, // Choc martial
            { 16856, 90 }, // Frappe mortelle (superieure)
        },
        {   // tank
            { 12170, 1 },  // Revanche
            { 8242, 10 },  // Heurt de bouclier
            { 15062, 20 }, // Mur protecteur
            { 11972, 30 }, // Coup de bouclier
            { 17139, 40 }, // Mot de pouvoir : Bouclier
            { 13005, 50 }, // Marteau de la justice
            { 22356, 60 }, // Lenteur
            { 11831, 70 }, // Nova de givre
            { 31408, 80 }, // Choc martial
            { 11426, 90 }, // Barriere de glace
        },
        {   // soin
            { 2061, 1 },   // Soins rapides
            { 8362, 10 },  // Renovation
            { 17139, 20 }, // Mot de pouvoir : Bouclier
            { 9734, 30 },  // Chatiment sacre
            { 15586, 40 }, // Soins
            { 14032, 50 }, // Mot de l'ombre : Douleur
            { 15981, 60 }, // Recuperation
            { 13005, 70 }, // Marteau de la justice
            { 15799, 80 }, // Salve de guerison
            { 15585, 90 }, // Priere de soins
        },
    };
    enum Comportement : uint8 { COMP_DEFENSIF = 0, COMP_AGRESSIF = 1, COMP_PASSIF = 2 };
    enum Famille : int8 { F_AUCUNE = -1, F_DEGATS = 0, F_SOIN = 1, F_PROTECTION = 2, F_CONTROLE = 3 };

    char const* const NOMS_ROLE[] = { "Attaque", "Tank", "Soin" };
    char const* const NOMS_COMP[] = { "D\xC3\xA9" "fensif", "Agressif", "Passif" };

    struct Fiche
    {
        uint8 slot = 0;
        uint32 entry = 0;
        uint32 display = 0;
        uint8 niveau = 1;
        uint32 xp = 0;
        uint8 role = ROLE_ATTAQUE;
        uint8 comportement = COMP_DEFENSIF;
        std::vector<uint32> sorts;
        uint64 mortJusqua = 0;
    };

    struct Donnees
    {
        std::map<uint8, Fiche> fiches;
        int8 actif = -1;           // emplacement du compagnon sorti (-1 : aucun)
        ObjectGuid invoque;        // creature actuellement invoquee
        bool charge = false;
    };

    std::mutex gVerrou;
    std::unordered_map<ObjectGuid::LowType, Donnees> gDonnees;

    // ------------------------------------------------------------------ outils
    void Dire(Player* player, std::string const& texte)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(texte.c_str());
    }

    std::string NomEntree(uint32 entry)
    {
        if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry))
            return ct->Name;
        return "?";
    }

    uint8 Palier(uint8 niveau)
    {
        uint8 p = 0;
        for (uint8 seuil : { 20, 40, 60, 80, 90 })
            if (niveau >= seuil)
                ++p;
        return p;
    }

    uint8 SortsDebloques(uint8 niveau) { return std::min<uint8>(NB_MAX, uint8(1 + Palier(niveau))); }

    uint32 XpPourNiveau(uint8 niveau)
    {
        uint32 base = sObjectMgr->GetXPForLevel(std::min<uint8>(niveau, 84));
        double v = base ? base : 1000.0;
        if (niveau >= 85)
            v *= std::pow(1.10, niveau - 84);
        return std::max<uint32>(100, uint32(v / 5.0));
    }

    std::string SortsTexte(std::vector<uint32> const& sorts)
    {
        std::string s;
        for (uint32 id : sorts)
        {
            if (!s.empty())
                s += ",";
            s += std::to_string(id);
        }
        return s;
    }

    std::vector<uint32> SortsDepuisTexte(std::string const& texte)
    {
        std::vector<uint32> out;
        for (std::string_view part : Trinity::Tokenize(texte, ',', false))
        {
            if (part.empty())
                continue;
            out.push_back(uint32(strtoul(std::string(part).c_str(), nullptr, 10)));   // 0 = emplacement vide
        }
        out.resize(NB_MAX, 0);
        return out;
    }

    // ------------------------------------------------------------ sorts appris
    std::string NomSort(uint32 id)
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(id);
        return (si && si->SpellName) ? std::string(si->SpellName) : std::to_string(id);
    }

    SortAppris const* DansCatalogue(uint8 role, uint32 id)
    {
        for (SortAppris const& s : CATALOGUE[std::min<uint8>(role, ROLE_SOIN)])
            if (s.id == id)
                return &s;
        return nullptr;
    }

    bool DansUnCatalogue(uint32 id)
    {
        for (uint8 r = 0; r < 3; ++r)
            if (DansCatalogue(r, id))
                return true;
        return false;
    }

    // remplit les emplacements debloques encore vides avec les meilleurs sorts du role
    std::vector<uint32> Completer(Fiche& f)
    {
        std::vector<uint32> appris;
        f.sorts.resize(NB_MAX, 0);
        uint8 ouverts = std::min<uint8>(NB_MAX, uint8(1 + Palier(f.niveau)));
        for (uint8 s = 0; s < ouverts; ++s)
        {
            if (f.sorts[s])
                continue;
            SortAppris const* choix = nullptr;
            for (SortAppris const& c : CATALOGUE[std::min<uint8>(f.role, ROLE_SOIN)])
                if (c.niveau <= f.niveau && std::find(f.sorts.begin(), f.sorts.end(), c.id) == f.sorts.end()
                    && sSpellMgr->GetSpellInfo(c.id) && (!choix || c.niveau > choix->niveau))
                    choix = &c;
            if (!choix)
                break;
            f.sorts[s] = choix->id;
            appris.push_back(choix->id);
        }
        return appris;
    }

    std::string TexteAppris(std::vector<uint32> const& appris)
    {
        std::string t;
        for (uint32 id : appris)
            t += (t.empty() ? "" : ", ") + NomSort(id);
        return t;
    }

    void Sauver(ObjectGuid::LowType owner, Fiche const& f, bool actif)
    {
        CharacterDatabase.PExecute("REPLACE INTO character_compagnon (owner, slot, entry, display, niveau, xp, role, comportement, sorts, mort_jusqua, actif) "
            "VALUES (%u, %u, %u, %u, %u, %u, %u, %u, '%s', " UI64FMTD ", %u)",
            owner, uint32(f.slot), f.entry, f.display, uint32(f.niveau), f.xp, uint32(f.role), uint32(f.comportement),
            SortsTexte(f.sorts).c_str(), f.mortJusqua, actif ? 1u : 0u);
    }

    void Effacer(ObjectGuid::LowType owner, uint8 slot)
    {
        CharacterDatabase.PExecute("DELETE FROM character_compagnon WHERE owner = %u AND slot = %u", owner, uint32(slot));
    }

    void Charger(Player* player)
    {
        Donnees d;
        if (QueryResult r = CharacterDatabase.PQuery("SELECT slot, entry, display, niveau, xp, role, comportement, sorts, mort_jusqua, actif FROM character_compagnon WHERE owner = %u", player->GetGUID().GetCounter()))
        {
            do
            {
                Field* c = r->Fetch();
                Fiche f;
                f.slot = c[0].GetUInt8();
                f.entry = c[1].GetUInt32();
                f.display = c[2].GetUInt32();
                f.niveau = std::max<uint8>(1, c[3].GetUInt8());
                f.xp = c[4].GetUInt32();
                f.role = std::min<uint8>(c[5].GetUInt8(), ROLE_SOIN);
                f.comportement = std::min<uint8>(c[6].GetUInt8(), COMP_PASSIF);
                f.sorts = SortsDepuisTexte(c[7].GetString());
                f.mortJusqua = c[8].GetUInt64();
                if (f.slot >= NB_MAX || !sObjectMgr->GetCreatureTemplate(f.entry))
                    continue;
                if (c[9].GetUInt8())
                    d.actif = int8(f.slot);
                d.fiches[f.slot] = f;
            } while (r->NextRow());
        }
        // compagnons captures avant l'apprentissage des sorts : ils rattrapent ce qui leur manque
        for (auto& [slot, f] : d.fiches)
            if (!Completer(f).empty())
                Sauver(player->GetGUID().GetCounter(), f, d.actif == int8(slot));
        d.charge = true;
        std::lock_guard<std::mutex> lock(gVerrou);
        gDonnees[player->GetGUID().GetCounter()] = d;
    }

    // ----------------------------------------------------------- classement des sorts
    Famille Classer(SpellInfo const* si)
    {
        if (!si || si->IsPassive() || si->IsChanneled())
            return F_AUCUNE;
        if (si->CalcCastTime() > 4000)
            return F_AUCUNE;
        for (SpellEffects interdit : { SPELL_EFFECT_SUMMON, SPELL_EFFECT_TELEPORT_UNITS, SPELL_EFFECT_INSTAKILL, SPELL_EFFECT_SCRIPT_EFFECT,
                                       SPELL_EFFECT_SUMMON_OBJECT_WILD, SPELL_EFFECT_TRANS_DOOR, SPELL_EFFECT_SUMMON_PET, SPELL_EFFECT_TRIGGER_MISSILE,
                                       SPELL_EFFECT_KILL_CREDIT, SPELL_EFFECT_SEND_EVENT, SPELL_EFFECT_CREATE_ITEM, SPELL_EFFECT_TAMECREATURE })
            if (si->HasEffect(interdit))
                return F_AUCUNE;
        for (AuraType interdit : { SPELL_AURA_MOD_SHAPESHIFT, SPELL_AURA_TRANSFORM, SPELL_AURA_MOUNTED, SPELL_AURA_CONTROL_VEHICLE, SPELL_AURA_MOD_POSSESS,
                                   SPELL_AURA_MOD_CHARM, SPELL_AURA_FEIGN_DEATH, SPELL_AURA_MOD_STEALTH, SPELL_AURA_MOD_INVISIBILITY, SPELL_AURA_BIND_SIGHT,
                                   SPELL_AURA_FAR_SIGHT, SPELL_AURA_PERIODIC_TRIGGER_SPELL })
            if (si->HasAura(interdit))
                return F_AUCUNE;

        if (si->HasEffect(SPELL_EFFECT_HEAL) || si->HasEffect(SPELL_EFFECT_HEAL_PCT) || si->HasAura(SPELL_AURA_PERIODIC_HEAL))
            return F_SOIN;
        if (si->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) || si->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE) || si->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE)
            || si->HasEffect(SPELL_EFFECT_NORMALIZED_WEAPON_DMG) || si->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL) || si->HasEffect(SPELL_EFFECT_HEALTH_LEECH)
            || si->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
            return F_DEGATS;
        if (!si->IsPositive() && (si->HasAura(SPELL_AURA_MOD_STUN) || si->HasAura(SPELL_AURA_MOD_ROOT) || si->HasAura(SPELL_AURA_MOD_DECREASE_SPEED)
            || si->HasAura(SPELL_AURA_MOD_SILENCE) || si->HasAura(SPELL_AURA_MOD_FEAR) || si->HasAura(SPELL_AURA_MOD_CONFUSE) || si->HasAura(SPELL_AURA_MOD_PACIFY)))
            return F_CONTROLE;
        if (si->IsPositive() && (si->HasAura(SPELL_AURA_SCHOOL_ABSORB) || si->HasAura(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN) || si->HasAura(SPELL_AURA_MOD_RESISTANCE)
            || si->HasAura(SPELL_AURA_MOD_MELEE_HASTE) || si->HasAura(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE) || si->HasAura(SPELL_AURA_MOD_INCREASE_HEALTH)
            || si->HasAura(SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT) || si->HasAura(SPELL_AURA_MOD_RESISTANCE_PCT)))
            return F_PROTECTION;
        return F_AUCUNE;
    }

    // les sorts que le monstre lancait : fiche (spell1..8) + scripts SmartAI
    std::vector<uint32> RecolterSorts(Creature* c)
    {
        std::vector<uint32> trouves;
        auto ajoute = [&trouves](uint32 id)
        {
            if (id && std::find(trouves.begin(), trouves.end(), id) == trouves.end())
                trouves.push_back(id);
        };
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        for (uint32 id : ct->spells)
            ajoute(id);
        for (SmartScriptHolder const& h : sSmartScriptMgr->GetScript(int32(ct->Entry), SMART_SCRIPT_TYPE_CREATURE))
            if (h.GetActionType() == SMART_ACTION_CAST || h.GetActionType() == SMART_ACTION_SELF_CAST || h.GetActionType() == SMART_ACTION_INVOKER_CAST)
                ajoute(h.action.cast.spell);

        // un sort de chaque famille d'abord, pour varier, dans l'ordre : soin, degats, controle, protection
        std::vector<uint32> par[4];
        for (uint32 id : trouves)
        {
            Famille f = Classer(sSpellMgr->GetSpellInfo(id));
            if (f != F_AUCUNE)
                par[f].push_back(id);
        }
        std::vector<uint32> out;
        for (size_t rang = 0; out.size() < NB_MAX; ++rang)
        {
            bool encore = false;
            for (int f : { F_SOIN, F_DEGATS, F_CONTROLE, F_PROTECTION })
                if (rang < par[f].size())
                {
                    encore = true;
                    if (out.size() < NB_MAX)
                        out.push_back(par[f][rang]);
                }
            if (!encore)
                break;
        }
        return out;
    }

    // ------------------------------------------------------------- statistiques
    // Valeurs de reference : tables de Cataclysm (celles des monstres de Cataclysm), en prenant
    // le maximum des niveaux inferieurs, car la base a des trous (armure 1 et vie 17 084 aux niveaux 89-90).
    float ValeurMontante(uint8 niveau, int quoi)
    {
        float meilleur = 0.0f;
        uint8 dernier = 1;
        for (uint8 l = 1; l <= std::min<uint8>(niveau, 88); ++l)
            if (CreatureBaseStats const* bs = sObjectMgr->GetCreatureBaseStats(l, UNIT_CLASS_WARRIOR))
            {
                float v = quoi == 0 ? bs->BaseHealth[3] : (quoi == 1 ? bs->BaseDamage[3] : float(bs->BaseArmor));
                if (v > meilleur)
                {
                    meilleur = v;
                    dernier = l;
                }
            }
        if (niveau > dernier)                       // au-dela des donnees fiables : +4 % par niveau
            meilleur *= std::pow(1.04f, float(niveau - dernier));
        return meilleur;
    }

    float DegatsDeBase(uint8 niveau)
    {
        float d = ValeurMontante(niveau, 1);
        return d < 1.0f ? 2.0f + niveau * 2.4f : d;
    }

    uint32 VieDeBase(uint8 niveau)
    {
        float v = ValeurMontante(niveau, 0);
        return v < 40.0f ? 40 + niveau * 120 : uint32(v);
    }

    float ArmureDeBase(uint8 niveau)
    {
        float a = ValeurMontante(niveau, 2);
        return a < 10.0f ? 20.0f + niveau * 60.0f : a;
    }

    void AppliquerStats(Creature* c, Fiche const& f, bool affaibli)
    {
        float mult = 1.0f + 0.08f * Palier(f.niveau);
        if (affaibli)
            mult *= FACTEUR_AVEC_FAMILIER;
        float vie = 1.0f, deg = 1.0f, armure = 1.0f, resist = 1.0f;
        float vieMaitre = 1.0f, armureMaitre = 0.8f;    // part de la vie / armure du joueur (minimum garanti)
        switch (f.role)
        {
            case ROLE_TANK: vie = 2.5f; deg = 0.6f; armure = 2.0f; resist = 2.0f; vieMaitre = 2.0f; armureMaitre = 1.5f; break;
            case ROLE_SOIN: vie = 0.9f; deg = 0.5f; vieMaitre = 0.9f; break;
            default: break;
        }
        float pct = c->GetMaxHealth() ? c->GetHealthPct() : 100.0f;

        float hp = VieDeBase(f.niveau) * vie;
        float ar = ArmureDeBase(f.niveau) * armure;
        // jamais plus faible que son maitre (a niveau egal) : suit l'equipement du joueur
        if (Unit* maitre = c->GetOwner())
            if (maitre->GetTypeId() == TYPEID_PLAYER && maitre->getLevel() > 0)
            {
                float part = std::min(1.0f, float(f.niveau) / float(maitre->getLevel()));
                hp = std::max(hp, float(maitre->GetMaxHealth()) * vieMaitre * part);
                ar = std::max(ar, float(maitre->GetArmor()) * armureMaitre * part);
            }

        c->SetLevel(f.niveau);
        c->SetCreateHealth(uint32(hp * mult));
        c->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, 0.0f);
        c->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, ar * mult);
        for (uint8 ecole = SPELL_SCHOOL_HOLY; ecole < MAX_SPELL_SCHOOL; ++ecole)
        {
            c->SetStatFlatModifier(UnitMods(UNIT_MOD_RESISTANCE_START + ecole), BASE_VALUE, f.niveau * 1.5f * resist * mult);
            c->UpdateResistances(ecole);
        }
        float d = DegatsDeBase(f.niveau) * deg * mult;
        c->SetBaseAttackTime(BASE_ATTACK, 2000);
        c->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, d);
        c->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, d * 1.5f);
        c->UpdateMaxHealth();
        c->UpdateArmor();
        c->UpdateAttackPowerAndDamage(false);
        c->SetHealth(std::max<uint32>(1, uint32(c->GetMaxHealth() * pct / 100.0f)));
        c->SetObjectScale(c->GetNativeObjectScale() * (1.0f + 0.05f * Palier(f.niveau)));
    }

    bool ConnaitResurrection(Player* p)
    {
        for (uint32 s : SORTS_RESURRECTION)
            if (p->HasSpell(s))
                return true;
        return false;
    }

    void EnvoyerListe(Player* player);
    Creature* Invoquer(Player* player, uint8 slot);
    void AjouterXp(Player* owner, Creature* compagnon, Unit* tue);

    // ------------------------------------------------------------ intelligence
    class CompagnonAI : public CreatureAI
    {
    public:
        CompagnonAI(Creature* c, ObjectGuid owner, Fiche const& f, bool affaibli) : CreatureAI(c), _owner(owner), _fiche(f), _affaibli(affaibli) { }

        void InitializeAI() override
        {
            AppliquerReaction();
            Suivre();
        }

        void AppliquerReaction()
        {
            switch (_fiche.comportement)
            {
                case COMP_AGRESSIF: me->SetReactState(REACT_AGGRESSIVE); break;
                case COMP_PASSIF:   me->SetReactState(REACT_PASSIVE); break;
                default:            me->SetReactState(REACT_DEFENSIVE); break;
            }
        }

        void ChangerFiche(Fiche const& f)
        {
            _fiche = f;
            AppliquerReaction();
            AppliquerStats(me, _fiche, _affaibli);
            if (_fiche.comportement == COMP_PASSIF && me->GetVictim())
                EnterEvadeMode(EVADE_REASON_OTHER);
        }

        Player* Maitre() const { return ObjectAccessor::GetPlayer(*me, _owner); }

        void Suivre()
        {
            if (Player* p = Maitre())
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                    me->GetMotionMaster()->MoveFollow(p, 2.5f, float(M_PI) * 0.75f);
            }
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            me->CombatStop(true);
            me->GetThreatManager().ClearAllThreat();
            me->GetMotionMaster()->Clear();
            Suivre();
        }

        bool PeutAttaquer(Unit* cible) const
        {
            return cible && cible->IsAlive() && me->IsValidAttackTarget(cible) && !cible->HasBreakableByDamageCrowdControlAura();
        }

        void Attaquer(Unit* cible)
        {
            if (!PeutAttaquer(cible) || _fiche.comportement == COMP_PASSIF)
                return;
            if (me->GetVictim() == cible)
                return;
            me->Attack(cible, _fiche.role != ROLE_SOIN);
            if (_fiche.role != ROLE_SOIN)
                me->GetMotionMaster()->MoveChase(cible);
        }

        void OwnerAttackedBy(Unit* attacker) override { if (_fiche.comportement != COMP_PASSIF) Attaquer(attacker); }
        void OwnerAttacked(Unit* target) override { if (_fiche.comportement != COMP_PASSIF) Attaquer(target); }
        void AttackStart(Unit* who) override { Attaquer(who); }

        void JustDied(Unit* /*killer*/) override
        {
            Player* p = Maitre();
            ObjectGuid::LowType low = _owner.GetCounter();
            Fiche copie;
            {
                std::lock_guard<std::mutex> lock(gVerrou);
                auto it = gDonnees.find(low);
                if (it == gDonnees.end())
                    return;
                auto fi = it->second.fiches.find(_fiche.slot);
                if (fi == it->second.fiches.end())
                    return;
                fi->second.mortJusqua = uint64(GameTime::GetGameTime()) + DELAI_RAPPEL;
                it->second.actif = -1;
                it->second.invoque.Clear();
                copie = fi->second;
            }
            Sauver(low, copie, false);
            if (p)
            {
                Dire(p, "|cffff6060" + NomEntree(copie.entry) + " est tomb\xC3\xA9" " au combat.|r Vous pourrez le rappeler dans 2 minutes, ou le ressusciter tout de suite avec un sort de r\xC3\xA9" "surrection (bouton \xC2\xAB" " Ressusciter \xC2\xBB" " de l'addon).");
                EnvoyerListe(p);
            }
        }

        void KilledUnit(Unit* victim) override
        {
            if (Player* p = Maitre())
                AjouterXp(p, me, victim);
        }

        uint32 CalculerValeur(SpellInfo const* si, uint8 i, Unit* cible) const
        {
            SpellEffectInfo const& e = si->Effects[i];
            float d = DegatsDeBase(_fiche.niveau) * (1.0f + 0.08f * Palier(_fiche.niveau)) * (_affaibli ? FACTEUR_AVEC_FAMILIER : 1.0f);
            switch (e.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                    return uint32(d * (_fiche.role == ROLE_ATTAQUE ? 3.0f : 2.0f));
                case SPELL_EFFECT_HEAL:
                    return uint32((cible ? cible->GetMaxHealth() : me->GetMaxHealth()) * (_fiche.role == ROLE_SOIN ? 0.20f : 0.10f));
                case SPELL_EFFECT_APPLY_AURA:
                    if (e.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE)
                        return uint32(d * 0.7f);
                    if (e.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL)
                        return uint32((cible ? cible->GetMaxHealth() : me->GetMaxHealth()) * 0.04f);
                    if (e.ApplyAuraName == SPELL_AURA_SCHOOL_ABSORB)
                        return uint32(me->GetMaxHealth() * 0.15f);
                    return 0;
                default:
                    return 0;
            }
        }

        bool Lancer(uint32 id, Unit* cible, uint32 recharge)
        {
            if (!cible || _recharges[id] > _horloge)
                return false;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(id);
            if (!si)
                return false;
            CastSpellExtraArgs args(TriggerCastFlags(TRIGGERED_IGNORE_POWER_COST | TRIGGERED_IGNORE_REAGENT_COST | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD));
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (uint32 v = CalculerValeur(si, i, cible))
                    args.AddSpellMod(SpellValueMod(SPELLVALUE_BASE_POINT0 + i), int32(v));
            if (me->CastSpell(cible, id, args) != SPELL_CAST_OK)
            {
                _recharges[id] = _horloge + 3000;
                return false;
            }
            _recharges[id] = _horloge + std::max<uint32>(recharge, si->GetRecoveryTime());
            return true;
        }

        // allie le plus blesse (maitre, groupe, compagnon lui-meme)
        Unit* AllieBlesse(Player* p, float seuil) const
        {
            Unit* pire = nullptr;
            float pirePct = seuil;
            auto teste = [&](Unit* u)
            {
                if (u && u->IsAlive() && u->IsInMap(me) && me->IsWithinDistInMap(u, 40.0f) && u->GetHealthPct() < pirePct)
                {
                    pire = u;
                    pirePct = u->GetHealthPct();
                }
            };
            teste(p);
            teste(me);
            if (Group* g = p->GetGroup())
                for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                    teste(ref->GetSource());
            return pire;
        }

        void Provoquer(Player* p)
        {
            std::vector<Unit*> membres = { p };
            if (Group* g = p->GetGroup())
                for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                    if (Player* m = ref->GetSource())
                        if (m != p && m->IsInMap(me) && me->IsWithinDistInMap(m, 40.0f))
                            membres.push_back(m);
            for (Unit* m : membres)
            {
                std::vector<Unit*> attaquants(m->getAttackers().begin(), m->getAttackers().end());
                for (Unit* a : attaquants)
                {
                    if (!a || a->GetTypeId() != TYPEID_UNIT || a->GetVictim() == me || !PeutAttaquer(a) || !me->IsWithinDistInMap(a, 30.0f))
                        continue;
                    if (_recharges[SORT_PROVOCATION] <= _horloge)
                    {
                        me->CastSpell(a, SORT_PROVOCATION, true);
                        _recharges[SORT_PROVOCATION] = _horloge + 4000;
                    }
                    a->GetThreatManager().MatchUnitThreatToHighestThreat(me);
                    a->GetThreatManager().AddThreat(me, float(me->GetMaxHealth()) * 0.2f, nullptr, true, true);
                    if (!me->GetVictim())
                        Attaquer(a);
                }
            }
        }

        void Reflechir(Player* p)
        {
            bool affaibli = p->GetPet() != nullptr;
            if (affaibli != _affaibli || p->GetMaxHealth() != _vieMaitre || uint32(p->GetArmor()) != _armureMaitre)
            {
                _affaibli = affaibli;
                _vieMaitre = p->GetMaxHealth();
                _armureMaitre = uint32(p->GetArmor());
                AppliquerStats(me, _fiche, _affaibli);
            }

            if (_fiche.comportement != COMP_PASSIF)
            {
                if (!me->GetVictim())
                {
                    Unit* cible = nullptr;
                    if (p->GetVictim() && p->IsInCombat())
                        cible = p->GetVictim();
                    else if (!p->getAttackers().empty())
                        cible = *p->getAttackers().begin();
                    if (cible && me->IsWithinDistInMap(cible, 40.0f))
                        Attaquer(cible);
                }
                if (_fiche.role == ROLE_TANK)
                    Provoquer(p);
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // soins (role soin, ou sorts de soin du monstre pour les autres roles en cas d'urgence)
            float seuil = _fiche.role == ROLE_SOIN ? 70.0f : 35.0f;
            if (Unit* blesse = AllieBlesse(p, seuil))
            {
                for (uint32 id : SortsActifs())
                    if (Classer(sSpellMgr->GetSpellInfo(id)) == F_SOIN && Lancer(id, blesse, 6000))
                        return;
                if (_fiche.role == ROLE_SOIN && Lancer(SORT_SOIN, blesse, 5000))
                    return;
            }

            Unit* victime = me->GetVictim();
            if (!victime)
                return;

            for (uint32 id : SortsActifs())
            {
                switch (Classer(sSpellMgr->GetSpellInfo(id)))
                {
                    case F_DEGATS:
                        if (Lancer(id, victime, 5000)) return;
                        break;
                    case F_CONTROLE:
                        if (Lancer(id, victime, 15000)) return;
                        break;
                    case F_PROTECTION:
                        if (Lancer(id, _fiche.role == ROLE_TANK ? me : static_cast<Unit*>(p), 20000)) return;
                        break;
                    default:
                        break;
                }
            }

            switch (_fiche.role)
            {
                case ROLE_ATTAQUE: Lancer(SORT_FRAPPE, victime, 8000); break;
                case ROLE_TANK:    Lancer(SORT_TONNERRE, victime, 10000); break;
                default: break;
            }
        }

        std::vector<uint32> SortsActifs() const
        {
            std::vector<uint32> v;
            for (size_t i = 0; i < std::min<size_t>(_fiche.sorts.size(), SortsDebloques(_fiche.niveau)); ++i)
                if (_fiche.sorts[i])
                    v.push_back(_fiche.sorts[i]);
            return v;
        }

        void UpdateAI(uint32 diff) override
        {
            _horloge += diff;
            Player* p = Maitre();
            if (!p || !p->IsInWorld() || p->GetMap() != me->GetMap())
            {
                me->DespawnOrUnsummon();
                return;
            }

            if (!me->IsWithinDistInMap(p, 70.0f))
            {
                me->CombatStop(true);
                me->NearTeleportTo(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
                Suivre();
                return;
            }

            if (_reflexion <= diff)
            {
                _reflexion = 1000;
                Reflechir(p);
            }
            else
                _reflexion -= diff;

            if (Unit* v = me->GetVictim())
            {
                if (!PeutAttaquer(v) || (_fiche.comportement == COMP_PASSIF))
                {
                    EnterEvadeMode(EVADE_REASON_OTHER);
                    return;
                }
                if (_fiche.role != ROLE_SOIN)
                    DoMeleeAttackIfReady();
            }
            else if (!me->HasUnitState(UNIT_STATE_CASTING))
                Suivre();
        }

    private:
        ObjectGuid _owner;
        Fiche _fiche;
        bool _affaibli;
        uint32 _vieMaitre = 0;
        uint32 _armureMaitre = 0;
        uint32 _horloge = 0;
        uint32 _reflexion = 1000;
        std::map<uint32, uint32> _recharges;
    };

    // --------------------------------------------------------------- invocation
    Creature* Trouver(Player* player)
    {
        ObjectGuid g;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(player->GetGUID().GetCounter());
            if (it == gDonnees.end())
                return nullptr;
            g = it->second.invoque;
        }
        if (!g)
            return nullptr;
        Creature* c = ObjectAccessor::GetCreature(*player, g);
        return (c && c->IsAlive()) ? c : nullptr;
    }

    void Renvoyer(Player* player, bool oublierActif)
    {
        if (Creature* c = Trouver(player))
            if (TempSummon* s = c->ToTempSummon())
                s->UnSummon();
        Fiche copie;
        bool sauver = false;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(player->GetGUID().GetCounter());
            if (it == gDonnees.end())
                return;
            it->second.invoque.Clear();
            if (oublierActif && it->second.actif >= 0)
            {
                auto fi = it->second.fiches.find(uint8(it->second.actif));
                if (fi != it->second.fiches.end())
                {
                    copie = fi->second;
                    sauver = true;
                }
                it->second.actif = -1;
            }
        }
        if (sauver)
            Sauver(player->GetGUID().GetCounter(), copie, false);
    }

    Creature* Invoquer(Player* player, uint8 slot)
    {
        Fiche f;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(player->GetGUID().GetCounter());
            if (it == gDonnees.end())
                return nullptr;
            auto fi = it->second.fiches.find(slot);
            if (fi == it->second.fiches.end())
                return nullptr;
            f = fi->second;
        }
        SummonPropertiesEntry const* prop = sSummonPropertiesStore.LookupEntry(PROPRIETES_INVOCATION);
        if (!prop || !player->IsInWorld())
            return nullptr;

        Renvoyer(player, false);

        SummonCreatureExtraArgs args;
        args.SummonProperties = prop;
        args.Summoner = player;
        TempSummon* s = player->GetMap()->SummonCreature(f.entry, player->GetNearPosition(2.0f, float(M_PI)), args);
        if (!s)
            return nullptr;

        s->RemoveAllAuras();
        s->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_NONE);
        s->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_PACIFIED | UNIT_FLAG_STUNNED);
        s->SetFaction(player->GetFaction());
        if (f.display)
            s->SetDisplayId(f.display);
        s->SetCorpseDelay(0);
        bool affaibli = player->GetPet() != nullptr;
        AppliquerStats(s, f, affaibli);
        s->SetFullHealth();
        s->AIM_Initialize(new CompagnonAI(s, player->GetGUID(), f, affaibli));

        Fiche copie;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            Donnees& d = gDonnees[player->GetGUID().GetCounter()];
            d.invoque = s->GetGUID();
            d.actif = int8(slot);
            copie = d.fiches[slot];
        }
        Sauver(player->GetGUID().GetCounter(), copie, true);
        return s;
    }

    // ---------------------------------------------------------------- experience
    void AjouterXp(Player* owner, Creature* compagnon, Unit* tue)
    {
        if (!owner || !compagnon || !tue || tue->GetTypeId() != TYPEID_UNIT || !compagnon->IsAlive())
            return;
        Creature* victime = tue->ToCreature();
        if (victime->IsCritter() || victime->IsTotem() || victime->IsPet() || victime->GetOwnerGUID().IsPlayer())
            return;

        std::string message;
        bool evolution = false;
        Fiche copie;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(owner->GetGUID().GetCounter());
            if (it == gDonnees.end() || it->second.actif < 0)
                return;
            Fiche& f = it->second.fiches[uint8(it->second.actif)];
            uint8 plafond = std::min<uint8>(NIVEAU_MAX, owner->getLevel());
            if (f.niveau >= plafond)
                return;
            int diff = int(victime->getLevel()) - int(f.niveau);
            float facteur = diff >= 0 ? 1.0f + 0.05f * std::min(diff, 4) : std::max(0.0f, 1.0f + diff / 10.0f);
            uint32 baseCont = victime->getLevel() <= 60 ? 45 : (victime->getLevel() <= 70 ? 235 : (victime->getLevel() <= 80 ? 580 : 1878));
            uint32 gain = uint32((f.niveau * 5 + baseCont) * facteur);
            if (victime->isElite())
                gain *= 2;
            if (!gain)
                return;
            f.xp += gain;
            uint8 avant = f.niveau;
            while (f.niveau < plafond && f.xp >= XpPourNiveau(f.niveau))
            {
                f.xp -= XpPourNiveau(f.niveau);
                ++f.niveau;
            }
            if (f.niveau >= plafond)
                f.xp = std::min(f.xp, XpPourNiveau(f.niveau) - 1);
            if (f.niveau != avant)
            {
                message = "|cff40ff40" + NomEntree(f.entry) + " passe au niveau " + std::to_string(f.niveau) + " !|r";
                if (Palier(f.niveau) != Palier(avant))
                {
                    evolution = true;
                    message += " |cffffd100Il \xC3\xA9" "volue : il devient plus fort et d\xC3\xA9" "bloque un nouvel emplacement de sort.|r";
                }
                std::vector<uint32> appris = Completer(f);
                if (!appris.empty())
                    message += " |cff80c0ffIl apprend : " + TexteAppris(appris) + ".|r";
            }
            copie = f;
        }
        Sauver(owner->GetGUID().GetCounter(), copie, true);
        if (!message.empty())
        {
            if (compagnon->IsAIEnabled())
                if (CompagnonAI* ai = dynamic_cast<CompagnonAI*>(compagnon->AI()))
                    ai->ChangerFiche(copie);
            if (evolution)
                compagnon->SetFullHealth();
            Dire(owner, message);
            EnvoyerListe(owner);
        }
    }

    // ------------------------------------------------------- echanges avec l'addon
    void EnvoyerListe(Player* player)
    {
        std::vector<std::string> lignes;
        int8 actif = -1;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(player->GetGUID().GetCounter());
            if (it != gDonnees.end())
            {
                actif = it->second.actif;
                uint64 maintenant = uint64(GameTime::GetGameTime());
                for (auto const& [slot, f] : it->second.fiches)
                {
                    uint32 reste = f.mortJusqua > maintenant ? uint32(f.mortJusqua - maintenant) : 0;
                    std::string catalogue;
                    for (SortAppris const& c : CATALOGUE[std::min<uint8>(f.role, ROLE_SOIN)])
                        catalogue += (catalogue.empty() ? "" : ",") + std::to_string(c.id) + ":" + std::to_string(c.niveau);
                    lignes.push_back("#COMP#L~" + std::to_string(slot + 1) + "~" + std::to_string(f.entry) + "~" + std::to_string(f.display) + "~"
                        + std::to_string(f.niveau) + "~" + std::to_string(f.xp) + "~" + std::to_string(XpPourNiveau(f.niveau)) + "~"
                        + std::to_string(f.role) + "~" + std::to_string(f.comportement) + "~" + std::to_string(reste) + "~"
                        + std::to_string(SortsDebloques(f.niveau)) + "~" + SortsTexte(f.sorts) + "~" + catalogue + "~" + NomEntree(f.entry));
                }
            }
        }
        Dire(player, "#COMP#DEBUT~" + std::to_string(actif + 1) + "~" + std::to_string(ConnaitResurrection(player) ? 1 : 0));
        for (std::string const& l : lignes)
            Dire(player, l);
        Dire(player, "#COMP#FIN");
    }

    bool LireSlot(ChatHandler* handler, char const* args, uint8& slot)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Indiquez le num\xC3\xA9" "ro du compagnon (1 \xC3\xA0" " 5).");
            return false;
        }
        int v = atoi(args);
        if (v < 1 || v > NB_MAX)
        {
            handler->SendSysMessage("Num\xC3\xA9" "ro de compagnon invalide (1 \xC3\xA0" " 5).");
            return false;
        }
        slot = uint8(v - 1);
        return true;
    }

    bool Existe(Player* p, uint8 slot, Fiche* sortie = nullptr)
    {
        std::lock_guard<std::mutex> lock(gVerrou);
        auto it = gDonnees.find(p->GetGUID().GetCounter());
        if (it == gDonnees.end())
            return false;
        auto fi = it->second.fiches.find(slot);
        if (fi == it->second.fiches.end())
            return false;
        if (sortie)
            *sortie = fi->second;
        return true;
    }
}

using namespace Compagnons;

// ---------------------------------------------------------------- le lasso
class spell_compagnon_lasso : public SpellScript
{
    SpellCastResult Verifier(Player* p, Creature* c, bool parler)
    {
        auto refus = [&](char const* texte) { if (parler) Dire(p, std::string("|cffff6060") + texte + "|r"); return SPELL_FAILED_DONT_REPORT; };
        if (!c || !c->IsAlive())
            return refus("Visez un monstre vivant.");
        if (c->IsPet() || c->IsSummon() || c->IsTotem() || c->GetOwnerGUID() || c->GetCharmerGUID() || c->IsVehicle())
            return refus("Cette cr\xC3\xA9" "ature ne peut pas \xC3\xAA" "tre captur\xC3\xA9" "e.");
        if (c->isWorldBoss() || c->IsDungeonBoss() || c->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS)
            return refus("Les boss ne peuvent pas \xC3\xAA" "tre captur\xC3\xA9" "s.");
        if (c->GetHealthPct() > AFFAIBLI_PCT)
            return refus("Affaiblissez-le d'abord : il doit avoir 20 % de vie ou moins.");
        std::lock_guard<std::mutex> lock(gVerrou);
        auto it = gDonnees.find(p->GetGUID().GetCounter());
        if (it != gDonnees.end() && it->second.fiches.size() >= NB_MAX)
            return refus("Vous avez d\xC3\xA9" "j\xC3\xA0" " 5 compagnons. Rel\xC3\xA2" "chez-en un pour faire de la place.");
        return SPELL_CAST_OK;
    }

    SpellCastResult CheckCast()
    {
        Player* p = GetCaster()->ToPlayer();
        if (!p)
            return SPELL_FAILED_DONT_REPORT;
        Unit* t = GetExplTargetUnit();
        return Verifier(p, t ? t->ToCreature() : nullptr, true);
    }

    void Capturer(SpellEffIndex /*effIndex*/)
    {
        Player* p = GetCaster()->ToPlayer();
        Creature* c = GetHitCreature();
        if (!p || !c || Verifier(p, c, true) != SPELL_CAST_OK)
            return;

        float chance = 50.0f + (AFFAIBLI_PCT - c->GetHealthPct()) * 2.5f;
        if (!roll_chance_f(chance))
        {
            Dire(p, "|cffffd100Le lasso a gliss\xC3\xA9" " ! " + NomEntree(c->GetEntry()) + " se d\xC3\xA9" "bat... R\xC3\xA9" "essayez.|r");
            return;
        }

        Fiche f;
        f.entry = c->GetEntry();
        f.display = c->GetDisplayId();
        f.niveau = 1;
        f.sorts = RecolterSorts(c);
        size_t sortsDuMonstre = f.sorts.size();
        f.sorts.resize(NB_MAX, 0);
        Completer(f);
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            Donnees& d = gDonnees[p->GetGUID().GetCounter()];
            uint8 libre = NB_MAX;
            for (uint8 i = 0; i < NB_MAX; ++i)
                if (!d.fiches.count(i))
                {
                    libre = i;
                    break;
                }
            if (libre == NB_MAX)
                return;
            f.slot = libre;
            d.fiches[libre] = f;
        }
        Sauver(p->GetGUID().GetCounter(), f, false);

        std::string nom = NomEntree(f.entry);
        c->DespawnOrUnsummon();
        Dire(p, "|cff40ff40Vous avez captur\xC3\xA9" " " + nom + " ! Il devient votre compagnon n\xC2\xB0" "" + std::to_string(f.slot + 1) + " (niveau 1, "
            + std::to_string(sortsDuMonstre) + " sort(s) appris du monstre).|r Ouvrez la fen\xC3\xAA" "tre Compagnons pour l'invoquer.");
        EnvoyerListe(p);
    }

    void Register() override
    {
        OnCheckCast.Register(&spell_compagnon_lasso::CheckCast);
        OnEffectHitTarget.Register(&spell_compagnon_lasso::Capturer, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// ---------------------------------------------------------------- joueur
class compagnons_playerscript : public PlayerScript
{
public:
    compagnons_playerscript() : PlayerScript("compagnons_playerscript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        Charger(player);
        int8 actif = -1;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            actif = gDonnees[player->GetGUID().GetCounter()].actif;
        }
        if (actif >= 0)
            Invoquer(player, uint8(actif));
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(gVerrou);
        gDonnees.erase(player->GetGUID().GetCounter());
    }

    void OnMapChanged(Player* player) override
    {
        int8 actif = -1;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(player->GetGUID().GetCounter());
            if (it == gDonnees.end())
                return;
            actif = it->second.actif;
        }
        if (actif >= 0 && !Trouver(player))
            Invoquer(player, uint8(actif));
    }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (Creature* c = Trouver(killer))
            if (c->IsWithinDistInMap(killed, 60.0f))
                AjouterXp(killer, c, killed);
    }
};

// ---------------------------------------------------------------- commandes
class compagnons_commandscript : public CommandScript
{
public:
    compagnons_commandscript() : CommandScript("compagnons_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> sous =
        {
            { "liste",        rbac::RBAC_PERM_COMMAND_SAVE,    false, &Liste,        "" },
            { "invoquer",     rbac::RBAC_PERM_COMMAND_SAVE,    false, &InvoquerCmd,  "" },
            { "renvoyer",     rbac::RBAC_PERM_COMMAND_SAVE,    false, &RenvoyerCmd,  "" },
            { "role",         rbac::RBAC_PERM_COMMAND_SAVE,    false, &RoleCmd,      "" },
            { "comportement", rbac::RBAC_PERM_COMMAND_SAVE,    false, &CompCmd,      "" },
            { "relacher",     rbac::RBAC_PERM_COMMAND_SAVE,    false, &RelacherCmd,  "" },
            { "ranimer",      rbac::RBAC_PERM_COMMAND_SAVE,    false, &RanimerCmd,   "" },
            { "sort",         rbac::RBAC_PERM_COMMAND_SAVE,    false, &SortCmd,      "" },
            { "stats",        rbac::RBAC_PERM_COMMAND_SAVE,    false, &StatsCmd,     "" },
            { "niveau",       rbac::RBAC_PERM_COMMAND_LEVELUP, false, &NiveauCmd,    "" },
        };
        static std::vector<ChatCommand> table =
        {
            { "compagnon", rbac::RBAC_PERM_COMMAND_SAVE, false, nullptr, "", sous },
        };
        return table;
    }

    static Player* Joueur(ChatHandler* h) { return h->GetSession() ? h->GetSession()->GetPlayer() : nullptr; }

    static bool Liste(ChatHandler* h, char const* /*args*/)
    {
        if (Player* p = Joueur(h))
            EnvoyerListe(p);
        return true;
    }

    static bool InvoquerCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        uint8 slot;
        if (!p || !LireSlot(h, args, slot))
            return true;
        Fiche f;
        if (!Existe(p, slot, &f))
        {
            h->SendSysMessage("Vous n'avez pas de compagnon \xC3\xA0" " cet emplacement.");
            return true;
        }
        if (!p->IsAlive())
        {
            h->SendSysMessage("Vous ne pouvez pas invoquer de compagnon en \xC3\xA9" "tant mort.");
            return true;
        }
        uint64 maintenant = uint64(GameTime::GetGameTime());
        if (f.mortJusqua > maintenant)
        {
            h->PSendSysMessage("%s se remet de ses blessures : encore %u secondes (ou ressuscitez-le).", NomEntree(f.entry).c_str(), uint32(f.mortJusqua - maintenant));
            return true;
        }
        if (Invoquer(p, slot))
            h->PSendSysMessage("|cff40ff40%s vous rejoint.|r", NomEntree(f.entry).c_str());
        else
            h->SendSysMessage("Impossible d'invoquer ce compagnon ici.");
        EnvoyerListe(p);
        return true;
    }

    static bool RenvoyerCmd(ChatHandler* h, char const* /*args*/)
    {
        if (Player* p = Joueur(h))
        {
            Renvoyer(p, true);
            h->SendSysMessage("Votre compagnon est renvoy\xC3\xA9" ".");
            EnvoyerListe(p);
        }
        return true;
    }

    static bool ModifierFiche(Player* p, uint8 slot, std::function<void(Fiche&)> const& changement)
    {
        Fiche copie;
        bool actif = false;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            auto it = gDonnees.find(p->GetGUID().GetCounter());
            if (it == gDonnees.end())
                return false;
            auto fi = it->second.fiches.find(slot);
            if (fi == it->second.fiches.end())
                return false;
            changement(fi->second);
            copie = fi->second;
            actif = it->second.actif == int8(slot);
        }
        Sauver(p->GetGUID().GetCounter(), copie, actif);
        if (actif)
            if (Creature* c = Trouver(p))
                if (c->IsAIEnabled())
                    if (CompagnonAI* ai = dynamic_cast<CompagnonAI*>(c->AI()))
                        ai->ChangerFiche(copie);
        return true;
    }

    static bool RoleCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        if (!p || !args)
            return true;
        std::string texte(args);
        std::istringstream in(texte);
        int numero = 0;
        std::string role;
        in >> numero >> role;
        if (numero < 1 || numero > NB_MAX)
        {
            h->SendSysMessage("Utilisation : .compagnon role <1-5> <attaque|tank|soin>");
            return true;
        }
        uint8 r;
        if (role == "attaque") r = ROLE_ATTAQUE;
        else if (role == "tank") r = ROLE_TANK;
        else if (role == "soin") r = ROLE_SOIN;
        else
        {
            h->SendSysMessage("R\xC3\xB4" "le inconnu : attaque, tank ou soin.");
            return true;
        }
        if (p->IsInCombat())
        {
            h->SendSysMessage("Impossible de changer de r\xC3\xB4" "le pendant un combat.");
            return true;
        }
        std::vector<uint32> appris;
        if (ModifierFiche(p, uint8(numero - 1), [r, &appris](Fiche& f)
            {
                f.role = r;
                // les sorts appris pour l'ancien role sont remplaces ; ceux du monstre restent
                f.sorts.resize(NB_MAX, 0);
                for (uint32& id : f.sorts)
                    if (id && DansUnCatalogue(id) && !DansCatalogue(r, id))
                        id = 0;
                appris = Completer(f);
            }))
        {
            h->PSendSysMessage("R\xC3\xB4" "le du compagnon %d : %s.", numero, NOMS_ROLE[r]);
            if (!appris.empty())
                h->PSendSysMessage("|cff80c0ffIl apprend : %s.|r", TexteAppris(appris).c_str());
        }
        EnvoyerListe(p);
        return true;
    }

    static bool CompCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        if (!p || !args)
            return true;
        std::string texte(args);
        uint8 c;
        if (texte.find("agressif") == 0) c = COMP_AGRESSIF;
        else if (texte.find("passif") == 0) c = COMP_PASSIF;
        else if (texte.find("defensif") == 0) c = COMP_DEFENSIF;
        else
        {
            h->SendSysMessage("Utilisation : .compagnon comportement <agressif|defensif|passif>");
            return true;
        }
        int8 actif;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            actif = gDonnees[p->GetGUID().GetCounter()].actif;
        }
        if (actif < 0)
        {
            h->SendSysMessage("Aucun compagnon n'est invoqu\xC3\xA9" ".");
            return true;
        }
        if (ModifierFiche(p, uint8(actif), [c](Fiche& f) { f.comportement = c; }))
            h->PSendSysMessage("Comportement : %s.", NOMS_COMP[c]);
        EnvoyerListe(p);
        return true;
    }

    static bool RelacherCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        uint8 slot;
        if (!p || !LireSlot(h, args, slot))
            return true;
        Fiche f;
        if (!Existe(p, slot, &f))
            return true;
        bool etaitActif;
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            etaitActif = gDonnees[p->GetGUID().GetCounter()].actif == int8(slot);
        }
        if (etaitActif)
            Renvoyer(p, true);
        {
            std::lock_guard<std::mutex> lock(gVerrou);
            gDonnees[p->GetGUID().GetCounter()].fiches.erase(slot);
        }
        Effacer(p->GetGUID().GetCounter(), slot);
        h->PSendSysMessage("%s retourne \xC3\xA0" " la vie sauvage.", NomEntree(f.entry).c_str());
        EnvoyerListe(p);
        return true;
    }

    static bool RanimerCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        uint8 slot;
        if (!p || !LireSlot(h, args, slot))
            return true;
        Fiche f;
        if (!Existe(p, slot, &f))
            return true;
        uint64 maintenant = uint64(GameTime::GetGameTime());
        if (f.mortJusqua <= maintenant)
        {
            h->SendSysMessage("Ce compagnon n'a pas besoin d'\xC3\xAA" "tre ressuscit\xC3\xA9" ".");
            return true;
        }
        if (!ConnaitResurrection(p))
        {
            h->SendSysMessage("Il faut conna\xC3\xAE" "tre un sort de r\xC3\xA9" "surrection (pr\xC3\xAA" "tre, paladin, chaman, druide, chevalier de la mort, ou Ressusciter le familier du chasseur).");
            return true;
        }
        if (!p->IsAlive())
            return true;
        ModifierFiche(p, slot, [](Fiche& fi) { fi.mortJusqua = 0; });
        if (Creature* c = Invoquer(p, slot))
        {
            c->SetHealth(c->CountPctFromMaxHealth(50));
            h->PSendSysMessage("|cff40ff40%s revient \xC3\xA0" " la vie !|r", NomEntree(f.entry).c_str());
        }
        EnvoyerListe(p);
        return true;
    }

    // commande de test (MJ) : .compagnon niveau <1-5> <niveau>
    static bool NiveauCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        if (!p || !args)
            return true;
        std::istringstream in{ std::string(args) };
        int numero = 0, niveau = 0;
        in >> numero >> niveau;
        if (numero < 1 || numero > NB_MAX || niveau < 1 || niveau > NIVEAU_MAX)
        {
            h->SendSysMessage("Utilisation : .compagnon niveau <1-5> <1-90>");
            return true;
        }
        std::vector<uint32> appris;
        ModifierFiche(p, uint8(numero - 1), [niveau, &appris](Fiche& f) { f.niveau = uint8(niveau); f.xp = 0; appris = Completer(f); });
        h->PSendSysMessage("Compagnon %d mis au niveau %d.", numero, niveau);
        if (!appris.empty())
            h->PSendSysMessage("|cff80c0ffIl apprend : %s.|r", TexteAppris(appris).c_str());
        EnvoyerListe(p);
        return true;
    }

    // .compagnon stats : chiffres du compagnon sorti
    static bool StatsCmd(ChatHandler* h, char const* /*args*/)
    {
        Player* p = Joueur(h);
        if (!p)
            return true;
        Creature* c = Trouver(p);
        if (!c)
        {
            h->SendSysMessage("Aucun compagnon n'est invoqu\xC3\xA9" ".");
            return true;
        }
        h->PSendSysMessage("%s (niveau %u) : vie %u / %u, armure %u, d\xC3\xA9" "g\xC3\xA2" "ts %u - %u toutes les 2 s, r\xC3\xA9" "sistance aux sorts %u.",
            c->GetName().c_str(), uint32(c->getLevel()), c->GetHealth(), c->GetMaxHealth(), c->GetArmor(),
            uint32(c->GetFloatValue(UNIT_FIELD_MINDAMAGE)), uint32(c->GetFloatValue(UNIT_FIELD_MAXDAMAGE)), uint32(c->GetResistance(SPELL_SCHOOL_FIRE)));
        if (p->GetPet())
            h->SendSysMessage("|cffffd100Votre familier est sorti : le compagnon est affaibli (40 %).|r");
        return true;
    }

    // .compagnon sort <1-5> <emplacement 1-5> <sort> : choisir le sort d'un emplacement
    static bool SortCmd(ChatHandler* h, char const* args)
    {
        Player* p = Joueur(h);
        if (!p || !args)
            return true;
        std::istringstream in{ std::string(args) };
        int numero = 0, place = 0;
        uint32 id = 0;
        in >> numero >> place >> id;
        if (numero < 1 || numero > NB_MAX || place < 1 || place > NB_MAX || !id)
        {
            h->SendSysMessage("Utilisation : .compagnon sort <compagnon 1-5> <emplacement 1-5> <num\xC3\xA9" "ro du sort>");
            return true;
        }
        Fiche f;
        if (!Existe(p, uint8(numero - 1), &f))
        {
            h->SendSysMessage("Vous n'avez pas de compagnon \xC3\xA0" " cet emplacement.");
            return true;
        }
        if (p->IsInCombat())
        {
            h->SendSysMessage("Impossible de changer de sort pendant un combat.");
            return true;
        }
        if (place > SortsDebloques(f.niveau))
        {
            h->PSendSysMessage("Cet emplacement n'est pas encore d\xC3\xA9" "bloqu\xC3\xA9" " (niveau %u requis).", uint32(place == 1 ? 1 : (place - 1) * 20));
            return true;
        }
        SortAppris const* c = DansCatalogue(f.role, id);
        if (!c || !sSpellMgr->GetSpellInfo(id))
        {
            h->PSendSysMessage("Ce sort ne fait pas partie des sorts du r\xC3\xB4" "le %s.", NOMS_ROLE[std::min<uint8>(f.role, ROLE_SOIN)]);
            return true;
        }
        if (c->niveau > f.niveau)
        {
            h->PSendSysMessage("Votre compagnon doit \xC3\xAA" "tre niveau %u pour apprendre ce sort.", uint32(c->niveau));
            return true;
        }
        uint8 cible = uint8(place - 1);
        ModifierFiche(p, uint8(numero - 1), [id, cible](Fiche& fi)
            {
                fi.sorts.resize(NB_MAX, 0);
                for (uint8 s = 0; s < NB_MAX; ++s)      // deja connu ailleurs : on echange les deux emplacements
                    if (s != cible && fi.sorts[s] == id)
                        fi.sorts[s] = fi.sorts[cible];
                fi.sorts[cible] = id;
            });
        h->PSendSysMessage("|cff80c0ff%s apprend %s (emplacement %d).|r", NomEntree(f.entry).c_str(), NomSort(id).c_str(), place);
        EnvoyerListe(p);
        return true;
    }
};

void AddSC_compagnons()
{
    RegisterSpellScript(spell_compagnon_lasso);
    new compagnons_playerscript();
    new compagnons_commandscript();
}
