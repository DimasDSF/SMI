// (More) Reallistic simulated bullets
//
// This code is originally based from article on Valve Developer Community
// The original code you can find by this link:
// http://developer.valvesoftware.com/wiki/Simulated_Bullets
 
// NOTENOTE: Tested only on localhost. There maybe a latency errors and others
// NOTENOTE: The simulation might be strange.
 
#include "cbase.h"
#include "util_shared.h"
#include "bullet_manager.h"
#include "effect_dispatch_data.h"
#include "tier0/vprof.h"
#include "decals.h"
 
 
CBulletManager *g_pBulletManager;
CUtlLinkedList<CSimulatedBullet*> g_Bullets;
 
#ifdef CLIENT_DLL//-------------------------------------------------
#include "engine/ivdebugoverlay.h"
#include "c_te_effect_dispatch.h"
ConVar g_debug_client_bullets( "g_debug_client_bullets", "0", FCVAR_CHEAT );
extern void FX_PlayerTracer( Vector& start, Vector& end);
#else//-------------------------------------------------------------
#include "te_effect_dispatch.h"
#include "soundent.h"
#include "player_pickup.h"
#include "ilagcompensationmanager.h"
ConVar g_debug_bullets( "g_debug_bullets", "0", FCVAR_CHEAT, "Debug of bullet simulation\nThe white line shows the bullet trail.\nThe red line shows not passed penetration test.\nThe green line shows passed penetration test. Turn developer mode for more information." );
#endif//------------------------------------------------------------
 
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
 
#define MAX_RICO_DOT_ANGLE 0.15f  //Maximum dot allowed for any ricochet
#define MIN_RICO_SPEED_PERC 0.55f //Minimum speed percent allowed for any ricochet
#define DEFAULT_BULLET_SPEED 700.00f

ConVar sv_bullet_speed_modifier( "sv_bullet_speed_modifier", "700.000000", (FCVAR_ARCHIVE | FCVAR_REPLICATED), "Bullet Speed Modifier" );
ConVar sv_player_bullet_offset_y( "sv_player_bullet_offset_y", "2.0");
ConVar sv_player_bullet_offset_z( "sv_player_bullet_offset_z", "-1.5");
ConVar g_debug_bullets_hit( "g_debug_bullets_hit", "0", FCVAR_CHEAT, "Gives full info on each bullet hit");

static void BulletStopSpeedCallback(IConVar *var, const char *pOldString, float flOldValue)
{
	if(BulletManager())
		BulletManager()->UpdateBulletStopSpeed();
}
ConVar sv_bullet_stop_speed( "sv_bullet_stop_speed", "40", (FCVAR_ARCHIVE | FCVAR_REPLICATED),
							"Integral speed at which to remove the bullet from the bullet queue\n-1 is butter mode\n-2 is 1 hit mode",
							BulletStopSpeedCallback );
 
 
 
LINK_ENTITY_TO_CLASS( bullet_manager, CBulletManager );
 
 
 
//==================================================
// Purpose:	Constructor
//==================================================
CSimulatedBullet::CSimulatedBullet()
{
	m_vecOrigin.Init();
	m_vecDirShooting.Init();
	m_flInitialBulletSpeed = m_flBulletSpeed = 0;
	m_flEntryDensity = 0.0f;
	bStuckInWall = false;
	m_bHitGlass = false;
 
	m_iDamageType = 2;
}
 
