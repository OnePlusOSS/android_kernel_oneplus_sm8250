/*=======================================================================
		Accuracy Test Sample code for LC898128
========================================================================*/
#define		__OISACCURACY__

//#include	"math.h"
#include <linux/kernel.h>
#include	"OisLc898128.h"
#include	"Ois.h"
#include	"Accuracy.h"

//****************************************************
//	CUSTOMER NECESSARY CREATING LIST
//****************************************************
/* for I2C communication */ 
extern	void RamWrite32A(int addr, int data);
extern 	void RamRead32A( unsigned short addr, void * data );
extern void	WitTim( unsigned short	UsWitTim );
extern UINT_8	FlashMultiRead( UINT_8 , UINT_32 , UINT_32 * , UINT_8 );

/* Raw data buffers */	
//Dual_Axis_t xy_raw_data[360/DEGSTEP + 1];
//Dual_Axis_t xy_raw_data[360/3 + 1];
//float xMaxAcc, yMaxAcc;
//float xLimit, yLimit;
#if 0
#define		ANGLE_LIMIT	0.105929591F
#define		LIMIT_RANGE	190.0F
#else
#define		ANGLE_LIMIT	0.091805644F
#define		LIMIT_RANGE	170.0F
#define		ANGLE_LIMIT_SMA	0.098867618F
#endif

// Checking radius
#define		DEGREE		0.65F						// 0.65 degree
//#define		ACCURACY	0.02F						// Accuracy (2.6% of LMTDEG) 100um * 2% = 2um
#define		ACCURACY	0.015F						// Accuracy  0.75deg(100um)/50 = 2um

// Parameter define
#define		DEGSTEP		3							// Degree of one step (3°)
#define		WAIT_MSEC	10							// Each step wait time(msec)
#define		LOOPTIME	3							// Read times at each step

// Loop Gain Up
#define		LPGSET		1.40						// 1.40(+3dB); 0.158(+4dB); 0.177(+5dB)

// Constants
#define		PI			3.14159						// π
#define		LMTDEG		0.75F						// Limit degree by LGYROLMT

#define		HallX_hs			0x81F8
#define		HallY_hs			0x8200

static float fix2float(unsigned int fix)
{
    if((fix & 0x80000000) > 0)
    {
        return ((float)fix-(float)0x100000000)/(float)0x7FFFFFFF;
    } else {
        return (float)fix/(float)0x7FFFFFFF;
    }
}

static unsigned int float2fix(float f)
{
    if(f < 0)
    {
        return (unsigned int)(f * (float)0x7FFFFFFF + 0x100000000);
    } else {
        return (unsigned int)(f * (float)0x7FFFFFFF);
    }
}

void LoopGainSet(unsigned char flag, float db)
{
    static UINT_32 xHs, yHs;
    static UINT_32 xLpGan, yLpGan;

	//Gain Change
    if(flag)  // 1:Gain Up
    {
        //Get Hs
	    RamRead32A(HallX_hs, &xHs);
	    RamRead32A(HallY_hs, &yHs);

		RamWrite32A(HallX_hs, (xHs & 0xFF000000) + 0x01000000 + (xHs & 0x00FFFFFF));   // Shift 2 bits
    	RamWrite32A(HallY_hs, (yHs & 0x0000FF00) + 0x00000100 + (yHs & 0xFFFF00FF));   // Shift 2 bits

	    //Get LoopGain1
	    RamRead32A(HallFilterCoeffX_hxgain1, &xLpGan);
	    RamRead32A(HallFilterCoeffY_hygain1, &yLpGan);

		RamWrite32A(HallFilterCoeffX_hxgain1, float2fix( fix2float(xLpGan) * db / 2));	// gain1 /2
    	RamWrite32A(HallFilterCoeffY_hygain1, float2fix( fix2float(xLpGan) * db / 2));  // gain1 /2

    } else { //	0:Restore

        // Restore Hs
        RamWrite32A(HallX_hs, xHs);
    	RamWrite32A(HallY_hs, yHs);
		
		//Restore LoopGain1
		RamWrite32A(HallFilterCoeffX_hxgain1, xLpGan);
	    RamWrite32A(HallFilterCoeffY_hygain1, yLpGan); 
	}
}

