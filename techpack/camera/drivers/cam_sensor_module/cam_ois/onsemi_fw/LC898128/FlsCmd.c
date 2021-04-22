//********************************************************************************
//
//		<< LC898128 Evaluation Soft >>
//	    Program Name	: FlsCmd.c
//		Design			: K.abe
//		History			: First edition
//********************************************************************************
#define		__OISFLSH__
//**************************
//	Include Header File
//**************************
#include	"Ois.h"
//#include 	<stdlib.h>
//#include	<math.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#define SEL_MODEL 0


#if (SELECT_VENDOR == 0x01)			// SUNNY FAB only

#define BURST_LENGTH_UC 		( 12*20 ) 	// 240 Total:242Byte
#define BURST_LENGTH_FC 		(  3*64 ) 	// 192 Total:194~195Byte

#else

/* Burst Length for updating to PMEM Max:256*/
#define BURST_LENGTH_UC 		( 20 ) 	// 120 Total:122Byte
//#define BURST_LENGTH_UC 		( 6*20 ) 	// 120 Total:122Byte
/* Burst Length for updating to Flash */
//#define BURST_LENGTH_FC 		( 32 )	 	// 32 Total: 34~35Byte
#define BURST_LENGTH_FC 		( 64 )	 	// 64 Total: 66~67Byte

#endif

//****************************************************
//	CUSTOMER NECESSARY CREATING FUNCTION LIST
//****************************************************
/* for I2C communication */
extern	void RamWrite32A( UINT_16, UINT_32 );
extern 	void RamRead32A( UINT_16, void * );
/* for I2C Multi Translation : Burst Mode*/
extern 	void CntWrt( void *, UINT_16) ;
extern	void CntRd( UINT_32, void *, UINT_16 ) ;

/* for Wait timer [Need to adjust for your system] */
extern void	WitTim( UINT_16 );

//**************************
//	Local Function Prototype
//**************************
UINT_8	FlashSingleRead( UINT_8 SelMat, UINT_32 UlAddress, UINT_32 *PulData );
UINT_8	FlashMultiRead( UINT_8 SelMat, UINT_32 UlAddress, UINT_32 *PulData , UINT_8 UcLength );
INT_32	lstsq( double x[], double y[], INT_32 , INT_32 , double c[] );

extern  UINT_8 GetInfomationAfterStartUp( DSPVER* Info );

extern const LINCRS ACT01_LinCrsParameter;

//********************************************************************************
// Function Name 	: IOWrite32A
// Retun Value		: None
// Argment Value	: IOadrs, IOdata
// Explanation		: Write data to IO area Command
// History			: First edition
//********************************************************************************
void IORead32A( UINT_32 IOadrs, UINT_32 *IOdata )
{
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamRead32A ( CMD_IO_DAT_ACCESS, IOdata ) ;
}

//********************************************************************************
// Function Name 	: IOWrite32A
// Retun Value		: None
// Argment Value	: IOadrs, IOdata
// Explanation		: Write data to IO area Command
// History			: First edition
//********************************************************************************
void IOWrite32A( UINT_32 IOadrs, UINT_32 IOdata )
{
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, IOdata ) ;
}

//********************************************************************************
// Function Name 	: UnlockCodeSet
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: <Flash Memory> Unlock Code Set
// History			: First edition
//********************************************************************************
UINT_8 UnlockCodeSet( void )
{
	UINT_32 UlReadVal, UlCnt=0;

	do {
		IOWrite32A( 0xE07554, 0xAAAAAAAA );							// UNLK_CODE1(E0_7554h) = AAAA_AAAAh
		IOWrite32A( 0xE07AA8, 0x55555555 );							// UNLK_CODE2(E0_7AA8h) = 5555_5555h
		IORead32A( 0xE07014, &UlReadVal );
		if( (UlReadVal & 0x00000080) != 0 )	return ( 0 ) ;			// Check UNLOCK(E0_7014h[7]) ?
		WitTim( 1 );
	} while( UlCnt++ < 10 );
	return ( 1 );
}

//********************************************************************************
// Function Name 	: UnlockCodeClear
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: <Flash Memory> Clear Unlock Code
// History			: First edition
//********************************************************************************
UINT_8 UnlockCodeClear(void)
{
	UINT_32 UlDataVal, UlCnt=0;

	do {
		IOWrite32A( 0xE07014, 0x00000010 );							// UNLK_CODE3(E0_7014h[4]) = 1
		IORead32A( 0xE07014, &UlDataVal );
		if( (UlDataVal & 0x00000080) == 0 )	return ( 0 ) ;			// Check UNLOCK(E0_7014h[7]) ?
		WitTim( 1 );
	} while( UlCnt++ < 10 );
	return ( 3 );
}


//********************************************************************************
// Function Name 	: WritePermission
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: LC898128 Command
// History			: First edition 						2018.05.15
//********************************************************************************
void WritePermission( void )
{
	IOWrite32A( 0xE074CC, 0x00000001 );								// RSTB_FLA_WR(E0_74CCh[0])=1
	IOWrite32A( 0xE07664, 0x00000010 );								// FLA_WR_ON(E0_7664h[4])=1
}

//********************************************************************************
// Function Name 	: AddtionalUnlockCodeSet
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: LC898128 Command
// History			: First edition 						2018.05.15
//********************************************************************************
void AddtionalUnlockCodeSet( void )
{
	IOWrite32A( 0xE07CCC, 0x0000ACD5 );								// UNLK_CODE3(E0_7CCCh) = 0000_ACD5h
}

//********************************************************************************
// Function Name 	: CoreResetwithoutMC128
// Retun Value		: 0:Non error 
// Argment Value	: None
// Explanation		: Program code Update to PMEM directly
// History			: First edition
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

	WitTim(100);//neo: need to reduce this time

	IOWrite32A( 0xE0701C, 0x00000002);
	IOWrite32A( 0xE07014, 0x00000010);

	IOWrite32A( 0xD00060, 0x00000001 ) ;
	WitTim( 15 ) ;//neo: need to reduce this time

	IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
	switch ( (UINT_8)UlReadVal ){
	case 0x08:
	case 0x0D:
		break;
	
	default:	
		return( 0xE0  | (UINT_8)UlReadVal );
	}

#if 0 // 1MHz Test
	IOWrite32A( 0xD00014, 0x00000002);
	IOWrite32A( 0xE07020, 0x00000014);
#endif
	return( 0 );
}

