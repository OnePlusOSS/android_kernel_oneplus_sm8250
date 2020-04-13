/**
 * @brief		OIS system common header for LC898128
 * 				Defines, Structures, function prototypes
 *
 * @author		Copyright (C) 2016, ON Semiconductor, all right reserved.
 *
 * @file		Ois.h
 * @date		svn:$Date:: 2016-06-22 10:57:58 +0900#$
 * @version	svn:$Revision: 59 $
 * @attention
 **/
#ifndef OIS_H_
#define OIS_H_

#include <linux/types.h>
#include "cam_sensor_util.h"
#include "cam_debug_util.h"

#define		INT_8	int8_t//char
#define		INT_16	int16_t//short
#define		INT_32	int32_t//long
#define		INT_64	int64_t//long long
#define		UINT_8	uint8_t//unsigned char
#define		UINT_16	uint16_t//unsigned short
#define		UINT_32	uint32_t//unsigned long
#define		UINT_64	uint64_t//unsigned long long


//#define		_BIG_ENDIAN_

#include	"OisAPI.h"
#include	"OisLc898128.h"

#if 0

#ifdef DEBUG
#include <AT91SAM7S.h>
#include <us.h>
 #ifndef	_CMD_H_
 extern void dbg_printf(const char *, ...);
 extern void dbg_Dump(const char *, int);
 #endif
 #define TRACE(fmt, ...)		dbg_printf(fmt, ## __VA_ARGS__)
 #define TRACE_DUMP(x,y)		dbg_Dump(x,y)
#else
 #define TRACE(...)
 #define TRACE_DUMP(x,y)
#endif

#else

#define DEBUG 1
#ifdef DEBUG
 #define TRACE(fmt, ...)		CAM_ERR(CAM_OIS, fmt, ## __VA_ARGS__)
 #define TRACE_DUMP(x,y)
#else
 #define TRACE(...)
 #define TRACE_DUMP(x,y)
#endif

#endif

//#define	TRNT
//#define	WED
/**************** Model name *****************/
#define	SELECT_VENDOR		0x00	// --- select vender ---//
									// 0bit : 
									// 1bit : 
									// 2bit : 
									// 3bit : 
									// 4bit : 
/**************** FW version *****************/
 #define	FW_VER			0x02
 #define	SUB_VER			0x00			// ATMEL SUB Version

/**************** Select Mode **************/
#define		MODULE_VENDOR	0

//#define		NEUTRAL_CENTER				//!< Upper Position Current 0mA Measurement
//#define		NEUTRAL_CENTER_FINE			//!< Optimize natural center current
#define		SEL_SHIFT_COR				//!< Shift correction
#define		__OIS_UIOIS_GYRO_USE__
//#define		ACT02_AMP_NARROW
/**************** Filter sampling **************/
#define		FS_MODE		1		// 0 : originally
								// 1 : SLOW
#if FS_MODE == 0
#define	FS_FREQ			18044.61942F
#else
#define	FS_FREQ			15027.3224F
#endif

#define	GYRO_SENSITIVITY	65.5		//!< Gyro sensitivity LSB/dps

