/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#ifndef PLAYERBOTS_BODY_CONTROL_H
#define PLAYERBOTS_BODY_CONTROL_H

#include <cstdint>
#include <string_view>

// The brain owns the intention; the body owns execution. This state is transient and never restored from a save.
// A stale heartbeat holds the body instead of silently handing the character to another autonomous planner.
struct BodyControl
{
    enum class Skill : uint8_t { Idle, Travel, Investigate, Quest, Follow, Repair, Supplies, Rendezvous, Activity };
    enum class State : uint8_t { Idle, Running, Interrupted, Blocked, Arrived };
    enum class Interrupt : uint8_t { None, Combat, Recovery, Maintenance, Human, BrainUnavailable };

    uint64_t generation = 0;
    uint64_t attachment = 0;
    uint64_t objective = 0;
    uint32_t heartbeat = 0;
    Skill skill = Skill::Idle;
    State state = State::Idle;
    Interrupt interruption = Interrupt::None;

    bool Attached() const { return generation && attachment; }
    bool Directed() const { return skill >= Skill::Follow && skill <= Skill::Activity; }
    bool Fresh(uint32_t now) const { return Attached() && uint32_t(now - heartbeat) <= 5000; }
    bool MayStartQuestCombat(uint64_t token, bool attempting, uint32_t now) const
    {
        return !Attached() || (Fresh(now) && skill == Skill::Quest && objective == token && attempting
            && state != State::Blocked);
    }

    bool Attach(uint64_t ownerGeneration, uint64_t ownerAttachment, uint32_t now)
    {
        if (!ownerGeneration || !ownerAttachment)
            return false;
        if (Attached() && (generation != ownerGeneration || attachment != ownerAttachment))
            return false; // A new body must explicitly detach the old incarnation first.
        generation = ownerGeneration;
        attachment = ownerAttachment;
        heartbeat = now;
        return true;
    }

    bool Issue(uint64_t ownerGeneration, uint64_t ownerAttachment, uint64_t token, Skill next, uint32_t now)
    {
        if (!Attached() || generation != ownerGeneration || attachment != ownerAttachment
            || next > Skill::Activity || (next != Skill::Idle && !token))
            return false;
        if (objective != token || skill != next)
        {
            objective = token;
            skill = next;
            state = next == Skill::Idle ? State::Idle : State::Running;
            interruption = Interrupt::None;
        }
        heartbeat = now;
        return true;
    }

    bool Detach(uint64_t ownerGeneration, uint64_t ownerAttachment)
    {
        if (generation != ownerGeneration || attachment != ownerAttachment)
            return false;
        *this = {};
        return true;
    }

    void Pause(Interrupt reason)
    {
        interruption = reason;
        if (state != State::Blocked && state != State::Arrived)
            state = State::Interrupted;
    }

    void Resume()
    {
        interruption = Interrupt::None;
        if (state == State::Interrupted)
            state = skill == Skill::Idle ? State::Idle : State::Running;
    }

    static std::string_view Name(Skill value)
    {
        switch (value)
        {
            case Skill::Idle: return "idle";
            case Skill::Travel: return "travel";
            case Skill::Investigate: return "investigate";
            case Skill::Quest: return "quest";
            case Skill::Follow: return "follow";
            case Skill::Repair: return "repair";
            case Skill::Supplies: return "supplies";
            case Skill::Rendezvous: return "rendezvous";
            case Skill::Activity: return "activity";
        }
        return "unknown";
    }

    static std::string_view Name(State value)
    {
        switch (value)
        {
            case State::Idle: return "idle";
            case State::Running: return "running";
            case State::Interrupted: return "interrupted";
            case State::Blocked: return "blocked";
            case State::Arrived: return "arrived";
        }
        return "unknown";
    }

    static std::string_view Name(Interrupt value)
    {
        switch (value)
        {
            case Interrupt::None: return "none";
            case Interrupt::Combat: return "combat";
            case Interrupt::Recovery: return "recovery";
            case Interrupt::Maintenance: return "maintenance";
            case Interrupt::Human: return "human";
            case Interrupt::BrainUnavailable: return "brain_unavailable";
        }
        return "unknown";
    }
};

#endif