/*-------------------------------------------------------------------
	Function Name: Accuracy
	Param:	none
	Return:	value = 0 (no NG point)
			value > 0 (High byte: X total NG points;
					   Low byte: Y total NG points)	
--------------------------------------------------------------------*/
//unsigned short Accuracy()
#if 0
unsigned short Accuracy(float ACCURACY, unsigned short RADIUS, unsigned short DEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3)
{
	float xpos, ypos;
	unsigned int xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    unsigned short xng = 0, yng = 0;
    unsigned short deg;
    float xRadius, yRadius;
	unsigned int xGyrogain, yGyrogain;
    unsigned int xGLenz, yGLenz;
    unsigned int xG2x4xb, yG2x4xb;
    unsigned int xGoutG, yGoutG;

	// Get Gyro gain
	RamRead32A(GyroFilterTableX_gxzoom, &xGyrogain);
	RamRead32A(GyroFilterTableY_gyzoom, &yGyrogain);

	// Get Lenz
	RamRead32A(GyroFilterTableX_gxlenz, &xGLenz);
	RamRead32A(GyroFilterTableY_gylenz, &yGLenz);

	// Get Shift
	RamRead32A(GyroFilterShiftX, &xG2x4xb);
	RamRead32A(GyroFilterShiftY, &yG2x4xb);
	
	// Get Gyro out gain
	RamRead32A(HallFilterCoeffX_hxgoutg, &xGoutG);
	RamRead32A(HallFilterCoeffY_hygoutg, &yGoutG);

	// Calculate Radius (LIMIT_RANGE) /* 不明 */
//	xRadius = ANGLE_LIMIT * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * 8 * fabsf(fix2float(xGoutG));
//	yRadius = ANGLE_LIMIT * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * 8 * fabsf(fix2float(yGoutG));
	xRadius = ANGLE_LIMIT * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * (1 << (unsigned char)( xG2x4xb >> 8 )) * fabsf(fix2float(xGoutG));
	yRadius = ANGLE_LIMIT * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * (1 << (unsigned char)( yG2x4xb >> 8 )) * fabsf(fix2float(yGoutG));

	// Calculate Limit
	xLimit = ACCURACY / LIMIT_RANGE * xRadius;
    yLimit = ACCURACY / LIMIT_RANGE * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * RADIUS / LIMIT_RANGE;
	yRadius = yRadius * RADIUS / LIMIT_RANGE;

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
	xpos = xRadius * cos(0);
	ypos = yRadius * sin(0);
	RamWrite32A(HALL_RAM_HXOFF1, float2fix(xpos));
	RamWrite32A(HALL_RAM_HYOFF1, float2fix(ypos));
	WitTim(WAIT_MSEC1);

	for( deg = 0; deg <= 360; deg += DEGSTEP ) // 0-360 degree
	{
		xpos = xRadius * cos(deg * PI/180);
		ypos = yRadius * sin(deg * PI/180);
    	RamWrite32A(HALL_RAM_HXOFF1, float2fix(xpos));
		RamWrite32A(HALL_RAM_HYOFF1, float2fix(ypos));

		xMaxHall = 0;
		yMaxHall = 0;
		WitTim(WAIT_MSEC2);
		
		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim(WAIT_MSEC3);
			RamRead32A( HALL_RAM_HXOUT0, &xhall_value );
			RamRead32A( HALL_RAM_HYOUT0, &yhall_value );
			if(fabsf(fix2float(xhall_value) - xpos) > fabsf(xMaxHall))	
				xMaxHall = fix2float(xhall_value) - xpos;
			if(fabsf(fix2float(yhall_value) - ypos) > fabsf(yMaxHall))	
				yMaxHall = fix2float(yhall_value) - ypos;
		}

		if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
		if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
		
        // Save raw data
		xy_raw_data[deg/DEGSTEP].xpos = xpos;
		xy_raw_data[deg/DEGSTEP].xhall = xMaxHall + xpos;
		xy_raw_data[deg/DEGSTEP].ypos = ypos;
		xy_raw_data[deg/DEGSTEP].yhall = yMaxHall + ypos;
		
		if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 

	}
	RamWrite32A(HALL_RAM_HXOFF1, 0); // x = center
	RamWrite32A(HALL_RAM_HYOFF1, 0); // y = center

	return (xng << 8) | yng;
}
#endif
unsigned short Accuracy()
{
	float xpos, ypos;
	UINT_32 xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    unsigned short xng = 0, yng = 0;
    unsigned short deg;
    float xRadius, yRadius;
    UINT_32 xGyroLimit, yGyroLimit;
	UINT_32 xGyrogain, yGyrogain;
    UINT_32 xGLenz, yGLenz;
    UINT_32 xShiftRG, yShiftRG;
    UINT_32 xHav2, yHav2;


    // Get GYROLIMT
    RamRead32A(Gyro_Limiter_X, &xGyroLimit);
	RamRead32A(Gyro_Limiter_Y, &yGyroLimit);
	
	// Get Gyro gain
	RamRead32A(GyroFilterTableX_gxzoom, &xGyrogain);
	RamRead32A(GyroFilterTableY_gyzoom, &yGyrogain);

	// Get Lenz
	RamRead32A(GyroFilterTableX_gxlenz, &xGLenz);
	RamRead32A(GyroFilterTableY_gylenz, &yGLenz);

	// Get ShiftRG
   	RamRead32A(GyroFilterShiftX, &xShiftRG);
	RamRead32A(GyroFilterShiftY, &yShiftRG);
	xShiftRG = 1 << ((xShiftRG & 0xFF00) >> 8);
    yShiftRG = 1 << ((yShiftRG & 0xFF00) >> 8);

	// Calculate moving Range
	xRadius = fabsf(fix2float(xGyroLimit)) * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * xShiftRG ;
	yRadius = fabsf(fix2float(yGyroLimit)) * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * yShiftRG ;

	// Calculate Limit
	xLimit = ACCURACY * xRadius;
    yLimit = ACCURACY * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * DEGREE / LMTDEG;
	yRadius = yRadius * DEGREE / LMTDEG;	    

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
//	xpos = xRadius * cos(0);
//	ypos = yRadius * sin(0);
	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
	RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));
	WitTim(100);

	for( deg = 0; deg <= 360; deg += DEGSTEP ) // 0-360 degree
	{
//		xpos = xRadius * cos(deg * PI/180);
//		ypos = yRadius * sin(deg * PI/180);
    	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
		RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));

		xMaxHall = 0;
		yMaxHall = 0;

		RamRead32A( XMoveAvg_D2, &xHav2 );
		RamRead32A( YMoveAvg_D2, &yHav2 );

		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim( WAIT_MSEC );
			RamRead32A( HALL_RAM_HXOUT2, &xhall_value );
			RamRead32A( HALL_RAM_HYOUT2, &yhall_value );
			if(fabsf(fix2float(xhall_value) + fix2float(xHav2)) > fabsf(xMaxHall))	
				xMaxHall = fix2float(xhall_value) + fix2float(xHav2);
			if(fabsf(fix2float(yhall_value) + fix2float(yHav2)) > fabsf(yMaxHall))
				yMaxHall = fix2float(yhall_value) + fix2float(yHav2);
		}

		if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
		if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
		
        // Save raw data
		xy_raw_data[deg/DEGSTEP].xpos = fix2float(xHav2);
		xy_raw_data[deg/DEGSTEP].xhall = xMaxHall + fix2float(xHav2);
		xy_raw_data[deg/DEGSTEP].ypos = fix2float(yHav2);
		xy_raw_data[deg/DEGSTEP].yhall = yMaxHall + fix2float(yHav2);
		
		if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 

	}
	RamWrite32A(HALL_RAM_GYROX_OUT, 0); // x = center
	RamWrite32A(HALL_RAM_GYROY_OUT, 0); // y = center

