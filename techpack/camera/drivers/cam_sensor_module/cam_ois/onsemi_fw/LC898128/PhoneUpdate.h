/**
 *		LC898128 Global declaration & prototype declaration
 *
 *		Copyright (C) 2017, ON Semiconductor, all right reserved.
 *
 **/
 
#ifndef PHONEUPDATE_H_
#define PHONEUPDATE_H_

#include <linux/types.h>
#include "cam_sensor_util.h"
#include "cam_debug_util.h"

//==============================================================================
//
//==============================================================================
#define	MODULE_VENDOR	1
#define	MDL_VER			2

#if 0
#ifdef DEBUG
 extern void dbg_printf(const char *, ...);
 extern void dbg_Dump(const char *, int);
 #define TRACE_INIT(x)			dbgu_init(x)
 #define TRACE_USB(fmt, ...)	dbg_UsbData(fmt, ## __VA_ARGS__)
 #define TRACE(fmt, ...)		dbg_printf(fmt, ## __VA_ARGS__)
 #define TRACE_DUMP(x,y)		dbg_Dump(x,y)
#else
 #define TRACE_INIT(x)
 #define TRACE(...)
 #define TRACE_DUMP(x,y)
 #define TRACE_USB(...)
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

#define		INT_8	int8_t//char
#define		INT_16	int16_t//short
#define		INT_32	int32_t//long
#define		INT_64	int64_t//long long
#define		UINT_8	uint8_t//unsigned char
#define		UINT_16	uint16_t//unsigned short
#define		UINT_32	uint32_t//unsigned long
#define		UINT_64	uint64_t//unsigned long long

