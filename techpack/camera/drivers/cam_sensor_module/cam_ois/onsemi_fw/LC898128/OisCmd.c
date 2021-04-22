/**
 * @brief		OIS system command for LC898128
 *
 * @author		Copyright (C) 2016, ON Semiconductor, all right reserved.
 *
 * @file		OisCmd.c
 * @date		svn:$Date:: 2016-06-22 10:57:58 +0900#$
 * @version	svn:$Revision: 59 $
 * @attention
 **/

//**************************
//	Include Header File
//**************************
#define		__OISCMD__

//#include	<stdlib.h>	/* use for abs() */
//#include	<math.h>	/* use for sqrt() */
#include <linux/kernel.h>
#include	"Ois.h"

#define SEL_MODEL 0

//****************************************************
//	MODE SELECTORS (Compile Switches)
//****************************************************
//#define		NEUTRAL_CENTER			// Upper Position Current 0mA Measurement
//#define		NEUTRAL_CENTER_FINE		// Optimize natural center current
//#define		HALL_ADJ_SERVO_ON		// 
//#define	HALLADJ_FULLCURRENT
#define			HALLADJ_NON_SINWAVE
#define			LOOPGAIN_FIX_VALUE

//****************************************************
//	LC898128 calibration parameters 
//****************************************************
#include "LC898128_Calibration_ACT01.h"
//****************************************************
//	CUSTOMER NECESSARY CREATING LIST
//****************************************************
/* for I2C communication */
extern	void RamWrite32A(INT_32 addr, INT_32 data);
extern 	void RamRead32A( UINT_16 addr, void * data );
/* for Wait timer [Need to adjust for your system] */
extern void	WitTim( UINT_16 );


//**************************
//	extern  Function LIST
//**************************
UINT_32	UlBufDat[ 64 ] ;							//!< Calibration data write buffer(256 bytes)

//**************************
//	Local Function Prototype
//**************************
void	IniCmd( void ) ;							//!< Command Execute Process Initial
void	IniPtAve( void ) ;							//!< Average setting
void	MesFil( UINT_8 ) ;							//!< Measure Filter Setting
void	MeasureStart( INT_32 , INT_32 , INT_32 ) ;	//!< Measure Start Function
void	MeasureStart2( INT_32 , INT_32 , INT_32 , UINT_16 );	//!< Measure Start 2 Function
void	MeasureWait( void ) ;						//!< Measure Wait
void	MemoryClear( UINT_16 , UINT_16 ) ;			//!< Memory Cloear
void	SetWaitTime( UINT_16 ) ; 					//!< Set Wait Timer

void	TneOff( UnDwdVal, UINT_8 ) ;				//!< Hall Offset Tuning
void	TneBia( UnDwdVal StTneVal, UINT_8 UcTneAxs, UINT_16 UsHalAdjRange ) ;		//!< Hall Bias Tuning
UINT_32	TnePtp ( UINT_8	UcDirSel, UINT_8	UcBfrAft, ADJ_HALL* p );
UINT_32	TneCen( UINT_8 UcTneAxs, ADJ_HALL* ptr );
UINT_32	LopGan( UINT_8 UcDirSel, ADJ_LOPGAN* ptr );
UINT_32	TneGvc( UINT_8	uc_mode );
UINT_8	TneHvc( void );
void	DacControl( UINT_8 UcMode, UINT_32 UiChannel, UINT_32 PuiData );
void	MeasAddressSelection( UINT_8 mode , INT_32 * measadr_a , INT_32 * measadr_b );
UINT_32	MeasGyAcOffset(  void  );

#ifdef	NEUTRAL_CENTER_FINE
void	TneFin( ADJ_LOPGAN* ptr ) ;							//!< Fine tune for natural center offset
#endif	// NEUTRAL_CENTER_FINE

void	RdHallCalData( void ) ;
void	SetSinWavePara( UINT_8 , UINT_8 ) ;			//!< Sin wave test function
void	SetSineWave( UINT_8 , UINT_8 );
void	SetSinWavGenInt( void );
void	SetTransDataAdr( UINT_16  , UINT_32  ) ;	//!< Hall VC Offset Adjust
//void	GetDir( UINT_8 *outX, UINT_8 *outY ) ;
void	MesFil2( UINT_16	UsMesFreq );
UINT_8 GetInfomationAfterStartUp( DSPVER* Info );
void	SetGyroCoef( UINT_8  );
void	SetAccelCoef( UINT_8 );


//**************************
//	define
//**************************
#define 	HALL_ADJ		0
#define 	LOOPGAIN		1
#define 	THROUGH			2
#define 	NOISE			3
#define		OSCCHK			4
#define		GAINCURV		5
#define		SELFTEST		6
#define 	LOOPGAIN2		7

// Measure Mode
#define		PTP_BEFORE		0
#define		PTP_AFTER		1

 #define 	TNE 			80						//!< Waiting Time For Movement
 #define 	OFFSET_DIV		2						//!< Divide Difference For Offset Step
 #define 	TIME_OUT		40						//!< Time Out Count

 #define	BIAS_HLMT		(UINT_32)0xBF000000
 #define	BIAS_LLMT		(UINT_32)0x20000000

/************** posture check ************/
#define		SENSITIVITY		4096	// LSB/g
#define		PSENS_MARG		(4096 / 4)	// 1/4g
#define		POSTURETH_P		(SENSITIVITY - PSENS_MARG)	// LSB/g
/************** posture check ************/

// Threshold of osciration amplitude
#define ULTHDVAL	0x01000000								// Threshold of the hale value

//**************************
//	Global Variable
//**************************
INT_16		SsNvcX = 1 ;									// NVC move direction X
INT_16		SsNvcY = 1 ;									// NVC move direction Y
UINT_8	BeforeControl;

//**************************
//	Const
//**************************

//********************************************************************************
// Function Name 	: MemClr
// Retun Value		: void
// Argment Value	: Clear Target PoINT_32er, Clear Byte Number
// Explanation		: Memory Clear Function
// History			: First edition
//********************************************************************************
void	MemClr( UINT_8	*NcTgtPtr, UINT_16	UsClrSiz )
{
	UINT_16	UsClrIdx ;

	for ( UsClrIdx = 0 ; UsClrIdx < UsClrSiz ; UsClrIdx++ )
	{
		*NcTgtPtr	= 0 ;
		NcTgtPtr++ ;
	}
}

//********************************************************************************
// Function Name 	: HallAdj
// Retun Value		: Hall Tuning SUCCESS or FAILURE
// Argment Value	: NON
// Explanation		: Hall System Auto Adjustment Function
// History			: First edition 						
//********************************************************************************
UINT_32 HallAdj( ADJ_HALL* Ptr , ADJ_LOPGAN* LpPtr )
{
	UINT_32	UlHlxSts, UlHlySts, UlReadVal;
	
	RtnCen( BOTH_OFF ) ;		// Both OFF
	WitTim( TNE ) ;
	RamWrite32A( HALL_RAM_HXOFF,  0x00000000 ) ;		// X Offset Clr
	RamWrite32A( HALL_RAM_HYOFF,  0x00000000 ) ;		// Y Offset Clr
	RamWrite32A( HallFilterCoeffX_hxgain0 , LpPtr->Hxgain ) ;
	RamWrite32A( HallFilterCoeffY_hygain0 , LpPtr->Hygain ) ;
	
	DacControl( 0, HLXBO , Ptr->XBiasInit ) ;
	RamWrite32A( StCaliData_UiHallBias_X , Ptr->XBiasInit ) ;
	DacControl( 0, HLYBO , Ptr->YBiasInit ) ;
	RamWrite32A( StCaliData_UiHallBias_Y , Ptr->YBiasInit ) ;
	DacControl( 0, HLXO, Ptr->XOffsetInit ) ;
	RamWrite32A( StCaliData_UiHallOffset_X , Ptr->XOffsetInit ) ;
	DacControl( 0, HLYO, Ptr->YOffsetInit ) ;
	RamWrite32A( StCaliData_UiHallOffset_Y , Ptr->YOffsetInit ) ;
	
	BeforeControl=1;
	UlHlySts = TneCen( Y_DIR, Ptr ) ;
	StAdjPar.StHalAdj.UsAdyOff = StAdjPar.StHalAdj.UsHlyCna  ;
	RamWrite32A( HALL_RAM_HYOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdyOff << 16 ) & 0xFFFF0000 )) ;
	RtnCen( YONLY_ON ) ;		// Y Servo ON
#if 0	
	// Recovery
	if( UlHlySts == EXE_HXADJ){
		Ptr->ActMaxDrive_X = 0x7FFFFFFF;
		Ptr->ActMinDrive_X = 0x80000000;
	}
#endif	
	WitTim( TNE ) ;
	BeforeControl=1;
	UlHlxSts = TneCen( X_DIR, Ptr ) ;
	StAdjPar.StHalAdj.UsAdxOff = StAdjPar.StHalAdj.UsHlxCna  ;
	RamWrite32A( HALL_RAM_HXOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdxOff << 16 ) & 0xFFFF0000 )) ;
	RtnCen( XONLY_ON ) ;		// X Servo ON
#if 0	
	// Recovery	
	if( UlHlxSts == EXE_HXADJ){
		Ptr->ActMaxDrive_X = 0x7FFFFFFF;
		Ptr->ActMinDrive_X = 0x80000000;
	}
#endif	
	WitTim( TNE ) ;
	UlHlySts = TneCen( Y_DIR, Ptr ) ;
	StAdjPar.StHalAdj.UsAdyOff = StAdjPar.StHalAdj.UsHlyCna  ;
	RamWrite32A( HALL_RAM_HYOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdyOff << 16 ) & 0xFFFF0000 )) ;
	RtnCen( YONLY_ON ) ;		// Y Servo ON

	WitTim( TNE ) ;
	UlHlxSts = TneCen( X_DIR, Ptr ) ;
	StAdjPar.StHalAdj.UsAdxOff = StAdjPar.StHalAdj.UsHlxCna  ;
	RamWrite32A( HALL_RAM_HXOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdxOff << 16 ) & 0xFFFF0000 )) ;

	if( ( UlHlySts | UlHlxSts ) == EXE_END ){
		RtnCen( BOTH_ON ) ;		// Both Servo ON
	}
	else{
		RtnCen( BOTH_OFF ) ;	// Both OFF
	}

	WitTim( TNE ) ;	

	RamRead32A( StCaliData_UiHallOffset_X , &UlReadVal ) ;
	StAdjPar.StHalAdj.UsHlxOff = (UINT_16)( UlReadVal >> 16 ) ;
		
	RamRead32A( StCaliData_UiHallBias_X , &UlReadVal ) ;
	StAdjPar.StHalAdj.UsHlxGan = (UINT_16)( UlReadVal >> 16 ) ;
		
	RamRead32A( StCaliData_UiHallOffset_Y , &UlReadVal ) ;
	StAdjPar.StHalAdj.UsHlyOff = (UINT_16)( UlReadVal >> 16 ) ;
		
	RamRead32A( StCaliData_UiHallBias_Y , &UlReadVal ) ;
	StAdjPar.StHalAdj.UsHlyGan = (UINT_16)( UlReadVal >> 16 ) ;
#ifdef	NEUTRAL_CENTER
	RtnCen( BOTH_OFF ) ;		// Both OFF
	WitTim( TNE ) ;
#else
#endif
	return ( UlHlySts | UlHlxSts );
}


//********************************************************************************
// Function Name 	: TneRun
// Retun Value		: Hall Tuning SUCCESS or FAILURE
// Argment Value	: NON
// Explanation		: Hall System Auto Adjustment Function
// History			: First edition 						
//********************************************************************************
UINT_32	TneRun( void )
{
	UINT_32	UlFinSts, UlReadVal;
	ADJ_HALL* HallPtr;
	ADJ_LOPGAN* LopgainPtr;
	DSPVER Info;
	
	// Check the status
	RamWrite32A( CMD_IO_ADR_ACCESS , ROMINFO );
	RamRead32A( CMD_IO_DAT_ACCESS, &UlReadVal );
	if( UlReadVal != 0x0A)	return( EXE_ERR );	// 

	// Select parameter
	if( GetInfomationAfterStartUp( &Info ) != 0) return( EXE_ERR );
	else if( Info.ActType == 0x01 ) {
		HallPtr = (ADJ_HALL*)&ACT01_HallCalParameter;
		LopgainPtr = (ADJ_LOPGAN* )&ACT01_LoopGainParameter;
	}
	else{
		return( EXE_ERR );
	}
	// F015 Command Check
	if(Info.GyroType == 0xFF)  return( EXE_ERR );

	/* Hall Adjustment */
	UlFinSts = HallAdj( HallPtr , LopgainPtr);
	if( ((UlFinSts & EXE_HXADJ) == EXE_HXADJ) || ((UlFinSts & EXE_HYADJ) == EXE_HYADJ) ) return ( UlFinSts );

	/* Hall Offser (neutral center)*/
#ifdef	NEUTRAL_CENTER
	TneHvc();
 #ifdef	NEUTRAL_CENTER_FINE
	TneFin( LopgainPtr );
 #endif
 
	RamWrite32A( HallFilterCoeffX_hxgain0 , LopgainPtr->Hxgain ) ;
	RamWrite32A( HallFilterCoeffY_hygain0 , LopgainPtr->Hygain ) ;
	RtnCen( BOTH_ON ) ;		// Y ON / X ON
	WitTim( TNE ) ;
#endif

	/* Loop gain Adjustment */

	UlFinSts |= LopGan( Y_DIR, LopgainPtr ) ;	// Y Loop Gain Adjust
	UlFinSts |= LopGan( X_DIR, LopgainPtr ) ;	// X Loop Gain Adjust
	
	/* Gyro DC offset Adjustment */
#ifdef __OIS_UIOIS_GYRO_USE__
#else
	UlFinSts |= TneGvc(0) ;
#ifdef	SEL_SHIFT_COR
	UlFinSts |= TneGvc(1) ;
	UlFinSts |= TneAvc(0x10);
//	UlFinSts |= TneAvc(0x11);
	if( (UlFinSts & (EXE_AXADJ | EXE_AYADJ | EXE_AZADJ)) == EXE_END ){
		TneAvc(0x80);
	}
#endif	//SEL_SHIFT_COR
#endif
	
	/* confirmation of hall stroke */
	StAdjPar.StHalAdj.UlAdjPhs = UlFinSts ;
	return( UlFinSts ) ;
}


