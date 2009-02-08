#include <new>

#include <engine/e_server_interface.h>
#include <engine/e_config.h>

#include "player.hpp"
#include "gamecontext.hpp"
#include <string.h>

MACRO_ALLOC_POOL_ID_IMPL(PLAYER, MAX_CLIENTS)

PLAYER::PLAYER(int client_id)
{
	respawn_tick = server_tick();
	character = 0;
	this->client_id = client_id;
}

PLAYER::~PLAYER()
{
	delete character;
	character = 0;
}

void PLAYER::tick()
{
	server_setclientscore(client_id, score);

	// do latency stuff
	{
		CLIENT_INFO info;
		if(server_getclientinfo(client_id, &info))
		{
			latency.accum += info.latency;
			latency.accum_max = max(latency.accum_max, info.latency);
			latency.accum_min = min(latency.accum_min, info.latency);
		}

		if(server_tick()%server_tickspeed() == 0)
		{
			latency.avg = latency.accum/server_tickspeed();
			latency.max = latency.accum_max;
			latency.min = latency.accum_min;
			latency.accum = 0;
			latency.accum_min = 1000;
			latency.accum_max = 0;
		}
	}
	
	if(!character && die_tick+server_tickspeed()*3 <= server_tick())
		spawning = true;

	if(character)
	{
		if(character->alive)
		{
			view_pos = character->pos;
		}
		else
		{
			delete character;
			character = 0;
		}
	}
	else if(spawning && respawn_tick <= server_tick())
		try_respawn();

	if(!(game.controller)->is_rpg())
		return;

	//Leveling up gratz !
	if(xp>nextlvl && !levelmax && race_name != VIDE)
		game.controller->on_level_up(this);

	//Check for special reload
	if(server_tick()-special_used_tick >= 0 && special_used)
	{
		special_used=false;
		game.create_sound_global(SOUND_HOOK_LOOP, client_id);
	}
	//Invicible (not used)
	if(invincible && server_tick()-invincible_start_tick > server_tickspeed()*3)
	{
		invincible=false;
		check=true;
	}
	//Dumb people don't choose race ? oO
	if(race_name==VIDE && server_tick()%(server_tickspeed()*2)==0 && team != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Choose a race say \"/race undead/orc/human/elf/tauren\"");
		game.send_broadcast(buf, client_id);
	}

	//Forcing a race
	if(config.sv_force_race && team != -1 && race_name == VIDE && server_tick()-force_race_tick >= server_tickspeed()*config.sv_force_race*60)
	{
		int i,force_race=-1;
		int nbRace[NBRACE]={0};
		for (i=0;i < MAX_CLIENTS;i++)
		{
			if(game.players[i] && game.players[i]->race_name != VIDE)
				nbRace[game.players[i]->race_name]++;
		}
		for (i=1;i < NBRACE;i++)
		{
			if(force_race == -1 && i != TAUREN || nbRace[i]<nbRace[force_race] && i != TAUREN)
				force_race=i;
		}
		char buf[128];
		str_format(buf, sizeof(buf), "Race forced");
		game.send_broadcast(buf, client_id);
		init_rpg();
		race_name = force_race;
		kill_character(-1);
		score++;
		dbg_msg("war3","Forcing player : %s to race : %d",server_clientname(client_id),race_name);
		check=true;
	}

	//Dunno about CPU usage
	if(check)
	{
		check_skins();
		check_name();
		check=false;
	}
}

void PLAYER::snap(int snapping_client)
{
	NETOBJ_CLIENT_INFO *client_info = (NETOBJ_CLIENT_INFO *)snap_new_item(NETOBJTYPE_CLIENT_INFO, client_id, sizeof(NETOBJ_CLIENT_INFO));
	str_to_ints(&client_info->name0, 6, server_clientname(client_id));
	str_to_ints(&client_info->skin0, 6, skin_name);
	client_info->use_custom_color = use_custom_color;
	client_info->color_body = color_body;
	client_info->color_feet = color_feet;

	NETOBJ_PLAYER_INFO *info = (NETOBJ_PLAYER_INFO *)snap_new_item(NETOBJTYPE_PLAYER_INFO, client_id, sizeof(NETOBJ_PLAYER_INFO));

	info->latency = latency.min;
	info->latency_flux = latency.max-latency.min;
	info->local = 0;
	info->cid = client_id;
	info->score = score;
	info->team = team;

	if(client_id == snapping_client)
		info->local = 1;	
}

