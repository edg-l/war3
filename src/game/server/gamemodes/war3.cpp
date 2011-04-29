/* copyright (c) 2007 rajh */
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entities/flag.h>
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "war3.h"
#include "ctf.h"
#include <string.h>
#include <stdio.h>

CGameControllerWAR::CGameControllerWAR(class CGameContext *pGameServer): IGameController(pGameServer)
{
	m_pGameType = "WAR3";
	m_pFlags[0] = 0;
	m_pFlags[1] = 0;
	m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	m_LevelMax=g_Config.m_SvLevelMax;
	DefaultLvlMap();
	LoadXpTable();
}


bool CGameControllerWAR::OnEntity(int Index, vec2 Pos)
{
	if(IGameController::OnEntity(Index, Pos))
		return true;
	
	int Team = -1;
	if(Index == ENTITY_FLAGSTAND_RED) Team = TEAM_RED;
	if(Index == ENTITY_FLAGSTAND_BLUE) Team = TEAM_BLUE;
	if(Team == -1 || m_pFlags[Team])
		return false;
		
	CFlag *f = new CFlag(&GameServer()->m_World, Team);
	f->m_StandPos = Pos;
	f->m_Pos = Pos;
	m_pFlags[Team] = f;
	GameServer()->m_World.InsertEntity(f);
	return true;
}

bool CGameControllerWAR::CanBeMovedOnBalance(int ClientID)
{
	CCharacter* Character = GameServer()->m_apPlayers[ClientID]->GetCharacter();
	if(Character)
	{
		for(int fi = 0; fi < 2; fi++)
		{
			CFlag *F = m_pFlags[fi];
			if(F->m_pCarryingCharacter == Character)
				return false;
		}
	}
	return true;
}

int CGameControllerWAR::OnCharacterDeath(class CCharacter *Victim, class CPlayer *Killer, int Weaponid)
{
	vec2 tempPos=Victim->m_Core.m_Pos;
	IGameController::OnCharacterDeath(Victim, Killer, Weaponid);

	//Xp for killing
	if(Killer && Killer->GetCID() != Victim->GetPlayer()->GetCID())
	{
		Killer->m_Xp+=(Victim->GetPlayer()->m_Lvl*5);
		if(Killer->m_Healed && GameServer()->m_apPlayers[Killer->m_HealFrom])
			GameServer()->m_apPlayers[Killer->m_HealFrom]->m_Xp+=(Victim->GetPlayer()->m_Lvl*5);
	}

	int had_flag = 0;
	
	// drop m_pFlags
	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *f = m_pFlags[fi];
		if(f && Killer && f->m_pCarryingCharacter == Killer->GetCharacter())
			had_flag |= 2;
		if(f && f->m_pCarryingCharacter == Victim)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
			f->m_DropTick = Server()->Tick();
			f->m_pCarryingCharacter = 0;
			f->m_Vel = vec2(0,0);
			
			if(Killer && Killer->GetTeam() != Victim->GetPlayer()->GetTeam())
				Killer->m_Score++;
				
			had_flag |= 1;
		}
	}

	//Exploding undead
	if(Victim->GetPlayer() && Victim->GetPlayer()->m_UndeadSpecial && !Victim->GetPlayer()->m_SpecialUsed && Weaponid != WEAPON_WORLD)
	{
		Victim->GetPlayer()->m_SpecialUsed=true;
		//exploded is used cause WEAPON_EXPLODE has not an unique ID
		Victim->GetPlayer()->m_Exploded=true;
		Victim->GetPlayer()->m_SpecialUsedTick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*3;
		GameServer()->CreateExplosion(tempPos, Victim->GetPlayer()->GetCID(), WEAPON_EXPLODE, false);
		Victim->GetPlayer()->m_Exploded=false;
	}
	//Forgot this one at respawn -_-
	/*else if(Victim->GetPlayer() &&  !Victim->GetPlayer()->m_UndeadSpecial)
	{
		Victim->GetPlayer()->m_SpecialUsed=false;
	}*/

	return had_flag;
}