//********************************************************************************
// Function Name 	: TnePtp
// Retun Value		: Hall Top & Bottom Gaps
// Argment Value	: X,Y Direction, Adjust Before After Parameter
// Explanation		: Measuring Hall Paek To Peak
// History			: First edition 						
//********************************************************************************
UINT_32	TnePtp ( UINT_8	UcDirSel, UINT_8	UcBfrAft, ADJ_HALL* p )
{
#ifdef	HALLADJ_NON_SINWAVE

	UnDwdVal	StTneVal ;
	INT_32		SlMeasureParameterA , SlMeasureParameterB ;
	INT_32		SlMeasureParameterNum ;
	INT_32		SlMeasureMaxValue, SlMeasureMinValue ;
	UINT_16		us_outaddress ;
	INT_32		sl_act_min_drv, sl_act_max_drv ;

TRACE("TnePtp\n ") ;

	MesFil( THROUGH ) ;					// Filter setting for measurement

	if( UcDirSel == X_DIR ) {								// X axis
		SlMeasureParameterA		=	HALL_RAM_HXIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HYIDAT ;		// Set Measure RAM Address
		us_outaddress			=	HALL_RAM_SINDX1 ;
		sl_act_max_drv			=	p->ActMinDrive_X ;
		sl_act_min_drv			=	p->ActMaxDrive_X ;
	} else if( UcDirSel == Y_DIR ) {						// Y axis
		SlMeasureParameterA		=	HALL_RAM_HYIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HXIDAT ;		// Set Measure RAM Address
		us_outaddress			=	HALL_RAM_SINDY1 ;
		sl_act_max_drv			=	p->ActMinDrive_Y ;
		sl_act_min_drv			=	p->ActMaxDrive_Y ;
	}
	
	SlMeasureParameterNum	=	2000 ;
	
	RamWrite32A( us_outaddress, sl_act_min_drv ) ;
	MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
	MeasureWait() ;						// Wait complete of measurement
	RamRead32A( StMeasFunc_MFA_SiMin1 , ( unsigned long * )&SlMeasureMinValue ) ;	// Min value

	RamWrite32A( us_outaddress, sl_act_max_drv ) ;
	MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
	MeasureWait() ;						// Wait complete of measurement
	RamRead32A( StMeasFunc_MFA_SiMax1 , ( unsigned long * )&SlMeasureMaxValue ) ;	// Max value

	StTneVal.StDwdVal.UsHigVal = (unsigned short)((SlMeasureMaxValue >> 16) & 0x0000FFFF );
	StTneVal.StDwdVal.UsLowVal = (unsigned short)((SlMeasureMinValue >> 16) & 0x0000FFFF );
	
	RamWrite32A( us_outaddress, 0 ) ;

#else	// HALLADJ_NON_SINWAVE

	UnDwdVal		StTneVal ;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureMaxValue , SlMeasureMinValue ;
	UINT_16	UsSinAdr ;

	INT_32			sl_act_min_drv, sl_act_max_drv;	
	INT_32			sl_act_num, sl_act_step, sl_act_wait, i ;	

TRACE("TnePtp\n") ;

	SetSinWavGenInt();
	SetTransDataAdr( SinWave_OutAddr	,	SinWave_Output ) ;		// 出力先アドレス
	SetTransDataAdr( CosWave_OutAddr	,	CosWave_OutAddr );		// 出力先アドレス

	if( UcDirSel == X_DIR ) {								// X axis
		SlMeasureParameterA		=	HALL_RAM_HXIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HYIDAT ;		// Set Measure RAM Address
		sl_act_max_drv		= (INT_32)(p->ActMinDrive_X) ;
		sl_act_min_drv		= (INT_32)(p->ActMaxDrive_X) ;	
		sl_act_num			= (INT_32)(p->ActStep_X_Num);
		sl_act_step			= (INT_32)((p->ActStep_X)*(-1));
		sl_act_wait			= (INT_32)(p->ActStep_X_time);
		UsSinAdr = HALL_RAM_SINDX1;

	} else if( UcDirSel == Y_DIR ) {						// Y axis
		SlMeasureParameterA		=	HALL_RAM_HYIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HXIDAT ;		// Set Measure RAM Address
		sl_act_max_drv		= (INT_32)(p->ActMinDrive_Y) ;
		sl_act_min_drv		= (INT_32)(p->ActMaxDrive_Y) ;
		sl_act_num			= (INT_32)(p->ActStep_Y_Num);
		sl_act_step			= (INT_32)((p->ActStep_Y)*(-1));		
		sl_act_wait			= (INT_32)(p->ActStep_Y_time);			
		UsSinAdr = HALL_RAM_SINDY1;

	}
	
	MesFil( THROUGH ) ;					// Filter setting for measurement

	for( i=0 ; i <= sl_act_num; i++ ){
		RamWrite32A( UsSinAdr, (sl_act_min_drv + ((sl_act_step) * (sl_act_num - i))) );
		WitTim(sl_act_wait);
TRACE("Min:%08x-",  (sl_act_min_drv + ((sl_act_step) * (sl_act_num - i))) );
	} 

	WitTim( p->WaitTime );
	MeasureStart( p->SinNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
	MeasureWait() ;						// Wait complete of measurement
	RamRead32A( StMeasFunc_MFA_SiMin1 , ( UINT_32 * )&SlMeasureMinValue ) ;	// Min value

	RamWrite32A( UsSinAdr		,	0x00000000 ) ;				// DelayRam Clear
TRACE("Min:0x00000000\n");	

	for( i=0 ; i <= sl_act_num; i++ ){
		RamWrite32A( UsSinAdr, (sl_act_max_drv - ((sl_act_step) * (sl_act_num -i))) );
		WitTim(sl_act_wait);
TRACE("Max:%08x-",  (sl_act_max_drv - ((sl_act_step) * (sl_act_num -i))) );
	} 

	WitTim( p->WaitTime );
	MeasureStart( p->SinNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
	MeasureWait() ;						// Wait complete of measurement
	RamRead32A( StMeasFunc_MFA_SiMin1 , ( UINT_32 * )&SlMeasureMaxValue ) ;	// Min value
		
	RamWrite32A( UsSinAdr		,	0x00000000 ) ;				// DelayRam Clear
TRACE("Max:0x00000000\n");		


	StTneVal.StDwdVal.UsHigVal = (UINT_16)((SlMeasureMaxValue >> 16) & 0x0000FFFF );
	StTneVal.StDwdVal.UsLowVal = (UINT_16)((SlMeasureMinValue >> 16) & 0x0000FFFF );
	
#endif	// HALLADJ_NON_SINWAVE

	if( UcBfrAft == 0 ) {
		if( UcDirSel == X_DIR ) {
			StAdjPar.StHalAdj.UsHlxCen	= ( ( INT_16 )StTneVal.StDwdVal.UsHigVal + ( INT_16 )StTneVal.StDwdVal.UsLowVal ) / 2 ;
			StAdjPar.StHalAdj.UsHlxMax	= StTneVal.StDwdVal.UsHigVal ;
			StAdjPar.StHalAdj.UsHlxMin	= StTneVal.StDwdVal.UsLowVal ;
		} else if( UcDirSel == Y_DIR ){
			StAdjPar.StHalAdj.UsHlyCen	= ( ( INT_16 )StTneVal.StDwdVal.UsHigVal + ( INT_16 )StTneVal.StDwdVal.UsLowVal ) / 2 ;
			StAdjPar.StHalAdj.UsHlyMax	= StTneVal.StDwdVal.UsHigVal ;
			StAdjPar.StHalAdj.UsHlyMin	= StTneVal.StDwdVal.UsLowVal ;
		}
	} else {
		if( UcDirSel == X_DIR ){
			StAdjPar.StHalAdj.UsHlxCna	= ( ( INT_16 )StTneVal.StDwdVal.UsHigVal + ( INT_16 )StTneVal.StDwdVal.UsLowVal ) / 2 ;
			StAdjPar.StHalAdj.UsHlxMxa	= StTneVal.StDwdVal.UsHigVal ;
			StAdjPar.StHalAdj.UsHlxMna	= StTneVal.StDwdVal.UsLowVal ;
		} else if( UcDirSel == Y_DIR ){
			StAdjPar.StHalAdj.UsHlyCna	= ( ( INT_16 )StTneVal.StDwdVal.UsHigVal + ( INT_16 )StTneVal.StDwdVal.UsLowVal ) / 2 ;
			StAdjPar.StHalAdj.UsHlyMxa	= StTneVal.StDwdVal.UsHigVal ;
			StAdjPar.StHalAdj.UsHlyMna	= StTneVal.StDwdVal.UsLowVal ;
		}
	}

TRACE("		ADJ(%d) MAX = %04x, MIN = %04x, CNT = %04x, ", UcDirSel, StTneVal.StDwdVal.UsHigVal, StTneVal.StDwdVal.UsLowVal, ( ( signed int )StTneVal.StDwdVal.UsHigVal + ( signed int )StTneVal.StDwdVal.UsLowVal ) / 2 ) ;
	StTneVal.StDwdVal.UsHigVal	= 0x7fff - StTneVal.StDwdVal.UsHigVal ;		// Maximum Gap = Maximum - Hall Peak Top
	StTneVal.StDwdVal.UsLowVal	= StTneVal.StDwdVal.UsLowVal - 0x8000 ; 	// Minimum Gap = Hall Peak Bottom - Minimum

TRACE("	GapH = %04x, GapL = %04x\n", StTneVal.StDwdVal.UsHigVal, StTneVal.StDwdVal.UsLowVal ) ;
TRACE("		Raw MAX = %08x, MIN = %08x\n", (unsigned int)SlMeasureMaxValue , (unsigned int)SlMeasureMinValue ) ;
	
	return( StTneVal.UlDwdVal ) ;
}

//********************************************************************************
// Function Name 	: TneCen
// Retun Value		: Hall Center Tuning Result
// Argment Value	: X,Y Direction, Hall Top & Bottom Gaps
// Explanation		: Hall Center Tuning Function
// History			: First edition 						
//********************************************************************************
UINT_32	TneCen( UINT_8 UcTneAxs, ADJ_HALL* ptr )
{
	UnDwdVal		StTneVal ;
	UINT_8 	UcTmeOut =1, UcTofRst= FAILURE ;
	UINT_16	UsBiasVal ;
	UINT_32	UlTneRst = FAILURE, UlBiasVal , UlValNow ;
	UINT_16	UsValBef,UsValNow ;
	UINT_32	UlBiaBef,UlBiaNow ;
	UINT_16	UsTargetMax, UsTargetMin;
	UINT_16	UsTargetRange;
	
	if (BeforeControl != 0)	StTneVal.UlDwdVal	= TnePtp( UcTneAxs , PTP_BEFORE, ptr ) ;
	else					StTneVal.UlDwdVal	= TnePtp( UcTneAxs , PTP_AFTER , ptr ) ;
	BeforeControl=0;

	TneOff( StTneVal, UcTneAxs ) ;
	UcTofRst	= SUCCESS ;				/* 暫定でOKにする */

	while ( UlTneRst && (UINT_32)UcTmeOut )
	{
		if( UcTofRst == FAILURE ) {
TRACE(" UcTofRst == FAILURE\n" ) ;
			TneOff( StTneVal, UcTneAxs ) ;
			StTneVal.UlDwdVal = TnePtp( UcTneAxs, PTP_AFTER, ptr ) ;
		} else {
TRACE(" else\n" ) ;
			if( UcTneAxs == X_DIR ) {
				RamRead32A( StCaliData_UiHallBias_X , &UlBiaBef ) ;		
				UsTargetRange = (UINT_16)ptr->XTargetRange;
			} else if( UcTneAxs == Y_DIR ) {
				RamRead32A( StCaliData_UiHallBias_Y , &UlBiaBef ) ;		
				UsTargetRange = (UINT_16)ptr->YTargetRange;
			}
			TneBia( StTneVal, UcTneAxs, UsTargetRange ) ;
			if( UcTneAxs == X_DIR ) {
				RamRead32A( StCaliData_UiHallBias_X , &UlBiaNow ) ;		
			} else if( UcTneAxs == Y_DIR ) {
				RamRead32A( StCaliData_UiHallBias_Y , &UlBiaNow ) ;		
			}
			if((( UlBiaBef == BIAS_HLMT ) && ( UlBiaNow == BIAS_HLMT ))
			|| (( UlBiaBef == BIAS_LLMT ) && ( UlBiaNow == BIAS_LLMT ))){
				UcTmeOut += 10;
TRACE("	No = %04d (bias count up)\n", UcTmeOut ) ;
			}
			StTneVal.UlDwdVal	= TnePtp( UcTneAxs , PTP_AFTER, ptr ) ;

			UcTofRst	= FAILURE ;
//			if( UcTneAxs == X_DIR ) {
//				RamRead32A( StCaliData_UiHallBias_X , &UlBiasVal ) ;
//			}else if( UcTneAxs == Y_DIR ){
//				RamRead32A( StCaliData_UiHallBias_Y , &UlBiasVal ) ;
//			}
//			if(UlBiasVal == 0x00000000){
//				UcTmeOut = TIME_OUT;
//			}
		}

		if( (StTneVal.StDwdVal.UsHigVal > ptr->OffsetMargin ) && (StTneVal.StDwdVal.UsLowVal > ptr->OffsetMargin ) )	/* position check */
		{
			UcTofRst	= SUCCESS ;
TRACE("  TofR = SUCC\n" ) ;
			UsValBef = UsValNow = 0x0000 ;
		}else if( (StTneVal.StDwdVal.UsHigVal <= ptr->OffsetMargin ) && (StTneVal.StDwdVal.UsLowVal <= ptr->OffsetMargin ) ){
			UcTofRst	= SUCCESS ;
			UlTneRst	= (UINT_32)FAILURE ;
		}else{
			UcTofRst	= FAILURE ;
TRACE("  TofR = FAIL\n" ) ;
			
			UsValBef = UsValNow ;

			if( UcTneAxs == X_DIR  ) {
				RamRead32A( StCaliData_UiHallOffset_X , &UlValNow ) ;
				UsValNow = (UINT_16)( UlValNow >> 16 ) ;
			}else if( UcTneAxs == Y_DIR ){
				RamRead32A( StCaliData_UiHallOffset_Y , &UlValNow ) ;
				UsValNow = (UINT_16)( UlValNow >> 16 ) ;
			}
			if( ((( UsValBef & 0xFF00 ) == 0x1000 ) && ( UsValNow & 0xFF00 ) == 0x1000 )
			 || ((( UsValBef & 0xFF00 ) == 0xEF00 ) && ( UsValNow & 0xFF00 ) == 0xEF00 ) )
			{
				UcTmeOut += 10;
TRACE("	No = %04d (offset count up)\n", UcTmeOut ) ;
				if( UcTneAxs == X_DIR ) {
					RamRead32A( StCaliData_UiHallBias_X , &UlBiasVal ) ;
					UsBiasVal = (UINT_16)( UlBiasVal >> 16 ) ;
				}else if( UcTneAxs == Y_DIR ){
					RamRead32A( StCaliData_UiHallBias_Y , &UlBiasVal ) ;
					UsBiasVal = (UINT_16)( UlBiasVal >> 16 ) ;
				}
				
				if( UsBiasVal > ptr->DecrementStep )
				{
					UsBiasVal -= ptr->DecrementStep ;
				}
				
				if( UcTneAxs == X_DIR ) {
					UlBiasVal = ( UINT_32 )( UsBiasVal << 16 ) ;
					DacControl( 0, HLXBO , UlBiasVal ) ;
					RamWrite32A( StCaliData_UiHallBias_X , UlBiasVal ) ;
				}else if( UcTneAxs == Y_DIR ){
					UlBiasVal = ( UINT_32 )( UsBiasVal << 16 ) ;
					DacControl( 0, HLYBO , UlBiasVal ) ;
					RamWrite32A( StCaliData_UiHallBias_Y , UlBiasVal ) ;
				}
			}

		}
		if(UcTneAxs == X_DIR){
			UsTargetMax = (UINT_16)ptr->XTargetMax ;
			UsTargetMin = (UINT_16)ptr->XTargetMin ;
		}else{
			UsTargetMax = (UINT_16)ptr->YTargetMax ;
			UsTargetMin = (UINT_16)ptr->YTargetMin ;
		}
		
		if((( (UINT_16)0xFFFF - ( StTneVal.StDwdVal.UsHigVal + StTneVal.StDwdVal.UsLowVal )) < UsTargetMax )
		&& (( (UINT_16)0xFFFF - ( StTneVal.StDwdVal.UsHigVal + StTneVal.StDwdVal.UsLowVal )) > UsTargetMin ) ) {
			if(UcTofRst	== SUCCESS)
			{
				UlTneRst	= (UINT_32)SUCCESS ;
				break ;
			}
		}
		UlTneRst	= (UINT_32)FAILURE ;
		UcTmeOut++ ;
TRACE("  Tne = FAIL\n" ) ;

TRACE("	No = %04d", UcTmeOut ) ;
		if ( UcTmeOut >= TIME_OUT ) {
			UcTmeOut	= 0 ;
		}		 																							// Set Time Out Count
	}

	SetSinWavGenInt() ;		// 
	
	if( UlTneRst == (UINT_32)FAILURE ) {
		if( UcTneAxs == X_DIR ) {
			UlTneRst					= EXE_HXADJ ;
			StAdjPar.StHalAdj.UsHlxGan	= 0xFFFF ;
			StAdjPar.StHalAdj.UsHlxOff	= 0xFFFF ;
		}else if( UcTneAxs == Y_DIR ) {
			UlTneRst					= EXE_HYADJ ;
			StAdjPar.StHalAdj.UsHlyGan	= 0xFFFF ;
			StAdjPar.StHalAdj.UsHlyOff	= 0xFFFF ;
		}
	} else {
		UlTneRst	= EXE_END ;
	}

	return( UlTneRst ) ;
}



//********************************************************************************
// Function Name 	: TneBia
// Retun Value		: Hall Top & Bottom Gaps
// Argment Value	: Hall Top & Bottom Gaps , X,Y Direction
// Explanation		: Hall Bias Tuning Function
// History			: First edition 						
//********************************************************************************
void TneBia( UnDwdVal StTneVal, UINT_8 UcTneAxs, UINT_16 UsHalAdjRange )
{
	UINT_32			UlSetBia;

TRACE("TneBia\n " ) ;
	if( UcTneAxs == X_DIR ) {
		RamRead32A( StCaliData_UiHallBias_X , &UlSetBia ) ;
	} else if( UcTneAxs == Y_DIR ) {
		RamRead32A( StCaliData_UiHallBias_Y , &UlSetBia ) ;
	}

TRACE("		UlSetBia = %08x\n ", (unsigned int)UlSetBia ) ;
	if( UlSetBia == 0x00000000 )	UlSetBia = 0x01000000 ;
	UlSetBia = (( UlSetBia >> 16 ) & (UINT_32)0x0000FF00 ) ;
	UlSetBia *= (UINT_32)UsHalAdjRange ;
	if(( StTneVal.StDwdVal.UsHigVal + StTneVal.StDwdVal.UsLowVal ) == 0xFFFF ){
		UlSetBia = BIAS_HLMT ;
	}else{
		UlSetBia /= (UINT_32)( 0xFFFF - ( StTneVal.StDwdVal.UsHigVal + StTneVal.StDwdVal.UsLowVal )) ;
		if( UlSetBia > (UINT_32)0x0000FFFF )		UlSetBia = 0x0000FFFF ;
		UlSetBia = ( UlSetBia << 16 ) ;
		if( UlSetBia > BIAS_HLMT )		UlSetBia = BIAS_HLMT ;
		if( UlSetBia < BIAS_LLMT )		UlSetBia = BIAS_LLMT ;
	}

	if( UcTneAxs == X_DIR ) {
		DacControl( 0, HLXBO , UlSetBia ) ;
TRACE("		HLXBO = %08x\n ",  (unsigned int)UlSetBia ) ;
		RamWrite32A( StCaliData_UiHallBias_X , UlSetBia) ;
	} else if( UcTneAxs == Y_DIR ){
		DacControl( 0, HLYBO , UlSetBia ) ;
TRACE("		HLYBO = %08x\n ",  (unsigned int)UlSetBia ) ;
		RamWrite32A( StCaliData_UiHallBias_Y , UlSetBia) ;
	}
TRACE("		( AXIS = %02x , BIAS = %08xh ) , \n", UcTneAxs , (unsigned int)UlSetBia ) ;
}


//********************************************************************************
// Function Name 	: TneOff
// Retun Value		: Hall Top & Bottom Gaps
// Argment Value	: Hall Top & Bottom Gaps , X,Y Direction
// Explanation		: Hall Offset Tuning Function
// History			: First edition 						
//********************************************************************************
void TneOff( UnDwdVal StTneVal, UINT_8 UcTneAxs )
{
	UINT_32	UlSetOff ;
	UINT_32	UlSetVal ;
	
TRACE("TneOff\n ") ;
	if( UcTneAxs == X_DIR ) {
		RamRead32A( StCaliData_UiHallOffset_X , &UlSetOff ) ;
	} else if( UcTneAxs == Y_DIR ){
		RamRead32A( StCaliData_UiHallOffset_Y , &UlSetOff ) ;
	}
	UlSetOff 	= ( UlSetOff >> 16 ) ;

	if ( StTneVal.StDwdVal.UsHigVal > StTneVal.StDwdVal.UsLowVal ) {
		UlSetVal	= ( UINT_32 )(( StTneVal.StDwdVal.UsHigVal - StTneVal.StDwdVal.UsLowVal ) / OFFSET_DIV ) ;	// Calculating Value For Increase Step
		UlSetOff	+= UlSetVal ;	// Calculating Value For Increase Step
		if( UlSetOff > 0x0000FFFF )		UlSetOff = 0x0000FFFF ;
	} else {
		UlSetVal	= ( UINT_32 )(( StTneVal.StDwdVal.UsLowVal - StTneVal.StDwdVal.UsHigVal ) / OFFSET_DIV ) ;	// Calculating Value For Decrease Step
		if( UlSetOff < UlSetVal ){
			UlSetOff	= 0x00000000 ;
		}else{
			UlSetOff	-= UlSetVal ;	// Calculating Value For Decrease Step
		}
	}

TRACE("		UlSetOff = %08x\n ",  (unsigned int)UlSetOff ) ;
	if( UlSetOff > ( INT_32 )0x0000EFFF ) {
		UlSetOff	= 0x0000EFFF ;
	} else if( UlSetOff < ( INT_32 )0x00001000 ) {
		UlSetOff	= 0x00001000 ;
	}

	UlSetOff = ( UlSetOff << 16 ) ;
	
	if( UcTneAxs == X_DIR ) {
		DacControl( 0, HLXO, UlSetOff ) ;
TRACE("		HLXO = %08x\n ",  (unsigned int)UlSetOff ) ;
		RamWrite32A( StCaliData_UiHallOffset_X , UlSetOff ) ;
	} else if( UcTneAxs == Y_DIR ){
		DacControl( 0, HLYO, UlSetOff ) ;
TRACE("		HLYO = %08x\n ",  (unsigned int)UlSetOff ) ;
		RamWrite32A( StCaliData_UiHallOffset_Y , UlSetOff ) ;
	}
TRACE("		( AXIS = %02x , OFST = %08xh ) , \n", UcTneAxs , (unsigned int)UlSetOff ) ;

}


//********************************************************************************
// Function Name 	: LopGan
// Retun Value		: Execute Result
// Argment Value	: X,Y Direction
// Explanation		: Loop Gain Adjust Function
// History			: First edition 						
//********************************************************************************
extern void	MesStart_FRA_Single( UINT_8	UcDirSel );
extern void	MesEnd_FRA_Sweep( void );
#include	"OisFRA.h"

UINT_32	LopGan( UINT_8 UcDirSel, ADJ_LOPGAN* ptr )
{

	UINT_32			UlReturnState ;

#ifdef	LOOPGAIN_FIX_VALUE	
//	UnFltVal		UnMesResult ;
/* 
	StFRAParam.StHostCom.UcAvgCycl	= 3 ;
	StFRAParam.StHostCom.SfAmpCom.SfFltVal	= 100.0f;
	StFRAParam.StHostCom.SfFrqCom.SfFltVal 	= 20.0f ;
	MesStart_FRA_Single( UcDirSel ) ;	
	MesEnd_FRA_Sweep() ;
//	StFRAParam.StMesRslt.SfGainAvg ;
*/
	if( UcDirSel == X_DIR ) {							// X axis
		RamWrite32A( HallFilterCoeffX_hxgain0 , ptr->Hxgain ) ;
		StAdjPar.StLopGan.UlLxgVal = ptr->Hxgain ;
		UlReturnState = EXE_END ;
	} else if( UcDirSel == Y_DIR ){						// Y axis
		RamWrite32A( HallFilterCoeffY_hygain0 , ptr->Hygain ) ;
		StAdjPar.StLopGan.UlLygVal = ptr->Hygain ;
		UlReturnState = EXE_END ;
	}

#else	// LOOPGAIN_FIX_VALUE
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	UINT_64			UllCalculateVal ;
	UINT_16			UsSinAdr ;
	UINT_32			UlFreq, UlGain;
	INT_32			SlNum ;
		
	if( UcDirSel == X_DIR ) {		// X axis
//		SlMeasureParameterA		=	HALL_RAM_HXOUT1 ;		// Set Measure RAM Address
//		SlMeasureParameterB		=	HALL_RAM_HXLOP ;		// Set Measure RAM Address
		SlMeasureParameterA		=	HALL_RAM_HXOUT3	 ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HALL_X_OUT ;		// Set Measure RAM Address
		
		RamWrite32A( HallFilterCoeffX_hxgain0 , ptr->Hxgain ) ;
		UsSinAdr = HALL_RAM_SINDX1;
//		UsSinAdr = HALL_RAM_SINDX0;
		UlFreq = ptr->XNoiseFreq;
		UlGain = ptr->XNoiseGain;
		SlNum  = ptr->XNoiseNum;
	} else if( UcDirSel == Y_DIR ){						// Y axis
//		SlMeasureParameterA		=	HALL_RAM_HYOUT1 ;		// Set Measure RAM Address
//		SlMeasureParameterB		=	HALL_RAM_HYLOP ;		// Set Measure RAM Address
		SlMeasureParameterA		=	HALL_RAM_HYOUT3 ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HALL_RAM_HALL_Y_OUT ;		// Set Measure RAM Address

		RamWrite32A( HallFilterCoeffY_hygain0 , ptr->Hygain ) ;
		UsSinAdr = HALL_RAM_SINDY1;
//		UsSinAdr = HALL_RAM_SINDY0;
		UlFreq = ptr->YNoiseFreq;
		UlGain = ptr->YNoiseGain;
		SlNum  = ptr->YNoiseNum;
	}
	
	SetSinWavGenInt();
	SetTransDataAdr( SinWave_OutAddr	,	SinWave_Output ) ;		// 出力先アドレス
	SetTransDataAdr( CosWave_OutAddr	,	CosWave_OutAddr );		// 出力先アドレス

	RamWrite32A( SinWave_Offset		,	UlFreq ) ;								// Freq Setting
	RamWrite32A( SinWave_Gain		,	UlGain ) ;								// Set Sine Wave Gain					
TRACE("		Loop Gain %d , Freq %08xh, Gain %08xh , Num %08xh \n", UcDirSel , UlFreq , UlGain , SlNum ) ;
	RamWrite32A( SinWaveC_Regsiter	,	0x00000001 ) ;								// Sine Wave Start

	SetTransDataAdr( SinWave_OutAddr	,	( UINT_32 )UsSinAdr ) ;	// Set Sine Wave Input RAM
	
	MesFil( LOOPGAIN2 ) ;					// Filter setting for measurement
//	MesFil( LOOPGAIN ) ;					// Filter setting for measurement
	
	MeasureStart( SlNum , SlMeasureParameterA , SlMeasureParameterB ) ;			// Start measure
	MeasureWait() ;						// Wait complete of measurement

	SetSinWavGenInt();		// Sine wave stop
	
	SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)0x00000000 ) ;	// Set Sine Wave Input RAM
	RamWrite32A( UsSinAdr		,	0x00000000 ) ;				// DelayRam Clear
	
	RamRead32A( StMeasFunc_MFA_LLiAbsInteg1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiAbsInteg1 + 4 	, &StMeasValueA.StUllnVal.UlHigVal ) ;
	RamRead32A( StMeasFunc_MFB_LLiAbsInteg2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiAbsInteg2 + 4		, &StMeasValueB.StUllnVal.UlHigVal ) ;
TRACE("		MFA %08x%08x, MFB %08x%08x\n", StMeasValueA.StUllnVal.UlHigVal , StMeasValueA.StUllnVal.UlLowVal , StMeasValueB.StUllnVal.UlHigVal , StMeasValueB.StUllnVal.UlHigVal ) ;
	
	if( UcDirSel == X_DIR ) {		// X axis
		UllCalculateVal = ( StMeasValueB.UllnValue * 1000 / StMeasValueA.UllnValue ) * ptr->Hxgain / ptr->XGap ;
		if( UllCalculateVal > (UINT_64)0x000000007FFFFFFF )		UllCalculateVal = (UINT_64)0x000000007FFFFFFF ;
		StAdjPar.StLopGan.UlLxgVal = (UINT_32)UllCalculateVal ;
		RamWrite32A( HallFilterCoeffX_hxgain0 , StAdjPar.StLopGan.UlLxgVal ) ;
TRACE("		hxgain0: %08x\n", StAdjPar.StLopGan.UlLxgVal ) ;
		if( (UllCalculateVal > ptr->XJudgeHigh) || ( UllCalculateVal < ptr->XJudgeLow ) ){
			UlReturnState = EXE_LXADJ ;
		}else{
			UlReturnState = EXE_END ;
		}
		
	}else if( UcDirSel == Y_DIR ){							// Y axis
		UllCalculateVal = ( StMeasValueB.UllnValue * 1000 / StMeasValueA.UllnValue ) * ptr->Hygain / ptr->YGap ;
		if( UllCalculateVal > (UINT_64)0x000000007FFFFFFF )		UllCalculateVal = (UINT_64)0x000000007FFFFFFF ;
		StAdjPar.StLopGan.UlLygVal = (UINT_32)UllCalculateVal ;
		RamWrite32A( HallFilterCoeffY_hygain0 , StAdjPar.StLopGan.UlLygVal ) ;
TRACE("		hygain0: %08x\n", StAdjPar.StLopGan.UlLygVal  ) ;
		if( (UllCalculateVal > ptr->YJudgeHigh) || ( UllCalculateVal < ptr->YJudgeLow ) ){
			UlReturnState = EXE_LYADJ ;
		}else{
			UlReturnState = EXE_END ;
		}
	}
#endif	// LOOPGAIN_FIX_VALUE
	return( UlReturnState ) ;

}


