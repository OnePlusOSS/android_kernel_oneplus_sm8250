/**
 * @brief		OIS system header for LC898128
 * 				API List for customers
 *
 * @author		Copyright (C) 2015, ON Semiconductor, all right reserved.
 *
 * @file		OisAPI.h
 * @date		svn:$Date:: 2016-06-17 16:42:32 +0900#$
 * @version		svn:$Revision: 54 $
 * @attention
 **/
#ifndef OISAPI_H_
#define OISAPI_H_
#include	"MeasurementLibrary.h"

//****************************************************
//	extern selector for API
//****************************************************
#ifdef	__OISCMD__
	#define	__OIS_CMD_HEADER__
#else
	#define	__OIS_CMD_HEADER__		extern
#endif

#ifdef	__OISFLSH__
	#define	__OIS_FLSH_HEADER__
#else
	#define	__OIS_FLSH_HEADER__		extern
#endif

#ifdef	__OISE2PH__
	#define	__OIS_E2PR_HEADER__
#else
	#define	__OIS_E2PR_HEADER__		extern
#endif
//****************************************************
//	MODE SELECTORS (Compile Switches)
//****************************************************
#define			__OIS_MODULE_CALIBRATION__		//!< for module maker to done the calibration.
//#define		__OIS_BIG_ENDIAN__				//!< endian of MPU

//#define		__OIS_CLOSED_AF__

//****************************************************
//	STRUCTURE DEFINE
//****************************************************
typedef struct {
	UINT_16				Index;
	UINT_8	            MasterSlave;    // 1: Normal OIS FW, 2: Servo ON FW
	UINT_8	            FWType;    // 1: Normal OIS FW, 2: Servo ON FW
	const UINT_8*		UpdataCode;
	UINT_32				SizeUpdataCode;
	UINT_64				SizeUpdataCodeCksm;
	const UINT_8*		FromCode;
	UINT_32				SizeFromCode;
	UINT_64				SizeFromCodeCksm;
	UINT_32				SizeFromCodeValid;
}	DOWNLOAD_TBL_EXT;
typedef struct STRECALIB {
	INT_16	SsFctryOffX ;
	INT_16	SsFctryOffY ;
	INT_16	SsRecalOffX ;
	INT_16	SsRecalOffY ;
	INT_16	SsDiffX ;
	INT_16	SsDiffY ;
} stReCalib ;

typedef struct STMESRAM {
	INT_32	SlMeasureMaxValue ;
	INT_32	SlMeasureMinValue ;
	INT_32	SlMeasureAmpValue ;
	INT_32	SlMeasureAveValue ;
} stMesRam ;									// Struct Measure Ram

typedef struct {
	UINT_32 XBiasInit;
	UINT_32 YBiasInit;
	UINT_32 XOffsetInit;
	UINT_32 YOffsetInit;
	UINT_32 OffsetMargin;
	UINT_32 XTargetRange;
	UINT_32 XTargetMax;
	UINT_32 XTargetMin;
	UINT_32 YTargetRange;
	UINT_32 YTargetMax;
	UINT_32 YTargetMin;
	UINT_32 SinNum;
	UINT_32 SinFreq;
	UINT_32 XSinGain;
	UINT_32 YSinGain;
	UINT_32 DecrementStep;
	UINT_32 ActMaxDrive_X;
	UINT_32 ActMaxDrive_Y;
	UINT_32 ActMinDrive_X;
	UINT_32 ActMinDrive_Y;
	UINT_32 ActStep_X;
	UINT_32 ActStep_X_Num;
	UINT_32 ActStep_X_time;
	UINT_32 ActStep_Y;
	UINT_32 ActStep_Y_Num;
	UINT_32 ActStep_Y_time;
	UINT_32 WaitTime;	
} ADJ_HALL;

typedef struct {
	UINT_32 Hxgain;
	UINT_32 Hygain;
	UINT_32 XNoiseNum;
	UINT_32 XNoiseFreq;
	UINT_32 XNoiseGain;
	UINT_32 XGap;
	UINT_32 YNoiseNum;
	UINT_32 YNoiseFreq;
	UINT_32 YNoiseGain;
	UINT_32 YGap;
	UINT_32 XJudgeHigh;
	UINT_32 XJudgeLow;
	UINT_32 YJudgeHigh;
	UINT_32 YJudgeLow;
} ADJ_LOPGAN;
	
