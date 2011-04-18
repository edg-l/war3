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

	if(!GameServer()->m_pController->is_rpg())
		return;

	//Leveling up gratz !
	if(xp>nextlvl && !levelmax && race_name != VIDE)
		GameServer()->m_pController->on_level_up(this);

	//Check for special reload
	if(Server()->Tick()-special_used_tick >= 0 && special_used)
	{
		special_used=false;
		GameServer()->CreateSoundGlobal(SOUND_HOOK_LOOP, m_ClientID);
	}
	//Invicible (not used)
	if(invincible && Server()->Tick()-invincible_start_tick > Server()->TickSpeed()*3)
	{
		invincible=false;
		check=true;
	}
	//Dumb people don't choose race ? oO
	if(race_name==VIDE && Server()->Tick()%(Server()->TickSpeed()*2)==0 && m_Team != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Choose a race say \"/race undead/orc/human/elf/tauren\"");
		GameServer()->SendBroadcast(buf, m_ClientID);
	}

	//Forcing a race
	if(g_Config.m_SvForceRace && m_Team != -1 && race_name == VIDE && Server()->Tick()-force_race_tick >= Server()->TickSpeed()*g_Config.m_SvForceRace*60)
	{
		int i,force_race=-1;
		int nbRace[NBRACE]={0};
		for (i=0;i < MAX_CLIENTS;i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->race_name != VIDE)
				nbRace[GameServer()->m_apPlayers[i]->race_name]++;
		}
		for (i=1;i < NBRACE;i++)
		{
			if(force_race == -1 && i != TAUREN || nbRace[i]<nbRace[force_race] && i != TAUREN)
				force_race=i;
		}
		char buf[128];
		str_format(buf, sizeof(buf), "Race forced");
		GameServer()->SendBroadcast(buf, m_ClientID);
		init_rpg();
		race_name = force_race;
		KillCharacter(-1);
		m_Score++;
		dbg_msg("war3","Forcing player : %s to race : %d",Server()->ClientName(m_ClientID),race_name);
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
	
	if(m_Team==-1)init_rpg();
	if(race_name == TAUREN)
	{
		int count_tauren=0;
		int i;
		for(i=0;i < MAX_CLIENTS;i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetCID() != -1 && GameServer()->m_apPlayers[i]->race_name == TAUREN && GameServer()->m_apPlayers[i]->GetTeam() == m_Team && GameServer()->m_apPlayers[i]->GetCID() != m_ClientID)
				count_tauren++;
		}
		if(count_tauren >= g_Config.m_SvMaxTauren)
		{
			race_name=HUMAN;
			reset_all();
		}
	}
	check=true;

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
void CPlayer::init_rpg()
{
	if(!GameServer()->m_pController->is_rpg())
		return;
	lvl = g_Config.m_SvLevelStart;
	nextlvl = GameServer()->m_pController->init_xp(lvl);	
	xp = 0;
	leveled=lvl-1;
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
	special_used=false;
	race_name=VIDE;
	force_race_tick=Server()->Tick();
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
void CPlayer::reset_all()
{	
	if(!GameServer()->m_pController->is_rpg())
		return;
	if(lvl < GameServer()->m_pController->get_level_max())levelmax=false;
	else levelmax=true;
	leveled=lvl-1;
	nextlvl = GameServer()->m_pController->init_xp(lvl);
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
bool CPlayer::choose_ability(int choice)
{
	if(!GameServer()->m_pController->is_rpg())
		return false;
	char buf[128];
	if(race_name==ORC)
	{
		if(choice == 1 && orc_dmg<4)
		{
			orc_dmg++;
			str_format(buf, sizeof(buf), "Damage + %d%%",orc_dmg*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice == 2 && orc_reload<4)
		{
			orc_reload++;
			str_format(buf, sizeof(buf), "Reload faster + %d",orc_reload);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice==3 && lvl >=6 && !orc_special)
		{
			orc_special=true;
			str_format(buf, sizeof(buf), "Teleport Backup enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
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
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice == 2 && human_mole<4)
		{
			human_mole++;
			str_format(buf, sizeof(buf), "Mole chance = %d%%",human_mole*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice==3 && lvl >=6 && !human_special)
		{
			human_special=true;
			str_format(buf, sizeof(buf), "Teleport enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
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
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice == 2 && elf_mirror<4)
		{
			elf_mirror++;
			str_format(buf, sizeof(buf), "Mirror damage + %d",elf_mirror);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice==3 && lvl >=6 && !elf_special)
		{
			elf_special=true;
			str_format(buf, sizeof(buf), "Immobilise enable");
			GameServer()->SendBroadcast(buf, m_ClientID);
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
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice == 2 && undead_vamp<4)
		{
			undead_vamp++;
			str_format(buf, sizeof(buf), "Vampiric + %d",undead_vamp);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice==3 && lvl >=6 && !undead_special)
		{
			undead_special=true;
			str_format(buf, sizeof(buf), "Kamikaze enabled");
			GameServer()->SendBroadcast(buf, m_ClientID);
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
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice == 2 && tauren_ressurect<4)
		{
			tauren_ressurect++;
			str_format(buf, sizeof(buf), "Ressurection + %d%%",tauren_ressurect*15);
			GameServer()->SendBroadcast(buf, m_ClientID);
			return true;
		}
		else if(choice==3 && lvl >=6 && !tauren_special)
		{
			tauren_special=true;
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
void CPlayer::vamp(int amount)
{
	if(!GameServer()->m_pController->is_rpg())
		return;
	if(Character)
	{	
		if(amount > undead_vamp)
			amount=undead_vamp;
		Character->IncreaseHealth(amount);
	}
}

//Function for using special
int CPlayer::use_special()
{
	if(!GameServer()->m_pController->is_rpg())
		return -3;
	if(!special_used)
	{
		if(elf_special && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			special_used=true;
			special_used_tick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*2;
			vec2 direction = normalize(vec2(Character->m_LatestInput.m_TargetX, Character->m_LatestInput.m_TargetY));
			vec2 at;
			CCharacter *hit;
			vec2 to=Character->m_Core.m_Pos+direction*700;
			GameServer()->Collision()->IntersectLine(Character->m_Core.m_Pos, to, 0x0, &to);
			hit = GameServer()->m_World.IntersectCharacter(Character->m_Core.m_Pos, to, 0.0f, at, Character);
			//if(hit)dbg_msg("test","hit : %d",hit->GetPlayer()->m_ClientID);
			if(!hit || (hit->GetPlayer()->m_Team == m_Team && !g_Config.m_SvTeamdamage))
				special_used=false;
			else if(hit->GetPlayer()->m_Team != m_Team || g_Config.m_SvTeamdamage)
				hit->stucked=Server()->Tick();
			return 0;
		}
		else if(human_special && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			special_used=true;
			GetCharacter()->stucked=0;
			special_used_tick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime;
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
				special_used=false;
				return -4;
			}
		}
		else if(orc_special && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			int res;
			special_used=true;
			special_used_tick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*4;
			poisoned=0;
			res=GameServer()->m_pController->drop_flag_orc(this);
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
		else if(undead_special && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			KillCharacter(WEAPON_SELF);
			return 0;
		}
		else if(tauren_special && GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		{
			special_used=true;
			invincible_start_tick=Server()->Tick();
			invincible=true;
			GameServer()->SendChatTarget(m_ClientID,"Shield used");
			check=true;
			special_used_tick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*9;
			return 0;
		}
	}
	else if(special_used)
	{
					
		char buf[128];
		str_format(buf, sizeof(buf), "Special reloading : %d sec",(int)((float)(special_used_tick-Server()->Tick())/(float)Server()->TickSpeed())+1);
		GameServer()->SendBroadcast(buf, m_ClientID);
		return 0;
	}
	if(!human_special && !orc_special && !elf_special && !undead_special && !tauren_special)
		return -1;
	else if(!GameServer()->m_apPlayers[m_ClientID]->GetCharacter())
		return -2;
	return -3;
}

//Print other players level
bool CPlayer::print_otherlvl()
{
	if(!GameServer()->m_pController->is_rpg())
		return false;
	char buf[128];
	char tmprace[30];
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->race_name != VIDE && GameServer()->m_apPlayers[i]->GetTeam() == m_Team)
		{
			if(GameServer()->m_apPlayers[i]->race_name == ORC)str_format(tmprace,sizeof(tmprace),"ORC");
			else if(GameServer()->m_apPlayers[i]->race_name == UNDEAD)str_format(tmprace,sizeof(tmprace),"UNDEAD");
			else if(GameServer()->m_apPlayers[i]->race_name == ELF)str_format(tmprace,sizeof(tmprace),"ELF");
			else if(GameServer()->m_apPlayers[i]->race_name == HUMAN)str_format(tmprace,sizeof(tmprace),"HUMAN");
			else if(GameServer()->m_apPlayers[i]->race_name == TAUREN)str_format(tmprace,sizeof(tmprace),"TAUREN");
			str_format(buf,sizeof(buf),"%s : race : %s level : %d",Server()->ClientName(i),tmprace,GameServer()->m_apPlayers[i]->lvl);
			GameServer()->SendChatTarget(m_ClientID, buf);
		}
	}
	return true;
}

//Print help
bool CPlayer::print_help()
{
	if(!GameServer()->m_pController->is_rpg())
		return false;
	char buf[128];
	if(race_name != VIDE)
	{
		if(race_name == ORC)
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
		else if(race_name == HUMAN)
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
		else if(race_name == ELF)
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
		else if(race_name == UNDEAD)
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
		else if(race_name == TAUREN)
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

void CPlayer::check_skins(void)
{
	if(race_name == ORC && strcmp(m_TeeInfos.m_SkinName,"orc"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"orc");
	else if(race_name == HUMAN && strcmp(m_TeeInfos.m_SkinName,"human"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"human");
	else if(race_name == UNDEAD && strcmp(m_TeeInfos.m_SkinName,"undead"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"undead");
	else if(race_name == ELF && strcmp(m_TeeInfos.m_SkinName,"elf"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"elf");
	else if(race_name == TAUREN && invincible && strcmp(m_TeeInfos.m_SkinName,"tauren_invincible"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"tauren_invincible");
	else if(race_name == TAUREN && strcmp(m_TeeInfos.m_SkinName,"tauren") && !invincible)
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"tauren");
	else if(race_name == VIDE && strcmp(m_TeeInfos.m_SkinName,"default"))
		str_format(m_TeeInfos.m_SkinName,sizeof(m_TeeInfos.m_SkinName),"default");
}

void CPlayer::check_name(void)
{
	if(!g_Config.m_SvRaceTag)
		return;

	if(race_name == ORC && !strncmp(Server()->ClientName(m_ClientID),"[ORC]",5))
		return;
	else if(race_name == HUMAN && !strncmp(Server()->ClientName(m_ClientID),"[HUM]",5))
		return;
	else if(race_name == UNDEAD && !strncmp(Server()->ClientName(m_ClientID),"[UND]",5))
		return;
	else if(race_name == ELF && !strncmp(Server()->ClientName(m_ClientID),"[ELF]",5))
		return;
	else if(race_name == TAUREN && !strncmp(Server()->ClientName(m_ClientID),"[TAU]",5))
		return;
	else if(race_name == VIDE && !strncmp(Server()->ClientName(m_ClientID),"[___]",5))
		return;
	char newname[MAX_NAME_LENGTH];
	char tmp[MAX_NAME_LENGTH];
	str_copy(newname,Server()->ClientName(m_ClientID),MAX_NAME_LENGTH);
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
	Server()->SetClientName(m_ClientID, tmp);
}
