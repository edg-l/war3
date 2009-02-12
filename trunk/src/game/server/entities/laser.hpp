/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */

#ifndef GAME_SERVER_ENTITY_LASER_H
#define GAME_SERVER_ENTITY_LASER_H

#include <game/server/entity.hpp>

class CHARACTER;

class LASER : public ENTITY
{
	vec2 from;
	vec2 dir;
	int dmg;
	float energy;
	int bounces;
	int eval_tick;
	int owner;
	int second_owner;
	
	bool hit_character(vec2 from, vec2 to);
	void do_bounce();
	
public:
	
	LASER(vec2 pos, vec2 direction, float start_energy, int owner, int dmg=tuning.laser_damage,int second_owner=-1);
	
	virtual void reset();
	virtual void tick();
	virtual void snap(int snapping_client);
};

#endif