// Command Status
#define		EXE_END		0x00000002L		//!< Execute End (Adjust OK)
#define		EXE_ERR		0x00000003L		//!< Adjust NG : Execution Failure 
#define		EXE_HXADJ	0x00000006L		//!< Adjust NG : X Hall NG (Gain or Offset)
#define		EXE_HYADJ	0x0000000AL		//!< Adjust NG : Y Hall NG (Gain or Offset)
#define		EXE_LXADJ	0x00000012L		//!< Adjust NG : X Loop NG (Gain)
#define		EXE_LYADJ	0x00000022L		//!< Adjust NG : Y Loop NG (Gain)
#define		EXE_GXADJ	0x00000042L		//!< Adjust NG : X Gyro NG (offset)
#define		EXE_GYADJ	0x00000082L		//!< Adjust NG : Y Gyro NG (offset)
#ifdef	SEL_SHIFT_COR
#define		EXE_GZADJ	0x00400002L		//!< Adjust NG : Z Gyro NG (offset)
#define		EXE_AZADJ	0x00200002L		// Adjust NG : Z ACCL NG (offset)
#define		EXE_AYADJ	0x00100002L		// Adjust NG : Y ACCL NG (offset)
#define		EXE_AXADJ	0x00080002L		// Adjust NG : X ACCL NG (offset)
#define		EXE_XSTRK	0x00040002L		// CONFIRM NG : X (offset)
#define		EXE_YSTRK	0x00020002L		// CONFIRM NG : Y (offset)
#endif	//SEL_SHIFT_COR
#define		EXE_HXMVER	0x06
#define		EXE_HYMVER	0x0A
#define		EXE_GXABOVE	0x06
#define		EXE_GXBELOW	0x0A
#define		EXE_GYABOVE	0x12
#define		EXE_GYBELOW	0x22


// Common Define
#define	SUCCESS			0x00			//!< Success
#define	FAILURE			0x01			//!< Failure

#ifndef ON
 #define	ON				0x01		//!< ON
 #define	OFF				0x00		//!< OFF
#endif
 #define	SPC				0x02		//!< Special Mode

#define	X_DIR			0x00			//!< X Direction
#define	Y_DIR			0x01			//!< Y Direction
#define	Z_DIR			0x02			//!< Z Direction(AF)

struct STFILREG {						//!< Register data table
	UINT_16	UsRegAdd ;
	UINT_8	UcRegDat ;
} ;

struct STFILRAM {						//!< Filter coefficient table
	UINT_16	UsRamAdd ;
	UINT_32	UlRamDat ;
} ;

struct STCMDTBL {						//!< Command table
	UINT_16 Cmd ;
	UINT_32 UiCmdStf ;
	void ( *UcCmdPtr )( void ) ;
} ;

/************************************************/
/*	Command										*/
/************************************************/
#define		CMD_IO_ADR_ACCESS				0xC000				//!< IO Write Access
#define		CMD_IO_DAT_ACCESS				0xD000				//!< IO Read Access
#define		CMD_RETURN_TO_CENTER			0xF010				//!< Center Servo ON/OFF choose axis
	#define		BOTH_SRV_OFF					0x00000000			//!< Both   Servo OFF
	#define		XAXS_SRV_ON						0x00000001			//!< X axis Servo ON
	#define		YAXS_SRV_ON						0x00000002			//!< Y axis Servo ON
	#define		BOTH_SRV_ON						0x00000003			//!< Both   Servo ON
	#define		ZAXS_SRV_OFF					0x00000004			//!< Z axis Servo OFF
	#define		ZAXS_SRV_ON						0x00000005			//!< Z axis Servo ON
#define		CMD_PAN_TILT					0xF011				//!< Pan Tilt Enable/Disable
	#define		PAN_TILT_OFF					0x00000000			//!< Pan/Tilt OFF
	#define		PAN_TILT_ON						0x00000001			//!< Pan/Tilt ON
#define		CMD_OIS_ENABLE					0xF012				//!< Ois Enable/Disable
	#define		OIS_DISABLE						0x00000000			//!< OIS Disable
	#define		OIS_DIS_PUS						0x00000008			//!< OIS Disable ( pasue calcuration value )
	#define		OIS_ENABLE						0x00000001			//!< OIS Enable
	#define		OIS_ENA_NCL						0x00000002			//!< OIS Enable ( none Delay clear )
	#define		OIS_ENA_DOF						0x00000004			//!< OIS Enable ( Drift offset exec )