//********************************************************************************
// Function Name 	: PmemUpdate128
// Retun Value		: 0:Non error 
// Argment Value	: None
// Explanation		: Program code Update to PMEM directly
// History			: First edition
//********************************************************************************
UINT_8 PmemUpdate128( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_8	data[BURST_LENGTH_UC +2 ];
	UINT_16	Remainder;	// 余り
	const UINT_8 *NcDataVal = ptr->UpdataCode;
	UINT_8	ReadData[8];
	long long CheckSumCode = ptr->SizeUpdataCodeCksm;
	UINT_8 *p = (UINT_8 *)&CheckSumCode;
	UINT_32 i, j;
	UINT_32	UlReadVal, UlCnt , UlNum ;
//--------------------------------------------------------------------------------
// 1. Write updata code to Pmem 
//--------------------------------------------------------------------------------
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	RamWrite32A( 0x3000, 0x00080000 );		// Pmem address set

	// Pmem data burst write
	data[0] = 0x40; // CmdH
	data[1] = 0x00; // CmdL

	// XXbyte毎の転送
	Remainder = ( (ptr->SizeUpdataCode*5) / BURST_LENGTH_UC ); 
	for(i=0 ; i< Remainder ; i++)
	{
		UlNum = 2;
		for(j=0 ; j < BURST_LENGTH_UC; j++){
			data[UlNum] =  *NcDataVal++;
//			if( ( j % 5) == 4)	TRACE("\n");
			UlNum++;
		}
		
		CntWrt( data, BURST_LENGTH_UC+2 );  // Cmd 2Byte.
	}
	Remainder = ( (ptr->SizeUpdataCode*5) % BURST_LENGTH_UC); 
	if (Remainder != 0 )
	{
		UlNum = 2;
		for(j=0 ; j < Remainder; j++){
			data[UlNum++] = *NcDataVal++;
		}
		CntWrt( data, Remainder+2 );  		// Cmd 2Byte
	}
	
//--------------------------------------------------------------------------------
// 2. Verify
//--------------------------------------------------------------------------------

	// Program RAMのCheckSumの起動
	data[0] = 0xF0;											//CmdID
	data[1] = 0x0E;											//CmdID
	data[2] = (unsigned char)((ptr->SizeUpdataCode >> 8) & 0x000000FF);	//書き込みデータ(MSB)
	data[3] = (unsigned char)(ptr->SizeUpdataCode & 0x000000FF);			//書き込みデータ
	data[4] = 0x00;											//書き込みデータ
	data[5] = 0x00;											//書き込みデータ(LSB)

	CntWrt( data, 6 ) ;

	// CheckSumの終了判定
	UlCnt = 0;
	do{
		WitTim( 1 );
		if( UlCnt++ > 10 ) {
			IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
			return (0x21) ;									// No enough memory
		}
		RamRead32A( 0x0088, &UlReadVal );					// PmCheck.ExecFlagの読み出し
	}while ( UlReadVal != 0 );

#if 0
	// CheckSum値の読み出し
	data[0] = 0xF0;											// CmdID
	data[1] = 0x0E;											// CmdID
	CntWrt( data, 2 ) ;
	CntRd( 0, ReadData , 8 );
#else
	CntRd( 0xF00E, ReadData , 8 );
#endif
	
	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
	// CheckSum値の判定(期待値は、Headerにdefineされている)
	for( i=0; i<8; i++) {
		if(ReadData[7-i] != *p++ ) {  							// CheckSum Codeの判定
TRACE("[2] PMEM verify Error %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",ReadData[0],ReadData[1],ReadData[2],ReadData[3],ReadData[4],ReadData[5],ReadData[6],ReadData[7]);
			return (0x22) ;					// verify ng
		}
	}
TRACE("[2] PMEM verify End\n");
	return( 0 );
}
//********************************************************************************
// Function Name 	: EraseUserMat128
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: User Mat All Erase

// History			: First edition
//********************************************************************************
UINT_8 EraseUserMat128(UINT_8 StartBlock, UINT_8 EndBlock )
{
	UINT_32 i;
	UINT_32	UlReadVal, UlCnt ;

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0xF007, 0x00000000 );					// FlashAccess Setup

	//***** User Matのブロック消去 *****
	for( i=StartBlock ; i<EndBlock ; i++) {
		RamWrite32A( 0xF00A, ( i << 10 ) );				// FromCmd.Addrの設定
		RamWrite32A( 0xF00C, 0x00000020 );				// FromCmd.Controlの設定(ブロック消去)

		// ブロック消去の終了判定
		WitTim( 5 );
		UlCnt = 0;
		do{
//			WitTim( 4 );
			WitTim( 1 );
			if( UlCnt++ > 10 ){
				IOWrite32A( 0xE0701C , 0x00000002);
				return (0x31) ;				// block erase timeout ng
			}
			RamRead32A( 0xF00C, &UlReadVal );					// FromCmd.Controlの読み出し
		}while ( UlReadVal != 0 );
	}
	IOWrite32A( 0xE0701C , 0x00000002);
	return(0);

}

//********************************************************************************
// Function Name 	: ProgramFlash128_LongBurst
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: User Mat All Erase

// History			: First edition
//********************************************************************************
UINT_8 ProgramFlash128_LongBurst( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_32	UlReadVal, UlCnt , UlNum ;
	UINT_8	data[(BURST_LENGTH_FC + 2)];
	UINT_32 i, j;
	UINT_16	Remainder;	// 余り
	const UINT_8 *NcFromVal = ptr->FromCode+BURST_LENGTH_FC;
	const UINT_8 *NcFromVal1st = ptr->FromCode;
	UINT_8 UcOddEvn;

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0x067C, 0x000800ac );		// F008 Update
	RamWrite32A( 0x0680, 0x000800be );		// F009 Update

	RamWrite32A( 0xF007, 0x00000000 );						// FlashAccess Setup
//	RamWrite32A( 0xF00A, 0x00000000 );						// FromCmd.Addrの設定
	RamWrite32A( 0xF00A, 0x00000030 );						// FromCmd.Addrの設定

	data[0] = 0xF0;						// CmdH
	data[1] = 0x08;						// CmdL

	for(i=1 ; i< ( ptr->SizeFromCode / BURST_LENGTH_FC ) ; i++)
	{
		if( ++UcOddEvn >1 )  	UcOddEvn = 0;	// 奇数偶数Check
		if (UcOddEvn == 0) data[1] = 0x08;
		else 			   data[1] = 0x09;		

		UlNum = 2;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}

		UlCnt = 0;
		if(UcOddEvn == 0){
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 100 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( (UlReadVal & 0x00000004) != 0 );			
		}else{
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 100 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( (UlReadVal & 0x00000008) != 0 );	
		}

		CntWrt( data, BURST_LENGTH_FC+2 );  // Cmd 2Byte.
	}
	Remainder = ( ptr->SizeFromCode % BURST_LENGTH_FC ) / 64;
	for(i=0 ; i< Remainder ; i++)
	{
		if( ++UcOddEvn >1 )  	UcOddEvn = 0;	// 奇数偶数Check
		if (UcOddEvn == 0) data[1] = 0x08;
		else 			   data[1] = 0x09;		

		UlNum = 2;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}

		UlCnt = 0;
		if(UcOddEvn == 0){
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 100 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( (UlReadVal & 0x00000004) != 0 );			
		}else{
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 100 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( (UlReadVal & 0x00000008) != 0 );	
		}

		CntWrt( data, BURST_LENGTH_FC+2 );  // Cmd 2Byte.
	}	
	UlCnt = 0;
	do{															// 書き込みの終了判定
//		WitTim( 4 );
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x42) ;				// write ng
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	

	/* write magic code */
	RamWrite32A( 0xF00A, 0x00000000 );						// FromCmd.Addrの設定
	data[1] = 0x08;						// CmdL
	UlNum = 2;
	for(j=0 ; j < BURST_LENGTH_FC; j++){
		data[UlNum++] = *NcFromVal1st++;
	}

	UlCnt = 0;
	do{															// 書き込みの終了判定
		RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
		if( UlCnt++ > 100 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x41) ;				// write ng
		}
	}while ( UlReadVal != 0 );			

	CntWrt( data, BURST_LENGTH_FC+2 );  // Cmd 2Byte.

	UlCnt = 0;
	do{															// 書き込みの終了判定
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x42) ;				// write ng
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	
	
	IOWrite32A( 0xE0701C , 0x00000002);
	return( 0 );
}

//********************************************************************************
// Function Name 	: ProgramFlash128_Standard
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: User Mat All Erase

