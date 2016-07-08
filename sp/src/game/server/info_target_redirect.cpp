//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Points to a info_target and can be changed via input
//
//=============================================================================//

#include "cbase.h"
#include "info_target_redirect.h"

LINK_ENTITY_TO_CLASS( info_target_redirect, CInfo_TargetDirect );

//---------------------------------------------------------------------
// Purpose: Get the targetname and control the change target
//---------------------------------------------------------------------

BEGIN_DATADESC( CInfo_TargetDirect )

	DEFINE_KEYFIELD( m_targetname, FIELD_STRING, "targetdirect" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ChangeTarget", InputChangeTarget ),

END_DATADESC()

//---------------------------------------------------------------------
// Purpose: Spawn
//---------------------------------------------------------------------
void CInfo_TargetDirect::Spawn( void )
{
	BaseClass::Spawn();

	if ( HasSpawnFlags(0x01) )
	{
		SetEFlags( EFL_FORCE_CHECK_TRANSMIT );
	}

	ChangeTarget();
}


//---------------------------------------------------------------------
// Purpose: Change the Target
//---------------------------------------------------------------------
void CInfo_TargetDirect::InputChangeTarget( inputdata_t &inputdata )
{
	m_targetname = MAKE_STRING( inputdata.value.String() );
	ChangeTarget();
}


//---------------------------------------------------------------------
// Purpose: Change the Target
//---------------------------------------------------------------------
void CInfo_TargetDirect::ChangeTarget()
{
	CBaseEntity *m_btargetname = gEntList.FindEntityByName( NULL, m_targetname );

	SetAbsOrigin( m_btargetname->GetAbsOrigin() );
	SetAbsAngles( m_btargetname->GetAbsAngles() );
}

/*const char *CInfo_TargetDirect::GetTarget()
{
	return STRING( m_targetname );
}*/

