/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include "player.h"

//For strcmp
#include <string.h>


MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }
	
CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	Character = 0;
	this->m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
}

CPlayer::~CPlayer()
{
	delete Character;
	Character = 0;
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	Server()->SetClientScore(m_ClientID, m_Score);

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}
	
	if(!Character && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
		m_Spawning = true;

	if(Character)
	{
		if(Character->IsAlive())
		{
			m_ViewPos = Character->m_Pos;
		}
		else
		{
			delete Character;
			Character = 0;
		}
	}
	else if(m_Spawning && m_RespawnTick <= Server()->Tick())
		TryRespawn();

	if(!GameServer()->m_pController->IsRpg())
		return;

	//Leveling up gratz !
	if(m_Xp>m_NextLvl && !m_LevelMax && m_RaceName != VIDE)
		GameServer()->m_pController->OnLevelUp(this);

	//Check for special reload
	if(Server()->Tick()-m_SpecialUsedTick >= 0 && m_SpecialUsed)
	{
		m_SpecialUsed=false;
		GameServer()->CreateSoundGlobal(SOUND_HOOK_LOOP, m_ClientID);
	}
	//Invicible (not used)
	if(m_Invincible && Server()->Tick()-m_InvincibleStartTick > Server()->TickSpeed()*3)
	{
		m_Invincible=false;
		m_Check=true;
	}
	//Dumb people don't choose race ? oO
	if(m_RaceName==VIDE && Server()->Tick()%(Server()->TickSpeed()*2)==0 && m_Team != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Choose a race say \"/race undead/orc/human/elf/tauren\"");
		GameServer()->SendBroadcast(buf, m_ClientID);
	}

	//Forcing a race
	if(g_Config.m_SvForceRace && m_Team != -1 && m_RaceName == VIDE && Server()->Tick()-m_ForceRaceTick >= Server()->TickSpeed()*g_Config.m_SvForceRace*60)
	{
		int i,force_race=-1;
		int nbRace[NBRACE]={0};
		for (i=0;i < MAX_CLIENTS;i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_RaceName != VIDE)
				nbRace[GameServer()->m_apPlayers[i]->m_RaceName]++;
		}
		for (i=1;i < NBRACE;i++)
		{
			if(force_race == -1 && i != TAUREN || nbRace[i]<nbRace[force_race] && i != TAUREN)
				force_race=i;
		}
		char buf[128];
		str_format(buf, sizeof(buf), "Race forced");
		GameServer()->SendBroadcast(buf, m_ClientID);
		InitRpg();
		m_RaceName = force_race;
		KillCharacter(-1);
		m_Score++;
		dbg_msg("war3","Forcing player : %s to race : %d",Server()->ClientName(m_ClientID),m_RaceName);
		m_Check=true;
	}

	//Dunno about CPU usage
	if(m_Check)
	{
		CheckSkins();
		CheckName();
		m_Check=false;
	}
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, m_ClientID, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = m_ClientID;
	pPlayerInfo->m_Score = m_Score;
	pPlayerInfo->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf),  "'%s' has left the game (%s)", Server()->ClientName(m_ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf),  "'%s' has left the game", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	if(Character)
		Character->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(Character)
		Character->OnDirectInput(NewInput);

	if(!Character && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		m_Spawning = true;
	
	if(!Character && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
		m_ViewPos = vec2(NewInput->m_TargetX, NewInput->m_TargetY);

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(Character && Character->IsAlive())
		return Character;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(Character)
	{
		Character->Die(m_ClientID, Weapon);
		delete Character;
		Character = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team)
{
	// clamp the m_Team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;
		
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf); 
	
	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	
	if(m_Team==-1)InitRpg();
	if(m_RaceName == TAUREN)
	{
		int count_tauren=0;
		int i;
		for(i=0;i < MAX_CLIENTS;i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetCID() != -1 && GameServer()->m_apPlayers[i]->m_RaceName == TAUREN && GameServer()->m_apPlayers[i]->GetTeam() == m_Team && GameServer()->m_apPlayers[i]->GetCID() != m_ClientID)
				count_tauren++;
		}
		if(count_tauren >= g_Config.m_SvMaxTauren)
		{
			m_RaceName=HUMAN;
			ResetAll();
		}
	}
	m_Check=true;

	str_format(aBuf, sizeof(aBuf), "m_Team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	
	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;
	
	if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos, this))
		return;

	m_Spawning = false;
	Character = new(m_ClientID) CCharacter(&GameServer()->m_World);
	Character->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);
}

