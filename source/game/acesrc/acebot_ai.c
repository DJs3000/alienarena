///////////////////////////////////////////////////////////////////////
//
//  ACE - Quake II Bot Base Code
//
//  Version 1.0
//
//  This file is Copyright(c), Steve Yeager 1998, All Rights Reserved
//
//
//	All other files are Copyright(c) Id Software, Inc.
//
//	Please see liscense.txt in the source directory for the copyright
//	information regarding those files belonging to Id Software, Inc.
//	
//	Should you decide to release a modified version of ACE, you MUST
//	include the following text (minus the BEGIN and END lines) in the 
//	documentation for your modification.
//
//	--- BEGIN ---
//
//	The ACE Bot is a product of Steve Yeager, and is available from
//	the ACE Bot homepage, at http://www.axionfx.com/ace.
//
//	This program is a modification of the ACE Bot, and is therefore
//	in NO WAY supported by Steve Yeager.

//	This program MUST NOT be sold in ANY form. If you have paid for 
//	this product, you should contact Steve Yeager immediately, via
//	the ACE Bot homepage.
//
//	--- END ---
//
//	I, Steve Yeager, hold no responsibility for any harm caused by the
//	use of this source code, especially to small children and animals.
//  It is provided as-is with no implied warranty or support.
//
//  I also wish to thank and acknowledge the great work of others
//  that has helped me to develop this code.
//
//  John Cricket    - For ideas and swapping code.
//  Ryan Feltrin    - For ideas and swapping code.
//  SABIN           - For showing how to do true client based movement.
//  BotEpidemic     - For keeping us up to date.
//  Telefragged.com - For giving ACE a home.
//  Microsoft       - For giving us such a wonderful crash free OS.
//  id              - Need I say more.
//  
//  And to all the other testers, pathers, and players and people
//  who I can't remember who the heck they were, but helped out.
//
///////////////////////////////////////////////////////////////////////
	
///////////////////////////////////////////////////////////////////////
//
//  acebot_ai.c -      This file contains all of the 
//                     AI routines for the ACE II bot.
//
//
// NOTE: I went back and pulled out most of the brains from
//       a number of these functions. They can be expanded on 
//       to provide a "higher" level of AI. 
////////////////////////////////////////////////////////////////////////

#include "../g_local.h"
#include "../m_player.h"

#include "acebot.h"

///////////////////////////////////////////////////////////////////////
// Main Think function for bot
///////////////////////////////////////////////////////////////////////
void ACEAI_Think (edict_t *self)
{
	usercmd_t	ucmd;

	if(!game.num_bots)
		return; //no bots, no need to go here

	// Set up client movement
	VectorCopy(self->client->ps.viewangles,self->s.angles);
	VectorSet (self->client->ps.pmove.delta_angles, 0, 0, 0);
	memset (&ucmd, 0, sizeof (ucmd));
	self->enemy = NULL;
	self->movetarget = NULL;
	
	// Force respawn 
	if (self->deadflag)
	{
		self->client->buttons = 0;
		ucmd.buttons = BUTTON_ATTACK;
	}
	
	if(self->state == STATE_WANDER && self->wander_timeout < level.time)
	  ACEAI_PickLongRangeGoal(self); // pick a new long range goal

	// Kill the bot if completely stuck somewhere 
	
	if(VectorLength(self->velocity) > 37) 
		self->suicide_timeout = level.time + 10.0;

	if(self->suicide_timeout < level.time && self->takedamage == DAMAGE_AIM && !level.intermissiontime)
	{
		self->health = 0;
		player_die (self, self, self, 100000, vec3_origin);
	}

	//reset the state from pauses for taunting
	if(self->suicide_timeout < level.time + 8)
		self->state = STATE_WANDER;

	//times up on spawn protection 
	if(level.time > self->client->spawnprotecttime + g_spawnprotect->integer)
		self->client->spawnprotected = false;
	
	// Find any short range goal - but not if in air(ie, jumping a jumppad)
	if(self->groundentity)
		ACEAI_PickShortRangeGoal(self);
	
	// Look for enemies
	if(ACEAI_FindEnemy(self))
	{	
		ACEAI_ChooseWeapon(self);
		ACEMV_Attack (self, &ucmd);
	}
	else
	{
		// Execute the move, or wander
		if(self->state == STATE_WANDER)
			ACEMV_Wander(self,&ucmd);
		else if(self->state == STATE_MOVE)
			ACEMV_Move(self,&ucmd);
	}
	
	//debug_printf("State: %d\n",self->state);

	// set approximate ping
	ucmd.msec = 75 + floor (random () * 25) + 1;

	self->client->ping = 0; //show in scoreboard ping of 0

	// set bot's view angle
	ucmd.angles[PITCH] = ANGLE2SHORT(self->s.angles[PITCH]);
	ucmd.angles[YAW] = ANGLE2SHORT(self->s.angles[YAW]);
	ucmd.angles[ROLL] = ANGLE2SHORT(self->s.angles[ROLL]);
	
	// send command through id's code
	ClientThink (self, &ucmd);
	
	self->nextthink = level.time + FRAMETIME;
}