TRACE(" xGyroLimit=%08x, xGyrogain=%08x, xGLenz=%08x, xShiftRG=%08x, xLimit=%f\n", xGyroLimit , xGyrogain , xGLenz , xShiftRG , xLimit);
TRACE(" yGyroLimit=%08x, yGyrogain=%08x, yGLenz=%08x, yShiftRG=%08x, yLimit=%f\n", yGyroLimit , yGyrogain , yGLenz , yShiftRG , yLimit);
	return (xng << 8) | yng;
}

UINT_16  HallCheck(void)
{
	short i;
	unsigned short ret;

	// Change Loop Gain
	LoopGainSet( 1, LPGSET );

	// do Hall Accuracy test
	ret = Accuracy();

	// Restore Loop Gain
	LoopGainSet( 0, LPGSET );

	if(ret)
	{
    	TRACE("\n VCM has NG points: X = %d, Y = %d", ret >> 8, ret & 0xff);
	} else {
    	TRACE("\n VCM is good!");
	}
	
	// Max Accuracy
	TRACE("\n X Max Accuracy = %f, Y Max Accuracy = %f", xMaxAcc, yMaxAcc);

	// Limit vale
	TRACE("\n xLimit = %f, yLimit = %f", xLimit, yLimit);

	// Circle
	for(i=0; i<=(360/DEGSTEP); i++)
	{
		TRACE("\n xPos = %f, xHall = %f, yPos = %f, yHall = %f", xy_raw_data[i].xpos, xy_raw_data[i].xhall, xy_raw_data[i].ypos, xy_raw_data[i].yhall);
	}
	return(ret);
}

