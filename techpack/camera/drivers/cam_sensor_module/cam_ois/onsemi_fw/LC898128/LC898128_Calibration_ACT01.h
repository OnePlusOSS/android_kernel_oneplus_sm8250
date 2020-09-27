//********************************************************************************
//		<< LC898128 Evaluation Soft>>
//		Program Name	: LC898128_Calibration_ACT01.h
// 		Explanation		: LC898128_L_ calibration parameters 
//		History			: First edition	
//********************************************************************************

// Version Name : 00-00-0000

// for	""

//********************************************************************************
// defines
//********************************************************************************
#define		X_BIAS			(0x40000000 )
//#define		Y_BIAS			(0x40000000 )
//#define		X_BIAS			(0x48000000 )   //20190815 Komori
#define		Y_BIAS			(0x38000000 )       //20190815 Komori
#define		X_OFST			(0x10000000 )	//
#define		Y_OFST			(0x10000000 )	//

#define 	MARGIN			(0x0300		)	// Margin
 
#define 	BIAS_ADJ_RANGE_X	(0x5999)			// 35%
//#define 	BIAS_ADJ_RANGE_X	(0x6666)			// 40%      //20190814 Komori
//#define 	BIAS_ADJ_RANGE_Y	(0x5999)			// 35%
//#define 	BIAS_ADJ_RANGE_Y	(0x51EB)			// 32%      //20190815 Komori
#define 	BIAS_ADJ_RANGE_Y	(0x4F5B)			// 31%
//#define 	BIAS_ADJ_RANGE_Y	(0x4CCC)			// 30%
//#define 	BIAS_ADJ_RANGE_Y	(0x4A3D)			// 29%
//#define 	BIAS_ADJ_RANGE_Y	(0x48F5)			// 28.5%
//#define 	BIAS_ADJ_RANGE_Y	(0x47AD)			// 28%
//#define 	BIAS_ADJ_RANGE_Y	(0x3FFF)			// 25%

#define		SINE_OFFSET		0x0008B8E5				// Freq Setting = Freq * 80000000h / Fs	: 4Hz
//#define		SINE_NUM		3756					// 15.027322/0.004 > num
#define		SINE_NUM		512						// 15.027322/0.004 > num
#define		SINE_GAIN_X		0x4D780000				// Set Sine Wave Gain  115mA (115mA*7fff/190mA)  190mA(min) 
#define		SINE_GAIN_Y		0x4D780000				// Set Sine Wave Gain  115mA (115mA*7fff/190mA)  190mA(min)

#define 	DECRE_CAL		(0x0100		)	// decrease value

//#define		ACT_MAX_DRIVE_X		0x33333333		// 80mA /200=0.4
//#define		ACT_MAX_DRIVE_Y		0x33333333		// 80mA /200=0.4
//#define		ACT_MIN_DRIVE_X		0xCCCCCCCC
//#define		ACT_MIN_DRIVE_Y		0xCCCCCCCC
#define		ACT_MAX_DRIVE_X		0x7FFFFFFF		// 200mA Max    //20190814 Komori
#define		ACT_MAX_DRIVE_Y		0x7FFFFFFF		// 200mA Max
#define		ACT_MIN_DRIVE_X		0x80000001
#define		ACT_MIN_DRIVE_Y		0x80000001

//#define		ACT_X_STEP_NUM				0x1F
#define		ACT_X_STEP_NUM				0
#define		ACT_X_STEP					(ACT_MAX_DRIVE_X/(ACT_X_STEP_NUM+1))
#define		ACT_X_STEP_TIME				2

//#define		ACT_Y_STEP_NUM				0x1F
#define		ACT_Y_STEP_NUM				0
#define		ACT_Y_STEP					(ACT_MAX_DRIVE_Y/(ACT_Y_STEP_NUM+1))
#define		ACT_Y_STEP_TIME				2

#define		MEASURE_WAIT				50

#define		SXGAIN_LOP		(0x38000000 )	// 0.437513
#define		SYGAIN_LOP		(0x38000000 )	// 0.437513
  