///////////////////////////////////////////////////////////////////////
// Evaluate the best long range goal and send the bot on
// its way. This is a good time waster, so use it sparingly. 
// Do not call it for every think cycle.
///////////////////////////////////////////////////////////////////////
void ACEAI_PickLongRangeGoal(edict_t *self)
{

	int i;
	int node;
	float weight,best_weight=0.0;
	int current_node,goal_node;
	edict_t *goal_ent, *ent;
	float cost;
	
	// look for a target 
	current_node = ACEND_FindClosestReachableNode(self,NODE_DENSITY,NODE_ALL);

	self->current_node = current_node;
	
	if(current_node == -1)
	{
		self->state = STATE_WANDER;
		self->wander_timeout = level.time + 1.0;
		self->goal_node = -1;
		return;
	}

	///////////////////////////////////////////////////////
	// Items
	///////////////////////////////////////////////////////
	for(i=0;i<num_items;i++)
	{
		if(item_table[i].ent == NULL || item_table[i].ent->solid == SOLID_NOT) // ignore items that are not there.
			continue;
		
		cost = ACEND_FindCost(current_node,item_table[i].node);
		
		if(cost == INVALID || cost < 2) // ignore invalid and very short hops
			continue;
	
		weight = ACEIT_ItemNeed(self, item_table[i].item);

		weight *= random(); // Allow random variations
		weight /= cost; // Check against cost of getting there
				
		if(weight > best_weight)
		{
			best_weight = weight;
			goal_node = item_table[i].node;
			goal_ent = item_table[i].ent;
		}
	}

	///////////////////////////////////////////////////////
	// Players
	///////////////////////////////////////////////////////
	// This should be its own function and is for now just
	// finds a player to set as the goal.
	for(i=0;i<game.maxclients;i++)
	{
		ent = g_edicts + i + 1;
		if(ent == self || !ent->inuse || (ent->client->invis_framenum > level.framenum))
			continue;

		node = ACEND_FindClosestReachableNode(ent,NODE_DENSITY,NODE_ALL);
		cost = ACEND_FindCost(current_node, node);

		if(cost == INVALID || cost < 3) // ignore invalid and very short hops
			continue;

		weight = 0.3; 
		
		weight *= random(); // Allow random variations
		weight /= cost; // Check against cost of getting there
		
		//check for flag, and if enemy has the flag, up the weight.
		if(weight > best_weight)
		{		
			best_weight = weight;
			goal_node = node;
			goal_ent = ent;
		}	
	}

	// If do not find a goal, go wandering....
	if(best_weight == 0.0 || goal_node == INVALID)
	{
		self->goal_node = INVALID;
		self->state = STATE_WANDER;
		self->wander_timeout = level.time + 1.0;
		if(debug_mode)
			debug_printf("%s did not find a LR goal, wandering.\n",self->client->pers.netname);
		return; // no path? 
	}
	
	// OK, everything valid, let's start moving to our goal.
	self->state = STATE_MOVE;
	self->tries = 0; // Reset the count of how many times we tried this goal
	 
	if(goal_ent != NULL && debug_mode)
		debug_printf("%s selected a %s at node %d for LR goal.\n",self->client->pers.netname, goal_ent->classname, goal_node);

	ACEND_SetGoal(self,goal_node);

}

