/*
 * azimov_talents.cpp - Talents Azimov (façon traits de Legion Remix)
 *
 *   Deux chemins de 50 talents en ligne : 1 = Récolte, 2 = Héroïque (héroïques aux positions 10/20/30/40/50).
 *   Chaque talent s'achète avec la monnaie Puissance Azimov (900), dans l'ordre du chemin.
 *   choice_group > 0 : plusieurs talents à la même position, un seul possédé ; l'échange est gratuit.
 *   spell_id > 0 : aura passive appliquée tant que le talent est possédé (ex. 99041 Poigne titanesque).
 *   versa / refining > 0 : score de Polyvalence / Raffinage ajouté tant que le talent est possédé (CustomStats).
 *   flags : 1 = Poigne titanesque magique (bâtons et armes d'hast en main gauche, compétences bâton / hast)
 *           2 = donne Poigne du titan + ambidextrie dès le chargement du personnage (avant l'inventaire),
 *               sinon les armes à deux mains en main gauche reviennent dans les sacs à chaque connexion.
 *   Alt + clic droit sur une arme à deux mains dans les sacs : l'équipe en main gauche (le client refuse le
 *   glisser-déposer des bâtons / armes d'hast en main gauche).
 *   Réinitialisation d'un chemin (ou des deux) : tout est remboursé.
 *
 * Tables : azworld2.azimov_talent (définitions), azcharacters.character_azimov_talent (talents possédés)
 *
 * Addon (préfixe AZTAL, chuchotement addon à soi-même) :
 *   client -> serveur : "AZTAL_Q" (tout demander), "AZTAL_B:<id>" (acheter), "AZTAL_R:<chemin>" (0 = les deux),
 *                       "AZTAL_E:<sac>:<case>" (équiper en main gauche ; sac 0 = sac à dos, 1-4 = sacs, cases à partir de 1)
 *   serveur -> client : "S" début, "D:id~chemin~pos~coût~héroïque~choix~sort~icône~nom", "T:id~description",
 *                       "C" vider les talents possédés, "O:id,id,..." talents possédés, "F" fin (rafraîchir)
 *
 * Commandes MJ : .aztalent recharger | .aztalent liste | .aztalent raz   (joueur ciblé ou soi-même)
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CustomStats.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "RBAC.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Stats.h"
#include "WorldSession.h"
#include "CellImpl.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <list>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Trinity::ChatCommands;

namespace
{
    char const* const ADDON_PREFIX = "AZTAL";
    constexpr uint8  FLAG_MAGIC = 0x1;
    constexpr uint8  FLAG_TITAN = 0x2;
    constexpr uint8  FLAG_HARVEST_KEEP   = 0x4;   // Moisson abondante : le point de récolte peut ne pas s'épuiser
    constexpr uint8  FLAG_HARVEST_POINTS = 0x8;   // Azimov Farming : chaque récolte donne de la Puissance Azimov
    constexpr uint8  FLAG_PROF_DOUBLE    = 0x10;  // Apprentissage éclair : chance de points de métier doublés
    constexpr uint8  FLAG_CRAFT_DOUBLE   = 0x20;  // Artisan inspiré : chance d'objet fabriqué en double
    constexpr uint8  FLAG_EXPERT_REFINING = 0x40; // Raffinage d'expert : +1 Raffinage par 10 points du meilleur métier de récolte
    constexpr uint8  FLAG_BOOK_DROP       = 0x80; // Bibliothécaire : chance de trouver un livre de métier en récoltant

    // Livres de métier (objets custom 1200101-1200115) : +5 points dans le métier du livre
    struct AzBook { uint32 item; uint16 skill; char const* name; };
    AzBook const BOOKS[] =
    {
        { 1200101, SKILL_ALCHEMY,        "Alchimie" },
        { 1200102, SKILL_BLACKSMITHING,  "Forge" },
        { 1200103, SKILL_ENCHANTING,     "Enchantement" },
        { 1200104, SKILL_ENGINEERING,    "Ingénierie" },
        { 1200105, SKILL_HERBALISM,      "Herboristerie" },
        { 1200106, SKILL_INSCRIPTION,    "Calligraphie" },
        { 1200107, SKILL_JEWELCRAFTING,  "Joaillerie" },
        { 1200108, SKILL_LEATHERWORKING, "Travail du cuir" },
        { 1200109, SKILL_MINING,         "Minage" },
        { 1200110, SKILL_SKINNING,       "Dépeçage" },
        { 1200111, SKILL_TAILORING,      "Couture" },
        { 1200112, SKILL_COOKING,        "Cuisine" },
        { 1200113, SKILL_FIRST_AID,      "Secourisme" },
        { 1200114, SKILL_FISHING,        "Pêche" },
        { 1200115, SKILL_ARCHAEOLOGY,    "Archéologie" },
    };
    constexpr uint32 BOOK_POINTS = 5;
    constexpr uint32 SPELL_TITAN_GRIP_PENALTY = 49152;
    constexpr uint32 SPELL_LEECH_LOG = 20004;      // « Vol de vie » : nom affiché pour le soin du talent dans le journal de combat
    constexpr uint32 SPELL_LIGHT_STRIKE = 9734;    // Châtiment sacré (dégâts de Sacré, visuel des talents de lumière)
    constexpr uint32 SPELL_STAVES   = 227;
    constexpr uint32 SPELL_POLEARMS = 200;

    struct AzTalent
    {
        uint32 id = 0;
        uint8  path = 0;
        uint8  position = 0;
        uint32 cost = 0;
        bool   heroic = false;
        uint8  choiceGroup = 0;
        uint32 spellId = 0;
        int32  versa = 0;
        int32  refining = 0;
        uint8  flags = 0;
        uint8  harvestKeep = 0;   // % de chances de Moisson abondante (cumulable)
        int32  haste = 0;         // score de hâte (mêlée, distance, sorts)
        int32  crit = 0;          // score de coup critique (mêlée, distance, sorts)
        float  dmgPct = 0.0f;     // % de dégâts infligés en plus
        float  leechPct = 0.0f;   // % des dégâts infligés rendus en vie (vol de vie)
        float  powerPct = 0.0f;   // % de Puissance Azimov gagnée en plus (toute source)
        uint8  gatherRestore = 0; // % de vie / mana / énergie rendus à chaque récolte (cumulable)
        uint8  lightProc = 0;     // 1 = Frappe de lumière (cible), 2 = Lumière éclatante (5 ennemis)
        float  allStatsPct = 0.0f; // % en plus sur toutes les stats (caractéristiques, scores, Polyvalence, Raffinage)
        std::string name;
        std::string icon;
        std::string description;
    };

    std::map<uint32, AzTalent> s_talents;                                     // id -> talent
    std::mutex s_lock;
    std::unordered_map<ObjectGuid::LowType, std::set<uint32>> s_owned;        // personnage -> talents

    struct AppliedStats { int32 versa = 0; int32 refining = 0; int32 haste = 0; int32 crit = 0; };
    std::unordered_map<ObjectGuid::LowType, float> s_dmgPct;                  // personnage -> % de dégâts en plus (protégé par s_lock)
    std::unordered_map<ObjectGuid::LowType, float> s_leechPct;                // personnage -> % de vol de vie (protégé par s_lock)
    std::unordered_map<ObjectGuid::LowType, uint8> s_lightProc;               // personnage -> talents de lumière (protégé par s_lock)
    std::unordered_map<ObjectGuid::LowType, uint32> s_lightNext;              // personnage -> prochaine frappe possible (ms de jeu)
    std::unordered_map<ObjectGuid::LowType, AppliedStats> s_stats;           // personnage -> scores déjà appliqués

    uint32 CurrencyId()
    {
        return uint32(sConfigMgr->GetIntDefault("Azimov.Puissance.MonnaieId", 900));
    }

    AzTalent const* GetTalent(uint32 id)
    {
        auto itr = s_talents.find(id);
        return itr != s_talents.end() ? &itr->second : nullptr;
    }

    // Coupe une chaîne UTF-8 sans casser un caractère
    std::string Utf8Cut(std::string const& s, size_t maxBytes)
    {
        if (s.size() <= maxBytes)
            return s;
        size_t n = maxBytes;
        while (n > 0 && (uint8(s[n]) & 0xC0) == 0x80)
            --n;
        return s.substr(0, n);
    }

    void LoadTalents()
    {
        s_talents.clear();
        QueryResult result = WorldDatabase.Query(
            "SELECT id, path, position, cost, heroic, choice_group, spell_id, name, icon, description, versa, refining, flags, harvest_keep, haste, crit, dmg_pct, leech_pct, power_pct, gather_restore, light_proc, all_stats_pct FROM azimov_talent");
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                AzTalent t;
                t.id          = f[0].GetUInt32();
                t.path        = f[1].GetUInt8();
                t.position    = f[2].GetUInt8();
                t.cost        = f[3].GetUInt32();
                t.heroic      = f[4].GetUInt8() != 0;
                t.choiceGroup = f[5].GetUInt8();
                t.spellId     = f[6].GetUInt32();
                t.name        = f[7].GetString();
                t.icon        = f[8].GetString();
                t.description = f[9].GetString();
                t.versa       = f[10].GetInt32();
                t.refining    = f[11].GetInt32();
                t.flags       = f[12].GetUInt8();
                t.harvestKeep = f[13].GetUInt8();
                t.haste       = f[14].GetInt32();
                t.crit        = f[15].GetInt32();
                t.dmgPct      = f[16].GetFloat();
                t.leechPct    = f[17].GetFloat();
                t.powerPct    = f[18].GetFloat();
                t.gatherRestore = f[19].GetUInt8();
                t.lightProc   = f[20].GetUInt8();
                t.allStatsPct = f[21].GetFloat();

                if (t.spellId && !sSpellMgr->GetSpellInfo(t.spellId))
                {
                    TC_LOG_ERROR("sql.sql", "azimov_talent %u : sort %u introuvable, talent laissé sans effet.", t.id, t.spellId);
                    t.spellId = 0;
                }
                s_talents[t.id] = t;
            } while (result->NextRow());
        }
        TC_LOG_INFO("server.loading", ">> Talents Azimov : %u talents chargés", uint32(s_talents.size()));
    }

    std::set<uint32> GetOwned(Player* player)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        auto itr = s_owned.find(player->GetGUID().GetCounter());
        return itr != s_owned.end() ? itr->second : std::set<uint32>();
    }

    // Talent possédé à cette position du chemin (0 si aucun)
    uint32 OwnedAt(std::set<uint32> const& owned, uint8 path, uint8 position)
    {
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
                if (t->path == path && t->position == position)
                    return id;
        return 0;
    }

    void ApplyAura(Player* player, AzTalent const* t)
    {
        if (t && t->spellId && !player->HasAura(t->spellId))
            player->AddAura(t->spellId, player);
    }

    void RemoveAuraOf(Player* player, AzTalent const* t)
    {
        if (t && t->spellId)
            player->RemoveAurasDueToSpell(t->spellId);
    }

    // Recalcule Polyvalence / Raffinage des talents possédés et applique seulement la différence
    void ApplyStats(Player* player)
    {
        std::set<uint32> owned = GetOwned(player);
        int32 versa = 0, refining = 0, haste = 0, crit = 0;
        float dmgPct = 0.0f, leechPct = 0.0f;
        uint8 flags = 0, lightProc = 0;
        float allStatsPct = 0.0f;
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
            {
                flags |= t->flags;
                versa += t->versa;
                refining += t->refining;
                haste += t->haste;
                crit += t->crit;
                dmgPct += t->dmgPct;
                leechPct += t->leechPct;
                lightProc |= t->lightProc;
                allStatsPct += t->allStatsPct;
            }

        // Raffinage d'expert : meilleur métier de récolte (herboristerie, minage, dépeçage) / 10
        if (flags & FLAG_EXPERT_REFINING)
            refining += int32(std::max({ player->GetSkillValue(SKILL_HERBALISM), player->GetSkillValue(SKILL_MINING), player->GetSkillValue(SKILL_SKINNING) }) / 10);

        AppliedStats old;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            AppliedStats& entry = s_stats[player->GetGUID().GetCounter()];
            old = entry;
            entry.versa = versa;
            entry.refining = refining;
            entry.haste = haste;
            entry.crit = crit;
            if (dmgPct > 0.0f)
                s_dmgPct[player->GetGUID().GetCounter()] = dmgPct;
            else
                s_dmgPct.erase(player->GetGUID().GetCounter());
            if (leechPct > 0.0f)
                s_leechPct[player->GetGUID().GetCounter()] = leechPct;
            else
                s_leechPct.erase(player->GetGUID().GetCounter());
            if (lightProc)
                s_lightProc[player->GetGUID().GetCounter()] = lightProc;
            else
                s_lightProc.erase(player->GetGUID().GetCounter());
        }

        if (haste != old.haste)
            for (CombatRating cr : { CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL })
                player->ApplyRatingMod(cr, haste - old.haste, true);
        if (crit != old.crit)
            for (CombatRating cr : { CR_CRIT_MELEE, CR_CRIT_RANGED, CR_CRIT_SPELL })
                player->ApplyRatingMod(cr, crit - old.crit, true);

        if (versa != old.versa)
            player->ApplyCustomVersatilityRating(versa - old.versa, true);
        if (refining != old.refining)
            player->ApplyCustomRefiningRating(refining - old.refining, true);
        // Toutes les stats (+X %) : multiplicateur du cœur, puis recalcul des caractéristiques et des scores
        float mult = 1.0f + allStatsPct / 100.0f;
        bool multChanged = std::fabs(player->GetAzimovAllStatsMult() - mult) > 0.0001f;
        if (multChanged)
        {
            player->SetAzimovAllStatsMult(mult);
            player->GetStats().UpdateTotalStatMultiplier(StatType::AllPrimaryStats);
            player->UpdateAllRatings();
        }

        if (versa != old.versa || refining != old.refining || multChanged)
            CustomStats::SendStatsToClient(player);
    }

    uint8 OwnedFlags(std::set<uint32> const& owned)
    {
        uint8 flags = 0;
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
                flags |= t->flags;
        return flags;
    }

    // Herbe ou filon (verrou de compétence herboristerie / minage)
    bool IsGatherNode(GameObject const* go)
    {
        if (!go || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            return false;
        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return false;
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
            if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_HERBALISM || lock->Index[i] == LOCKTYPE_MINING))
                return true;
        return false;
    }

    // Azimov Farming : 1 à 2 Puissance Azimov par récolte réussie (réglable)
    void GiveHarvestPoints(Player* player, uint8 flags)
    {
        if (!(flags & FLAG_HARVEST_POINTS))
            return;
        uint32 mini = uint32(sConfigMgr->GetIntDefault("Azimov.Talents.Farming.Min", 1));
        uint32 maxi = uint32(sConfigMgr->GetIntDefault("Azimov.Talents.Farming.Max", 2));
        if (maxi < mini)
            maxi = mini;
        if (uint32 amount = urand(mini, maxi))
            player->ModifyCurrency(CurrencyId(), int32(amount));
    }

    // Crochet du cœur : herbe / filon entièrement ramassé. true = le point ne s'épuise pas.
    // Second souffle du récolteur : rend un % de vie, de mana et de la ressource principale (énergie, rage…)
    void RestoreOnGather(Player* player, std::set<uint32> const& owned)
    {
        int32 pct = 0;
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
                pct += t->gatherRestore;
        if (pct <= 0 || !player->IsAlive())
            return;
        player->ModifyHealth(int32(player->CountPctFromMaxHealth(pct)));
        if (int32 maxMana = player->GetMaxPower(POWER_MANA))
            player->ModifyPower(POWER_MANA, CalculatePct(maxMana, pct));
        PowerType power = player->GetPowerType();
        if (power != POWER_MANA)
            if (int32 maxPower = player->GetMaxPower(power))
                player->ModifyPower(power, std::max(1, CalculatePct(maxPower, pct)));
    }

    // Bibliothécaire (Récolte 40) : chance de trouver un livre de métier au hasard
    void DropBook(Player* player, uint8 flags)
    {
        if (!(flags & FLAG_BOOK_DROP) || !roll_chance_i(sConfigMgr->GetIntDefault("Azimov.Talents.Livres.Chance", 10)))
            return;
        AzBook const& book = BOOKS[urand(0, uint32(std::size(BOOKS)) - 1)];
        if (player->AddItem(book.item, 1))
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ff96Talent Azimov : vous trouvez un livre de %s !|r", book.name);
    }

    bool OnGatherNodeLooted(Player* player, GameObject* go)
    {
        if (!player || !IsGatherNode(go))
            return false;
        std::set<uint32> owned = GetOwned(player);
        uint8 flags = OwnedFlags(owned);
        GiveHarvestPoints(player, flags);
        RestoreOnGather(player, owned);
        DropBook(player, flags);
        if (flags & FLAG_EXPERT_REFINING)
            ApplyStats(player); // le métier a pu monter pendant la récolte

        // Moisson abondante : héroïque (config, 10 %) + petits talents (+1 % chacun…)
        int32 chance = (flags & FLAG_HARVEST_KEEP) ? sConfigMgr->GetIntDefault("Azimov.Talents.Moisson.Chance", 10) : 0;
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
                chance += t->harvestKeep;
        return chance > 0 && roll_chance_i(chance);
    }

    // Crochet du cœur : Puissance Azimov gagnée (toute source, hors remboursement / commande).
    // Bonus = gain x % des talents ; la partie décimale se joue aux dés (1 point à 5 % = 5 % de chances de +1).
    int32 OnCurrencyGain(Player* player, uint32 currencyId, int32 amount)
    {
        if (!player || amount <= 0 || currencyId != CurrencyId())
            return amount;
        float pct = 0.0f;
        for (uint32 id : GetOwned(player))
            if (AzTalent const* t = GetTalent(id))
                pct += t->powerPct;
        if (pct <= 0.0f)
            return amount;
        float bonus = float(amount) * pct / 100.0f;
        int32 whole = int32(bonus);
        if (roll_chance_f((bonus - float(whole)) * 100.0f))
            ++whole;
        return amount + whole;
    }

    // Crochet du cœur : dépeçage entièrement ramassé
    void OnSkinningLooted(Player* player, Creature* /*creature*/)
    {
        if (!player)
            return;
        std::set<uint32> owned = GetOwned(player);
        uint8 flags = OwnedFlags(owned);
        GiveHarvestPoints(player, flags);
        RestoreOnGather(player, owned);
        DropBook(player, flags);
        if (flags & FLAG_EXPERT_REFINING)
            ApplyStats(player); // le dépeçage a pu monter
    }

    // Crochet du cœur : montée d'un métier (principal ou secondaire). Renvoie les points à donner.
    uint32 OnProfessionStep(Player* player, uint16 skillId, uint32 step)
    {
        if (!player || !step)
            return step;
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
        if (!skill || (skill->CategoryID != SKILL_CATEGORY_PROFESSION && skill->CategoryID != SKILL_CATEGORY_SECONDARY))
            return step;
        if (!(OwnedFlags(GetOwned(player)) & FLAG_PROF_DOUBLE))
            return step;
        if (!roll_chance_i(sConfigMgr->GetIntDefault("Azimov.Talents.MetierDouble.Chance", 10)))
            return step;
        ChatHandler(player->GetSession()).PSendSysMessage("|cff00ff96Talent Azimov : points de métier doublés !|r");
        return step * 2;
    }

    // Métier principal ou secondaire (hors pêche)
    bool IsCraftProfession(uint32 skillId)
    {
        if (skillId == SKILL_FISHING)
            return false;
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
        return skill && (skill->CategoryID == SKILL_CATEGORY_PROFESSION || skill->CategoryID == SKILL_CATEGORY_SECONDARY);
    }

    // Crochet du cœur : objets créés par un sort. Double la quantité des fabrications de métier.
    uint32 OnCraftCount(Player* player, SpellInfo const* spellInfo, uint32 count)
    {
        if (!player || !spellInfo || !count)
            return count;
        if (!(OwnedFlags(GetOwned(player)) & FLAG_CRAFT_DOUBLE))
            return count;

        bool craft = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellInfo->Id);
        for (auto itr = bounds.first; itr != bounds.second && !craft; ++itr)
            craft = IsCraftProfession(itr->second->SkillLine);
        if (!craft)
            return count;

        if (!roll_chance_i(sConfigMgr->GetIntDefault("Azimov.Talents.ArtisanInspire.Chance", 10)))
            return count;
        ChatHandler(player->GetSession()).PSendSysMessage("|cff00ff96Talent Azimov : fabrication en double !|r");
        return count * 2;
    }

    // Range l'arme d'un emplacement dans les sacs (rien si les sacs sont pleins)
    void StoreEquipped(Player* player, uint8 slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            return;
        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(dest, item, true);
        }
    }

    bool IsStaffOrPolearm(Item* item)
    {
        if (!item)
            return false;
        uint32 sub = item->GetTemplate()->GetSubClass();
        return item->GetTemplate()->GetClass() == ITEM_CLASS_WEAPON &&
            (sub == ITEM_SUBCLASS_WEAPON_STAFF || sub == ITEM_SUBCLASS_WEAPON_POLEARM);
    }

    // Poigne titanesque magique : drapeau du cœur + compétences bâton / arme d'hast
    void ApplyFlags(Player* player)
    {
        bool magic = (OwnedFlags(GetOwned(player)) & FLAG_MAGIC) != 0;
        if (magic)
        {
            player->SetCanTitanGripMagic(true);
            uint16 value = uint16(player->GetMaxSkillValueForLevel());
            if (!player->HasSkill(SKILL_STAVES))
                player->SetSkill(SKILL_STAVES, 0, value, value);
            if (!player->HasSkill(SKILL_POLEARMS))
                player->SetSkill(SKILL_POLEARMS, 0, value, value);
            player->AddWeaponProficiency((1 << ITEM_SUBCLASS_WEAPON_STAFF) | (1 << ITEM_SUBCLASS_WEAPON_POLEARM));
            player->SendProficiency(ITEM_CLASS_WEAPON, player->GetWeaponProficiency());
        }
        else if (player->CanTitanGripMagic())
        {
            player->SetCanTitanGripMagic(false);
            if (IsStaffOrPolearm(player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND)))
                StoreEquipped(player, EQUIPMENT_SLOT_OFFHAND);
            if (!player->HasSpell(SPELL_STAVES) && player->HasSkill(SKILL_STAVES))
                player->SetSkill(SKILL_STAVES, 0, 0, 0);
            if (!player->HasSpell(SPELL_POLEARMS) && player->HasSkill(SKILL_POLEARMS))
                player->SetSkill(SKILL_POLEARMS, 0, 0, 0);
            Item* main = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            if (main && main->GetSkill() && !player->HasSkill(main->GetSkill()))
                StoreEquipped(player, EQUIPMENT_SLOT_MAINHAND);
        }
        player->CheckTitanGripPenalty();
    }

    // Juste avant le chargement de l'inventaire (crochet du cœur AzimovBeforeInventoryLoad)
    void EarlyLoad(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::set<uint32> owned;
        if (QueryResult result = CharacterDatabase.PQuery("SELECT talent_id FROM character_azimov_talent WHERE guid = %u", guid))
        {
            do
                owned.insert(result->Fetch()[0].GetUInt32());
            while (result->NextRow());
        }
        uint8 flags = OwnedFlags(owned);
        if (flags & (FLAG_TITAN | FLAG_MAGIC))
        {
            if (!player->CanTitanGrip())
                player->SetCanTitanGrip(true, SPELL_TITAN_GRIP_PENALTY);
            if (!player->CanDualWield())
                player->SetCanDualWield(true);
        }
        if (flags & FLAG_MAGIC)
            player->SetCanTitanGripMagic(true);
    }

    // Alt + clic droit dans les sacs : arme à deux mains -> main gauche
    void EquipOffhand(Player* player, uint32 clientBag, uint32 clientSlot)
    {
        if (clientSlot < 1 || clientBag > 4)
            return;
        uint8 bag, slot;
        if (clientBag == 0)
        {
            bag = INVENTORY_SLOT_BAG_0;
            slot = uint8(INVENTORY_SLOT_ITEM_START + clientSlot - 1);
            if (slot >= INVENTORY_SLOT_ITEM_END)
                return;
        }
        else
        {
            bag = uint8(INVENTORY_SLOT_BAG_START + clientBag - 1);
            slot = uint8(clientSlot - 1);
        }

        Item* item = player->GetItemByPos(bag, slot);
        if (!item || item->GetTemplate()->GetInventoryType() != INVTYPE_2HWEAPON)
            return;
        if (!player->CanTitanGrip() || (IsStaffOrPolearm(item) && !player->CanTitanGripMagic()))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r Il faut la Poigne titanesque%s pour porter cette arme en main gauche.",
                IsStaffOrPolearm(item) ? " magique" : "");
            return;
        }
        if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r Équipez d'abord une arme en main droite.");
            return;
        }
        player->SwapItem(uint16(bag) << 8 | slot, uint16(INVENTORY_SLOT_BAG_0) << 8 | EQUIPMENT_SLOT_OFFHAND);
        player->CheckTitanGripPenalty();
    }

    void Send(Player* player, std::string const& text)
    {
        player->WhisperAddon(text, ADDON_PREFIX, player);
    }

    void SendOwned(Player* player)
    {
        std::set<uint32> owned = GetOwned(player);
        Send(player, "C");
        std::string line = "O:";
        for (uint32 id : owned)
        {
            std::string part = std::to_string(id);
            if (line.size() + part.size() + 1 > 240)
            {
                Send(player, line);
                line = "O:";
            }
            if (line.size() > 2)
                line += ",";
            line += part;
        }
        if (line.size() > 2)
            Send(player, line);
        Send(player, "F");
    }

    void SendAll(Player* player)
    {
        Send(player, "S");
        for (auto const& kv : s_talents)
        {
            AzTalent const& t = kv.second;
            char head[96];
            snprintf(head, sizeof(head), "D:%u~%u~%u~%u~%u~%u~%u~", t.id, t.path, t.position, t.cost,
                t.heroic ? 1 : 0, t.choiceGroup, (t.spellId || t.versa || t.refining || t.flags || t.harvestKeep || t.haste || t.crit || t.dmgPct > 0.0f || t.leechPct > 0.0f || t.powerPct > 0.0f || t.gatherRestore || t.lightProc || t.allStatsPct > 0.0f) ? 1 : 0);
            Send(player, Utf8Cut(std::string(head) + t.icon + "~" + t.name, 250));
            if (!t.description.empty())
                Send(player, Utf8Cut("T:" + std::to_string(t.id) + "~" + t.description, 250));
        }
        SendOwned(player);
    }

    void Notify(Player* player, char const* text)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r %s", text);
    }

    void Buy(Player* player, uint32 id)
    {
        AzTalent const* t = GetTalent(id);
        if (!t)
            return;

        std::set<uint32> owned = GetOwned(player);
        if (owned.count(id))
            return;

        if (t->position > 1 && !OwnedAt(owned, t->path, t->position - 1))
        {
            Notify(player, "Achetez d'abord le talent précédent de ce chemin.");
            return;
        }

        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        uint32 current = OwnedAt(owned, t->path, t->position);

        if (current)
        {
            AzTalent const* old = GetTalent(current);
            if (!t->choiceGroup || !old || old->choiceGroup != t->choiceGroup)
                return;

            // échange gratuit entre talents d'un même choix (même aura : on ne la retire pas)
            if (old->spellId != t->spellId)
                RemoveAuraOf(player, old);
            {
                std::lock_guard<std::mutex> guard(s_lock);
                s_owned[guid].erase(current);
                s_owned[guid].insert(id);
            }
            CharacterDatabase.PExecute("DELETE FROM character_azimov_talent WHERE guid = %u AND talent_id = %u", guid, current);
            CharacterDatabase.PExecute("INSERT IGNORE INTO character_azimov_talent (guid, talent_id) VALUES (%u, %u)", guid, id);
            ApplyAura(player, t);
            ApplyStats(player);
            ApplyFlags(player);
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r Choix changé : |cffffd700%s|r.", t->name.c_str());
            SendOwned(player);
            return;
        }

        uint32 currency = CurrencyId();
        if (t->cost && !player->HasCurrency(currency, t->cost))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r Il faut %u Puissance Azimov pour |cffffd700%s|r.",
                t->cost, t->name.c_str());
            return;
        }

        if (t->cost)
            player->ModifyCurrency(currency, -int32(t->cost));
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_owned[guid].insert(id);
        }
        CharacterDatabase.PExecute("INSERT IGNORE INTO character_azimov_talent (guid, talent_id) VALUES (%u, %u)", guid, id);
        ApplyAura(player, t);
        ApplyStats(player);
        ApplyFlags(player);
        ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r Talent appris : |cffffd700%s|r.", t->name.c_str());
        SendOwned(player);
    }

    // path = 0 : les deux chemins. Rembourse tout.
    uint32 Reset(Player* player, uint8 path)
    {
        std::set<uint32> owned = GetOwned(player);
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        uint32 refund = 0, count = 0;

        for (uint32 id : owned)
        {
            AzTalent const* t = GetTalent(id);
            if (t && path && t->path != path)
                continue;

            if (t)
            {
                refund += t->cost;
                RemoveAuraOf(player, t);
            }
            {
                std::lock_guard<std::mutex> guard(s_lock);
                s_owned[guid].erase(id);
            }
            CharacterDatabase.PExecute("DELETE FROM character_azimov_talent WHERE guid = %u AND talent_id = %u", guid, id);
            ++count;
        }

        if (refund)
            player->ModifyCurrency(CurrencyId(), int32(refund), false, true); // remboursement : sans bonus de Puissance
        ApplyStats(player);
        ApplyFlags(player);

        if (count)
            ChatHandler(player->GetSession()).PSendSysMessage("|cff00ccff[Talents Azimov]|r %u talent(s) réinitialisé(s), %u Puissance Azimov remboursée.",
                count, refund);
        SendOwned(player);
        return count;
    }

    void LoadPlayer(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::set<uint32> owned;
        if (QueryResult result = CharacterDatabase.PQuery("SELECT talent_id FROM character_azimov_talent WHERE guid = %u", guid))
        {
            do
                owned.insert(result->Fetch()[0].GetUInt32());
            while (result->NextRow());
        }
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_owned[guid] = owned;
        }
        for (uint32 id : owned)
            ApplyAura(player, GetTalent(id));
        ApplyStats(player);
        ApplyFlags(player);
    }
}