// History			: First edition
//********************************************************************************
#if (BURST_LENGTH_FC == 32) || (BURST_LENGTH_FC == 64)
UINT_8 ProgramFlash128_Standard( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_32	UlReadVal, UlCnt , UlNum ;
	UINT_8	data[(BURST_LENGTH_FC + 3)];
	UINT_32 i, j;

	const UINT_8 *NcFromVal = ptr->FromCode + 64;
	const UINT_8 *NcFromVal1st = ptr->FromCode;
	UINT_8 UcOddEvn;

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0xF007, 0x00000000 );						// FlashAccess Setup
	RamWrite32A( 0xF00A, 0x00000010 );						// FromCmd.Addrの設定
	data[0] = 0xF0;						// CmdH
	data[1] = 0x08;						// CmdL
	data[2] = 0x00;						// FromCmd.BufferAのアドレス

	for(i=1 ; i< ( ptr->SizeFromCode / 64 ) ; i++)
	{
		if( ++UcOddEvn >1 )  	UcOddEvn = 0;	// 奇数偶数Check
		if (UcOddEvn == 0) data[1] = 0x08;
		else 			   data[1] = 0x09;		
TRACE("[%d]UcOddEvn= %d , data[1]= %d \n", i, data[1], NcFromVal );

#if (BURST_LENGTH_FC == 32)
		// 32Byteならば、2回に分けて送らないといけない。
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
	  	data[2] = 0x20;		//+32Byte
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
#elif (BURST_LENGTH_FC == 64)
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
#endif

		RamWrite32A( 0xF00B, 0x00000010 );							// FromCmd.Lengthの設定
		UlCnt = 0;
		if (UcOddEvn == 0){
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 250 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( UlReadVal != 0 );
		 	RamWrite32A( 0xF00C, 0x00000004 );	// FromCmd.Controlの設定(書き込み)
		}else{
			do{															// 書き込みの終了判定
				RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
				if( UlCnt++ > 250 ) {
					IOWrite32A( 0xE0701C , 0x00000002);
					return (0x41) ;				// write ng
				}
			}while ( UlReadVal != 0 );
			RamWrite32A( 0xF00C, 0x00000008 );	// FromCmd.Controlの設定(書き込み)
		}			
	}
	
	UlCnt = 0;	
	do{															// 書き込みの終了判定
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
		if( UlCnt++ > 250 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x41) ;				// write ng
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	
	
	{	/* write magic code */
		RamWrite32A( 0xF00A, 0x00000000 );						// FromCmd.Addrの設定
		data[1] = 0x08;
//		data[1] = 0x09;
TRACE("[%d]UcOddEvn= %d , data[1]= %d \n", 0, data[1], NcFromVal1st );

#if (BURST_LENGTH_FC == 32)
		// 32Byteならば、2回に分けて送らないといけない。
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
	  	data[2] = 0x20;		//+32Byte
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
#elif (BURST_LENGTH_FC == 64)
		data[2] = 0x00;
		UlNum = 3;
		for(j=0 ; j < BURST_LENGTH_FC; j++){
			data[UlNum++] = *NcFromVal1st++;
		}
		CntWrt( data, BURST_LENGTH_FC+3 );  // Cmd 3Byte.
#endif

		RamWrite32A( 0xF00B, 0x00000010 );							// FromCmd.Lengthの設定
		UlCnt = 0;
		do{															// 書き込みの終了判定
			RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
			if( UlCnt++ > 250 ) {
				IOWrite32A( 0xE0701C , 0x00000002);
				return (0x41) ;				// write ng
			}
		}while ( UlReadVal != 0 );
	 	RamWrite32A( 0xF00C, 0x00000004 );	// FromCmd.Controlの設定(書き込み)
//	 	RamWrite32A( 0xF00C, 0x00000008 );	// FromCmd.Controlの設定(書き込み)
	}
	
	UlCnt = 0;	
	do{															// 書き込みの終了判定
		WitTim( 1 );	
		RamRead32A( 0xF00C, &UlReadVal );						// FromCmd.Controlの読み出し
		if( UlCnt++ > 250 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x41) ;				// write ng
		}
	}while ( (UlReadVal & 0x0000000C) != 0 );	

	IOWrite32A( 0xE0701C , 0x00000002);
	return( 0 );
}
#endif


//********************************************************************************
// Function Name 	: CheckDrvOffAdj
// Retun Value		: Driver Offset Re-adjustment 
// Argment Value	: NON
// Explanation		: Driver Offset Re-adjustment 
// History			: First edition 						
//********************************************************************************
//extern UINT_8 CoreResetwithoutMC128( void );
//extern void IOWrite32A( UINT_32 IOadrs, UINT_32 IOdata );
//extern void IORead32A( UINT_32 IOadrs, UINT_32 *IOdata );
//extern UINT_8 PmemUpdate128( DOWNLOAD_TBL_EXT* ptr );


UINT_32 CheckDrvOffAdj( void )
{
	UINT_32 UlReadDrvOffx, UlReadDrvOffy,  UlReadDrvOffaf;

	IOWrite32A( FLASHROM_ACSCNT, 2 ); //3word
	IOWrite32A( FLASHROM_FLA_ADR, ((UINT_32)INF_MAT1 << 16) | 0xD );
	
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	IOWrite32A( FLASHROM_CMD, 0x00000001 );
	IORead32A( FLASHROM_FLA_RDAT, &UlReadDrvOffaf ) ;  	// #13
	IORead32A( FLASHROM_FLA_RDAT, &UlReadDrvOffx ) ;  	// #14
	IORead32A( FLASHROM_FLA_RDAT, &UlReadDrvOffy ) ;	// #15

	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
	if( ((UlReadDrvOffx & 0x000FF00) == 0x0000100)
		|| ((UlReadDrvOffy & 0x000FF00) == 0x0000100) 
		|| ((UlReadDrvOffaf & 0x000FF00) == 0x0000800) ){		//error
		return( 0x93 );
	}
	
	if( ((UlReadDrvOffx & 0x0000080) == 0)
	 && ((UlReadDrvOffy & 0x00000080) ==0)
	 && ((UlReadDrvOffaf & 0x00008000) ==0) ){
	
#if 0 //Erase : Test for repeatablity 	
		if( FlashBlockErase( INF_MAT1 , 0 ) != 0 )	return( 0x92 ) ;	// Erase Error	
		return( 1 ); 	// 0 : Uppdated	
#else	
		return( 0 ); 	// 0 : Uppdated
#endif
	}
	return( 1 );		// 1 : Still not.
}

//********************************************************************************
// Function Name 	: DrvOffAdj
// Retun Value		: Driver Offset Re-adjustment 
// Argment Value	: NON
// Explanation		: Driver Offset Re-adjustment 
// History			: First edition 						
//********************************************************************************
#include	"PmemCode128.h"
const DOWNLOAD_TBL_EXT Adj_Dtbl[] = {
	{0xEEEE, 0, 0, LC898128_PM, LC898128_PmemCodeSize,  LC898128_PmemCodeCheckSum, (void*)0, 0, 0, 0}
};


UINT_32 DrvOffAdj( void )
{
	UINT_8 ans=0;
	UINT_32 UlReadVal;
TRACE("DrvOffAdj \n");
	
//Infomatの確認。もしRe-Adjustしていないなら再実行。
	ans = CheckDrvOffAdj();
	if( ans == 1 ){

	 	ans = CoreResetwithoutMC128();		// Start up to boot exection
	 	if(ans != 0) return( ans );	

	 	ans = PmemUpdate128( (DOWNLOAD_TBL_EXT *)Adj_Dtbl );			// Update the special program for updating the flash memory.
		if(ans != 0) return( ans );

		IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
		RamWrite32A( 0xF001,  0x00000000 ) ;		// 
		WitTim( 1 ) ;	

		IOWrite32A( 0xE07CCC, 0x0000C5AD );					// additional unlock for INFO
		IOWrite32A( 0xE07CCC, 0x0000ACD5 );								// UNLK_CODE3(E0_7CCCh) = 0000_ACD5h
		WitTim( 10 ) ;//neo: need to reduce this time	

		IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
		IOWrite32A( SYSDSP_REMAP,				0x00001000 ) ;		// CORE_RST[12], MC_IGNORE2[10] = 1 PRAMSEL[7:6]=01b
		WitTim( 15 ) ;												// Bootプログラムを回すのに15msec必要。
//neo: need to reduce this time
		IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
TRACE("[%08x]DrvOffAdj  \n",(unsigned int)UlReadVal );
		if( UlReadVal != 0x08)		return( 0x90 );

		ans = CheckDrvOffAdj();

	}
	return(ans);
}