///////////////////////////////////////////////////////////////////////
// Pick best goal based on importance and range. This function
// overrides the long range goal selection for items that
// are very close to the bot and are reachable.
///////////////////////////////////////////////////////////////////////

//use this so that bots aren't trying to get to an enemy or item that is behind grating or glass.
qboolean ACEIT_IsVisibleSolid(edict_t *self, edict_t *other)
{
	trace_t tr;

	if(other->client) {
		if(other->client->invis_framenum > level.framenum)
			return false;
	}
	
	tr = gi.trace (self->s.origin, vec3_origin, vec3_origin, other->s.origin, self, MASK_SOLID);
		
	// Blocked, do not shoot
	if (tr.fraction != 1.0)
		return false; 
	
	return true;

}

void ACEAI_PickShortRangeGoal(edict_t *self)
{
	edict_t *target;
	float weight,best_weight=0.0;
	edict_t *best;
	int index;
	
	// look for a target (should make more efficent later)
	target = findradius(NULL, self->s.origin, 200);//was 200
	
	while(target)
	{
		if(target->classname == NULL)
			return;
		
		// Missle avoidance code
		// Set our movetarget to be the rocket or grenade fired at us. 
		if(strcmp(target->classname,"rocket")==0 || strcmp(target->classname,"grenade") ==0)
		{
			if(debug_mode) 
				debug_printf("ROCKET ALERT!\n");

			self->movetarget = target;
			return;
		}
	    if (strcmp(target->classname, "player") == 0) //so players can't sneak RIGHT up on a bot
		{
			if(!target->deadflag && !self->in_deathball && !OnSameTeam(self, target) && !(target->client->invis_framenum > level.framenum))
			{
				self->movetarget = target;
			}
		}
		if (ACEIT_IsReachable(self,target->s.origin))
		{
			if (infront(self, target) && ACEIT_IsVisibleSolid(self, target))
			{
				index = ACEIT_ClassnameToIndex(target->classname);
				weight = ACEIT_ItemNeed(self, index);
				
				if(weight > best_weight)
				{
					best_weight = weight;
					best = target;
				}
			}
		}

		// next target
		target = findradius(target, self->s.origin, 200); //was 200
	}

	if(best_weight)
	{
		self->movetarget = best;
		
		if(debug_mode && self->goalentity != self->movetarget)
			debug_printf("%s selected a %s for SR goal.\n",self->client->pers.netname, self->movetarget->classname);
		
		self->goalentity = best;

	}

}

///////////////////////////////////////////////////////////////////////
// Scan for enemy 
///////////////////////////////////////////////////////////////////////

qboolean ACEAI_infront (edict_t *self, edict_t *other)
{
	vec3_t	vec;
	float	dot;
	vec3_t	forward;
	gitem_t *vehicle;
	
	vehicle = FindItemByClassname("item_bomber");

	if (self->client->pers.inventory[ITEM_INDEX(vehicle)]) {
		return true;	//do this so that they aren't getting lost and just flying off
	}
	vehicle = FindItemByClassname("item_strafer");

	if (self->client->pers.inventory[ITEM_INDEX(vehicle)]) {
		return true;	//do this so that they aren't getting lost and just flying off
	}

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorSubtract (other->s.origin, self->s.origin, vec);
	VectorNormalize (vec);
	dot = DotProduct (vec, forward);

	if (dot > (1.0 - self->awareness))
		return true;
	return false;
}

