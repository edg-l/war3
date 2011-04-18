/* copyright (c) 2007 rajh */
#ifndef GAME_SERVER_GAMEMODES_WAR_H
#define GAME_SERVER_GAMEMODES_WAR_H
#include <game/server/gamecontroller.h>

#define LVLMAX 10

class CGameControllerWAR : public IGameController
{
public:
	class CFlag *flags[2];
	int lvlmap[LVLMAX];
	int level_max;

	void default_lvlmap();

	CGameControllerWAR(class CGameContext *pGameServer);
	virtual void Tick();
	virtual int OnCharacterDeath(class CCharacter *victim, class CPlayer *killer, int weapon);
	virtual bool OnEntity(int index, vec2 pos);
	virtual bool is_rpg();
	virtual void on_level_up(CPlayer *player);
	virtual void display_stats(CPlayer *player,CPlayer *from);
	virtual int drop_flag_orc(CPlayer *player);
	virtual void load_xp_table();
	virtual int init_xp(int level);
	virtual int get_level_max();
	virtual void OnCharacterSpawn(class CCharacter *chr);
};
#endif