unsigned short AccuracyH(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3)
{
	float xpos, ypos;
	unsigned int xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    unsigned short xng = 0, yng = 0;
    unsigned short deg;
    float xRadius, yRadius;
	unsigned int xGyrogain, yGyrogain;
    unsigned int xGLenz, yGLenz;
    unsigned int xG2x4xb, yG2x4xb;
    unsigned int xGoutG, yGoutG;

	// Get Gyro gain
	RamRead32A(GyroFilterTableX_gxzoom, &xGyrogain);
	RamRead32A(GyroFilterTableY_gyzoom, &yGyrogain);

	// Get Lenz
	RamRead32A(GyroFilterTableX_gxlenz, &xGLenz);
	RamRead32A(GyroFilterTableY_gylenz, &yGLenz);

	// Get Shift
	RamRead32A(GyroFilterShiftX, &xG2x4xb);
	RamRead32A(GyroFilterShiftY, &yG2x4xb);
	
	// Get Gyro out gain
	RamRead32A(HallFilterCoeffX_hxgoutg, &xGoutG);
	RamRead32A(HallFilterCoeffY_hygoutg, &yGoutG);

	// Calculate Radius (LIMIT_RANGE) /* 不明 */
//	xRadius = ANGLE_LIMIT * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * 8 * fabsf(fix2float(xGoutG));
//	yRadius = ANGLE_LIMIT * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * 8 * fabsf(fix2float(yGoutG));
	xRadius = ANGLE_LIMIT * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * (1 << (unsigned char)( xG2x4xb >> 8 )) * fabsf(fix2float(xGoutG));
	yRadius = ANGLE_LIMIT * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * (1 << (unsigned char)( yG2x4xb >> 8 )) * fabsf(fix2float(yGoutG));

	// Calculate Limit
	xLimit = flACCURACY / LIMIT_RANGE * xRadius;
    yLimit = flACCURACY / LIMIT_RANGE * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * RADIUS / LIMIT_RANGE;
	yRadius = yRadius * RADIUS / LIMIT_RANGE;

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
	xpos = xRadius * cos(0);
	ypos = yRadius * sin(0);
	RamWrite32A(HALL_RAM_HXOFF1, float2fix(xpos));
	RamWrite32A(HALL_RAM_HYOFF1, float2fix(ypos));
	WitTim(WAIT_MSEC1);

	for( deg = 0; deg <= 360; deg += usDEGSTEP ) // 0-360 degree
	{
		xpos = xRadius * cos(deg * PI/180);
		ypos = yRadius * sin(deg * PI/180);
    	RamWrite32A(HALL_RAM_HXOFF1, float2fix(xpos));
		RamWrite32A(HALL_RAM_HYOFF1, float2fix(ypos));

		xMaxHall = 0;
		yMaxHall = 0;
		WitTim(WAIT_MSEC2);
		
		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim(WAIT_MSEC3);
			RamRead32A( HALL_RAM_HXOUT0, &xhall_value );
			RamRead32A( HALL_RAM_HYOUT0, &yhall_value );
			if(fabsf(fix2float(xhall_value) - xpos) > fabsf(xMaxHall))	
				xMaxHall = fix2float(xhall_value) - xpos;
			if(fabsf(fix2float(yhall_value) - ypos) > fabsf(yMaxHall))	
				yMaxHall = fix2float(yhall_value) - ypos;
		}

		if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
		if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
		
        // Save raw data
		xy_raw_data[deg/usDEGSTEP].xpos = xpos;
		xy_raw_data[deg/usDEGSTEP].xhall = xMaxHall + xpos;
		xy_raw_data[deg/usDEGSTEP].ypos = ypos;
		xy_raw_data[deg/usDEGSTEP].yhall = yMaxHall + ypos;
		
		if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 

	}
	RamWrite32A(HALL_RAM_HXOFF1, 0); // x = center
	RamWrite32A(HALL_RAM_HYOFF1, 0); // y = center

	return (xng << 8) | yng;
}

//unsigned short HallCheck(void)
UINT_16 HallCheckH(float flACCURACY, UINT_16 RADIUS, UINT_16 usDEGSTEP, UINT_16 WAIT_MSEC1, UINT_16 WAIT_MSEC2, UINT_16 WAIT_MSEC3)
{
	INT_16	i;
//	unsigned short ret = Accuracy();
	UINT_16 ret = AccuracyH(flACCURACY, RADIUS, usDEGSTEP, WAIT_MSEC1, WAIT_MSEC2, WAIT_MSEC3);

	if(ret)
	{
    	TRACE("\n VCM has NG points: X = %d, Y = %d", ret >> 8, ret & 0xff);
	} else {
    	TRACE("\n VCM is good!");
	}
	
	// Max Accuracy
	//TRACE("\n X Max Accuracy = %f, Y Max Accuracy = %f", xMaxAcc, yMaxAcc);
	TRACE("\n X Max Accuracy = %d, Y Max Accuracy = %d", (int)(xMaxAcc*1000), (int)(yMaxAcc*1000));

	// Limit vale
	//TRACE("\n xLimit = %f, yLimit = %f", xLimit, yLimit);
	TRACE("\n xLimit = %d, yLimit = %d", (int)(xLimit*1000), (int)(yLimit*1000));

	// Circle
	for(i=0; i<=(360/usDEGSTEP); i++)
	{
		//TRACE("\n xPos = %f, xHall = %f, yPos = %f, yHall = %f", xy_raw_data[i].xpos, xy_raw_data[i].xhall, xy_raw_data[i].ypos, xy_raw_data[i].yhall);
		TRACE("\n xPos = %d, xHall = %d, yPos = %d, yHall = %d", (int)(xy_raw_data[i].xpos*1000), (int)(xy_raw_data[i].xhall*1000), (int)(xy_raw_data[i].ypos*1000), (int)(xy_raw_data[i].yhall*1000));
	}
	
	return( ret );
}