//********************************************************************************
// Function Name 	: MesFil
// Retun Value		: NON
// Argment Value	: Measure Filter Mode
// Explanation		: Measure Filter Setting Function
// History			: First edition
//********************************************************************************
void	MesFil( UINT_8	UcMesMod )		// 18.0446kHz/15.027322kHz
{
	UINT_32	UlMeasFilaA , UlMeasFilaB , UlMeasFilaC ;
	UINT_32	UlMeasFilbA , UlMeasFilbB , UlMeasFilbC ;

#if FS_MODE == 0
	if( !UcMesMod ) {								// Hall Bias&Offset Adjust

		UlMeasFilaA	=	0x0341F6F7 ;	// LPF 150Hz
		UlMeasFilaB	=	0x0341F6F7 ;
		UlMeasFilaC	=	0x797C1211 ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	} else if( UcMesMod == LOOPGAIN ) {				// Loop Gain Adjust

		UlMeasFilaA	=	0x12FAFFF9 ;	// LPF1000Hz
		UlMeasFilaB	=	0x12FAFFF9 ;
		UlMeasFilaC	=	0x5A0A000D ;
		UlMeasFilbA	=	0x7F55BD91 ;	// HPF30Hz
		UlMeasFilbB	=	0x80AA426F ;
		UlMeasFilbC	=	0x7EAB7B22 ;

	} else if( UcMesMod == LOOPGAIN2 ) {				// Loop Gain Adjust2

		UlMeasFilaA	=	0x01F98673 ;	// LPF90Hz
		UlMeasFilaB	=	0x01F98673 ;
		UlMeasFilaC	=	0x7C0CF31B ;
		UlMeasFilbA	=	0x7FC70CAF ;	// HPF10Hz
		UlMeasFilbB	=	0x8038F350 ;
		UlMeasFilbC	=	0x7F8E195F ;

	} else if( UcMesMod == THROUGH ) {				// for Through

		UlMeasFilaA	=	0x7FFFFFFF ;	// Through
		UlMeasFilaB	=	0x00000000 ;
		UlMeasFilaC	=	0x00000000 ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	} else if( UcMesMod == NOISE ) {				// SINE WAVE TEST for NOISE

		UlMeasFilaA	=	0x0341F6F7 ;	// LPF150Hz
		UlMeasFilaB	=	0x0341F6F7 ;
		UlMeasFilaC	=	0x797C1211 ;
		UlMeasFilbA	=	0x0341F6F7 ;	// LPF150Hz
		UlMeasFilbB	=	0x0341F6F7 ;
		UlMeasFilbC	=	0x797C1211 ;

	} else if(UcMesMod == OSCCHK) {
		UlMeasFilaA	=	0x065A887F ;	// LPF300Hz
		UlMeasFilaB	=	0x065A887F ;
		UlMeasFilaC	=	0x734AEF01 ;
		UlMeasFilbA	=	0x065A887F ;	// LPF300Hz
		UlMeasFilbB	=	0x065A887F ;
		UlMeasFilbC	=	0x734AEF01 ;

	} else if( UcMesMod == SELFTEST ) {				// GYRO SELF TEST

		UlMeasFilaA	=	0x12FAFFF9 ;	// LPF1000Hz
		UlMeasFilaB	=	0x12FAFFF9 ;
		UlMeasFilaC	=	0x5A0A000D ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	}
#else //FS_MODE
	if( !UcMesMod ) {								// Hall Bias&Offset Adjust

		UlMeasFilaA	=	0x03E4526B ;	// LPF 150Hz
		UlMeasFilaB	=	0x03E4526B ;
		UlMeasFilaC	=	0x78375B2B ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	} else if( UcMesMod == LOOPGAIN ) {				// Loop Gain Adjust

		UlMeasFilaA	=	0x1621ECCD ;	// LPF1000Hz
		UlMeasFilaB	=	0x1621ECCD ;
		UlMeasFilaC	=	0x53BC2664 ;
		UlMeasFilbA	=	0x7F33C48F ;	// HPF30Hz
		UlMeasFilbB	=	0x80CC3B71 ;
		UlMeasFilbC	=	0x7E67891F ;

	} else if( UcMesMod == LOOPGAIN2 ) {				// Loop Gain Adjust2

//		UlMeasFilaA	=	0x025D2733 ;	// LPF90Hz
//		UlMeasFilaB	=	0x025D2733 ;
//		UlMeasFilaC	=	0x7B45B19B ;
//		UlMeasFilbA	=	0x7FBBA379 ;	// HPF10Hz
//		UlMeasFilbB	=	0x80445C87 ;
//		UlMeasFilbC	=	0x7F7746F1 ;
		UlMeasFilaA	=	0x25BD51E6 ;	// LPF2000Hz
		UlMeasFilaB	=	0x25BD51E6 ;
		UlMeasFilaC	=	0x34855C33 ;
		UlMeasFilbA	=	0x7D60FBCD ;	// HPF100Hz
		UlMeasFilbB	=	0x829F0433 ;
		UlMeasFilbC	=	0x7AC1F799 ;

	} else if( UcMesMod == THROUGH ) {				// for Through

		UlMeasFilaA	=	0x7FFFFFFF ;	// Through
		UlMeasFilaB	=	0x00000000 ;
		UlMeasFilaC	=	0x00000000 ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	} else if( UcMesMod == NOISE ) {				// SINE WAVE TEST for NOISE

		UlMeasFilaA	=	0x03E4526B ;	// LPF150Hz
		UlMeasFilaB	=	0x03E4526B ;
		UlMeasFilaC	=	0x78375B2B ;
		UlMeasFilbA	=	0x03E4526B ;	// LPF150Hz
		UlMeasFilbB	=	0x03E4526B ;
		UlMeasFilbC	=	0x78375B2B ;

	} else if(UcMesMod == OSCCHK) {
		UlMeasFilaA	=	0x078DD83D ;	// LPF300Hz
		UlMeasFilaB	=	0x078DD83D ;
		UlMeasFilaC	=	0x70E44F85 ;
		UlMeasFilbA	=	0x078DD83D ;	// LPF300Hz
		UlMeasFilbB	=	0x078DD83D ;
		UlMeasFilbC	=	0x70E44F85 ;

	} else if( UcMesMod == SELFTEST ) {				// GYRO SELF TEST

		UlMeasFilaA	=	0x1621ECCD ;	// LPF1000Hz
		UlMeasFilaB	=	0x1621ECCD ;
		UlMeasFilaC	=	0x53BC2664 ;
		UlMeasFilbA	=	0x7FFFFFFF ;	// Through
		UlMeasFilbB	=	0x00000000 ;
		UlMeasFilbC	=	0x00000000 ;

	}
#endif //FS_MODE

	RamWrite32A ( MeasureFilterA_Coeff_a1	, UlMeasFilaA ) ;
	RamWrite32A ( MeasureFilterA_Coeff_b1	, UlMeasFilaB ) ;
	RamWrite32A ( MeasureFilterA_Coeff_c1	, UlMeasFilaC ) ;

	RamWrite32A ( MeasureFilterA_Coeff_a2	, UlMeasFilbA ) ;
	RamWrite32A ( MeasureFilterA_Coeff_b2	, UlMeasFilbB ) ;
	RamWrite32A ( MeasureFilterA_Coeff_c2	, UlMeasFilbC ) ;

	RamWrite32A ( MeasureFilterB_Coeff_a1	, UlMeasFilaA ) ;
	RamWrite32A ( MeasureFilterB_Coeff_b1	, UlMeasFilaB ) ;
	RamWrite32A ( MeasureFilterB_Coeff_c1	, UlMeasFilaC ) ;

	RamWrite32A ( MeasureFilterB_Coeff_a2	, UlMeasFilbA ) ;
	RamWrite32A ( MeasureFilterB_Coeff_b2	, UlMeasFilbB ) ;
	RamWrite32A ( MeasureFilterB_Coeff_c2	, UlMeasFilbC ) ;
}

//********************************************************************************
// Function Name 	: ClrMesFil
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Clear Measure Filter Function
// History			: First edition
//********************************************************************************
void	ClrMesFil( void )
{
	RamWrite32A ( MeasureFilterA_Delay_z11	, 0 ) ;
	RamWrite32A ( MeasureFilterA_Delay_z12	, 0 ) ;

	RamWrite32A ( MeasureFilterA_Delay_z21	, 0 ) ;
	RamWrite32A ( MeasureFilterA_Delay_z22	, 0 ) ;

	RamWrite32A ( MeasureFilterB_Delay_z11	, 0 ) ;
	RamWrite32A ( MeasureFilterB_Delay_z12	, 0 ) ;

	RamWrite32A ( MeasureFilterB_Delay_z21	, 0 ) ;
	RamWrite32A ( MeasureFilterB_Delay_z22	, 0 ) ;
}

//********************************************************************************
// Function Name 	: MeasureStart
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Measure start setting Function
// History			: First edition
//********************************************************************************
void	MeasureStart( INT_32 SlMeasureParameterNum , INT_32 SlMeasureParameterA , INT_32 SlMeasureParameterB )
{
	MemoryClear( StMeasFunc_SiSampleNum , sizeof( MeasureFunction_Type ) ) ;
	RamWrite32A( StMeasFunc_MFA_SiMax1	 , 0x80000000 ) ;					// Set Min
	RamWrite32A( StMeasFunc_MFB_SiMax2	 , 0x80000000 ) ;					// Set Min
	RamWrite32A( StMeasFunc_MFA_SiMin1	 , 0x7FFFFFFF ) ;					// Set Max
	RamWrite32A( StMeasFunc_MFB_SiMin2	 , 0x7FFFFFFF ) ;					// Set Max

	SetTransDataAdr( StMeasFunc_MFA_PiMeasureRam1	, ( UINT_32 )SlMeasureParameterA ) ;		// Set Measure Filter A Ram Address
	SetTransDataAdr( StMeasFunc_MFB_PiMeasureRam2	, ( UINT_32 )SlMeasureParameterB ) ;		// Set Measure Filter B Ram Address

	RamWrite32A( StMeasFunc_SiSampleNum	 	, 0 ) ;											// Clear Measure Counter

	ClrMesFil() ;																			// Clear Delay Ram
//	SetWaitTime(50) ;
	SetWaitTime(1) ;

	RamWrite32A( StMeasFunc_SiSampleMax		, SlMeasureParameterNum ) ;				// Set Measure Max Number

}


//********************************************************************************
// Function Name 	: MeasureStart2
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Measure start setting Function
// History			: First edition 						
//********************************************************************************
void	MeasureStart2( INT_32 SlMeasureParameterNum , INT_32 SlMeasureParameterA , INT_32 SlMeasureParameterB , UINT_16 UsTime )
{
	MemoryClear( StMeasFunc_SiSampleNum , sizeof( MeasureFunction_Type ) ) ;
	RamWrite32A( StMeasFunc_MFA_SiMax1	 , 0x80000001 ) ;					// Set Min 
	RamWrite32A( StMeasFunc_MFB_SiMax2	 , 0x80000001 ) ;					// Set Min 
	RamWrite32A( StMeasFunc_MFA_SiMin1	 , 0x7FFFFFFF ) ;					// Set Max 
	RamWrite32A( StMeasFunc_MFB_SiMin2	 , 0x7FFFFFFF ) ;					// Set Max 
	
	SetTransDataAdr( StMeasFunc_MFA_PiMeasureRam1	 , ( UINT_32 )SlMeasureParameterA ) ;		// Set Measure Filter A Ram Address
	SetTransDataAdr( StMeasFunc_MFB_PiMeasureRam2	 , ( UINT_32 )SlMeasureParameterB ) ;		// Set Measure Filter B Ram Address

	RamWrite32A( StMeasFunc_SiSampleNum	 	, 0 ) ;									// Clear Measure Counter

	ClrMesFil() ;						// Clear Delay Ram
	SetWaitTime(UsTime) ;

	RamWrite32A( StMeasFunc_SiSampleMax		, SlMeasureParameterNum ) ;				// Set Measure Max Number


}

//********************************************************************************
// Function Name 	: MeasureWait
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Wait complete of Measure Function
// History			: First edition
//********************************************************************************
void	MeasureWait( void )
{

	UINT_32	SlWaitTimerSt ;

	UINT_16	UsTimeOut = 2000;

	WitTim(10);

	do {
		RamRead32A( StMeasFunc_SiSampleMax, &SlWaitTimerSt ) ;
		WitTim(1);
	} while ( SlWaitTimerSt && --UsTimeOut );

}

//********************************************************************************
// Function Name 	: MemoryClear
// Retun Value		: NON
// Argment Value	: Top poINT_32er , Size
// Explanation		: Memory Clear Function
// History			: First edition
//********************************************************************************
void	MemoryClear( UINT_16 UsSourceAddress, UINT_16 UsClearSize )
{
	UINT_16	UsLoopIndex ;

	for ( UsLoopIndex = 0 ; UsLoopIndex < UsClearSize ;  ) {
		RamWrite32A( UsSourceAddress	, 	0x00000000 ) ;				// 4Byte
		UsSourceAddress += 4;
		UsLoopIndex += 4 ;
	}
}

//********************************************************************************
// Function Name 	: SetWaitTime
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Set Timer wait Function
// History			: First edition
//********************************************************************************
#if FS_MODE == 0
#define 	ONE_MSEC_COUNT	18			// 18.0446kHz * 18 ≒ 1ms
#else //FS_MODE
#define 	ONE_MSEC_COUNT	15			// 15.0273kHz * 15 ≒ 1ms
#endif //FS_MODE
void	SetWaitTime( UINT_16 UsWaitTime )
{
	RamWrite32A( WaitTimerData_UiWaitCounter	, 0 ) ;
	RamWrite32A( WaitTimerData_UiTargetCount	, (UINT_32)(ONE_MSEC_COUNT * UsWaitTime)) ;
}


//********************************************************************************
// Function Name 	: TneGvc
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Tunes the Gyro VC offset
// History			: First edition
//********************************************************************************
#define 	GYROF_NUM		2048			// 2048times
#define 	GYROF_UPPER		0x06D6			// 
#define 	GYROF_LOWER		0xF92A			// 
UINT_32	TneGvc(  UINT_8	uc_mode  )
{
	UINT_32	UlRsltSts;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum ;
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureAveValueA , SlMeasureAveValueB ;
	
	
	//平均値測定
	
	MesFil( THROUGH ) ;					// Set Measure Filter

	SlMeasureParameterNum	=	GYROF_NUM ;					// Measurement times
#ifdef	SEL_SHIFT_COR
	if( uc_mode == 0 ){
#endif	//SEL_SHIFT_COR
		SlMeasureParameterA		=	GYRO_RAM_GX_ADIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	GYRO_RAM_GY_ADIDAT ;		// Set Measure RAM Address
#ifdef	SEL_SHIFT_COR
	}else{
		SlMeasureParameterA		=	GYRO_ZRAM_GZ_ADIDAT ;		// Set Measure RAM Address
		SlMeasureParameterB		=	GYRO_ZRAM_GZ_ADIDAT ;		// Set Measure RAM Address
	}
#endif	//SEL_SHIFT_COR
	
	MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
	
	MeasureWait() ;					// Wait complete of measurement
	
TRACE("Read Adr = %04x, %04xh \n",StMeasFunc_MFA_LLiIntegral1 + 4 , StMeasFunc_MFA_LLiIntegral1) ;
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4		, &StMeasValueA.StUllnVal.UlHigVal ) ;
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4		, &StMeasValueB.StUllnVal.UlHigVal ) ;
	
TRACE("GX_OFT = %08x, %08xh \n",(unsigned int)StMeasValueA.StUllnVal.UlHigVal,(unsigned int)StMeasValueA.StUllnVal.UlLowVal) ;
TRACE("GY_OFT = %08x, %08xh \n",(unsigned int)StMeasValueB.StUllnVal.UlHigVal,(unsigned int)StMeasValueB.StUllnVal.UlLowVal) ;
	SlMeasureAveValueA = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
	SlMeasureAveValueB = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;
