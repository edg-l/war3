#include <new>
#include <engine/e_server_interface.h>
#include <engine/e_config.h>
#include <game/server/gamecontext.hpp>
#include <game/mapitems.hpp>

#include "character.hpp"
#include "laser.hpp"
#include "projectile.hpp"

struct INPUT_COUNT
{
	int presses;
	int releases;
};

static INPUT_COUNT count_input(int prev, int cur)
{
	INPUT_COUNT c = {0,0};
	prev &= INPUT_STATE_MASK;
	cur &= INPUT_STATE_MASK;
	int i = prev;
	while(i != cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.presses++;
		else
			c.releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CHARACTER, MAX_CLIENTS)

// player
CHARACTER::CHARACTER()
: ENTITY(NETOBJTYPE_CHARACTER)
{
	proximity_radius = phys_size;
}

void CHARACTER::reset()
{
	destroy();
}

bool CHARACTER::spawn(PLAYER *player, vec2 pos, int team)
{
	player_state = PLAYERSTATE_UNKNOWN;
	emote_stop = -1;
	last_action = -1;
	active_weapon = WEAPON_GUN;
	last_weapon = WEAPON_HAMMER;
	queued_weapon = -1;
	
	//clear();
	this->player = player;
	this->pos = pos;
	this->team = team;
	
	core.reset();
	core.world = &game.world.core;
	core.pos = pos;
	game.world.core.characters[player->client_id] = &core;

	reckoning_tick = 0;
	mem_zero(&sendcore, sizeof(sendcore));
	mem_zero(&reckoningcore, sizeof(reckoningcore));
	
	game.world.insert_entity(this);
	alive = true;
	player->force_balanced = false;

	game.controller->on_character_spawn(this);

	//Human respawn with armor.
	if((game.controller)->is_rpg())
		armor=player->human_armor*2;

	return true;
}

void CHARACTER::destroy()
{
	game.world.core.characters[player->client_id] = 0;
	//Reset players power (not sure it should be here)
	if((game.controller)->is_rpg())
		player->init_rpg();
	alive = false;
}

void CHARACTER::set_weapon(int w)
{
	if(w == active_weapon)
		return;
		
	last_weapon = active_weapon;
	queued_weapon = -1;
	active_weapon = w;
	if(active_weapon < 0 || active_weapon >= NUM_WEAPONS)
		active_weapon = 0;
	
	game.create_sound(pos, SOUND_WEAPON_SWITCH);
}

bool CHARACTER::is_grounded()
{
	if(col_check_point((int)(pos.x+phys_size/2), (int)(pos.y+phys_size/2+5)))
		return true;
	if(col_check_point((int)(pos.x-phys_size/2), (int)(pos.y+phys_size/2+5)))
		return true;
	return false;
}


int CHARACTER::handle_ninja()
{
	if(active_weapon != WEAPON_NINJA)
		return 0;
	
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));

	if ((server_tick() - ninja.activationtick) > (data->weapons.ninja.duration * server_tickspeed() / 1000))
	{
		// time's up, return
		weapons[WEAPON_NINJA].got = false;
		active_weapon = last_weapon;
		if(active_weapon == WEAPON_NINJA)
			active_weapon = WEAPON_GUN;
		set_weapon(active_weapon);
		return 0;
	}
	
	// force ninja weapon
	set_weapon(WEAPON_NINJA);

	ninja.currentmovetime--;

	if (ninja.currentmovetime == 0)
	{
		// reset player velocity
		core.vel *= 0.2f;
		//return MODIFIER_RETURNFLAGS_OVERRIDEWEAPON;
	}

	if (ninja.currentmovetime > 0)
	{
		// Set player velocity
		core.vel = ninja.activationdir * data->weapons.ninja.velocity;
		vec2 oldpos = pos;
		move_box(&core.pos, &core.vel, vec2(phys_size, phys_size), 0.0f);
		// reset velocity so the client doesn't predict stuff
		core.vel = vec2(0.0f,0.0f);
		if ((ninja.currentmovetime % 2) == 0)
		{
			//create_smoke(pos);
		}

		// check if we hit anything along the way
		{
			CHARACTER *ents[64];
			vec2 dir = pos - oldpos;
			float radius = phys_size * 2.0f; //length(dir * 0.5f);
			vec2 center = oldpos + dir * 0.5f;
			int num = game.world.find_entities(center, radius, (ENTITY**)ents, 64, NETOBJTYPE_CHARACTER);

			for (int i = 0; i < num; i++)
			{
				// Check if entity is a player
				if (ents[i] == this)
					continue;
				// make sure we haven't hit this object before
				bool balreadyhit = false;
				for (int j = 0; j < numobjectshit; j++)
				{
					if (hitobjects[j] == ents[i])
						balreadyhit = true;
				}
				if (balreadyhit)
					continue;

				// check so we are sufficiently close
				if (distance(ents[i]->pos, pos) > (phys_size * 2.0f))
					continue;

				// hit a player, give him damage and stuffs...
				game.create_sound(ents[i]->pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(numobjectshit < 10)
					hitobjects[numobjectshit++] = ents[i];
					
				ents[i]->take_damage(vec2(0,10.0f), data->weapons.ninja.base->damage, player->client_id,WEAPON_NINJA);
			}
		}
		return 0;
	}

	return 0;
}