//==================================================
// Purpose:	Constructor
//==================================================
CSimulatedBullet::CSimulatedBullet( FireBulletsInfo_t info,Vector newdir,CBaseEntity *pInfictor, CBaseEntity *pAdditionalIgnoreEnt,
						bool bTraceHull
#ifndef CLIENT_DLL
						, CBaseEntity *pCaller
#endif
)
{
	// Input validation
	Assert( pInfictor );
#ifndef CLIENT_DLL
	Assert( pCaller );
#endif
 
	bulletinfo = info;			// Setup Fire bullets information here
 
	p_eInfictor = pInfictor;	// Setup inflictor
 
	// Create a list of entities with which this bullet does not collide.
	m_pIgnoreList = new CTraceFilterSimpleList(COLLISION_GROUP_NONE);
	Assert( m_pIgnoreList );
 
	bStuckInWall = false;
	m_bHitGlass = false;
 
	// Don't collide with the entity firing the bullet.
	m_pIgnoreList->AddEntityToIgnore(p_eInfictor);
 
	// Don't collide with some optionally-specified entity.
	if(pAdditionalIgnoreEnt!=NULL)
		m_pIgnoreList->AddEntityToIgnore(pAdditionalIgnoreEnt);
 
	m_iDamageType = GetAmmoDef()->DamageType( bulletinfo.m_iAmmoType );
 
 
	//m_szTracerName = (char*)p_eInfictor->GetTracerType(); 
 
	// Basic information about the bullet
	m_flInitialBulletSpeed = m_flBulletSpeed = GetAmmoDef()->GetAmmoOfIndex(bulletinfo.m_iAmmoType)->bulletSpeed;
	m_vecDirShooting = newdir;
	if (p_eInfictor->IsPlayer())
	{
		Vector right;
		AngleVectors(p_eInfictor->GetAbsAngles(),NULL,&right,NULL);
		m_vecOrigin = bulletinfo.m_vecSrc + Vector(0, 0, sv_player_bullet_offset_z.GetFloat()) + right * sv_player_bullet_offset_y.GetFloat();
	}
	else
	{
		m_vecOrigin = bulletinfo.m_vecSrc;
	}
	m_bTraceHull = bTraceHull;
#ifndef CLIENT_DLL
	m_hCaller = pCaller;
#endif
	m_flEntryDensity = 0.0f;
	m_vecTraceRay = m_vecOrigin + m_vecDirShooting * m_flBulletSpeed;
	m_flRayLength = m_flInitialBulletSpeed;
}
 
 
 