TRACE("GX_AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueA) ;
TRACE("GY_AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueB) ;
	
	SlMeasureAveValueA = ( SlMeasureAveValueA >> 16 ) & 0x0000FFFF ;
	SlMeasureAveValueB = ( SlMeasureAveValueB >> 16 ) & 0x0000FFFF ;
	
	UlRsltSts = EXE_END ;
#ifdef	SEL_SHIFT_COR
	if( uc_mode == 0){
#endif	//SEL_SHIFT_COR
		StAdjPar.StGvcOff.UsGxoVal = ( UINT_16 )( SlMeasureAveValueA & 0x0000FFFF );		//Measure Result Store
		if(( (INT_16)StAdjPar.StGvcOff.UsGxoVal > (INT_16)GYROF_UPPER ) || ( (INT_16)StAdjPar.StGvcOff.UsGxoVal < (INT_16)GYROF_LOWER )){
			UlRsltSts |= EXE_GXADJ ;
		}
		RamWrite32A( GYRO_RAM_GXOFFZ , (( SlMeasureAveValueA << 16 ) & 0xFFFF0000 ) ) ;		// X axis Gyro offset
		
		StAdjPar.StGvcOff.UsGyoVal = ( UINT_16 )( SlMeasureAveValueB & 0x0000FFFF );		//Measure Result Store
		if(( (INT_16)StAdjPar.StGvcOff.UsGyoVal > (INT_16)GYROF_UPPER ) || ( (INT_16)StAdjPar.StGvcOff.UsGyoVal < (INT_16)GYROF_LOWER )){
			UlRsltSts |= EXE_GYADJ ;
		}
		RamWrite32A( GYRO_RAM_GYOFFZ , (( SlMeasureAveValueB << 16 ) & 0xFFFF0000 ) ) ;		// Y axis Gyro offset
	
TRACE("GX_AVEOFT_RV = %08xh \n",(unsigned int)SlMeasureAveValueA) ;
TRACE("GY_AVEOFT_RV = %08xh \n",(unsigned int)SlMeasureAveValueB) ;
	
		RamWrite32A( GYRO_RAM_GYROX_OFFSET , 0x00000000 ) ;			// X axis Drift Gyro offset
		RamWrite32A( GYRO_RAM_GYROY_OFFSET , 0x00000000 ) ;			// Y axis Drift Gyro offset
		RamWrite32A( GyroFilterDelayX_GXH1Z2 , 0x00000000 ) ;		// X axis H1Z2 Clear
		RamWrite32A( GyroFilterDelayY_GYH1Z2 , 0x00000000 ) ;		// Y axis H1Z2 Clear
#ifdef	SEL_SHIFT_COR
	}else{
		StAdjPar.StGvcOff.UsGzoVal = ( UINT_16 )( SlMeasureAveValueA & 0x0000FFFF );		//Measure Result Store
		if(( (INT_16)StAdjPar.StGvcOff.UsGzoVal > (INT_16)GYROF_UPPER ) || ( (INT_16)StAdjPar.StGvcOff.UsGzoVal < (INT_16)GYROF_LOWER )){
			UlRsltSts |= EXE_GZADJ ;
		}
		RamWrite32A( GYRO_ZRAM_GZOFFZ , (( SlMeasureAveValueA << 16 ) & 0xFFFF0000 ) ) ;		// Z axis Gyro offset

		RamWrite32A( GyroRAM_Z_GYRO_OFFSET , 0x00000000 ) ;			// Z axis Drift Gyro offset
	}
#endif	//SEL_SHIFT_COR
	return( UlRsltSts );
	
		
}


#ifdef	SEL_SHIFT_COR
//********************************************************************************
// Function Name 	: TneAvc
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Tunes the Accel VC offset for All
// History			: First edition
//********************************************************************************
#define 	ACCLOF_NUM		4096				// 4096times
UINT_32	TneAvc( UINT_8 ucposture )
{
	UINT_32			UlRsltSts;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum ;
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureAveValueA , SlMeasureAveValueB ;
	INT_32			SlMeasureRetValueX , SlMeasureRetValueY , SlMeasureRetValueZ;
	UINT_8			i , j , k;
	INT_32			SlDiff[3] ;

	UlRsltSts = EXE_END ;
	if( ucposture < 0x7f ){
		//平均値測定
		for( i=0 ; i<2 ; i++ )
		{
			MesFil( THROUGH ) ;					// Set Measure Filter

			SlMeasureParameterNum	=	ACCLOF_NUM ;					// Measurement times
			switch(i){
			case 0:
				SlMeasureParameterA		=	ACCLRAM_X_AC_ADIDAT ;			// Set Measure RAM Address
				SlMeasureParameterB		=	ACCLRAM_Y_AC_ADIDAT ;			// Set Measure RAM Address
				break;
			case 1:
				SlMeasureParameterA		=	ACCLRAM_Z_AC_ADIDAT ;			// Set Measure RAM Address
				SlMeasureParameterB		=	ACCLRAM_Z_AC_ADIDAT ;			// Set Measure RAM Address
				break;
			}

			MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure

			MeasureWait() ;					// Wait complete of measurement

			RamRead32A( StMeasFunc_MFA_LLiIntegral1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;
			RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4		, &StMeasValueA.StUllnVal.UlHigVal ) ;
			RamRead32A( StMeasFunc_MFB_LLiIntegral2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;
			RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4		, &StMeasValueB.StUllnVal.UlHigVal ) ;

			SlMeasureAveValueA = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
			SlMeasureAveValueB = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;

			switch(i){
			case 0:
				SlMeasureRetValueX = SlMeasureAveValueA ;
				SlMeasureRetValueY = SlMeasureAveValueB ;
				break;
			case 1:
				SlMeasureRetValueZ = SlMeasureAveValueA ;
				break;
			}

		}


TRACE("VAL(X,Y,Z) pos = \t%08xh\t%08xh\t%08xh\t%d \n",(INT_32)SlMeasureRetValueX ,(INT_32)SlMeasureRetValueY,(INT_32)SlMeasureRetValueZ, ucposture ) ;
		if(( SlMeasureRetValueZ < (INT_32)(POSTURETH_P<<16)) && (ucposture == 0x10)){
				UlRsltSts = EXE_ERR ;
TRACE(" POS14 [ERROR] \t%08xh < %08xh\n", (unsigned int)(SlMeasureRetValueZ), (unsigned int)(POSTURETH_P<<16) ) ;
		}else{
TRACE("DEBUG = \t%08xh\t \n", abs( (INT_32)(ACCL_SENS << 16) - abs(SlMeasureRetValueZ) ) ) ;
			if( abs(SlMeasureRetValueX) > ZEROG_MRGN_XY )									UlRsltSts |= EXE_AXADJ ;
			if( abs(SlMeasureRetValueY) > ZEROG_MRGN_XY )									UlRsltSts |= EXE_AYADJ ;
			if( abs( (INT_32)(ACCL_SENS << 16) - abs(SlMeasureRetValueZ)) > ZEROG_MRGN_Z )	UlRsltSts |= EXE_AZADJ ;
			if( UlRsltSts == EXE_END ){
				StPosOff.UlAclOfSt |= 0x0000003F;
TRACE("POS14(X,Y,Z) st = \t%08xh\t%08xh\t%08xh\t%08xh \n", (unsigned int)StPosOff.StPos.Pos[4][0], (unsigned int)StPosOff.StPos.Pos[4][1], (unsigned int)StPosOff.StPos.Pos[4][2], (unsigned int)StPosOff.UlAclOfSt ) ;

				SlDiff[0] = SlMeasureRetValueX - (INT_32)0;
				SlDiff[1] = SlMeasureRetValueY - (INT_32)0;
//				if(ucposture == 0x10){
					SlDiff[2] = SlMeasureRetValueZ - (INT_32)(ACCL_SENS << 16);
//				}else{
//					SlDiff[2] = SlMeasureRetValueZ - (INT_32)(-ACCL_SENS << 16);
//				}
				StPosOff.StPos.Pos[4][0] = SlDiff[0];
				StPosOff.StPos.Pos[4][1] = SlDiff[1];
				StPosOff.StPos.Pos[4][2] = SlDiff[2];
			}
		}
	}else{
		switch(ucposture){
		case 0x80:	/* 計算 */

			if(StPosOff.UlAclOfSt == 0x3fL ){
				/*X offset*/
				StAclVal.StAccel.SlOffsetX = StPosOff.StPos.Pos[4][0] ;
				/*Y offset*/
				StAclVal.StAccel.SlOffsetY = StPosOff.StPos.Pos[4][1] ;
				/*Z offset*/
				StAclVal.StAccel.SlOffsetZ = StPosOff.StPos.Pos[4][2] ;
#ifdef DEBUG
TRACE("ACLOFST(X,Y,Z) = \t%08xh\t%08xh\t%08xh \n",(INT_32)StAclVal.StAccel.SlOffsetX ,(INT_32)StAclVal.StAccel.SlOffsetY,(INT_32)StAclVal.StAccel.SlOffsetZ   ) ;
#endif //DEBUG
				RamWrite32A( ACCLRAM_X_AC_OFFSET , StAclVal.StAccel.SlOffsetX ) ;	// X axis Accel offset
				RamWrite32A( ACCLRAM_Y_AC_OFFSET , StAclVal.StAccel.SlOffsetY ) ;	// Y axis Accel offset
				RamWrite32A( ACCLRAM_Z_AC_OFFSET , StAclVal.StAccel.SlOffsetZ ) ;	// Z axis Accel offset
				
				StAclVal.StAccel.SlOffsetX = ( StAclVal.StAccel.SlOffsetX >> 16 ) & 0x0000FFFF;
				StAclVal.StAccel.SlOffsetY = ( StAclVal.StAccel.SlOffsetY >> 16 ) & 0x0000FFFF;
				StAclVal.StAccel.SlOffsetZ = ( StAclVal.StAccel.SlOffsetZ >> 16 ) & 0x0000FFFF;
				
				for( j=0 ; j < 6 ; j++ ){
					k = 4 * j;
					RamWrite32A( AcclFilDly_X + k , 0x00000000 ) ;			// X axis Accl LPF Clear
					RamWrite32A( AcclFilDly_Y + k , 0x00000000 ) ;			// Y axis Accl LPF Clear
					RamWrite32A( AcclFilDly_Z + k , 0x00000000 ) ;			// Z axis Accl LPF Clear
				}

			}else{
				UlRsltSts = EXE_ERR ;
			}
			break;
			
		case 0xFF:	/* RAM clear */
			MemClr( ( UINT_8 * )&StPosOff, sizeof( stPosOff ) ) ;	// Adjust Parameter Clear
			MemClr( ( UINT_8 * )&StAclVal, sizeof( stAclVal ) ) ;	// Adjust Parameter Clear
//			StPosOff.UlAclOfSt = 0L;
			break;
		}
	}

TRACE(" Result = %08x\n",(INT_32)UlRsltSts ) ;
	return( UlRsltSts );


}


void MeasAddressSelection( UINT_8 mode , INT_32 * measadr_a , INT_32 * measadr_b )
{
	if( mode == 0 ){
		*measadr_a		=	GYRO_RAM_GX_ADIDAT ;		// Set Measure RAM Address
		*measadr_b		=	GYRO_RAM_GY_ADIDAT ;		// Set Measure RAM Address
	}else if( mode == 1 ){
		*measadr_a		=	GYRO_ZRAM_GZ_ADIDAT ;		// Set Measure RAM Address
		*measadr_b		=	ACCLRAM_Z_AC_ADIDAT ;		// Set Measure RAM Address
	}else{
		*measadr_a		=	ACCLRAM_X_AC_ADIDAT ;			// Set Measure RAM Address
		*measadr_b		=	ACCLRAM_Y_AC_ADIDAT ;			// Set Measure RAM Address
	}
}

#define 	MESOF_NUM		2048			// 2048times
#define 	GYROFFSET_H		( 0x06D6 << 16 )			// 
#define		GSENS		( 4096 << 16 )				// LSB/g
#define		GSENS_MARG		(GSENS / 4)			// 1/4g
#define		POSTURETH		(GSENS - GSENS_MARG)	// LSB/g
#define		ZG_MRGN		(409 << 16)					// G tolerance  100mGとする
UINT_32	MeasGyAcOffset(  void  )
{
	UINT_32	UlRsltSts;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum ;
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureAveValueA[3] , SlMeasureAveValueB[3] ;
	UINT_8			i ;

	
	//平均値測定
	
	MesFil( THROUGH ) ;					// Set Measure Filter

	SlMeasureParameterNum	=	MESOF_NUM ;					// Measurement times
	
	for( i=0 ; i<3 ; i++ )
	{
		MeasAddressSelection( i, &SlMeasureParameterA , &SlMeasureParameterB );
	
		MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure
		
		MeasureWait() ;					// Wait complete of measurement
	
TRACE("Read Adr = %04x, %04xh \n",StMeasFunc_MFA_LLiIntegral1 + 4 , StMeasFunc_MFA_LLiIntegral1) ;
		RamRead32A( StMeasFunc_MFA_LLiIntegral1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
		RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4		, &StMeasValueA.StUllnVal.UlHigVal ) ;
		RamRead32A( StMeasFunc_MFB_LLiIntegral2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
		RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4		, &StMeasValueB.StUllnVal.UlHigVal ) ;
	
TRACE("(%d) AOFT = %08x, %08xh \n",i,(unsigned int)StMeasValueA.StUllnVal.UlHigVal,(unsigned int)StMeasValueA.StUllnVal.UlLowVal) ;
TRACE("(%d) BOFT = %08x, %08xh \n",i,(unsigned int)StMeasValueB.StUllnVal.UlHigVal,(unsigned int)StMeasValueB.StUllnVal.UlLowVal) ;
		SlMeasureAveValueA[i] = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
		SlMeasureAveValueB[i] = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;
TRACE("AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueA[i]) ;
TRACE("AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueB[i]) ;
	
	}
	
	UlRsltSts = EXE_END ;
	
	
	if( abs(SlMeasureAveValueA[0]) > GYROFFSET_H )					UlRsltSts |= EXE_GXADJ ;
	if( abs(SlMeasureAveValueB[0]) > GYROFFSET_H ) 					UlRsltSts |= EXE_GYADJ ;
	if( abs(SlMeasureAveValueA[1]) > GYROFFSET_H ) 					UlRsltSts |= EXE_GZADJ ;
	if(    (SlMeasureAveValueB[1]) < POSTURETH )					UlRsltSts |= EXE_AZADJ ;
	if( abs(SlMeasureAveValueA[2]) > ZG_MRGN )						UlRsltSts |= EXE_AXADJ ;
	if( abs(SlMeasureAveValueB[2]) > ZG_MRGN )						UlRsltSts |= EXE_AYADJ ;
	if( abs( GSENS  - SlMeasureAveValueB[1]) > ZG_MRGN )			UlRsltSts |= EXE_AZADJ ;


	if( UlRsltSts == EXE_END ){
		RamWrite32A( GYRO_RAM_GXOFFZ ,		SlMeasureAveValueA[0] ) ;							// X axis Gyro offset
		RamWrite32A( GYRO_RAM_GYOFFZ ,		SlMeasureAveValueB[0] ) ;							// Y axis Gyro offset
		RamWrite32A( GYRO_ZRAM_GZOFFZ ,		SlMeasureAveValueA[1] ) ;							// Z axis Gyro offset
		RamWrite32A( ACCLRAM_X_AC_OFFSET ,	SlMeasureAveValueA[2] ) ;							// X axis Accel offset
		RamWrite32A( ACCLRAM_Y_AC_OFFSET ,	SlMeasureAveValueB[2] ) ;							// Y axis Accel offset
		RamWrite32A( ACCLRAM_Z_AC_OFFSET , 	SlMeasureAveValueB[1] - (INT_32)GSENS ) ;		// Z axis Accel offset

		RamWrite32A( GYRO_RAM_GYROX_OFFSET , 0x00000000 ) ;			// X axis Drift Gyro offset
		RamWrite32A( GYRO_RAM_GYROY_OFFSET , 0x00000000 ) ;			// Y axis Drift Gyro offset
		RamWrite32A( GyroRAM_Z_GYRO_OFFSET , 0x00000000 ) ;			// Z axis Drift Gyro offset
		RamWrite32A( GyroFilterDelayX_GXH1Z2 , 0x00000000 ) ;		// X axis H1Z2 Clear
		RamWrite32A( GyroFilterDelayY_GYH1Z2 , 0x00000000 ) ;		// Y axis H1Z2 Clear
		RamWrite32A( AcclFilDly_X + 8 , 0x00000000 ) ;			// X axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Y + 8 , 0x00000000 ) ;			// Y axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Z + 8 , 0x00000000 ) ;			// Z axis Accl LPF Clear
		RamWrite32A( AcclFilDly_X + 12 , 0x00000000 ) ;			// X axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Y + 12 , 0x00000000 ) ;			// Y axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Z + 12 , 0x00000000 ) ;			// Z axis Accl LPF Clear
		RamWrite32A( AcclFilDly_X + 16 , 0x00000000 ) ;			// X axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Y + 16 , 0x00000000 ) ;			// Y axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Z + 16 , 0x00000000 ) ;			// Z axis Accl LPF Clear
		RamWrite32A( AcclFilDly_X + 20 , 0x00000000 ) ;			// X axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Y + 20 , 0x00000000 ) ;			// Y axis Accl LPF Clear
		RamWrite32A( AcclFilDly_Z + 20 , 0x00000000 ) ;			// Z axis Accl LPF Clear
	}
	return( UlRsltSts );
	
		
}
#endif	//SEL_SHIFT_COR

//********************************************************************************
// Function Name 	: RtnCen
// Retun Value		: Command Status
// Argment Value	: Command Parameter
// Explanation		: Return to center Command Function
// History			: First edition
//********************************************************************************
UINT_8	RtnCen( UINT_8	UcCmdPar )
{
	UINT_8	UcSndDat = FAILURE;

	if( !UcCmdPar ){								// X,Y centering
		RamWrite32A( CMD_RETURN_TO_CENTER , BOTH_SRV_ON ) ;
	}else if( UcCmdPar == XONLY_ON ){				// only X centering
		RamWrite32A( CMD_RETURN_TO_CENTER , XAXS_SRV_ON ) ;
	}else if( UcCmdPar == YONLY_ON ){				// only Y centering
		RamWrite32A( CMD_RETURN_TO_CENTER , YAXS_SRV_ON ) ;
	}else{											// Both off
		RamWrite32A( CMD_RETURN_TO_CENTER , BOTH_SRV_OFF ) ;
	}

	do {
		UcSndDat = RdStatus(1);
	} while( UcSndDat == FAILURE );

TRACE("RtnCen() = %02x\n", UcSndDat ) ;
	return( UcSndDat );
}



//********************************************************************************
// Function Name 	: OisEna
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS Enable Control Function
// History			: First edition
//********************************************************************************
void	OisEna( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_OIS_ENABLE , OIS_ENABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisEna( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: OisEnaNCL
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS Enable Control Function w/o delay clear
// History			: First edition
//********************************************************************************
void	OisEnaNCL( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_OIS_ENABLE , OIS_ENA_NCL | OIS_ENABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisEnaNCL( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: OisEnaDrCl
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS Enable Control Function force drift cancel
// History			: First edition
//********************************************************************************
void	OisEnaDrCl( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_OIS_ENABLE , OIS_ENA_DOF | OIS_ENABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisEnaDrCl( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: OisEnaDrNcl
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS Enable Control Function w/o delay clear and force drift cancel
// History			: First edition
//********************************************************************************
void	OisEnaDrNcl( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_OIS_ENABLE , OIS_ENA_DOF | OIS_ENA_NCL | OIS_ENABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisEnaDrCl( Status) = %02x\n", UcStRd ) ;
}
//********************************************************************************
// Function Name 	: OisDis
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS Disable Control Function
// History			: First edition
//********************************************************************************
void	OisDis( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_OIS_ENABLE , OIS_DISABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisDis( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: OisPause
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: OIS pause Control Function
// History			: First edition 						
//********************************************************************************
void	OisPause( void )
{
	UINT_8	UcStRd = 1;
	
	RamWrite32A( CMD_OIS_ENABLE , OIS_DIS_PUS ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" OisPause( Status , cnt ) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: SscEna
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Ssc Enable Control Function
// History			: First edition
//********************************************************************************
void	SscEna( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_SSC_ENABLE , SSC_ENABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" SscEna( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: SscDis
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Ssc Disable Control Function
// History			: First edition
//********************************************************************************
void	SscDis( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_SSC_ENABLE , SSC_DISABLE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" SscDis( Status) = %02x\n", UcStRd ) ;
}


//********************************************************************************
// Function Name 	: SetRec
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Rec Mode Enable Function
// History			: First edition
//********************************************************************************
void	SetRec( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_MOVE_STILL_MODE ,	MOVIE_MODE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" SetRec( Status) = %02x\n", UcStRd ) ;
}


//********************************************************************************
// Function Name 	: SetStill
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Set Still Mode Enable Function
// History			: First edition
//********************************************************************************
void	SetStill( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_MOVE_STILL_MODE ,	STILL_MODE ) ;
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" SetRec( Status) = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: SetRecPreview
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Rec Preview Mode Enable Function
// History			: First edition
//********************************************************************************
void	SetRecPreview( UINT_8 mode )
{
	UINT_8	UcStRd = 1;

	switch( mode ){
	case 0:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	MOVIE_MODE ) ;
		break;
	case 1:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	MOVIE_MODE1 ) ;
		break;
	case 2:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	MOVIE_MODE2 ) ;
		break;
	case 3:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	MOVIE_MODE3 ) ;
		break;
	}
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
//TRACE(" SetRec( %02x ) = %02x\n", mode , UcStRd ) ;
}


//********************************************************************************
// Function Name 	: SetStillPreview
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Set Still Preview Mode Enable Function
// History			: First edition
//********************************************************************************
void	SetStillPreview( UINT_8 mode )
{
	UINT_8	UcStRd = 1;

	switch( mode ){
	case 0:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	STILL_MODE ) ;
		break;
	case 1:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	STILL_MODE1 ) ;
		break;
	case 2:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	STILL_MODE2 ) ;
		break;
	case 3:
		RamWrite32A( CMD_MOVE_STILL_MODE ,	STILL_MODE3 ) ;
		break;
	}
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
//TRACE(" SetRec( %02x ) = %02x\n", mode , UcStRd ) ;
}


//********************************************************************************
// Function Name 	: SetStandbyMode
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Set Standby mode Function
// History			: First edition
//********************************************************************************
void	SetStandbyMode( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_STANDBY_ENABLE ,	STANDBY_MODE ) ;	// Standby
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
//TRACE(" SetStandbyMode = %02x\n", UcStRd ) ;
}
//********************************************************************************
// Function Name 	: SetActiveMode
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Set Active mode Function
// History			: First edition
//********************************************************************************
void	SetActiveMode( void )
{
	UINT_8	UcStRd = 1;

	RamWrite32A( CMD_IO_ADR_ACCESS, ADDA_FSCTRL ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 0x00000090 ) ;
	RamWrite32A( CMD_STANDBY_ENABLE ,	ACTIVE_MODE ) ;	// Active
	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
//TRACE(" SetActiveMode = %02x\n", UcStRd ) ;
}

//********************************************************************************
// Function Name 	: SetSinWavePara
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Sine wave Test Function
// History			: First edition
//********************************************************************************
	/********* Parameter Setting *********/
	/* Servo Sampling Clock		=	20.0195kHz						*/
	/* Freq						=	SinFreq*80000000h/Fs			*/
	/* 05 00 XX MM 				XX:Freq MM:Sin or Circle */
#if FS_MODE == 0
const UINT_32	CucFreqVal[ 17 ]	= {
		0xFFFFFFFF,				//  0:  Stop
		0x0001D0E1,				//  1: 1Hz
		0x0003A1C3,				//  2: 2Hz
		0x000572A5,				//  3: 3Hz
		0x00074387,				//  4: 4Hz
		0x00091468,				//  5: 5Hz
		0x000AE54A,				//  6: 6Hz
		0x000CB62C,				//  7: 7Hz
		0x000E870E,				//  8: 8Hz
		0x001057EF,				//  9: 9Hz
		0x001228D1,				//  A: 10Hz
		0x0013F9B3,				//  B: 11Hz
		0x0015CA95,				//  C: 12Hz
		0x00179B76,				//  D: 13Hz
		0x00196C58,				//  E: 14Hz
		0x001B3D3A,				//  F: 15Hz
		0x001D0E1C				// 10: 16Hz
	} ;
#else //FS_MODE
const UINT_32	CucFreqVal[ 17 ]	= {
		0xFFFFFFFF,				//  0:  Stop
		0x00022E39,				//  1: 1Hz
		0x00045C72,				//  2: 2Hz
		0x00068AAB,				//  3: 3Hz
		0x0008B8E5,				//  4: 4Hz
		0x000AE71E,				//  5: 5Hz
		0x000D1557,				//  6: 6Hz
		0x000F4390,				//  7: 7Hz
		0x001171CA,				//  8: 8Hz
		0x0013A003,				//  9: 9Hz
		0x0015CE3C,				//  A: 10Hz
		0x0017FC76,				//  B: 11Hz
		0x001A2AAF,				//  C: 12Hz
		0x001C58E8,				//  D: 13Hz
		0x001E8721,				//  E: 14Hz
		0x0020B55B,				//  F: 15Hz
		0x0022E394				// 10: 16Hz
	} ;
#endif //FS_MODE

// 	RamWrite32A( SinWave.Gain , 0x00000000 ) ;			// Gainはそれぞれ設定すること
// 	RamWrite32A( CosWave.Gain , 0x00000000 ) ;			// Gainはそれぞれ設定すること
void	SetSinWavePara( UINT_8 UcTableVal ,  UINT_8 UcMethodVal )
{
	UINT_32	UlFreqDat ;


	if(UcTableVal > 0x10 )
		UcTableVal = 0x10 ;			/* Limit */
	UlFreqDat = CucFreqVal[ UcTableVal ] ;

	if( UcMethodVal == CIRCWAVE) {
		RamWrite32A( SinWave_Phase	,	0x00000000 ) ;		// 正弦波の位相量
		RamWrite32A( CosWave_Phase 	,	0x20000000 );		// 正弦波の位相量
	}else{
		RamWrite32A( SinWave_Phase	,	0x00000000 ) ;		// 正弦波の位相量
		RamWrite32A( CosWave_Phase 	,	0x00000000 );		// 正弦波の位相量
	}


	if( UlFreqDat == 0xFFFFFFFF )			/* Sine波中止 */
	{
		RamWrite32A( SinWave_Offset		,	0x00000000 ) ;									// 発生周波数のオフセットを設定
		RamWrite32A( SinWave_Phase		,	0x00000000 ) ;									// 正弦波の位相量
//		RamWrite32A( SinWave_Gain		,	0x00000000 ) ;									// 発生周波数のアッテネータ(初期値は0[dB])
//		SetTransDataAdr( SinWave_OutAddr	,	 (UINT_32)SinWave_Output );			// 出力先アドレス

		RamWrite32A( CosWave_Offset		,	0x00000000 );									// 発生周波数のオフセットを設定
		RamWrite32A( CosWave_Phase 		,	0x00000000 );									// 正弦波の位相量
//		RamWrite32A( CosWave_Gain 		,	0x00000000 );									// 発生周波数のアッテネータ(初期値はCut)
//		SetTransDataAdr( CosWave_OutAddr	,	 (UINT_32)CosWave_Output );			// 出力先アドレス

		RamWrite32A( SinWaveC_Regsiter	,	0x00000000 ) ;									// Sine Wave Stop
		SetTransDataAdr( SinWave_OutAddr	,	0x00000000 ) ;		// 出力先アドレス
		SetTransDataAdr( CosWave_OutAddr	,	0x00000000 );		// 出力先アドレス
		RamWrite32A( HALL_RAM_HXOFF1		,	0x00000000 ) ;				// DelayRam Clear
		RamWrite32A( HALL_RAM_HYOFF1		,	0x00000000 ) ;				// DelayRam Clear
	}
	else
	{
		RamWrite32A( SinWave_Offset		,	UlFreqDat ) ;									// 発生周波数のオフセットを設定
		RamWrite32A( CosWave_Offset		,	UlFreqDat );									// 発生周波数のオフセットを設定

		RamWrite32A( SinWaveC_Regsiter	,	0x00000001 ) ;									// Sine Wave Start
		SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)HALL_RAM_HXOFF1 ) ;		// 出力先アドレス
		SetTransDataAdr( CosWave_OutAddr	,	 (UINT_32)HALL_RAM_HYOFF1 );		// 出力先アドレス

	}


}




//********************************************************************************
// Function Name 	: SetPanTiltMode
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Pan-Tilt Enable/Disable
// History			: First edition
//********************************************************************************
void	SetPanTiltMode( UINT_8 UcPnTmod )
{
	UINT_8	UcStRd = 1;

	switch ( UcPnTmod ) {
		case OFF :
			RamWrite32A( CMD_PAN_TILT ,	PAN_TILT_OFF ) ;
			break ;
		case ON :
			RamWrite32A( CMD_PAN_TILT ,	PAN_TILT_ON ) ;
			break ;
	}

	while( UcStRd ) {
		UcStRd = RdStatus(1);
	}
TRACE(" PanTilt( Status) = %02x , %02x \n", UcStRd , UcPnTmod ) ;
}

 #ifdef	NEUTRAL_CENTER
//********************************************************************************
// Function Name 	: TneHvc
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Tunes the Hall VC offset
// History			: First edition
//********************************************************************************
UINT_8	TneHvc( void )
{
	UINT_8	UcRsltSts;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum ;
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureAveValueA , SlMeasureAveValueB ;

	RtnCen( BOTH_OFF ) ;		// Both OFF

	WitTim( 500 ) ;

	//平均値測定

	MesFil( THROUGH ) ;					// Set Measure Filter

	SlMeasureParameterNum	=	64 ;		// 64times
	SlMeasureParameterA		=	(UINT_32)HALL_RAM_HXIDAT ;		// Set Measure RAM Address
	SlMeasureParameterB		=	(UINT_32)HALL_RAM_HYIDAT ;		// Set Measure RAM Address

	MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;					// Start measure

	ClrMesFil();					// Clear Delay RAM
	SetWaitTime(50) ;

	MeasureWait() ;					// Wait complete of measurement

	RamRead32A( StMeasFunc_MFA_LLiIntegral1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4 	, &StMeasValueA.StUllnVal.UlHigVal ) ;
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;

	SlMeasureAveValueA = (INT_32)((( (INT_64)StMeasValueA.UllnValue * 100 ) / SlMeasureParameterNum ) / 100 ) ;
	SlMeasureAveValueB = (INT_32)((( (INT_64)StMeasValueB.UllnValue * 100 ) / SlMeasureParameterNum ) / 100 ) ;

	StAdjPar.StHalAdj.UsHlxCna = ( UINT_16 )(( SlMeasureAveValueA >> 16 ) & 0x0000FFFF );		//Measure Result Store
	StAdjPar.StHalAdj.UsHlxCen = StAdjPar.StHalAdj.UsHlxCna;											//Measure Result Store

	StAdjPar.StHalAdj.UsHlyCna = ( UINT_16 )(( SlMeasureAveValueB >> 16 ) & 0x0000FFFF );		//Measure Result Store
	StAdjPar.StHalAdj.UsHlyCen = StAdjPar.StHalAdj.UsHlyCna;											//Measure Result Store

	RamWrite32A( HALL_RAM_HXOFF,  (UINT_32)((StAdjPar.StHalAdj.UsHlxCen << 16 ) & 0xFFFF0000 )) ;
	RamWrite32A( HALL_RAM_HYOFF,  (UINT_32)((StAdjPar.StHalAdj.UsHlyCen << 16 ) & 0xFFFF0000 )) ;

	UcRsltSts = EXE_END ;				// Clear Status

	return( UcRsltSts );
}
 #endif	//NEUTRAL_CENTER

 #ifdef	NEUTRAL_CENTER_FINE
//********************************************************************************
// Function Name 	: TneFin
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Tunes the Hall VC offset current optimize
// History			: First edition
//********************************************************************************
#define		ADOFF_FINE_NUM		2000	
void	TneFin( ADJ_LOPGAN* ptr )
{
	UINT_32		UlReadVal ;
	UINT_16		UsAdxOff, UsAdyOff ;
	INT_32		SlMeasureAveValueA , SlMeasureAveValueB ;
	UnllnVal	StMeasValueA , StMeasValueB ;
	UINT_32		UlMinimumValueA, UlMinimumValueB ;
	UINT_16		UsAdxMin, UsAdyMin ;
	UINT_8		UcFin ;
	
	// Loop gain set for servo
	RamWrite32A( HallFilterCoeffX_hxgain0 , ptr->Hxgain ) ;
	RamWrite32A( HallFilterCoeffY_hygain0 , ptr->Hygain ) ;
	
	// Get natural center offset
	RamRead32A( HALL_RAM_HXOFF,  &UlReadVal ) ;
	UsAdxOff = UsAdxMin = (UINT_16)( UlReadVal >> 16 ) ;

	RamRead32A( HALL_RAM_HYOFF,  &UlReadVal ) ;
	UsAdyOff = UsAdyMin = (UINT_16)( UlReadVal >> 16 ) ;
//TRACE("*****************************************************\n" );
//TRACE("TneFin: Before Adx=%04X, Ady=%04X\n", UsAdxOff, UsAdyOff );

	// Servo ON
	RtnCen( BOTH_ON ) ;
	WitTim( TNE ) ;

	MesFil( THROUGH ) ;					// Filter setting for measurement
	MeasureStart( ADOFF_FINE_NUM , HALL_RAM_HALL_X_OUT , HALL_RAM_HALL_Y_OUT ) ;					// Start measure
	MeasureWait() ;						// Wait complete of measurement

	RamRead32A( StMeasFunc_MFA_LLiIntegral1 	, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4 , &StMeasValueA.StUllnVal.UlHigVal ) ;
	SlMeasureAveValueA = (INT_32)((( (INT_64)StMeasValueA.UllnValue * 100 ) / ADOFF_FINE_NUM ) / 100 ) ;

	RamRead32A( StMeasFunc_MFB_LLiIntegral2 	, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;
	SlMeasureAveValueB = (INT_32)((( (INT_64)StMeasValueB.UllnValue * 100 ) / ADOFF_FINE_NUM ) / 100 ) ;

	UlMinimumValueA = abs(SlMeasureAveValueA) ;
	UlMinimumValueB = abs(SlMeasureAveValueB) ;
	UcFin = 0x11 ;

	while( UcFin ) {
		if( UcFin & 0x01 ) {
			if( UlMinimumValueA >= abs(SlMeasureAveValueA) ) {
				UlMinimumValueA = abs(SlMeasureAveValueA) ;
				UsAdxMin = UsAdxOff ;
				// 収束を早めるために、出力値に比例させる
				if( SlMeasureAveValueA > 0 )
					UsAdxOff = (INT_16)UsAdxOff + (SlMeasureAveValueA >> 17) + 1 ;
				else
					UsAdxOff = (INT_16)UsAdxOff + (SlMeasureAveValueA >> 17) - 1 ;

				RamWrite32A( HALL_RAM_HXOFF,  (UINT_32)((UsAdxOff << 16 ) & 0xFFFF0000 )) ;
			} else {
//TRACE("X fine\n");
				UcFin &= 0xFE ;
			}
		}

		if( UcFin & 0x10 ) {
			if( UlMinimumValueB >= abs(SlMeasureAveValueB) ) {
				UlMinimumValueB = abs(SlMeasureAveValueB) ;
				UsAdyMin = UsAdyOff ;
				// 収束を早めるために、出力値に比例させる
				if( SlMeasureAveValueB > 0 )
					UsAdyOff = (INT_16)UsAdyOff + (SlMeasureAveValueB >> 17) + 1 ;
				else
					UsAdyOff = (INT_16)UsAdyOff + (SlMeasureAveValueB >> 17) - 1 ;

				RamWrite32A( HALL_RAM_HYOFF,  (UINT_32)((UsAdyOff << 16 ) & 0xFFFF0000 )) ;
			} else {
//TRACE("Y fine\n");
				UcFin &= 0xEF ;
			}
		}
		
		MeasureStart( ADOFF_FINE_NUM , HALL_RAM_HALL_X_OUT , HALL_RAM_HALL_Y_OUT ) ;					// Start measure
		MeasureWait() ;						// Wait complete of measurement

		RamRead32A( StMeasFunc_MFA_LLiIntegral1 	, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
		RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4 , &StMeasValueA.StUllnVal.UlHigVal ) ;
		SlMeasureAveValueA = (INT_32)((( (INT_64)StMeasValueA.UllnValue * 100 ) / ADOFF_FINE_NUM ) / 100 ) ;

		RamRead32A( StMeasFunc_MFB_LLiIntegral2 	, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
		RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;
		SlMeasureAveValueB = (INT_32)((( (INT_64)StMeasValueB.UllnValue * 100 ) / ADOFF_FINE_NUM ) / 100 ) ;
//TRACE("-->Adx %04X, Ady %04X\n", UsAdxOff, UsAdyOff );
	}	// while
//TRACE("TneFin: After Adx=%04X, Ady=%04X\n", UsAdxMin, UsAdyMin );
	StAdjPar.StHalAdj.UsHlxCna = UsAdxMin;								//Measure Result Store
	StAdjPar.StHalAdj.UsHlxCen = StAdjPar.StHalAdj.UsHlxCna;			//Measure Result Store

	StAdjPar.StHalAdj.UsHlyCna = UsAdyMin;								//Measure Result Store
	StAdjPar.StHalAdj.UsHlyCen = StAdjPar.StHalAdj.UsHlyCna;			//Measure Result Store

	StAdjPar.StHalAdj.UsAdxOff = StAdjPar.StHalAdj.UsHlxCna  ;
	StAdjPar.StHalAdj.UsAdyOff = StAdjPar.StHalAdj.UsHlyCna  ;

	// Servo OFF
	RtnCen( BOTH_OFF ) ;		// Both OFF


TRACE("    XadofFin = %04xh \n", StAdjPar.StHalAdj.UsAdxOff ) ;
TRACE("    YadofFin = %04xh \n", StAdjPar.StHalAdj.UsAdyOff ) ;
	RamWrite32A( HALL_RAM_HXOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdxOff << 16 ) & 0xFFFF0000 )) ;
	RamWrite32A( HALL_RAM_HYOFF,  (UINT_32)((StAdjPar.StHalAdj.UsAdyOff << 16 ) & 0xFFFF0000 )) ;

}
 #endif	//NEUTRAL_CENTER_FINE


//********************************************************************************
// Function Name 	: TneSltPos
// Retun Value		: NON
// Argment Value	: Position number(1, 2, 3, 4, 5, 6, 7, 0:reset)
// Explanation		: Move measurement position function
//********************************************************************************
void	TneSltPos( UINT_8 UcPos )
{
	INT_16 SsOff = 0x0000 ;
	INT_32	SlX, SlY;
	DSPVER Info;
	LINCRS TEMP = {0, 0, 0, 0, 0};		//20190718 Komori
	LINCRS* LnCsPtr = (LINCRS*)&TEMP;	//20190718 Komori

	GetInfomationAfterStartUp( &Info );

	UcPos &= 0x07 ;

	if ( UcPos ) {
		SsOff = LnCsPtr->STEPX * (UcPos - 4);
//		SsOff = (INT_16)(SsOff / sqrt(2));		// for circle limit(root2)
	}

	SsNvcX = LnCsPtr->DRIVEX;
	SsNvcY = LnCsPtr->DRIVEY;

	SlX = (INT_32)((SsOff * SsNvcX) << 16);
	SlY = (INT_32)((SsOff * SsNvcY) << 16);

	RamWrite32A( HALL_RAM_GYROX_OUT, SlX ) ;
	RamWrite32A( HALL_RAM_GYROY_OUT, SlY ) ;

}

//********************************************************************************
// Function Name 	: TneVrtPos
// Retun Value		: NON
// Argment Value	: Position number(1, 2, 3, 4, 5, 6, 7, 0:reset)
// Explanation		: Move measurement position function
//********************************************************************************
void	TneVrtPos( UINT_8 UcPos )
{
	INT_16 SsOff = 0x0000 ;
	INT_32	SlX, SlY;
	DSPVER Info;
	LINCRS TEMP = {0, 0, 0, 0, 0};		//20190718 Komori
	LINCRS* LnCsPtr = (LINCRS*)&TEMP;	//20190718 Komori

	GetInfomationAfterStartUp( &Info );

	UcPos &= 0x07 ;

	if ( UcPos ) {
		SsOff = (INT_16)LnCsPtr->STEPY * LnCsPtr->DRIVEY * (UcPos - 4);
	}

		SlX = 0x00000000;
		SlY = (INT_32)(SsOff << 16);

	RamWrite32A( HALL_RAM_GYROX_OUT, SlX ) ;
	RamWrite32A( HALL_RAM_GYROY_OUT, SlY ) ;
}

//********************************************************************************
// Function Name 	: TneHrzPos
// Retun Value		: NON
// Argment Value	: Position number(1, 2, 3, 4, 5, 6, 7, 0:reset)
// Explanation		: Move measurement position function
//********************************************************************************
void	TneHrzPos( UINT_8 UcPos )
{
	INT_16 SsOff = 0x0000 ;
	INT_32	SlX, SlY;
	DSPVER Info;
	LINCRS TEMP = {0, 0, 0, 0, 0};		//20190718 Komori
	LINCRS* LnCsPtr = (LINCRS*)&TEMP;	//20190718 Komori

	GetInfomationAfterStartUp( &Info );

	UcPos &= 0x07 ;

	if ( UcPos ) {
		SsOff = (INT_16)LnCsPtr->STEPX * LnCsPtr->DRIVEX * (UcPos - 4);
	}

		SlX = (INT_32)(SsOff << 16);
		SlY = 0x00000000;

	RamWrite32A( HALL_RAM_GYROX_OUT, SlX ) ;
	RamWrite32A( HALL_RAM_GYROY_OUT, SlY ) ;
}

//********************************************************************************
// Function Name 	: SetSinWavGenInt
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Sine wave generator initial Function
// History			: First edition
//********************************************************************************
void	SetSinWavGenInt( void )
{

	RamWrite32A( SinWave_Offset		,	0x00000000 ) ;		// 発生周波数のオフセットを設定
	RamWrite32A( SinWave_Phase		,	0x60000000 ) ;		// 正弦波の位相量
	RamWrite32A( SinWave_Gain		,	0x00000000 ) ;		// 発生周波数のアッテネータ(初期値は0[dB])
//	RamWrite32A( SinWave_Gain		,	0x7FFFFFFF ) ;		// 発生周波数のアッテネータ(初期値はCut)
//	SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)SinWave_Output ) ;		// 初期値の出力先アドレスは、自分のメンバ

	RamWrite32A( CosWave_Offset		,	0x00000000 );		// 発生周波数のオフセットを設定
	RamWrite32A( CosWave_Phase 		,	0x00000000 );		// 正弦波の位相量
	RamWrite32A( CosWave_Gain 		,	0x00000000 );		// 発生周波数のアッテネータ(初期値はCut)
//	RamWrite32A( CosWave_Gain 		,	0x7FFFFFFF );		// 発生周波数のアッテネータ(初期値は0[dB])
//	SetTransDataAdr( CosWave_OutAddr	,	(UINT_32)CosWave_Output );		// 初期値の出力先アドレスは、自分のメンバ

	RamWrite32A( SinWaveC_Regsiter	,	0x00000000 ) ;								// Sine Wave Stop

}


//********************************************************************************
// Function Name 	: SetTransDataAdr
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Trans Address for Data Function
// History			: First edition
//********************************************************************************
void	SetTransDataAdr( UINT_16 UsLowAddress , UINT_32 UlLowAdrBeforeTrans )
{
	UnDwdVal	StTrsVal ;

	if( UlLowAdrBeforeTrans < 0x00009000 ){
		StTrsVal.StDwdVal.UsHigVal = (UINT_16)(( UlLowAdrBeforeTrans & 0x0000F000 ) >> 8 ) ;
		StTrsVal.StDwdVal.UsLowVal = (UINT_16)( UlLowAdrBeforeTrans & 0x00000FFF ) ;
	}else{
		StTrsVal.UlDwdVal = UlLowAdrBeforeTrans ;
	}
//TRACE(" TRANS  ADR = %04xh , DAT = %08xh \n",UsLowAddress , StTrsVal.UlDwdVal ) ;
	RamWrite32A( UsLowAddress	,	StTrsVal.UlDwdVal );

}


//********************************************************************************
// Function Name 	: RdStatus
// Retun Value		: 0:success 1:FAILURE
// Argment Value	: bit check  0:ALL  1:bit24
// Explanation		: High level status check Function
// History			: First edition
//********************************************************************************
UINT_8	RdStatus( UINT_8 UcStBitChk )
{
	UINT_32	UlReadVal ;

	RamRead32A( CMD_READ_STATUS , &UlReadVal );
TRACE(" (Rd St) = %08x\n", (UINT_32)UlReadVal ) ;
	if( UcStBitChk ){
		UlReadVal &= READ_STATUS_INI ;
	}
	if( !UlReadVal ){
		return( SUCCESS );
	}else{
		return( FAILURE );
	}
}


//********************************************************************************
// Function Name 	: DacControl
// Retun Value		: Firmware version
// Argment Value	: NON
// Explanation		: Dac Control Function
// History			: First edition
//********************************************************************************
void	DacControl( UINT_8 UcMode, UINT_32 UiChannel, UINT_32 PuiData )
{
	UINT_32	UlAddaInt ;
	if( !UcMode ) {
		RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_DASEL ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS , UiChannel ) ;
		RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_DAO ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS , PuiData ) ;
		;
		;
		UlAddaInt = 0x00000040 ;
		while ( (UlAddaInt & 0x00000040) != 0 ) {
			RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_ADDAINT ) ;
			RamRead32A(  CMD_IO_DAT_ACCESS , &UlAddaInt ) ;
			;
		}
	} else {
		RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_DASEL ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS , UiChannel ) ;
		RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_DAO ) ;
		RamRead32A(  CMD_IO_DAT_ACCESS , &PuiData ) ;
		;
		;
		UlAddaInt = 0x00000040 ;
		while ( (UlAddaInt & 0x00000040) != 0 ) {
			RamWrite32A( CMD_IO_ADR_ACCESS , ADDA_ADDAINT ) ;
			RamRead32A(  CMD_IO_DAT_ACCESS , &UlAddaInt ) ;
			;
		}
	}

	return ;
}