void PLAYER::on_disconnect()
{
	kill_character(WEAPON_GAME);
	
	//game.controller->on_player_death(&game.players[client_id], 0, -1);
		
	char buf[512];
	str_format(buf, sizeof(buf),  "%s has left the game", server_clientname(client_id));
	game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf);

	dbg_msg("game", "leave player='%d:%s'", client_id, server_clientname(client_id));
}

void PLAYER::on_predicted_input(NETOBJ_PLAYER_INPUT *new_input)
{
	CHARACTER *chr = get_character();
	if(chr)
		chr->on_predicted_input(new_input);
}

void PLAYER::on_direct_input(NETOBJ_PLAYER_INPUT *new_input)
{
	CHARACTER *chr = get_character();
	if(chr)
		chr->on_direct_input(new_input);

	if(!chr && team >= 0 && (new_input->fire&1))
		spawning = true;
	
	if(!chr && team == -1)
		view_pos = vec2(new_input->target_x, new_input->target_y);
}

CHARACTER *PLAYER::get_character()
{
	if(character && character->alive)
		return character;
	return 0;
}

void PLAYER::kill_character(int weapon)
{
	//CHARACTER *chr = get_character();
	if(character)
	{
		character->die(client_id, weapon);
		delete character;
		character = 0;
	}
}

void PLAYER::respawn()
{
	if(team > -1)
	{
		spawning = true;
		//At respawn special shouldn't be reset in fact :D
		//if(!undead_special)special_used=false;
		dmg_mirror=false;
	}
}

void PLAYER::set_team(int new_team)
{
	// clamp the team
	new_team = game.controller->clampteam(new_team);
	if(team == new_team)
		return;
		
	char buf[512];
	str_format(buf, sizeof(buf), "%s joined the %s", server_clientname(client_id), game.controller->get_team_name(new_team));
	game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf); 
	
	kill_character(WEAPON_GAME);
	team = new_team;
	score = 0;
	if(team==-1)init_rpg();
	if(race_name == TAUREN)
	{
		int count_tauren=0;
		int i;
		for(i=0;i < MAX_CLIENTS;i++)
		{
			if(game.players[i] && game.players[i]->client_id != -1 && game.players[i]->race_name == TAUREN && game.players[i]->team == team && game.players[i]->client_id != client_id)
				count_tauren++;
		}
		if(count_tauren >= config.sv_max_tauren)
		{
			race_name=HUMAN;
			check=true;
			reset_all();
		}
	}
	dbg_msg("game", "team_join player='%d:%s' team=%d", client_id, server_clientname(client_id), team);
	
	game.controller->on_player_info_change(game.players[client_id]);
}

void PLAYER::try_respawn()
{
	vec2 spawnpos = vec2(100.0f, -60.0f);
	
	if(!game.controller->can_spawn(this, &spawnpos))
		return;

	// check if the position is occupado
	ENTITY *ents[2] = {0};
	int num_ents = game.world.find_entities(spawnpos, 64, ents, 2, NETOBJTYPE_CHARACTER);
	
	if(num_ents == 0)
	{
		spawning = false;
		character = new(client_id) CHARACTER();
		character->spawn(this, spawnpos, team);
		game.create_playerspawn(spawnpos);
	}
}

//Init vars
void PLAYER::init_rpg()
{
	if(!(game.controller)->is_rpg())
		return;
	lvl = 1;
	nextlvl = game.controller->init_xp(lvl);	
	xp = 0;
	leveled=0;
	levelmax=false;
	human_armor=0;
	human_mole=0;
	human_special=false;
	orc_dmg=0;
	orc_reload=0;
	orc_special=false;
	undead_taser=0;
	undead_vamp=0;
	undead_special=false;
	exploded=false;
	elf_poison=0;
	elf_special=false;
	elf_mirror=0;
	mirrordmg_tick=0;
	mirrorlimit=0;
	dmg_mirror=false;
	special_used=false;
	race_name=VIDE;
	force_race_tick=server_tick();
	poisoned=0;
	poison_start_tick=0;
	start_poison=0;
	poisoner=-1;
	tauren_special=false;
	tauren_hot=0;
	tauren_ressurect=0;
	ressurected=false;
	hot=0;
	hot_start_tick=0;
	start_hot=0;
	hot_from=-1;
	healed=false;
	heal_tick=-1;
	started_heal=-1;
	heal_from=-1;
	death_tile=false;
	check=true;
}

