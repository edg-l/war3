/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;
	
	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
	
	m_pPlayer = pPlayer;
	m_Pos = Pos;
	
	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));
	
	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	//Human respawn with armor.
	if(GameServer()->m_pController->IsRpg())
		m_Armor=m_pPlayer->m_HumanArmor*2;

	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	//Reset players power (not sure it should be here)
	if(GameServer()->m_pController->IsRpg())
		m_pPlayer->InitRpg();
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;
		
	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);
	
	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;
	
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;
		if(m_ActiveWeapon == WEAPON_NINJA)
			m_ActiveWeapon = WEAPON_GUN;
			
		SetWeapon(m_ActiveWeapon);
		return;
	}
	
	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);
		
		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;
					
				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];
					
				aEnts[i]->TakeDamage(vec2(0, 10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}
		
		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;
	
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;
	
	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0)
		return;
		
	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
	
	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;

	//Orc at reload lvl 3 can FullAuto with gun
	if(m_ActiveWeapon == WEAPON_GUN && m_pPlayer->m_OrcReload == 4)
		FullAuto = true;
 
	if(m_ActiveWeapon != WEAPON_GRENADE && m_pPlayer && m_pPlayer->m_RaceName == TAUREN && m_pPlayer->m_StartedHeal != -1)
	{
		char buf[128];
		if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal] && GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->GetCharacter())
		{
			str_format(buf,sizeof(buf),"Stopped healing (wrong weapon)");
			GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
			GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
			m_pPlayer->m_StartedHeal=-1;
		}					
		else if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal])
			{
			str_format(buf,sizeof(buf),"Stopped healing (the healed character is dead)");
			GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
			GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
			m_pPlayer->m_StartedHeal=-1;
		}
		else
		{
			str_format(buf,sizeof(buf),"Stopped healing ");
			GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
			m_pPlayer->m_StartedHeal=-1;
		}	
		return;
	}

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;
		
	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;
		
	if(!WillFire)
		return;
		
	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		if(m_pPlayer && m_pPlayer->m_RaceName == TAUREN && m_pPlayer->m_StartedHeal != -1)
		{
			char buf[128];
			if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal] && GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->GetCharacter())
			{
				str_format(buf,sizeof(buf),"Stopped healing (you didn't hold fire)");
				GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
				GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
				m_pPlayer->m_StartedHeal=-1;
			}					
			else if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal])
				{
				str_format(buf,sizeof(buf),"Stopped healing (the healed character is dead)");
				GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
				GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
				m_pPlayer->m_StartedHeal=-1;
			}
			else
			{
				str_format(buf,sizeof(buf),"Stopped healing ");
				GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
				m_pPlayer->m_StartedHeal=-1;
			}	
		}
		return;
	}
	
	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;
	
	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects Hit
			m_NumObjectsHit = 0;
			GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);
			
			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts, 
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];
				
				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);
				
				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);
					
				pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
					m_pPlayer->GetCID(), m_ActiveWeapon);
				Hits++;
			}
			
			// if we Hit anything, we have to wait for the reload
			if(Hits)
				m_ReloadTimer = Server()->TickSpeed()/3;
			
		} break;

		case WEAPON_GUN:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, 0, 0, -1, WEAPON_GUN);
				
			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			
			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
				
			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
	
			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;
		
		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;
			//Orc at damage lvl 3 fire more bullet with shotgun
			if(m_pPlayer->m_OrcDmg>=3 && (GameServer()->m_pController)->IsRpg())ShotSpread = 3;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);
			
			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);
					
				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);
				
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
			}

			Server()->SendMsg(&Msg, 0,m_pPlayer->GetCID());					
			
			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			if(m_pPlayer && m_pPlayer->m_RaceName == TAUREN && m_pPlayer->m_StartedHeal == -1)
			{
				vec2 direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
				vec2 at;
				CCharacter *hit;
				char buf[128];
				vec2 to=m_Core.m_Pos+direction*150*m_pPlayer->m_Lvl;
				GameServer()->Collision()->IntersectLine(m_Core.m_Pos, to, 0x0, &to);
				hit = GameServer()->m_World.IntersectCharacter(m_Core.m_Pos, to, 0.0f, at, this);
				if(hit && hit->m_pPlayer->GetTeam() == m_pPlayer->GetTeam())
				{
					hit->m_pPlayer->m_Healed=true;
					hit->m_pPlayer->m_HealTick=Server()->Tick();
					m_pPlayer->m_StartedHeal=hit->m_pPlayer->GetCID();
					hit->m_pPlayer->m_HealFrom=m_pPlayer->GetCID();
					str_format(buf,sizeof(buf),"Started healing %s",Server()->ClientName(hit->m_pPlayer->GetCID()));
					GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
				}
			}
			else if(m_pPlayer && m_pPlayer->m_RaceName == TAUREN && m_pPlayer->m_StartedHeal != -1)
			{
				char buf[128];
				int dist;
				if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal] && GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->GetCharacter())
				{
					dist=distance(m_Pos,GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->GetCharacter()->m_Pos);
					if(dist > 150*m_pPlayer->m_Lvl)
					{
						str_format(buf,sizeof(buf),"Stopped healing (You are too far from the healed character)");
						GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
						GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
						m_pPlayer->m_StartedHeal=-1;
					}
				}					
				else if(GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal])
				{
					str_format(buf,sizeof(buf),"Stopped healing  (the healed character is dead)");
					GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
					GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
					m_pPlayer->m_StartedHeal=-1;
				}
				else
				{
					str_format(buf,sizeof(buf),"Stopped healing ");
					GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
					m_pPlayer->m_StartedHeal=-1;
				}	
			}
			else
			{
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
					m_pPlayer->GetCID(),
					ProjStartPos,
					Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
					1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);
				
				CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
				Msg.AddInt(1);
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
				Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
				
				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
		} break;
		
		case WEAPON_RIFLE:
		{
			if(m_pPlayer->m_RaceName == TAUREN)
			{
				m_pPlayer->m_Bounces=4;
				m_pPlayer->m_LastHealed=-1;
				new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
			else
			{
				new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
		} break;
		
		case WEAPON_NINJA:
		{
			// reset Hit objects
			m_NumObjectsHit = 0;
			
			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
		} break;
		
	}
	
	m_AttackTick = Server()->Tick();
	
	if((m_aWeapons[m_ActiveWeapon].m_Ammo > 0 && m_pPlayer->m_RaceName != TAUREN) || (m_pPlayer->m_RaceName == TAUREN && m_ActiveWeapon!=WEAPON_GRENADE && m_aWeapons[m_ActiveWeapon].m_Ammo > 0)) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;
	

	//Orc reload stuff
 	if((GameServer()->m_pController)->IsRpg() && m_pPlayer->m_OrcReload != 0 && !m_ReloadTimer)
	{
		switch(m_pPlayer->m_OrcReload)
		{
			case 1:
		 		m_ReloadTimer=(g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() )/ 1150;
				break;
			case 2:
		 		m_ReloadTimer=(g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() )/ 1300;
				break;
			case 3:
		 		m_ReloadTimer=(g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() )/ 1450;
				break;
			case 4:
		 		m_ReloadTimer=(g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() )/ 1600;
				break;
		}
	}
	if((!m_ReloadTimer && m_pPlayer->m_RaceName != TAUREN) || (m_pPlayer->m_RaceName == TAUREN && m_ActiveWeapon != WEAPON_GRENADE && m_ActiveWeapon != WEAPON_RIFLE))
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
	else if(!m_ReloadTimer && m_pPlayer->m_RaceName == TAUREN && m_ActiveWeapon == WEAPON_GRENADE)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 3000;
	else if(!m_ReloadTimer && m_pPlayer->m_RaceName == TAUREN && m_ActiveWeapon == WEAPON_RIFLE)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed()/ 250;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();
	
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(AmmoRegenTime)
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 10);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}
	
	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{	
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;
	
	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();
		
	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;
	
	// or are not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;	
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));
	
	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}
	
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::Tick()
{
	if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());
		
		m_pPlayer->m_ForceBalanced = false;
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);
	
	// handle death-tiles and leaving gamelayer
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}
	
	// handle Weapons
	HandleWeapons();

	// Previnput
	m_PrevInput = m_Input;

	//Poison Hot tick ect
	if((GameServer()->m_pController)->IsRpg())
	{
		if(m_pPlayer->m_Poisoned && Server()->Tick()-m_pPlayer->m_PoisonStartTick > Server()->TickSpeed() && GameServer()->m_apPlayers[m_pPlayer->m_Poisoner])
		{
			m_pPlayer->m_PoisonStartTick=Server()->Tick();
			TakeDamage(vec2 (0,0),m_pPlayer->m_Poisoned,GameServer()->m_apPlayers[m_pPlayer->m_Poisoner]->GetCID(),WEAPON_POISON);
			m_pPlayer->m_StartPoison--;
		}
		if(m_pPlayer->m_StartPoison <=0)
			m_pPlayer->m_Poisoned=0;

		if(m_pPlayer->m_Hot && Server()->Tick()-m_pPlayer->m_HotStartTick > Server()->TickSpeed() && GameServer()->m_apPlayers[m_pPlayer->m_HotFrom])
		{
			m_pPlayer->m_HotStartTick=Server()->Tick();
			//if(health < 10)
				IncreaseHealth(1);
			//else
				IncreaseArmor(1);
			GameServer()->m_apPlayers[m_pPlayer->m_HotFrom]->m_Xp++;
			GameServer()->CreateSound(GameServer()->m_apPlayers[m_pPlayer->m_HotFrom]->m_ViewPos, SOUND_PICKUP_HEALTH, m_pPlayer->m_HotFrom);
			m_pPlayer->m_StartHot--;
		}
		if(m_pPlayer->m_StartHot <=0)
			m_pPlayer->m_Hot=0;

		//Grenade heal
		if(m_pPlayer->m_Healed && Server()->Tick()-m_pPlayer->m_HealTick > Server()->TickSpeed() && GameServer()->m_apPlayers[m_pPlayer->m_HealFrom])
		{
			m_pPlayer->m_HealTick=Server()->Tick();
			if(m_Health < 10)
			{
				GameServer()->CreateHammerHit(m_Pos);
				GameServer()->CreateSound(GameServer()->m_apPlayers[m_pPlayer->m_HealFrom]->m_ViewPos, SOUND_PICKUP_HEALTH, m_pPlayer->m_HealFrom);
			}
			IncreaseHealth(2);
		}
		//Previous stuff (should be deleted ?)
		//if((GameServer()->m_pController)->IsRpg() && m_Core.m_Vel.x < (20.0f*((float)m_pPlayer->undead_speed/2.5f)) && m_Core.m_Vel.x > (-20.0f*((float)m_pPlayer->undead_speed/2.5f)))m_Core.m_Vel.x*=1.1f;
		//dbg_msg("test","%f %d",m_Core.m_Vel.x,m_Core.m_Vel.x);

		if(stucked && Server()->Tick()-stucked < Server()->TickSpeed()*3)
			m_Core.m_Vel=vec2(0.0f,0.0f);

		if (m_pPlayer->m_UndeadTaser && m_Core.m_HookedPlayer != -1 && Server()->Tick()-m_pPlayer->m_UndeadTaserTick > Server()->TickSpeed() && GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetTeam() != m_pPlayer->GetTeam())
		{
			GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()->TakeDamage(vec2(0,-1.0f), m_pPlayer->m_UndeadTaser, m_pPlayer->GetCID(), WEAPON_TASER);
			m_pPlayer->m_UndeadTaserTick=Server()->Tick();
		}
	}

	if(m_pPlayer->m_IsChainHeal && Server()->Tick()-m_pPlayer->m_BounceTick > Server()->TickSpeed()/2)
	{
		vec2 targ_m_Pos=normalize(m_pPlayer->m_pHealChar->m_Pos - m_Pos);
		m_pPlayer->m_IsChainHeal=false;
		new CLaser(GameWorld(), m_Pos, targ_m_Pos, GameServer()->Tuning()->m_LaserReach, m_pPlayer->m_ChainHealFrom, m_pPlayer->GetCID());
		GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
	}
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}
	
	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	
	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;
	
	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x", 
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());
	
	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);
	
	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);

	
	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}
	
	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