//********************************************************************************
// Function Name 	: RdHallCalData
// Retun Value		: Read calibration data
// Argment Value	: NON
// Explanation		: Read calibration Data Function
// History			: First edition
//********************************************************************************
void	RdHallCalData( void )
{
	UnDwdVal		StReadVal ;

	RamRead32A(  StCaliData_UsCalibrationStatus, &StAdjPar.StHalAdj.UlAdjPhs ) ;

//	RamRead32A( StCaliData_SiHallMax_Before_X,	&StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlxMax = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMin_Before_X, &StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlxMin = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMax_After_X,	&StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlxMxa = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMin_After_X,	&StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlxMna = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMax_Before_Y, &StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlyMax = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMin_Before_Y, &StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlyMin = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMax_After_Y,	&StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlyMxa = StReadVal.StDwdVal.UsHigVal ;
//	RamRead32A( StCaliData_SiHallMin_After_Y,	&StReadVal.UlDwdVal ) ;
//	StAdjPar.StHalAdj.UsHlyMna = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_UiHallBias_X,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsHlxGan = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_UiHallOffset_X,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsHlxOff = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_UiHallBias_Y,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsHlyGan = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_UiHallOffset_Y,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsHlyOff = StReadVal.StDwdVal.UsHigVal ;

	RamRead32A( StCaliData_SiLoopGain_X,	&StAdjPar.StLopGan.UlLxgVal ) ;
	RamRead32A( StCaliData_SiLoopGain_Y,	&StAdjPar.StLopGan.UlLygVal ) ;

	RamRead32A( StCaliData_SiLensCen_Offset_X,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsAdxOff = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_SiLensCen_Offset_Y,	&StReadVal.UlDwdVal ) ;
	StAdjPar.StHalAdj.UsAdyOff = StReadVal.StDwdVal.UsHigVal ;

	RamRead32A( StCaliData_SiGyroOffset_X,		&StReadVal.UlDwdVal ) ;
	StAdjPar.StGvcOff.UsGxoVal = StReadVal.StDwdVal.UsHigVal ;
	RamRead32A( StCaliData_SiGyroOffset_Y,		&StReadVal.UlDwdVal ) ;
	StAdjPar.StGvcOff.UsGyoVal = StReadVal.StDwdVal.UsHigVal ;

}