//==================================================
// Purpose:	Deconstructor
//==================================================
CSimulatedBullet::~CSimulatedBullet()
{
	delete m_pIgnoreList;
}
//==================================================
// Purpose:	Simulates a bullet through a ray of its bullet speed
//==================================================
bool CSimulatedBullet::SimulateBullet(void)
{
	VPROF( "C_SimulatedBullet::SimulateBullet" );

	if(!IsFinite(m_flBulletSpeed))
		return false;		 //prevent a weird crash
 
	trace_t trace;
 
	if(m_flBulletSpeed <= 0) //Avoid errors;
		return false;
 
	if(!p_eInfictor)
	{
		p_eInfictor = bulletinfo.m_pAttacker;
 
		if(!p_eInfictor)
			return false;
	}
 
	m_flRayLength = m_flBulletSpeed;
 
	m_flBulletSpeed += 0.8f * m_vecDirShooting.z; //TODO: Bullet mass
	m_vecTraceRay = m_vecOrigin + m_vecDirShooting * m_flBulletSpeed; 
 
	m_vecDirShooting.z -= 0.1 / m_flBulletSpeed;
 
#ifdef GAME_DLL
	if(bulletinfo.m_flLatency!= 0 )
	{
		m_vecTraceRay *= bulletinfo.m_flLatency * 100;
	}
#endif
 
	if(!IsInWorld())
	{
		return false;
	}
 
	if(bStuckInWall)
		return false;
 
	if(!m_bWasInWater)
	{
		WaterHit(m_vecOrigin,m_vecTraceRay);
	}
 
	m_bWasInWater = (UTIL_PointContents(m_vecOrigin)& MASK_WATER )?true:false;
 
	if(m_bWasInWater)
	{
		m_flBulletSpeed -= m_flBulletSpeed * 0.6;
#ifdef GAME_DLL
		//This is a server stuff
		UTIL_BubbleTrail(m_vecOrigin, m_vecTraceRay, 5 );
#endif
	}
 
#ifndef CLIENT_DLL
	if(m_bWasInWater)
	{
		CEffectData tracerData;
		tracerData.m_vStart = m_vecOrigin;
		tracerData.m_vOrigin = m_vecTraceRay;
 
		tracerData.m_fFlags = TRACER_TYPE_WATERBULLET;
 
		DispatchEffect( "TracerSound", tracerData );
	}
#endif
 
 
	bool bulletSpeedCheck;
	bulletSpeedCheck = false;
 
 
	if(m_bTraceHull)
		UTIL_TraceHull( m_vecOrigin, m_vecTraceRay, Vector(-3, -3, -3), Vector(3, 3, 3), MASK_SHOT, m_pIgnoreList, &trace );
	else
		UTIL_TraceLine( m_vecOrigin, m_vecTraceRay, MASK_SHOT, m_pIgnoreList, &trace );
 
	if(!m_bWasInWater)
	{
		UTIL_Tracer( m_vecOrigin, trace.endpos, p_eInfictor->entindex(), TRACER_DONT_USE_ATTACHMENT, 0, true,p_eInfictor->GetTracerType());
	}
 
#ifdef CLIENT_DLL
	if(g_debug_client_bullets.GetBool())
	{
		debugoverlay->AddLineOverlay( trace.startpos, trace.endpos, 255, 0, 0, true, 10.0f );
	}
#else
	if(g_debug_bullets.GetBool())
	{
		NDebugOverlay::Line( trace.startpos, trace.endpos, 255, 255, 255, true, 10.0f );
	}
 
	// Now hit all triggers along the ray that respond to shots...
	// Clip the ray to the first collided solid returned from traceline
	CTakeDamageInfo triggerInfo( p_eInfictor, bulletinfo.m_pAttacker, bulletinfo.m_flDamage, GetDamageType() );
	CalculateBulletDamageForce( &triggerInfo, bulletinfo.m_iAmmoType, m_vecDirShooting, trace.endpos );
	triggerInfo.ScaleDamageForce( bulletinfo.m_flDamageForceScale );
	triggerInfo.SetAmmoType( bulletinfo.m_iAmmoType );
	BulletManager()->SendTraceAttackToTriggers( triggerInfo, trace.startpos, trace.endpos, m_vecDirShooting );
#endif
 
	if (trace.fraction == 1.0f)
	{
		m_vecOrigin += m_vecDirShooting * m_flBulletSpeed; //Do the WAY
 
		CEffectData data;
		data.m_vStart = trace.startpos;
		data.m_vOrigin = trace.endpos;
		data.m_nDamageType = GetDamageType();
 
		DispatchEffect( "RagdollImpact", data );
 
		return true;
	}
	else
	{
		EntityImpact(trace);
 
		if (trace.m_pEnt == p_eInfictor) //HACK: Remove bullet if we hit self (for frag grenades)
			return false;
 
		if(!(trace.surface.flags&SURF_SKY))
		{
			if(trace.allsolid)//in solid
			{
				if(!AllSolid(trace))
					return false;
 
				m_vecOrigin += m_vecDirShooting * m_flBulletSpeed; //Do the WAY
 
				bulletSpeedCheck = true;
			}
			else if(trace.fraction!=1.0f)//hit solid
			{
				if(!EndSolid(trace))
					return false;
 
				bulletSpeedCheck = true;
			}
			else if(trace.startsolid)//exit solid
			{
				if(!StartSolid(trace))
					return false;
 
				m_vecOrigin += m_vecDirShooting * m_flBulletSpeed; //Do the WAY
 
				bulletSpeedCheck = true;
			}
			else
			{
				//don't do a bullet speed check for not touching anything
			}
		}
		else
		{
			return false; //Through sky? No.
		}
	}
 
	if(bulletSpeedCheck)
	{
		if(m_flBulletSpeed<=BulletManager()->BulletStopSpeed())
		{
			return false;
		}
	}
 
	return true;
}
 
 
//==================================================
// Purpose:	Simulates when a solid is exited
//==================================================
bool CSimulatedBullet::StartSolid(trace_t &ptr)
{
	switch(BulletManager()->BulletStopSpeed())
	{
		case BUTTER_MODE:
		{
			//Do nothing to bullet speed
			return true;
		}
		case ONE_HIT_MODE:
		{
			return false;
		}
		default:
		{
			float flPenetrationDistance = VectorLength(AbsEntry - AbsExit);

			if (sv_bullet_speed_modifier.GetFloat() <= 0)
			{
				sv_bullet_speed_modifier.SetValue(DEFAULT_BULLET_SPEED);
			}
			m_flBulletSpeed -= flPenetrationDistance * m_flEntryDensity / sv_bullet_speed_modifier.GetFloat();
			return true;
		}
	}
	return true;
}
 
 
//==================================================
// Purpose:	Simulates when a solid is being passed through
//==================================================
bool CSimulatedBullet::AllSolid(trace_t &ptr)
{
	switch(BulletManager()->BulletStopSpeed())
	{
		case BUTTER_MODE:
		{
			//Do nothing to bullet speed
			return true;
		}
		case ONE_HIT_MODE:
		{
			return false;
		}
		default:
		{
			if (sv_bullet_speed_modifier.GetFloat() <= 0)
			{
				sv_bullet_speed_modifier.SetValue(DEFAULT_BULLET_SPEED);
			}
			m_flBulletSpeed -= m_flBulletSpeed * m_flEntryDensity / sv_bullet_speed_modifier.GetFloat();
			return true;
		}
	}
	return true;
}
 
 
//==================================================
// Purpose:	Simulate when a surface is hit
//==================================================
bool CSimulatedBullet::EndSolid(trace_t &ptr)
{
	m_vecEntryPosition = ptr.endpos;
 
#ifndef CLIENT_DLL
	int soundEntChannel = ( bulletinfo.m_nFlags&FIRE_BULLETS_TEMPORARY_DANGER_SOUND ) ? SOUNDENT_CHANNEL_BULLET_IMPACT : SOUNDENT_CHANNEL_UNSPECIFIED;
 
	CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, m_vecEntryPosition, 200, 0.5, p_eInfictor, soundEntChannel );