#define		CMD_MOVE_STILL_MODE				0xF013				//!< Select mode
	#define		MOVIE_MODE						0x00000000			//!< Movie mode
	#define		STILL_MODE						0x00000001			//!< Still mode
	#define		MOVIE_MODE1						0x00000002			//!< Movie Preview mode 1
	#define		STILL_MODE1						0x00000003			//!< Still Preview mode 1
	#define		MOVIE_MODE2						0x00000004			//!< Movie Preview mode 2
	#define		STILL_MODE2						0x00000005			//!< Still Preview mode 2
	#define		MOVIE_MODE3						0x00000006			//!< Movie Preview mode 3
	#define		STILL_MODE3						0x00000007			//!< Still Preview mode 3
#define		CMD_CALIBRATION					0xF014				//!< Gyro offset re-calibration
#define		CMD_GYROINITIALCOMMAND			0xF015				//!< Select gyro sensor
#define		CMD_STANDBY_ENABLE				0xF019
	#define		ACTIVE_MODE						0x00000000			//!< Active mode
	#define		STANDBY_MODE					0x00000001			//!< Standby mode
#define		CMD_AF_POSITION					0xF01A				// AF Position
#define		CMD_SSC_ENABLE					0xF01C				//!< Select mode
	#define		SSC_DISABLE						0x00000000			//!< Ssc Disable
	#define		SSC_ENABLE						0x00000001			//!< Ssc Enable

#define		CMD_READ_STATUS					0xF100				//!< Status Read

#define		READ_STATUS_INI					0x01000000

#define		STBOSCPLL						0x00D00074			//!< STB OSC
	#define		OSC_STB							0x00000002			//!< OSC standby

// Calibration.h *******************************************************************
#define	HLXO				0x00000001			//!< D/A Converter Channel Select OIS X Offset
#define	HLYO				0x00000002			//!< D/A Converter Channel Select OIS Y Offset
#define	HLXBO				0x00000008			//!< D/A Converter Channel Select OIS X BIAS
#define	HLYBO				0x00000010			//!< D/A Converter Channel Select OIS Y BIAS

// MeasureFilter.h *******************************************************************
typedef struct {
	INT_32				SiSampleNum ;			//!< Measure Sample Number
	INT_32				SiSampleMax ;			//!< Measure Sample Number Max

	struct {
		INT_32			SiMax1 ;				//!< Max Measure Result
		INT_32			SiMin1 ;				//!< Min Measure Result
		UINT_32	UiAmp1 ;						//!< Amplitude Measure Result
		INT_64		LLiIntegral1 ;				//!< Integration Measure Result
		INT_64		LLiAbsInteg1 ;				//!< Absolute Integration Measure Result
		INT_32			PiMeasureRam1 ;			//!< Measure Delay RAM Address
	} MeasureFilterA ;

	struct {
		INT_32			SiMax2 ;				//!< Max Measure Result
		INT_32			SiMin2 ;				//!< Min Measure Result
		UINT_32	UiAmp2 ;						//!< Amplitude Measure Result
		INT_64		LLiIntegral2 ;				//!< Integration Measure Result
		INT_64		LLiAbsInteg2 ;				//!< Absolute Integration Measure Result
		INT_32			PiMeasureRam2 ;			//!< Measure Delay RAM Address
	} MeasureFilterB ;
} MeasureFunction_Type ;


/*** caution [little-endian] ***/

#ifdef _BIG_ENDIAN_
// Big endian
// Word Data Union
union	WRDVAL{
	INT_16	SsWrdVal ;
	UINT_16	UsWrdVal ;
	UINT_8	UcWrkVal[ 2 ] ;
	INT_8	ScWrkVal[ 2 ] ;
	struct {
		UINT_8	UcHigVal ;
		UINT_8	UcLowVal ;
	} StWrdVal ;
} ;


union	DWDVAL {
	UINT_32	UlDwdVal ;
	UINT_16	UsDwdVal[ 2 ] ;
	struct {
		UINT_16	UsHigVal ;
		UINT_16	UsLowVal ;
	} StDwdVal ;
	struct {
		UINT_8	UcRamVa3 ;
		UINT_8	UcRamVa2 ;
		UINT_8	UcRamVa1 ;
		UINT_8	UcRamVa0 ;
	} StCdwVal ;
} ;

