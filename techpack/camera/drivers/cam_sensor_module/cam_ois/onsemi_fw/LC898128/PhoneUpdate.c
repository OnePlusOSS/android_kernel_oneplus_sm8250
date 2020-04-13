/**
 *		LC898128 Flash update
 *
 *		Copyright (C) 2017, ON Semiconductor, all right reserved.
 *
 **/



//**************************
//	Include Header File
//**************************
#include	"PhoneUpdate.h"

//#include	<stdlib.h>
//#include	<math.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include	"FromCode_01_02_01_00.h"
#include	"FromCode_01_02_02_01.h"

/* Burst Length for updating to PMEM */
#define BURST_LENGTH_UC 		( 3*20 ) 	// 60 Total:63Byte Burst
//#define BURST_LENGTH_UC 		( 6*20 ) 	// 120 Total:123Byte Burst
/* Burst Length for updating to Flash */
#define BURST_LENGTH_FC 		( 32 )	 	// 32 Total: 35Byte Burst
//#define BURST_LENGTH_FC 		( 64 )	 	// 64 Total: 67Byte Burst

//****************************************************
//	CUSTOMER NECESSARY CREATING FUNCTION LIST
//****************************************************
/* for I2C communication */
extern	void RamWrite32A( UINT_16, UINT_32 );
extern 	INT_32 RamRead32A( UINT_16, void * );
/* for I2C Multi Translation : Burst Mode*/
extern 	void CntWrt( void *, UINT_16) ;
extern	void CntRd( UINT_32, void *, UINT_16 ) ;

/* for Wait timer [Need to adjust for your system] */
extern void	WitTim( UINT_16 );

//**************************
//	extern  Function LIST
//**************************

//**************************
//	Table of download file
//**************************

UINT_32	FW_info[][3] =
{
    /* on Module vendor, Actuator Size,   on vesion number */
    {0x01,             0x01,         VERNUM_01_02_01_00},
    {0x01,             0x02,         VERNUM_01_02_02_01}
};


const DOWNLOAD_TBL_EXT DTbl[] = {
	{0x010100, 1, CcUpdataCode128_01_02_01_00, UpDataCodeSize_01_02_01_00,  UpDataCodeCheckSum_01_02_01_00, CcFromCode128_01_02_01_00, sizeof(CcFromCode128_01_02_01_00), FromCheckSum_01_02_01_00, FromCheckSumSize_01_02_01_00 },
	{0x010201, 1, CcUpdataCode128_01_02_02_01, UpDataCodeSize_01_02_02_01,  UpDataCodeCheckSum_01_02_02_01, CcFromCode128_01_02_02_01, sizeof(CcFromCode128_01_02_02_01), FromCheckSum_01_02_02_01, FromCheckSumSize_01_02_02_01 },
	{0xFFFFFF, 0,         (void*)0,                         0,                            0,                         (void*)0,                          0,                             0,                        0}
};



//**************************
//	Local Function Prototype
//**************************
void	SetGyroCoef( UINT_8  );
void	SetAccelCoef( UINT_8 );

//********************************************************************************
// Function Name 	: IOWrite32A
//********************************************************************************
void IORead32A( UINT_32 IOadrs, UINT_32 *IOdata )
{
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamRead32A ( CMD_IO_DAT_ACCESS, IOdata ) ;
}

//********************************************************************************
// Function Name 	: IOWrite32A
//********************************************************************************
void IOWrite32A( UINT_32 IOadrs, UINT_32 IOdata )
{
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, IOdata ) ;
}

//********************************************************************************
// Function Name 	: UnlockCodeSet
//********************************************************************************
UINT_8 UnlockCodeSet( void )
{
	UINT_32 UlReadVal, UlCnt=0;

	do {
		IOWrite32A( 0xE07554, 0xAAAAAAAA );
		IOWrite32A( 0xE07AA8, 0x55555555 );
		IORead32A( 0xE07014, &UlReadVal );
		if( (UlReadVal & 0x00000080) != 0 )	return ( 0 ) ;	
		WitTim( 1 );
	} while( UlCnt++ < 10 );
	return ( 1 );
}

//********************************************************************************
// Function Name 	: UnlockCodeClear
//********************************************************************************
UINT_8 UnlockCodeClear(void)
{
	UINT_32 UlDataVal, UlCnt=0;

	do {
		IOWrite32A( 0xE07014, 0x00000010 );	
		IORead32A( 0xE07014, &UlDataVal );
		if( (UlDataVal & 0x00000080) == 0 )	return ( 0 ) ;	
		WitTim( 1 );
	} while( UlCnt++ < 10 );
	return ( 3 );
}
//********************************************************************************
// Function Name 	: WritePermission
//********************************************************************************
void WritePermission( void )
{
	IOWrite32A( 0xE074CC, 0x00000001 );	
	IOWrite32A( 0xE07664, 0x00000010 );	
}

//********************************************************************************
// Function Name 	: AddtionalUnlockCodeSet
//********************************************************************************
void AddtionalUnlockCodeSet( void )
{
	IOWrite32A( 0xE07CCC, 0x0000ACD5 );	
}
//********************************************************************************
// Function Name 	: CoreResetwithoutMC128
//********************************************************************************
UINT_8 CoreResetwithoutMC128( void )
{
	UINT_32	UlReadVal ;
	
	IOWrite32A( 0xE07554, 0xAAAAAAAA);
	IOWrite32A( 0xE07AA8, 0x55555555);
	
	IOWrite32A( 0xE074CC, 0x00000001);
	IOWrite32A( 0xE07664, 0x00000010);
	IOWrite32A( 0xE07CCC, 0x0000ACD5);
	IOWrite32A( 0xE0700C, 0x00000000);
	IOWrite32A( 0xE0701C, 0x00000000);
	IOWrite32A( 0xE07010, 0x00000004);

	WitTim(100);

	IOWrite32A( 0xE0701C, 0x00000002);
	IOWrite32A( 0xE07014, 0x00000010);

	IOWrite32A( 0xD00060, 0x00000001 ) ;
	WitTim( 15 ) ;

	IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
	switch ( (UINT_8)UlReadVal ){
	case 0x08:
	case 0x0D:
		break;
	
	default:	
		return( 0xE0  | (UINT_8)UlReadVal );
	}
	
	return( 0 );
}