typedef struct {
	UINT_8 Vendor;
	UINT_8 User;
	UINT_8 Model;
	UINT_8 Version;
	UINT_8 CalbId;
	UINT_8 SubVer;
	UINT_8 ActType;
	UINT_8 GyroType;
} DSPVER;

typedef struct {
	UINT_8	XY_SWAP;
	INT_32	STEPX;
	INT_32	STEPY;
	INT_32	DRIVEX;
	INT_32	DRIVEY;
} LINCRS;

typedef struct STCRSPOINT {

	mlPoint	point[7] ;

} stCrsPoint ;

typedef struct {
	double XonXmove[7];
	double YonXmove[7];
	double XonYmove[7];
	double YonYmove[7];
} stPixelCoordinate;

typedef struct {
	INT_32 ACTIVE_GG_X_000;
	INT_32 ACTIVE_GG_X_090;
	INT_32 ACTIVE_GG_X_180;
	INT_32 ACTIVE_GG_X_270;
	INT_32 ACTIVE_GG_X_UP;
	INT_32 ACTIVE_GG_X_DOWN;
	INT_32 ACTIVE_GG_Y_000;
	INT_32 ACTIVE_GG_Y_090;
	INT_32 ACTIVE_GG_Y_180;
	INT_32 ACTIVE_GG_Y_270;
	INT_32 ACTIVE_GG_Y_UP;
	INT_32 ACTIVE_GG_Y_DOWN;
} stAGG;


//****************************************************
//	API LIST
//****************************************************
/* Status Read and OIS enable [mandatory] */
__OIS_CMD_HEADER__	UINT_8	RdStatus( UINT_8 ) ;						//!< Status Read whether initialization finish or not.
__OIS_CMD_HEADER__	void	OisEna( void ) ;						//!< OIS Enable function
__OIS_CMD_HEADER__	void	OisDis( void ) ;						//!< OIS Disable function
__OIS_CMD_HEADER__	void	OisEnaNCL( void ) ;						//!< OIS Enable function w/o delay clear
__OIS_CMD_HEADER__	void	OisPause( void );						//!< OIS disable function w/ pause

/* Others [option] */
__OIS_CMD_HEADER__	UINT_8	RtnCen( UINT_8 ) ;						//!< Return to center function. Hall servo on/off
__OIS_CMD_HEADER__	void	OisEnaDrCl( void ) ;					//!< OIS Enable function force drift cancel
__OIS_CMD_HEADER__	void	OisEnaDrNcl( void ) ;					//!< OIS Enable function w/o delay clear and force drift cancel
__OIS_CMD_HEADER__	void	SetRec( void ) ;						//!< Change to recording mode function
__OIS_CMD_HEADER__	void	SetStill( void ) ;						//!< Change to still mode function

__OIS_CMD_HEADER__	void	SetStandbyMode( void );					//!< Set Standby mode
__OIS_CMD_HEADER__	void	SetActiveMode( void );					//!< Set Active mode

__OIS_CMD_HEADER__	void	SetPanTiltMode( UINT_8 ) ;				//!< Pan/Tilt control (default ON)
//__OIS_CMD_HEADER__	void	RdHallCalData( void ) ;					//!< Read Hall Calibration Data in Data Ram

__OIS_CMD_HEADER__	UINT_8	RunHea( void ) ;						//!< Hall Examination of Acceptance
__OIS_CMD_HEADER__	UINT_8	RunGea( void ) ;						//!< Gyro Examination of Acceptance
//__OIS_CMD_HEADER__	UINT_8	RunGea2( UINT_8 ) ;						//!< Gyro Examination of Acceptance


__OIS_CMD_HEADER__	void	OscStb( void );							//!< Standby the oscillator
//__OIS_CMD_HEADER__	UINT_8	GyroReCalib( stReCalib * ) ;			//!< Gyro offset re-calibration
__OIS_CMD_HEADER__	UINT_32	ReadCalibID( void ) ;					//!< Read calibration ID
//__OIS_CMD_HEADER__	UINT_16	GyrSlf( void ) ;						//!< Gyro self test

__OIS_CMD_HEADER__	UINT_8	GyrWhoAmIRead( void ) ;					//!< Gyro Who am I Read
__OIS_CMD_HEADER__	UINT_8	GyrWhoAmICheck( void ) ;				//!< Gyro Who am I Check
__OIS_CMD_HEADER__	UINT_8	GyrIdRead( UINT_8 * ) ;					//!< Gyro ID Read