qboolean ACEAI_FindEnemy(edict_t *self)
{
	int i;
	vec3_t dist;
	edict_t		*bestenemy = NULL;
	float		bestweight = 99999;
	float		weight;
	gitem_t *flag1_item, *flag2_item;
	edict_t *target;
	edict_t	*ent;

	if(ctf->value) {
		flag1_item = FindItemByClassname("item_flag_red");
		flag2_item = FindItemByClassname("item_flag_blue");
	}

	if(self->in_deathball && (self->health > 25)) { //cannot, or should not, fire at players when in a deathball
		//look for goal - if health is too low, drop the ball and fight back
		target = findradius(NULL, self->s.origin, 200);
		self->enemy = NULL;
		while(target)
		{
			if(target->classname == NULL) {
				self->enemy = NULL;
				return false;
			}
			if(self->dmteam == RED_TEAM && (strcmp(target->classname, "item_blue_dbtarget") == 0))
				self->enemy = target;
			else if(self->dmteam == BLUE_TEAM && (strcmp(target->classname, "item_red_dbtarget") == 0))
				self->enemy = target;
			else if(self->dmteam == NO_TEAM && (strcmp(target->classname, "item_dbtarget") == 0))
				self->enemy = target;
			target = findradius(target, self->s.origin, 200); 
		}
		if(self->enemy) {
			//safe_bprintf(PRINT_MEDIUM, "Target Aquired!\n");
			self->movetarget = self->enemy;
			self->goalentity= self->enemy; //face it, and fire
			return true;
		}
		else
			return false;
	}
	//only look for these if your team's spider is vulnerable
	if(tca->value && ((self->dmteam == RED_TEAM && red_team_score < 2) || (self->dmteam == BLUE_TEAM && blue_team_score < 2))) {
		target = findradius(NULL, self->s.origin, 300);
		self->enemy = NULL;
		while(target)
		{
			if(target->classname == NULL) {
				self->enemy = NULL;
				return false;
			}
			if(self->dmteam == RED_TEAM) {
				if(strcmp(target->classname, "misc_bluespidernode") == 0)
					self->enemy = target;
			}
			if(self->dmteam == BLUE_TEAM) {
				if(strcmp(target->classname, "misc_redspidernode") == 0)
					self->enemy = target;
			}
			target = findradius(target, self->s.origin, 300);
			if(self->enemy) {
				//safe_bprintf(PRINT_MEDIUM, "Target Aquired!\n");
				self->movetarget = self->enemy;
				self->goalentity= self->enemy; //face it, and fire
				return true;
			}
			else
				return false;
		}
	}
	
	if(self->oldenemy != NULL) //(was shot from behind)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = NULL;
		return true;
	}

	for(i=0;i<game.maxclients;i++)
	{
		ent = g_edicts + i + 1;
		if(ent == NULL || ent == self || !ent->inuse ||
		   ent->solid == SOLID_NOT) 
		   continue;
	
		if(!ent->deadflag && ACEAI_infront(self, ent) && ACEIT_IsVisibleSolid(self, ent) && gi.inPVS (self->s.origin, ent->s.origin)
			&& (!OnSameTeam(self, ent)))
		{
			VectorSubtract(self->s.origin, ent->s.origin, dist);
			weight = VectorLength( dist );
			
			// Check if best target, or better than current target
			if (weight < bestweight)
			{
				bestweight = weight;
				bestenemy = ent;
			}
			
		}
	}
	if(bestenemy) {

		self->enemy = bestenemy;
		//if using a blaster, and it's far away, don't fire, it's pointless
		if((self->client->pers.weapon == FindItem("blaster")) && (bestweight > 1500)) {
			self->enemy = NULL;
			return false;
		}
		//if carrying a flag, and not close to an enemy, continue running to goal
		if(ctf->value) {
			if (((self->client->pers.inventory[ITEM_INDEX(flag1_item)]) || 
				(self->client->pers.inventory[ITEM_INDEX(flag2_item)])) && bestweight > 300) {
				self->enemy = NULL;
				return false;
			}
		}
		return true;
	}
	return false;
  
}