//********************************************************************************
// Function Name 	: FlashUpdate128
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: Flash Update for LC898128
// History			: First edition
//********************************************************************************
UINT_8 FlashUpdate128( DOWNLOAD_TBL_EXT* ptr )
{
	UINT_8 ans=0;
	UINT_32	UlReadVal, UlCnt ;
	
//--------------------------------------------------------------------------------
// 0. <Info Mat1> Driver Offset
//--------------------------------------------------------------------------------
	ans =  DrvOffAdj();
 	if(ans != 0) return( ans );	

 	ans = CoreResetwithoutMC128();		// Start up to boot exection
 	if(ans != 0) return( ans );	

	ans = Mat2ReWrite();				// MAT2 re-write process
 	if(ans != 0 && ans != 1) return( ans );	
	
 	ans = PmemUpdate128( ptr );			// Update the special program for updating the flash memory.
	if(ans != 0) return( ans );

//--------------------------------------------------------------------------------
// <User Mat> Erase
//--------------------------------------------------------------------------------
	if( UnlockCodeSet() != 0 ) 		return (0x33) ;				// Unlock Code set ng
	WritePermission();									// Write Permission
	AddtionalUnlockCodeSet();							// Additional Unlock Code Set

#if	(SEL_MODEL == 0x10)
 	ans = EraseUserMat128(0, 10); // Full Block.
#else
 	ans = EraseUserMat128(0, 7);  // 0-6 Block for use user area.
#endif
	if(ans != 0){
		if( UnlockCodeClear() != 0 ) 	return (0x32) ;				// unlock code clear ng
		else					 		return( ans );
	}

//	if( UnlockCodeClear() != 0 ) 	return (0x32) ;				// unlock code clear ng

//--------------------------------------------------------------------------------
// 4. <User Mat> Write
//--------------------------------------------------------------------------------
//	if( UnlockCodeSet() != 0 ) 	return (0x44) ;				// Unlock Code set ng
//	WritePermission();										// Write Permission
//	AddtionalUnlockCodeSet();								// Additional Unlock Code Set

#if (SELECT_VENDOR == 0x01)			// SUNNY FAB only
 	ans = ProgramFlash128_LongBurst( ptr );
	if(ans != 0){
		if( UnlockCodeClear() != 0 ) 	return (0x43) ;				// unlock code clear ng
		else					 		return( ans );
	}
#else
 	ans = ProgramFlash128_Standard( ptr );
	if(ans != 0){
		if( UnlockCodeClear() != 0 ) 	return (0x43) ;				// unlock code clear ng
		else					 		return( ans );
	}
#endif
TRACE("[%d]ProgramFlash end \n", ans );

	if( UnlockCodeClear() != 0 ) 	return (0x43) ;				// unlock code clear ng

//--------------------------------------------------------------------------------
// 5. <User Mat> Verify
//--------------------------------------------------------------------------------

	IOWrite32A( 0xE0701C , 0x00000000);
	RamWrite32A( 0xF00A, 0x00000000 );									// FromCmd.Addrの設定
	RamWrite32A( 0xF00D, ptr->SizeFromCodeValid );						// 有効CheckSumサイズの設定

	RamWrite32A( 0xF00C, 0x00000100 );									// FromCmd.Controlの設定(CheckSum実行)
	WitTim( 6 );
	UlCnt = 0;
	do{																	// 書き込みの終了判定
		RamRead32A( 0xF00C, &UlReadVal );								// FromCmd.Controlの読み出し
		if( UlCnt++ > 10 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			return (0x51) ;				// check sum excute ng
		}
		WitTim( 1 );		
	}while ( UlReadVal != 0 );

	RamRead32A( 0xF00D, &UlReadVal );									// CheckSum値の読み出し

	if( UlReadVal != ptr->SizeFromCodeCksm ) {
		IOWrite32A( 0xE0701C , 0x00000002);
		return( 0x52 );
	}

TRACE("[]UserMat Verify OK \n" );
//	CoreReset
	IOWrite32A( SYSDSP_REMAP,				0x00001000 ) ;		// CORE_RST[12], MC_IGNORE2[10] = 1 PRAMSEL[7:6]=01b
	WitTim( 15 ) ;												// Bootプログラムを回すのに15msec必要。
	//neo: need to reduce this time
	IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
	if( UlReadVal != 0x0A)		return( 0x53 );

TRACE("[]Remap OK \n" );
	return ( 0 );

}

//********************************************************************************
// Function Name 	: FlashBlockErase
// Retun Value		: 0 : Success, 1 : Unlock Error, 2 : Time Out Error
// Argment Value	: Use Mat , Flash Address
// Explanation		: <Flash Memory> Block Erase
// History			: First edition 						
// Unit of erase	: informaton mat  128 Byte
//					: user mat         4k Byte
//********************************************************************************
UINT_8	FlashBlockErase( UINT_8 SelMat , UINT_32 SetAddress )
{
	UINT_32	UlReadVal, UlCnt;
	UINT_8	ans	= 0 ;

	// fail safe
	// reject irregular mat
	if( SelMat != USER_MAT && SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )	return 10;	// INF_MAT2もAccessしない
	// reject command if address inner NVR3
	if( SetAddress > 0x000003FF )											return 9;

	// Flash write準備
	ans	= UnlockCodeSet();
	if( ans != 0 )	return( ans ) ;							// Unlock Code Set

	WritePermission();										// Write permission
	if( SelMat != USER_MAT ){
		if( SelMat == INF_MAT2 )	IOWrite32A( 0xE07CCC, 0x00006A4B );
		else						IOWrite32A( 0xE07CCC, 0x0000C5AD );		// additional unlock for INFO
	}
	AddtionalUnlockCodeSet();								// common additional unlock code set
	
	IOWrite32A( FLASHROM_FLA_ADR, ((UINT_32)SelMat << 16) | ( SetAddress & 0x00003C00 )) ;
	// Sector Erase Start
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	IOWrite32A( FLASHROM_CMD, 4 ) ;

	WitTim( 5 ) ;

	UlCnt	= 0 ;

	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( FLASHROM_FLAINT, &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;

	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
	ans	= UnlockCodeClear();								// Unlock Code Clear
	if( ans != 0 )	return( ans ) ;							// Unlock Code Set

	return( ans ) ;
}

//********************************************************************************
// Function Name 	: FlashSingleRead
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: <Flash Memory> Flash Single Read( 4 Byte read )
// History			: First edition
//********************************************************************************
UINT_8	FlashSingleRead( UINT_8 SelMat, UINT_32 UlAddress, UINT_32 *PulData )
{

	// fail safe
	// reject irregular mat
	if( SelMat != USER_MAT && SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )	return 10;	// INF_MAT2もAccessしない
	// reject command if address inner NVR3
	if( UlAddress > 0x000003FF )											return 9;
	
	IOWrite32A( FLASHROM_ACSCNT, 0x00000000 );
	IOWrite32A( FLASHROM_FLA_ADR, ((UINT_32)SelMat << 16) | ( UlAddress & 0x00003FFF ) );
	
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	IOWrite32A( FLASHROM_CMD, 0x00000001 );
	
	IORead32A( FLASHROM_FLA_RDAT, PulData ) ;
	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);

	return( 0 ) ;
}

//********************************************************************************
// Function Name 	: FlashMultiRead
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: <Flash Memory> Flash Multi Read( 4 Byte * length  max read : 128byte)
// History			: First edition
//********************************************************************************
UINT_8	FlashMultiRead( UINT_8 SelMat, UINT_32 UlAddress, UINT_32 *PulData , UINT_8 UcLength )
{
	UINT_8	i	 ;

	// fail safe
	// reject irregular mat
	if( SelMat != USER_MAT && SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )	return 10;	// INF_MAT2はRead only Accessしない
	// reject command if address inner NVR3
	if( UlAddress > 0x000003FF )											return 9;
	
	IOWrite32A( FLASHROM_ACSCNT, 0x00000000 | (UINT_32)(UcLength-1) );
	IOWrite32A( FLASHROM_FLA_ADR, ((UINT_32)SelMat << 16) | ( UlAddress & 0x00003FFF ) );
	
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	IOWrite32A( FLASHROM_CMD, 0x00000001 );
	for( i=0 ; i < UcLength ; i++ ){
		IORead32A( FLASHROM_FLA_RDAT, &PulData[i] ) ;
TRACE("Read Data[%02x] = %08x\n", i , PulData[i] );
	}
	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);

	return( 0 ) ;
}