void CGameControllerWAR::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	pGameDataObj->m_TeamscoreRed = m_aTeamscore[TEAM_RED];
	pGameDataObj->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];

	if(m_pFlags[TEAM_RED])
	{
		if(m_pFlags[TEAM_RED]->m_AtStand)
			pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
		else if(m_pFlags[TEAM_RED]->m_pCarryingCharacter && m_pFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierRed = m_pFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else
			pGameDataObj->m_FlagCarrierRed = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierRed = FLAG_MISSING;
	if(m_pFlags[TEAM_BLUE])
	{
		if(m_pFlags[TEAM_BLUE]->m_AtStand)
			pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
		else if(m_pFlags[TEAM_BLUE]->m_pCarryingCharacter && m_pFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierBlue = m_pFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else
			pGameDataObj->m_FlagCarrierBlue = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierBlue = FLAG_MISSING;
}


void CGameControllerWAR::Tick()
{
	IGameController::Tick();

	DoTeamScoreWincheck();
	
	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *f = m_pFlags[fi];
		
		if(!f)
			continue;
		
		// flag hits death-tile, reset it
		if(GameServer()->Collision()->GetCollisionAt((int)f->m_Pos.x, (int)f->m_Pos.y)&GameServer()->Collision()->COLFLAG_DEATH)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
			f->Reset();
			continue;
		}
		
		//
		if(f->m_pCarryingCharacter)
		{
			// update flag position
			f->m_Pos = f->m_pCarryingCharacter->m_Pos;
			
			if(m_pFlags[fi^1] && m_pFlags[fi^1]->m_AtStand)
			{
				if(distance(f->m_Pos, m_pFlags[fi^1]->m_Pos) < 32)
				{
					// CAPTURE! \o/
					m_aTeamscore[fi^1] += 100;
					f->m_pCarryingCharacter->GetPlayer()->m_Score += 5;
					//Xp
					f->m_pCarryingCharacter->GetPlayer()->m_Xp += 50;
					if(f->m_pCarryingCharacter->GetPlayer()->m_Healed && GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->m_HealFrom])
						GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->m_HealFrom]->m_Xp+=50;

					dbg_msg("game", "flag_capture player='%d:%s'",
						f->m_pCarryingCharacter->GetPlayer()->GetCID(),
						Server()->ClientName(f->m_pCarryingCharacter->GetPlayer()->GetCID()));

					char buf[512];
					float capture_time = (Server()->Tick() - f->m_GrabTick)/(float)Server()->TickSpeed();
					if(capture_time <= 60)
					{
						str_format(buf, sizeof(buf), "the %s flag was captured by %s (%d.%s%d seconds)", fi ? "blue" : "red", Server()->ClientName(f->m_pCarryingCharacter->GetPlayer()->GetCID()), (int)capture_time%60, ((int)(capture_time*100)%100)<10?"0":"", (int)(capture_time*100)%100);
					}
					else
					{
						str_format(buf, sizeof(buf), "the %s flag was captured by %s", fi ? "blue" : "red", Server()->ClientName(f->m_pCarryingCharacter->GetPlayer()->GetCID()));
					}
					GameServer()->SendChat(-1, -2, buf);
					for(int i = 0; i < 2; i++)
						m_pFlags[i]->Reset();
					
					GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
				}
			}			
		}
		else
		{
			CCharacter *close_characters[MAX_CLIENTS];
			int num = GameServer()->m_World.FindEntities(f->m_Pos, CFlag::ms_PhysSize, (CEntity**)close_characters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
			for(int i = 0; i < num; i++)
			{
				if(!close_characters[i]->IsAlive() || close_characters[i]->GetPlayer()->GetTeam() == TEAM_SPECTATORS || GameServer()->Collision()->IntersectLine(f->m_Pos, close_characters[i]->m_Pos, NULL, NULL))
					continue;
				
				if(close_characters[i]->GetPlayer()->GetTeam() == f->m_Team)
				{
					// return the flag
					if(!f->m_AtStand)
					{
						CCharacter *Chr = close_characters[i];
						Chr->GetPlayer()->m_Score += 1;
						//Xp
						Chr->GetPlayer()->m_Xp += 20;
						if(Chr->GetPlayer()->m_Healed && GameServer()->m_apPlayers[Chr->GetPlayer()->m_HealFrom])
							GameServer()->m_apPlayers[Chr->GetPlayer()->m_HealFrom]->m_Xp+=20;

						dbg_msg("game", "flag_return player='%d:%s'",
							Chr->GetPlayer()->GetCID(),
							Server()->ClientName(Chr->GetPlayer()->GetCID()));

						GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
						f->Reset();
					}
				}
				else
				{
					// take the flag
					if(f->m_AtStand)
					{
						m_aTeamscore[fi^1]++;
						f->m_GrabTick = Server()->Tick();
					}
					f->m_AtStand = 0;
					f->m_pCarryingCharacter = close_characters[i];
					f->m_pCarryingCharacter->GetPlayer()->m_Score += 1;
					//Xp
					f->m_pCarryingCharacter->GetPlayer()->m_Xp += 10;
					if(f->m_pCarryingCharacter->GetPlayer()->m_Healed && GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->m_HealFrom])
						GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->m_HealFrom]->m_Xp+=10;

					dbg_msg("game", "flag_grab player='%d:%s'",
						f->m_pCarryingCharacter->GetPlayer()->GetCID(),
						Server()->ClientName(f->m_pCarryingCharacter->GetPlayer()->GetCID()));
					
					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(!GameServer()->m_apPlayers[c])
							continue;
							
						if(GameServer()->m_apPlayers[c]->GetTeam() == fi)
							GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, GameServer()->m_apPlayers[c]->GetCID());
						else
							GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_PL, GameServer()->m_apPlayers[c]->GetCID());
					}
					break;
				}
			}
			
			if(!f->m_pCarryingCharacter && !f->m_AtStand)
			{
				if(Server()->Tick() > f->m_DropTick + Server()->TickSpeed()*30)
				{
					GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
					f->Reset();
				}
				else
				{
					f->m_Vel.y += GameServer()->m_World.m_Core.m_Tuning.m_Gravity;
					GameServer()->Collision()->MoveBox(&f->m_Pos, &f->m_Vel, vec2(f->ms_PhysSize, f->ms_PhysSize), 0.5f);
				}
			}
		}
	}
}