unsigned short AccuracyG(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3)
{
	float xpos, ypos;
	unsigned int xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    unsigned short xng = 0, yng = 0;
    unsigned short deg;
    float xRadius, yRadius;
    unsigned int xGyroLimit, yGyroLimit;
	unsigned int xGyrogain, yGyrogain;
    unsigned int xGLenz, yGLenz;
    unsigned int xShiftRG, yShiftRG;
    unsigned int xHav2, yHav2;

    // Get GYROLIMT
    RamRead32A(Gyro_Limiter_X, &xGyroLimit);
	RamRead32A(Gyro_Limiter_Y, &yGyroLimit);
	
	// Get Gyro gain
	RamRead32A(GyroFilterTableX_gxzoom, &xGyrogain);
	RamRead32A(GyroFilterTableY_gyzoom, &yGyrogain);

	// Get Lenz
	RamRead32A(GyroFilterTableX_gxlenz, &xGLenz);
	RamRead32A(GyroFilterTableY_gylenz, &yGLenz);

	// Get Shift
   	RamRead32A(GyroFilterShiftX, &xShiftRG);
	RamRead32A(GyroFilterShiftY, &yShiftRG);
	xShiftRG = 1 << ((xShiftRG & 0x0000FF00) >> 8);
    yShiftRG = 1 << ((yShiftRG & 0x0000FF00) >> 8);
	
	// Calculate Radius (LIMIT_RANGE) /* 不明 */
	xRadius = fabsf(fix2float(xGyroLimit)) * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * xShiftRG ;
	yRadius = fabsf(fix2float(yGyroLimit)) * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * yShiftRG ;

	// Calculate Limit
	xLimit = flACCURACY / LIMIT_RANGE * xRadius;
    yLimit = flACCURACY / LIMIT_RANGE * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * RADIUS / LIMIT_RANGE;
	yRadius = yRadius * RADIUS / LIMIT_RANGE;

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
	xpos = xRadius * cos(0);
	ypos = yRadius * sin(0);
	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
	RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));
	WitTim(WAIT_MSEC1);

	for( deg = 0; deg <= 360; deg += usDEGSTEP ) // 0-360 degree
	{
		xpos = xRadius * cos(deg * PI/180);
		ypos = yRadius * sin(deg * PI/180);
    	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
		RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));

		xMaxHall = 0;
		yMaxHall = 0;
		WitTim(WAIT_MSEC2);
		RamRead32A( XMoveAvg_D2, &xHav2 );
		RamRead32A( YMoveAvg_D2, &yHav2 );
		
		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim(WAIT_MSEC3);
			RamRead32A( HALL_RAM_HXOUT2, &xhall_value );
			RamRead32A( HALL_RAM_HYOUT2, &yhall_value );
			if(fabsf(fix2float(xhall_value) + fix2float(xHav2)) > fabsf(xMaxHall))	
				xMaxHall = fix2float(xhall_value) + fix2float(xHav2);
			if(fabsf(fix2float(yhall_value) + fix2float(yHav2)) > fabsf(yMaxHall))
				yMaxHall = fix2float(yhall_value) + fix2float(yHav2);
		}
TRACE( "( xpos, xHax2, xhall, xMax ) = ( %f, %f, %f, %f)\n", xpos , fix2float(xHav2) , fix2float(xhall_value), xMaxHall );
TRACE( "( ypos, yHax2, yhall, yMax ) = ( %f, %f, %f, %f)\n", ypos , fix2float(yHav2) , fix2float(yhall_value), yMaxHall );

		if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
		if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
		
        // Save raw data
		xy_raw_data[deg/usDEGSTEP].xpos = fix2float(xHav2);
		xy_raw_data[deg/usDEGSTEP].xhall = xMaxHall + fix2float(xHav2);
		xy_raw_data[deg/usDEGSTEP].ypos = fix2float(yHav2);
		xy_raw_data[deg/usDEGSTEP].yhall = yMaxHall + fix2float(yHav2);
		
		if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 

	}
	RamWrite32A(HALL_RAM_GYROX_OUT, 0); // x = center
	RamWrite32A(HALL_RAM_GYROY_OUT, 0); // y = center

	return (xng << 8) | yng;
}