//********************************************************************************
// Function Name 	: FlashBlockWrite
// Retun Value		: 0 : Success, 1 : Unlock Error, 2 : Time Out Error
// Argment Value	: Info Mat , Flash Address
// Explanation		: <Flash Memory> Block Erase
// History			: First edition 						
// Unit of erase	: informaton mat   64 Byte
//					: user mat         64 Byte
//********************************************************************************
UINT_8	FlashBlockWrite( UINT_8 SelMat , UINT_32 SetAddress , UINT_32 *PulData)
{
	UINT_32	UlReadVal, UlCnt;
	UINT_8	ans	= 0 ;
	UINT_8	i	 ;

	// fail safe
	// reject irregular mat
//	if( SelMat != INF_MAT0 && SelMat != INF_MAT1  )			return 10;	// USR MAT,INF_MAT2もAccessしない
	if( SelMat != INF_MAT0 && SelMat != INF_MAT1 && SelMat != INF_MAT2  )			return 10;	// USR MATはAccessしない
	// 
	if( SetAddress > 0x000003FF )							return 9;

	// Flash write準備
	ans	= UnlockCodeSet();
	if( ans != 0 )	return( ans ) ;							// Unlock Code Set

	WritePermission();										// Write permission
	if( SelMat != USER_MAT ){
		if( SelMat == INF_MAT2 )	IOWrite32A( 0xE07CCC, 0x00006A4B );
		else						IOWrite32A( 0xE07CCC, 0x0000C5AD );		// additional unlock for INFO
	}
	AddtionalUnlockCodeSet();								// common additional unlock code set
	
	IOWrite32A( FLASHROM_FLA_ADR, ((UINT_32)SelMat << 16) | ( SetAddress & 0x000010 )) ;// addressはpageのみ指定
	// page write Start
	IOWrite32A( FLASHROM_FLAMODE , 0x00000000);
	IOWrite32A( FLASHROM_CMD, 2 ) ;

//	WitTim( 5 ) ;

	UlCnt	= 0 ;

	for( i=0 ; i< 16 ; i++ ){
		IOWrite32A( FLASHROM_FLA_WDAT, PulData[i]  );	// Write data
TRACE("Write Data[%d] = %08x\n", i , PulData[i] );
	}
	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( FLASHROM_FLAINT, &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;

	// page program
	IOWrite32A( FLASHROM_CMD, 8  );	
	
	do {
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;

		IORead32A( FLASHROM_FLAINT, &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;
	
	IOWrite32A( FLASHROM_FLAMODE , 0x00000002);
	ans	= UnlockCodeClear();								// Unlock Code Clear
	return( ans ) ;							

}





//********************************************************************************
// Function Name 	: WrHallCalData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write hall calibration data
// History			: First edition 				
//********************************************************************************
UINT_8 WrHallCalData( UINT_8 UcMode )
{
	UINT_32	UlMAT0[32];
	UINT_8 ans = 0, i;
	UINT_16	UsCkVal,UsCkVal_Bk ;

TRACE( "WrHallCalData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG == 1
	/* modify   *****************************************************/
	if( UcMode ){	// write
		UlMAT0[CALIBRATION_STATUS] &= ~( HALL_CALB_FLG | HALL_CALB_BIT );
		UlMAT0[CALIBRATION_STATUS] |= (StAdjPar.StHalAdj.UlAdjPhs | GYRO_GAIN_FLG) ;							// Calibration Status
		UlMAT0[HALL_BIAS_OFFSET]	= (UINT_32)(((UINT_32)(StAdjPar.StHalAdj.UsHlyOff&0xFF00)<<16) | ((UINT_32)(StAdjPar.StHalAdj.UsHlyGan & 0xFF00)<<8) | ((UINT_32)(StAdjPar.StHalAdj.UsHlxOff & 0xFF00)>>0) | ((UINT_32)(StAdjPar.StHalAdj.UsHlxGan & 0xFF00)>>8 ));
		UlMAT0[LOOP_GAIN_XY]		= ((StAdjPar.StLopGan.UlLygVal & 0xFFFF0000) | (StAdjPar.StLopGan.UlLxgVal>>16));
		UlMAT0[LENS_OFFSET]			= (UINT_32)(((UINT_32)(StAdjPar.StHalAdj.UsAdyOff )<<16) | (UINT_32)(StAdjPar.StHalAdj.UsAdxOff ));
		UlMAT0[GYRO_GAIN_X]			= 0x3FFFFFFF;
		UlMAT0[GYRO_GAIN_Y]			= 0x3FFFFFFF;
		UlMAT0[HL_XMAXMIN]			= (UINT_32)(((UINT_32)StAdjPar.StHalAdj.UsHlxMxa<<16) | (UINT_32)StAdjPar.StHalAdj.UsHlxMna);
		UlMAT0[HL_YMAXMIN]			= (UINT_32)(((UINT_32)StAdjPar.StHalAdj.UsHlyMxa<<16) | (UINT_32)StAdjPar.StHalAdj.UsHlyMna);
////		UlMAT0[G_OFFSET_XY]			= (UINT_32)(((UINT_32)StAdjPar.StGvcOff.UsGyoVal<<16) | (UINT_32)StAdjPar.StGvcOff.UsGxoVal);
//#ifdef	SEL_SHIFT_COR
////		UlMAT0[G_OFFSET_Z_AX]		= (UINT_32)(((UINT_32)(StAclVal.StAccel.SlOffsetX )<<16) | (UINT_32)(StAdjPar.StGvcOff.UsGzoVal ));
////		UlMAT0[A_OFFSET_YZ]			= (UINT_32)(((UINT_32)(StAclVal.StAccel.SlOffsetZ )<<16) | (UINT_32)(StAclVal.StAccel.SlOffsetY ));
//#endif
//		UlMAT0[LENS_OFFSET_BK]		= (UINT_32)(((UINT_32)(StAdjPar.StHalAdj.UsAdyOff )<<16) | (UINT_32)(StAdjPar.StHalAdj.UsAdxOff ));
	}else{
		UlMAT0[CALIBRATION_STATUS] |= ( HALL_CALB_FLG | 0x000000FF | GYRO_GAIN_FLG );
		UlMAT0[HALL_BIAS_OFFSET] 	= 0xFFFFFFFF;
		UlMAT0[LOOP_GAIN_XY]		= 0xFFFFFFFF;
		UlMAT0[LENS_OFFSET]			= 0xFFFFFFFF;
		UlMAT0[GYRO_GAIN_X]			= 0xFFFFFFFF;
		UlMAT0[GYRO_GAIN_Y]			= 0xFFFFFFFF;
		UlMAT0[HL_XMAXMIN]			= 0xFFFFFFFF;
		UlMAT0[HL_YMAXMIN]			= 0xFFFFFFFF;
//		UlMAT0[G_OFFSET_XY]			= 0xFFFFFFFF;
//#ifdef	SEL_SHIFT_COR
//		UlMAT0[G_OFFSET_Z_AX]		= 0xFFFFFFFF;
//		UlMAT0[A_OFFSET_YZ]			= 0xFFFFFFFF;
//#endif
//		UlMAT0[LENS_OFFSET_BK]		= 0xFFFFFFFF;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x00 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);
	
TRACE( "WrHallCalData____COMPLETE\n" );
	return(0);
}

//********************************************************************************
// Function Name 	: WrGyroGainData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write gyro gain data
// History			: First edition 				
//********************************************************************************
UINT_8 WrGyroGainData( UINT_8 UcMode )
{
	UINT_32	UlMAT0[32];
	UINT_32	UlReadGxzoom , UlReadGyzoom;
	UINT_8 ans = 0, i;
	UINT_16	UsCkVal,UsCkVal_Bk ;

TRACE( "WrGyroGainData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG == 1
	/* modify   *****************************************************/
	if( UcMode ){	// write
		RamRead32A(  GyroFilterTableX_gxzoom , &UlReadGxzoom ) ;
		RamRead32A(  GyroFilterTableY_gyzoom , &UlReadGyzoom ) ;
		
		UlMAT0[CALIBRATION_STATUS] &= ~( GYRO_GAIN_FLG );
		UlMAT0[GYRO_GAIN_X] = UlReadGxzoom;
		UlMAT0[GYRO_GAIN_Y] = UlReadGyzoom;
	}else{
		UlMAT0[CALIBRATION_STATUS] |= GYRO_GAIN_FLG;
		UlMAT0[GYRO_GAIN_X] = 0x3FFFFFFF;
		UlMAT0[GYRO_GAIN_Y] = 0x3FFFFFFF;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , 0 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);
	
TRACE( "WrGyroGainData____COMPLETE\n" );
	return(0);
}

//********************************************************************************
// Function Name 	: WrLinCalData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write Hall Linearity data
// History			: First edition 				
//********************************************************************************
UINT_8 WrLinCalData( UINT_8 UcMode, mlLinearityValue *linval )
{
	UINT_32	UlMAT0[32];
	UINT_8 ans = 0, i;
	UINT_16	UsCkVal,UsCkVal_Bk ;
	double		*pPosX, *pPosY;
	UINT_32	PosDifX, PosDifY;
	DSPVER Info;
	LINCRS* LnCsPtr;

	GetInfomationAfterStartUp( &Info );
	if( Info.ActType == 0x01 ) {
		LnCsPtr = (LINCRS*)&ACT01_LinCrsParameter;
	}
TRACE( "WrLinCalData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG == 1
	/* modify   *****************************************************/
	if( UcMode ){	// write
		if( LnCsPtr->XY_SWAP ==1 ){
			pPosX = linval->positionY;
			pPosY = linval->positionX;
		}else{
			pPosX = linval->positionX;
			pPosY = linval->positionY;
		}
		UlMAT0[CALIBRATION_STATUS] &= ~( HLLN_CALB_FLG );
		UlMAT0[LN_POS1] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS2] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS3] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS4] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS5] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS6] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS7] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		if( LnCsPtr->XY_SWAP ==1 ){
			PosDifX = LnCsPtr->STEPY; 
			PosDifY = LnCsPtr->STEPX; 
		}else{
			PosDifX = LnCsPtr->STEPX; 
			PosDifY = LnCsPtr->STEPY; 
		}
		UlMAT0[LN_STEP] = (UINT_32)(((UINT_32)PosDifY << 16) | (UINT_32)PosDifX);
	}else{
		UlMAT0[CALIBRATION_STATUS] |= HLLN_CALB_FLG;
		UlMAT0[LN_POS1] = 0xFFFFFFFF;
		UlMAT0[LN_POS2] = 0xFFFFFFFF;
		UlMAT0[LN_POS3] = 0xFFFFFFFF;
		UlMAT0[LN_POS4] = 0xFFFFFFFF;
		UlMAT0[LN_POS5] = 0xFFFFFFFF;
		UlMAT0[LN_POS6] = 0xFFFFFFFF;
		UlMAT0[LN_POS7] = 0xFFFFFFFF;
		UlMAT0[LN_STEP] = 0xFFFFFFFF;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , 0 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);
	
TRACE( "WrLinCalData____COMPLETE\n" );
	return(0);
}

//********************************************************************************
// Function Name 	: WrMixCalData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write cross talk data
// History			: First edition 				
//********************************************************************************
UINT_8 WrMixCalData( UINT_8 UcMode, mlMixingValue *mixval )
{
	UINT_32	UlMAT0[32];
	UINT_8 ans = 0, i;
	UINT_16	UsCkVal,UsCkVal_Bk ;
	DSPVER Info;
	LINCRS* LnCsPtr;

	GetInfomationAfterStartUp( &Info );
	if( Info.ActType == 0x01 ) {
		LnCsPtr = (LINCRS*)&ACT01_LinCrsParameter;
	}

TRACE( "WrMixCalData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG
	/* modify   *****************************************************/
	if( UcMode ){	// write
		mixval->hx45yL = (-1)*mixval->hx45yL;
		mixval->hy45xL = (-1)*mixval->hy45xL;

		if(mixval->hy45yL<0){			/* for MeasurementLibrary 1.X */
			mixval->hy45yL = (-1)*mixval->hy45yL;
			mixval->hx45yL = (-1)*mixval->hx45yL;
		}
	
		if(( LnCsPtr->STEPX*LnCsPtr->DRIVEX > 0 && LnCsPtr->STEPY*LnCsPtr->DRIVEY < 0 ) 
		      || ( LnCsPtr->STEPX*LnCsPtr->DRIVEX < 0 && LnCsPtr->STEPY*LnCsPtr->DRIVEY > 0 )){
			mixval->hx45yL = (-1)*mixval->hx45yL;
			mixval->hy45xL = (-1)*mixval->hy45xL;
		}
	
		UlMAT0[CALIBRATION_STATUS] &= ~( MIXI_CALB_FLG );
		UlMAT0[MIXING_X] = (UINT_32)((mixval->hx45yL & 0xFFFF0000) | (( mixval->hx45xL >> 16) & 0x0000FFFF));
		UlMAT0[MIXING_Y] = (UINT_32)((mixval->hy45xL & 0xFFFF0000) | (( mixval->hy45yL >> 16) & 0x0000FFFF));
		UlMAT0[MIXING_SFT] = (UINT_32)((((UINT_32)mixval->hysx << 8) & 0x0000FF00) | ((UINT_32)mixval->hxsx & 0x000000FF ));
	}else{
		UlMAT0[CALIBRATION_STATUS] |= MIXI_CALB_FLG;
		UlMAT0[MIXING_X] = 0xFFFFFFFF;
		UlMAT0[MIXING_Y] = 0xFFFFFFFF;
		UlMAT0[MIXING_SFT] = 0xFFFFFFFF;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , 0 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);

TRACE( "WrMixCalData____COMPLETE\n" );
	return(0);
}

//********************************************************************************
// Function Name 	: WrLinMixCalData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write cross talk & Linearity data
// History			: First edition 				
//********************************************************************************
UINT_8 WrLinMixCalData( UINT_8 UcMode, mlMixingValue *mixval , mlLinearityValue *linval  )
{
	UINT_32	UlMAT0[32];
	UINT_8 ans = 0 , i ;
	UINT_16	UsCkVal,UsCkVal_Bk ;
	double		*pPosX, *pPosY;
	UINT_32	PosDifX, PosDifY;
	DSPVER Info;
	LINCRS* LnCsPtr;

	GetInfomationAfterStartUp( &Info );

	if( Info.ActType == 0x01 ) {
		LnCsPtr = (LINCRS*)&ACT01_LinCrsParameter;
	}

TRACE( "WrLinMixCalData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG == 1
	/* modify   *****************************************************/
	if( UcMode ){	// write
		if( LnCsPtr->XY_SWAP ==1 ){
			pPosX = linval->positionY;
			pPosY = linval->positionX;
		}else{
			pPosX = linval->positionX;
			pPosY = linval->positionY;
		}
		UlMAT0[CALIBRATION_STATUS] &= ~( HLLN_CALB_FLG | MIXI_CALB_FLG );
		UlMAT0[LN_POS1] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS2] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS3] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS4] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS5] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS6] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		UlMAT0[LN_POS7] = (UINT_32)((UINT_32)(*pPosY++ * 10)<<16) | (UINT_32)(*pPosX++ * 10);
		if( LnCsPtr->XY_SWAP ==1 ){
			PosDifX = (linval->dacY[4]>>16); 
			PosDifY = (linval->dacX[4]>>16); 
		}else{
			PosDifX = (linval->dacX[4]>>16); 
			PosDifY = (linval->dacY[4]>>16); 
		}
		UlMAT0[LN_STEP] = (UINT_32)(((UINT_32)PosDifY << 16) | (UINT_32)PosDifX);
		
		mixval->hx45yL = (-1)*mixval->hx45yL;
		mixval->hy45xL = (-1)*mixval->hy45xL;

		if(mixval->hy45yL<0){			/* for MeasurementLibrary 1.X */
			mixval->hy45yL = (-1)*mixval->hy45yL;
			mixval->hx45yL = (-1)*mixval->hx45yL;
		}
	
	//*****************************************************//
		/* X と Y でStep 極性が違う時 */
		if(( (INT_32)linval->dacY[4] > 0 && (INT_32)linval->dacX[4] < 0 ) || ( (INT_32)linval->dacY[4] < 0 && (INT_32)linval->dacX[4] > 0 )){
			mixval->hx45yL = (-1)*mixval->hx45yL;
			mixval->hy45xL = (-1)*mixval->hy45xL;
		}
	//****************************************************//
	
		UlMAT0[MIXING_X] = (UINT_32)((mixval->hx45yL & 0xFFFF0000) | (( mixval->hx45xL >> 16) & 0x0000FFFF));
		UlMAT0[MIXING_Y] = (UINT_32)((mixval->hy45xL & 0xFFFF0000) | (( mixval->hy45yL >> 16) & 0x0000FFFF));
		UlMAT0[MIXING_SFT] = (UINT_32)((((UINT_32)mixval->hysx << 8) & 0x0000FF00) | ((UINT_32)mixval->hxsx & 0x000000FF ));
	}else{
		UlMAT0[CALIBRATION_STATUS] |= ( HLLN_CALB_FLG | MIXI_CALB_FLG );
		UlMAT0[LN_POS1] = 0xFFFFFFFF;
		UlMAT0[LN_POS2] = 0xFFFFFFFF;
		UlMAT0[LN_POS3] = 0xFFFFFFFF;
		UlMAT0[LN_POS4] = 0xFFFFFFFF;
		UlMAT0[LN_POS5] = 0xFFFFFFFF;
		UlMAT0[LN_POS6] = 0xFFFFFFFF;
		UlMAT0[LN_POS7] = 0xFFFFFFFF;
		UlMAT0[LN_STEP] = 0xFFFFFFFF;
		UlMAT0[MIXING_X] = 0xFFFFFFFF;
		UlMAT0[MIXING_Y] = 0xFFFFFFFF;
		UlMAT0[MIXING_SFT] = 0xFFFFFFFF;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);
