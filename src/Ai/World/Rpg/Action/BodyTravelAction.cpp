/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#include "NewRpgBaseAction.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Playerbots.h"

bool NewRpgBaseAction::MoveBodyTo(WorldPosition const& dest)
{
    auto& info = botAI->rpgInfo;
    auto& travel = info.bodyTravel;
    auto const now = getMSTime();
    if (dest == WorldPosition() || dest.GetMapId() != bot->GetMapId())
    {
        info.objectiveControl.Fail(QuestObjectiveControl::Failure::MissingLocation);
        return false;
    }
    if (dest != info.moveFarPos)
    {
        info.SetMoveFarTo(dest);
        travel = {};
    }
    if (bot->IsInCombat() || !bot->IsAlive() || bot->IsBeingTeleported() || bot->IsInFlight()
        || bot->IsSitState() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
    {
        travel.Pause(now);
        return false;
    }
    BodyTravel::Point const position{bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
    travel.Observe(position, now);
    if (bot->GetExactDist(dest) < 5)
    {
        travel.ClearPath();
        return true;
    }
    if (travel.HasPath())
    {
        auto const end = travel.End();
        if (position.Distance(end) < 5)
            travel.ClearPath();
        else if (travel.Stalled())
        {
            // Clear only a matching owned point movement, never combat/follow/flight motion.
            auto const& last = AI_VALUE(LastMovement&, "last movement");
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE
                && last.lastMoveToMapId == bot->GetMapId() && last.lastMoveToX == end.x
                && last.lastMoveToY == end.y && last.lastMoveToZ == end.z)
            {
                bot->GetMotionMaster()->Clear();
                bot->StopMoving();
            }
            travel.ClearPath();
            ++travel.failures;
        }
        else
        {
            auto const& last = AI_VALUE(LastMovement&, "last movement");
            if (bot->isMoving() && last.lastMoveToMapId == bot->GetMapId()
                && last.lastMoveToX == end.x && last.lastMoveToY == end.y && last.lastMoveToZ == end.z)
                return true;
            if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
                return true;
            // Combat or recovery can have replaced the spline. Resume the same committed leg.
            return MoveTo(bot->GetMapId(), end.x, end.y, end.z, false, false, false, true);
        }
    }
    if (travel.failures >= 3)
    {
        info.objectiveControl.Fail(QuestObjectiveControl::Failure::Navigation);
        return false;
    }
    if (travel.nextAttempt && int32_t(now - travel.nextAttempt) < 0)
        return false;
    travel.nextAttempt = now + 2000;
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
        return true;

    uint32_t const permitted = PATHFIND_NORMAL | PATHFIND_INCOMPLETE;
    // Smooth paths have a fixed point budget. Ask for the corridor's corners first, then walk a bounded
    // leg along that corridor; a far destination must not become a rejected shortcut at the smooth-path cap.
    PathGenerator corridor(bot);
    corridor.SetUseStraightPath(true);
    bool const routed = corridor.CalculatePath(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ())
        && !(corridor.GetPathType() & ~permitted);
    auto leg = corridor.GetActualEndPosition();
    if (routed)
    {
        float remaining = 160.0f;
        auto const& corners = corridor.GetPath();
        for (std::size_t index = 1; index < corners.size(); ++index)
        {
            auto const segment = corners[index] - corners[index - 1];
            float const length = segment.length();
            if (length > remaining)
            {
                leg = corners[index - 1] + segment * (remaining / length);
                break;
            }
            remaining -= length;
        }
    }
    PathGenerator path(bot);
    bool const calculated = routed && path.CalculatePath(leg.x, leg.y, leg.z);
    auto const& endpoint = path.GetActualEndPosition();
    BodyTravel::Point const end{endpoint.x, endpoint.y, endpoint.z};
    if (calculated && !(path.GetPathType() & ~permitted) && !travel.Tried(end))
    {
        std::vector<BodyTravel::Point> points;
        for (auto const& point : path.GetPath())
            points.push_back({point.x, point.y, point.z});
        BodyTravel candidate = travel;
        if (candidate.Commit(std::move(points), now)
            && MoveTo(bot->GetMapId(), end.x, end.y, end.z, false, false, false, true))
        {
            candidate.Remember(end);
            travel = std::move(candidate);
            return true;
        }
    }
    ++travel.failures;
    if (travel.failures >= 3)
        info.objectiveControl.Fail(QuestObjectiveControl::Failure::Navigation);
    return false;
}
