/*
 * azimov_quetes_penombre.cpp - Bois de la Pénombre
 *
 * 26760 Cry For The Moon (« Hurler à la lune ») :
 *   Blizzard : en acceptant la quête auprès d'Oliver Harris (43730) à Corbeaucolline, Oliver et Jitters
 *   tentent de soigner le worgen tapi (43950). La scène révèle Sven Yorgen ; l'objectif est le crédit 43969.
 *   La base n'a ni les acteurs de la scène, ni leurs textes, ni leurs scripts.
 *   Ici : à l'acceptation, un worgen PERSONNEL (visible seulement par ce joueur) apparaît entre Oliver et
 *   Jitters, la scène se joue (Oliver et Jitters parlent depuis leur spawn normal), puis le crédit est donné.
 *
 * SQL (azworld2) :
 *   UPDATE creature_template SET ScriptName = 'npc_azimov_oliver_harris' WHERE entry = 43730;
 *   UPDATE creature_template SET ScriptName = 'npc_azimov_worgen_cure'  WHERE entry = 43950;
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "EventMap.h"
#include "GridDefines.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include <cmath>
#include <iterator>

namespace
{
    constexpr uint32 QUEST_CRY_FOR_THE_MOON   = 26760;
    constexpr uint32 NPC_OLIVER_HARRIS        = 43730;
    constexpr uint32 NPC_JITTERS              = 288;
    constexpr uint32 NPC_WORGEN_CURE          = 43950;
    constexpr uint32 NPC_CRY_FOR_MOON_CREDIT  = 43969;

    // Corbeaucolline : entre Oliver (-10754.7, 338.8) et Jitters (-10767.1, 330.7), au pied de la guillotine
    constexpr float WORGEN_X = -10760.9f;
    constexpr float WORGEN_Y = 334.7f;

    enum CureEvents
    {
        EV_LINE = 1,
        EV_CREDIT,
    };

    enum class Speaker { Oliver, Jitters, Worgen, WorgenYell };

    struct Line { Speaker who; char const* text; uint32 delayAfter; };

    Line const CureScene[] =
    {
        { Speaker::Oliver,     "C'est parti...",                                                                  4000 },
        { Speaker::Oliver,     "Ça fonctionne. Tiens-le bien, Jitters.",                                          4000 },
        { Speaker::Jitters,    "Je... je n'y arrive pas...",                                                      3500 },
        { Speaker::Worgen,     "Jitters...",                                                                      3500 },
        { Speaker::Oliver,     "Bon sang, Jitters, je t'ai dit de le TENIR !",                                    3500 },
        { Speaker::WorgenYell, "JITTERS !",                                                                       3500 },
        { Speaker::Worgen,     "Je me souviens, maintenant... Tout est de ta faute !",                            4500 },
        { Speaker::Worgen,     "C'est toi qui as amené les worgens ici. Tu as guidé les Chevaucheurs noirs jusqu'à ma ferme, et tu t'es caché pendant qu'ils venaient.", 6500 },
        { Speaker::Worgen,     "Je devrais te tuer pour ça...",                                                   4000 },
        { Speaker::Oliver,     "Lâche prise. C'est tout ce qui te sépare encore des bêtes.",                      5000 },
        { Speaker::Worgen,     "... Sven. Je m'appelle Sven Yorgen.",                                             4500 },
        { Speaker::Worgen,     "Tu as beaucoup à te faire pardonner, Jitters. Mais je ne prendrai pas la voie facile.", 4000 },
    };
}

// 43730 Oliver Harris : lance la scène à l'acceptation de 26760
struct npc_azimov_oliver_harris : public ScriptedAI
{
    npc_azimov_oliver_harris(Creature* creature) : ScriptedAI(creature) { }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (!player || quest->GetQuestId() != QUEST_CRY_FOR_THE_MOON)
            return;

        float z = me->GetMapHeight(WORGEN_X, WORGEN_Y, me->GetPositionZ() + 3.0f);
        if (z <= INVALID_HEIGHT)
            z = me->GetPositionZ();

        float o = std::atan2(me->GetPositionY() - WORGEN_Y, me->GetPositionX() - WORGEN_X);
        player->SummonCreature(NPC_WORGEN_CURE, Position(WORGEN_X, WORGEN_Y, z, o),
            TEMPSUMMON_TIMED_DESPAWN, 120000, 0, player->GetGUID());
    }
};

// 43950 Worgen tapi (personnel) : joue la scène puis donne le crédit à son joueur
struct npc_azimov_worgen_cure : public ScriptedAI
{
    npc_azimov_worgen_cure(Creature* creature) : ScriptedAI(creature), _line(0) { }

    void IsSummonedBy(Unit* summoner) override
    {
        if (summoner->GetTypeId() == TYPEID_PLAYER)
            _playerGuid = summoner->GetGUID();

        me->SetFaction(FACTION_FRIENDLY);   // 35, SharedDefines.h
        me->SetReactState(REACT_PASSIVE);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
        me->SetHomePosition(me->GetPosition());

        events.ScheduleEvent(EV_LINE, 2000);
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EV_LINE:
                {
                    if (_line >= std::size(CureScene))
                    {
                        events.ScheduleEvent(EV_CREDIT, 1000);
                        break;
                    }
                    Line const& l = CureScene[_line++];
                    switch (l.who)
                    {
                        case Speaker::Oliver:
                            if (Creature* oliver = me->FindNearestCreature(NPC_OLIVER_HARRIS, 40.0f))
                            {
                                oliver->SetFacingToObject(me);
                                oliver->Say(l.text, LANG_UNIVERSAL);
                            }
                            break;
                        case Speaker::Jitters:
                            if (Creature* jitters = me->FindNearestCreature(NPC_JITTERS, 40.0f))
                            {
                                jitters->SetFacingToObject(me);
                                jitters->Say(l.text, LANG_UNIVERSAL);
                            }
                            break;
                        case Speaker::Worgen:
                            me->Say(l.text, LANG_UNIVERSAL);
                            break;
                        case Speaker::WorgenYell:
                            if (Creature* jitters = me->FindNearestCreature(NPC_JITTERS, 40.0f))
                                me->SetFacingToObject(jitters);
                            me->Yell(l.text, LANG_UNIVERSAL);
                            break;
                    }
                    events.ScheduleEvent(EV_LINE, l.delayAfter);
                    break;
                }
                case EV_CREDIT:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                        if (player->GetQuestStatus(QUEST_CRY_FOR_THE_MOON) == QUEST_STATUS_INCOMPLETE)
                            player->KilledMonsterCredit(NPC_CRY_FOR_MOON_CREDIT);
                    me->DespawnOrUnsummon(5000);
                    break;
            }
        }
    }

private:
    EventMap events;
    ObjectGuid _playerGuid;
    size_t _line;
};

void AddSC_azimov_quetes_penombre()
{
    RegisterCreatureAI(npc_azimov_oliver_harris);
    RegisterCreatureAI(npc_azimov_worgen_cure);
}