TRACE( "UlMAT0[ %d ] = %08x\n", i, UlMAT0[i] );

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , 0 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);
	
TRACE( "WrLinMixCalData____COMPLETE\n" );
	return(0);
}



//********************************************************************************
// Function Name 	: WrOptCenerData
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Flash write hall calibration data
// History			: First edition 				
//********************************************************************************
UINT_8 WrOptCenerData( UINT_8 UcMode )
{
	UINT_32	UlMAT0[32];
	UINT_8 ans = 0, i;
	UINT_16	UsCkVal,UsCkVal_Bk ;
	UINT_32	UlReadLensxoffset,UlReadLensyoffset;

TRACE( "WrOptCenerData : Mode = %d\n", UcMode);
	/* Back up ******************************************************/
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 1 );
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT0 , 0 );	// all erase
	if( ans != 0 )	return( 2 ) ;							// Unlock Code Set

#if DEBUG == 1
	for(i=0;i<32;i++){
TRACE( "[ %d ] = %08x\n", i, UlMAT0[i] );
	}
#endif //DEBUG == 1
	/* modify   *****************************************************/
	if( UcMode ){	// write
		RamRead32A(  OpticalOffset_X , &UlReadLensxoffset ) ;
		RamRead32A(  OpticalOffset_Y , &UlReadLensyoffset ) ;
		UlMAT0[OPTCENTER]			= (UINT_32)((UlReadLensyoffset & 0xFFFF0000) | ((UlReadLensxoffset >> 16 ) & 0x0000FFFF));
	}else{
		UlMAT0[OPTCENTER]			= 0x00000000;
	}
	/* calcurate check sum ******************************************/
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	UlMAT0[MAT0_CKSM] = ((UINT_32)UsCkVal<<16) | ( UlMAT0[MAT0_CKSM] & 0x0000FFFF);

	/* update ******************************************************/
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x00 , UlMAT0 );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT0 , (UINT_32)0x10 , &UlMAT0[0x10] );
	if( ans != 0 )	return( 3 ) ;							// Unlock Code Set

	/* Verify ******************************************************/
	UsCkVal_Bk = UsCkVal;
	ans =FlashMultiRead( INF_MAT0, 0, UlMAT0, 32 );	// check sum 以外
	if( ans )	return( 4 );
	
	UsCkVal = 0;
	for( i=0; i < 31; i++ ){
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>16);
		UsCkVal +=  (UINT_8)(UlMAT0[i]>>24);
	}
	// Remainder
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>0);
	UsCkVal +=  (UINT_8)(UlMAT0[i]>>8);
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UsCkVal, UsCkVal_Bk );
	if( UsCkVal != UsCkVal_Bk )		return(5);
	
