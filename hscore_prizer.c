#include <math.h>
#include "asss.h"
#include "hscore.h"

local Imodman *mm;
local Ichat *chat;
local Igame *game;
local Ihscoreitems *items;
local Inet *net;
local Iarenaman *aman;

local HashTable *inCenterWarper;

local int spawnkey;

typedef struct SpawnData
{
	int checkx1;
	int checky1;
	int checkx2;
	int checky2;

	int spawnx1;
	int spawny1;
	int spawnx2;
	int spawny2;
} SpawnData;

local void prize(Player *p)
{
	Target *t;
	t->type = T_PLAYER;
	t->u.p = p;

	game->GivePrize(t, 9, items->getPropertySum(p, p->pkt.ship, "bomblevel"));
	game->GivePrize(t, 8, items->getPropertySum(p, p->pkt.ship, "gunlevel"));

	game->GivePrize(t, 21, items->getPropertySum(p, p->pkt.ship, "repel"));
	game->GivePrize(t, 22, items->getPropertySum(p, p->pkt.ship, "burst"));
	game->GivePrize(t, 23, items->getPropertySum(p, p->pkt.ship, "thor"));
	game->GivePrize(t, 24, items->getPropertySum(p, p->pkt.ship, "portal"));
	game->GivePrize(t, 25, items->getPropertySum(p, p->pkt.ship, "decoy"));
	game->GivePrize(t, 26, items->getPropertySum(p, p->pkt.ship, "brick"));
	game->GivePrize(t, 27, items->getPropertySum(p, p->pkt.ship, "rocket"));

	game->GivePrize(t, 6, items->getPropertySum(p, p->pkt.ship, "xradar"));
	game->GivePrize(t, 5, items->getPropertySum(p, p->pkt.ship, "cloak"));
	game->GivePrize(t, 4, items->getPropertySum(p, p->pkt.ship, "stealth"));
	game->GivePrize(t, 20, items->getPropertySum(p, p->pkt.ship, "antiwarp"));

	game->GivePrize(t, 11, items->getPropertySum(p, p->pkt.ship, "thrust"));
	game->GivePrize(t, 12, items->getPropertySum(p, p->pkt.ship, "speed"));
	game->GivePrize(t, 2, items->getPropertySum(p, p->pkt.ship, "energy"));
	game->GivePrize(t, 1, items->getPropertySum(p, p->pkt.ship, "recharge"));
	game->GivePrize(t, 3, items->getPropertySum(p, p->pkt.ship, "rotation"));

	game->GivePrize(t, 10, items->getPropertySum(p, p->pkt.ship, "bounce"));
	game->GivePrize(t, 16, items->getPropertySum(p, p->pkt.ship, "prox"));
	game->GivePrize(t, 19, items->getPropertySum(p, p->pkt.ship, "shrapnel"));
	game->GivePrize(t, 15, items->getPropertySum(p, p->pkt.ship, "multifire"));
}

local void Pppk(Player *p, byte *p2, int len)
{
    struct C2SPosition *pos = (struct C2SPosition *)p2;

	Arena *arena = p->arena;
	SpawnData *sd = P_ARENA_DATA(arena, spawnkey);
	Target t;
	t.type = T_PLAYER;
	t.u.p = p;

	/* handle common errors */
	if (!arena) return;

	/* speccers don't get their position sent to anyone */
	if (p->p_ship == SHIP_SPEC)
		return;

	if (sd->checkx1 != 0) //module attached to the arena? (maybe a bad test, eh?)
	{
		if (sd->checkx1 < pos->x>>4 && pos->x>>4 < sd->checkx2 && sd->checky1 < pos->y>>4 && pos->y>>4 < sd->checky2)
		{
			int *last = HashGetOne(inCenterWarper, p->name);
			if (!last || *last < current_ticks())
			{
				afree(last);
				int * time = amalloc(sizeof(*time));
				*time = current_ticks() + 200;
				HashReplace(inCenterWarper, p->name, time);

				int x = (rand() % (sd->spawnx2 - sd->spawnx1)) + sd->spawnx1;
				int y = (rand() % (sd->spawny2 - sd->spawny1)) + sd->spawny1;

				game->WarpTo(&t, x, y);
				if (p->position.bounty == 0)
				{
					prize(p);
				}
			}
		}
	}
}

EXPORT int MM_hscore_prizer(int action, Imodman *_mm, Arena *arena)
{
	if(action == MM_LOAD)
	{
		mm = _mm;

		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);

		if(!chat || !game || !items || !net || !aman)
			return MM_FAIL;

		spawnkey = aman->AllocateArenaData(sizeof(SpawnData));
		if (spawnkey == -1)
            return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		inCenterWarper = HashAlloc();

		return MM_OK;
	}
	else if(action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);

		aman->FreeArenaData(spawnkey);

		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(net);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
    {
		SpawnData *sd = P_ARENA_DATA(arena, spawnkey);

		sd->checkx1 = cfg->GetInt(arena->cfg, "hyperspace", "checkx1", 502);
		sd->checky1 = cfg->GetInt(arena->cfg, "hyperspace", "checky1", 502);
		sd->checkx2 = cfg->GetInt(arena->cfg, "hyperspace", "checkx2", 520);
		sd->checky2 = cfg->GetInt(arena->cfg, "hyperspace", "checky2", 520);

		sd->spawnx1 = cfg->GetInt(arena->cfg, "hyperspace", "spawnx1", 322);
		sd->spawny1 = cfg->GetInt(arena->cfg, "hyperspace", "spawny1", 322);
		sd->spawnx2 = cfg->GetInt(arena->cfg, "hyperspace", "spawnx2", 700);
		sd->spawny2 = cfg->GetInt(arena->cfg, "hyperspace", "spawny2", 700);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        return MM_OK;
    }
	return MM_FAIL;
}