class azimov_talents_world : public WorldScript
{
public:
    azimov_talents_world() : WorldScript("azimov_talents_world") { }

    void OnStartup() override
    {
        LoadTalents();
    }
};

class azimov_talents_player : public PlayerScript
{
public:
    azimov_talents_player() : PlayerScript("azimov_talents_player") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        LoadPlayer(player);
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(s_lock);
        s_owned.erase(player->GetGUID().GetCounter());
        s_stats.erase(player->GetGUID().GetCounter());
        s_dmgPct.erase(player->GetGUID().GetCounter());
        s_leechPct.erase(player->GetGUID().GetCounter());
        s_lightProc.erase(player->GetGUID().GetCounter());
        s_lightNext.erase(player->GetGUID().GetCounter());
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || receiver != player || msg.compare(0, 6, "AZTAL_") != 0)
            return;

        if (msg == "AZTAL_Q")
            SendAll(player);
        else if (msg.compare(0, 8, "AZTAL_B:") == 0)
            Buy(player, uint32(strtoul(msg.c_str() + 8, nullptr, 10)));
        else if (msg.compare(0, 8, "AZTAL_R:") == 0)
            Reset(player, uint8(strtoul(msg.c_str() + 8, nullptr, 10)));
        else if (msg.compare(0, 8, "AZTAL_E:") == 0)
        {
            char* end = nullptr;
            uint32 bag = uint32(strtoul(msg.c_str() + 8, &end, 10));
            uint32 slot = (end && *end == ':') ? uint32(strtoul(end + 1, nullptr, 10)) : 0;
            EquipOffhand(player, bag, slot);
        }
    }
};