void CHARACTER::do_weaponswitch()
{
	if(reload_timer != 0) // make sure we have reloaded
		return;
		
	if(queued_weapon == -1) // check for a queued weapon
		return;

	if(weapons[WEAPON_NINJA].got) // if we have ninja, no weapon selection is possible
		return;

	// switch weapon
	set_weapon(queued_weapon);
}

void CHARACTER::handle_weaponswitch()
{
	int wanted_weapon = active_weapon;
	if(queued_weapon != -1)
		wanted_weapon = queued_weapon;
	
	// select weapon
	int next = count_input(latest_previnput.next_weapon, latest_input.next_weapon).presses;
	int prev = count_input(latest_previnput.prev_weapon, latest_input.prev_weapon).presses;

	if(next < 128) // make sure we only try sane stuff
	{
		while(next) // next weapon selection
		{
			wanted_weapon = (wanted_weapon+1)%NUM_WEAPONS;
			if(weapons[wanted_weapon].got)
				next--;
		}
	}

	if(prev < 128) // make sure we only try sane stuff
	{
		while(prev) // prev weapon selection
		{
			wanted_weapon = (wanted_weapon-1)<0?NUM_WEAPONS-1:wanted_weapon-1;
			if(weapons[wanted_weapon].got)
				prev--;
		}
	}

	// direct weapon selection
	if(latest_input.wanted_weapon)
		wanted_weapon = input.wanted_weapon-1;

	// check for insane values
	if(wanted_weapon >= 0 && wanted_weapon < NUM_WEAPONS && wanted_weapon != active_weapon && weapons[wanted_weapon].got)
		queued_weapon = wanted_weapon;
	
	do_weaponswitch();
}

