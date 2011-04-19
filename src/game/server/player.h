/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()
	
public:
	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };
	
	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);
	
	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();
	
	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;
	
	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;
	
	// TODO: clean this up
	struct 
	{
		char m_SkinName[64];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;
	
	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations	
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;	
	} m_Latency;
	
	//War3
	void InitRpg();
	void ResetAll();
	bool ChooseAbility(int Choice);

	//Levels var
	int m_Lvl;
	int m_NextLvl;
	int m_Xp;
	int m_Leveled;
	bool m_LevelMax;
	
	//Human vars
	int m_HumanArmor;
	int m_HumanMole;
	bool m_HumanSpecial;
	//For human killing themself for mole
	bool m_Suicide;

	//Orcs var
	int m_OrcDmg;
	int m_OrcReload;
	bool m_OrcSpecial;

	//Undead vars
	int m_UndeadTaser;
	int m_UndeadTaserTick;
	int m_UndeadVamp;
	void Vamp(int Amount);
	bool m_UndeadSpecial;
	bool m_Exploded;

	//Elf vars
	int m_ElfPoison;
	int m_Poisoned;
	int m_PoisonStartTick;
	int m_StartPoison;
	int m_Poisoner;
	int m_ElfMirror;
	int m_MirrorDmgTick;
	int m_MirrorLimit;
	bool m_ElfSpecial;

	//Tauren vars
	bool m_TaurenSpecial;
	int m_TaurenHot;
	int m_TaurenRessurect;
	bool m_Ressurected;
	int m_Hot;
	int m_HotStartTick;
	int m_StartHot;
	int m_HotFrom;
	vec2 m_DeathPos;
	bool m_Invincible;
	int m_InvincibleStartTick;
	bool m_Healed;
	int m_HealTick;
	int m_HealFrom;
	int m_StartedHeal;
	bool m_DeathTile;
	int m_Bounces;
	int m_BounceTick;
	CCharacter *m_pHealChar;
	int m_LastHealed;
	bool m_IsChainHeal;
	int m_ChainHealFrom;

	//Other
	bool m_SpecialUsed;
	int m_SpecialUsedTick;
	int m_RaceName;

	//Unused skills


	//Functions
	int UseSpecial(void);
	bool PrintOtherLvl(void);
	bool PrintHelp(void);

	//Checking :D
	int m_ForceRaceTick;
	bool m_Check;
	void CheckSkins(void);
	void CheckName(void);

private:
	CCharacter *Character;
	CGameContext *m_pGameServer;
	
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;
	
	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
};

#endif