class azimov_talents_command : public CommandScript
{
public:
    azimov_talents_command() : CommandScript("azimov_talents_command") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> subTable =
        {
            { "recharger", rbac::RBAC_PERM_COMMAND_RELOAD, false, &HandleReload, "" },
            { "liste",     rbac::RBAC_PERM_COMMAND_GPS,    false, &HandleList,   "" },
            { "raz",       rbac::RBAC_PERM_COMMAND_RELOAD, false, &HandleReset,  "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "aztalent", rbac::RBAC_PERM_COMMAND_GPS, false, nullptr, "", subTable },
        };
        return commandTable;
    }

    static bool HandleReload(ChatHandler* handler, char const* /*args*/)
    {
        LoadTalents();
        if (Player* self = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr)
            ApplyStats(self);
        handler->PSendSysMessage("Talents Azimov : %u talents rechargés (les autres joueurs en ligne : effet à leur prochaine connexion).", uint32(s_talents.size()));
        return true;
    }

    static bool HandleList(ChatHandler* handler, char const* /*args*/)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        std::set<uint32> owned = GetOwned(target);
        handler->PSendSysMessage("%s : %u talent(s) Azimov.", target->GetName().c_str(), uint32(owned.size()));
        for (uint32 id : owned)
            if (AzTalent const* t = GetTalent(id))
                handler->PSendSysMessage("  [%u] chemin %u, position %u : %s", t->id, t->path, t->position, t->name.c_str());
        return true;
    }

    static bool HandleReset(ChatHandler* handler, char const* /*args*/)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        handler->PSendSysMessage("%s : %u talent(s) réinitialisé(s).", target->GetName().c_str(), Reset(target, 0));
        return true;
    }
};