void CHARACTER::fire_weapon()
{
	if(reload_timer != 0)
		return;
		
	do_weaponswitch();
	
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));
	
	bool fullauto = false;
	if(active_weapon == WEAPON_GRENADE || active_weapon == WEAPON_SHOTGUN || active_weapon == WEAPON_RIFLE)
		fullauto = true;

	//Orc at reload lvl 3 can fullauto with gun
	if(active_weapon == WEAPON_GUN && player->orc_reload == 3)
		fullauto = true;


	// check if we gonna fire
	bool will_fire = false;
	if(count_input(latest_previnput.fire, latest_input.fire).presses) will_fire = true;
	if(fullauto && (latest_input.fire&1) && weapons[active_weapon].ammo) will_fire = true;
	if(!will_fire)
	{
		if(player && player->race_name == TAUREN && player->started_heal != -1)
		{
			char buf[128];
			if(game.players[player->started_heal] && game.players[player->started_heal]->get_character())
			{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				game.players[player->started_heal]->healed=false;
				player->started_heal=-1;
			}					
			else if(game.players[player->started_heal])
				{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				game.players[player->started_heal]->healed=false;
				player->started_heal=-1;
			}
			else
			{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				player->started_heal=-1;
			}	
		}
		return;
	}
		
	// check for ammo
	if(!weapons[active_weapon].ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		reload_timer = 125 * server_tickspeed() / 1000;;
		game.create_sound(pos, SOUND_WEAPON_NOAMMO);
		if(player && player->race_name == TAUREN && player->started_heal != -1)
		{
			char buf[128];
			if(game.players[player->started_heal] && game.players[player->started_heal]->get_character())
			{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				game.players[player->started_heal]->healed=false;
				player->started_heal=-1;
			}					
			else if(game.players[player->started_heal])
				{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				game.players[player->started_heal]->healed=false;
				player->started_heal=-1;
			}
			else
			{
				str_format(buf,sizeof(buf),"Stopped healing ");
				game.send_chat_target(player->client_id,buf);
				player->started_heal=-1;
			}	
		}
		return;
	}
	
	vec2 projectile_startpos = pos+direction*phys_size*0.75f;
	
	switch(active_weapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects hit
			numobjectshit = 0;
			game.create_sound(pos, SOUND_HAMMER_FIRE);
			
			CHARACTER *ents[64];
			int hits = 0;
			int num = game.world.find_entities(pos+direction*phys_size*0.75f, phys_size*0.5f, (ENTITY**)ents, 64, NETOBJTYPE_CHARACTER);

			for (int i = 0; i < num; i++)
			{
				CHARACTER *target = ents[i];
				if (target == this)
					continue;
					
				// hit a player, give him damage and stuffs...
				vec2 fdir = normalize(ents[i]->pos - pos);

				// set his velocity to fast upward (for now)
				game.create_hammerhit(pos);
				ents[i]->take_damage(vec2(0,-1.0f), data->weapons.hammer.base->damage, player->client_id, active_weapon);
				vec2 dir;
				if (length(target->pos - pos) > 0.0f)
					dir = normalize(target->pos - pos);
				else
					dir = vec2(0,-1);
					
				target->core.vel += normalize(dir + vec2(0,-1.1f)) * 10.0f;
				hits++;
			}
			
			// if we hit anything, we have to wait for the reload
			if(hits)
				reload_timer = server_tickspeed()/3;
			
		} break;

		case WEAPON_GUN:
		{
			PROJECTILE *proj = new PROJECTILE(WEAPON_GUN,
				player->client_id,
				projectile_startpos,
				direction,
				(int)(server_tickspeed()*tuning.gun_lifetime),
				1, 0, 0, -1, WEAPON_GUN);
				
			// pack the projectile and send it to the client directly
			NETOBJ_PROJECTILE p;
			proj->fill_info(&p);
			
			msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
			msg_pack_int(1);
			for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
				msg_pack_int(((int *)&p)[i]);
			msg_pack_end();
			server_send_msg(player->client_id);
							
			game.create_sound(pos, SOUND_GUN_FIRE);
		} break;
		
		case WEAPON_SHOTGUN:
		{
			int shotspread = 2;
			//Orc at damage lvl 3 fire more bullet with shotgun
			if(player->orc_dmg>=2 && (game.controller)->is_rpg())shotspread = 3;

			msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
			msg_pack_int(shotspread*2+1);
			
			for(int i = -shotspread; i <= shotspread; i++)
			{
				float spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = get_angle(direction);
				a += spreading[i+2];
				float v = 1-(abs(i)/(float)shotspread);
				float speed = mix((float)tuning.shotgun_speeddiff, 1.0f, v);
				PROJECTILE *proj = new PROJECTILE(WEAPON_SHOTGUN,
					player->client_id,
					projectile_startpos,
					vec2(cosf(a), sinf(a))*speed,
					(int)(server_tickspeed()*tuning.shotgun_lifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);
					
				// pack the projectile and send it to the client directly
				NETOBJ_PROJECTILE p;
				proj->fill_info(&p);
				
				for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
					msg_pack_int(((int *)&p)[i]);
			}

			msg_pack_end();
			server_send_msg(player->client_id);					
			
			game.create_sound(pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			if(player && player->race_name == TAUREN && player->started_heal == -1)
			{
				vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));
				vec2 at;
				CHARACTER *hit;
				char buf[128];
				vec2 to=core.pos+direction*700;
				col_intersect_line(core.pos, to, 0x0, &to);
				hit = game.world.intersect_character(core.pos, to, 0.0f, at, this);
				if(hit && hit->player->team == team)
				{
					hit->player->healed=true;
					hit->player->heal_tick=server_tick();
					player->started_heal=hit->player->client_id;
					hit->player->heal_from=player->client_id;
					str_format(buf,sizeof(buf),"Started healing %s",server_clientname(hit->player->client_id));
					game.send_chat_target(player->client_id,buf);
				}
			}
			else if(player && player->race_name == TAUREN && player->started_heal != -1)
			{
				char buf[128];
				int dist;
				if(game.players[player->started_heal] && game.players[player->started_heal]->get_character())
				{
					dist=distance(pos,game.players[player->started_heal]->get_character()->pos);
					if(dist > 700)
					{
						str_format(buf,sizeof(buf),"Stopped healing ");
						game.send_chat_target(player->client_id,buf);
						game.players[player->started_heal]->healed=false;
						player->started_heal=-1;
					}
					dbg_msg("heal","%d",dist);
				}					
				else if(game.players[player->started_heal])
				{
					str_format(buf,sizeof(buf),"Stopped healing ");
					game.send_chat_target(player->client_id,buf);
					game.players[player->started_heal]->healed=false;
					player->started_heal=-1;
				}
				else
				{
					str_format(buf,sizeof(buf),"Stopped healing ");
					game.send_chat_target(player->client_id,buf);
					player->started_heal=-1;
				}	
			}
			else
			{
				PROJECTILE *proj = new PROJECTILE(WEAPON_GRENADE,
					player->client_id,
					projectile_startpos,
					direction,
					(int)(server_tickspeed()*tuning.grenade_lifetime),
					1, PROJECTILE::PROJECTILE_FLAGS_EXPLODE, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
	
				// pack the projectile and send it to the client directly
				NETOBJ_PROJECTILE p;
				proj->fill_info(&p);
				
				msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
				msg_pack_int(1);
				for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
					msg_pack_int(((int *)&p)[i]);
				msg_pack_end();
				server_send_msg(player->client_id);
	
				game.create_sound(pos, SOUND_GRENADE_FIRE);
			}
		} break;
		
		case WEAPON_RIFLE:
		{
			new LASER(pos, direction, tuning.laser_reach, player->client_id);
			game.create_sound(pos, SOUND_RIFLE_FIRE);
		} break;
		
		case WEAPON_NINJA:
		{
			attack_tick = server_tick();
			ninja.activationdir = direction;
			ninja.currentmovetime = data->weapons.ninja.movetime * server_tickspeed() / 1000;

			//reload_timer = data->weapons.ninja.base->firedelay * server_tickspeed() / 1000 + server_tick();
			
			// reset hit objects
			numobjectshit = 0;

			game.create_sound(pos, SOUND_NINJA_FIRE);
			
		} break;
		
	}

	if(weapons[active_weapon].ammo > 0 && player->race_name != TAUREN || player->race_name == TAUREN && active_weapon!=WEAPON_GRENADE && weapons[active_weapon].ammo > 0) // -1 == unlimited
		weapons[active_weapon].ammo--;
	attack_tick = server_tick();

	//Orc reload stuff
 	if((game.controller)->is_rpg() && player->orc_reload != 0 && !reload_timer)
	{
		switch(player->orc_reload)
		{
			case 1:
		 		reload_timer=(data->weapons.id[active_weapon].firedelay * server_tickspeed() )/ 1250;
				break;
			case 2:
		 		reload_timer=(data->weapons.id[active_weapon].firedelay * server_tickspeed() )/ 1500;
				break;
			case 3:
		 		reload_timer=(data->weapons.id[active_weapon].firedelay * server_tickspeed() )/ 1750;
				break;
		}
	}
	if(!reload_timer && player->race_name != TAUREN || player->race_name == TAUREN && active_weapon != WEAPON_GRENADE)
		reload_timer = data->weapons.id[active_weapon].firedelay * server_tickspeed() / 1000;
	else if(!reload_timer && player->race_name == TAUREN && active_weapon == WEAPON_GRENADE)
		reload_timer = data->weapons.id[active_weapon].firedelay * server_tickspeed() / 5000;
}