//Reset vars
void PLAYER::reset_all()
{	
	if(!(game.controller)->is_rpg())
		return;
	levelmax=false;
	leveled=lvl-1;
	human_armor=0;
	human_mole=0;
	human_special=false;
	orc_dmg=0;
	orc_reload=0;
	orc_special=false;
	undead_taser=0;
	undead_vamp=0;
	undead_special=false;
	exploded=false;
	elf_poison=0;
	elf_special=false;
	elf_mirror=0;
	mirrordmg_tick=0;
	mirrorlimit=0;
	dmg_mirror=false;
	special_used=false;
	poisoned=0;
	poison_start_tick=0;
	start_poison=0;
	poisoner=-1;
	tauren_special=false;
	tauren_hot=0;
	tauren_ressurect=0;
	ressurected=false;
	hot=0;
	hot_start_tick=0;
	start_hot=0;
	hot_from=-1;
	healed=false;
	heal_tick=-1;
	started_heal=-1;
	heal_from=-1;
	death_tile=false;
}

//Choose an ability
bool PLAYER::choose_ability(int choice)
{
	if(!(game.controller)->is_rpg())
		return false;
	char buf[128];
	if(race_name==ORC)
	{
		if(choice == 1 && orc_dmg<4)
		{
			orc_dmg++;
			str_format(buf, sizeof(buf), "Damage + %d%%",orc_dmg*15);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice == 2 && orc_reload<4)
		{
			orc_reload++;
			str_format(buf, sizeof(buf), "Reload faster + %d",orc_reload);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice==3 && lvl >=6 && !orc_special)
		{
			orc_special=true;
			str_format(buf, sizeof(buf), "Teleport Backup enable");
			game.send_broadcast(buf, client_id);
			return true;
		}
		else
			return false;
	}
	else if(race_name==HUMAN)
	{
		if(choice == 1 && human_armor<4)
		{
			human_armor++;
			str_format(buf, sizeof(buf), "Armor + %d%%",human_armor*15);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice == 2 && human_mole<4)
		{
			human_mole++;
			str_format(buf, sizeof(buf), "Mole chance = %d%%",human_mole*15);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice==3 && lvl >=6 && !human_special)
		{
			human_special=true;
			str_format(buf, sizeof(buf), "Teleport enable");
			game.send_broadcast(buf, client_id);
			return true;
		}
		else
			return false;
	}
	else if(race_name==ELF)
	{
		if(choice == 1 && elf_poison<4)
		{
			elf_poison++;
			str_format(buf, sizeof(buf), "Poison %d ticks",elf_poison*2);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice == 2 && elf_mirror<4)
		{
			elf_mirror++;
			str_format(buf, sizeof(buf), "Mirror damage + %d",elf_mirror);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice==3 && lvl >=6 && !elf_special)
		{
			elf_special=true;
			str_format(buf, sizeof(buf), "Immobilise enable");
			game.send_broadcast(buf, client_id);
			return true;
		}
		else
			return false;
	}
	else if(race_name==UNDEAD)
	{
		if(choice == 1 && undead_taser<4)
		{
			undead_taser++;
			str_format(buf, sizeof(buf), "Taser + %d",undead_taser);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice == 2 && undead_vamp<4)
		{
			undead_vamp++;
			str_format(buf, sizeof(buf), "Vampiric + %d",undead_vamp);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice==3 && lvl >=6 && !undead_special)
		{
			undead_special=true;
			str_format(buf, sizeof(buf), "Kamikaze enabled");
			game.send_broadcast(buf, client_id);
			return true;
		}
		else
			return false;
	}
	else if(race_name==TAUREN)
	{
		if(choice == 1 && tauren_hot<4)
		{
			tauren_hot++;
			str_format(buf, sizeof(buf), "Hot %d tick",tauren_hot*2);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice == 2 && tauren_ressurect<4)
		{
			tauren_ressurect++;
			str_format(buf, sizeof(buf), "Ressurection + %d%%",tauren_ressurect*15);
			game.send_broadcast(buf, client_id);
			return true;
		}
		else if(choice==3 && lvl >=6 && !tauren_special)
		{
			tauren_special=true;
			str_format(buf, sizeof(buf), "Shield enabled");
			game.send_broadcast(buf, client_id);
			return true;
		}
		else
			return false;
	}
	else
		return false;
}


//Vamp function
void PLAYER::vamp(int amount)
{
	if(!(game.controller)->is_rpg())
		return;
	if(character)
	{	
		if(amount > undead_vamp)
			amount=undead_vamp;
		character->increase_health(amount);
	}
}