// Livres de métier : clic droit = +5 points dans le métier du livre (sans dépasser le maximum actuel)
class item_azimov_livre_metier : public ItemScript
{
public:
    item_azimov_livre_metier() : ItemScript("item_azimov_livre_metier") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        AzBook const* book = nullptr;
        for (AzBook const& b : BOOKS)
            if (b.item == item->GetEntry())
                book = &b;
        if (!book)
            return false;

        ChatHandler chat(player->GetSession());
        if (!player->HasSkill(book->skill))
        {
            chat.PSendSysMessage("|cffff6060Vous ne connaissez pas le métier %s.|r", book->name);
            return true;
        }
        uint16 cur = player->GetPureSkillValue(book->skill);
        uint16 max = player->GetPureMaxSkillValue(book->skill);
        if (cur >= max)
        {
            chat.PSendSysMessage("|cffff6060Votre %s est déjà au maximum de votre rang (%u / %u). Apprenez le rang suivant auprès d'un maître.|r", book->name, cur, max);
            return true;
        }
        uint16 val = uint16(std::min<uint32>(cur + BOOK_POINTS, max));
        player->SetSkill(book->skill, player->GetSkillStep(book->skill), val, max);
        uint32 count = 1;
        player->DestroyItemCount(item, count, true);
        chat.PSendSysMessage("|cff00ff96Livre de %s : +%u points (%u / %u).|r", book->name, uint32(val - cur), uint32(val), uint32(max));
        if (book->skill == SKILL_HERBALISM || book->skill == SKILL_MINING || book->skill == SKILL_SKINNING)
            ApplyStats(player);   // Raffinage d'expert suit le meilleur métier de récolte
        return true;
    }
};