//unsigned short HallCheck(void)
UINT_16 HallCheckG(float flACCURACY, UINT_16 RADIUS, UINT_16 usDEGSTEP, UINT_16 WAIT_MSEC1, UINT_16 WAIT_MSEC2, UINT_16 WAIT_MSEC3)
{
	INT_16	i;
	UINT_16 ret;
	
	// Change Loop Gain
	LoopGainSet( 1, LPGSET );

	//	unsigned short ret = Accuracy();
	ret = AccuracyG(flACCURACY, RADIUS, usDEGSTEP, WAIT_MSEC1, WAIT_MSEC2, WAIT_MSEC3);

	// Restore Loop Gain
	LoopGainSet( 0, LPGSET );

	if(ret)
	{
    	TRACE("\n VCM has NG points: X = %d, Y = %d", ret >> 8, ret & 0xff);
	} else {
    	TRACE("\n VCM is good!");
	}
	
	// Max Accuracy
	//TRACE("\n X Max Accuracy = %f, Y Max Accuracy = %f", xMaxAcc, yMaxAcc);
	TRACE("\n X Max Accuracy = %d, Y Max Accuracy = %d", (int)(xMaxAcc*1000), (int)(yMaxAcc*1000));

	// Limit vale
	//TRACE("\n xLimit = %f, yLimit = %f", xLimit, yLimit);
	TRACE("\n xLimit = %d, yLimit = %d", (int)(xLimit*1000), (int)(yLimit*1000));

	// Circle
	for(i=0; i<=(360/usDEGSTEP); i++)
	{
		//TRACE("\n xPos = %f, xHall = %f, yPos = %f, yHall = %f", xy_raw_data[i].xpos, xy_raw_data[i].xhall, xy_raw_data[i].ypos, xy_raw_data[i].yhall);
		TRACE("\n xPos = %d, xHall = %d, yPos = %d, yHall = %d", (int)(xy_raw_data[i].xpos*1000), (int)(xy_raw_data[i].xhall*1000), (int)(xy_raw_data[i].ypos*1000), (int)(xy_raw_data[i].yhall*1000));
	}
	
	return( ret );
}
/************************************************************************/
/*****			calculate by use linearity correction data			*****/
/************************************************************************/
#define	pixelsize	1.22f	// um/pixel
UINT_16 AccuracyL(float flACCURACY, UINT_16 RADIUS, UINT_16 usDEGSTEP, UINT_16 WAIT_MSEC1, UINT_16 WAIT_MSEC2, UINT_16 WAIT_MSEC3)
{
	float xpos, ypos;
	UINT_32 xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    UINT_16	xng = 0, yng = 0;
    UINT_16	deg;
    float xRadius, yRadius;
	UINT_32		uixpxl,uiypxl;
	INT_32		sixstp,siystp;
	UINT_32		linbuf[8];	/* pos1～pos7, step */
	UINT_32		calibdata;
	UINT_8		ans = 0;

	// Get Linearity data
	RamRead32A( StCaliData_UsCalibrationStatus , &calibdata ) ;	
	if( calibdata & HLLN_CALB_FLG ){
		return ( 0xFFFF );
	}
	
	ans =FlashMultiRead( INF_MAT0, LN_POS1 , linbuf , 8 );	
	if( ans )	return( 0xFFFE );

	uixpxl = (( linbuf[4] & 0x0000FFFF ) - ( linbuf[3] & 0x0000FFFF )) >>  0;
	uiypxl = (( linbuf[4] & 0xFFFF0000 ) - ( linbuf[3] & 0xFFFF0000 )) >> 16;
	sixstp  = ( INT_32 )( linbuf[7] & 0x0000FFFF ) << 16;
	siystp  = ( INT_32 )( linbuf[7] & 0xFFFF0000 ) << 0;
TRACE( "uixpxl %08x, uiypxl %08x \n", uixpxl , uiypxl );
TRACE( "sixstp %08x, siystp %08x \n", sixstp , siystp );
	
	// Calculate Radius (100um)
	xRadius = ( fabsf(fix2float(sixstp)) * 10.0f) / ((float)uixpxl * pixelsize);
	yRadius = ( fabsf(fix2float(siystp)) * 10.0f) / ((float)uiypxl * pixelsize);
TRACE( "xRadiusA %f, yRadiusA %f \n", xRadius , yRadius );

	// Calculate Limit
	xLimit = flACCURACY * xRadius;
    yLimit = flACCURACY * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * RADIUS ;
	yRadius = yRadius * RADIUS ;
TRACE( "xRadiusM %f, yRadiusM %f \n", xRadius , yRadius );
TRACE( "xRadiusM %08x, yRadiusM %08x \n", float2fix(xRadius) , float2fix(yRadius) );

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
	xpos = xRadius * cos(0);
	ypos = yRadius * sin(0);
	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
	RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));
	WitTim(WAIT_MSEC1);

	for( deg = 0; deg <= 360; deg += usDEGSTEP ) // 0-360 degree
	{
		xpos = xRadius * cos(deg * PI/180);
		ypos = yRadius * sin(deg * PI/180);
    	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
		RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));

		xMaxHall = 0;
		yMaxHall = 0;
		WitTim(WAIT_MSEC2);
		
		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim(WAIT_MSEC3);
			RamRead32A( HALL_RAM_HXOUT0, &xhall_value );
			RamRead32A( HALL_RAM_HYOUT0, &yhall_value );
			if(fabsf(fix2float(xhall_value)) > fabsf(xMaxHall))	
				xMaxHall = fix2float(xhall_value);
			if(fabsf(fix2float(yhall_value)) > fabsf(yMaxHall))	
				yMaxHall = fix2float(yhall_value);
		}

		if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
		if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
		
        // Save raw data
		xy_raw_data[deg/usDEGSTEP].xpos = xpos;
		xy_raw_data[deg/usDEGSTEP].xhall = xMaxHall;	/* gap */
		xy_raw_data[deg/usDEGSTEP].ypos = ypos;
		xy_raw_data[deg/usDEGSTEP].yhall = yMaxHall;	/* gap */
		
		if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 

	}
	RamWrite32A(HALL_RAM_GYROX_OUT, 0); // x = center
	RamWrite32A(HALL_RAM_GYROY_OUT, 0); // y = center

	return (xng << 8) | yng;
}