bool CGameControllerWAR::IsRpg()
{
	return true;
}

//Level up stuff
void CGameControllerWAR::OnLevelUp(CPlayer *Player)
{
	if(Player->m_RaceName != VIDE && !Player->m_LevelMax && Player->m_Lvl < m_LevelMax)
	{
		Player->m_Xp-=Player->m_NextLvl;
		if(Player->m_Xp < 0)Player->m_Xp=0;
		Player->m_Lvl++;
		GameServer()->CreateSoundGlobal(SOUND_TEE_CRY, Player->GetCID());
		Player->m_NextLvl = InitXp(Player->m_Lvl);
		Player->m_Leveled++;
		if(Player->m_Lvl==m_LevelMax)
			Player->m_LevelMax=true;
		DisplayStats(Player, Player);
	}
	else if(Player->m_RaceName == VIDE && Player->GetTeam() != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Please choose a race\n say \"/race name\"");
		GameServer()->SendBroadcast(buf, Player->GetCID());
	}
	else if(!Player->m_LevelMax && Player->m_Lvl >= m_LevelMax)
	{
		Player->m_Lvl=m_LevelMax;
		GameServer()->CreateSoundGlobal(SOUND_TEE_CRY, Player->GetCID());
		Player->m_LevelMax=true;
		DisplayStats(Player,Player);
	}
}