bool CCharacter::IncreaseHealth(int Amount)
{
	/*//Not used yet i was planing on buff undead
	if(player && m_pPlayer->m_RaceName == UNDEAD)
	{
		if(health >= 15)
			return false;
		health = clamp(health+amount, 0, 15);
		return true;
	}*/
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	//Same as heal
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;

	m_pPlayer->m_Poisoned=0;
	m_pPlayer->m_Hot=0;

	if(m_pPlayer && m_pPlayer->m_RaceName == TAUREN  && GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal] && m_pPlayer->m_StartedHeal != -1)
	{
		char buf[128];
		str_format(buf,sizeof(buf),"Stopped healing (you died)");
		GameServer()->SendChatTarget(m_pPlayer->GetCID(),buf);
		GameServer()->m_apPlayers[m_pPlayer->m_StartedHeal]->m_Healed=false;
		m_pPlayer->m_StartedHeal=-1;
	}	

	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);
	
	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_pPlayer->m_DeathPos=m_Pos;
	if(Weapon==WEAPON_WORLD)
		m_pPlayer->m_DeathTile=true;
	
	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;

	//Tauren hot
	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && GameServer()->m_apPlayers[From]->m_TaurenHot && Weapon == WEAPON_GUN && m_pPlayer->m_Hot!=1)
	{
		m_pPlayer->m_Hot=1;
		m_pPlayer->m_HotStartTick=Server()->Tick();
		m_pPlayer->m_HotFrom=From;
		m_pPlayer->m_StartHot=GameServer()->m_apPlayers[From]->m_TaurenHot*2;
		return true;
	}
	else if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && GameServer()->m_apPlayers[From]->m_TaurenHot && Weapon == WEAPON_GUN && m_pPlayer->m_Hot == 1)
	{
		m_pPlayer->m_HotFrom=From;
		m_pPlayer->m_StartHot=GameServer()->m_apPlayers[From]->m_TaurenHot*2;
		return true;
	}

	//Tauren chain heal
	if(GameServer()->m_apPlayers[From] && GameServer()->m_apPlayers[From]->GetCharacter() && GameServer()->m_apPlayers[From]->m_Bounces > 1 && GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && GameServer()->m_apPlayers[From]->m_RaceName == TAUREN && Weapon == WEAPON_RIFLE && From != m_pPlayer->GetCID())
	{
		CCharacter *ents[64];
		m_pPlayer->m_pHealChar=NULL;
		float mindist=-1;
		int num = GameServer()->m_World.FindEntities(m_Pos, 500.0f, (CEntity**)ents, 64, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < num; i++)
		{
			if(ents[i]==this || ents[i]->m_pPlayer->GetCID() == GameServer()->m_apPlayers[From]->m_LastHealed || ents[i]==GameServer()->m_apPlayers[From]->GetCharacter() || (ents[i]->m_pPlayer->GetTeam() != m_pPlayer->GetTeam() && GameServer()->m_pController->IsTeamplay()))
				continue;
			CCharacter *hit;
			vec2 at;
			float dist = distance(ents[i]->m_Pos,m_Pos);
			vec2 to = m_Pos+normalize(ents[i]->m_Pos - m_Pos)*500;
			GameServer()->Collision()->IntersectLine(m_Pos, to, 0x0, &to);
			hit = GameServer()->m_World.IntersectCharacter(m_Pos, to, 0.0f, at, this);
			if((dist < mindist || mindist == -1) && hit)
			{
				m_pPlayer->m_pHealChar = hit;
			}
		}
		int tmpdmg=GameServer()->m_apPlayers[From]->m_Lvl*GameServer()->m_apPlayers[From]->m_Bounces;
		if(m_pPlayer->m_pHealChar)
		{
			GameServer()->m_apPlayers[From]->m_Bounces--;
			m_pPlayer->m_BounceTick=Server()->Tick();
			m_pPlayer->m_IsChainHeal=true;
			m_pPlayer->m_ChainHealFrom=From;
		}
		GameServer()->m_apPlayers[From]->m_LastHealed=m_pPlayer->GetCID();
		GameServer()->m_apPlayers[From]->m_Xp+=tmpdmg;
		IncreaseHealth(tmpdmg/2);
		IncreaseArmor(tmpdmg/2);
		return true;
	}


	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)
		return false;

	//Invicible
	if((GameServer()->m_pController)->IsRpg() && m_pPlayer->m_Invincible)
	{
		if(GameServer()->m_apPlayers[From] && GameServer()->m_apPlayers[From]->GetCharacter() && Weapon != WEAPON_MIRROR)
			GameServer()->m_apPlayers[From]->GetCharacter()->TakeDamage(vec2(0,0),Dmg,m_pPlayer->GetCID(),WEAPON_MIRROR);
		return false;
	}

	//Armor reduce and damage increase
	if((GameServer()->m_pController)->IsRpg() && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		float dmgincrease=(float)Dmg*((float)GameServer()->m_apPlayers[From]->m_OrcDmg*15.0f/100.0f);
		float dmgdecrease=(float)Dmg*((float)m_pPlayer->m_HumanArmor*15.0f/100.0f);
		Dmg=(int)round((float)Dmg+dmgincrease-dmgdecrease);
		if(Dmg<=0)Dmg=1;
		if(g_Config.m_DbgWar3)dbg_msg("damage","decrease : %f increase : %f dmg recieve : %d",dmgincrease,dmgdecrease,Dmg);
	}

	//Poison | vampire | mirror
	if((GameServer()->m_pController)->IsRpg())
	{
		if(GameServer()->m_apPlayers[From]->m_UndeadVamp && From != m_pPlayer->GetCID())GameServer()->m_apPlayers[From]->Vamp(Dmg);
		if(GameServer()->m_apPlayers[From]->m_ElfPoison && From != m_pPlayer->GetCID() && !m_pPlayer->m_Poisoned && Weapon != WEAPON_MIRROR)
		{
			m_pPlayer->m_Poisoned=1;
			m_pPlayer->m_PoisonStartTick=Server()->Tick();
			m_pPlayer->m_Poisoner=From;
			m_pPlayer->m_StartPoison=GameServer()->m_apPlayers[From]->m_ElfPoison*2;
		}
		if(From != m_pPlayer->GetCID() && m_pPlayer->m_ElfMirror && GameServer()->m_apPlayers[From] && !GameServer()->m_apPlayers[From]->m_ElfMirror && GameServer()->m_apPlayers[From]->GetCharacter() && GameServer()->m_apPlayers[From]->GetCharacter()->m_Alive && m_pPlayer->m_MirrorLimit < m_pPlayer->m_ElfMirror && Weapon != WEAPON_MIRROR)
		{
			int mirrordmg=Dmg;
			if(mirrordmg > m_pPlayer->m_ElfMirror)mirrordmg=m_pPlayer->m_ElfMirror;
			GameServer()->m_apPlayers[From]->GetCharacter()->TakeDamage(vec2(0,0),mirrordmg,m_pPlayer->GetCID(),WEAPON_MIRROR);
			m_pPlayer->m_MirrorDmgTick=Server()->Tick();
			m_pPlayer->m_MirrorLimit++;
		}
		if(Server()->Tick()-m_pPlayer->m_MirrorDmgTick > Server()->TickSpeed()/2)
		{
			m_pPlayer->m_MirrorLimit=0;
		}
	}

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg/2);

	//Increasing xp with damage
	else if((GameServer()->m_pController)->IsRpg() && (GameServer()->m_apPlayers[From]->GetTeam() != m_pPlayer->GetTeam() || !(GameServer()->m_pController)->IsTeamplay()))
	{
		CPlayer *p=GameServer()->m_apPlayers[From];
		p->m_Xp+=Dmg;
		if(p->m_Healed && GameServer()->m_apPlayers[p->m_HealFrom])
			GameServer()->m_apPlayers[p->m_HealFrom]->m_Xp+=Dmg;
	}

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}
			
			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}
		
		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, CmaskOne(From));

	// check for death
	if(m_Health <= 0)
	{
		/*else if((GameServer()->m_pController)->IsRpg() && m_pPlayer->other_invincible && !m_pPlayer->m_Invincible_used)
		{
			m_pPlayer->m_Invincible_used=true;
			m_pPlayer->m_Invincible=1;
			m_pPlayer->m_Invincible_start_tick=Server()->Tick();
			health=1;
		}*/
		Die(From, Weapon);
		
		//Reset poison at death
		m_pPlayer->m_Poisoned=0;
		m_pPlayer->m_Hot=0;

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}
	
		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	
	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;
	
	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}
	
	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
	
	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 || m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID)
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}