//Function for using special
int PLAYER::use_special()
{
	if(!(game.controller)->is_rpg())
		return -3;
	if(!special_used)
	{
		if(elf_special && game.players[client_id]->get_character())
		{
			special_used=true;
			special_used_tick=server_tick()+server_tickspeed()*config.sv_specialtime*2;
			vec2 direction = normalize(vec2(character->latest_input.target_x, character->latest_input.target_y));
			vec2 at;
			CHARACTER *hit;
			vec2 to=character->core.pos+direction*700;
			col_intersect_line(character->core.pos, to, 0x0, &to);
			hit = game.world.intersect_character(character->core.pos, to, 0.0f, at, character);
			//if(hit)dbg_msg("test","hit : %d",hit->player->client_id);
			if(!hit || (hit->player->team == team && !config.sv_teamdamage))
				special_used=false;
			else if(hit->player->team != team || config.sv_teamdamage)
				hit->stucked=server_tick();
			return 0;
		}
		else if(human_special && game.players[client_id]->get_character())
		{
			special_used=true;
			get_character()->stucked=0;
			special_used_tick=server_tick()+server_tickspeed()*config.sv_specialtime;
			vec2 direction = normalize(vec2(character->latest_input.target_x, character->latest_input.target_y));
			vec2 prevdir=direction;
			vec2 tmpvec;
			col_intersect_line(character->core.pos, character->core.pos+direction*1000, 0x0, &direction);
			tmpvec=direction-prevdir*100;
			if(!col_is_solid(tmpvec.x-14,tmpvec.y-14) && !col_is_solid(tmpvec.x+14,tmpvec.y-14) && !col_is_solid(tmpvec.x-14,tmpvec.y+14) && !col_is_solid(tmpvec.x+14,tmpvec.y+14))
			{
				character->core.pos=tmpvec;
				return 0;
			}
			else
			{
				special_used=false;
				return -4;
			}
		}
		else if(orc_special && game.players[client_id]->get_character())
		{
			int res;
			special_used=true;
			special_used_tick=server_tick()+server_tickspeed()*config.sv_specialtime*4;
			poisoned=0;
			res=game.controller->drop_flag_orc(this);
			if(res==-1)
			{
				vec2 spawnpos = vec2(100.0f, -60.0f);
				if(!game.controller->can_spawn(this, &spawnpos))
					return -3;
				else
					character->core.pos=spawnpos;
			}
			else
			{
				CHARACTER* to = game.players[res]->get_character();
				CHARACTER* from = get_character();
				if(from && to)
				{
					from->core.pos.x=to->core.pos.x;
					from->core.pos.y=to->core.pos.y;
				}
			}			
			return 0;
		}
		else if(undead_special && game.players[client_id]->get_character())
		{
			kill_character(WEAPON_SELF);
			return 0;
		}
		else if(tauren_special && game.players[client_id]->get_character())
		{
			special_used=true;
			invincible_start_tick=server_tick();
			invincible=true;
			game.send_chat_target(client_id,"Shield used");
			check=true;
			special_used_tick=server_tick()+server_tickspeed()*config.sv_specialtime*12;
			return 0;
		}
	}
	else if(special_used)
	{
					
		char buf[128];
		str_format(buf, sizeof(buf), "Special reloading : %d sec",(int)((float)(special_used_tick-server_tick())/(float)server_tickspeed())+1);
		game.send_broadcast(buf, client_id);
		return 0;
	}
	if(!human_special && !orc_special && !elf_special && !undead_special && !tauren_special)
		return -1;
	else if(!game.players[client_id]->get_character())
		return -2;
	return -3;
}

//Print other players level
bool PLAYER::print_otherlvl()
{
	if(!(game.controller)->is_rpg())
		return false;
	char buf[128];
	char tmprace[30];
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		if(game.players[i] && game.players[i]->race_name != VIDE && game.players[i]->team == team)
		{
			if(game.players[i]->race_name == ORC)str_format(tmprace,sizeof(tmprace),"ORC");
			else if(game.players[i]->race_name == UNDEAD)str_format(tmprace,sizeof(tmprace),"UNDEAD");
			else if(game.players[i]->race_name == ELF)str_format(tmprace,sizeof(tmprace),"ELF");
			else if(game.players[i]->race_name == HUMAN)str_format(tmprace,sizeof(tmprace),"HUMAN");
			str_format(buf,sizeof(buf),"%s : race : %s level : %d",server_clientname(i),tmprace,game.players[i]->lvl);
			game.send_chat_target(client_id, buf);
		}
	}
	return true;
}