#endif
 
	if(FStrEq(ptr.surface.name,"tools/toolsblockbullets"))
	{
		return false;
	}
	m_flEntryDensity = physprops->GetSurfaceData(ptr.surface.surfaceProps)->physics.density;
 
	trace_t rtr;
	Vector vecEnd = m_vecEntryPosition + m_vecDirShooting * 32; //32 units
 
	//Doing now test of penetration
	UTIL_TraceLine(m_vecEntryPosition + m_vecDirShooting * 1.1, vecEnd, MASK_SHOT, m_pIgnoreList, &rtr);
 
	AbsEntry = m_vecEntryPosition;
	AbsExit  = rtr.startpos;
 
	float flPenetrationDistance = VectorLength(AbsEntry - AbsExit);

	DesiredDistance = 0.0f;
	char *hitMaterial = "invalid";
	m_bHitGlass = false;
	surfacedata_t *p_penetrsurf = physprops->GetSurfaceData( ptr.surface.surfaceProps );
	switch (p_penetrsurf->game.material)
	{
		case CHAR_TEX_WOOD:
			DesiredDistance = 9.0f; // 9 units in hammer
			hitMaterial = "wood";
			break;
		case CHAR_TEX_GRATE:
			DesiredDistance = 6.0f; // 6 units in hammer
			hitMaterial = "grate";
			break;
		case CHAR_TEX_CONCRETE:
			DesiredDistance = 4.0f; // 4 units in hammer
			hitMaterial = "concrete";
			break;
		case CHAR_TEX_TILE:
			DesiredDistance = 5.0f; // 5 units in hammer
			hitMaterial = "tile";
			break;
		case CHAR_TEX_COMPUTER:
			DesiredDistance = 5.0f; // 5 units in hammer
			hitMaterial = "computer";
			break;
		case CHAR_TEX_GLASS:
			DesiredDistance = 8.0f; // maximum 8 units in hammer.
			hitMaterial = "glass";
			m_bHitGlass = true;
			break;
		case CHAR_TEX_VENT:
			DesiredDistance = 4.0f; // 4 units in hammer and no more(!)
			hitMaterial = "vent";
			break;
		case CHAR_TEX_METAL:
			DesiredDistance = 5.0f; // 2 units in hammer. We cannot penetrate a really 'fat' metal wall. Corners are good.
			hitMaterial = "metal";
			break;
		case CHAR_TEX_PLASTIC:
			DesiredDistance = 8.0f; // 8 units in hammer: Plastic can more
			hitMaterial = "plastic";
			break;
		case CHAR_TEX_BLOODYFLESH:
			DesiredDistance = 16.0f; // 16 units in hammer
			hitMaterial = "bloodyflesh";
			break;
		case CHAR_TEX_FLESH:
			DesiredDistance = 16.0f; // 16 units in hammer
			hitMaterial = "flesh";
			break;
		case CHAR_TEX_SAND:
			DesiredDistance = 6.0f;
			hitMaterial = "sand";
			break;
		case CHAR_TEX_DIRT:
			DesiredDistance = 6.0f; // 6 units in hammer: >4 cm of plaster can be penetrated
			hitMaterial = "dirt";
			break;
	}
	DesiredDistance = (17-DesiredDistance)*4;

	float MaxPenetrDistForSpeed;
	float m_flPenetrationFactor = GetAmmoDef()->PenetrationFactor(GetAmmoTypeIndex());
	if (DesiredDistance != 0 && m_flPenetrationFactor != 0)
	{
		MaxPenetrDistForSpeed = m_flBulletSpeed/(m_flEntryDensity/(DesiredDistance/m_flPenetrationFactor));
	}
	else
	{
		MaxPenetrDistForSpeed = 0;
	}
	Vector	reflect;
	float fldot = m_vecDirShooting.Dot( ptr.plane.normal );						//Getting angles from lasttrace
 
	bool bMustDoRico = (fldot > -MAX_RICO_DOT_ANGLE && GetBulletSpeedRatio() > MIN_RICO_SPEED_PERC); // We can't do richochet when bullet has lowest speed
 
	if ( bMustDoRico )
	{
		m_flBulletSpeed *= (1.0f / -fldot) * random->RandomFloat(0.005,0.1);
 
		reflect = m_vecDirShooting + ( ptr.plane.normal * ( fldot*-2.0f ) );	//reflecting
 
		reflect[0] += random->RandomFloat( fldot, -fldot );
		reflect[1] += random->RandomFloat( fldot, -fldot );
		reflect[2] += random->RandomFloat( fldot, -fldot );
 
		m_flEntryDensity *= 0.2;
 
		m_vecDirShooting = reflect;
 
		m_vecOrigin = ptr.endpos+m_vecDirShooting;//(ptr.endpos + m_vecDirShooting*1.1) + m_vecDirShooting * m_flBulletSpeed;
	}
	else
	{
		if (g_debug_bullets_hit.GetBool())
		{
			DevMsg("Hit:\n"
				"{\n" 
				" Hit Material: %s\n"
				" Current bullet speed: %f\n"
				" Engine Entry Density: %f\n"
				" Desired Material Density: %f\n"
				" Penetration Distance: %f\n"
				" MaxPenDist: %f\n"
				"}\n"
				,hitMaterial,m_flBulletSpeed,m_flEntryDensity,DesiredDistance*m_flPenetrationFactor,flPenetrationDistance,MaxPenetrDistForSpeed);
		}
		if (flPenetrationDistance > MaxPenetrDistForSpeed || ptr.IsDispSurface()) //|| !bulletinfo.m_bMustPenetrate DesiredDistance
		{
			bStuckInWall = true;
 
#ifdef GAME_DLL
			if(g_debug_bullets.GetBool())
			{
				NDebugOverlay::Line( AbsEntry, AbsExit, 255, 0, 0, true, 10.0f );
				if(!ptr.IsDispSurface())
				{
					DevMsg("Cannot penetrate surface: %s, The distance(%f) > %f \n",hitMaterial,flPenetrationDistance,MaxPenetrDistForSpeed);
				}
				else
					DevMsg("Displacement penetration was tempolary disabled\n");
			}
#endif
		}
		else
		{
				trace_t tr;
				UTIL_TraceLine(AbsExit + m_vecDirShooting * 1.1 ,AbsEntry,MASK_SHOT,m_pIgnoreList,&tr);
				UTIL_ImpactTrace( &tr, GetDamageType() ); // On exit
				if(m_bHitGlass)
				{
					CEffectData	data;

					data.m_vNormal = tr.plane.normal * -1;
					data.m_vOrigin = tr.endpos;

					DispatchEffect( "GlassImpact", data );
				}
 
				#ifdef GAME_DLL
					if(g_debug_bullets.GetBool())
					{			
						NDebugOverlay::Line( AbsEntry, AbsExit, 0, 255, 0, true, 10.0f );
					}
				#endif
 
				m_vecDirShooting[0] += (random->RandomFloat( -flPenetrationDistance, flPenetrationDistance ))*0.01;
				m_vecDirShooting[1] += (random->RandomFloat( -flPenetrationDistance, flPenetrationDistance ))*0.01;
				m_vecDirShooting[2] += (random->RandomFloat( -flPenetrationDistance, flPenetrationDistance ))*0.01;
 
				VectorNormalize(m_vecDirShooting);
 
				m_vecOrigin = AbsExit+m_vecDirShooting;
		}
	}
 