union	ULLNVAL {
	UINT_64	UllnValue ;
	UINT_32	UlnValue[ 2 ] ;
	struct {
		UINT_32	UlHigVal ;
		UINT_32	UlLowVal ;
	} StUllnVal ;
} ;


// Float Data Union
union	FLTVAL {
	float	SfFltVal ;
	UINT_32	UlLngVal ;
	UINT_16	UsDwdVal[ 2 ] ;
	struct {
		UINT_16	UsHigVal ;
		UINT_16	UsLowVal ;
	} StFltVal ;
} ;

#else	// BIG_ENDDIAN
// Little endian
// Word Data Union
union	WRDVAL{
	UINT_16	UsWrdVal ;
	UINT_8	UcWrkVal[ 2 ] ;
	struct {
		UINT_8	UcLowVal ;
		UINT_8	UcHigVal ;
	} StWrdVal ;
} ;

typedef union WRDVAL	UnWrdVal ;

union	DWDVAL {
	UINT_32	UlDwdVal ;
	UINT_16	UsDwdVal[ 2 ] ;
	struct {
		UINT_16	UsLowVal ;
		UINT_16	UsHigVal ;
	} StDwdVal ;
	struct {
		UINT_8	UcRamVa0 ;
		UINT_8	UcRamVa1 ;
		UINT_8	UcRamVa2 ;
		UINT_8	UcRamVa3 ;
	} StCdwVal ;
} ;

typedef union DWDVAL	UnDwdVal;

union	ULLNVAL {
	UINT_64	UllnValue ;
	UINT_32	UlnValue[ 2 ] ;
	struct {
		UINT_32	UlLowVal ;
		UINT_32	UlHigVal ;
	} StUllnVal ;
} ;

typedef union ULLNVAL	UnllnVal;


// Float Data Union
union	FLTVAL {
	float	SfFltVal ;
	UINT_32	UlLngVal ;
	UINT_16	UsDwdVal[ 2 ] ;
	struct {
		UINT_16	UsLowVal ;
		UINT_16	UsHigVal ;
	} StFltVal ;
} ;

#endif	// _BIG_ENDIAN_
/*
typedef union WRDVAL	UnWrdVal ;
typedef union DWDVAL	UnDwdVal;
typedef union ULLNVAL	UnllnVal;
*/
typedef union FLTVAL	UnFltVal ;


typedef struct STADJPAR {
	struct {
		UINT_32	UlAdjPhs ;				//!< Hall Adjust Phase

		UINT_16	UsHlxCna ;				//!< Hall Center Value after Hall Adjust
		UINT_16	UsHlxMax ;				//!< Hall Max Value
		UINT_16	UsHlxMxa ;				//!< Hall Max Value after Hall Adjust
		UINT_16	UsHlxMin ;				//!< Hall Min Value
		UINT_16	UsHlxMna ;				//!< Hall Min Value after Hall Adjust
		UINT_16	UsHlxGan ;				//!< Hall Gain Value
		UINT_16	UsHlxOff ;				//!< Hall Offset Value
		UINT_16	UsAdxOff ;				//!< Hall A/D Offset Value
		UINT_16	UsHlxCen ;				//!< Hall Center Value

		UINT_16	UsHlyCna ;				//!< Hall Center Value after Hall Adjust
		UINT_16	UsHlyMax ;				//!< Hall Max Value
		UINT_16	UsHlyMxa ;				//!< Hall Max Value after Hall Adjust
		UINT_16	UsHlyMin ;				//!< Hall Min Value
		UINT_16	UsHlyMna ;				//!< Hall Min Value after Hall Adjust
		UINT_16	UsHlyGan ;				//!< Hall Gain Value
		UINT_16	UsHlyOff ;				//!< Hall Offset Value
		UINT_16	UsAdyOff ;				//!< Hall A/D Offset Value
		UINT_16	UsHlyCen ;				//!< Hall Center Value

	} StHalAdj ;

	struct {
		UINT_32	UlLxgVal ;				//!< Loop Gain X
		UINT_32	UlLygVal ;				//!< Loop Gain Y
	} StLopGan ;

	struct {
		UINT_16	UsGxoVal ;				//!< Gyro A/D Offset X
		UINT_16	UsGyoVal ;				//!< Gyro A/D Offset Y
		UINT_16	UsGzoVal ;				//!< Gyro A/D Offset Z
	} StGvcOff ;
} stAdjPar ;

