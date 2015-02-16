#include "cbase.h"
#include "hud.h"
#include "text_message.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "view.h"
#include <KeyValues.h>
#include <vgui_controls/AnimationController.h>
#include <vgui/ISurface.h>
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialvar.h"
#include "IEffects.h"
#include "hudelement.h"
#include "clienteffectprecachesystem.h"
#include "sourcevr/isourcevirtualreality.h"

using namespace vgui;

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Purpose: HDU Damage indication
//-----------------------------------------------------------------------------
class CHudFSColor : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudFSColor, vgui::Panel );

public:
	CHudFSColor( const char *pElementName );
	void Init( void );
	void Reset( void );
	virtual bool ShouldDraw( void );

private:
	virtual void Paint();
	virtual void ApplySchemeSettings(vgui::IScheme *pScheme);

private:
	CPanelAnimationVar( Color, m_DmgFullscreenColor, "DmgFullscreenColor", "255 0 0 0" );

	void DrawFullscreenDamageIndicator();

	CMaterialReference m_WhiteAdditiveMaterial;
};

DECLARE_HUDELEMENT( CHudFSColor );

using namespace vgui;
//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudFSColor::CHudFSColor( const char *pElementName ) : CHudElement( pElementName ), BaseClass(NULL, "HudFSColor")
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_WhiteAdditiveMaterial.Init( "vgui/white_additive", TEXTURE_GROUP_VGUI ); 
	
	SetHiddenBits( HIDEHUD_HEALTH );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudFSColor::Reset( void )
{
	m_DmgFullscreenColor[3] = 0;
}

void CHudFSColor::Init( void )
{
	Reset();
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CHudFSColor::ShouldDraw( void )
{
	bool bNeedsDraw = m_DmgFullscreenColor[3];

	return ( bNeedsDraw && CHudElement::ShouldDraw() );
}

//-----------------------------------------------------------------------------
// Purpose: Draws full screen damage fade
//-----------------------------------------------------------------------------
void CHudFSColor::DrawFullscreenDamageIndicator()
{
	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_WhiteAdditiveMaterial );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
	int r = m_DmgFullscreenColor[0], g = m_DmgFullscreenColor[1], b = m_DmgFullscreenColor[2], a = m_DmgFullscreenColor[3];

	float wide = GetWide(), tall = GetTall();

	meshBuilder.Color4ub( r, g, b, a );
	meshBuilder.TexCoord2f( 0,0,0 );
	meshBuilder.Position3f( 0.0f, 0.0f, 0 );
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ub( r, g, b, a );
	meshBuilder.TexCoord2f( 0,1,0 );
	meshBuilder.Position3f( wide, 0.0f, 0 );
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ub( r, g, b, a );
	meshBuilder.TexCoord2f( 0,1,1 );
	meshBuilder.Position3f( wide, tall, 0 );
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ub( r, g, b, a );
	meshBuilder.TexCoord2f( 0,0,1 );
	meshBuilder.Position3f( 0.0f, tall, 0 );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// Purpose: Paints the damage display
//-----------------------------------------------------------------------------
void CHudFSColor::Paint()
{
	// draw fullscreen damage indicators
	DrawFullscreenDamageIndicator();

	// draw side damage indicators
}

//-----------------------------------------------------------------------------
// Purpose: hud scheme settings
//-----------------------------------------------------------------------------
void CHudFSColor::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	SetPaintBackgroundEnabled(false);

	int vx, vy, vw, vh;
	vgui::surface()->GetFullscreenViewport( vx, vy, vw, vh );

	SetForceStereoRenderToFrameBuffer( true );

	SetSize(vw, vh);
}