TRACE( "WrOptCenerData____COMPLETE\n" );
	return(0);
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* function name    : lstsq								 							  	 */
/* input parameter  : double x[] X軸の配列データ                                         */
/*                    double y[] Y軸の配列データ                                         */
/*                    int n      データ個数                                              */
/*                    int m      近似多項式次数                                          */
/* output parameter : double c[] 近似した各項の係数配列                                  */
/*                    戻り値は、0で正常、-1で収束失敗                                    */
/* comment          : 最小2乗近似法                                                      */
/*				   	   	    			      								  2018.03.07 */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
INT_32 lstsq( double x[], double y[], INT_32 n, INT_32 m, double c[] )
{
	INT_32 i, j, k, m2, mp1, mp2;
	double *a, aik, pivot, *w, w1, w2, w3;

	if(m >= n || m < 1) {
		return -1;
	}

	mp1 = m + 1;
	mp2 = m + 2;
	m2 = 2 * m;
	a = (double *)kzalloc(mp1 * mp2 * sizeof(double), GFP_KERNEL);
	if(a == NULL) {
		return -1;
	}
	w = (double *)kzalloc(mp1 * 2 * sizeof(double), GFP_KERNEL);
	if(w == NULL) {
		kfree(a);
		return  -1;
	}

	for(i = 0; i < m2; i++) {
		w1 = 0.;
		for(j = 0; j < n; j++) {
			w2 = w3 = x[j];
			for(k = 0; k < i; k++) w2 *= w3;
			w1 += w2;
		}
		w[i] = w1;
	}
	a[0] = n;
	for(i = 0; i < mp1; i++)
		for(j = 0; j < mp1; j++) if(i || j)	a[i * mp2 + j] = w[i + j - 1];

	w1 = 0.;
	for(i = 0; i < n; i++) w1 += y[i];
	a[mp1] = w1;
	for(i = 0; i < m; i++) {
		w1 = 0.;
		for(j = 0; j < n; j++) {
			w2 = w3 = x[j];
			for(k = 0; k < i; k++) w2 *= w3;
			w1 += y[j] * w2;
		}
		a[mp2 * (i + 1) + mp1] = w1;
	}

	for(k = 0; k < mp1; k++) {
		pivot = a[mp2 * k + k];
		a[mp2 * k + k] = 1.0;
		for(j = k + 1; j < mp2; j++) a[mp2 * k + j] /= pivot;
		for(i = 0; i < mp1; i++) {
			if(i != k) {
				aik = a[mp2 * i + k];
				for(j = k; j < mp2; j++) a[mp2 * i + j] -= aik * a[mp2 * k + j];
			}
		}
	}
	for(i = 0; i < mp1; i++) c[i] = a[mp2 * i + mp1];

	kfree(w);
	kfree(a);
	return 0;
}