/******* X ******/
#define 	LOOP_NUM_HX		480			// 15.027322kHz/0.500kHz*16times
#define 	LOOP_FREQ_HX	0x044247E0		// 500Hz  = Freq * 80000000h / Fs
//#define 	LOOP_NUM_HX		2671			// 15.027322kHz/0.090kHz*16times
//#define 	LOOP_FREQ_HX	0x00C43F60		// 	90Hz  = Freq * 80000000h / Fs
//#define 	LOOP_NUM_HX		(8014/1)		// 15.027322kHz/0.030kHz* 8times
//#define 	LOOP_FREQ_HX	0x00416AAE		// 	30Hz  = Freq * 80000000h / Fs

//#define 	LOOP_GAIN_HX	0x040C3708		// -30dB
//#define 	LOOP_GAIN_HX	0x08137F40		// -24dB
#define 	LOOP_GAIN_HX	0x2026F340		// -12dB
//#define		GAIN_GAP_HX		(2000)			// 20*log(1000/1000)=-6dB
//#define		GAIN_GAP_HX		(1400)			// 20*log(1000/1000)=-3dB
#define		GAIN_GAP_HX		(1000)			// 20*log(1000/1000)=0dB
//#define		GAIN_GAP_HX		(250)			// 20*log(1000/250)=12dB

/******* Y ******/
#define 	LOOP_NUM_HY		480			// 15.027322kHz/0.500kHz*16times
#define 	LOOP_FREQ_HY	0x044247E0		// 500Hz  = Freq * 80000000h / Fs
//#define 	LOOP_NUM_HY		2671			// 15.027322kHz/0.090kHz*16times
//#define 	LOOP_FREQ_HY	0x00C43F60		// 	90Hz  = Freq * 80000000h / Fs
//#define 	LOOP_NUM_HY		(8014/1)		// 15.027322kHz/0.030kHz* 8times
//#define 	LOOP_FREQ_HY	0x00416AAE		// 	30Hz  = Freq * 80000000h / Fs

//#define 	LOOP_GAIN_HY	0x040C3708		// -30dB
//#define 	LOOP_GAIN_HY	0x08137F40		// -24dB
 #define 	LOOP_GAIN_HY	0x2026F340		// -12dB
//#define		GAIN_GAP_HY		(2000)			// 20*log(1000/1000)=-6dB
//#define		GAIN_GAP_HY		(1400)			// 20*log(1000/1000)=-3dB
#define		GAIN_GAP_HY		(1000)			// 20*log(1000/1000)=0dB
//#define		GAIN_GAP_HY		(250)			// 20*log(1000/250)=12dB

#define 	LOOP_MAX_X		(SXGAIN_LOP << 1)	// x2
#define 	LOOP_MIN_X		(SXGAIN_LOP >> 1)	// x0.5
#define 	LOOP_MAX_Y		(SYGAIN_LOP << 1)	// x2
#define 	LOOP_MIN_Y		(SYGAIN_LOP >> 1)	// x0.5

#define		SLT_XY_SWAP			0		// 0: pos  1: swap
#define		SLT_OFFSET_X	(0xFFFFF400)
#define		SLT_OFFSET_Y	(0xFFFFF400)	
#define		SLT_DRIVE_X		(1L)
#define		SLT_DRIVE_Y		(1L)