int CHARACTER::handle_weapons()
{
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));

	/*
	if(config.dbg_stress)
	{
		for(int i = 0; i < NUM_WEAPONS; i++)
		{
			weapons[i].got = true;
			weapons[i].ammo = 10;
		}

		if(reload_timer) // twice as fast reload
			reload_timer--;
	} */

	//if(active_weapon == WEAPON_NINJA)
	handle_ninja();


	// check reload timer
	if(reload_timer)
	{
		reload_timer--;
		return 0;
	}
	
	/*
	if (active_weapon == WEAPON_NINJA)
	{
		// don't update other weapons while ninja is active
		return handle_ninja();
	}*/

	// fire weapon, if wanted
	fire_weapon();

	// ammo regen
	int ammoregentime = data->weapons.id[active_weapon].ammoregentime;
	if(ammoregentime)
	{
		// If equipped and not active, regen ammo?
		if (reload_timer <= 0)
		{
			if (weapons[active_weapon].ammoregenstart < 0)
				weapons[active_weapon].ammoregenstart = server_tick();

			if ((server_tick() - weapons[active_weapon].ammoregenstart) >= ammoregentime * server_tickspeed() / 1000)
			{
				// Add some ammo
				weapons[active_weapon].ammo = min(weapons[active_weapon].ammo + 1, 10);
				weapons[active_weapon].ammoregenstart = -1;
			}
		}
		else
		{
			weapons[active_weapon].ammoregenstart = -1;
		}
	}
	
	return 0;
}