__OIS_CMD_HEADER__	UINT_8	MesRam( INT_32 , INT_32 , INT_32 , stMesRam* , stMesRam* );

#ifdef	__OIS_MODULE_CALIBRATION__

 /* Calibration Main [mandatory] */
 __OIS_CMD_HEADER__	UINT_32	TneRun( void );							//!< calibration for bi-direction AF

 __OIS_CMD_HEADER__	void	TneSltPos( UINT_8 ) ;					//!< for NVC
 __OIS_CMD_HEADER__	void	TneVrtPos( UINT_8 ) ;					//!< for CROSS TALK
 __OIS_CMD_HEADER__ void	TneHrzPos( UINT_8 ) ;					//!< for CROSS TALK
 __OIS_CMD_HEADER__ UINT_32	TneAvc( UINT_8 ) ;						//!< calibration for 6 axis offset
 __OIS_CMD_HEADER__	UINT_8	FrqDet( void ) ;						//!< oscillation detect

 __OIS_CMD_HEADER__	UINT_8	WrHallCalData( UINT_8 UcMode );
 __OIS_CMD_HEADER__	UINT_8	WrGyroGainData( UINT_8 ) ;				//!< upload the gyro gain to Flash
 __OIS_CMD_HEADER__	UINT_8	WrMixingData( void ) ;					//!< Flash Write Mixing Data Function
 __OIS_CMD_HEADER__	UINT_8	WrMixCalData( UINT_8, mlMixingValue * ) ;
 __OIS_CMD_HEADER__	UINT_8	WrGyroOffsetData( void ) ;

 #ifdef	HF_LINEAR_ENA
// __OIS_CMD_HEADER__	void	SetHalLnData( UINT_16 * );
// __OIS_CMD_HEADER__	INT_16	WrHalLnData( UINT_8 );
 #endif	// HF_LINEAR_ENA

 #ifdef	HF_MIXING_ENA
 __OIS_CMD_HEADER__	INT_8	WrMixCalData( UINT_8, mlMixingValue * ) ;//!< upload the mixing coefficient to Flash
 #endif	// HF_MIXING_ENA
 __OIS_CMD_HEADER__	UINT_8	WrAclOffsetData( void ) ;				//!< accelerator offset and matrix to Flash

 __OIS_CMD_HEADER__	UINT_8	WrLinCalData( UINT_8, mlLinearityValue * ) ;
 __OIS_CMD_HEADER__	UINT_8	WrLinMixCalData( UINT_8, mlMixingValue *, mlLinearityValue * ) ;
 __OIS_CMD_HEADER__	UINT_32	MeasGain ( UINT_16	, UINT_16	 , UINT_32  );
 __OIS_CMD_HEADER__	UINT_8	WrLinMix2ndCalData( UINT_8 , mlMixingValue * , mlLinearityValue * , stCrsPoint * );
 
 __OIS_FLSH_HEADER__	UINT_8	CalcSetLinMix2ndData( UINT_8, stPixelCoordinate * );
 __OIS_FLSH_HEADER__	UINT_8	RotationCorrectCalData( UINT_8 , double );
 __OIS_FLSH_HEADER__	UINT_8	AGGCorrectCalData( UINT_8 , stAGG * );

 __OIS_FLSH_HEADER__	UINT_8 WrOptCenerData( UINT_8 );
 
 __OIS_CMD_HEADER__	UINT_8 SetAngleCorrection( float , UINT_8 , UINT_8  );

 /* Flash Update */
 __OIS_FLSH_HEADER__	UINT_8	UnlockCodeSet( void ) ;					//!< <Flash Memory> Unlock Code Set
 __OIS_FLSH_HEADER__	UINT_8	UnlockCodeClear(void) ;					//!< <Flash Memory> Clear Unlock Code

 __OIS_FLSH_HEADER__	UINT_8	FlashBlockErase( UINT_8 , UINT_32 ) ;


 __OIS_FLSH_HEADER__	UINT_8	FlashUpdate128( DOWNLOAD_TBL_EXT *  );
//#ifdef	TRNT
 __OIS_FLSH_HEADER__	UINT_8	LoadUareToPM( DOWNLOAD_TBL_EXT * , UINT_8 );
//#endif
 __OIS_FLSH_HEADER__	UINT_8 Mat2ReWrite( void );

#endif	// __OIS_MODULE_CALIBRATION__

#endif /* #ifndef OISAPI_H_ */
