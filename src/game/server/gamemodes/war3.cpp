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
	flags[0] = 0;
	flags[1] = 0;
	m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	level_max=g_Config.m_SvLevelMax;
	default_lvlmap();
	load_xp_table();
}


bool CGameControllerWAR::OnEntity(int index, vec2 pos)
{
	if(IGameController::OnEntity(index, pos))
		return true;
	
	int team = -1;
	if(index == ENTITY_FLAGSTAND_RED) team = TEAM_RED;
	if(index == ENTITY_FLAGSTAND_BLUE) team = TEAM_BLUE;
	if(team == -1 || flags[team])
		return false;
		
	CFlag *f = new CFlag(&GameServer()->m_World, team);
	f->m_StandPos = pos;
	f->m_Pos = pos;
	flags[team] = f;
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
			CFlag *F = flags[fi];
			if(F->m_pCarryingCharacter == Character)
				return false;
		}
	}
	return true;
}

int CGameControllerWAR::OnCharacterDeath(class CCharacter *victim, class CPlayer *killer, int weaponid)
{
	vec2 tempPos=victim->m_Core.m_Pos;
	IGameController::OnCharacterDeath(victim, killer, weaponid);

	//Xp for killing
	if(killer && killer->GetCID() != victim->GetPlayer()->GetCID())
	{
		killer->xp+=(victim->GetPlayer()->lvl*5);
		if(killer->healed && GameServer()->m_apPlayers[killer->heal_from])
			GameServer()->m_apPlayers[killer->heal_from]->xp+=(victim->GetPlayer()->lvl*5);
	}

	int had_flag = 0;
	
	// drop flags
	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *f = flags[fi];
		if(f && killer && f->m_pCarryingCharacter == killer->GetCharacter())
			had_flag |= 2;
		if(f && f->m_pCarryingCharacter == victim)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
			f->m_DropTick = Server()->Tick();
			f->m_pCarryingCharacter = 0;
			f->m_Vel = vec2(0,0);
			
			if(killer && killer->GetTeam() != victim->GetPlayer()->GetTeam())
				killer->m_Score++;
				
			had_flag |= 1;
		}
	}

	//Exploding undead
	if(victim->GetPlayer() && victim->GetPlayer()->undead_special && !victim->GetPlayer()->special_used && weaponid != WEAPON_WORLD)
	{
		victim->GetPlayer()->special_used=true;
		//exploded is used cause WEAPON_EXPLODE has not an unique ID
		victim->GetPlayer()->exploded=true;
		victim->GetPlayer()->special_used_tick=Server()->Tick()+Server()->TickSpeed()*g_Config.m_SvSpecialTime*3;
		GameServer()->CreateExplosion(tempPos, victim->GetPlayer()->GetCID(), WEAPON_EXPLODE, false);
		victim->GetPlayer()->exploded=false;
	}
	//Forgot this one at respawn -_-
	/*else if(victim->GetPlayer() &&  !victim->GetPlayer()->undead_special)
	{
		victim->GetPlayer()->special_used=false;
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

	if(flags[TEAM_RED])
	{
		if(flags[TEAM_RED]->m_AtStand)
			pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
		else if(flags[TEAM_RED]->m_pCarryingCharacter && flags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierRed = flags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else
			pGameDataObj->m_FlagCarrierRed = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierRed = FLAG_MISSING;
	if(flags[TEAM_BLUE])
	{
		if(flags[TEAM_BLUE]->m_AtStand)
			pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
		else if(flags[TEAM_BLUE]->m_pCarryingCharacter && flags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierBlue = flags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
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
		CFlag *f = flags[fi];
		
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
			
			if(flags[fi^1] && flags[fi^1]->m_AtStand)
			{
				if(distance(f->m_Pos, flags[fi^1]->m_Pos) < 32)
				{
					// CAPTURE! \o/
					m_aTeamscore[fi^1] += 100;
					f->m_pCarryingCharacter->GetPlayer()->m_Score += 5;
					//Xp
					f->m_pCarryingCharacter->GetPlayer()->xp += 50;
					if(f->m_pCarryingCharacter->GetPlayer()->healed && GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->heal_from])
						GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->heal_from]->xp+=50;

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
						flags[i]->Reset();
					
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
						CCharacter *chr = close_characters[i];
						chr->GetPlayer()->m_Score += 1;
						//Xp
						chr->GetPlayer()->xp += 20;
						if(chr->GetPlayer()->healed && GameServer()->m_apPlayers[chr->GetPlayer()->heal_from])
							GameServer()->m_apPlayers[chr->GetPlayer()->heal_from]->xp+=20;

						dbg_msg("game", "flag_return player='%d:%s'",
							chr->GetPlayer()->GetCID(),
							Server()->ClientName(chr->GetPlayer()->GetCID()));

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
					f->m_pCarryingCharacter->GetPlayer()->xp += 10;
					if(f->m_pCarryingCharacter->GetPlayer()->healed && GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->heal_from])
						GameServer()->m_apPlayers[f->m_pCarryingCharacter->GetPlayer()->heal_from]->xp+=10;

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

bool CGameControllerWAR::is_rpg()
{
	return true;
}

//Level up stuff
void CGameControllerWAR::on_level_up(CPlayer *player)
{
	if(player->race_name != VIDE && !player->levelmax && player->lvl < level_max)
	{
		player->xp-=player->nextlvl;
		if(player->xp < 0)player->xp=0;
		player->lvl++;
		GameServer()->CreateSoundGlobal(SOUND_TEE_CRY, player->GetCID());
		player->nextlvl = init_xp(player->lvl);
		player->leveled++;
		if(player->lvl==level_max)
			player->levelmax=true;
		display_stats(player,player);
	}
	else if(player->race_name == VIDE && player->GetTeam() != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Please choose a race\n say \"/race name\"");
		GameServer()->SendBroadcast(buf, player->GetCID());
	}
	else if(!player->levelmax && player->lvl >= level_max)
	{
		player->lvl=level_max;
		GameServer()->CreateSoundGlobal(SOUND_TEE_CRY, player->GetCID());
		player->levelmax=true;
		display_stats(player,player);
	}
}

//Display current stats
void CGameControllerWAR::display_stats(CPlayer *player,CPlayer *from)
{
	char buf[128];
	char tmp[128];
	if(player->GetCID() == from->GetCID())
	{
			str_format(buf, sizeof(buf), "Stats : (%d point to spend)",player->leveled);
	}
	else if(player->lvl >= level_max)
		str_format(buf, sizeof(buf), "Final lvl Stats : (%d point to spend)",player->leveled);
	else
		str_format(buf, sizeof(buf), "Stats : ");
	if(player->race_name == ORC)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Damage lvl %d/4",player->orc_dmg);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Reload lvl %d/4",player->orc_reload);
		strcat(buf,tmp);
		if(player->lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport Backup %d/1",player->orc_special?1:0);
			strcat(buf,tmp);
		}
		
	}
	else if(player->race_name == ELF)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Poison lvl %d/4",player->elf_poison);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Mirror damage lvl %d/4",player->elf_mirror);
		strcat(buf,tmp);
		if(player->lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Immobilise %d/1",player->elf_special?1:0);
			strcat(buf,tmp);
		}
	}
	else if(player->race_name == UNDEAD)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Taser lvl %d/4",player->undead_taser);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Vampiric damage lvl %d/4",player->undead_vamp);
		strcat(buf,tmp);
		if(player->lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Kamikaz %d/1",player->undead_special?1:0);
			strcat(buf,tmp);
		}
	}
	else if(player->race_name == HUMAN)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Armor lvl %d/4",player->human_armor);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Mole chance lvl %d/4",player->human_mole);
		strcat(buf,tmp);
		if(player->lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport %d/1",player->human_special?1:0);
			strcat(buf,tmp);
		}
	}
	else if(player->race_name == TAUREN)
	{
		str_format(tmp,sizeof(tmp),"\n1 : Hot lvl %d/4",player->tauren_hot);
		strcat(buf,tmp);
		str_format(tmp,sizeof(tmp),"\n2 : Ressurect chance lvl %d/4",player->tauren_ressurect);
		strcat(buf,tmp);
		if(player->lvl >= 6)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Shield %d/1",player->tauren_special?1:0);
			strcat(buf,tmp);
		}
	}
	GameServer()->SendBroadcast(buf, from->GetCID());
}	

//Fix orc flag
int CGameControllerWAR::drop_flag_orc(CPlayer *player)
{
	CFlag *f = flags[!player->GetTeam()];
	if(f && f->m_pCarryingCharacter && f->m_pCarryingCharacter == player->GetCharacter())
	{
		GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
		f->m_DropTick = Server()->Tick();
		f->m_pCarryingCharacter = 0;
		f->m_Vel = vec2(0,0);
		return -1;
	}
	else if(f && f->m_pCarryingCharacter && f->m_pCarryingCharacter != player->GetCharacter())
		return f->m_pCarryingCharacter->GetPlayer()->GetCID();
	return -1;
}

//Load custom xp table
void CGameControllerWAR::load_xp_table()
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
		for(i=0;i < level_max;i++)
		{
			fscanf(xptable,"%d\n",&lvlmap[i]);
		}
		fclose(xptable);
}

//Yes its ugly and ?
int CGameControllerWAR::init_xp(int level)
{
	if(level < 1 || level > level_max)
		return 0;
	return lvlmap[level-1];
}

int CGameControllerWAR::get_level_max()
{
	return level_max;
}

void CGameControllerWAR::OnCharacterSpawn(class CCharacter *chr)
{
	// Ressurection with 5 hp
	if(chr->GetPlayer()->race_name == TAUREN && chr->GetPlayer()->ressurected)
		chr->m_Health = 5;
	else
		chr->m_Health = 10;
	
	// give default weapons
	chr->m_aWeapons[WEAPON_HAMMER].m_Got = 1;
	chr->m_aWeapons[WEAPON_HAMMER].m_Ammo = -1;
	chr->m_aWeapons[WEAPON_GUN].m_Got = 1;
	chr->m_aWeapons[WEAPON_GUN].m_Ammo = 10;
}

void CGameControllerWAR::default_lvlmap()
{
	//Init xp table
	lvlmap[0]=10;
	lvlmap[1]=50;
	lvlmap[2]=100;
	lvlmap[3]=200;
	lvlmap[4]=300;
	lvlmap[5]=500;
	lvlmap[6]=700;
	lvlmap[7]=1200;
	lvlmap[8]=2000;
	lvlmap[9]=0; /* Not used */
}