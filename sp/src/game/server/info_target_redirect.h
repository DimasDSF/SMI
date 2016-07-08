//---------------------------------------------------------------------
// Purpose: Constructor
//---------------------------------------------------------------------

class CInfo_TargetDirect : public CBaseEntity
{
	DECLARE_CLASS( CInfo_TargetDirect, CBaseEntity );
	DECLARE_DATADESC()

	public:
		void	Spawn( void );

		void InputChangeTarget( inputdata_t &inputdata );
		void ChangeTarget();

		//const char *GetTarget();

	private:
		string_t m_targetname;
};