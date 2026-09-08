/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#ifndef PLAYERBOTS_QUEST_OBJECTIVE_CONTROL_H
#define PLAYERBOTS_QUEST_OBJECTIVE_CONTROL_H

#include <cstdint>
#include <set>

// Value-only ownership boundary. The planner selects a quest; New RPG still executes ordinary gameplay.
// A failure is an observation for the owner, never permission to teleport or abandon its quest.
struct QuestObjectiveControl
{
    enum class Phase : uint8_t
    {
        Selecting, Traveling, Attempting, TurnIn
    };
    enum class Failure : uint8_t
    {
        None, MissingLocation, Navigation, NoProgress, Reward
    };

    uint64_t token = 0;
    uint32_t quest = 0;
    uint32_t place = 0;
    uint64_t scans = 0;
    bool lastScanEmpty = false;
    bool plannerAttached = false;
    uint32_t cooperativeQuest = 0;
    uint8_t partyMembers = 0; // Set only after the planner observes agreement, membership and readiness.
    // Gathering/recovery/support retains combat and loot, but holds independent pulls and routes.
    bool cooperationHold = false;
    bool cooperativeTurnIn = false; // Every participant has finished gathering credit; each still needs its own reward.

    bool PartyReady(uint32_t questId, uint32_t requiredMembers) const
    {
        return plannerAttached && questId && cooperativeQuest == questId && partyMembers >= 2 && partyMembers <= 5
            && partyMembers >= requiredMembers;
    }
    Phase phase = Phase::Selecting;
    Failure failure = Failure::None;
    std::set<uint32_t> retained;
    std::set<uint32_t> deferred;

    bool Claim(uint64_t ownerToken, uint32_t questId)
    {
        if (!ownerToken || !questId || (token && (token != ownerToken || quest != questId || place)))
            return false;
        if (!token)
        {
            token = ownerToken;
            quest = questId;
            phase = Phase::Selecting;
            failure = Failure::None;
        }
        retained.insert(questId);
        deferred.erase(questId);
        return true;
    }

    bool ClaimPlace(uint64_t ownerToken, uint32_t area)
    {
        if (!ownerToken || !area || (token && (token != ownerToken || place != area || quest)))
            return false;
        if (!token)
        {
            token = ownerToken;
            place = area;
            scans = 0;
            lastScanEmpty = false;
            phase = Phase::Selecting;
            failure = Failure::None;
        }
        return true;
    }

    bool Release(uint64_t ownerToken)
    {
        if (!token || token != ownerToken)
            return false;
        token = 0;
        quest = 0;
        place = 0;
        scans = 0;
        lastScanEmpty = false;
        phase = Phase::Selecting;
        failure = Failure::None;
        return true;
    }

    bool Owns(uint32_t questId) const { return token && questId && quest == questId; }
    void Fail(Failure reason)
    {
        if (token && failure == Failure::None)
            failure = reason;
    }
};

#endif