//Display current stats
void CGameControllerWAR::DisplayStats(CPlayer *Player,CPlayer *From)
{
	char buf[128];
	char tmp[128];
	if(Player->GetCID() == From->GetCID())
	{
			str_format(buf, sizeof(buf), "Stats : (%d point to spend)",Player->m_Leveled);
	}
	else if(Player->m_Lvl >= m_LevelMax)
		str_format(buf, sizeof(buf), "Final lvl Stats : (%d point to spend)",Player->m_Leveled);
	else
		str_format(buf, sizeof(buf), "Stats : ");
	if(Player->m_RaceName == ORC)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Damage lvl %d/4",Player->m_OrcDmg);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Reload lvl %d/4",Player->m_OrcReload);
		strcat(buf,tmp);
		if(Player->m_Lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport Backup %d/1",Player->m_OrcSpecial?1:0);
			strcat(buf,tmp);
		}
		
	}
	else if(Player->m_RaceName == ELF)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Poison lvl %d/4",Player->m_ElfPoison);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Mirror damage lvl %d/4",Player->m_ElfMirror);
		strcat(buf,tmp);
		if(Player->m_Lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Immobilise %d/1",Player->m_ElfSpecial?1:0);
			strcat(buf,tmp);
		}
	}
	else if(Player->m_RaceName == UNDEAD)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Taser lvl %d/4",Player->m_UndeadTaser);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Vampiric damage lvl %d/4",Player->m_UndeadVamp);
		strcat(buf,tmp);
		if(Player->m_Lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Kamikaz %d/1",Player->m_UndeadSpecial?1:0);
			strcat(buf,tmp);
		}
	}
	else if(Player->m_RaceName == HUMAN)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Armor lvl %d/4",Player->m_HumanArmor);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Mole chance lvl %d/4",Player->m_HumanMole);
		strcat(buf,tmp);
		if(Player->m_Lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport %d/1",Player->m_HumanSpecial?1:0);
			strcat(buf,tmp);
		}
	}
	else if(Player->m_RaceName == TAUREN)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Hot lvl %d/4",Player->m_TaurenHot);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Ressurect chance lvl %d/4",Player->m_TaurenRessurect);
		strcat(buf,tmp);
		if(Player->m_Lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Shield %d/1",Player->m_TaurenSpecial?1:0);
			strcat(buf,tmp);
		}
	}
	GameServer()->SendBroadcast(buf, From->GetCID());
}	

//Fix orc flag
int CGameControllerWAR::DropFlagOrc(CPlayer *Player)
{
	CFlag *f = m_pFlags[!Player->GetTeam()];
	if(f && f->m_pCarryingCharacter && f->m_pCarryingCharacter == Player->GetCharacter())
	{
		GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
		f->m_DropTick = Server()->Tick();
		f->m_pCarryingCharacter = 0;
		f->m_Vel = vec2(0,0);
		return -1;
	}
	else if(f && f->m_pCarryingCharacter && f->m_pCarryingCharacter != Player->GetCharacter())
		return f->m_pCarryingCharacter->GetPlayer()->GetCID();
	return -1;
}

//Load custom xp table
void CGameControllerWAR::LoadXpTable()
{
		FILE *xptable;
		int i;
		xptable=fopen ("xp_table.war","r");
		if(xptable == NULL)
		{
			perror("Fopen (file exist ?)");
			dbg_msg("WAR3","Faild to open xp_table.war. File exist ?");
			return;
		}
		for(i=0;i < m_LevelMax;i++)
		{
			fscanf(xptable,"%d\n",&m_apLvlMap[i]);
		}
		fclose(xptable);
}

//Yes its ugly and ?
int CGameControllerWAR::InitXp(int Level)
{
	if(Level < 1 || Level > m_LevelMax)
		return 0;
	return m_apLvlMap[Level-1];
}

int CGameControllerWAR::GetLevelMax()
{
	return m_LevelMax;
}

void CGameControllerWAR::OnCharacterSpawn(class CCharacter *Chr)
{
	// Ressurection with 5 hp
	if(Chr->GetPlayer()->m_RaceName == TAUREN && Chr->GetPlayer()->m_Ressurected)
		Chr->m_Health = 5;
	else
		Chr->m_Health = 10;
	
	// give default weapons
	Chr->m_aWeapons[WEAPON_HAMMER].m_Got = 1;
	Chr->m_aWeapons[WEAPON_HAMMER].m_Ammo = -1;
	Chr->m_aWeapons[WEAPON_GUN].m_Got = 1;
	Chr->m_aWeapons[WEAPON_GUN].m_Ammo = 10;
}

void CGameControllerWAR::DefaultLvlMap()
{
	//Init xp table
	m_apLvlMap[0]=10;
	m_apLvlMap[1]=50;
	m_apLvlMap[2]=100;
	m_apLvlMap[3]=200;
	m_apLvlMap[4]=300;
	m_apLvlMap[5]=500;
	m_apLvlMap[6]=700;
	m_apLvlMap[7]=1200;
	m_apLvlMap[8]=2000;
	m_apLvlMap[9]=0; /* Not used */
}