// Dégâts infligés en plus (talents « +X % de dégâts »)
class azimov_talents_damage : public UnitScript
{
public:
    azimov_talents_damage() : UnitScript("azimov_talents_damage") { }

    static float Bonus(Unit* attacker)
    {
        if (!attacker || attacker->GetTypeId() != TYPEID_PLAYER)
            return 0.0f;
        std::lock_guard<std::mutex> guard(s_lock);
        auto itr = s_dmgPct.find(attacker->GetGUID().GetCounter());
        return itr != s_dmgPct.end() ? itr->second : 0.0f;
    }

    // Vol de vie : une part des dégâts réellement infligés revient en vie
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!damage || !attacker || attacker == victim || attacker->GetTypeId() != TYPEID_PLAYER || !attacker->IsAlive())
            return;
        float pct = 0.0f;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_leechPct.find(attacker->GetGUID().GetCounter());
            if (itr != s_leechPct.end())
                pct = itr->second;
        }
        if (pct > 0.0f)
            if (uint32 heal = uint32(damage * pct / 100.0f))
            {
                // Soin passé par un sort (« Vol de vie ») : visible dans le journal de combat, donc dans Skada / Recount
                if (SpellInfo const* leech = sSpellMgr->GetSpellInfo(SPELL_LEECH_LOG))
                {
                    HealInfo healInfo(attacker, attacker, heal, leech, leech->GetSchoolMask());
                    attacker->HealBySpell(healInfo);
                }
                else
                    attacker->ModifyHealth(int32(heal));
            }

        LightStrike(attacker->ToPlayer(), victim);
    }

    // Héroïque 30 : Frappe de lumière / Lumière éclatante (une fois toutes les 30 s au plus)
    static void LightStrike(Player* player, Unit* victim)
    {
        if (!player || !victim || !victim->IsAlive() || !player->IsValidAttackTarget(victim))
            return;
        uint8 proc = 0;
        uint32 const now = GameTime::GetGameTimeMS();
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_lightProc.find(player->GetGUID().GetCounter());
            if (itr == s_lightProc.end())
                return;
            proc = itr->second;
            uint32& next = s_lightNext[player->GetGUID().GetCounter()];
            if (next && int32(next - now) > 0)
                return;
            next = now + uint32(std::max(1, sConfigMgr->GetIntDefault("Azimov.Talents.Lumiere.Delai", 30))) * IN_MILLISECONDS;
        }

        // Puissance de référence : la plus haute entre puissance d'attaque (mêlée / distance) et puissance des sorts sacrés
        float power = std::max({ player->GetTotalAttackPowerValue(BASE_ATTACK), player->GetTotalAttackPowerValue(RANGED_ATTACK),
            float(player->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_HOLY)) });
        bool const zone = (proc & 2) != 0;
        int32 const pct = zone ? sConfigMgr->GetIntDefault("Azimov.Talents.Lumiere.Zone.Pct", 150) : sConfigMgr->GetIntDefault("Azimov.Talents.Lumiere.Pct", 300);
        int32 const dmg = std::max(1, int32(power * pct / 100.0f) + int32(player->getLevel()) * 10);

        std::vector<ObjectGuid> targets{ victim->GetGUID() };
        if (zone)
        {
            std::list<Unit*> around;
            Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(victim, player, 8.0f);
            Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(victim, around, check);
            Cell::VisitAllObjects(victim, searcher, 8.0f);
            for (Unit* u : around)
            {
                if (targets.size() >= 5)
                    break;
                if (u != victim && player->IsValidAttackTarget(u))
                    targets.push_back(u->GetGUID());
            }
        }

        // Lancé au tick suivant : on est dans Unit::DealDamage, pas question d'y imbriquer un sort
        player->m_Events.AddEventAtOffset([player, targets, dmg]()
        {
            for (ObjectGuid const& guid : targets)
                if (Unit* target = ObjectAccessor::GetUnit(*player, guid))
                    if (target->IsAlive() && player->IsValidAttackTarget(target))
                        player->CastSpell(target, SPELL_LIGHT_STRIKE, CastSpellExtraArgs(TRIGGERED_FULL_MASK).AddSpellBP0(dmg));
        }, 1ms);
    }

    void ModifyMeleeDamage(Unit* /*target*/, Unit* attacker, uint32& damage) override
    {
        if (float pct = Bonus(attacker))
            damage = uint32(damage * (1.0f + pct / 100.0f));
    }

    void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage) override
    {
        if (damage > 0)
            if (float pct = Bonus(attacker))
                damage = int32(damage * (1.0f + pct / 100.0f));
    }

    void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* attacker, uint32& damage) override
    {
        if (float pct = Bonus(attacker))
            damage = uint32(damage * (1.0f + pct / 100.0f));
    }
};

void AddSC_azimov_talents()
{
    AzimovBeforeInventoryLoad = &EarlyLoad;
    AzimovGatherNodeLooted = &OnGatherNodeLooted;
    AzimovSkinningLooted = &OnSkinningLooted;
    AzimovProfessionStep = &OnProfessionStep;
    AzimovCraftCount = &OnCraftCount;
    AzimovCurrencyGain = &OnCurrencyGain;
    new azimov_talents_world();
    new azimov_talents_player();
    new azimov_talents_command();
    new azimov_talents_damage();
    new item_azimov_livre_metier();
}
