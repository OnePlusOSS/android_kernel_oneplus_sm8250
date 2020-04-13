//********************************************************************************
//
//		<< LC898128 Evaluation Soft >>
//	    Program Name	: FlsDownload.c
//		History			: First edition
//********************************************************************************
#define	__OISDWL__
//**************************
//	Include Header File
//**************************
#include	"Ois.h"

#include	"OisAPI.h"

// 19811 Wide
#include	"FromCode_01_02_01_00.h"

// 19811 Tele
#include	"FromCode_01_02_02_01.h"

#include	"OisDWL.h"

//****************************************************
//	CUSTOMER NECESSARY CREATING FUNCTION LIST
//****************************************************
//#ifdef	TRNT
extern 	INT_32 RamRead32A( UINT_16, void * );
//#endif

//********************************************************************************
// Function Name 	: FlashUpload128
// Retun Value		: NON
// Argment Value	: chiperase
// Explanation		: Flash Update for LC898128
// History			: First edition
//********************************************************************************
const DOWNLOAD_TBL_EXT DTbl[] = {
	{0x0101, 0, 1, CcUpdataCode128_01_02_01_00, UpDataCodeSize_01_02_01_00,  UpDataCodeCheckSum_01_02_01_00, CcFromCode128_01_02_01_00, sizeof(CcFromCode128_01_02_01_00), FromCheckSum_01_02_01_00, FromCheckSumSize_01_02_01_00 },
	{0x0101, 1, 1, CcUpdataCode128_01_02_02_01, UpDataCodeSize_01_02_02_01,  UpDataCodeCheckSum_01_02_02_01, CcFromCode128_01_02_02_01, sizeof(CcFromCode128_01_02_02_01), FromCheckSum_01_02_02_01, FromCheckSumSize_01_02_02_01 },
	{0xFFFF, 0, 0,         (void*)0,                         0,                            0,                         (void*)0,                          0,                             0,                        0}
};

UINT_8 FlashUpload128( UINT_8 ModuleVendor, UINT_8 ActVer, UINT_8 MasterSlave, UINT_8 FWType)
{
	DOWNLOAD_TBL_EXT* ptr ;
	UINT_32	data;

	ptr = ( DOWNLOAD_TBL_EXT * )DTbl ;

	do {
		if( (ptr->Index == ( ((UINT_16)ModuleVendor<<8) + ActVer)) && (ptr->FWType == FWType) && (ptr->MasterSlave == MasterSlave)) {

			// UploadFile‚ª64Byte‚Ý‚ÉPadding‚³‚ê‚Ä‚¢‚È‚¢‚È‚ç‚ÎAErrorB
			if( ( ptr->SizeFromCode % 64 ) != 0 )	return (0xF1) ;

            if(!RamRead32A(0x8000, &data)) {
                if (data == (ptr->FromCode[153] << 24 |
                    ptr->FromCode[154] << 16 |
                    ptr->FromCode[155] << 8 |
                    ptr->FromCode[156])) {
                        TRACE("The FW is the latest, no need to upload\n");
                        return 0;
                } else {
                    TRACE("The FW 0x%x is not the latest 0x%x, will upload\n", data, (ptr->FromCode[153] << 24 |
                    ptr->FromCode[154] << 16 |
                    ptr->FromCode[155] << 8 |
                    ptr->FromCode[156]));
                }
            } else {
                TRACE("Read 0x8000 failed\n");
                return 0xF2;
            }

			return FlashUpdate128( ptr );
		}
		ptr++ ;
	} while (ptr->Index != 0xFFFF ) ;

	return 0xF0 ;
}

//#ifdef	TRNT
UINT_8 LoadUserAreaToPM( void )
{
	DOWNLOAD_TBL_EXT* ptr ;
	UINT_32	UlReadVernum;
	UINT_8 ModuleVendor;
	UINT_8 ActVer;
	
	ptr = ( DOWNLOAD_TBL_EXT * )DTbl ;

	RamRead32A( 0x8000 , &UlReadVernum );
	ModuleVendor = (UINT_8)((UlReadVernum & 0xFF000000) >> 24);
	RamRead32A( 0x8004 , &UlReadVernum );
	ActVer = (UINT_8)((UlReadVernum & 0x0000FF00) >> 8);

TRACE(" VENDER = %02x , ACT = %02x \n", ModuleVendor , ActVer );
	do {
		if( ptr->Index == ( ((UINT_16)ModuleVendor<<8) + ActVer) ) {

			if( ( ptr->SizeFromCode % 64 ) != 0 )	return (0xF1) ;

			return LoadUareToPM( ptr , 0 );
		}
		ptr++ ;
	} while (ptr->Index != 0xFFFF ) ;

	return 0xF0 ;
}

//#endif