//Print help
bool PLAYER::print_help()
{
	if(!(game.controller)->is_rpg())
		return false;
	char buf[128];
	if(race_name != VIDE)
	{
		if(race_name == ORC)
		{
			str_format(buf,sizeof(buf),"ORC:");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Damage : Damage +15/30/45/60%%");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Reload : Fire rate increase each level");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Special : Teleport you to spawn or to your teammates with the flag (lvl 6 required)");
			game.send_chat_target(client_id, buf);
		}
		else if(race_name == HUMAN)
		{
			str_format(buf,sizeof(buf),"HUMAN:");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Armor : Armor +15/30/45/60%%");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Mole : 15/30/45/60%% chance to respawn in enemy base");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Special : teleport you where you are aiming at (lvl 6 required)");
			game.send_chat_target(client_id, buf);
		}
		else if(race_name == ELF)
		{
			str_format(buf,sizeof(buf),"ELF:");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Poison : Deal 1 damage each second during 2/4/6/8 tick");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Mirror : Reverse 1/2/3/4 damage");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Special : Immobilise the player you are aiming at (lvl 6 required)");
			game.send_chat_target(client_id, buf);
		}
		else if(race_name == UNDEAD)
		{
			str_format(buf,sizeof(buf),"UNDEAD:");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Taser: Hook deals damage");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Vampiric: Absorb ennemy hp");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Special : Kamikaze, when you die you explode dealing lot of damage (lvl 6 required)");
			game.send_chat_target(client_id, buf);
		}
		else if(race_name == TAUREN)
		{
			str_format(buf,sizeof(buf),"TAUREN:");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Tauren have a native ability wich is healing with grenade launcher(2 hp / sec) range increased by lvl");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Hot : Healing(hp and armor) over time for 2/4/6/8 ticks with pistol(like a poison)");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Ressurection : 15/30/45/60%% chance to ressurect at the place where one died");
			game.send_chat_target(client_id, buf);
			str_format(buf,sizeof(buf),"Special : Shield for 3 sec(damage are reflected)(lvl 6 required)");
			game.send_chat_target(client_id, buf);
		}
		return true;
	}
	else
		return false;
}

void PLAYER::check_skins(void)
{
	if(race_name == ORC && strcmp(skin_name,"orc"))
		str_format(skin_name,sizeof(skin_name),"orc");
	else if(race_name == HUMAN && strcmp(skin_name,"human"))
		str_format(skin_name,sizeof(skin_name),"human");
	else if(race_name == UNDEAD && strcmp(skin_name,"undead"))
		str_format(skin_name,sizeof(skin_name),"undead");
	else if(race_name == ELF && strcmp(skin_name,"elf"))
		str_format(skin_name,sizeof(skin_name),"elf");
	else if(race_name == TAUREN && invincible && strcmp(skin_name,"tauren_invincible"))
		str_format(skin_name,sizeof(skin_name),"tauren_invincible");
	else if(race_name == TAUREN && strcmp(skin_name,"tauren"))
		str_format(skin_name,sizeof(skin_name),"tauren");
	else if(race_name == VIDE && strcmp(skin_name,"default"))
		str_format(skin_name,sizeof(skin_name),"default");
}

void PLAYER::check_name(void)
{
	if(!config.sv_race_tag)
		return;

	if(race_name == ORC && !strncmp(server_clientname(client_id),"[ORC]",5))
		return;
	else if(race_name == HUMAN && !strncmp(server_clientname(client_id),"[HUM]",5))
		return;
	else if(race_name == UNDEAD && !strncmp(server_clientname(client_id),"[UND]",5))
		return;
	else if(race_name == ELF && !strncmp(server_clientname(client_id),"[ELF]",5))
		return;
	else if(race_name == TAUREN && !strncmp(server_clientname(client_id),"[TAU]",5))
		return;
	else if(race_name == VIDE && !strncmp(server_clientname(client_id),"[___]",5))
		return;
	char newname[MAX_NAME_LENGTH];
	char tmp[MAX_NAME_LENGTH];
	str_copy(newname,server_clientname(client_id),MAX_NAME_LENGTH);
	if(race_name == VIDE)
		str_format(tmp,sizeof(tmp),"[___]");
	else if(race_name == ORC)
		str_format(tmp,sizeof(tmp),"[ORC]");
	else if(race_name == UNDEAD)
		str_format(tmp,sizeof(tmp),"[UND]");
	else if(race_name == HUMAN)
		str_format(tmp,sizeof(tmp),"[HUM]");
	else if(race_name == ELF)
		str_format(tmp,sizeof(tmp),"[ELF]");
	else if(race_name == TAUREN)
		str_format(tmp,sizeof(tmp),"[TAU]");
	strncat(tmp,newname+5,MAX_NAME_LENGTH-7);
	tmp[MAX_NAME_LENGTH-1]=0;
	server_setclientname(client_id, tmp);
}
