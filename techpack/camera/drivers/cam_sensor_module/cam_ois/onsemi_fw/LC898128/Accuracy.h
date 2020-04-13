/*=======================================================================
		Accuracy Test Sample code for LC898124
                                             	by Rex.Tang
                                                2016.03.27
========================================================================*/
#ifndef __OISACCURACY_H__
#define __OISACCURACY_H__

#ifdef	__OISACCURACY__
	#define	__OISACCURACY_HEADER__
#else
	#define	__OISACCURACY_HEADER__		extern
#endif

// Checking radius
//#define		RADIUS 		75							// 75um

// Parameter define
//#define		DEGSTEP		3							// Degree of one step (3Åã)
//#define		ACCURACY	3.0F						// Accuracy (Å}3.0um)
//#define		WAIT_MSEC	10							// Each step wait time(msec)
#define		LOOPTIME	3							// Read times at each step



union	FLTVAL2 {
	float			SfFltVal ;
	struct {
		unsigned char UcDataHH;
		unsigned char UcDataHL;
		unsigned char UcDataLH;
		unsigned char UcDataLL;
	} StFltVal ;
} ;

typedef union FLTVAL2	UnFltVal2 ;

typedef struct tag_Dual_Axis
{
	float xpos;
	float xhall;
	float ypos;
	float yhall;
}Dual_Axis_t;

/* Raw data buffers */	
//Dual_Axis_t xy_raw_data[360/DEGSTEP + 1];
__OISACCURACY_HEADER__	Dual_Axis_t	xy_raw_data[360/3 + 1];
__OISACCURACY_HEADER__	float		xMaxAcc, yMaxAcc;
__OISACCURACY_HEADER__	float		xLimit, yLimit;

//__OISACCURACY_HEADER__	unsigned short HallCheck(float ACCURACY, unsigned short RADIUS, unsigned short DEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3);
__OISACCURACY_HEADER__	unsigned short HallCheck(void);
__OISACCURACY_HEADER__	unsigned short HallCheckH(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3);
__OISACCURACY_HEADER__	unsigned short HallCheckL(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3);
__OISACCURACY_HEADER__	unsigned short HallCheckS(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3 , unsigned char ACT_AXIS);
__OISACCURACY_HEADER__	unsigned short HallCheckG(float flACCURACY, unsigned short RADIUS, unsigned short usDEGSTEP, unsigned short WAIT_MSEC1, unsigned short WAIT_MSEC2, unsigned short WAIT_MSEC3);
//__OISACCURACY_HEADER__	unsigned char  SMA_Sensitivity( unsigned short LIMIT_RANGE_SMA, unsigned short RADIUS, unsigned short DEGREE, unsigned short WAIT_MSEC1);

#endif	//__OISACCURACY_H__