//********************************************************************************
// Function Name 	: PmemUpdate128
//********************************************************************************
UINT_8 PmemUpdate128( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_8	data[BURST_LENGTH_UC +2 ];
	UINT_16	Remainder;
	const UINT_8 *NcDataVal = ptr->UpdataCode;
	UINT_8	ReadData[8];
	long long CheckSumCode = ptr->SizeUpdataCodeCksm;
	UINT_8 *p = (UINT_8 *)&CheckSumCode;
	UINT_32 i, j;
	UINT_32	UlReadVal, UlCnt , UlNum ;
//--------------------------------------------------------------------------------
// 
//--------------------------------------------------------------------------------
	IOWrite32A( 0xE0701C, 0x00000000);
	RamWrite32A( 0x3000, 0x00080000 );


	data[0] = 0x40;
	data[1] = 0x00;


	Remainder = ( (ptr->SizeUpdataCode*5) / BURST_LENGTH_UC ); 
	for(i=0 ; i< Remainder ; i++)
	{
		UlNum = 2;
		for(j=0 ; j < BURST_LENGTH_UC; j++){
			data[UlNum] =  *NcDataVal++;
			if( ( j % 5) == 4)	TRACE("\n");
			UlNum++;
		}
		
		CntWrt( data, BURST_LENGTH_UC+2 );
	}
	Remainder = ( (ptr->SizeUpdataCode*5) % BURST_LENGTH_UC); 
	if (Remainder != 0 )
	{
		UlNum = 2;
		for(j=0 ; j < Remainder; j++){
			data[UlNum++] = *NcDataVal++;
		}
		CntWrt( data, Remainder+2 );
	}
	
//--------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------


	data[0] = 0xF0;											
	data[1] = 0x0E;											
	data[2] = (unsigned char)((ptr->SizeUpdataCode >> 8) & 0x000000FF);	
	data[3] = (unsigned char)(ptr->SizeUpdataCode & 0x000000FF);
	data[4] = 0x00;											
	data[5] = 0x00;											

	CntWrt( data, 6 ) ;


	UlCnt = 0;
	do{
		WitTim( 1 );
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C, 0x00000002);
			return (0x21) ;									
		}
		RamRead32A( 0x0088, &UlReadVal );					
	}while ( UlReadVal != 0 );

	CntRd( 0xF00E, ReadData , 8 );
	
	IOWrite32A( 0xE0701C, 0x00000002);
	for( i=0; i<8; i++) {
		if(ReadData[7-i] != *p++ ) {  						
			return (0x22) ;				
		}
	}

	return( 0 );
}

//********************************************************************************
// Function Name 	: EraseUserMat128
//********************************************************************************
UINT_8 EraseUserMat128(UINT_8 StartBlock, UINT_8 EndBlock )
{
	UINT_32 i;
	UINT_32	UlReadVal, UlCnt ;

	IOWrite32A( 0xE0701C, 0x00000000);
	RamWrite32A( 0xF007, 0x00000000 );


	for( i=StartBlock ; i<EndBlock ; i++) {
		RamWrite32A( 0xF00A, ( i << 10 ) );	
		RamWrite32A( 0xF00C, 0x00000020 );	


		WitTim( 5 );
		UlCnt = 0;
		do{

			WitTim( 1 );
			if( UlCnt++ > 10 ){
				IOWrite32A( 0xE0701C, 0x00000002);
				return (0x31) ;			
			}
			RamRead32A( 0xF00C, &UlReadVal );
		}while ( UlReadVal != 0 );
	}
	IOWrite32A( 0xE0701C, 0x00000002);
	return(0);

}

//********************************************************************************
// Function Name 	: ProgramFlash128_Standard
//********************************************************************************
UINT_8 ProgramFlash128_Standard( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_32	UlReadVal, UlCnt , UlNum ;
	UINT_8	data[(BURST_LENGTH_FC + 3)];
	UINT_32 i, j;

	const UINT_8 *NcFromVal = ptr->FromCode + 64;
	const UINT_8 *NcFromVal1st = ptr->FromCode;
	UINT_8 UcOddEvn;

	IOWrite32A( 0xE0701C, 0x00000000);
	RamWrite32A( 0xF007, 0x00000000 );
	RamWrite32A( 0xF00A, 0x00000010 );
	data[0] = 0xF0;					
	data[1] = 0x08;					
	data[2] = 0x00;					
	
	for(i=1 ; i< ( ptr->SizeFromCode / 64 ) ; i++)
	{
		if( ++UcOddEvn >1 )  	UcOddEvn = 0;	
		if (UcOddEvn == 0) data[1] = 0x08;
		else 			   data[1] = 0x09;		

#if (BURST_LENGTH_FC == 32)
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 ); 
	  	data[2] = 0x20;		
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 ); 
#elif (BURST_LENGTH_FC == 64)
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  
#endif

		RamWrite32A( 0xF00B, 0x00000010 );	
		UlCnt = 0;
		if (UcOddEvn == 0){
			do{								
				RamRead32A( 0xF00C, &UlReadVal );	
				if( UlCnt++ > 250 ) {
					IOWrite32A( 0xE0701C, 0x00000002);
					return (0x41) ;			
				}
			}while ( UlReadVal  != 0 );			
		 	RamWrite32A( 0xF00C, 0x00000004 );
		}else{
			do{								
				RamRead32A( 0xF00C, &UlReadVal );	
				if( UlCnt++ > 250 ) {
					IOWrite32A( 0xE0701C, 0x00000002);
					return (0x41) ;			
				}
			}while ( UlReadVal  != 0 );			
			RamWrite32A( 0xF00C, 0x00000008 );	
		}
	}
	UlCnt = 0;
	do{										
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );	
		if( UlCnt++ > 250 ) {
			IOWrite32A( 0xE0701C, 0x00000002);
			return (0x41) ;				
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	

	{
		RamWrite32A( 0xF00A, 0x00000000 );	
		data[1] = 0x08;

#if (BURST_LENGTH_FC == 32)
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );
	  	data[2] = 0x20;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );
#elif (BURST_LENGTH_FC == 64)
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );
#endif

		RamWrite32A( 0xF00B, 0x00000010 );
		UlCnt = 0;
		do{	
			RamRead32A( 0xF00C, &UlReadVal );
			if( UlCnt++ > 250 ) {
				IOWrite32A( 0xE0701C , 0x00000002);
				return (0x41) ;	
			}
		}while ( UlReadVal != 0 );
	 	RamWrite32A( 0xF00C, 0x00000004 );
	}
	
	UlCnt = 0;	
	do{	
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );
		if( UlCnt++ > 250 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x41) ;	
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	

	IOWrite32A( 0xE0701C, 0x00000002);
	return( 0 );
}


//********************************************************************************
// Function Name 	: FlashMultiRead
//********************************************************************************
UINT_8	FlashMultiRead( UINT_8 SelMat, UINT_32 UlAddress, UINT_32 *PulData , UINT_8 UcLength )
{
	UINT_8	i	 ;



	if( SelMat != USER_MAT && SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )	return 10;

	if( UlAddress > 0x000003FF )											return 9;
	
	IOWrite32A( 0xE07008 , 0x00000000 | (UINT_32)(UcLength-1) );
	IOWrite32A( 0xE0700C , ((UINT_32)SelMat << 16) | ( UlAddress & 0x00003FFF ) );
	
	IOWrite32A( 0xE0701C , 0x00000000);
	IOWrite32A( 0xE07010 , 0x00000001 );
	for( i=0 ; i < UcLength ; i++ ){
		IORead32A( 0xE07000 , &PulData[i] ) ;
	}

	IOWrite32A( 0xE0701C , 0x00000002);
	return( 0 ) ;
}