//Init vars
void CPlayer::InitRpg()
{
	if(!GameServer()->m_pController->IsRpg())
		return;
	m_Lvl = g_Config.m_SvLevelStart;
	m_NextLvl = GameServer()->m_pController->InitXp(m_Lvl);	
	m_Xp = 0;
	m_Leveled=m_Lvl-1;
	m_LevelMax=false;
	m_HumanArmor=0;
	m_HumanMole=0;
	m_HumanSpecial=false;
	m_OrcDmg=0;
	m_OrcReload=0;
	m_OrcSpecial=false;
	m_UndeadTaser=0;
	m_UndeadVamp=0;
	m_UndeadSpecial=false;
	m_Exploded=false;
	m_ElfPoison=0;
	m_ElfSpecial=false;
	m_ElfMirror=0;
	m_MirrorDmgTick=0;
	m_MirrorLimit=0;
	m_SpecialUsed=false;
	m_RaceName=VIDE;
	m_ForceRaceTick=Server()->Tick();
	m_Poisoned=0;
	m_PoisonStartTick=0;
	m_StartPoison=0;
	m_Poisoner=-1;
	m_TaurenSpecial=false;
	m_TaurenHot=0;
	m_TaurenRessurect=0;
	m_Ressurected=false;
	m_Hot=0;
	m_HotStartTick=0;
	m_StartHot=0;
	m_HotFrom=-1;
	m_Healed=false;
	m_HealTick=-1;
	m_StartedHeal=-1;
	m_HealFrom=-1;
	m_DeathTile=false;
	m_Check=true;
}

//Reset vars
void CPlayer::ResetAll()
{	
	if(!GameServer()->m_pController->IsRpg())
		return;
	if(m_Lvl < GameServer()->m_pController->GetLevelMax())m_LevelMax=false;
	else m_LevelMax=true;
	m_Leveled=m_Lvl-1;
	m_NextLvl = GameServer()->m_pController->InitXp(m_Lvl);
	m_HumanArmor=0;
	m_HumanMole=0;
	m_HumanSpecial=false;
	m_OrcDmg=0;
	m_OrcReload=0;
	m_OrcSpecial=false;
	m_UndeadTaser=0;
	m_UndeadVamp=0;
	m_UndeadSpecial=false;
	m_Exploded=false;
	m_ElfPoison=0;
	m_ElfSpecial=false;
	m_ElfMirror=0;
	m_MirrorDmgTick=0;
	m_MirrorLimit=0;
	m_TaurenSpecial=false;
	m_TaurenHot=0;
	m_TaurenRessurect=0;
	m_Ressurected=false;
	m_Hot=0;
	m_HotStartTick=0;
	m_StartHot=0;
	m_HotFrom=-1;
	m_Healed=false;
	m_HealTick=-1;
	m_StartedHeal=-1;
	m_HealFrom=-1;
	m_DeathTile=false;
}

