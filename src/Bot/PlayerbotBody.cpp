/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#include "PlayerbotAI.h"
#include "AiObjectContext.h"
#include "AiFactory.h"
#include "Engine.h"
#include "GameObject.h"
#include "LootObjectStack.h"
#include "MotionMaster.h"
#include "NewRpgAction.h"
#include "Playerbots.h"
#include "WorldSession.h"

bool PlayerbotAI::DoBodyAction()
{
    auto& body = rpgInfo.body;
    if (!body.Attached())
        return false;
    auto const now = getMSTime();
    auto pause = [&](BodyControl::Interrupt reason)
    {
        body.Pause(reason);
        rpgInfo.bodyTravel.Pause(now);
    };
    if (IsExternallyControlled() || IsSelfBot(bot) || !bot->GetSession()->IsBot() || HasGameClientMaster()
        || bot->InBattleground())
    {
        body = {};
        rpgInfo.bodyTravel = {};
        rpgInfo.bodyRoute = {};
        return false; // Do not stop or overwrite a human's movement at handoff.
    }
    if (!bot->IsAlive() || currentState == BOT_STATE_DEAD)
    {
        pause(BodyControl::Interrupt::Recovery);
        return false;
    }
    if (bot->IsInCombat() || currentState == BOT_STATE_COMBAT)
    {
        pause(BodyControl::Interrupt::Combat);
        return false; // Existing class combat owns the body until combat ends.
    }
    if (!body.Fresh(now))
    {
        pause(BodyControl::Interrupt::BrainUnavailable);
        // Stop only our point movement. Follow, flight and other controllers own their own generators.
        if ((!body.Directed() || body.skill == BodyControl::Skill::Activity)
            && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
        }
        return true;
    }
    if (!bodyEngine)
    {
        bodyEngine = new Engine(this, aiObjectContext);
        AiFactory::AddDefaultNonCombatStrategies(bot, this, bodyEngine, true);
        bodyEngine->Init();
    }
    if (bodyEngine->ExecuteAction("destroy junk") == ACTION_RESULT_OK
        || bodyEngine->DoNextAction(nullptr, 0, false) || bot->IsSitState() || bot->IsNonMeleeSpellCast(false)
        || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
    {
        pause(BodyControl::Interrupt::Maintenance);
        return true;
    }
    body.Resume();
    if (body.Directed())
    {
        if (body.skill != BodyControl::Skill::Activity)
            rpgInfo.bodyTravel.Pause(now);
        return true; // Alles owns follow, preparation, rendezvous and purpose-specific activity execution.
    }
    auto const& control = rpgInfo.objectiveControl;
    if (body.skill == BodyControl::Skill::Idle)
    {
        if (rpgInfo.bodyTravel.HasPath())
        {
            auto const end = rpgInfo.bodyTravel.End();
            auto const& last = aiObjectContext->GetValue<LastMovement&>("last movement")->Get();
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE
                && last.lastMoveToMapId == bot->GetMapId() && last.lastMoveToX == end.x
                && last.lastMoveToY == end.y && last.lastMoveToZ == end.z)
            {
                bot->GetMotionMaster()->Clear();
                bot->StopMoving();
            }
            rpgInfo.bodyTravel = {};
        }
        return true;
    }
    if (body.objective != control.token || control.failure != QuestObjectiveControl::Failure::None)
    {
        body.state = BodyControl::State::Blocked;
        return true;
    }

    // Explicit skill dispatch, with the ordinary scheduler's physical preconditions and action telemetry.
    // The old noncombat engine is never ticked here, so it cannot choose a competing grind/roam destination.
    switch (body.skill)
    {
        case BodyControl::Skill::Travel:
            bodyEngine->ExecuteAction("new rpg go camp");
            if (control.place && control.phase == QuestObjectiveControl::Phase::Attempting)
                body.state = BodyControl::State::Arrived;
            break;
        case BodyControl::Skill::Investigate:
            bodyEngine->ExecuteAction("new rpg wander npc");
            break;
        case BodyControl::Skill::Quest:
        {
            // Keep ordinary corpse interaction available without installing a roaming loot strategy.
            auto* loot = aiObjectContext->GetValue<LootObjectStack*>("available loot")->Get();
            for (auto const guid : aiObjectContext->GetValue<GuidVector>("nearest corpses")->Get())
                loot->Add(guid);
            for (auto const guid : aiObjectContext->GetValue<GuidVector>("nearest game objects")->Get())
                if (auto* object = GetGameObject(guid); object && object->ActivateToQuest(bot))
                    loot->Add(guid);
            // "Has available loot" excludes a target already within reach. Both phases need execution.
            if (aiObjectContext->GetValue<bool>("can loot")->Get()
                || aiObjectContext->GetValue<bool>("has available loot")->Get())
            {
                bodyEngine->ExecuteAction("loot");
                auto* target = aiObjectContext->GetValue<LootObject>("loot target");
                auto current = target->Get();
                auto* object = current.GetWorldObject(bot);
                auto const& last = aiObjectContext->GetValue<LastMovement&>("last movement")->Get();
                bool const matchingMove = object && last.lastMoveToMapId == bot->GetMapId()
                    && object->GetExactDist(last.lastMoveToX, last.lastMoveToY, last.lastMoveToZ)
                        < INTERACTION_DISTANCE;
                bool const expired = matchingMove && uint32_t(now - last.msTime) >= 30000;
                bool const approaching = matchingMove && bot->isMoving() && !expired;
                if (bodyEngine->ExecuteAction("open loot") == ACTION_RESULT_OK || approaching
                    || (!expired && bodyEngine->ExecuteAction("move to loot") == ACTION_RESULT_OK))
                {
                    pause(BodyControl::Interrupt::Maintenance);
                    break;
                }
                if (!current.IsEmpty())
                    loot->Defer(current.guid);
                target->Set(LootObject());
            }
            bool const attempting = control.phase == QuestObjectiveControl::Phase::Attempting;
            if (!attempting || bodyEngine->ExecuteAction("attack anything") != ACTION_RESULT_OK)
                bodyEngine->ExecuteAction("new rpg do quest");
            break;
        }
        case BodyControl::Skill::Idle:
        case BodyControl::Skill::Follow:
        case BodyControl::Skill::Repair:
        case BodyControl::Skill::Supplies:
        case BodyControl::Skill::Activity:
        case BodyControl::Skill::Rendezvous:
            break;
    }
    if (control.failure != QuestObjectiveControl::Failure::None)
        body.state = BodyControl::State::Blocked;
    return true;
}