void CHARACTER::on_predicted_input(NETOBJ_PLAYER_INPUT *new_input)
{
	// check for changes
	if(mem_comp(&input, new_input, sizeof(NETOBJ_PLAYER_INPUT)) != 0)
		last_action = server_tick();
		
	// copy new input
	mem_copy(&input, new_input, sizeof(input));
	num_inputs++;
	
	// or are not allowed to aim in the center
	if(input.target_x == 0 && input.target_y == 0)
		input.target_y = -1;	
}

void CHARACTER::on_direct_input(NETOBJ_PLAYER_INPUT *new_input)
{
	mem_copy(&latest_previnput, &latest_input, sizeof(latest_input));
	mem_copy(&latest_input, new_input, sizeof(latest_input));
	
	if(num_inputs > 2 && team != -1)
	{
		handle_weaponswitch();
		fire_weapon();
	}
	
	mem_copy(&latest_previnput, &latest_input, sizeof(latest_input));
}

void CHARACTER::tick()
{
	if(player->force_balanced)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "You were moved to %s due to team balancing", game.controller->get_team_name(team));
		game.send_broadcast(buf, player->client_id);
		
		player->force_balanced = false;
	}

	//player_core core;
	//core.pos = pos;
	//core.jumped = jumped;
	core.input = input;
	core.tick(true);
	
	float phys_size = 28.0f;
	// handle death-tiles
	if(col_get((int)(pos.x+phys_size/2), (int)(pos.y-phys_size/2))&COLFLAG_DEATH ||
			col_get((int)(pos.x+phys_size/2), (int)(pos.y+phys_size/2))&COLFLAG_DEATH ||
			col_get((int)(pos.x-phys_size/2), (int)(pos.y-phys_size/2))&COLFLAG_DEATH ||
			col_get((int)(pos.x-phys_size/2), (int)(pos.y+phys_size/2))&COLFLAG_DEATH)
	{
		die(player->client_id, WEAPON_WORLD);
	}

	// handle weapons
	handle_weapons();

	player_state = input.player_state;

	//Poison Hot tick ect
	if((game.controller)->is_rpg())
	{
		if(player->poisoned && server_tick()-player->poison_start_tick > server_tickspeed() && game.players[player->poisoner])
		{
			player->poison_start_tick=server_tick();
			take_damage(vec2 (0,0),player->poisoned,game.players[player->poisoner]->client_id,WEAPON_POISON);
			player->start_poison--;
		}
		if(player->start_poison <=0)
			player->poisoned=0;

		if(player->hot && server_tick()-player->hot_start_tick > server_tickspeed() && game.players[player->hot_from])
		{
			player->hot_start_tick=server_tick();
			increase_health(1);
			game.players[player->hot_from]->xp++;
			game.create_sound(game.players[player->hot_from]->view_pos, SOUND_PICKUP_HEALTH, cmask_one(player->hot_from));
			player->start_hot--;
		}
		if(player->start_hot <=0)
			player->hot=0;

		//Grenade heal
		if(player->healed && server_tick()-player->heal_tick > server_tickspeed() && game.players[player->heal_from])
		{
			player->heal_tick=server_tick();
			if(health < 10)
			{
				game.create_hammerhit(pos);
			}
			increase_health(1);
			game.create_sound(game.players[player->heal_from]->view_pos, SOUND_PICKUP_HEALTH, cmask_one(player->heal_from));
		}
		//Previous stuff (should be deleted ?)
		//if((game.controller)->is_rpg() && core.vel.x < (20.0f*((float)player->undead_speed/2.5f)) && core.vel.x > (-20.0f*((float)player->undead_speed/2.5f)))core.vel.x*=1.1f;
		//dbg_msg("test","%f %d",core.vel.x,core.vel.x);

		if(stucked && server_tick()-stucked < server_tickspeed()*3)
			core.vel=vec2(0.0f,0.0f);

		if (player->undead_taser && core.hooked_player != -1 && server_tick()-player->undead_taser_tick > server_tickspeed() && game.players[core.hooked_player]->team != player->team)
		{
			game.players[core.hooked_player]->get_character()->take_damage(vec2(0,-1.0f), player->undead_taser, player->client_id, WEAPON_TASER);
			player->undead_taser_tick=server_tick();
		}
	}
	// Previnput
	previnput = input;
	return;
}