//********************************************************************************
// Function Name 	: OscStb
// Retun Value		: NON
// Argment Value	: Command Parameter
// Explanation		: Osc Standby Function
// History			: First edition
//********************************************************************************
void	OscStb( void )
{
	RamWrite32A( CMD_IO_ADR_ACCESS , STBOSCPLL ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS , OSC_STB ) ;
}

#if 0
//********************************************************************************
// Function Name 	: GyrSlf
// Retun Value		: Gyro self test SUCCESS or FAILURE
// Argment Value	: NON
// Explanation		: Gyro self test Function
// History			: First edition 									2016.8.3
//********************************************************************************
UINT_8	GyrSlf( void )
{
	UINT_8		UcFinSts = 0 ;
	float		flGyrRltX ;
	float		flGyrRltY ;
	float		flMeasureAveValueA , flMeasureAveValueB ;
	INT_32		SlMeasureParameterNum ;
	INT_32		SlMeasureParameterA, SlMeasureParameterB ;
	UnllnVal	StMeasValueA , StMeasValueB ;
	UINT_32		UlReadVal;

	// Setup for self test
	RamWrite32A( 0xF01D, 0x75000000 );	// Read who am I
	while( RdStatus( 0 ) ) ;

	RamRead32A ( 0xF01D, &UlReadVal );

	if( (UlReadVal >> 24) != 0x85 )
	{
		TRACE("WHO AM I read error %08X\n", UlReadVal );
		return	(0xFF);
	}

	// Pre test
	RamWrite32A( 0xF01E, 0x1B100000 );	// GYRO_CONFIG FS_SEL=2(175LSB/dps) XG_ST=OFF YG_ST=OFF
	while( RdStatus( 0 ) ) ;

	MesFil( SELFTEST ) ;

	SlMeasureParameterNum	=	20 * 4;									// 20 sample * 4FS ( 40ms )
	SlMeasureParameterA		=	(UINT_32)GYRO_RAM_GX_ADIDAT ;			// Set Measure RAM Address
	SlMeasureParameterB		=	(UINT_32)GYRO_RAM_GY_ADIDAT ;			// Set Measure RAM Address

	ClrMesFil() ;														// Clear Delay Ram
	WitTim( 300 ) ;

	RamWrite32A( StMeasFunc_PMC_UcPhaseMesMode, 0x00000000 ) ;			// Set Phase Measure Mode

	// Start measure
	MeasureStart( SlMeasureParameterNum, SlMeasureParameterA , SlMeasureParameterB ) ;
	MeasureWait() ;														// Wait complete of measurement


	RamRead32A( StMeasFunc_MFA_LLiIntegral1 	, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4 , &StMeasValueA.StUllnVal.UlHigVal ) ;

	RamRead32A( StMeasFunc_MFB_LLiIntegral2 	, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;

	flMeasureAveValueA = (float)((( (INT_64)StMeasValueA.UllnValue >> 16 ) / (float)SlMeasureParameterNum ) ) ;
	flMeasureAveValueB = (float)((( (INT_64)StMeasValueB.UllnValue >> 16 ) / (float)SlMeasureParameterNum ) ) ;

	flGyrRltX = flMeasureAveValueA / 175.0 ;	// sensitivity 175 dps
	flGyrRltY = flMeasureAveValueB / 175.0 ;	// sensitivity 175 dps

	TRACE("SlMeasureParameterNum = %08X\n", (UINT_32)SlMeasureParameterNum);
	TRACE("StMeasValueA.StUllnVal.UlLowVal = %08X\n", (UINT_32)StMeasValueA.StUllnVal.UlLowVal);
	TRACE("StMeasValueA.StUllnVal.UlHigVal = %08X\n", (UINT_32)StMeasValueA.StUllnVal.UlHigVal);
	TRACE("StMeasValueB.StUllnVal.UlLowVal = %08X\n", (UINT_32)StMeasValueB.StUllnVal.UlLowVal);
	TRACE("StMeasValueB.StUllnVal.UlHigVal = %08X\n", (UINT_32)StMeasValueB.StUllnVal.UlHigVal);
	TRACE("flMeasureAveValueA = %f\n", flMeasureAveValueA );
	TRACE("flMeasureAveValueB = %f\n", flMeasureAveValueB );
	TRACE("flGyrRltX = %f dps\n", flGyrRltX );
	TRACE("flGyrRltY = %f dps\n", flGyrRltY );

	if( fabs(flGyrRltX) >= 25 ){
		UcFinSts |= 0x10;
		TRACE( "X self test 175dps NG\n" );
	}

	if( fabs(flGyrRltY) >= 25 ){
		UcFinSts |= 0x01;
		TRACE( "Y self test 175dps NG\n" );
	}

	// Self test main
	RamWrite32A( 0xF01E, 0x1BDB0000 );	// GYRO_CONFIG FS_SEL=3(87.5LSB/dps) XG_ST=ON YG_ST=ON
	while( RdStatus( 0 ) ) ;

	ClrMesFil() ;														// Clear Delay Ram
	WitTim( 300 ) ;

	RamWrite32A( StMeasFunc_PMC_UcPhaseMesMode, 0x00000000 ) ;			// Set Phase Measure Mode

	// Start measure
	MeasureStart( SlMeasureParameterNum, SlMeasureParameterA , SlMeasureParameterB ) ;
	MeasureWait() ;														// Wait complete of measurement


	RamRead32A( StMeasFunc_MFA_LLiIntegral1 	, &StMeasValueA.StUllnVal.UlLowVal ) ;	// X axis
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4 , &StMeasValueA.StUllnVal.UlHigVal ) ;

	RamRead32A( StMeasFunc_MFB_LLiIntegral2 	, &StMeasValueB.StUllnVal.UlLowVal ) ;	// Y axis
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;

	flMeasureAveValueA = (float)((( (INT_64)StMeasValueA.UllnValue >> 16 ) / (float)SlMeasureParameterNum ) ) ;
	flMeasureAveValueB = (float)((( (INT_64)StMeasValueB.UllnValue >> 16 ) / (float)SlMeasureParameterNum ) ) ;

	flGyrRltX = flMeasureAveValueA / GYRO_SENSITIVITY ;
	flGyrRltY = flMeasureAveValueB / GYRO_SENSITIVITY ;

	TRACE("SlMeasureParameterNum = %08X\n", (UINT_32)SlMeasureParameterNum);
	TRACE("StMeasValueA.StUllnVal.UlLowVal = %08X\n", (UINT_32)StMeasValueA.StUllnVal.UlLowVal);
	TRACE("StMeasValueA.StUllnVal.UlHigVal = %08X\n", (UINT_32)StMeasValueA.StUllnVal.UlHigVal);
	TRACE("StMeasValueB.StUllnVal.UlLowVal = %08X\n", (UINT_32)StMeasValueB.StUllnVal.UlLowVal);
	TRACE("StMeasValueB.StUllnVal.UlHigVal = %08X\n", (UINT_32)StMeasValueB.StUllnVal.UlHigVal);
	TRACE("flMeasureAveValueA = %f\n", flMeasureAveValueA );
	TRACE("flMeasureAveValueB = %f\n", flMeasureAveValueB );
	TRACE("flGyrRltX = %f dps\n", flGyrRltX );
	TRACE("flGyrRltY = %f dps\n", flGyrRltY );


	if( UcFinSts != 0 )	{
		// error 175 dps
		if( fabs(flGyrRltX) >= 60){
			UcFinSts |= 0x20;
			TRACE( "X self test 87.5dps NG\n" );
		}

		if( fabs(flGyrRltY) >= 60){
			UcFinSts |= 0x02;
			TRACE( "Y self test 87.5dps NG\n" );
		}
	} else {
		// normal
		if( fabs(flGyrRltX) < 60){
			UcFinSts |= 0x20;
			TRACE( "X self test 87.5dps NG\n" );
		}

		if( fabs(flGyrRltY) < 60){
			UcFinSts |= 0x02;
			TRACE( "Y self test 87.5dps NG\n" );
		}
	}

	// Set normal mode
	RamWrite32A( 0xF01E, 0x1B180000 );	// GYRO_CONFIG FS_SEL=3(87.5LSB/dps) XG_ST=OFF YG_ST=OFF
	while( RdStatus( 0 ) ) ;


	TRACE("GyrSlf result=%02X\n", UcFinSts );
	return( UcFinSts ) ;
}
#endif

//********************************************************************************
// Function Name 	: GyrWhoAmIRead
// Retun Value		: Gyro Who am I Read
// Argment Value	: NON
// Explanation		: Gyro Who am I Read Function
// History			: First edition 									2016.11.01
//********************************************************************************
UINT_8	GyrWhoAmIRead( void )
{
	UINT_8		UcRtnVal ;
	UINT_32		UlReadVal;

	// Setup for self test
	RamWrite32A( 0xF01D, 0x75000000 );	// Read who am I
	while( RdStatus( 0 ) ) ;

	RamRead32A ( 0xF01D, &UlReadVal );
	
	UcRtnVal = UlReadVal >> 24;
	
TRACE("WHO AM I read %02X\n", UcRtnVal );
	
	return(UcRtnVal);
}

//********************************************************************************
// Function Name 	: GyrWhoAmICheck
// Retun Value		: Gyro Who am I Check
// Argment Value	: NON
// Explanation		: Gyro Who am I Chek Function
// History			: First edition 									2016.11.01
//********************************************************************************
UINT_8	GyrWhoAmICheck( void )
{
	UINT_8		UcReadVal ;
	
	UcReadVal = GyrWhoAmIRead();
	
	if( UcReadVal == 0x20 ){		// ICM-20690
TRACE("WHO AM I read success\n");
		return	(FAILURE);
	}
	else{
TRACE("WHO AM I read failure\n");
		return	(SUCCESS);
	}
}

//********************************************************************************
// Function Name 	: GyrIdRead
// Retun Value		: Gyro ID Read
// Argment Value	: NON
// Explanation		: Gyro ID Read Function
// History			: First edition 									2016.11.07
//********************************************************************************
UINT_8	GyrIdRead( UINT_8 *UcGyroID )
{
	UINT_8		i ;
	UINT_32		UlReadVal;

	for( i=0; i<7 ; i++ ){
		
		// bank_sel
		RamWrite32A( 0xF01E, 0x6D000000 );
		while( RdStatus( 0 ) ) ;
		
		// start_addr
		RamWrite32A( 0xF01E, 0x6E000000 | (i << 16) );
		while( RdStatus( 0 ) ) ;
		
		// mem_r_w
		RamWrite32A( 0xF01D, 0x6F000000 );
		while( RdStatus( 0 ) ) ;
		
		// ID0[7:0] / ID1[7:0] ... ID6[7:0]
		RamRead32A ( 0xF01D, &UlReadVal );
		UcGyroID[i] = UlReadVal >> 24;
	}
	
TRACE("UcGyroID %02X %02X %02X %02X %02X %02X %02X \n", UcGyroID[0], UcGyroID[1], UcGyroID[2], UcGyroID[3], UcGyroID[4], UcGyroID[5], UcGyroID[6] );
	
	return(SUCCESS);
}


#if 0
//********************************************************************************
// Function Name 	: GyroReCalib
// Retun Value		: Command Status
// Argment Value	: Offset information data pointer
// Explanation		: Re calibration Command Function
// History			: First edition
//********************************************************************************
UINT_8	GyroReCalib( stReCalib * pReCalib )
{
	UINT_8	UcSndDat ;
	UINT_32	UlRcvDat ;
	UINT_32	UlGofX, UlGofY ;
	UINT_32	UiChkSum ;

//------------------------------------------------------------------------------------------------
// Backup ALL Calibration data
//------------------------------------------------------------------------------------------------
	ReadCalData128( UlBufDat, &UiChkSum );

	// HighLevelコマンド
	RamWrite32A( CMD_CALIBRATION , 0x00000000 ) ;

	do {
		UcSndDat = RdStatus(1);
	} while (UcSndDat != 0);

	RamRead32A( CMD_CALIBRATION , &UlRcvDat ) ;
	UcSndDat = (unsigned char)(UlRcvDat >> 24);								// 終了ステータス

	// 戻り値を編集
	if( UlBufDat[ GYRO_FCTRY_OFST_X ] == 0xFFFFFFFF )
		pReCalib->SsFctryOffX = (UlBufDat[ GYRO_OFFSET_X ] >> 16) ;
	else
		pReCalib->SsFctryOffX = (UlBufDat[ GYRO_FCTRY_OFST_X ] >> 16) ;

	if( UlBufDat[ GYRO_FCTRY_OFST_Y ] == 0xFFFFFFFF )
		pReCalib->SsFctryOffY = (UlBufDat[ GYRO_OFFSET_Y ] >> 16) ;
	else
		pReCalib->SsFctryOffY = (UlBufDat[ GYRO_FCTRY_OFST_Y ] >> 16) ;

	// キャリブレーション後の値を取得
	RamRead32A(  GYRO_RAM_GXOFFZ , &UlGofX ) ;
	RamRead32A(  GYRO_RAM_GYOFFZ , &UlGofY ) ;

	pReCalib->SsRecalOffX = (UlGofX >> 16) ;
	pReCalib->SsRecalOffY = (UlGofY >> 16) ;
	pReCalib->SsDiffX = abs( pReCalib->SsFctryOffX - pReCalib->SsRecalOffX) ;
	pReCalib->SsDiffY = abs( pReCalib->SsFctryOffY - pReCalib->SsRecalOffY) ;

TRACE("GyroReCalib() = %02x\n", UcSndDat ) ;
TRACE("Factory X = %04X, Y = %04X\n", pReCalib->SsFctryOffX, pReCalib->SsFctryOffY );
TRACE("Recalib X = %04X, Y = %04X\n", pReCalib->SsRecalOffX, pReCalib->SsRecalOffY );
TRACE("Diff    X = %04X, Y = %04X\n", pReCalib->SsDiffX, pReCalib->SsDiffY );
TRACE("UlBufDat[19] = %08X, [20] = %08X\n", UlBufDat[19], UlBufDat[20] );
TRACE("UlBufDat[49] = %08X, [50] = %08X\n", UlBufDat[49], UlBufDat[50] );

	return( UcSndDat );
}
#endif
//********************************************************************************
// Function Name 	: ReadCalibID
// Retun Value		: Calibraion ID
// Argment Value	: NONE
// Explanation		: Read calibraion ID Function
// History			: First edition
//********************************************************************************
UINT_32	ReadCalibID( void )
{
	UINT_32	UlCalibId;

	// Read calibration data
	RamRead32A( SiCalID, &UlCalibId );

	return( UlCalibId );
}


