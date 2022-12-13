#ifndef _OPLUS_PROJECT_H_
#define _OPLUS_PROJECT_H_

#define MAX_OCP 6
#define MAX_LEN 8
#define FEATURE_COUNT 10
#define ALIGN4(s) ((sizeof(s) + 3)&(~0x3))

#define FEATURE1_OPEARTOR_OPEN_MASK 0000
#define FEATURE1_FOREIGN_MASK 0001
#define FEATURE1_OPEARTOR_CMCC_MASK 0010
#define FEATURE1_OPEARTOR_CT_MASK 0011
#define FEATURE1_OPEARTOR_CU_MASK 0100
#define FEATURE1_OPEARTOR_MAX_MASK 1111

enum {
        OPLUS_UNKNOWN,
};

enum OPLUS_OPERATOR{
        OPERATOR_UNKOWN,
};

enum f_index {
	IDX_1 = 1,
	IDX_2,
	IDX_3,
	IDX_4,
	IDX_5,
	IDX_6,
	IDX_7,
	IDX_8,
	IDX_9,
	IDX_10,
};

enum PCB_VERSION {
	PRE_EVB1 = 0,
	PRE_EVB2,
	EVB1,
	EVB2,
	T0,
	T1,
	T2,
	T3,
	EVT1,
	EVT2,
	EVT3,
	EVT4,
	DVT1,
	DVT2,
	DVT3,
	DVT4,
	PVT1,
	PVT2,
	PVT3,
	MP1,
	MP2,
	MP3,
	PCB_MAX,
};

enum OPLUS_ENG_VERSION {
    RELEASE                 = 0x00,
    AGING                   = 0x01,
    CTA                     = 0x02,
    PERFORMANCE             = 0x03,
    PREVERSION              = 0x04,
    ALL_NET_CMCC_TEST       = 0x05,
    ALL_NET_CMCC_FIELD      = 0x06,
    ALL_NET_CU_TEST         = 0x07,
    ALL_NET_CU_FIELD        = 0x08,
    ALL_NET_CT_TEST         = 0x09,
    ALL_NET_CT_FIELD        = 0x0A,
    HIGH_TEMP_AGING         = 0x0B,
    FACTORY                 = 0x0C
};

struct pcb_match {
	enum PCB_VERSION version;
	char *str;
};

typedef struct
{
	uint32_t	nVerison;
	uint32_t	nProject;
	uint32_t	nDtsi;
	uint32_t	nAudio;
	uint32_t	nRF;
	uint32_t	nFeature[FEATURE_COUNT];
	uint32_t	nOplusBootMode;
	uint32_t 	nPCB;
	uint8_t		nPmicOcp[MAX_OCP];
	uint8_t		reserved[16]; /*reseved[0] & reserved[1] used for compability of upgrade P->Q*/
} ProjectInfoCDTType;

typedef struct
{
  uint32_t   version;
  uint32_t   is_confidential;
} EngInfoType;

unsigned int get_project(void);
int get_PCB_Version(void);
int get_eng_version(void);
int32_t get_Modem_Version(void);
int32_t get_Operator_Version(void);
bool is_confidential(void);
bool oplus_daily_build(void);
uint32_t get_oplus_feature(enum f_index index);

#endif