void CHARACTER::tick_defered()
{
	// advance the dummy
	{
		WORLD_CORE tempworld;
		reckoningcore.world = &tempworld;
		reckoningcore.tick(false);
		reckoningcore.move();
		reckoningcore.quantize();
	}
	
	//lastsentcore;
	/*if(!dead)
	{*/
		vec2 start_pos = core.pos;
		vec2 start_vel = core.vel;
		bool stuck_before = test_box(core.pos, vec2(28.0f, 28.0f));
		
		core.move();
		bool stuck_after_move = test_box(core.pos, vec2(28.0f, 28.0f));
		core.quantize();
		bool stuck_after_quant = test_box(core.pos, vec2(28.0f, 28.0f));
		pos = core.pos;
		
		if(!stuck_before && (stuck_after_move || stuck_after_quant))
		{
			dbg_msg("player", "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x", 
				stuck_before,
				stuck_after_move,
				stuck_after_quant,
				start_pos.x, start_pos.y,
				start_vel.x, start_vel.y,
				*((unsigned *)&start_pos.x), *((unsigned *)&start_pos.y),
				*((unsigned *)&start_vel.x), *((unsigned *)&start_vel.y));
		}

		int events = core.triggered_events;
		int mask = cmask_all_except_one(player->client_id);
		
		if(events&COREEVENT_GROUND_JUMP) game.create_sound(pos, SOUND_PLAYER_JUMP, mask);
		
		//if(events&COREEVENT_HOOK_LAUNCH) snd_play_random(CHN_WORLD, SOUND_HOOK_LOOP, 1.0f, pos);
		if(events&COREEVENT_HOOK_ATTACH_PLAYER) game.create_sound(pos, SOUND_HOOK_ATTACH_PLAYER, cmask_all());
		if(events&COREEVENT_HOOK_ATTACH_GROUND) game.create_sound(pos, SOUND_HOOK_ATTACH_GROUND, mask);
		if(events&COREEVENT_HOOK_HIT_NOHOOK) game.create_sound(pos, SOUND_HOOK_NOATTACH, mask);
		//if(events&COREEVENT_HOOK_RETRACT) snd_play_random(CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, pos);
	//}
	
	if(team == -1)
	{
		pos.x = input.target_x;
		pos.y = input.target_y;
	}
	
	// update the sendcore if needed
	{
		NETOBJ_CHARACTER predicted;
		NETOBJ_CHARACTER current;
		mem_zero(&predicted, sizeof(predicted));
		mem_zero(&current, sizeof(current));
		reckoningcore.write(&predicted);
		core.write(&current);

		// only allow dead reackoning for a top of 3 seconds
		if(reckoning_tick+server_tickspeed()*3 < server_tick() || mem_comp(&predicted, &current, sizeof(NETOBJ_CHARACTER)) != 0)
		{
			reckoning_tick = server_tick();
			sendcore = core;
			reckoningcore = core;
		}
	}
}