//Choose an ability
bool CPlayer::ChooseAbility(int Choice)
{
	if(!GameServer()->m_pController->IsRpg())
		return false;
	char buf[128];
	if(m_RaceName==ORC)
	{
		if(Choice == 1 && m_OrcDmg<4)
		{
			m_OrcDmg++;
			str_format(buf, sizeof(buf), "Damage + %d%%",m_OrcDmg*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice == 2 && m_OrcReload<4)
		{
			m_OrcReload++;
			str_format(buf, sizeof(buf), "Reload faster + %d",m_OrcReload);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice==3 && m_Lvl >=6 && !m_OrcSpecial)
		{
			m_OrcSpecial=true;
			str_format(buf, sizeof(buf), "Teleport Backup enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else
			return false;
	}
	else if(m_RaceName==HUMAN)
	{
		if(Choice == 1 && m_HumanArmor<4)
		{
			m_HumanArmor++;
			str_format(buf, sizeof(buf), "Armor + %d%%",m_HumanArmor*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice == 2 && m_HumanMole<4)
		{
			m_HumanMole++;
			str_format(buf, sizeof(buf), "Mole chance = %d%%",m_HumanMole*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice==3 && m_Lvl >=6 && !m_HumanSpecial)
		{
			m_HumanSpecial=true;
			str_format(buf, sizeof(buf), "Teleport enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else
			return false;
	}
	else if(m_RaceName==ELF)
	{
		if(Choice == 1 && m_ElfPoison<4)
		{
			m_ElfPoison++;
			str_format(buf, sizeof(buf), "Poison %d ticks",m_ElfPoison*2);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice == 2 && m_ElfMirror<4)
		{
			m_ElfMirror++;
			str_format(buf, sizeof(buf), "Mirror damage + %d",m_ElfMirror);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice==3 && m_Lvl >=6 && !m_ElfSpecial)
		{
			m_ElfSpecial=true;
			str_format(buf, sizeof(buf), "Immobilise enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else
			return false;
	}
	else if(m_RaceName==UNDEAD)
	{
		if(Choice == 1 && m_UndeadTaser<4)
		{
			m_UndeadTaser++;
			str_format(buf, sizeof(buf), "Taser + %d",m_UndeadTaser);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice == 2 && m_UndeadVamp<4)
		{
			m_UndeadVamp++;
			str_format(buf, sizeof(buf), "Vampiric + %d",m_UndeadVamp);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice==3 && m_Lvl >=6 && !m_UndeadSpecial)
		{
			m_UndeadSpecial=true;
			str_format(buf, sizeof(buf), "Kamikaze enabled");
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else
			return false;
	}
	else if(m_RaceName==TAUREN)
	{
		if(Choice == 1 && m_TaurenHot<4)
		{
			m_TaurenHot++;
			str_format(buf, sizeof(buf), "Hot %d tick",m_TaurenHot*2);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice == 2 && m_TaurenRessurect<4)
		{
			m_TaurenRessurect++;
			str_format(buf, sizeof(buf), "Ressurection + %d%%",m_TaurenRessurect*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(Choice==3 && m_Lvl >=6 && !m_TaurenSpecial)
		{
			m_TaurenSpecial=true;
			str_format(buf, sizeof(buf), "Shield enabled");
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else
			return false;
	}
	else
		return false;
}


//Vamp function
void CPlayer::Vamp(int Amount)
{
	if(!GameServer()->m_pController->IsRpg())
		return;
	if(Character)
	{	
		if(Amount > m_UndeadVamp)
			Amount=m_UndeadVamp;
		Character->IncreaseHealth(Amount);
	}
}

//Function for using special
int CPlayer::UseSpecial()
{
	if(!GameServer()->m_pController->IsRpg())
		return -3;
	if(!m_SpecialUsed)
	{
		if(m_ElfSpecial && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			m_SpecialUsed=true;
			m_SpecialUsedTick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*2;
			vec2 direction = normalize(vec2(Character->m_LatestInput.m_TargetX, Character->m_LatestInput.m_TargetY));
			vec2 at;
			CCharacter *hit;
			vec2 to=Character->m_Core.m_Pos+direction*700;
			GameServer()->Collision()->IntersectLine(Character->m_Core.m_Pos, to, 0x0, &to);
			hit = GameServer()->m_World.IntersectCharacter(Character->m_Core.m_Pos, to, 0.0f, at, Character);
			//if(hit)dbg_msg("test","hit : %d",hit->GetPlayer()->m_ClientID);
			if(!hit || (hit->GetPlayer()->m_Team == m_Team && !g_Config.m_SvTeamdamage))
				m_SpecialUsed=false;
			else if(hit->GetPlayer()->m_Team != m_Team || g_Config.m_SvTeamdamage)
				hit->stucked=Server()->Tick();
			return 0;
		}
		else if(m_HumanSpecial && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			m_SpecialUsed=true;
			GetCharacter()->stucked=0;
			m_SpecialUsedTick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime;
			vec2 direction = normalize(vec2(Character->m_LatestInput.m_TargetX, Character->m_LatestInput.m_TargetY));
			vec2 prevdir=direction;
			vec2 tmpvec;
			GameServer()->Collision()->IntersectLine(Character->m_Core.m_Pos, Character->m_Core.m_Pos+direction*1000, 0x0, &direction);
			tmpvec=direction-prevdir*100;
			if(!GameServer()->Collision()->IsTileSolid(tmpvec.x-14,tmpvec.y-14) && !GameServer()->Collision()->IsTileSolid(tmpvec.x+14,tmpvec.y-14) && !GameServer()->Collision()->IsTileSolid(tmpvec.x-14,tmpvec.y+14) && !GameServer()->Collision()->IsTileSolid(tmpvec.x+14,tmpvec.y+14))
			{
				Character->m_Core.m_Pos=tmpvec;
				return 0;
			}
			else
			{
				m_SpecialUsed=false;
				return -4;
			}
		}
		else if(m_OrcSpecial && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			int res;
			m_SpecialUsed=true;
			m_SpecialUsedTick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*4;
			m_Poisoned=0;
			res=GameServer()->m_pController->DropFlagOrc(this);
			if(res==-1)
			{
				vec2 spawnpos = vec2(100.0f, -60.0f);
				/*if(!GameServer()->m_pController->CanSpawn(this, &spawnpos))
					return -3;
				else
					Character->m_Core.m_Pos=spawnpos;*/
			}
			else
			{
				CCharacter* to = GameServer()->m_apPlayers[res]->GetCharacter();
				CCharacter* from = GetCharacter();
				if(from && to)
				{
					from->m_Core.m_Pos.x=to->m_Core.m_Pos.x;
					from->m_Core.m_Pos.y=to->m_Core.m_Pos.y;
				}
			}			
			return 0;
		}
		else if(m_UndeadSpecial && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			KillCharacter(WEAPON_SELF);
			return 0;
		}
		else if(m_TaurenSpecial && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			m_SpecialUsed=true;
			m_InvincibleStartTick=Server()->Tick();
			m_Invincible=true;
			GameServer()->SendChatTarget(m_ClientID,"Shield used");
			m_Check=true;
			m_SpecialUsedTick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*9;
			return 0;
		}
	}
	else if(m_SpecialUsed)
	{
					
		char buf[128];
		str_format(buf, sizeof(buf), "Special reloading : %d sec",(int)((float)(m_SpecialUsedTick-Server()->Tick())/(float)Server()->TickSpeed())+1);
		GameServer()->SendBroadcast(buf, m_ClientID);
		return 0;
	}
	if(!m_HumanSpecial && !m_OrcSpecial && !m_ElfSpecial && !m_UndeadSpecial && !m_TaurenSpecial)
		return -1;
	else if(!GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		return -2;
	return -3;
}

//Print other players level
bool CPlayer::PrintOtherLvl()
{
	if(!GameServer()->m_pController->IsRpg())
		return false;
	char buf[128];
	char tmprace[30];
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_RaceName != VIDE && GameServer()->m_apPlayers[i]->GetTeam() == m_Team)
		{
			if(GameServer()->m_apPlayers[i]->m_RaceName == ORC)str_format(tmprace,sizeof(tmprace),"ORC");
			else if(GameServer()->m_apPlayers[i]->m_RaceName == UNDEAD)str_format(tmprace,sizeof(tmprace),"UNDEAD");
			else if(GameServer()->m_apPlayers[i]->m_RaceName == ELF)str_format(tmprace,sizeof(tmprace),"ELF");
			else if(GameServer()->m_apPlayers[i]->m_RaceName == HUMAN)str_format(tmprace,sizeof(tmprace),"HUMAN");
			else if(GameServer()->m_apPlayers[i]->m_RaceName == TAUREN)str_format(tmprace,sizeof(tmprace),"TAUREN");
			str_format(buf,sizeof(buf),"%s : race : %s level : %d",Server()->ClientName(i),tmprace,GameServer()->m_apPlayers[i]->m_Lvl);
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
	}
	return true;
}

//Print help
bool CPlayer::PrintHelp()
{
	if(!GameServer()->m_pController->IsRpg())
		return false;
	char buf[128];
	if(m_RaceName != VIDE)
	{
		if(m_RaceName == ORC)
		{
			str_format(buf,sizeof(buf),"ORC:");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Damage : Damage +15/30/45/60%%");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Reload : Fire rate increase each level");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Special : Teleport you to spawn or to your m_Teammates with the flag (lvl 6 required)");
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
		else if(m_RaceName == HUMAN)
		{
			str_format(buf,sizeof(buf),"HUMAN:");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Armor : Armor +15/30/45/60%%");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Mole : 15/30/45/60%% chance to respawn in enemy base");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Special : teleport you where you are aiming at (lvl 6 required)");
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
		else if(m_RaceName == ELF)
		{
			str_format(buf,sizeof(buf),"ELF:");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Poison : Deal 1 damage each second during 2/4/6/8 tick");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Mirror : Reverse 1/2/3/4 damage");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Special : Immobilise the player you are aiming at (lvl 6 required)");
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
		else if(m_RaceName == UNDEAD)
		{
			str_format(buf,sizeof(buf),"UNDEAD:");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Taser: Hook deals 1/2/3/4 damages");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Vampiric: Absorb ennemy hp");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Special : Kamikaze, when you die you explode dealing lot of damage (lvl 6 required)");
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
		else if(m_RaceName == TAUREN)
		{
			str_format(buf,sizeof(buf),"TAUREN:");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Tauren have a native ability wich is healing with grenade launcher(2 hp / sec) range increased by lvl");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Tauren can heal with laser too wich will make a chain heal");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Hot : Healing(hp and armor) over time for 2/4/6/8 ticks with pistol(like a poison)");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Ressurection : 15/30/45/60%% chance to ressurect at the place where one died");
			GameServer()->SendChatTarget(m_ClientID, buf);
			str_format(buf,sizeof(buf),"Special : Shield for 3 sec(damage are reflected)(lvl 6 required)");
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
		return true;
	}
	else
		return false;
}

void CPlayer::CheckSkins(void)
{
	if(m_RaceName == ORC && strcmp(m_TeeInfos.m_SkinName,"orc"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"orc");
	else if(m_RaceName == HUMAN && strcmp(m_TeeInfos.m_SkinName,"human"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"human");
	else if(m_RaceName == UNDEAD && strcmp(m_TeeInfos.m_SkinName,"undead"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"undead");
	else if(m_RaceName == ELF && strcmp(m_TeeInfos.m_SkinName,"elf"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"elf");
	else if(m_RaceName == TAUREN && m_Invincible && strcmp(m_TeeInfos.m_SkinName,"tauren_m_Invincible"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"tauren_m_Invincible");
	else if(m_RaceName == TAUREN && strcmp(m_TeeInfos.m_SkinName,"tauren") && !m_Invincible)
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"tauren");
	else if(m_RaceName == VIDE && strcmp(m_TeeInfos.m_SkinName,"default"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"default");
}

void CPlayer::CheckName(void)
{
	if(!g_Config.m_SvRaceTag)
		return;

	if(m_RaceName == ORC && !strncmp(Server()->ClientName(m_ClientID),"[ORC]",5))
		return;
	else if(m_RaceName == HUMAN && !strncmp(Server()->ClientName(m_ClientID),"[HUM]",5))
		return;
	else if(m_RaceName == UNDEAD && !strncmp(Server()->ClientName(m_ClientID),"[UND]",5))
		return;
	else if(m_RaceName == ELF && !strncmp(Server()->ClientName(m_ClientID),"[ELF]",5))
		return;
	else if(m_RaceName == TAUREN && !strncmp(Server()->ClientName(m_ClientID),"[TAU]",5))
		return;
	else if(m_RaceName == VIDE && !strncmp(Server()->ClientName(m_ClientID),"[___]",5))
		return;
	char newname[MAX_NAME_LENGTH];
	char tmp[MAX_NAME_LENGTH];
	str_copy(newname,Server()->ClientName(m_ClientID),MAX_NAME_LENGTH);
	if(m_RaceName == VIDE)
		str_format(tmp,sizeof(tmp),"[___]");
	else if(m_RaceName == ORC)
		str_format(tmp,sizeof(tmp),"[ORC]");
	else if(m_RaceName == UNDEAD)
		str_format(tmp,sizeof(tmp),"[UND]");
	else if(m_RaceName == HUMAN)
		str_format(tmp,sizeof(tmp),"[HUM]");
	else if(m_RaceName == ELF)
		str_format(tmp,sizeof(tmp),"[ELF]");
	else if(m_RaceName == TAUREN)
		str_format(tmp,sizeof(tmp),"[TAU]");
	strncat(tmp,newname+5,MAX_NAME_LENGTH-7);
	tmp[MAX_NAME_LENGTH-1]=0;
	Server()->SetClientName(m_ClientID, tmp);
}