//********************************************************************************
// Function Name 	: Mat2ReWrite
// Retun Value		: NON
// Argment Value	: NON
// Explanation		: Mat2 re-write function
// History			: First edition 				
//********************************************************************************
UINT_8 Mat2ReWrite( void )
{
	UINT_32	UlMAT2[32];
	UINT_32	UlCKSUM=0;
	UINT_8	ans , i ;
	UINT_32	UlCkVal ,UlCkVal_Bk;

	ans = FlashMultiRead( INF_MAT2, 0, UlMAT2, 32 );
	if(ans)	return( 0xA0 );
	/* FT_REPRG check *****/
//	if( UlMAT2[FT_REPRG] == PRDCT_WR || UlMAT2[FT_REPRG] == USER_WR ){
	if( UlMAT2[FT_REPRG] == USER_WR ){
TRACE( "Already re-write\n" );
		return( 0x00 );	// not need
	}
	
	/* Check code check *****/
	if( UlMAT2[CHECKCODE1] != CHECK_CODE1 )	return( 0xA1 );
	if( UlMAT2[CHECKCODE2] != CHECK_CODE2 )	return( 0xA2 );
	
	/* Check sum check *****/
	for( i=16 ; i<MAT2_CKSM ; i++){
		UlCKSUM += UlMAT2[i];
	}
	if(UlCKSUM != UlMAT2[MAT2_CKSM])		return( 0xA3 );
	
	/* registor re-write flag *****/
	UlMAT2[FT_REPRG] = USER_WR;
	
	/* backup sum check before re-write *****/
	UlCkVal_Bk = 0;
	for( i=0; i < 32; i++ ){		// 全領域
		UlCkVal_Bk +=  UlMAT2[i];
	}
	
	/* Erase   ******************************************************/
	ans = FlashBlockErase( INF_MAT2 , 0 );	// all erase
	if( ans != 0 )	return( 0xA4 ) ;							// Unlock Code Set
	
	/* excute re-write *****/
	ans = FlashBlockWrite( INF_MAT2 , 0 , UlMAT2 );
	if( ans != 0 )	return( 0xA5 ) ;							// Unlock Code Set
	ans = FlashBlockWrite( INF_MAT2 , (UINT_32)0x10 , &UlMAT2[0x10] );
	if( ans != 0 )	return( 0xA5 ) ;							// Unlock Code Set

	ans =FlashMultiRead( INF_MAT2, 0, UlMAT2, 32 );
	if( ans )	return( 0xA0 );
	
	UlCkVal = 0;
	for( i=0; i < 32; i++ ){		// 全領域
		UlCkVal +=  UlMAT2[i];
	}
	
TRACE( "[RVAL]:[BVal]=[%04x]:[%04x]\n",UlCkVal, UlCkVal_Bk );
	if( UlCkVal != UlCkVal_Bk )		return( 0xA6 );				// write data != writen data
	
	return( 0x01 );				// re-write ok
}	
	

//#ifdef	TRNT
//********************************************************************************
// Function Name 	: LoadUareToPM
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: load user area data from Flash memory to PM
// History			: First edition
//********************************************************************************
UINT_8 LoadUareToPM( DOWNLOAD_TBL_EXT* ptr , UINT_8 mode )
{
	UINT_8 ans=0;
	UINT_32	UlReadVal=0;
	UINT_32	UlReadVer;
	UINT_32	UlCnt=0;
	
	if( !mode ){
		RamRead32A( 0x8000 , &UlReadVer );
		if( (UlReadVer & 0xFFFFFF00) == 0x01120000 ){
			RamWrite32A( 0xE000 , 0x00000000 );		// to boot
			WitTim( 15 ) ;												// Bootプログラムを回すのに15msec必要。
//neo: need to reduce this time
			IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
		}else{
			return( 0x01 );	// NG
		}
		if( UlReadVal != 0x0B ){
			IOWrite32A( SYSDSP_REMAP,				0x00001400 ) ;		// CORE_RST[12], MC_IGNORE2[10] = 1
			WitTim( 15 ) ;												// Bootプログラムを回すのに15msec必要。
//neo: need to reduce this time
			IORead32A( ROMINFO,				(UINT_32 *)&UlReadVal ) ;	
			if( UlReadVal != 0x0B) {
				return( 0x02 );
			}
		}
		
	 	ans = PmemUpdate128( ptr );			// Update the special program for updating the flash memory.
		if(ans != 0) return( ans );
	}
	
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
	
//********************************************************************************
// Function Name 	: RdUareaFromPm
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: Read user area data from program memory
// History			: First edition
//********************************************************************************
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
    UINT_8 i;
	UINT_32	ReadData;
	if(!UcLength)	return(0xff);
	if( !mode ){
		RamWrite32A( 0x5000 , UlAddress );
	}
	for( i=0 ; i <= UcLength ;  )
	{
		RamRead32A( 0x5001 , &ReadData );
//TRACE("[%02x]%08x\n",i,ReadData);
		PucData[i++] = (UINT_8)(ReadData >> 24);
		PucData[i++] = (UINT_8)(ReadData >> 16);
		PucData[i++] = (UINT_8)(ReadData >> 8);
		PucData[i++] = (UINT_8)(ReadData >> 0);
	}

	return( 0 );
}
	
//********************************************************************************
// Function Name 	: WrUareaToPm
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: Write user area data to PM
// History			: First edition
//********************************************************************************
UINT_8	WrUareaToPm( UINT_32 UlAddress, UINT_8 *PucData , UINT_8 UcLength , UINT_8 mode )
{
    UINT_8 i;
	UINT_8	PucBuf[254];	// address2Byte + data252Byte
	
	if(!UcLength)	return(0xff);
	
	PucBuf[0] = 0x50;
	PucBuf[1] = 0x01;
	
	for(i=0 ; i<=UcLength ; i++)
	{
		PucBuf[i+2] = PucData[i];
TRACE("[%02x:%02x]",i+2,PucBuf[i+2]);
	}
TRACE("\n");
	if( !mode ){
		RamWrite32A( 0x5000 , UlAddress );
	}
	CntWrt( PucBuf , (UINT_16)UcLength+3 );

	return( 0 );
}

//********************************************************************************
// Function Name 	: WrUareaToFlash
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: Update user area data from PM to Flash memory
// History			: First edition
//********************************************************************************
UINT_8	WrUareaToFlash( void )
{
	UINT_32	UlReadVal;
	UINT_32	UlCntE=0;
	UINT_32	UlCntW=0;
	UINT_8	ans=0;
	
	ans	= UnlockCodeSet();
	if( ans != 0 )	return( ans ) ;							// Unlock Code Set
	WritePermission();									// Write Permission
	AddtionalUnlockCodeSet();							// Additional Unlock Code Set
	
	IOWrite32A( 0xE0701C , 0x00000000);		// 
//	RamWrite32A( 0xF007, 0x00000000 );						// FlashAccess Setup
	
	RamWrite32A( 0x5005 , 0x00000000 );		// erase User area data on flash memory
	
	WitTim( 10 );//neo: need to reduce this time
	do{
		WitTim( 1 );
		if( UlCntE++ > 20 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			if( UnlockCodeClear() != 0 ) 	return (0x11) ;				// unlock code clear ng
			return (0x10) ;									// erase ng
		}
		RamRead32A( 0x5005, &UlReadVal );					// complite Flagの読み出し
	}while ( UlReadVal != 0 );
	
	RamWrite32A( 0x5006 , 0x00000000 );		// write user area data from PM to flash memory
	
	WitTim( 300 );//neo: need to reduce this time
	do{
		WitTim( 1 );
		if( UlCntW++ > 300 ) {
			IOWrite32A( 0xE0701C , 0x00000002);
			if( UnlockCodeClear() != 0 ) 	return (0x21) ;				// unlock code clear ng
			return (0x20) ;									// write ng
		}
		RamRead32A( 0x5006, &UlReadVal );					// complite Flagの読み出し
	}while ( UlReadVal != 0 );
	IOWrite32A( 0xE0701C , 0x00000002);
	if( UnlockCodeClear() != 0 ) 	return (0x31) ;				// unlock code clear ng

TRACE("[E-CNT: %d] [W-CNT: %d]\n",UlCntE,UlCntW);
	return( 0 );
}
//#endif