#ifdef GAME_DLL
	//Cancel making dust underwater:
	if(!m_bWasInWater)
	{
		if(m_bHitGlass)
		{
			CEffectData	data;

			data.m_vNormal = ptr.plane.normal;
			data.m_vOrigin = ptr.endpos;

			DispatchEffect( "GlassImpact", data );
		}
		UTIL_ImpactTrace( &ptr, GetDamageType() );
	}
#endif
 
	if (sv_bullet_speed_modifier.GetFloat() <= 0)
		{
			sv_bullet_speed_modifier.SetValue(DEFAULT_BULLET_SPEED);
		}
	m_flBulletSpeed -= flPenetrationDistance * m_flEntryDensity / sv_bullet_speed_modifier.GetFloat();
 
	if(BulletManager()->BulletStopSpeed()==ONE_HIT_MODE)
	{
		return false;
	}
	return true;
}
 
//-----------------------------------------------------------------------------
// analog of HandleShotImpactingWater
//-----------------------------------------------------------------------------
bool CSimulatedBullet::WaterHit(const Vector &vecStart,const Vector &vecEnd)
{
	trace_t	waterTrace;
	// Trace again with water enabled
	UTIL_TraceLine( vecStart, vecEnd, (MASK_SHOT|CONTENTS_WATER|CONTENTS_SLIME), m_pIgnoreList, &waterTrace );
 
	// See if this is the point we entered
	if ( ( enginetrace->GetPointContents( waterTrace.endpos - Vector(0,0,0.1f) ) & (CONTENTS_WATER|CONTENTS_SLIME) ) == 0 )
		return false;
 
	int	nMinSplashSize = GetAmmoDef()->MinSplashSize(GetAmmoTypeIndex()) * (1.5 - GetBulletSpeedRatio());
	int	nMaxSplashSize = GetAmmoDef()->MaxSplashSize(GetAmmoTypeIndex()) * (1.5 - GetBulletSpeedRatio()); //High speed - small splash
 
	CEffectData	data;
	data.m_vOrigin = waterTrace.endpos;
	data.m_vNormal = waterTrace.plane.normal;
	data.m_flScale = random->RandomFloat( nMinSplashSize, nMaxSplashSize );
	if ( waterTrace.contents & CONTENTS_SLIME )
	{
		data.m_fFlags |= FX_WATER_IN_SLIME;
	}
	DispatchEffect( "gunshotsplash", data );
	return true;
}
 
 
 