//unsigned short HallCheck(void)
UINT_16 HallCheckL(float flACCURACY, UINT_16 RADIUS, UINT_16 usDEGSTEP, UINT_16 WAIT_MSEC1, UINT_16 WAIT_MSEC2, UINT_16 WAIT_MSEC3)
{
	INT_16 i;
	UINT_16 ret = AccuracyL(flACCURACY, RADIUS, usDEGSTEP, WAIT_MSEC1, WAIT_MSEC2, WAIT_MSEC3);

	if(ret)
	{
    	TRACE("\n VCM has NG points: X = %d, Y = %d", ret >> 8, ret & 0xff);
	} else {
    	TRACE("\n VCM is good!");
	}
	
	// Max Accuracy
	//TRACE("\n X Max Accuracy = %f, Y Max Accuracy = %f", xMaxAcc, yMaxAcc);
	TRACE("\n X Max Accuracy = %d, Y Max Accuracy = %d", (int)(xMaxAcc*1000), (int)(yMaxAcc*1000));

	// Limit vale
	//TRACE("\n xLimit = %f, yLimit = %f", xLimit, yLimit);
	TRACE("\n xLimit = %d, yLimit = %d", (int)(xLimit*1000), (int)(yLimit*1000));

	// Circle
	for(i=0; i<=(360/usDEGSTEP); i++)
	{
		//TRACE("\n xPos = %f, xHall = %f, yPos = %f, yHall = %f", xy_raw_data[i].xpos, xy_raw_data[i].xhall, xy_raw_data[i].ypos, xy_raw_data[i].yhall);
		TRACE("\n xPos = %d, xHall = %d, yPos = %d, yHall = %d", (int)(xy_raw_data[i].xpos*1000), (int)(xy_raw_data[i].xhall*1000), (int)(xy_raw_data[i].ypos*1000), (int)(xy_raw_data[i].yhall*1000));
	}
	
	return( ret );
}

unsigned short AccuracyS(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3 , unsigned char ACT_AXIS)
{
	float xpos, ypos;
	unsigned int xhall_value, yhall_value;
	float xMaxHall, yMaxHall;
    unsigned short xng = 0, yng = 0;
    unsigned short deg;
    float xRadius, yRadius;
    unsigned int xGyroLimit, yGyroLimit;
	unsigned int xGyrogain, yGyrogain;
    unsigned int xGLenz, yGLenz;
    unsigned int xShiftRG, yShiftRG;
    unsigned int xHav2, yHav2;

    // Get GYROLIMT
    RamRead32A(Gyro_Limiter_X, &xGyroLimit);
	RamRead32A(Gyro_Limiter_Y, &yGyroLimit);
	
	// Get Gyro gain
	RamRead32A(GyroFilterTableX_gxzoom, &xGyrogain);
	RamRead32A(GyroFilterTableY_gyzoom, &yGyrogain);

	// Get Lenz
	RamRead32A(GyroFilterTableX_gxlenz, &xGLenz);
	RamRead32A(GyroFilterTableY_gylenz, &yGLenz);

	// Get Shift
	RamRead32A(GyroFilterShiftX, &xShiftRG);
	RamRead32A(GyroFilterShiftY, &yShiftRG);
	xShiftRG = 1 << ((xShiftRG & 0x0000FF00) >> 8);
    yShiftRG = 1 << ((yShiftRG & 0x0000FF00) >> 8);
	
	// Calculate Radius (LIMIT_RANGE) /* 不明 */
	xRadius = fabsf(fix2float(xGyroLimit)) * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * xShiftRG ;
	yRadius = fabsf(fix2float(yGyroLimit)) * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * yShiftRG ;

	// Calculate Limit
	xLimit = flACCURACY / LIMIT_RANGE * xRadius;
    yLimit = flACCURACY / LIMIT_RANGE * yRadius;

	// Radius change (by RADIUS value)
	xRadius = xRadius * RADIUS / LIMIT_RANGE;
	yRadius = yRadius * RADIUS / LIMIT_RANGE;

	xMaxAcc = 0;
	yMaxAcc = 0;

	// Circle check
	xpos = xRadius * sin(0);
	ypos = yRadius * sin(0);
	if(ACT_AXIS == X_DIR){
		RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
	}else{
		RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));
	}
	WitTim(WAIT_MSEC1);

	for( deg = 0; deg <= 360; deg += usDEGSTEP ) // 0-360 degree
	{
//		xpos = xRadius * cos(deg * PI/180);
		xpos = xRadius * sin(deg * PI/180);
		ypos = yRadius * sin(deg * PI/180);
		if(ACT_AXIS == X_DIR){
	    	RamWrite32A(HALL_RAM_GYROX_OUT, float2fix(xpos));
		}else{
			RamWrite32A(HALL_RAM_GYROY_OUT, float2fix(ypos));
		}

		xMaxHall = 0;
		yMaxHall = 0;
		WitTim(WAIT_MSEC2);
		RamRead32A( XMoveAvg_D2, &xHav2 );
		RamRead32A( YMoveAvg_D2, &yHav2 );
		
		for(short i=0; i<LOOPTIME; i++)
		{
			WitTim(WAIT_MSEC3);
			if(ACT_AXIS == X_DIR){
				RamRead32A( HALL_RAM_HXOUT2, &xhall_value );
				if(fabsf(fix2float(xhall_value) + fix2float(xHav2)) > fabsf(xMaxHall))	
					xMaxHall = fix2float(xhall_value) + fix2float(xHav2);
			}else{
				RamRead32A( HALL_RAM_HYOUT2, &yhall_value );
				if(fabsf(fix2float(yhall_value) + fix2float(yHav2)) > fabsf(yMaxHall))
					yMaxHall = fix2float(yhall_value) + fix2float(yHav2);
			}
		}

		if(ACT_AXIS == X_DIR){
			if(fabsf(xMaxHall) > xMaxAcc)	xMaxAcc = fabsf(xMaxHall);
			xy_raw_data[deg/usDEGSTEP].xpos = fix2float(xHav2);
			xy_raw_data[deg/usDEGSTEP].xhall = xMaxHall + fix2float(xHav2);
			xy_raw_data[deg/usDEGSTEP].ypos = 0;
			xy_raw_data[deg/usDEGSTEP].yhall = 0;
			if(fabsf(xMaxHall) > xLimit)	xng++; 	// Have NG point;
		}else{
			if(fabsf(yMaxHall) > yMaxAcc)	yMaxAcc = fabsf(yMaxHall);
	        // Save raw data
			xy_raw_data[deg/usDEGSTEP].ypos = fix2float(yHav2);
			xy_raw_data[deg/usDEGSTEP].yhall = yMaxHall + fix2float(yHav2);
			xy_raw_data[deg/usDEGSTEP].xpos = 0;
			xy_raw_data[deg/usDEGSTEP].xhall = 0;
			if(fabsf(yMaxHall) > yLimit)	yng++; 	// Have NG point; 
		}
		

	}
	RamWrite32A(HALL_RAM_GYROX_OUT, 0); // x = center
	RamWrite32A(HALL_RAM_GYROY_OUT, 0); // y = center

	return (xng << 8) | yng;
}

