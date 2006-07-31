//HSCore Rewards
//D1st0rt and Bomook
//5/31/05

#include "asss.h"
#include "fg_wz.h"
#include "hscore.h"
#include <math.h>
#include "hscore_teamnames.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Iplayerdata *pd;
local Ichat *chat;
local Iconfig *cfg;
local Ihscoremoney *money;

//This is assuming we're using fg_wz.py
local void flagWinCallback(Arena *arena, int freq, int *points)
{
	int players = 0, onfreq = 0, points, exp;
	Player *i;
	Link *link;
	Ijackpot *jackpot;
	Iteamnames *teamnames;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
			i->arena == arena &&
			i->p_ship != SHIP_SPEC &&
			IS_HUMAN(i))
		{
			players++;
			if (i->p_freq == freq)
				onfreq++;
		}
	pd->Unlock();

	jackpot = mm->GetInterface(I_JACKPOT, arena);
	if (jackpot)
	{
		points = jackpot->GetJP(arena);
		mm->ReleaseInterface(jackpot);
	}
	
	exp = (players - onfreq);

	/* cfghelp: Flag:SplitPoints, arena, bool, def: 0
	 * Whether to split a flag reward between the members of a freq or
	 * give them each the full amount. */
	if (onfreq > 0 && cfg->GetInt(arena->cfg, "Flag", "SplitPoints", 0))
		points /= onfreq;

	teamnames = mm->GetInterface(I_TEAMNAMES, arena);
	if (teamnames)
	{
		const char *name = teamnames->getFreqTeamName(freq, arena);
		if (name != NULL)
		{
			chat->SendArenaMessage(arena, "%s won flag game. Reward: $%d (%d exp)", name, points, exp);
		}
		else
		{
			chat->SendArenaMessage(arena, "Unidentified team won flag game. Reward: $%d (%d exp)", points, exp);
		}
	}
	else
	{
		chat->SendArenaMessage(arena, "Reward: $%d (%d exp)", points, exp);
	}

	 //Distribute Wealth
    pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if(i->arena == arena && i->p_freq == freq && i->p_ship != SHIP_SPEC)
		{
			money->giveMoney(i, points, MONEY_TYPE_FLAG);
			money->giveExp(i, exp);
			//no need to send message, as the team announcement works just fine
			//chat->SendMessage(p, "You received $%d and %d exp for a flag victory.", reward, exp);
		}
	}
	pd->Unlock();
}

local void goalCallback(Arena *arena, Player *scorer, int bid, int x, int y)
{
	/*
	 * money = coefficient * (pop^0.5) + minimum
	 * Defaults:
	 * coefficient = 500, minimum = 300
	 */

	//Variable Declarations
	double amount, coeff, min;
	int reward, exp;
	Player *p;
	Link *link;

	//Read Settings
	/* cfghelp: Hyperspace:GoalCoeff, arena, int, def: 50, mod: hscore_rewards
	 * The goal reward coefficient. */
	coeff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "GoalCoeff", 50);
	/* cfghelp: Hyperspace:GoalMin, arena, int, def: 30, mod: hscore_rewards
	 * The amount to add to the goal reward. */
	min   = (double)cfg->GetInt(arena->cfg, "Hyperspace", "GoalMin",   30);

	//Calculate Reward
	amount = pow((double)arena->playing, 0.5);
	amount *= coeff;
	amount += min;
	reward  = (int)amount;
	exp = (int)(amount / 10);

	//Distribute Wealth
	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if(p->p_freq == scorer->p_freq && p->p_ship != SHIP_SPEC)
		{
			money->giveMoney(p, reward, MONEY_TYPE_BALL);
			money->giveExp(p, exp);
			chat->SendMessage(p, "You received $%d and %d exp for a team goal.", reward, exp);
		}
	}
	pd->Unlock();
}

