#include "asss.h"
#include "hscore.h"
#include "hscore_storeman.h"
#include "hscore_database.h"
#include "hscore_shipnames.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Ihscoremoney *money;
local Ihscoreitems *items;
local Ihscoredatabase *database;

local void printAllCategories(Player *p)
{
	PerArenaData *arenaData = database->getPerArenaData(player->arena);
	Link *link;

	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
	chat->SendMessage(p, "| Category Name                    | Category Description                                             |");
	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
	chat->SendMessage(p, "| Ships                            | All the ship hulls can you buy in this arena.                    |");

	for (link = LLGetHead(&(arenaData->categoryList)); link; link = link->next)
	{
		Category *category = link->data;

		chat->SendMessage(p, "| %-32s | %-64s |", category->name, category->description);
	}

	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
}

local void printCategoryItems(Player *p, Category *category)
{
	chat->SendMessage(p, "<print category items in %s>", category->name);
}

local void printShipList(Player *p)
{
	chat->SendMessage(p, "<print ship list>");
}

local void buyItem(Player *p, Item *item)
{
	chat->SendMessage(p, "<buy item %s>", item->name);
}

local void sellItem(Player *p, Item *item)
{
	chat->SendMessage(p, "<sell item %s>", item->name);
}

local void buyShip(Player *p, int ship)
{
	chat->SendMessage(p, "<buy ship #%i>", ship);
}

local void sellShip(Player *p, int ship)
{
	chat->SendMessage(p, "<sell ship #%i>", ship);
}

local helptext_t buyHelp =
"Targets: none\n"
"Args: none or <item> or <category> or <ship>\n"
"If there is no arugment, this command will display a list of this arena's buy categories.\n"
"If the argument is a category, this command will display all the items in that category.\n"
"If the argument is an item, this command will attemt to buy it for the buy price.\n";

local void buyCommand(const char *command, const char *params, Player *p, const Target *target)
{
	PerArenaData *arenaData = database->getPerArenaData(player->arena);

	if (strcasecmp(params, "") == 0) //no params
	{
		printAllCategories(p);
	}
	else //has params
	{
		if (strcasecmp(params, "ships") == 0) //print ship list
		{
			printShipList(p);
		}
		else
		{
			//check if they're asking for a ship
			for (int i = 0; i < 8; i++)
			{
				if (strcasecmp(params, shipNames[i]) == 0)
				{
					buyShip(p, i);
				}
			}

			//check if they're asking for a category
			for (link = LLGetHead(&(arenaData->categoryList)); link; link = link->next)
			{
				Category *category = link->data;

				if (strcasecmp(params, category->name) == 0)
				{
					printCategoryItems(p, category);
					return;
				}
			}

			//not a category. check for an item
			Item *item = items->getItemByName(params, p->arena);
			if (item != NULL)
			{
				buyItem(p, item);
			}

			//neither an item nor a ship nor a category
			chat->SendMessage(p, "No item %s in this arena.", params);
		}
	}
}

local helptext_t sellHelp =
"Targets: none\n"
"Args: <item> or [{-f}] <ship>\n"
"Removes the item from your ship and refunds you the item's sell price.\n"
"If selling a ship, the {-f} will force the ship to be sold, even if the ship\n"
"is not empty, destroying all items onboard.\n";

local void sellCommand(const char *command, const char *params, Player *p, const Target *target)
{
	PerArenaData *arenaData = database->getPerArenaData(player->arena);

	if (strcasecmp(params, "") == 0) //no params
	{
		chat->SendMessage(p, "Please use ?buy to find the item you wish to sell");
	}
	else //has params
	{
		//check if they're asking for a ship
		for (int i = 0; i < 8; i++)
		{
			if (strcasecmp(params, shipNames[i]) == 0)
			{
				buyShip(p, i);
			}
		}

		//check for an item
		Item *item = items->getItemByName(params, p->arena);
		if (item != NULL)
		{
			sellItem(p, item);
		}

		//not a ship nor an item
		chat->SendMessage(p, "No item %s in this arena.", params);
	}
}

EXPORT int MM_hscore_buysell(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		money = mm->GetInterface(I_HSCORE_MONEY, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !chat || !cfg || !cmd || !money || !items || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(money);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(database);

			return MM_FAIL;
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(money);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		cmd->AddCommand("buy", buyCommand, arena, buyHelp);
		cmd->AddCommand("sell", sellCommand, arena, sellHelp);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		cmd->RemoveCommand("buy", buyCommand, arena);
		cmd->RemoveCommand("sell", sellCommand, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