///////////////////////////////////////////////////////////////////////
// Hold fire with RL/BFG?
///////////////////////////////////////////////////////////////////////
qboolean ACEAI_CheckShot(edict_t *self)
{
	trace_t tr;

	tr = gi.trace (self->s.origin, tv(-8,-8,-8), tv(8,8,8), self->enemy->s.origin, self, MASK_SOLID);
	
	// Blocked, do not shoot
	if (tr.fraction != 1.0)
		return false; 
	
	return true;
}


void ACEAI_Use_Invisibility (edict_t *ent)
{
	gitem_t *it;

	it = FindItem("Invisibility");
	ent->client->pers.inventory[ITEM_INDEX(it)]--;
	ValidateSelectedItem (ent);

	it = FindItem("Sproing");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	it = FindItem("Haste");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	ent->client->resp.reward_pts = 0;
	ent->client->resp.powered = false;

	if (ent->client->invis_framenum > level.framenum)
		ent->client->invis_framenum += 300;
	else
		ent->client->invis_framenum = level.framenum + 300;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/powerup.wav"), 1, ATTN_NORM, 0);
}

void ACEAI_Use_Haste (edict_t *ent)
{
	gitem_t *it;

	it = FindItem("Haste");
	ent->client->pers.inventory[ITEM_INDEX(it)]--;
	ValidateSelectedItem (ent);

	it = FindItem("Sproing");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	it = FindItem("Invisibility");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	ent->client->resp.reward_pts = 0;
	ent->client->resp.powered = false;

	if (ent->client->haste_framenum > level.framenum)
		ent->client->haste_framenum += 300;
	else
		ent->client->haste_framenum = level.framenum + 300;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/powerup.wav"), 1, ATTN_NORM, 0);
}

void ACEAI_Use_Sproing (edict_t *ent)
{
	gitem_t *it;

	it = FindItem("Sproing");
	ent->client->pers.inventory[ITEM_INDEX(it)]--;
	ValidateSelectedItem (ent);

	it = FindItem("Haste");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	it = FindItem("Invisibility");
	ent->client->pers.inventory[ITEM_INDEX(it)] = 0;

	ent->client->resp.reward_pts = 0;
	ent->client->resp.powered = false;

	if (ent->client->sproing_framenum > level.framenum)
		ent->client->sproing_framenum += 300;
	else
		ent->client->sproing_framenum = level.framenum + 300;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/powerup.wav"), 1, ATTN_NORM, 0);
}