local int calculateExpReward(Player *killer, Player *killed)
{
	/* cfghelp: Hyperspace:UseDiscrete, arena, int, def: 1, mod: hscore_rewards
	 * 1 = Use discrete method of calculating exp rewards.
	 * 0 = Use old exponential method.*/
	int useDiscrete = cfg->GetInt(arena->cfg, "Hyperspace", "UseDiscrete",  1);
	
	//get exp
	//add one to prevent divide by zero errors
	double kexp = (double) money->getExp(killer) + 1;
	double dexp = (double) money->getExp(killed) + 1;
	
	int reward = 0;
	
	if (useDiscrete)
	{
		double ratio = dexp / kexp;
		
		if (ratio < 0.25)
		{
			reward = 0;
		}
		else if (ratio < 0.75)
		{
			reward = 1;
		}
		else if (ratio < 1.25)
		{
			reward = 2;
		}
		else if (ratio < 2.00)
		{
			reward = 3;
		}
		else
		{
			reward = 4;
		}
	}
	else
	{
		/* cfghelp: Hyperspace:KillCoeff, arena, int, def: 10, mod: hscore_rewards
		 * Kill reward coefficient (discrete = 0). */
		double coeff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "KillCoeff", 10);
		/* cfghelp: Hyperspace:KillMin, arena, int, def: 1, mod: hscore_rewards
		 * Amount added to exp (discrete = 0). */
		double min   = (double)cfg->GetInt(arena->cfg, "Hyperspace", "KillMin",   1);
		
		//Calculate Earned Experience
		double amount = log(dexp + 1) / log(kexp + 2);
		amount *= coeff;
		amount += min;
		reward = (int)amount;
	}
	
	return reward;
}

//bonus money is shared between teammates
local int calculateBonusMoneyReward(Player *killer, Player *killed)
{
	/* cfghelp: Hyperspace:KillerBountyMult, arena, int, def: 1, mod: hscore_rewards
	 * Amount to multiply by killer's bounty to add to the money reward.  1000 = 100%*/
	double killerBountyMult = (double)cfg->GetInt(arena->cfg, "Hyperspace", "KillerBountyMult",  1) / 500.0;
	/* cfghelp: Hyperspace:KilleeBountyMult, arena, int, def: 1, mod: hscore_rewards
	 * Amount to multiply by killee's bounty to add to the money reward. 1000 = 100% */
	double killeeBountyMult = (double)cfg->GetInt(arena->cfg, "Hyperspace", "KilleeBountyMult",  1) / 500.0;
	
	double bountyBonus = (double)(killer->position.bounty) * killerBountyMult;
	bountyBonus += (double)(killed->position.bounty) * killeeBountyMult;
	
	//other bonuses can be added here in the future
	
	return (int)bountyBonus;
}

//base money is for the killing player only
local int calculateBaseMoneyReward(Player *killer, Player *killed)
{
	/* cfghelp: Hyperspace:KilleeBountyMult, arena, int, def: 75, mod: hscore_rewards
	 * base money = base multipiler * exponental formula. */
	double baseMultiplier = (double)cfg->GetInt(arena->cfg, "Hyperspace", "BaseMultiplier",  75);
	
	double kexp = (double) money->getExp(killer) + 1;
	double dexp = (double) money->getExp(killed) + 1;
	
	double baseMoney = exp( -kexp / (dexp + 1)) * baseMultiplier;
	
	return (int)baseMoney;
}

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	if(killer->p_freq == killed->p_freq)
	{
		chat->SendMessage(killer, "No reward for teamkill of %s.", killed->name);
	}
	else
	{
		/* cfghelp: Hyperspace:MinBonusPlayers, arena, int, def: 4, mod: hscore_rewards
		 * Minimum number of players in game required for bonus money. */
		int minBonusPlayers  = cfg->GetInt(arena->cfg, "Hyperspace", "MinBonusPlayers",  4);
		/* cfghelp: Hyperspace:DisableMoneyRewards, arena, int, def: 0, mod: hscore_rewards
		 * If no money should be awarded for kills. */
		int disableMoneyRewards  = cfg->GetInt(arena->cfg, "Hyperspace", "DisableMoneyRewards",  0);
		/* cfghelp: Hyperspace:DisableExpRewards, arena, int, def: 0, mod: hscore_rewards
		 * If no exp should be awarded for kills. */
		int disableMoneyRewards  = cfg->GetInt(arena->cfg, "Hyperspace", "DisableExpRewards",  0);

		//Calculate Earned Money
		int exp = calculateExpReward(killer, killed);
		int baseMoney = calculateBaseMoneyReward(killer, killed);
		int bonusMoney = calculateBonusMoneyReward(killer, killed);

		if (arena->playing < minBonusPlayers)
		{
			bonusMoney = 0;
		}
		
		//Distribute Wealth
		if (!disableExpRewards)
		{
			money->giveExp(killer, exp);
			
			if (disableMoneyRewards)
			{
				chat->SendMessage(killer, "You received %d exp for killing %s.", exp, killed->name);
			}
		}
		
		if (!disableMoneyRewards)
		{
			money->giveMoney(killer, baseMoney + bonusMoney, MONEY_TYPE_KILL);
			
			if (disableExpRewards)
			{
				chat->SendMessage(killer, "You received %d money for killing %s.", baseMoney + bonusMoney, killed->name);
			}			
			else
			{
				chat->SendMessage(killer, "You received %d money and %d exp for killing %s.", baseMoney + bonusMoney, exp, killed->name);
			}
		
			//give money to teammates
			Player *p;
			Link *link;
			/* cfghelp: Hyperspace:TeammateReward, arena, int, def: 500, mod: hscore_rewards
			 * The percentage (max) that a teammate can receive from a kill.
			 * 1000 = 100%*/
			double teammateRewardCoeff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "TeammateReward", 500) / 1000.0; //50%
			/* cfghelp: Hyperspace:DistFalloff, arena, int, def: 1440000, mod: hscore_rewards
			 * Distance falloff divisor in pixels^2. */
			double distanceFalloff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "DistFalloff", 1440000); //pixels^2
			
			pd->Lock();
			FOR_EACH_PLAYER(p)
			{
				if(p->arena == killer->arena && p->p_freq == killer->p_freq && p->p_ship != SHIP_SPEC && p != killer && !(p->position.status & STATUS_SAFEZONE))
				{
					double maxReward = teammateRewardCoeff * (double)(bonusMoney + calculateBaseMoneyReward(p, killed));

					int xdelta = (p->position.x - killer->position.x);
					int ydelta = (p->position.y - killer->position.y);
					double distPercentage = ((double)(xdelta * xdelta + ydelta * ydelta)) / distanceFalloff;

					int reward = (int)(maxReward * exp(-distPercentage));

					money->giveMoney(p, reward, MONEY_TYPE_KILL);

					//check if they received more than %30. if they did, message them. otherwise, don't bother.
					if (reward > (int)(0.30 * maxReward))
					{
						chat->SendMessage(p, "You received $%d for %s's kill of %s.", reward, killer->name, killed->name);
					}
				}
			}
			pd->Unlock();
		}
	}
}

