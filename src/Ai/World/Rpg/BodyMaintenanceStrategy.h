/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#ifndef PLAYERBOTS_BODY_MAINTENANCE_STRATEGY_H
#define PLAYERBOTS_BODY_MAINTENANCE_STRATEGY_H

#include "Strategy.h"

class BodyMaintenanceStrategy : public Strategy
{
public:
    explicit BodyMaintenanceStrategy(PlayerbotAI* ai) : Strategy(ai) { }
    std::string const getName() override { return "body maintenance"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        // Process physical interaction receipts. Guild/LFG/duel/quest selection is deliberately absent.
        triggers.push_back(new TriggerNode("group invite", {NextAction("accept invitation", 100.0f)}));
        triggers.push_back(new TriggerNode("loot response", {NextAction("store loot", 100.0f)}));
        triggers.push_back(new TriggerNode("item push result", {NextAction("quest item push result", 100.0f)}));
        triggers.push_back(new TriggerNode("quest update add kill", {NextAction("quest update add kill", 100.0f)}));
        triggers.push_back(new TriggerNode("quest update failed", {NextAction("quest update failed", 100.0f)}));
        triggers.push_back(new TriggerNode("quest update complete", {NextAction("quest update complete", 100.0f)}));
        triggers.push_back(new TriggerNode("uninvite", {NextAction("uninvite", 100.0f)}));
        triggers.push_back(new TriggerNode("uninvite guid", {NextAction("uninvite", 100.0f)}));
        triggers.push_back(new TriggerNode("loot roll", {NextAction("loot roll", 100.0f)}));
    }
};

#endif