//****************************************************
//	STRUCTURE DEFINE
//****************************************************
typedef struct {
	UINT_32				Index;
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

typedef struct {
	INT_32				SiSampleNum ;	
	INT_32				SiSampleMax ;	

	struct {
		INT_32			SiMax1 ;		
		INT_32			SiMin1 ;		
		UINT_32	UiAmp1 ;				
		INT_64		LLiIntegral1 ;		
		INT_64		LLiAbsInteg1 ;		
		INT_32			PiMeasureRam1 ;	
	} MeasureFilterA ;

	struct {
		INT_32			SiMax2 ;		
		INT_32			SiMin2 ;		
		UINT_32	UiAmp2 ;				
		INT_64		LLiIntegral2 ;		
		INT_64		LLiAbsInteg2 ;		
		INT_32			PiMeasureRam2 ;	
	} MeasureFilterB ;
} MeasureFunction_Type ;

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

#define		EXE_END		0x00000002L	
#define		EXE_GXADJ	0x00000042L	
#define		EXE_GYADJ	0x00000082L	
#define		EXE_GZADJ	0x00400002L	
#define		EXE_AZADJ	0x00200002L	
#define		EXE_AYADJ	0x00100002L	
#define		EXE_AXADJ	0x00080002L	
#define		EXE_HXMVER	0x06
#define		EXE_HYMVER	0x0A
#define		EXE_GXABOVE	0x06
#define		EXE_GXBELOW	0x0A
#define		EXE_GYABOVE	0x12
#define		EXE_GYBELOW	0x22

#define	SUCCESS			0x00
#define	FAILURE			0x01

#define	FT_REPRG				( 15 )
	#define	PRDCT_WR				0x55555555
	#define	USER_WR					0xAAAAAAAA
#define	MAT2_CKSM				( 29 )
#define	CHECKCODE1				( 30 )
	#define	CHECK_CODE1				0x99756768
#define	CHECKCODE2				( 31 )
	#define	CHECK_CODE2				0x01AC28AC
	
//==============================================================================
//
//==============================================================================
#define		CMD_IO_ADR_ACCESS				0xC000				//!< IO Write Access
#define		CMD_IO_DAT_ACCESS				0xD000				//!< IO Read Access
#define 	SYSDSP_DSPDIV					0xD00014
#define 	SYSDSP_SOFTRES					0xD0006C
#define 	SYSDSP_REMAP					0xD000AC
#define 	SYSDSP_CVER						0xD00100
#define		ROMINFO							0xE050D4
#define FLASHROM_128		0xE07000	// Flash Memory I/F配置アドレス
#define 		FLASHROM_FLA_RDAT					(FLASHROM_128 + 0x00)
#define 		FLASHROM_FLA_WDAT					(FLASHROM_128 + 0x04)
#define 		FLASHROM_ACSCNT						(FLASHROM_128 + 0x08)
#define 		FLASHROM_FLA_ADR					(FLASHROM_128 + 0x0C)
	#define			USER_MAT				0
	#define			INF_MAT0				1
	#define			INF_MAT1				2
	#define			INF_MAT2				4
#define 		FLASHROM_CMD						(FLASHROM_128 + 0x10)
#define 		FLASHROM_FLAWP						(FLASHROM_128 + 0x14)
#define 		FLASHROM_FLAINT						(FLASHROM_128 + 0x18)
#define 		FLASHROM_FLAMODE					(FLASHROM_128 + 0x1C)
#define 		FLASHROM_TPECPW						(FLASHROM_128 + 0x20)
#define 		FLASHROM_TACC						(FLASHROM_128 + 0x24)

#define 		FLASHROM_ERR_FLA					(FLASHROM_128 + 0x98)
#define 		FLASHROM_RSTB_FLA					(FLASHROM_128 + 0x4CC)
#define 		FLASHROM_UNLK_CODE1					(FLASHROM_128 + 0x554)
#define 		FLASHROM_CLK_FLAON					(FLASHROM_128 + 0x664)
#define 		FLASHROM_UNLK_CODE2					(FLASHROM_128 + 0xAA8)
#define 		FLASHROM_UNLK_CODE3					(FLASHROM_128 + 0xCCC)

#define		READ_STATUS_INI					0x01000000

#define			HallFilterD_HXDAZ1			0x0048
#define			HallFilterD_HYDAZ1			0x0098

#define			HALL_RAM_HXOFF              0x00D8
#define			HALL_RAM_HYOFF				0x0128
#define			HALL_RAM_HXOFF1				0x00DC
#define			HALL_RAM_HYOFF1				0x012C
#define			HALL_RAM_HXOUT0				0x00E0
#define			HALL_RAM_HYOUT0				0x0130
#define			HALL_RAM_SINDX1				0x00F0
#define			HALL_RAM_SINDY1				0x0140
#define			HALL_RAM_HALL_X_OUT			0x00F4
#define			HALL_RAM_HALL_Y_OUT			0x0144
#define			HALL_RAM_HXIDAT				0x0178
#define			HALL_RAM_HYIDAT				0x017C
#define			HALL_RAM_GYROX_OUT			0x0180
#define			HALL_RAM_GYROY_OUT			0x0184
#define			HallFilterCoeffX_hxgain0	0x80F0
#define			HallFilterCoeffY_hygain0	0x818C
#define			Gyro_Limiter_X				0x8330
#define			Gyro_Limiter_Y   	        0x8334
#define			GyroFilterTableX_gxzoom		0x82B8
#define			GyroFilterTableY_gyzoom		0x8318
#define			GyroFilterTableX_gxlenz		0x82BC
#define			GyroFilterTableY_gylenz		0x831C
#define			GyroFilterShiftX			0x8338
#define			GyroFilterShiftY			0x833C

#define			GYRO_RAM_GX_ADIDAT			0x0220
#define			GYRO_RAM_GY_ADIDAT			0x0224
#define			GYRO_RAM_GXOFFZ				0x0240
#define			GYRO_RAM_GYOFFZ				0x0244
#define			GYRO_ZRAM_GZ_ADIDAT			0x0394
#define			GYRO_ZRAM_GZOFFZ			0x03A0
#define			ACCLRAM_X_AC_ADIDAT			0x0448
#define			ACCLRAM_X_AC_OFFSET			0x044C
#define			ACCLRAM_Y_AC_ADIDAT			0x0474
#define			ACCLRAM_Y_AC_OFFSET			0x0478
#define			ACCLRAM_Z_AC_ADIDAT			0x04A0
#define			ACCLRAM_Z_AC_OFFSET			0x04A4

#define		OIS_POS_BY_AF_X					0x05A8
#define			OIS_POS_BY_AF_X1				(0x0000 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X2				(0x0004 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X3				(0x0008 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X4				(0x000C + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X5				(0x0010 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X6				(0x0014 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X7				(0x0018 + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X8				(0x001C + OIS_POS_BY_AF_X )
#define			OIS_POS_BY_AF_X9				(0x0020 + OIS_POS_BY_AF_X )

#define		OIS_POS_BY_AF_Y					0x05CC
#define			OIS_POS_BY_AF_Y1				(0x0000 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y2				(0x0004 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y3				(0x0008 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y4				(0x000C + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y5				(0x0010 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y6				(0x0014 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y7				(0x0018 + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y8				(0x001C + OIS_POS_BY_AF_Y )
#define			OIS_POS_BY_AF_Y9				(0x0020 + OIS_POS_BY_AF_Y )



/************************************************/
/*	Command										*/
/************************************************/
#define		CMD_IO_ADR_ACCESS				0xC000			
#define		CMD_IO_DAT_ACCESS				0xD000			
#define		CMD_RETURN_TO_CENTER			0xF010			
	#define		BOTH_SRV_OFF					0x00000000	
	#define		XAXS_SRV_ON						0x00000001	
	#define		YAXS_SRV_ON						0x00000002	
	#define		BOTH_SRV_ON						0x00000003	
#define		CMD_PAN_TILT					0xF011			
	#define		PAN_TILT_OFF					0x00000000	
	#define		PAN_TILT_ON						0x00000001	
#define		CMD_OIS_ENABLE					0xF012			
	#define		OIS_DISABLE						0x00000000	
	#define		OIS_ENABLE						0x00000001	
	#define		SMA_OIS_ENABLE					0x00010000	
	#define		BOTH_OIS_ENABLE					0x00010001	
	#define		OIS_ENABLE_LF					0x00000011	
	#define		SMA_OIS_ENABLE_LF				0x00010010	
	#define		BOTH_OIS_ENABLE_LF				0x00010011	
#define		CMD_MOVE_STILL_MODE				0xF013			
	#define		MOVIE_MODE						0x00000000	
	#define		STILL_MODE						0x00000001	
	#define		MOVIE_MODE1						0x00000002	
	#define		STILL_MODE1						0x00000003	
	#define		MOVIE_MODE2						0x00000004	
	#define		STILL_MODE2						0x00000005	
	#define		MOVIE_MODE3						0x00000006	
	#define		STILL_MODE3						0x00000007	
#define		CMD_GYROINITIALCOMMAND			0xF015			
	#define		SET_ICM20690					0x00000000
	#define		SET_LSM6DSM						0x00000002
	#define		SET_BMI260						0x00000006
#define		CMD_OSC_DETECTION				0xF017			
	#define		OSC_DTCT_DISABLE				0x00000000
	#define		OSC_DTCT_ENABLE					0x00000001
#define		CMD_SSC_ENABLE					0xF01C			
	#define		SSC_DISABLE						0x00000000	
	#define		SSC_ENABLE						0x00000001	
#define		CMD_GYRO_RD_ACCS				0xF01D			
#define		CMD_GYRO_WR_ACCS				0xF01E			
#define		CMD_SMA_CONTROL					0xF01F
	#define		SMA_STOP						0x00000000
	#define		SMA_START						0x00000001
#define		CMD_READ_STATUS					0xF100			
#define			READ_STATUS_INI					0x01000000

#define		CNT050MS		 676
#define		CNT100MS		1352
#define		CNT200MS		2703

//==============================================================================
// Prototype
//==============================================================================
//extern UINT_8	FlashDownload128( UINT_8 , UINT_8, UINT_8  );

extern UINT_8	SetAngleCorrection( float , UINT_8 , UINT_8  );
extern UINT_8	UnlockCodeSet( void );
extern UINT_8	UnlockCodeClear(void);
extern UINT_32	MeasGyAcOffset(  void  );
extern void		SetGyroOffset( UINT_16 GyroOffsetX, UINT_16 GyroOffsetY, UINT_16 GyroOffsetZ );
extern void		SetAcclOffset( UINT_16 AcclOffsetX, UINT_16 AcclOffsetY, UINT_16 AcclOffsetZ );
extern void		GetGyroOffset( UINT_16* GyroOffsetX, UINT_16* GyroOffsetY, UINT_16* GyroOffsetZ );
extern void		GetAcclOffset( UINT_16* AcclOffsetX, UINT_16* AcclOffsetY, UINT_16* AcclOffsetZ );

extern UINT_8	RdStatus( UINT_8 UcStBitChk );
extern void		OisEna( void );
extern void		OisDis( void );
extern void		OisDis_Slope( void );
extern void		OisEna_S( void );
extern void		OisEna_SV( void );
extern void		SetPanTiltMode( UINT_8 UcPnTmod );
extern void		SscEna( void );
extern void		SscDis( void );

extern UINT_8	RunHea( void );
extern UINT_8	RunGea( void );

extern UINT_32	FW_info[][3];

extern void		PreparationForPowerOff( void );
extern void		VcmStandby( void );
extern void		VcmActive( void );
extern void		SrvOn( void );
extern void		SrvOff( void );
extern void		SetStandbyMode( void );
extern void		SetActiveMode( void );

extern UINT_8	LoadUserAreaToPM( void );
extern UINT_8	LoadUareToPM( DOWNLOAD_TBL_EXT* ptr , UINT_8 mode );
extern UINT_8	RdBurstUareaFromPm( UINT_32 UlAddress, UINT_8 *PucData , UINT_8 UcLength , UINT_8 mode );
extern UINT_8	RdSingleUareaFromPm( UINT_32 UlAddress, UINT_8 *PucData , UINT_8 UcLength , UINT_8 mode );

#endif /* #ifndef OIS_H_ */