//********************************************************************************
// Function Name 	: FrqDet
// Retun Value		: 0:PASS, 1:OIS X NG, 2:OIS Y NG, 4:CLAF NG
// Argment Value	: NON
// Explanation		: Module Check
// History			: First edition
//********************************************************************************
UINT_8 FrqDet( void )
{
	INT_32 SlMeasureParameterA , SlMeasureParameterB ;
	INT_32 SlMeasureParameterNum ;
	UINT_32 UlXasP_P , UlYasP_P ;

	UINT_8 UcRtnVal;

	UcRtnVal = 0;

	//Measurement Setup
	MesFil( OSCCHK ) ;													// Set Measure Filter

	// waiting for stable the actuator
	WitTim( 300 ) ;

#if FS_MODE == 0
	SlMeasureParameterNum	=	902 ;									// ( 50ms )
#else //FS_MODE
	SlMeasureParameterNum	=	751 ;									// ( 50ms )
#endif //FS_MODE

	SlMeasureParameterA		=	(UINT_32)HALL_RAM_HXIDAT ;				// Set Measure RAM Address
	SlMeasureParameterB		=	(UINT_32)HALL_RAM_HYIDAT ;				// Set Measure RAM Address

	// Start measure
	MeasureStart2( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB, 50 ) ;
	MeasureWait() ;														// Wait complete of measurement
	RamRead32A( StMeasFunc_MFA_UiAmp1, &UlXasP_P ) ;					// X Axis Peak to Peak
	RamRead32A( StMeasFunc_MFB_UiAmp2, &UlYasP_P ) ;					// Y Axis Peak to Peak
TRACE("UlXasP_P = 0x%08X\r\n", (UINT_32)UlXasP_P ) ;
TRACE("UlYasP_P = 0x%08X\r\n", (UINT_32)UlYasP_P ) ;

	// Amplitude value check X
	if(  UlXasP_P > ULTHDVAL ){
		UcRtnVal = 1;
	}
	// Amplitude value check Y
	if(  UlYasP_P > ULTHDVAL ){
		UcRtnVal |= 2;
	}



	return(UcRtnVal);													// Retun Status value
}

//********************************************************************************
// Function Name 	: MesRam
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Measure 
// History			: First edition 						2015.07.06 
//********************************************************************************
UINT_8	 MesRam( INT_32 SlMeasureParameterA, INT_32 SlMeasureParameterB, INT_32 SlMeasureParameterNum, stMesRam* pStMesRamA, stMesRam* pStMesRamB )
{
	UnllnVal	StMeasValueA , StMeasValueB ;
	
	MesFil( THROUGH ) ;							// Set Measure Filter
	
	MeasureStart2( SlMeasureParameterNum,  SlMeasureParameterA, SlMeasureParameterB, 1 ) ;		// Start measure
	
	MeasureWait() ;								// Wait complete of measurement
	
	// A : X axis
	RamRead32A( StMeasFunc_MFA_SiMax1 , &(pStMesRamA->SlMeasureMaxValue) ) ;			// Max value
	RamRead32A( StMeasFunc_MFA_SiMin1 , &(pStMesRamA->SlMeasureMinValue) ) ;			// Min value
	RamRead32A( StMeasFunc_MFA_UiAmp1 , &(pStMesRamA->SlMeasureAmpValue) ) ;			// Amp value
	RamRead32A( StMeasFunc_MFA_LLiIntegral1,	 &(StMeasValueA.StUllnVal.UlLowVal) ) ;	// Integration Low
	RamRead32A( StMeasFunc_MFA_LLiIntegral1 + 4, &(StMeasValueA.StUllnVal.UlHigVal) ) ;	// Integration Hig
	pStMesRamA->SlMeasureAveValue = 
				(INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;	// Ave value
	
	// B : Y axis
	RamRead32A( StMeasFunc_MFB_SiMax2 , &(pStMesRamB->SlMeasureMaxValue) ) ;			// Max value
	RamRead32A( StMeasFunc_MFB_SiMin2 , &(pStMesRamB->SlMeasureMinValue) ) ;			// Min value
	RamRead32A( StMeasFunc_MFB_UiAmp2 , &(pStMesRamB->SlMeasureAmpValue) ) ;			// Amp value
	RamRead32A( StMeasFunc_MFB_LLiIntegral2,	 &(StMeasValueB.StUllnVal.UlLowVal) ) ;	// Integration Low
	RamRead32A( StMeasFunc_MFB_LLiIntegral2 + 4, &(StMeasValueB.StUllnVal.UlHigVal) ) ;	// Integration Hig
	pStMesRamB->SlMeasureAveValue = 
				(INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;	// Ave value
	
	return( 0 );
}

//********************************************************************************
// Function Name 	: MeasGain
// Retun Value		: Hall amp & Sine amp
// Argment Value	: X,Y Direction, Freq
// Explanation		: Measuring Hall Paek To Peak
// History			: First edition 						
//********************************************************************************
#if FS_MODE == 0
#define	FS4TIME	(UINT_32)0x000119F0		// 18044 * 4
#define	FRQOFST	(UINT_32)0x0001D0E5		// 80000000h / 18044
#else //FS_MODE
#define	FS4TIME	(UINT_32)0x0000EACC		// 15027 * 4
#define	FRQOFST	(UINT_32)0x00022E3C		// 80000000h / 15027
#endif //FS_MODE

UINT_32	MeasGain ( UINT_16	UcDirSel, UINT_16	UsMeasFreq , UINT_32 UlMesAmp )
{
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum , SlSineWaveOffset;
	UnllnVal		StMeasValueA  , StMeasValueB ;
	UINT_32	UlReturnVal;

	StMeasValueA.UllnValue = 0;
	StMeasValueB.UllnValue = 0;
	SlMeasureParameterNum	=	(INT_32)( FS4TIME / (UINT_32)UsMeasFreq) * 2;	// 
	
	if( UcDirSel == X_DIR ) {								// X axis
		SlMeasureParameterA		=	HALL_RAM_HXOUT0 ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HallFilterD_HXDAZ1 ;	// Set Measure RAM Address
	} else if( UcDirSel == Y_DIR ) {						// Y axis
		SlMeasureParameterA		=	HALL_RAM_HYOUT0 ;		// Set Measure RAM Address
		SlMeasureParameterB		=	HallFilterD_HYDAZ1 ;	// Set Measure RAM Address
	}
	SetSinWavGenInt();
	
	SlSineWaveOffset = (INT_32)( FRQOFST * (UINT_32)UsMeasFreq );
	RamWrite32A( SinWave_Offset		,	SlSineWaveOffset ) ;		// Freq Setting = Freq * 80000000h / Fs	

	RamWrite32A( SinWave_Gain		,	UlMesAmp ) ;			// Set Sine Wave Gain

	RamWrite32A( SinWaveC_Regsiter	,	0x00000001 ) ;				// Sine Wave Start
	if( UcDirSel == X_DIR ) {
		SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)HALL_RAM_HXOFF1 ) ;	// Set Sine Wave Input RAM
	}else if( UcDirSel == Y_DIR ){
		SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)HALL_RAM_HYOFF1 ) ;	// Set Sine Wave Input RAM
	}
	
	MesFil2( UsMeasFreq ) ;					// Filter setting for measurement

	MeasureStart2( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB , 8000/UsMeasFreq ) ;			// Start measure
	
//#if !LPF_ENA
	WitTim( 8000 / UsMeasFreq ) ;