bool CHARACTER::increase_health(int amount)
{
	/*//Not used yet i was planing on buff undead
	if(player && player->race_name == UNDEAD)
	{
		if(health >= 15)
			return false;
		health = clamp(health+amount, 0, 15);
		return true;
	}
	else
	{*/
		if(health >= 10)
			return false;
		health = clamp(health+amount, 0, 10);
		return true;
	//}
}

bool CHARACTER::increase_armor(int amount)
{
	/*//Same as health
	if(player && player->race_name == UNDEAD)
	{
		if(armor >= 0)
			return false;
		armor = clamp(armor+amount, 0, 0);
		return true;
	}
	else
	{*/
		if(armor >= 10)
			return false;
		armor = clamp(armor+amount, 0, 10);
		return true;
	//}
}

void CHARACTER::die(int killer, int weapon)
{
	/*if (dead || team == -1)
		return;*/
	int mode_special = game.controller->on_character_death(this, game.players[killer], weapon);

	dbg_msg("game", "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		killer, server_clientname(killer),
		player->client_id, server_clientname(player->client_id), weapon, mode_special);

	// send the kill message
	NETMSG_SV_KILLMSG msg;
	msg.killer = killer;
	msg.victim = player->client_id;
	msg.weapon = weapon;
	msg.mode_special = mode_special;
	msg.pack(MSGFLAG_VITAL);
	server_send_msg(-1);

	// a nice sound
	game.create_sound(pos, SOUND_PLAYER_DIE);

	// set dead state
	// TODO: do stuff here
	/*
	die_pos = pos;
	dead = true;
	*/
	
	// this is for auto respawn after 3 secs
	player->die_tick = server_tick();

	player->death_pos=pos;
	if(weapon==WEAPON_WORLD)
		player->death_tile=true;
	
	alive = false;
	game.world.remove_entity(this);
	game.world.core.characters[player->client_id] = 0;
	game.create_death(pos, player->client_id);
	
	// we got to wait 0.5 secs before respawning
	player->respawn_tick = server_tick()+server_tickspeed()/2;

	player->poisoned=0;
	player->hot=0;
}