//==================================================
// Purpose:	Entity impact procedure
//==================================================
void CSimulatedBullet::EntityImpact(trace_t &ptr)
{
	if(bStuckInWall)
		return;
 
	if (ptr.m_pEnt != NULL)
	{
		//Hit inflicted once to avoid perfomance errors
 
		//if(!p_eInfictor->IsPlayer())
		//{
			if (ptr.m_pEnt->IsPlayer() || ptr.m_pEnt->IsNPC())
			{
				if (m_pIgnoreList->ShouldHitEntity(ptr.m_pEnt,MASK_SHOT))
				{
					m_pIgnoreList->AddEntityToIgnore(ptr.m_pEnt);
				}
				else
				{
					return;
				}
			}
		//}
 
		if(ptr.m_pEnt == m_hLastHit)
			return;
 
		m_hLastHit = ptr.m_pEnt;
 
		float flActualDamage = g_pGameRules->GetAmmoDamage( p_eInfictor, ptr.m_pEnt, bulletinfo.m_iAmmoType );
		float flActualForce  = bulletinfo.m_flDamageForceScale; 
 
		if (!ptr.m_pEnt->IsPlayer() || !ptr.m_pEnt->IsNPC())
		{
			//To make it more reallistic
			float fldot = m_vecDirShooting.Dot( ptr.plane.normal );	 
			//We affecting damage by angle. If we has lower angle of reflection, doing lower damage.
			flActualDamage *= -fldot;
			flActualForce  *= -fldot;
			//But we not doing it on the players or NPC's
		}
 
		flActualDamage *= GetBulletSpeedRatio(); //And also affect damage by speed modifications
		flActualForce  *= GetBulletSpeedRatio(); //Slow bullet - bad force...
		
		flActualDamage = max(flActualDamage, 0);
		flActualForce = max(flActualForce, 0);

		DevMsg("Hit: %s, Damage: %f, Force: %f \n",ptr.m_pEnt->GetClassname(),flActualDamage,flActualForce);
 
		CTakeDamageInfo dmgInfo( p_eInfictor, bulletinfo.m_pAttacker, flActualDamage, GetDamageType() );
 
		CalculateBulletDamageForce( &dmgInfo, bulletinfo.m_iAmmoType, m_vecDirShooting, ptr.endpos );
		dmgInfo.ScaleDamageForce( flActualForce );
		dmgInfo.SetAmmoType( bulletinfo.m_iAmmoType );
 
		ptr.m_pEnt->DispatchTraceAttack( dmgInfo, bulletinfo.m_vecDirShooting, &ptr );
 
	#ifdef GAME_DLL
		ApplyMultiDamage(); //It's requried
 
		if(!m_bWasInWater)
		{
			surfacedata_t *p_penetrsurf = physprops->GetSurfaceData( ptr.surface.surfaceProps );
			if(p_penetrsurf->game.material == CHAR_TEX_GLASS)
			{
				CEffectData	data;

				data.m_vNormal = ptr.plane.normal;
				data.m_vOrigin = ptr.endpos;

				DispatchEffect( "GlassImpact", data );
			}
			UTIL_ImpactTrace( &ptr, GetDamageType() );
		}

		if ( GetAmmoDef()->Flags(GetAmmoTypeIndex()) & AMMO_FORCE_DROP_IF_CARRIED )
		{
			// Make sure if the player is holding this, he drops it
			Pickup_ForcePlayerToDropThisObject( ptr.m_pEnt );		
		}
	#endif
	}
}
 
 
//==================================================
// Purpose:	Simulates all bullets every centisecond
//==================================================
#ifndef CLIENT_DLL
void CBulletManager::Think(void)
#else
void CBulletManager::ClientThink(void)
#endif
{
	unsigned short iNext=0;
	for ( unsigned short i=g_Bullets.Head(); i != g_Bullets.InvalidIndex(); i=iNext )
	{
		iNext = g_Bullets.Next( i );
		if ( g_Bullets[i]->m_flTimeStamp + 3 < gpGlobals->curtime)
			RemoveBullet( i );
		if ( !g_Bullets[i]->SimulateBullet() )
			RemoveBullet( i );
	}
	if(g_Bullets.Head()!=g_Bullets.InvalidIndex())
	{
#ifdef CLIENT_DLL
		SetNextClientThink( gpGlobals->curtime + 0.01f );
#else
		SetNextThink( gpGlobals->curtime + 0.01f );
#endif
	}
}
 
 
//==================================================
// Purpose:	Called by sv_bullet_stop_speed callback to keep track of resources
//==================================================
void CBulletManager::UpdateBulletStopSpeed(void)
{
	m_iBulletStopSpeed = sv_bullet_stop_speed.GetInt();
}
 