__OIS_CMD_HEADER__	stAdjPar	StAdjPar ;		//!< Calibration data

typedef struct STHALLINEAR {
	UINT_16	XCoefA[6] ;
	UINT_16	XCoefB[6] ;
	UINT_16	XZone[5] ;
	UINT_16	YCoefA[6] ;
	UINT_16	YCoefB[6] ;
	UINT_16	YZone[5] ;
} stHalLinear ;

typedef struct STPOSOFF {
	struct {
		INT_32	Pos[6][3];
	} StPos;
	UINT_32		UlAclOfSt ;				//!< accel offset status

} stPosOff ;

__OIS_CMD_HEADER__	stPosOff	StPosOff ;				//!< Execute Command Parameter

typedef struct STACLVAL {
	struct {
		INT_32	SlOffsetX ;
		INT_32	SlOffsetY ;
		INT_32	SlOffsetZ ;
	} StAccel ;

	INT_32	SlInvMatrix[9] ;

} stAclVal ;

__OIS_CMD_HEADER__	stAclVal	StAclVal ;				//!< Execute Command Parameter


//	for RtnCen
#define		BOTH_ON			0x00
#define		XONLY_ON		0x01
#define		YONLY_ON		0x02
#define		BOTH_OFF		0x03
#define		ZONLY_OFF		0x04
#define		ZONLY_ON		0x05
//	for SetSinWavePara
#define		SINEWAVE		0
#define		XHALWAVE		1
#define		YHALWAVE		2
#define		ZHALWAVE		3
#define		XACTTEST		10
#define		YACTTEST		11
#define		CIRCWAVE		255
//	for TnePtp
#define		HALL_H_VAL		0x3F800000		//!< 1.0
//	for TneCen
#define		OFFDAC_8BIT		0				//!< 8bit Offset DAC select
#define		OFFDAC_3BIT		1				//!< 3bit Offset DAC select
#define		PTP_BEFORE		0
#define		PTP_AFTER		1
#define		PTP_ACCEPT		2
//	for RunHea
#define		ACT_CHK_FRQ		0x0008B8E5	// 4Hz	
#define		ACT_CHK_NUM		3756		
#define		ACT_THR			0x000003E8	
#define		ACT_MARGIN		0.75f		
//	for RunGea
#define		GEA_NUM			512				
#define		GEA_DIF_HIG		0x0083			
#define		GEA_DIF_LOW		0x0001			
 
// for RunGea2
// level of judgement
#define		GEA_MAX_LVL		0x0A41			//!< 2030_87.5lsb/‹/s    max 30‹/s-p-p
#define		GEA_MIN_LVL		0x1482			//!< 2030_87.5lsb/‹/s    min 60‹/s-p-p
// mode
#define		GEA_MINMAX_MODE	0x00			//!< min, max mode
#define		GEA_MEAN_MODE	0x01			//!< mean mode


// for Accelerometer offset measurement
#ifdef	SEL_SHIFT_COR

//100mG‚Æ‚·‚é
//#define		ZEROG_MRGN_Z	(409 << 16)			// G tolerance for Z
//#define		ZEROG_MRGN_XY	(409 << 16)			// G tolerance for XY
// XY 250mG , Z 320mG from Huawei
#define		ZEROG_MRGN_Z	(1310 << 16)			// G tolerance for Z
#define		ZEROG_MRGN_XY	(1024 << 16)			// G tolerance for XY

#define		ACCL_SENS		4096
#endif	//SEL_SHIFT_COR

#endif /* #ifndef OIS_H_ */