local int getPeriodicPoints(Arena *arena, int freq, int freqplayers, int totalplayers, int flagsowned)
{
	/*
	 * money = (flagcoeff * flagcount) / (teamcoeff * teamsize)
	 * Defaults:
	 * flagcoeff = 200, teamcoeff = 1
	 */

	//Variable Declarations
	double amount, fcoeff, tcoeff;
	int reward, exp;
	Player *p;
	Link *link;


	//Read Settings
	/* cfghelp: Hyperspace:PeriodicCoeff, arena, int, def: 200, mod: hscore_rewards
	 * Periodic Flag Coefficient. */
	fcoeff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "PeriodicCoeff", 200);
	/* cfghelp: Hyperspace:PeriodicCoeff, arena, int, def: 200, mod: hscore_rewards
	 * Periodic Team Coefficient. */
	tcoeff = (double)cfg->GetInt(arena->cfg, "Hyperspace", "PeriodicTeamCoeff", 200);


	//Calculate reward
	amount  = (fcoeff * (double)flagsowned);
	amount /= (tcoeff * (double)freqplayers);
	reward = (int)amount;
	exp = (int)(amount / 15);

	//Distribute Wealth
	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if(p->p_freq == freq && p->p_ship != SHIP_SPEC)
		{
			money->giveMoney(p, reward, MONEY_TYPE_FLAG);
			money->giveExp(p, exp);
			chat->SendMessage(p, "You received $%d and %d exp for holding %d flag(s).", reward, exp, flagsowned);
		}
	}
	pd->Unlock();

	return reward;
}

local Iperiodicpoints periodicInterface =
{
	INTERFACE_HEAD_INIT(I_PERIODIC_POINTS, "pp-basic")
	getPeriodicPoints
};

EXPORT const char info_hscore_rewards[] = "v1.1 D1st0rt";

EXPORT int MM_hscore_rewards(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		money = mm->GetInterface(I_HSCORE_MONEY, ALLARENAS);

		if (!lm || !chat || !cfg || !money || !pd)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(money);

			return MM_FAIL;
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(money);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&periodicInterface, arena);

		mm->RegCallback(CB_WARZONEWIN, flagWinCallback, arena);
		mm->RegCallback(CB_GOAL, goalCallback, arena);
		mm->RegCallback(CB_KILL, killCallback, arena);

		//chat->SendArenaMessage(arena, "OMGWTFBBQ!!!");

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&periodicInterface, arena);

		mm->UnregCallback(CB_WARZONEWIN, flagWinCallback, arena);
		mm->UnregCallback(CB_GOAL, goalCallback, arena);
		mm->UnregCallback(CB_KILL, killCallback, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