//********************************************************************************
// Function Name 	: FlashBlockErase
//********************************************************************************
UINT_8	FlashBlockErase( UINT_8 SelMat , UINT_32 SetAddress )
{
	UINT_32	UlReadVal, UlCnt;
	UINT_8	ans	= 0 ;



	if( SelMat != USER_MAT && SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )	return 10;

	if( SetAddress > 0x000003FF )											return 9;


	ans	= UnlockCodeSet();
	if( ans != 0 )	return( ans ) ;		

	WritePermission();		
	if( SelMat != USER_MAT ){
		if( SelMat == INF_MAT2 )	IOWrite32A( 0xE07CCC, 0x00006A4B );
		else						IOWrite32A( 0xE07CCC, 0x0000C5AD );
	}
	AddtionalUnlockCodeSet();	
	
	IOWrite32A( 0xE0700C , ((UINT_32)SelMat << 16) | ( SetAddress & 0x00003C00 )) ;

	IOWrite32A( 0xE0701C , 0x00000000);
	IOWrite32A( 0xE07010 , 4 ) ;

	WitTim( 5 ) ;

	UlCnt	= 0 ;

	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( FLASHROM_FLAINT, &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;

	IOWrite32A( 0xE0701C , 0x00000002);
	ans	= UnlockCodeClear();	
	if( ans != 0 )	return( ans ) ;	

	return( ans ) ;
}
//********************************************************************************
// Function Name 	: FlashBlockWrite
//********************************************************************************
UINT_8	FlashBlockWrite( UINT_8 SelMat , UINT_32 SetAddress , UINT_32 *PulData)
{
	UINT_32	UlReadVal, UlCnt;
	UINT_8	ans	= 0 ;
	UINT_8	i	 ;

	if( SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )			return 10;
	// 
	if( SetAddress > 0x000003FF )							return 9;

	ans	= UnlockCodeSet();
	if( ans != 0 )	return( ans ) ;	

	WritePermission();	
	if( SelMat != USER_MAT ){
		if( SelMat == INF_MAT2 )	IOWrite32A( 0xE07CCC, 0x00006A4B );
		else						IOWrite32A( 0xE07CCC, 0x0000C5AD );
	}
	AddtionalUnlockCodeSet();
	
	IOWrite32A( 0xE0700C , ((UINT_32)SelMat << 16) | ( SetAddress & 0x000010 )) ;
	
	IOWrite32A( 0xE0701C , 0x00000000);
	IOWrite32A( 0xE07010 , 2 ) ;


	UlCnt	= 0 ;

	for( i=0 ; i< 16 ; i++ ){
		IOWrite32A( 0xE07004 , PulData[i]  );
	}
	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( 0xE07018 , &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;

	IOWrite32A( 0xE07010 , 8  );	
	
	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( 0xE07018 , &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;
	
	IOWrite32A( 0xE0701C , 0x00000002);
	ans	= UnlockCodeClear();
	return( ans ) ;							

}

//********************************************************************************
// Function Name 	: Mat2ReWrite
//********************************************************************************
UINT_8 Mat2ReWrite( void )
{
	UINT_32	UlMAT2[32];
	UINT_32	UlCKSUM=0;
	UINT_8	ans , i ;
	UINT_32	UlCkVal ,UlCkVal_Bk;

	ans = FlashMultiRead( INF_MAT2, 0, UlMAT2, 32 );
	if(ans)	return( 0xA0 );

	if( UlMAT2[FT_REPRG] == PRDCT_WR || UlMAT2[FT_REPRG] == USER_WR ){
		return( 0x00 );
	}
	
	if( UlMAT2[CHECKCODE1] != CHECK_CODE1 )	return( 0xA1 );
	if( UlMAT2[CHECKCODE2] != CHECK_CODE2 )	return( 0xA2 );
	
	for( i=16 ; i<MAT2_CKSM ; i++){
		UlCKSUM += UlMAT2[i];
	}
	if(UlCKSUM != UlMAT2[MAT2_CKSM])		return( 0xA3 );
	
	UlMAT2[FT_REPRG] = USER_WR;
	
	UlCkVal_Bk = 0;
	for( i=0; i < 32; i++ ){
		UlCkVal_Bk +=  UlMAT2[i];
	}
	
	ans = FlashBlockErase( INF_MAT2 , 0 );
	if( ans != 0 )	return( 0xA4 ) ;		
	
	ans = FlashBlockWrite( INF_MAT2 , 0 , UlMAT2 );
	if( ans != 0 )	return( 0xA5 ) ;
	ans = FlashBlockWrite( INF_MAT2 , (UINT_32)0x10 , &UlMAT2[0x10] );
	if( ans != 0 )	return( 0xA5 ) ;

	ans =FlashMultiRead( INF_MAT2, 0, UlMAT2, 32 );
	if( ans )	return( 0xA0 );
	
	UlCkVal = 0;
	for( i=0; i < 32; i++ ){
		UlCkVal +=  UlMAT2[i];
	}
	
	if( UlCkVal != UlCkVal_Bk )		return( 0xA6 );	
	
	return( 0x01 );	
}

//********************************************************************************
// Function Name 	: FlashUpdate128
//********************************************************************************
UINT_8 FlashUpdate128( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_8 ans=0;
	UINT_32	UlReadVal, UlCnt ;
	
 	ans = CoreResetwithoutMC128();
 	if(ans != 0) return( ans );	

	ans = Mat2ReWrite();
 	if(ans != 0 && ans != 1) return( ans );	
	
 	ans = PmemUpdate128( ptr );	
	if(ans != 0) return( ans );

//--------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------
	if( UnlockCodeSet() != 0 ) 		return (0x33) ;	
	WritePermission();								
	AddtionalUnlockCodeSet();						

 #if	0
 	ans = EraseUserMat128(0, 10); // Full Block.
#else
 	ans = EraseUserMat128(0, 7);  // 0-6 Block for use user area.
#endif
	if(ans != 0){
		if( UnlockCodeClear() != 0 ) 	return (0x32) ;	
		else					 		return( ans );
	}

 	ans = ProgramFlash128_Standard( ptr );
	if(ans != 0){
		if( UnlockCodeClear() != 0 ) 	return (0x43) ;	
		else					 		return( ans );
	}

	if( UnlockCodeClear() != 0 ) 	return (0x43) ;		

//--------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0xF00A, 0x00000000 );				
	RamWrite32A( 0xF00D, ptr->SizeFromCodeValid );	

	RamWrite32A( 0xF00C, 0x00000100 );				
	WitTim( 6 );
	UlCnt = 0;
	do{												
		RamRead32A( 0xF00C, &UlReadVal );			
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x51) ;			
		}
		WitTim( 1 );		
	}while ( UlReadVal != 0 );

	RamRead32A( 0xF00D, &UlReadVal );			

	if( UlReadVal != ptr->SizeFromCodeCksm ) {
		IOWrite32A( 0xE0701C , 0x00000002);
		return( 0x52 );
	}

	IOWrite32A( SYSDSP_REMAP,				0x00001000 ) ;	
	WitTim( 15 ) ;											
	IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
	if( UlReadVal != 0x0A)		return( 0x53 );

	return( 0 );
}

//********************************************************************************
// Function Name 	: FlashDownload_128
//********************************************************************************
UINT_8 FlashDownload128( UINT_8 ModuleVendor, UINT_8 ActVer, UINT_8 MasterSlave, UINT_8 FWType)
{
	DOWNLOAD_TBL_EXT* ptr = NULL;
	UINT_32	data1 = 0;
	UINT_32	data2 = 0;

	ptr = ( DOWNLOAD_TBL_EXT * )DTbl ;

	do {
		if((ptr->Index == ( ((UINT_32)ModuleVendor<<16) + ((UINT_32)ActVer<<8) + MasterSlave)) && (ptr->FWType == FWType)) {

			// UploadFileが64Byte刻みにPaddingされていないならば、Error。
			if( ( ptr->SizeFromCode % 64 ) != 0 )	return (0xF1) ;

			if(!RamRead32A(0x8000, &data1)) {
				if(!RamRead32A(0x8004, &data2)) {
					if ((data1 == (ptr->FromCode[153] << 24 |
								   ptr->FromCode[154] << 16 |
								   ptr->FromCode[155] << 8 |
								   ptr->FromCode[156])) &&
						((data2 & 0xFFFFFF00 ) == (ptr->FromCode[158] << 24 |
								   ptr->FromCode[159] << 16 |
								   ptr->FromCode[160] << 8 ))) {
							TRACE("The FW 0x%x:0x%x is the latest, no need to upload\n", data1, data2);
							return 0;
					} else {
							TRACE("0x8000 = 0x%x 0x8004 = 0x%x is not the latest 0x%x:0x%x, will upload\n", data1, data2,
									(ptr->FromCode[153] << 24 |
									 ptr->FromCode[154] << 16 |
									 ptr->FromCode[155] << 8 |
									 ptr->FromCode[156]),
									(ptr->FromCode[158] << 24 |
									 ptr->FromCode[159] << 16 |
									 ptr->FromCode[160] << 8 |
									 ptr->FromCode[161]));
					}
				} else {
					TRACE("Read 0x8004 failed\n");
					return 0xF2;
				}
			} else {
				TRACE("Read 0x8000 failed\n");
				return 0xF2;
			}

			return FlashUpdate128( ptr );
		}
		ptr++ ;
	} while (ptr->Index != 0xFFFFFF ) ;

	return 0xF0 ;
}


void	SetGyroOffset( UINT_16 GyroOffsetX, UINT_16 GyroOffsetY, UINT_16 GyroOffsetZ )
{
	RamWrite32A( GYRO_RAM_GXOFFZ , (( GyroOffsetX << 16 ) & 0xFFFF0000 ) ) ;
	RamWrite32A( GYRO_RAM_GYOFFZ , (( GyroOffsetY << 16 ) & 0xFFFF0000 ) ) ;
	RamWrite32A( GYRO_ZRAM_GZOFFZ , (( GyroOffsetZ << 16 ) & 0xFFFF0000 ) ) ;
}

void	SetAcclOffset( UINT_16 AcclOffsetX, UINT_16 AcclOffsetY, UINT_16 AcclOffsetZ )
{
	RamWrite32A( ACCLRAM_X_AC_OFFSET , ( ( AcclOffsetX << 16 ) & 0xFFFF0000 ) ) ;
	RamWrite32A( ACCLRAM_Y_AC_OFFSET , ( ( AcclOffsetY << 16 ) & 0xFFFF0000 ) ) ;
	RamWrite32A( ACCLRAM_Z_AC_OFFSET , ( ( AcclOffsetZ << 16 ) & 0xFFFF0000 ) ) ;
}

void	GetGyroOffset( UINT_16* GyroOffsetX, UINT_16* GyroOffsetY, UINT_16* GyroOffsetZ )
{
	UINT_32	ReadValX, ReadValY, ReadValZ;
	RamRead32A( GYRO_RAM_GXOFFZ  , &ReadValX );	
	RamRead32A( GYRO_RAM_GYOFFZ  , &ReadValY );	
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

void	MeasFil( void )
{
	UINT_32	UlMeasFilaA , UlMeasFilaB , UlMeasFilaC ;
	UINT_32	UlMeasFilbA , UlMeasFilbB , UlMeasFilbC ;

	UlMeasFilaA	=	0x7FFFFFFF ;
	UlMeasFilaB	=	0x00000000 ;
	UlMeasFilaC	=	0x00000000 ;
	UlMeasFilbA	=	0x7FFFFFFF ;
	UlMeasFilbB	=	0x00000000 ;
	UlMeasFilbC	=	0x00000000 ;


	RamWrite32A ( 0x8388	, UlMeasFilaA ) ;
	RamWrite32A ( 0x8380	, UlMeasFilaB ) ;
	RamWrite32A ( 0x8384	, UlMeasFilaC ) ;

	RamWrite32A ( 0x8394	, UlMeasFilbA ) ;
	RamWrite32A ( 0x838C	, UlMeasFilbB ) ;
	RamWrite32A ( 0x8390	, UlMeasFilbC ) ;

	RamWrite32A ( 0x83A0	, UlMeasFilaA ) ;
	RamWrite32A ( 0x8398	, UlMeasFilaB ) ;
	RamWrite32A ( 0x839C	, UlMeasFilaC ) ;

	RamWrite32A ( 0x83AC	, UlMeasFilbA ) ;
	RamWrite32A ( 0x83A4	, UlMeasFilbB ) ;
	RamWrite32A ( 0x83A8	, UlMeasFilbC ) ;
}
void	MemoryClear( UINT_16 UsSourceAddress, UINT_16 UsClearSize )
{
	UINT_16	UsLoopIndex ;

	for ( UsLoopIndex = 0 ; UsLoopIndex < UsClearSize ;  ) {
		RamWrite32A( UsSourceAddress	, 	0x00000000 ) ;	
		UsSourceAddress += 4;
		UsLoopIndex += 4 ;
	}
}
void	SetTransDataAdr( UINT_16 UsLowAddress , UINT_32 UlLowAdrBeforeTrans )
{
	UnDwdVal	StTrsVal ;

	if( UlLowAdrBeforeTrans < 0x00009000 ){
		StTrsVal.StDwdVal.UsHigVal = (UINT_16)(( UlLowAdrBeforeTrans & 0x0000F000 ) >> 8 ) ;
		StTrsVal.StDwdVal.UsLowVal = (UINT_16)( UlLowAdrBeforeTrans & 0x00000FFF ) ;
	}else{
		StTrsVal.UlDwdVal = UlLowAdrBeforeTrans ;
	}
	RamWrite32A( UsLowAddress	,	StTrsVal.UlDwdVal );

}
#define 	ONE_MSEC_COUNT	15
void	SetWaitTime( UINT_16 UsWaitTime )
{
	RamWrite32A( 0x0324	, 0 ) ;
	RamWrite32A( 0x0328	, (UINT_32)(ONE_MSEC_COUNT * UsWaitTime)) ;
}
void	ClrMesFil( void )
{
	RamWrite32A ( 0x02D0	, 0 ) ;
	RamWrite32A ( 0x02D4	, 0 ) ;

	RamWrite32A ( 0x02D8	, 0 ) ;
	RamWrite32A ( 0x02DC	, 0 ) ;

	RamWrite32A ( 0x02E0	, 0 ) ;
	RamWrite32A ( 0x02E4	, 0 ) ;

	RamWrite32A ( 0x02E8	, 0 ) ;
	RamWrite32A ( 0x02EC	, 0 ) ;
}

void MeasAddressSelection( UINT_8 mode , INT_32 * measadr_a , INT_32 * measadr_b )
{
	if( mode == 0 ){
		*measadr_a		=	GYRO_RAM_GX_ADIDAT ;	
		*measadr_b		=	GYRO_RAM_GY_ADIDAT ;	
	}else if( mode == 1 ){
		*measadr_a		=	GYRO_ZRAM_GZ_ADIDAT ;	
		*measadr_b		=	ACCLRAM_Z_AC_ADIDAT ;	
	}else{
		*measadr_a		=	ACCLRAM_X_AC_ADIDAT ;	
		*measadr_b		=	ACCLRAM_Y_AC_ADIDAT ;	
	}
}
void	MeasureStart( INT_32 SlMeasureParameterNum , INT_32 SlMeasureParameterA , INT_32 SlMeasureParameterB )
{
	MemoryClear( 0x0278 , sizeof( MeasureFunction_Type ) ) ;
	RamWrite32A( 0x0280	 , 0x80000000 ) ;	
	RamWrite32A( 0x02A8	 , 0x80000000 ) ;	
	RamWrite32A( 0x0284	 , 0x7FFFFFFF ) ;	
	RamWrite32A( 0x02AC	 , 0x7FFFFFFF ) ;	

	SetTransDataAdr( 0x02A0	, ( UINT_32 )SlMeasureParameterA ) ;	
	SetTransDataAdr( 0x02C8	, ( UINT_32 )SlMeasureParameterB ) ;	
	RamWrite32A( 0x0278	 	, 0 ) ;									
	ClrMesFil() ;													
	SetWaitTime(1) ;
	RamWrite32A( 0x027C		, SlMeasureParameterNum ) ;		
}
void	MeasureWait( void )
{
	UINT_32	SlWaitTimerSt ;
	UINT_16	UsTimeOut = 2000;

	do {
		RamRead32A( 0x027C, &SlWaitTimerSt ) ;
		UsTimeOut--;
	} while ( SlWaitTimerSt && UsTimeOut );

}
void	SetSinWavGenInt( void )
{

	RamWrite32A( 0x02FC		,	0x00000000 ) ;	
	RamWrite32A( 0x0300		,	0x60000000 ) ;	
	RamWrite32A( 0x0304		,	0x00000000 ) ;	

	RamWrite32A( 0x0310		,	0x00000000 );	
	RamWrite32A( 0x0314 	,	0x00000000 );	
	RamWrite32A( 0x0318 	,	0x00000000 );	

	RamWrite32A( 0x02F4	,	0x00000000 ) ;								// Sine Wave Stop

}

#define 	MESOF_NUM		2048				
#define 	GYROFFSET_H		( 0x06D6 << 16 )	
#define		GSENS			( 4096 << 16 )		
#define		GSENS_MARG		(GSENS / 4)			
#define		POSTURETH		(GSENS - GSENS_MARG)	
#define		ZG_MRGN			(1310 << 16)			
#define		XYG_MRGN		(1024 << 16)			
UINT_32	MeasGyAcOffset(  void  )
{
	UINT_32	UlRsltSts;
	INT_32			SlMeasureParameterA , SlMeasureParameterB ;
	INT_32			SlMeasureParameterNum ;
	UnllnVal		StMeasValueA , StMeasValueB ;
	INT_32			SlMeasureAveValueA[3] , SlMeasureAveValueB[3] ;
	UINT_8			i ;

	
	
	MeasFil( ) ;

	SlMeasureParameterNum	=	MESOF_NUM ;
	
	for( i=0 ; i<3 ; i++ )
	{
		MeasAddressSelection( i, &SlMeasureParameterA , &SlMeasureParameterB );
	
		MeasureStart( SlMeasureParameterNum , SlMeasureParameterA , SlMeasureParameterB ) ;	
		
		MeasureWait() ;		
	
		RamRead32A( 0x0290 		, &StMeasValueA.StUllnVal.UlLowVal ) ;
		RamRead32A( 0x0290 + 4	, &StMeasValueA.StUllnVal.UlHigVal ) ;
		RamRead32A( 0x02B8 		, &StMeasValueB.StUllnVal.UlLowVal ) ;
		RamRead32A( 0x02B8 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;
	
		SlMeasureAveValueA[i] = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
		SlMeasureAveValueB[i] = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;
	
	}
	
	UlRsltSts = EXE_END ;
	
	
	if( abs(SlMeasureAveValueA[0]) > GYROFFSET_H )					UlRsltSts |= EXE_GXADJ ;
	if( abs(SlMeasureAveValueB[0]) > GYROFFSET_H ) 					UlRsltSts |= EXE_GYADJ ;
	if( abs(SlMeasureAveValueA[1]) > GYROFFSET_H ) 					UlRsltSts |= EXE_GZADJ ;
	if(    (SlMeasureAveValueB[1]) < POSTURETH )					UlRsltSts |= EXE_AZADJ ;
	if( abs(SlMeasureAveValueA[2]) > XYG_MRGN )						UlRsltSts |= EXE_AXADJ ;
	if( abs(SlMeasureAveValueB[2]) > XYG_MRGN )						UlRsltSts |= EXE_AYADJ ;
	if( abs( GSENS  - SlMeasureAveValueB[1]) > ZG_MRGN )			UlRsltSts |= EXE_AZADJ ;


	if( UlRsltSts == EXE_END ){
		RamWrite32A( GYRO_RAM_GXOFFZ ,		SlMeasureAveValueA[0] ) ;					
		RamWrite32A( GYRO_RAM_GYOFFZ ,		SlMeasureAveValueB[0] ) ;					
		RamWrite32A( GYRO_ZRAM_GZOFFZ ,		SlMeasureAveValueA[1] ) ;					
		RamWrite32A( ACCLRAM_X_AC_OFFSET ,	SlMeasureAveValueA[2] ) ;					
		RamWrite32A( ACCLRAM_Y_AC_OFFSET ,	SlMeasureAveValueB[2] ) ;					
		RamWrite32A( ACCLRAM_Z_AC_OFFSET , 	SlMeasureAveValueB[1] - (INT_32)GSENS ) ;	

		RamWrite32A( 0x01D8 , 		0x00000000 ) ;		
		RamWrite32A( 0x01FC , 		0x00000000 ) ;		
		RamWrite32A( 0x0378 , 		0x00000000 ) ;		
		RamWrite32A( 0x019C , 		0x00000000 ) ;		
		RamWrite32A( 0x01C4 , 		0x00000000 ) ;		
		RamWrite32A( 0x03C0 + 8 ,	0x00000000 ) ;		
		RamWrite32A( 0x03F0 + 8 ,	0x00000000 ) ;		
		RamWrite32A( 0x0420 + 8 ,	0x00000000 ) ;		
		RamWrite32A( 0x03C0 + 12 ,	0x00000000 ) ;		
		RamWrite32A( 0x03F0 + 12 ,	0x00000000 ) ;		
		RamWrite32A( 0x0420 + 12 ,	0x00000000 ) ;		
		RamWrite32A( 0x03C0 + 16 ,	0x00000000 ) ;		
		RamWrite32A( 0x03F0 + 16 ,	0x00000000 ) ;		
		RamWrite32A( 0x0420 + 16 ,	0x00000000 ) ;		
		RamWrite32A( 0x03C0 + 20 ,	0x00000000 ) ;		
		RamWrite32A( 0x03F0 + 20 ,	0x00000000 ) ;		
		RamWrite32A( 0x0420 + 20 ,	0x00000000 ) ;		
	}
	return( UlRsltSts );
	
		
}

const UINT_8 PACT0Tbl[] = { 0xFF, 0xFF };	/* Dummy table */
const UINT_8 PACT1Tbl[] = { 0x20, 0xDF };	/* [ACT_02][ACT_01][ACT_03][ACT_05] */


UINT_8 SetAngleCorrection( float DegreeGap, UINT_8 SelectAct, UINT_8 Arrangement )
{
	double OffsetAngle = 0.0f;
	double OffsetAngleV_slt = 0.0f;
//	double OffsetAngleS_slt = 0.0f;
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
//			OffsetAngle = (double)( DegreeGap ) * 3.141592653589793238 / 180.0f ;
//			UcCnvF = PACT1Tbl[ Arrangement ];
//			break;
//		case 0x02 :
//		case 0x03 :
//		case 0x05 :
//		case 0x06 :
//		case 0x07 :
//		case 0x08 :
//		case 0x09 :
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
	
	RamWrite32A( 0x8270 , 		(UINT_32)Slgx45x );
	RamWrite32A( 0x8274 , 		(UINT_32)Slgx45y );
	RamWrite32A( 0x82D0 , 		(UINT_32)Slgy45y );
	RamWrite32A( 0x82D4 , 		(UINT_32)Slgy45x );
	RamWrite32A( 0x8640 , 		(UINT_32)Slgx45x );
	RamWrite32A( 0x8644 , 		(UINT_32)Slgx45y );
	RamWrite32A( 0x8648 , 		(UINT_32)Slgy45y );
	RamWrite32A( 0x864C , 		(UINT_32)Slgy45x );
	
	if(SelectAct == 0x00) {
		OffsetAngleV_slt = (double)( 45.0f ) * 3.141592653589793238 / 180.0f ;
	}else{
		OffsetAngleV_slt = (double)( 0.0f ) * 3.141592653589793238 / 180.0f ;
	}
//	Slagx45x = (INT_32)( cos( OffsetAngleV_slt )*2147483647.0);
//	Slagx45y = (INT_32)(-sin( OffsetAngleV_slt )*2147483647.0);
//	Slagy45y = (INT_32)( cos( OffsetAngleV_slt )*2147483647.0);
//	Slagy45x = (INT_32)( sin( OffsetAngleV_slt )*2147483647.0);
	RamWrite32A( 0x86E8 , 			(UINT_32)Slagx45x );
	RamWrite32A( 0x86EC , 			(UINT_32)Slagx45y );
	RamWrite32A( 0x86F0 , 			(UINT_32)Slagy45y );
	RamWrite32A( 0x86F4 , 			(UINT_32)Slagy45x );

//	OffsetAngleS_slt = (double)( -90.0f ) * 3.141592653589793238 / 180.0f ;
//	Slagx45x = (INT_32)( cos( OffsetAngleS_slt )*2147483647.0);
//	Slagx45y = (INT_32)(-sin( OffsetAngleS_slt )*2147483647.0);
//	Slagy45y = (INT_32)( cos( OffsetAngleS_slt )*2147483647.0);
//	Slagy45x = (INT_32)( sin( OffsetAngleS_slt )*2147483647.0);
//	RamWrite32A( 0x86F8 , 			(UINT_32)Slagx45x );
//	RamWrite32A( 0x86FC , 			(UINT_32)Slagx45y );
//	RamWrite32A( 0x8700 , 			(UINT_32)Slagy45y );
//	RamWrite32A( 0x8704 , 			(UINT_32)Slagy45x );


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
	RamWrite32A( 0x865C , (UINT_32)Slgxx );
	RamWrite32A( 0x8660 , (UINT_32)Slgxy );
	RamWrite32A( 0x8664 , (UINT_32)Slgyy );
	RamWrite32A( 0x8668 , (UINT_32)Slgyx );
	RamWrite32A( 0x866C , (UINT_32)Slgzp );
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
	RamWrite32A( 0x8670 , (UINT_32)Slaxx );
	RamWrite32A( 0x8674 , (UINT_32)Slaxy );
	RamWrite32A( 0x8678 , (UINT_32)Slayy );
	RamWrite32A( 0x867C , (UINT_32)Slayx );
	RamWrite32A( 0x8680 , (UINT_32)Slazp );
}

UINT_8	RdStatus( UINT_8 UcStBitChk )
{
	UINT_32	UlReadVal ;

	RamRead32A( 0xF100 , &UlReadVal );
	if( UcStBitChk ){
		UlReadVal &= READ_STATUS_INI ;
	}
	if( !UlReadVal ){
		return( SUCCESS );
	}else{
		return( FAILURE );
	}
}
void	OisEna( void )	// OIS ( SMA , VCM ) = ( OFF, ON )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF012 , 0x00000001 ) ;
	while( UcStRd && (UlStCnt++ < CNT050MS )) {
		UcStRd = RdStatus(1);
	}
}
void	OisEna_S( void )	// OIS ( SMA , VCM ) = ( ON, OFF )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF012 , 0x00010000 ) ;
	while( UcStRd && (UlStCnt++ < CNT050MS )) {
		UcStRd = RdStatus(1);
	}
}
void	OisEna_SV( void )	// OIS ( SMA , VCM ) = ( ON, ON )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF012 , 0x00010001 ) ;
	while( UcStRd && (UlStCnt++ < CNT050MS )) {
		UcStRd = RdStatus(1);
	}
}