///////////////////////////////////////////////////////////////////////
// Choose the best weapon for bot 
///////////////////////////////////////////////////////////////////////
void ACEAI_ChooseWeapon(edict_t *self)
{	
	float range;
	vec3_t v;
	float c;
	
	if (self->in_vehicle) {
		return; 
	}
	
	if (self->in_deathball) {
		return; //cannot switch or fire weapons when in a deathball.
	}

	if(self->client->resp.powered) { //got enough reward points, use something
		c = random();
		if( c < 0.5)
			ACEAI_Use_Invisibility(self);
		else if (c < 0.7)
			ACEAI_Use_Haste(self);
		else
			ACEAI_Use_Sproing(self);
	}

	//mutators
	if(instagib->value || rocket_arena->value)
		return;

	// if no enemy, then what are we doing here?
	if(!self->enemy)
		return;

	// Base selection on distance.
	VectorSubtract (self->s.origin, self->enemy->s.origin, v);
	range = VectorLength(v);

	//what is the bot's favorite weapon? The bot will always check for it's favorite
	//weapon first, which is set in the bot's config file. 
	
	if(self->skill > 1) { //don't allow lower skill bots to use these weapons
		if(!strcmp(self->faveweap, "Alien Vaporizer"))
			
		{
			if(ACEIT_ChangeWeapon(self,FindItem(self->faveweap)))
			{
				self->accuracy = self->weapacc[9];
				return;
			}
		}
		if(!strcmp(self->faveweap, "Alien Disruptor"))
		{
			if(ACEIT_ChangeWeapon(self,FindItem(self->faveweap)))
			{
				self->accuracy = self->weapacc[2];
				return;
			}
		}
	}
	if(!strcmp(self->faveweap, "Disruptor")) 
	{
		if(ACEIT_ChangeWeapon(self,FindItem(self->faveweap)))
		{
			self->accuracy = self->weapacc[8];
			return;
		}
	}
    if(!strcmp(self->faveweap, "Pulse Rifle"))
	{
		if(ACEIT_ChangeWeapon(self,FindItem(self->faveweap)))
		{
			self->accuracy = self->weapacc[3];
			return;
		}
	}
	if(!strcmp(self->faveweap, "Alien Smartgun"))
	{
		if(ACEAI_CheckShot(self) && ACEIT_ChangeWeapon(self, FindItem("Alien Smartgun")))
		{
			self->accuracy = self->weapacc[7];
			return;
		}	
	}
	if(!strcmp(self->faveweap, "Rocket Launcher")) 
	{
		if(range > 200)
		{
			if(ACEAI_CheckShot(self) && ACEIT_ChangeWeapon(self,FindItem("Rocket Launcher")))
			{
				self->accuracy = self->weapacc[6];
				return;
			}
		}
	}
	if(!strcmp(self->faveweap, "Flame Thrower"))
	{
		if(range < 500 || (range < 800 && self->skill == 3)) {
				if(ACEIT_ChangeWeapon(self,FindItem("Flame Thrower")))
				{
					self->accuracy = self->weapacc[4];
					return;
				}
		}
	}
	if(!strcmp(self->faveweap, "Violator"))
	{
		if(range < 300) { //because it's a fav weap, we want them to really try and use it
				if(ACEIT_ChangeWeapon(self,FindItem("Violator")))
				{
					self->accuracy = 1.0;
					return; 
				}
		}
	}
	//now go through normal weapon favoring routine
	// always favor the Vaporizor, unless close, then use the violator
	if(range < 200) {
		if(ACEIT_ChangeWeapon(self,FindItem("Violator")))
		{
			self->accuracy = 1.0;
			return;
		}
	}

	if(self->skill > 1) {
		if(ACEIT_ChangeWeapon(self,FindItem("Alien Vaporizer")))
		{
			self->accuracy = self->weapacc[9];
			return;
		}
	}
	
	if(ACEAI_CheckShot(self) && ACEIT_ChangeWeapon(self, FindItem("Alien Smartgun")))
	{
		self->accuracy = self->weapacc[7];
		return;
	}	
	
	// Longer range so the bot doesn't blow himself up!
	if(range > 200)
	{
		if(ACEAI_CheckShot(self) && ACEIT_ChangeWeapon(self,FindItem("Rocket Launcher")))
		{
			self->accuracy = self->weapacc[6];
			return;
		}
	}
	
	// Only use FT in certain ranges
	if(range < 500 || (range < 800 && self->skill == 3)) {
	
			if(ACEIT_ChangeWeapon(self,FindItem("Flame Thrower")))
			{
				self->accuracy = self->weapacc[4];
				return;
			}
	}

	if(ACEIT_ChangeWeapon(self,FindItem("Disruptor")))
	{
		self->accuracy = self->weapacc[8];
		return;
	}
	
	if(ACEIT_ChangeWeapon(self,FindItem("Pulse Rifle")))
	{
		self->accuracy = self->weapacc[3];
		return;
	}
	
	if(self->skill > 1) {
		if(ACEIT_ChangeWeapon(self,FindItem("Alien Disruptor")))
		{
			self->accuracy = self->weapacc[2];
			return;
		}	
	}

	if(ACEIT_ChangeWeapon(self,FindItem("Blaster")))
   	{
		self->accuracy = self->weapacc[1];
		return;
	}
	
	return;

}
