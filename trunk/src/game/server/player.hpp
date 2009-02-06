#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.hpp"

// player object
class PLAYER
{
	MACRO_ALLOC_POOL_ID()
private:
	CHARACTER *character;
public:
	PLAYER(int client_id);
	~PLAYER();

	// TODO: clean this up
	char skin_name[64];
	int use_custom_color;
	int color_body;
	int color_feet;
	
	int respawn_tick;
	int die_tick;
	//
	bool spawning;
	int client_id;
	int team;
	int score;
	bool force_balanced;
	
	//
	int vote;
	int64 last_votecall;

	//
	int64 last_chat;
	int64 last_setteam;
	int64 last_changeinfo;
	int64 last_emote;
	int64 last_kill;

	// network latency calculations	
	struct
	{
		int accum;
		int accum_min;
		int accum_max;
		int avg;
		int min;
		int max;	
	} latency;
	
	// this is used for snapping so we know how we can clip the view for the player
	vec2 view_pos;

	void init(int client_id);
	
	CHARACTER *get_character();
	
	void kill_character(int weapon);

	void try_respawn();
	void respawn();
	void set_team(int team);
	
	void tick();
	void snap(int snapping_client);

	void on_direct_input(NETOBJ_PLAYER_INPUT *new_input);
	void on_predicted_input(NETOBJ_PLAYER_INPUT *new_input);
	void on_disconnect();

	//War3
	void init_rpg();
	void reset_all();
	bool choose_ability(int choice);

	//Levels var
	int lvl;
	int nextlvl;
	int xp;
	int leveled;
	bool levelmax;
	
	//Human vars
	int human_armor;
	int human_mole;
	bool human_special;
	//For human killing themself for mole
	bool suicide;

	//Orcs var
	int orc_dmg;
	int orc_reload;
	bool orc_special;

	//Undead vars
	int undead_taser;
	int undead_taser_tick;
	int undead_vamp;
	void vamp(int amount);
	bool undead_special;
	bool exploded;

	//Elf vars
	int elf_poison;
	int poisoned;
	int poison_start_tick;
	int start_poison;
	int poisoner;
	int elf_mirror;
	int mirrordmg_tick;
	int mirrorlimit;
	bool elf_special;

	//Tauren vars
	bool tauren_special;
	int tauren_hot;
	int tauren_ressurect;
	bool ressurected;
	int hot;
	int hot_start_tick;
	int start_hot;
	int hot_from;
	vec2 death_pos;
	bool invincible;
	int invincible_start_tick;
	bool healed;
	int heal_tick;
	int heal_from;
	int started_heal;

	//Other
	bool special_used;
	int special_used_tick;
	int race_name;

	//Unused skills


	//Functions
	int use_special(void);
	bool print_otherlvl(void);
	bool print_help(void);

	//Checking :D
	int force_race_tick;
	bool check;
	void check_skins(void);
	void check_name(void);
};

#endif