void	OisDis( void )	// OIS ( SMA , VCM ) = ( OFF, OFF )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF012 , 0x00000000 ) ;
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void	OisDis_Slope( void )	// OIS ( SMA , VCM ) = ( OFF, OFF )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF012 , 0x00000008 ) ;
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void	SetPanTiltMode( UINT_8 UcPnTmod )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	switch ( UcPnTmod ) {
		case 0 :
			RamWrite32A( 0xF011 ,	0x00000000 ) ;
			break ;
		case 1 :
			RamWrite32A( 0xF011 ,	0x00000001 ) ;
			break ;
	}

	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void	SscEna( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF01C , 0x00000001 ) ;
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void	SscDis( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF01C , 0x00000000 ) ;
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}



 #define		ACT_CHK_FRQ		0x0008B8E5	
 #define		ACT_CHK_NUM		3756		
 #define		ACT_THR			0x000003E8	
 #define		ACT_MARGIN		0.75f		
 
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
	
	return( UcRst ) ;
}


 #define		GEA_NUM			512				
 #define		GEA_DIF_HIG		0x0083			
 #define		GEA_DIF_LOW		0x0001			
 
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
	
		RamRead32A( 0x0290 		, &StMeasValueA.StUllnVal.UlLowVal ) ;	
		RamRead32A( 0x0290 + 4	, &StMeasValueA.StUllnVal.UlHigVal ) ;
		RamRead32A( 0x02B8 		, &StMeasValueB.StUllnVal.UlLowVal ) ;	
		RamRead32A( 0x02B8 + 4	, &StMeasValueB.StUllnVal.UlHigVal ) ;
	
		SlMeasureAveValueA = (INT_32)( (INT_64)StMeasValueA.UllnValue / SlMeasureParameterNum ) ;
		SlMeasureAveValueB = (INT_32)( (INT_64)StMeasValueB.UllnValue / SlMeasureParameterNum ) ;
		// 
		UsGxoVal[UcCnt] = (UINT_16)( SlMeasureAveValueA >> 16 );	
		
		// 
		UsGyoVal[UcCnt] = (UINT_16)( SlMeasureAveValueB >> 16 );	
		
		
		
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
	
	
	return( UcRst ) ;
}