//#endif
	MeasureWait() ;						// Wait complete of measurement
	
	RamWrite32A( SinWaveC_Regsiter	,	0x00000000 ) ;								// Sine Wave Stop
	
	if( UcDirSel == X_DIR ) {
		SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)0x00000000 ) ;	// Set Sine Wave Input RAM
		RamWrite32A( HALL_RAM_HXOFF1		,	0x00000000 ) ;				// DelayRam Clear
	}else if( UcDirSel == Y_DIR ){
		SetTransDataAdr( SinWave_OutAddr	,	(UINT_32)0x00000000 ) ;	// Set Sine Wave Input RAM
		RamWrite32A( HALL_RAM_HYOFF1		,	0x00000000 ) ;				// DelayRam Clear
	}

	RamRead32A( StMeasFunc_MFA_LLiAbsInteg1 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	
	RamRead32A( StMeasFunc_MFA_LLiAbsInteg1 + 4 	, &StMeasValueA.StUllnVal.UlHigVal ) ;
	RamRead32A( StMeasFunc_MFB_LLiAbsInteg2 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	
	RamRead32A( StMeasFunc_MFB_LLiAbsInteg2 + 4		, &StMeasValueB.StUllnVal.UlHigVal ) ;

	
	UlReturnVal = (INT_32)((INT_64)StMeasValueA.UllnValue * 100 / (INT_64)StMeasValueB.UllnValue  ) ;


	return( UlReturnVal ) ;
}
//********************************************************************************
// Function Name 	: MesFil2
// Retun Value		: NON
// Argment Value	: Measure Filter Mode
// Explanation		: Measure Filter Setting Function
// History			: First edition 		
//********************************************************************************
#if FS_MODE == 0
#define	DivOffset	5746.68f		/* 18044.6/3.14 */
#else //FS_MODE
#define	DivOffset	4785.76f		/* 15027.3/3.14 */
#endif //FS_MODE

void	MesFil2( UINT_16	UsMesFreq )		
{
	UINT_32	UlMeasFilA1 , UlMeasFilB1 , UlMeasFilC1 , UlTempval ;
	UINT_32	UlMeasFilA2 , UlMeasFilC2 ;
		
	UlTempval = (UINT_32)(2147483647 * (float)UsMesFreq / ((float)UsMesFreq + DivOffset ));
	UlMeasFilA1	=	0x7fffffff - UlTempval;
	UlMeasFilB1	=	~UlMeasFilA1 + 0x00000001;	
	UlMeasFilC1	=	0x7FFFFFFF - ( UlTempval << 1 ) ;

	UlMeasFilA2	=	UlTempval ;	
	UlMeasFilC2	=	UlMeasFilC1 ;

	
	RamWrite32A ( MeasureFilterA_Coeff_a1	, UlMeasFilA1 ) ;
	RamWrite32A ( MeasureFilterA_Coeff_b1	, UlMeasFilB1 ) ;
	RamWrite32A ( MeasureFilterA_Coeff_c1	, UlMeasFilC1 ) ;

	RamWrite32A ( MeasureFilterA_Coeff_a2	, UlMeasFilA2 ) ;
	RamWrite32A ( MeasureFilterA_Coeff_b2	, UlMeasFilA2 ) ;
	RamWrite32A ( MeasureFilterA_Coeff_c2	, UlMeasFilC2 ) ;

	RamWrite32A ( MeasureFilterB_Coeff_a1	, UlMeasFilA1 ) ;
	RamWrite32A ( MeasureFilterB_Coeff_b1	, UlMeasFilB1 ) ;
	RamWrite32A ( MeasureFilterB_Coeff_c1	, UlMeasFilC1 ) ;

	RamWrite32A ( MeasureFilterB_Coeff_a2	, UlMeasFilA2 ) ;
	RamWrite32A ( MeasureFilterB_Coeff_b2	, UlMeasFilA2 ) ;
	RamWrite32A ( MeasureFilterB_Coeff_c2	, UlMeasFilC2 ) ;
}


//********************************************************************************
// Function Name 	: MonitorInfo
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: 
// History			: Second edition
//********************************************************************************
void MonitorInfo( DSPVER* Dspcode )
{
TRACE("Vendor : %02x \n", Dspcode->Vendor );
TRACE("User : %02x \n", Dspcode->User );
TRACE("Model : %02x \n", Dspcode->Model );
TRACE("Version : %02x \n", Dspcode->Version );


if(Dspcode->ActType == 0x00 )
TRACE("actuator type :  \n");	

TRACE("SubVer : %02x \n", Dspcode->SubVer );	
TRACE("CalibID : %02x \n", Dspcode->CalbId );	


if(Dspcode->GyroType == GYRO_ICM20690 )
TRACE("gyro type : INVEN ICM20690 \n");	
if(Dspcode->GyroType == GYRO_LSM6DSM )
TRACE("gyro type : ST LSM6DSM \n")	;

}


//********************************************************************************
// Function Name 	: GetInfomationAfterStartUp
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: <Pmem Memory> Write Data
// History			: First edition
//********************************************************************************
UINT_8 GetInfomationAfterStartUp( DSPVER* Info )
{
	UINT_32 Data;
	UINT_32 UlReadVal;

	RamWrite32A( CMD_IO_ADR_ACCESS , ROMINFO );
	RamRead32A( CMD_IO_DAT_ACCESS, &UlReadVal );
	if( (UINT_8)UlReadVal != 0x0A ) return( 1 );

	RamRead32A( (SiVerNum + 0), &Data );
	Info->Vendor 	= (UINT_8)(Data >> 24 );
	Info->User 		= (UINT_8)(Data >> 16 );
	Info->Model 		= (UINT_8)(Data >> 8 );
	Info->Version 	= (UINT_8)(Data >> 0 );
	RamRead32A( (SiVerNum + 4), &Data );
	Info->CalbId  =  (UINT_8)(Data >> 24 );
	Info->SubVer  =  (UINT_8)(Data >> 16 );
	Info->ActType =  (UINT_8)(Data >> 8 );
	Info->GyroType = (UINT_8)(Data >> 0 );

	 MonitorInfo( Info );
	return( 0 );
}


//********************************************************************************
// Function Name 	: SetAngleCorrection
// Retun Value		: True/Fail
// Argment Value	: 
// Explanation		: Angle Correction
// History			: First edition
//********************************************************************************
/*  bit7  	HX GYR			Hall X  と同方向のGyro信号がGX?               0:GX  1:GY  */
/*  bit6  	HX GYR pol		Hall X+ と同方向のGyro信号がX+とG+で同方向?   0:NEG 1:POS */
/*  bit5  	HY GYR pol		Hall Y+ と同方向のGyro信号がY+とG+で同方向?   0:NEG 1:POS */
/*  bit4  	GZ pol			基本極性に対してGyroZ信号が同方向?            0:NEG 1:POS */
/*  bit3  	HX ACL			Hall X  と同方向のAccl信号がAX?               0:AX  1:AY  */
/*  bit2  	HX ACL pol		Hall X+ と同方向のAccl信号がX+とA+で同方向?   0:NEG 1:POS */
/*  bit1  	HY ACL pol		Hall Y+ と同方向のAccl信号がY+とA+で同方向?   0:NEG 1:POS */
/*  bit0  	AZ pol			基本極性に対してAcclZ信号が同方向?            0:NEG 1:POS */
                      //   top0°btm0°//
const UINT_8 PACT0Tbl[] = { 0xFF, 0xFF };	/* Dummy table */
const UINT_8 PACT1Tbl[] = { 0x20, 0xDF };	/* [ACT_02][ACT_01][ACT_03] */
const UINT_8 PACT2Tbl[] = { 0x46, 0xB9 };	/* [---] */


UINT_8 SetAngleCorrection( float DegreeGap, UINT_8 SelectAct, UINT_8 Arrangement )
{
	double OffsetAngle = 0.0f;
	double OffsetAngleV_slt = 0.0f;
#if(SEL_MODEL == 0x10)
	double OffsetAngleS_slt = 0.0f;
#endif
	INT_32 Slgx45x = 0, Slgx45y = 0;
	INT_32 Slgy45y = 0, Slgy45x = 0;
	INT_32 Slagx45x = 0, Slagx45y = 0;
	INT_32 Slagy45y = 0, Slagy45x = 0;
	
	UINT_8	UcCnvF = 0;

	if( ( DegreeGap > 180.0f) || ( DegreeGap < -180.0f ) ) return ( 1 );
	if( Arrangement >= 2 ) return ( 1 );

/************************************************************************/
/*      	Gyro angle correction										*/
/************************************************************************/
	switch(SelectAct) {
//		case 0x00 :
//			OffsetAngle = (double)( 45.0f + DegreeGap ) * 3.141592653589793238 / 180.0f ;
//			UcCnvF = PACT1Tbl[ Arrangement ];
//			break;
//		case 0x01 :
//			OffsetAngle = (double)( 0.0f + DegreeGap ) * 3.141592653589793238 / 180.0f ;
//			UcCnvF = PACT1Tbl[ Arrangement ];
//			break;
//		case 0x02 :
//		case 0x03 :
//		case 0x05 :
//		case 0x06 :
//		case 0x07 :
//		case 0x08 :
		default :
			OffsetAngle = (double)( DegreeGap ) * 3.141592653589793238 / 180.0f ;
			UcCnvF = PACT1Tbl[ Arrangement ];
			break;
//		default :
//			break;
	}
	
	SetGyroCoef( UcCnvF );
	SetAccelCoef( UcCnvF );

	//***********************************************//
	// Gyro & Accel rotation correction
	//***********************************************//
//	Slgx45x = (INT_32)( cos( OffsetAngle )*2147483647.0);
//	Slgx45y = (INT_32)(-sin( OffsetAngle )*2147483647.0);
//	Slgy45y = (INT_32)( cos( OffsetAngle )*2147483647.0);
//	Slgy45x = (INT_32)( sin( OffsetAngle )*2147483647.0);
	
	RamWrite32A( GyroFilterTableX_gx45x , 			(UINT_32)Slgx45x );
	RamWrite32A( GyroFilterTableX_gx45y , 			(UINT_32)Slgx45y );
	RamWrite32A( GyroFilterTableY_gy45y , 			(UINT_32)Slgy45y );
	RamWrite32A( GyroFilterTableY_gy45x , 			(UINT_32)Slgy45x );
	RamWrite32A( Accl45Filter_XAmain , 				(UINT_32)Slgx45x );
	RamWrite32A( Accl45Filter_XAsub  , 				(UINT_32)Slgx45y );
	RamWrite32A( Accl45Filter_YAmain , 				(UINT_32)Slgy45y );
	RamWrite32A( Accl45Filter_YAsub  , 				(UINT_32)Slgy45x );
	
	if(SelectAct == 0x00) {
		OffsetAngleV_slt = (double)( 45.0f ) * 3.141592653589793238 / 180.0f ;
	}else{
		OffsetAngleV_slt = (double)( 0.0f ) * 3.141592653589793238 / 180.0f ;
	}
//	Slagx45x = (INT_32)( cos( OffsetAngleV_slt )*2147483647.0);
//	Slagx45y = (INT_32)(-sin( OffsetAngleV_slt )*2147483647.0);
//	Slagy45y = (INT_32)( cos( OffsetAngleV_slt )*2147483647.0);
//	Slagy45x = (INT_32)( sin( OffsetAngleV_slt )*2147483647.0);
	RamWrite32A( X_main , 			(UINT_32)Slagx45x );
	RamWrite32A( X_sub , 			(UINT_32)Slagx45y );
	RamWrite32A( Y_main , 			(UINT_32)Slagy45y );
	RamWrite32A( Y_sub , 			(UINT_32)Slagy45x );

#if(SEL_MODEL == 0x10)
	OffsetAngleS_slt = (double)( -90.0f ) * 3.141592653589793238 / 180.0f ;
//	Slagx45x = (INT_32)( cos( OffsetAngleS_slt )*2147483647.0);
//	Slagx45y = (INT_32)(-sin( OffsetAngleS_slt )*2147483647.0);
//	Slagy45y = (INT_32)( cos( OffsetAngleS_slt )*2147483647.0);
//	Slagy45x = (INT_32)( sin( OffsetAngleS_slt )*2147483647.0);
	RamWrite32A( SX_main , 			(UINT_32)Slagx45x );
	RamWrite32A( SX_sub , 			(UINT_32)Slagx45y );
	RamWrite32A( SY_main , 			(UINT_32)Slagy45y );
	RamWrite32A( SY_sub , 			(UINT_32)Slagy45x );
#endif
	return ( 0 );
}

void	SetGyroCoef( UINT_8 UcCnvF )
{
	INT_32 Slgxx = 0, Slgxy = 0;
	INT_32 Slgyy = 0, Slgyx = 0;
	INT_32 Slgzp = 0;
	/************************************************/
	/*  signal convet								*/
	/************************************************/
	switch( UcCnvF & 0xE0 ){
		/* HX <== GX , HY <== GY */
	case 0x00:
		Slgxx = 0x7FFFFFFF ;	Slgxy = 0x00000000 ;	Slgyy = 0x7FFFFFFF ;	Slgyx = 0x00000000 ;	break;	//HX<==GX(NEG), HY<==GY(NEG)
	case 0x20:
		Slgxx = 0x7FFFFFFF ;	Slgxy = 0x00000000 ;	Slgyy = 0x80000001 ;	Slgyx = 0x00000000 ;	break;	//HX<==GX(NEG), HY<==GY(POS)
	case 0x40:
		Slgxx = 0x80000001 ;	Slgxy = 0x00000000 ;	Slgyy = 0x7FFFFFFF ;	Slgyx = 0x00000000 ;	break;	//HX<==GX(POS), HY<==GY(NEG)
	case 0x60:
		Slgxx = 0x80000001 ;	Slgxy = 0x00000000 ;	Slgyy = 0x80000001 ;	Slgyx = 0x00000000 ;	break;	//HX<==GX(POS), HY<==GY(POS)
		/* HX <== GY , HY <== GX */
	case 0x80:
		Slgxx = 0x00000000 ;	Slgxy = 0x7FFFFFFF ;	Slgyy = 0x00000000 ;	Slgyx = 0x7FFFFFFF ;	break;	//HX<==GY(NEG), HY<==GX(NEG)
	case 0xA0:
		Slgxx = 0x00000000 ;	Slgxy = 0x7FFFFFFF ;	Slgyy = 0x00000000 ;	Slgyx = 0x80000001 ;	break;	//HX<==GY(NEG), HY<==GX(POS)
	case 0xC0:
		Slgxx = 0x00000000 ;	Slgxy = 0x80000001 ;	Slgyy = 0x00000000 ;	Slgyx = 0x7FFFFFFF ;	break;	//HX<==GY(POS), HY<==GX(NEG)
	case 0xE0:
		Slgxx = 0x00000000 ;	Slgxy = 0x80000001 ;	Slgyy = 0x00000000 ;	Slgyx = 0x80000001 ;	break;	//HX<==GY(NEG), HY<==GX(NEG)
	}
	switch( UcCnvF & 0x10 ){
	case 0x00:
		Slgzp = 0x7FFFFFFF ;	break;																			//GZ(POS)
	case 0x10:
		Slgzp = 0x80000001 ;	break;																			//GZ(NEG)
	}
	RamWrite32A( MS_SEL_GX0 , (UINT_32)Slgxx );
	RamWrite32A( MS_SEL_GX1 , (UINT_32)Slgxy );
	RamWrite32A( MS_SEL_GY0 , (UINT_32)Slgyy );
	RamWrite32A( MS_SEL_GY1 , (UINT_32)Slgyx );
	RamWrite32A( MS_SEL_GZ , (UINT_32)Slgzp );
}

void	SetAccelCoef( UINT_8 UcCnvF )
{
	INT_32 Slaxx = 0, Slaxy = 0;
	INT_32 Slayy = 0, Slayx = 0;
	INT_32 Slazp = 0;
	
	switch( UcCnvF & 0x0E ){
		/* HX <== AX , HY <== AY */
	case 0x00:
		Slaxx = 0x7FFFFFFF ;	Slaxy = 0x00000000 ;	Slayy = 0x7FFFFFFF ;	Slayx = 0x00000000 ;	break;	//HX<==AX(NEG), HY<==AY(NEG)
	case 0x02:
		Slaxx = 0x7FFFFFFF ;	Slaxy = 0x00000000 ;	Slayy = 0x80000001 ;	Slayx = 0x00000000 ;	break;	//HX<==AX(NEG), HY<==AY(POS)
	case 0x04:
		Slaxx = 0x80000001 ;	Slaxy = 0x00000000 ;	Slayy = 0x7FFFFFFF ;	Slayx = 0x00000000 ;	break;	//HX<==AX(POS), HY<==AY(NEG)
	case 0x06:
		Slaxx = 0x80000001 ;	Slaxy = 0x00000000 ;	Slayy = 0x80000001 ;	Slayx = 0x00000000 ;	break;	//HX<==AX(POS), HY<==AY(POS)
		/* HX <== AY , HY <== AX */
	case 0x08:
		Slaxx = 0x00000000 ;	Slaxy = 0x7FFFFFFF ;	Slayy = 0x00000000 ;	Slayx = 0x7FFFFFFF ;	break;	//HX<==AY(NEG), HY<==AX(NEG)
	case 0x0A:
		Slaxx = 0x00000000 ;	Slaxy = 0x7FFFFFFF ;	Slayy = 0x00000000 ;	Slayx = 0x80000001 ;	break;	//HX<==AY(NEG), HY<==AX(POS)
	case 0x0C:
		Slaxx = 0x00000000 ;	Slaxy = 0x80000001 ;	Slayy = 0x00000000 ;	Slayx = 0x7FFFFFFF ;	break;	//HX<==AY(POS), HY<==AX(NEG)
	case 0x0E:
		Slaxx = 0x00000000 ;	Slaxy = 0x80000001 ;	Slayy = 0x00000000 ;	Slayx = 0x80000001 ;	break;	//HX<==AY(NEG), HY<==AX(NEG)
	}
	switch( UcCnvF & 0x01 ){
	case 0x00:
		Slazp = 0x7FFFFFFF ;	break;																			//AZ(POS)
	case 0x01:
		Slazp = 0x80000001 ;	break;																			//AZ(NEG)
	}
	RamWrite32A( MS_SEL_AX0 , (UINT_32)Slaxx );
	RamWrite32A( MS_SEL_AX1 , (UINT_32)Slaxy );
	RamWrite32A( MS_SEL_AY0 , (UINT_32)Slayy );
	RamWrite32A( MS_SEL_AY1 , (UINT_32)Slayx );
	RamWrite32A( MS_SEL_AZ , (UINT_32)Slazp );
}


//********************************************************************************
// Function Name 	: SetGyroOffset
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: set the gyro offset data. before do this before Remapmain.
// History			: First edition 						
//********************************************************************************
void	SetGyroOffset( UINT_16 GyroOffsetX, UINT_16 GyroOffsetY, UINT_16 GyroOffsetZ )
{
	RamWrite32A( GYRO_RAM_GXOFFZ , (( GyroOffsetX << 16 ) & 0xFFFF0000 ) ) ;		// X axis Gyro offset
	RamWrite32A( GYRO_RAM_GYOFFZ , (( GyroOffsetY << 16 ) & 0xFFFF0000 ) ) ;		// Y axis Gyro offset
	RamWrite32A( GYRO_ZRAM_GZOFFZ , (( GyroOffsetZ << 16 ) & 0xFFFF0000 ) ) ;		// Z axis Gyro offset
}


//********************************************************************************
// Function Name 	: SetAcclOffset
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: set the accl offset data. before do this before Remapmain.
// History			: First edition 						
//********************************************************************************
void	SetAcclOffset( UINT_16 AcclOffsetX, UINT_16 AcclOffsetY, UINT_16 AcclOffsetZ )
{
	RamWrite32A( ACCLRAM_X_AC_OFFSET , ( ( AcclOffsetX << 16 ) & 0xFFFF0000 ) ) ;		// X axis Accl offset
	RamWrite32A( ACCLRAM_Y_AC_OFFSET , ( ( AcclOffsetY << 16 ) & 0xFFFF0000 ) ) ;		// Y axis Accl offset
	RamWrite32A( ACCLRAM_Z_AC_OFFSET , ( ( AcclOffsetZ << 16 ) & 0xFFFF0000 ) ) ;		// Z axis Accl offset
}

void	GetGyroOffset( UINT_16* GyroOffsetX, UINT_16* GyroOffsetY, UINT_16* GyroOffsetZ )
{
	UINT_32	ReadValX, ReadValY, ReadValZ;
	RamRead32A( GYRO_RAM_GXOFFZ , &ReadValX );	
	RamRead32A( GYRO_RAM_GYOFFZ , &ReadValY );	
	RamRead32A( GYRO_ZRAM_GZOFFZ , &ReadValZ );	
	*GyroOffsetX = ( UINT_16 )(( ReadValX >> 16) & 0x0000FFFF );
	*GyroOffsetY = ( UINT_16 )(( ReadValY >> 16) & 0x0000FFFF );
	*GyroOffsetZ = ( UINT_16 )(( ReadValZ >> 16) & 0x0000FFFF );	
}
	
void	GetAcclOffset( UINT_16* AcclOffsetX, UINT_16* AcclOffsetY, UINT_16* AcclOffsetZ )
{
	UINT_32	ReadValX, ReadValY, ReadValZ;
	RamRead32A( ACCLRAM_X_AC_OFFSET , &ReadValX );	
	RamRead32A( ACCLRAM_Y_AC_OFFSET , &ReadValY );	
	RamRead32A( ACCLRAM_Z_AC_OFFSET , &ReadValZ );	
	*AcclOffsetX = ( UINT_16 )(( ReadValX >> 16) & 0x0000FFFF );
	*AcclOffsetY = ( UINT_16 )(( ReadValY >> 16) & 0x0000FFFF );
	*AcclOffsetZ = ( UINT_16 )(( ReadValZ >> 16) & 0x0000FFFF );	
}

UINT_8	TstActMov( UINT_8 UcDirSel )
{
	UINT_8	UcRsltSts = 0;
	INT_32	SlMeasureParameterNum ;
	INT_32	SlMeasureParameterA , SlMeasureParameterB ;
	UnllnVal	StMeasValueA  , StMeasValueB ;
	float		SfLimit , Sfzoom , Sflenz , Sfshift ;
	UINT_32		UlLimit , Ulzoom , Ullenz , Ulshift , UlActChkLvl ;
	UINT_8		i;
	UINT_32		UlReturnVal;

	if( UcDirSel == 0x00 ) {							
		RamRead32A( Gyro_Limiter_X 			, ( UINT_32 * )&UlLimit ) ;	
		RamRead32A( GyroFilterTableX_gxzoom , ( UINT_32 * )&Ulzoom ) ;	
		RamRead32A( GyroFilterTableX_gxlenz , ( UINT_32 * )&Ullenz ) ;	
		RamRead32A( GyroFilterShiftX 		, ( UINT_32 * )&Ulshift ) ;	
	}else{
		RamRead32A( Gyro_Limiter_Y 			, ( UINT_32 * )&UlLimit ) ;	
		RamRead32A( GyroFilterTableY_gyzoom , ( UINT_32 * )&Ulzoom ) ;	
		RamRead32A( GyroFilterTableY_gylenz , ( UINT_32 * )&Ullenz ) ;	
		RamRead32A( GyroFilterShiftY 		, ( UINT_32 * )&Ulshift ) ;	
	}

TRACE(" DIR = %d, lmt = %08x, zom = %08x , lnz = %08x ,sft = %08x \n", UcDirSel, (unsigned int)UlLimit , (unsigned int)Ulzoom , (unsigned int)Ullenz , (unsigned int)Ulshift  ) ;

	SfLimit = (float)UlLimit / (float)0x7FFFFFFF;
	if( Ulzoom == 0){
		Sfzoom = 0;
	}else{
		Sfzoom = (float)abs(Ulzoom) / (float)0x7FFFFFFF;
	}
	if( Ullenz == 0){
		Sflenz = 0;
	}else{
		Sflenz = (float)Ullenz / (float)0x7FFFFFFF;
	}
	Ulshift = ( Ulshift & 0x0000FF00) >> 8 ;	
	Sfshift = 1;
	for( i = 0 ; i < Ulshift ; i++ ){
		Sfshift *= 2;
	}
	UlActChkLvl = (UINT_32)( (float)0x7FFFFFFF * SfLimit * Sfzoom * Sflenz * Sfshift * ACT_MARGIN );
TRACE(" lvl = %08x \n", (unsigned int)UlActChkLvl  ) ;

	SlMeasureParameterNum	=	ACT_CHK_NUM ;

	if( UcDirSel == 0x00 ) {								
		SlMeasureParameterA		=	HALL_RAM_HXOFF1 ;		
		SlMeasureParameterB		=	HallFilterD_HXDAZ1 ;	
	} else if( UcDirSel == 0x01 ) {						
		SlMeasureParameterA		=	HALL_RAM_HYOFF1 ;		
		SlMeasureParameterB		=	HallFilterD_HYDAZ1 ;	
	}
	SetSinWavGenInt();
	
	RamWrite32A( 0x02FC		,	ACT_CHK_FRQ ) ;		
	RamWrite32A( 0x0304		,	UlActChkLvl ) ;		
	RamWrite32A( 0x02F4		,	0x00000001 ) ;		
	if( UcDirSel == 0x00 ) {
		SetTransDataAdr( 0x030C	,	(UINT_32)HALL_RAM_HXOFF1 ) ;	
	}else if( UcDirSel == 0x01 ){
		SetTransDataAdr( 0x030C	,	(UINT_32)HALL_RAM_HYOFF1 ) ;	
	}
	RamWrite32A ( 0x8388	, 0x03E452C7 ) ;
	RamWrite32A ( 0x8380	, 0x03E452C7 ) ;
	RamWrite32A ( 0x8384	, 0x78375A71 ) ;

	RamWrite32A ( 0x8394	, 0x03E452C7 ) ;
	RamWrite32A ( 0x838C	, 0x03E452C7 ) ;
	RamWrite32A ( 0x8390	, 0x78375A71 ) ;

	RamWrite32A ( 0x83A0	, 0x03E452C7 ) ;
	RamWrite32A ( 0x8398	, 0x03E452C7 ) ;
	RamWrite32A ( 0x839C	, 0x78375A71 ) ;

	RamWrite32A ( 0x83AC	, 0x03E452C7 ) ;
	RamWrite32A ( 0x83A4	, 0x03E452C7 ) ;
	RamWrite32A ( 0x83A8	, 0x78375A71 ) ;

	MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;		
	
	MeasureWait() ;		
	
	RamWrite32A( 0x02F4	,	0x00000000 ) ;	
	
	if( UcDirSel == 0x00 ) {
		SetTransDataAdr( 0x030C	,	(UINT_32)0x00000000 ) ;
		RamWrite32A( HALL_RAM_HXOFF1		,	0x00000000 ) ;		
	}else if( UcDirSel == 0x01 ){
		SetTransDataAdr( 0x030C	,	(UINT_32)0x00000000 ) ;
		RamWrite32A( HALL_RAM_HYOFF1		,	0x00000000 ) ;		
	}
	RamRead32A( 0x0298 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	
	RamRead32A( 0x0298 + 4 	, &StMeasValueA.StUllnVal.UlHigVal ) ;
	RamRead32A( 0x02C0 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	
	RamRead32A( 0x02C0 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;


	UlReturnVal = (INT_32)((INT_64)StMeasValueA.UllnValue * 100 / (INT_64)StMeasValueB.UllnValue  ) ;


TRACE(" Ret = %d \n", (unsigned int)UlReturnVal ) ;

	
	UcRsltSts = EXE_END ;
	if( UlReturnVal < ACT_THR ){
		if ( !UcDirSel ) {				
			UcRsltSts = EXE_HXMVER ;
		}else{							
			UcRsltSts = EXE_HYMVER ;
		}
	}

	return( UcRsltSts ) ;

}
UINT_8	RunHea( void )
{
	UINT_8 	UcRst ;
	UcRst = EXE_END ;
	UcRst |= TstActMov( 0x00 ) ;
	UcRst |= TstActMov( 0x01 ) ;
	
//TRACE("UcRst = %02x\n", UcRst ) ;
	return( UcRst ) ;
}


UINT_8	RunGea( void )
{
	UnllnVal	StMeasValueA , StMeasValueB ;
	INT_32		SlMeasureParameterA , SlMeasureParameterB ;
	UINT_8 		UcRst, UcCnt, UcXLowCnt, UcYLowCnt, UcXHigCnt, UcYHigCnt ;
	UINT_16		UsGxoVal[10], UsGyoVal[10], UsDif;
	INT_32		SlMeasureParameterNum , SlMeasureAveValueA , SlMeasureAveValueB ;

	
	UcRst = EXE_END ;
	UcXLowCnt = UcYLowCnt = UcXHigCnt = UcYHigCnt = 0 ;
	
	RamWrite32A ( 0x8388	, 0x7FFFFFFF ) ;
	RamWrite32A ( 0x8380	, 0x00000000 ) ;
	RamWrite32A ( 0x8384	, 0x00000000 ) ;

	RamWrite32A ( 0x8394	, 0x7FFFFFFF ) ;
	RamWrite32A ( 0x838C	, 0x00000000 ) ;
	RamWrite32A ( 0x8390	, 0x00000000 ) ;

	RamWrite32A ( 0x83A0	, 0x7FFFFFFF ) ;
	RamWrite32A ( 0x8398	, 0x00000000 ) ;
	RamWrite32A ( 0x839C	, 0x00000000 ) ;

	RamWrite32A ( 0x83AC	, 0x7FFFFFFF ) ;
	RamWrite32A ( 0x83A4	, 0x00000000 ) ;
	RamWrite32A ( 0x83A8	, 0x00000000 ) ;
	
	for( UcCnt = 0 ; UcCnt < 10 ; UcCnt++ )
	{
	

		SlMeasureParameterNum	=	GEA_NUM ;					
		SlMeasureParameterA		=	GYRO_RAM_GX_ADIDAT ;		
		SlMeasureParameterB		=	GYRO_RAM_GY_ADIDAT ;		
		
		MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;		
	
		MeasureWait() ;				
	
//TRACE("Read Adr = %04x, %04xh \n",StMeasFunc_MFA_LLiIntegral1 + 4 , StMeasFunc_MFA_LLiIntegral1) ;
		RamRead32A( 0x0290 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	
		RamRead32A( 0x0290 + 4	, &StMeasValueA.StUllnVal.UlHigVal ) ;
		RamRead32A( 0x02B8 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	
		RamRead32A( 0x02B8 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;
	
TRACE("GX_OFT = %08x, %08xh \n",(unsigned int)StMeasValueA.StUllnVal.UlHigVal,(unsigned int)StMeasValueA.StUllnVal.UlLowVal) ;
TRACE("GY_OFT = %08x, %08xh \n",(unsigned int)StMeasValueB.StUllnVal.UlHigVal,(unsigned int)StMeasValueB.StUllnVal.UlLowVal) ;
		SlMeasureAveValueA = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
		SlMeasureAveValueB = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;
TRACE("GX_AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueA) ;
TRACE("GY_AVEOFT = %08xh \n",(unsigned int)SlMeasureAveValueB) ;
		// 
		UsGxoVal[UcCnt] = (UINT_16)( SlMeasureAveValueA >> 16 );	
		
		// 
		UsGyoVal[UcCnt] = (UINT_16)( SlMeasureAveValueB >> 16 );	
		
TRACE("UcCnt = %02x, UsGxoVal[UcCnt] = %04x\n", UcCnt, UsGxoVal[UcCnt] ) ;
TRACE("UcCnt = %02x, UsGyoVal[UcCnt] = %04x\n", UcCnt, UsGyoVal[UcCnt] ) ;
		
		
		if( UcCnt > 0 )
		{
			if ( (INT_16)UsGxoVal[0] > (INT_16)UsGxoVal[UcCnt] ) {
				UsDif = (UINT_16)((INT_16)UsGxoVal[0] - (INT_16)UsGxoVal[UcCnt]) ;
			} else {
				UsDif = (UINT_16)((INT_16)UsGxoVal[UcCnt] - (INT_16)UsGxoVal[0]) ;
			}
			
			if( UsDif > GEA_DIF_HIG ) {
				UcXHigCnt ++ ;
			}
			if( UsDif < GEA_DIF_LOW ) {
				UcXLowCnt ++ ;
			}
TRACE("CNT = %02x  ,  X diff = %04x ", UcCnt , UsDif ) ;
			
			if ( (INT_16)UsGyoVal[0] > (INT_16)UsGyoVal[UcCnt] ) {
				UsDif = (UINT_16)((INT_16)UsGyoVal[0] - (INT_16)UsGyoVal[UcCnt]) ;
			} else {
				UsDif = (UINT_16)((INT_16)UsGyoVal[UcCnt] - (INT_16)UsGyoVal[0]) ;
			}
			
			if( UsDif > GEA_DIF_HIG ) {
				UcYHigCnt ++ ;
			}
			if( UsDif < GEA_DIF_LOW ) {
				UcYLowCnt ++ ;
			}
TRACE("  Y diff = %04x \n", UsDif ) ;
		}
	}
	
	if( UcXHigCnt >= 1 ) {
		UcRst = UcRst | EXE_GXABOVE ;
	}
	if( UcXLowCnt > 8 ) {
		UcRst = UcRst | EXE_GXBELOW ;
	}
	
	if( UcYHigCnt >= 1 ) {
		UcRst = UcRst | EXE_GYABOVE ;
	}
	if( UcYLowCnt > 8 ) {
		UcRst = UcRst | EXE_GYBELOW ;
	}
	
TRACE("UcRst = %02x\n", UcRst ) ;
	
	return( UcRst ) ;
}