#ifndef CLIENT_DLL
void CBulletManager::SendTraceAttackToTriggers( const CTakeDamageInfo &info, const Vector& start, const Vector& end, const Vector& dir )
{
	TraceAttackToTriggers(info,start,end,dir);
}
#endif
 
 
//==================================================
// Purpose:	Add bullet to linked list
//			Handle lag compensation with "prebullet simulation"
// Output:	Bullet's index (-1 for error)
//==================================================
int CBulletManager::AddBullet(CSimulatedBullet *pBullet)
{
	if (pBullet->GetAmmoTypeIndex() == -1)
	{
		Msg("ERROR: Undefined ammo type!\n");
		return -1;
	}
	pBullet->m_flTimeStamp = gpGlobals->curtime;
	unsigned short index = g_Bullets.AddToTail(pBullet);
#ifdef CLIENT_DLL
	DevMsg( "Client Bullet Created (%i)\n", index);
	if(g_Bullets.Count()==1)
	{
		SetNextClientThink(gpGlobals->curtime + 0.01f);
	}
#else
	DevMsg( "Bullet Created (%i) LagCompensation %f\n", index, pBullet->bulletinfo.m_flLatency );
	if(pBullet->bulletinfo.m_flLatency !=0.0f)
		pBullet->SimulateBullet(); //Pre-simulation
 
	if(g_Bullets.Count()==1)
	{
		SetNextThink(gpGlobals->curtime + 0.01f);
	}
#endif
	return index;
}
 
 
//==================================================
// Purpose:	Remove the bullet at index from the linked list
// Output:	Next index
//==================================================
void CBulletManager::RemoveBullet(int index)
{
	g_Bullets.Next(index);
#ifdef CLIENT_DLL
	DevMsg("Client ");
#endif
	DevMsg( "Bullet Removed (%i)\n", index );
	g_Bullets.Remove(index);
	if(g_Bullets.Count()==0)
	{
#ifdef CLIENT_DLL
		SetNextClientThink( TICK_NEVER_THINK );
#else
		SetNextThink( TICK_NEVER_THINK );
#endif
	}
}