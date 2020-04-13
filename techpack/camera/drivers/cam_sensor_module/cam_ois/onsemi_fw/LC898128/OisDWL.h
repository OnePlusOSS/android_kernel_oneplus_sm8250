/**
 * @brief		OIS system header for LC898128
 * 				API List for customers
 *
 * @author		Copyright (C) 2015, ON Semiconductor, all right reserved.
 *
 * @file		OisDWL.h
 * @date		svn:$Date:: 2016-06-17 16:42:32 +0900#$
 * @version		svn:$Revision: 54 $
 * @attention
 **/

#ifdef	__OISDWL__
	#define	__OIS_FLDL_HEADER__
#else
	#define	__OIS_FLDL_HEADER__		extern
#endif

#include "Ois.h"

 __OIS_FLDL_HEADER__	UINT_8	FlashUpload128( UINT_8 ModuleVendor, UINT_8 ActVer, UINT_8 MasterSlave, UINT_8 FWType);
//#ifdef	TRNT
 __OIS_FLDL_HEADER__	UINT_8	LoadUserAreaToPM( void  );
//#endif