//********************************************************************************
// structure for calibration
//********************************************************************************
/* const*/ ADJ_HALL ACT01_HallCalParameter = { 
/* XBiasInit */		X_BIAS,
/* YBiasInit */		Y_BIAS,
/* XOffsetInit */	X_OFST,
/* YOffsetInit */	Y_OFST,
/* Margin */		MARGIN,
/* XTargetRange */	BIAS_ADJ_RANGE_X,
/* XTargetMax */	(BIAS_ADJ_RANGE_X + (MARGIN/2)),
/* XTargetMin */	(BIAS_ADJ_RANGE_X - (MARGIN/2)),
/* YTargetRange */	BIAS_ADJ_RANGE_Y,
/* YTargetMax */	(BIAS_ADJ_RANGE_Y + (MARGIN/2)),
/* YTargetMin */	(BIAS_ADJ_RANGE_Y - (MARGIN/2)),
/* SinNum */		SINE_NUM,			
/* SinFreq */		SINE_OFFSET,
/* XSinGain */		SINE_GAIN_X,
/* YSinGain */		SINE_GAIN_Y,
/* DecrementStep */ DECRE_CAL,
/* ActMaxDrive_X 	*/	ACT_MAX_DRIVE_X,
/* ActMaxDrive_Y	*/	ACT_MAX_DRIVE_Y,
/* ActMinDrive_X	*/	ACT_MIN_DRIVE_X,
/* ActMinDrive_Y	*/	ACT_MIN_DRIVE_Y,
/* ActStep_X	*/  ACT_X_STEP,
/* ActStep_X_Num*/  ACT_X_STEP_NUM,
/* ActStep_X_time*/ ACT_X_STEP_TIME,
/* ActStep_Y 	*/  ACT_Y_STEP,
/* ActStep_Y_Num*/  ACT_Y_STEP_NUM,
/* ActStep_Y_time*/ ACT_Y_STEP_TIME,
/* WaitTime*/  	MEASURE_WAIT,  
}; //   


const ADJ_LOPGAN ACT01_LoopGainParameter = { 
/* Hxgain */		SXGAIN_LOP,
/* Hygain */		SYGAIN_LOP,
/* XNoiseNum */		LOOP_NUM_HX,
/* XNoiseFreq */	LOOP_FREQ_HX,
/* XNoiseGain */	LOOP_GAIN_HX, 
/* XGap  */			GAIN_GAP_HX,
/* YNoiseNum */		LOOP_NUM_HY,
/* YNoiseFreq */	LOOP_FREQ_HY,
/* YNoiseGain */	LOOP_GAIN_HY, 
/* YGap  */			GAIN_GAP_HY,
/* XJudgeHigh */ 	LOOP_MAX_X,
/* XJudgeLow  */ 	LOOP_MIN_X,
/* YJudgeHigh */ 	LOOP_MAX_Y,
/* YJudgeLow  */ 	LOOP_MIN_Y,
}; //   

const LINCRS ACT01_LinCrsParameter = { 
/* XY_SWAP */		SLT_XY_SWAP,
/* STEPX */			SLT_OFFSET_X,
/* STEPY */			SLT_OFFSET_Y,
/* DRIVEX */		SLT_DRIVE_X,
/* DRIVEY */		SLT_DRIVE_Y,
}; //   



#undef	X_BIAS
#undef	Y_BIAS
#undef	X_OFST
#undef	Y_OFST
#undef 	MARGIN
#undef 	BIAS_ADJ_RANGE_X
#undef 	BIAS_ADJ_RANGE_Y
#undef	SINE_NUM
#undef	SINE_OFFSET
#undef	SINE_GAIN_X
#undef	SINE_GAIN_Y
#undef 	DECRE_CAL

#undef ACT_MAX_DRIVE_X
#undef ACT_MAX_DRIVE_Y
#undef ACT_MIN_DRIVE_X
#undef ACT_MIN_DRIVE_Y
#undef ACT_X_STEP
#undef ACT_X_STEP_NUM
#undef ACT_X_STEP_TIME
#undef ACT_Y_STEP
#undef ACT_Y_STEP_NUM
#undef ACT_Y_STEP_TIME
#undef MEASURE_WAIT

#undef	SXGAIN_LOP
#undef	SYGAIN_LOP
#undef 	LOOP_NUM_HX
#undef 	LOOP_FREQ_HX
#undef 	LOOP_GAIN_HX
#undef	GAIN_GAP_HX
#undef 	LOOP_NUM_HY
#undef 	LOOP_FREQ_HY
#undef 	LOOP_GAIN_HY
#undef	GAIN_GAP_HY
#undef 	LOOP_MAX_X
#undef 	LOOP_MIN_X
#undef 	LOOP_MAX_Y
#undef 	LOOP_MIN_Y


#undef	SLT_XY_SWAP
#undef	SLT_OFFSET_X
#undef	SLT_OFFSET_Y
#undef	SLT_DRIVE_X
#undef	SLT_DRIVE_Y