//unsigned short HallCheck(void)
UINT_16 HallCheckS(float flACCURACY, UINT_16 RADIUS, UINT_16 usDEGSTEP, UINT_16 WAIT_MSEC1, UINT_16 WAIT_MSEC2, UINT_16 WAIT_MSEC3 , UINT_8 ACT_AXIS)
{
	INT_16	i;
//	unsigned short ret = Accuracy();
	UINT_16 ret = AccuracyS(flACCURACY, RADIUS, usDEGSTEP, WAIT_MSEC1, WAIT_MSEC2, WAIT_MSEC3, ACT_AXIS);

	if(ret)
	{
    	TRACE("\n VCM has NG points: X = %d, Y = %d", ret >> 8, ret & 0xff);
	} else {
    	TRACE("\n VCM is good!");
	}
	
	// Max Accuracy
	//TRACE("\n X Max Accuracy = %f, Y Max Accuracy = %f", xMaxAcc, yMaxAcc);
	TRACE("\n X Max Accuracy = %d, Y Max Accuracy = %d", (int)(xMaxAcc*1000), (int)(yMaxAcc*1000));

	// Limit vale
	//TRACE("\n xLimit = %f, yLimit = %f", xLimit, yLimit);
	TRACE("\n xLimit = %d, yLimit = %d", (int)(xLimit*1000), (int)(yLimit*1000));

	// Circle
	for(i=0; i<=(360/usDEGSTEP); i++)
	{
		//TRACE("\n xPos = %f, xHall = %f, yPos = %f, yHall = %f", xy_raw_data[i].xpos, xy_raw_data[i].xhall, xy_raw_data[i].ypos, xy_raw_data[i].yhall);
		TRACE("\n xPos = %d, xHall = %d, yPos = %d, yHall = %d", (int)(xy_raw_data[i].xpos*1000), (int)(xy_raw_data[i].xhall*1000), (int)(xy_raw_data[i].ypos*1000), (int)(xy_raw_data[i].yhall*1000));
	}
	
	return( ret );
}

#if 0
unsigned char SMA_Sensitivity( unsigned short LIMIT_RANGE_SMA, unsigned short RADIUS, unsigned short DEGREE, unsigned short WAIT_MSEC1)
{
	float xpos, ypos;
    float xRadius, yRadius;
	unsigned int xGyrogain, yGyrogain;
    unsigned int xGLenz, yGLenz;

	// Get Gyro gain
	RamRead32A(0x8890, &xGyrogain);
	RamRead32A(0x88C4, &yGyrogain);

	// Get Lenz
	RamRead32A(0x8894, &xGLenz);
	RamRead32A(0x88C8, &yGLenz);

	// Calculate Radius (LIMIT_RANGE) /* 不明 */
	xRadius = ANGLE_LIMIT_SMA * fabsf(fix2float(xGyrogain)) * fabsf(fix2float(xGLenz)) * 1 ;
	yRadius = ANGLE_LIMIT_SMA * fabsf(fix2float(yGyrogain)) * fabsf(fix2float(yGLenz)) * 1 ;


	// Radius change (by RADIUS value)
	xRadius = xRadius * (float)RADIUS / (float)LIMIT_RANGE_SMA;
	yRadius = yRadius * (float)RADIUS / (float)LIMIT_RANGE_SMA;

	xpos = xRadius * cos(DEGREE * PI/180);
	ypos = yRadius * sin(DEGREE * PI/180);

	RamWrite32A(0x610, float2fix(xpos));
	RamWrite32A(0x61C, float2fix(ypos));

	WitTim(WAIT_MSEC1);

	return (1);
}
#endif
