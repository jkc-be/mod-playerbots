/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DestroyItemAction.h"
#include "Event.h"
#include "ItemCountValue.h"
#include "Observatory.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include <algorithm>
#include <iterator>
#include <limits>

bool DestroyJunkAction::isUseful()
{
    return sPlayerbotAIConfig.autoDestroyJunk && sRandomPlayerbotMgr.IsRandomBot(bot) &&
           !botAI->HasGameClientMaster() && !IsSelfBot(bot) && bot->IsAlive() && !bot->IsInCombat() &&
           AI_VALUE(uint8, "bag space") > 80;
}

bool DestroyJunkAction::Execute(Event /*event*/)
{
    // Recheck when executing: a queued action must not outlive a change of master or inventory pressure.
    context->GetValue<uint8>("bag space")->Reset();
    if (!isUseful())
        return false;

    FindItemsByQualityVisitor visitor(ITEM_QUALITY_POOR, std::numeric_limits<uint32>::max());
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);
    std::set<uint32> protectedItems = AI_VALUE(std::set<uint32>&, "always loot list");
    for (uint8 questSlot = 0; questSlot < MAX_QUEST_LOG_SIZE; ++questSlot)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(bot->GetQuestSlotQuestId(questSlot));
        if (!quest)
            continue;
        protectedItems.insert(quest->GetSrcItemId());
        protectedItems.insert(std::begin(quest->RequiredItemId), std::end(quest->RequiredItemId));
        protectedItems.insert(std::begin(quest->ItemDrop), std::end(quest->ItemDrop));
    }
    Item* cheapest = nullptr;
    uint64 cheapestPrice = std::numeric_limits<uint64>::max();
    for (Item* item : visitor.GetResult())
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (item->IsInTrade() || item->IsRefundable() || proto->HasFlag(ITEM_FLAG_NO_USER_DESTROY) ||
            protectedItems.contains(proto->ItemId) || proto->StartQuest || proto->Class == ITEM_CLASS_QUEST ||
            proto->Class == ITEM_CLASS_KEY || proto->Class == ITEM_CLASS_CONTAINER ||
            proto->Class == ITEM_CLASS_QUIVER || proto->Class == ITEM_CLASS_CONSUMABLE ||
            proto->Class == ITEM_CLASS_REAGENT || proto->Class == ITEM_CLASS_PROJECTILE)
            continue;

        if (std::any_of(std::begin(proto->Spells), std::end(proto->Spells),
                        [](auto const& spell) { return spell.SpellId != 0; }))
            continue;

        std::string const qualifier = std::to_string(proto->ItemId);
        context->GetValue<ItemUsage>("item usage", qualifier)->Reset();
        ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", qualifier);
        if (usage != ITEM_USAGE_NONE && usage != ITEM_USAGE_VENDOR)
            continue;

        uint64 const price = uint64(proto->SellPrice) * item->GetCount();
        if (!cheapest || price < cheapestPrice)
        {
            cheapest = item;
            cheapestPrice = price;
        }
    }

    if (!cheapest)
        return false;

    uint32 const itemId = cheapest->GetEntry();
    uint32 const count = cheapest->GetCount();
    uint8 const bag = cheapest->GetBagSlot();
    uint8 const slot = cheapest->GetSlot();
    bot->DestroyItem(bag, slot, true);
    context->GetValue<uint8>("bag space")->Reset();
    context->GetValue<ItemUsage>("item usage", std::to_string(itemId))->Reset();
    Observatory::Event(bot, "junk_discarded", count, std::to_string(itemId));
    return true;
}

bool DestroyItemAction::Execute(Event event)
{
    std::string const text = event.getParam();
    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        DestroyItem(&visitor);
    }

    return true;
}

void DestroyItemAction::DestroyItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    for (Item* item : items)
    {
        std::ostringstream out;
        out << chat->FormatItem(item->GetTemplate()) << " destroyed";
        botAI->TellMaster(out);

        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }
}

bool SmartDestroyItemAction::isUseful() { return !IsRealPlayer(botAI->GetMaster()); }

bool SmartDestroyItemAction::Execute(Event /*event*/)
{
    uint8 bagSpace = AI_VALUE(uint8, "bag space");

    if (bagSpace < 90)
        return false;

    // Only destroy grey items when the master is a real player or selfbot, and the bot is in a real guild.
    if (botAI->HasGameClientMaster() && botAI->IsInRealGuild())
    {
        std::set<Item*> items;
        FindItemsToTradeByQualityVisitor visitor(ITEM_QUALITY_POOR, 5);
        IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);
        items.insert(visitor.GetResult().begin(), visitor.GetResult().end());

        for (auto& item : items)
        {
            FindItemByIdVisitor visitor(item->GetTemplate()->ItemId);
            DestroyItem(&visitor);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }
        return true;
    }

    std::vector<uint32> bestToDestroy = {ITEM_USAGE_NONE};  // First destroy anything useless.

    if (!AI_VALUE(bool, "can sell") &&
        AI_VALUE(
            bool,
            "should get money"))  // We need money so quest items are less important since they can't directly be sold.
        bestToDestroy.push_back(ITEM_USAGE_QUEST);
    else  // We don't need money so destroy the cheapest stuff.
    {
        bestToDestroy.push_back(ITEM_USAGE_VENDOR);
        bestToDestroy.push_back(ITEM_USAGE_AH);
    }

    // If we still need room
    bestToDestroy.push_back(
        ITEM_USAGE_SKILL);  // Items that might help tradeskill are more important than above but still expenable.
    bestToDestroy.push_back(ITEM_USAGE_USE);  // These are more likely to be usefull 'soon' but still expenable.

    for (auto& usage : bestToDestroy)
    {
        std::vector<Item*> items = AI_VALUE2(std::vector<Item*>, "inventory items", "usage " + std::to_string(usage));
        std::reverse(items.begin(), items.end());

        for (auto& item : items)
        {
            FindItemByIdVisitor visitor(item->GetTemplate()->ItemId);
            DestroyItem(&visitor);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }
    }

    return false;
}
