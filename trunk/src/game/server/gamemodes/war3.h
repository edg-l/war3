/* copyright (c) 2007 rajh */
#ifndef GAME_SERVER_GAMEMODES_WAR_H
#define GAME_SERVER_GAMEMODES_WAR_H
#include <game/server/gamecontroller.h>
#include <game/server/entity.h>

#define LVLMAX 10

class CGameControllerWAR : public IGameController
{
public:
	class CFlag *m_pFlags[2];
	int m_apLvlMap[LVLMAX];
	int m_LevelMax;

	void DefaultLvlMap();

	CGameControllerWAR(class CGameContext *pGameServer);
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	virtual bool CanBeMovedOnBalance(int ClientID);
	virtual int OnCharacterDeath(class CCharacter *Victim, class CPlayer *Killer, int Weapon);
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual bool IsRpg();
	virtual void OnLevelUp(CPlayer *Player);
	virtual void DisplayStats(CPlayer *Player,CPlayer *From);
	virtual int DropFlagOrc(CPlayer *Player);
	virtual void LoadXpTable();
	virtual int InitXp(int Level);
	virtual int GetLevelMax();
	virtual void OnCharacterSpawn(class CCharacter *Chr);
};
#endif
