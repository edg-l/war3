/* copyright (c) 2007 rajh */

#include <game/server/gamecontroller.hpp>
#include <game/server/entity.hpp>

#define LVLMAX 8

class GAMECONTROLLER_WAR : public GAMECONTROLLER
{
public:
	class FLAG *flags[2];
	int lvlmap[LVLMAX];

	GAMECONTROLLER_WAR();
	virtual void tick();
	virtual int on_character_death(class CHARACTER *victim, class PLAYER *killer, int weapon);
	virtual bool on_entity(int index, vec2 pos);
	virtual bool is_rpg() const;
	virtual void on_level_up(PLAYER *player);
	virtual void display_stats(PLAYER *player,PLAYER *from);
	virtual int drop_flag_orc(PLAYER *player);
	virtual void load_xp_table();
	virtual int init_xp(int level);
	virtual void on_character_spawn(class CHARACTER *chr);
};