void PreparationForPowerOff( void )
{
	UINT_32 UlReadVa;
		
	RamRead32A( 0x8004, &UlReadVa );
	if( (UINT_8)UlReadVa == 0x02 ){
		RamWrite32A( CMD_GYRO_WR_ACCS, 0x00027000 );
	}
}

void	SrvOn( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF010 , 0x00000003 ) ;

	while( UcStRd && ( UlStCnt++ < CNT200MS)) {
		UcStRd = RdStatus(1);
	}
}

void	SrvOff( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;
	
	RamWrite32A( 0xF010 , 0x00000000 ) ;

	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void VcmStandby( void )
{
	IOWrite32A( 0xD00078, 0x00000000 );
	IOWrite32A( 0xD00074, 0x00000010 );
	IOWrite32A( 0xD00004, 0x00000005 );
}

void VcmActive( void )
{
	IOWrite32A( 0xD00004, 0x00000007 );
	IOWrite32A( 0xD00074, 0x00000000 );
	IOWrite32A( 0xD00078, 0x00000F3F );
}

void SetStandbyMode( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	RamWrite32A( 0xF019 ,	0x00000001 ) ;
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}

void SetActiveMode( void )
{
	UINT_8	UcStRd = 1;
	UINT_32	UlStCnt = 0;

	IOWrite32A( 0xD01008 ,	0x00000090 ) ;
	RamWrite32A( 0xF019 ,	0x00000000 ) ;	
	while( UcStRd && ( UlStCnt++ < CNT050MS)) {
		UcStRd = RdStatus(1);
	}
}



UINT_8 LoadUserAreaToPM( void )
{
	DOWNLOAD_TBL_EXT* ptr ;
	UINT_32	UlReadVernum;
	UINT_8 ModuleVendor;
	UINT_8 ActVer;
	UINT_8 MasterSlave;
	
	ptr = ( DOWNLOAD_TBL_EXT * )DTbl ;

	RamRead32A( 0x8000 , &UlReadVernum );
	switch( UlReadVernum & 0xFF000000 ){
	case 0x01000000:
		ModuleVendor = 0x01;
		break;
	default:
		return 0xF0 ;
	}
	RamRead32A( 0x8004 , &UlReadVernum );
	ActVer		= (UINT_8)((UlReadVernum & 0x0000FF00) >> 8);
	MasterSlave	= (UINT_8)((UlReadVernum & 0xFF000000) >> 24);
	do {
		if( ptr->Index == ( ((UINT_32)ModuleVendor<<16) + ((UINT_32)ActVer<<8) + MasterSlave ) ) {

			if( ( ptr->SizeFromCode % 64 ) != 0 )	return (0xF1) ;

			return LoadUareToPM( ptr , 0 );
		}
		ptr++ ;
	} while (ptr->Index != 0xFFFF ) ;

	return 0xF0 ;
}

UINT_8 LoadUareToPM( DOWNLOAD_TBL_EXT* ptr , UINT_8 mode )
{
	UINT_8 ans=0;
	UINT_32	UlReadVal=0;
	UINT_32	UlCnt=0;
	
	if( !mode ){
		RamWrite32A( 0xE000 , 0x00000000 );		// to boot
		WitTim( 15 ) ;													// Bootプログラムを回すのに15msec必要。
		IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
		if( UlReadVal != 0x0B ){
			IOWrite32A( SYSDSP_REMAP,				0x00001400 ) ;		// CORE_RST[12], MC_IGNORE2[10] = 1
			WitTim( 15 ) ;												// Bootプログラムを回すのに15msec必要。
			IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
			if( UlReadVal != 0x0B) {
				return( 0x02 );
			}
		}
		
	}
	
	ans = PmemUpdate128( ptr );				// Update the special program for updating the flash memory.
	if(ans != 0) return( ans );

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0xF007, 0x00000000 );		// boot command Access Setup
	RamWrite32A( 0x5004 , 0x00000000 );		// Trans user data from flash memory to pm
	
	do{
		WitTim( 1 );
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x10) ;									// trans ng
		}
		RamRead32A( 0x5004, &UlReadVal );					// PmCheck.ExecFlagの読み出し
	}while ( UlReadVal != 0 );
	IOWrite32A( 0xE0701C , 0x00000002);
	
	return( 0 );
}
UINT_8	RdBurstUareaFromPm( UINT_32 UlAddress, UINT_8 *PucData , UINT_8 UcLength , UINT_8 mode )
{
	if(!UcLength)	return(0xff);
	if( !mode ){
		RamWrite32A( 0x5000 , UlAddress );
	}
	RamWrite32A( 0x5002 , (UINT_32)UcLength );
	WitTim( 1 ) ;													// 1ms Wait for prepare trans data
	CntRd( 0x5002 , PucData , (UINT_16)UcLength+1);

	return( 0 );
}
UINT_8	RdSingleUareaFromPm( UINT_32 UlAddress, UINT_8 *PucData , UINT_8 UcLength , UINT_8 mode )
{
	UINT_32	ReadData;
	UINT_8 i;

	if(!UcLength)	return(0xff);
	if( !mode ){
		RamWrite32A( 0x5000 , UlAddress );
	}
	for( i=0 ; i <= UcLength ;  )
	{
		RamRead32A( 0x5001 , &ReadData );
		PucData[i++] = (UINT_8)(ReadData >> 24);
		PucData[i++] = (UINT_8)(ReadData >> 16);
		PucData[i++] = (UINT_8)(ReadData >> 8);
		PucData[i++] = (UINT_8)(ReadData >> 0);
	}

	return( 0 );
}
	