bool CHARACTER::take_damage(vec2 force, int dmg, int from, int weapon)
{
	core.vel += force;

	//Tauren hot
	if(game.controller->is_friendly_fire(player->client_id, from) && game.players[from]->tauren_hot && weapon == WEAPON_GUN)
	{
		player->hot=1;
		player->hot_start_tick=server_tick();
		player->hot_from=from;
		player->start_hot=game.players[from]->tauren_hot*2;
		return true;
	}
	if(game.controller->is_friendly_fire(player->client_id, from) && !config.sv_teamdamage)
		return false;

	//Invicible
	if((game.controller)->is_rpg() && player->invincible)
		return false;

	//Armor reduce and damage increase
	if((game.controller)->is_rpg() && from != player->client_id)
	{
		float dmgincrease=(float)dmg*((float)game.players[from]->orc_dmg*20.0f/100.0f);
		float dmgdecrease=(float)dmg*((float)player->human_armor*20.0f/100.0f);
		dmg=(int)round((float)dmg+dmgincrease-dmgdecrease);
		if(dmg<=0)dmg=1;
		if(config.dbg_war3)dbg_msg("damage","%f %f %d",dmgincrease,dmgdecrease,dmg);
	}

	//Poison | vampire | mirror
	if((game.controller)->is_rpg())
	{
		if(game.players[from]->undead_vamp && from != player->client_id)game.players[from]->vamp(dmg*game.players[from]->undead_vamp);
		if(game.players[from]->elf_poison && from != player->client_id && !player->poisoned && weapon != WEAPON_MIRROR)
		{
			player->poisoned=1;
			player->poison_start_tick=server_tick();
			player->poisoner=from;
			player->start_poison=game.players[from]->elf_poison*2;
		}
		if(from != player->client_id && player->elf_mirror && !game.players[from]->elf_mirror && game.players[from]->get_character() && game.players[from]->get_character()->alive && player->mirrorlimit < 3)
		{
			int mirrordmg=dmg;
			if(mirrordmg > player->elf_mirror)mirrordmg=player->elf_mirror;
			game.players[from]->get_character()->take_damage(vec2(0,0),mirrordmg,player->client_id,WEAPON_MIRROR);
			player->mirrordmg_tick=server_tick();
			player->mirrorlimit++;
		}
		if(server_tick()-player->mirrordmg_tick > server_tickspeed()/2)
		{
			player->mirrorlimit=0;
		}
	}

	// player only inflicts half damage on self
	if(from == player->client_id)
		dmg = max(1, dmg/2);

	//Increasing xp with damage
	else if((game.controller)->is_rpg() && (game.players[from]->team != player->team || !(game.controller)->is_teamplay()))
	{
		PLAYER *p=game.players[from];
		p->xp+=dmg;
		if(p->healed && game.players[p->heal_from])
			game.players[p->heal_from]->xp+=dmg;
	}

	damage_taken++;

	// create healthmod indicator
	if(server_tick() < damage_taken_tick+25)
	{
		// make sure that the damage indicators doesn't group together
		game.create_damageind(pos, damage_taken*0.25f, dmg);
	}
	else
	{
		damage_taken = 0;
		game.create_damageind(pos, 0, dmg);
	}

	if(dmg)
	{
		if(armor)
		{
			if(dmg > 1)
			{
				health--;
				dmg--;
			}
			
			if(dmg > armor)
			{
				dmg -= armor;
				armor = 0;
			}
			else
			{
				armor -= dmg;
				dmg = 0;
			}
		}
		
		health -= dmg;
	}

	damage_taken_tick = server_tick();

	// do damage hit sound
	if(from >= 0 && from != player->client_id && game.players[from])
		game.create_sound(game.players[from]->view_pos, SOUND_HIT, cmask_one(from));

	// check for death
	if(health <= 0)
	{
		
		/*else if((game.controller)->is_rpg() && player->other_invincible && !player->invincible_used)
		{
			player->invincible_used=true;
			player->invincible=1;
			player->invincible_start_tick=server_tick();
			health=1;
		}*/
		die(from, weapon);

		//Reset poison at death
		player->poisoned=0;
		player->hot=0;
	
		// set attacker's face to happy (taunt!)
		if (from >= 0 && from != player->client_id && game.players[from])
		{
			CHARACTER *chr = game.players[from]->get_character();
			if (chr)
			{
				chr->emote_type = EMOTE_HAPPY;
				chr->emote_stop = server_tick() + server_tickspeed();
			}
		}
		return false;
	}

	if (dmg > 2)
		game.create_sound(pos, SOUND_PLAYER_PAIN_LONG);
	else
		game.create_sound(pos, SOUND_PLAYER_PAIN_SHORT);

	emote_type = EMOTE_PAIN;
	emote_stop = server_tick() + 500 * server_tickspeed() / 1000;

	// spawn blood?
	return true;
}

void CHARACTER::snap(int snapping_client)
{
	if(networkclipped(snapping_client))
		return;
	
	NETOBJ_CHARACTER *character = (NETOBJ_CHARACTER *)snap_new_item(NETOBJTYPE_CHARACTER, player->client_id, sizeof(NETOBJ_CHARACTER));
	
	// write down the core
	if(game.world.paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		character->tick = 0;
		core.write(character);
	}
	else
	{
		character->tick = reckoning_tick;
		sendcore.write(character);
	}
	
	// set emote
	if (emote_stop < server_tick())
	{
		emote_type = EMOTE_NORMAL;
		emote_stop = -1;
	}

	character->emote = emote_type;

	character->ammocount = 0;
	character->health = 0;
	character->armor = 0;
	
	character->weapon = active_weapon;
	character->attacktick = attack_tick;

	character->direction = input.direction;

	if(player->client_id == snapping_client)
	{
		character->health = health;
		character->armor = armor;
		if(weapons[active_weapon].ammo > 0)
			character->ammocount = weapons[active_weapon].ammo;
	}

	if (character->emote == EMOTE_NORMAL)
	{
		if(250 - ((server_tick() - last_action)%(250)) < 5)
			character->emote = EMOTE_BLINK;
	}

	character->player_state = player_state;
}
