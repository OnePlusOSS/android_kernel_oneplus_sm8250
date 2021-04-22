
#ifndef SYNAPTICS_H
#define SYNAPTICS_H
#define CONFIG_SYNAPTIC_RED

/*********PART1:Head files**********************/
#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>

#include "../touchpanel_common.h"
#include "synaptics_firmware_v2.h"

/*********PART2:Define Area**********************/
#define SYNAPTICS_RMI4_PRODUCT_ID_SIZE 10
#define SYNAPTICS_RMI4_PRODUCT_INFO_SIZE 2

#define DiagonalUpperLimit  1100
#define DiagonalLowerLimit  900

#define MAX_RESERVE_SIZE 4
#define MAX_LIMIT_NAME_SIZE 16

#define Limit_MagicNum1     0x494D494C
#define Limit_MagicNum2     0x474D4954
#define Limit_MagicNum2_V2  0x32562D54
#define Limit_ItemMagic     0x4F50504F


/*********PART3:Struct Area**********************/
typedef enum {
	BASE_NEGATIVE_FINGER = 0x02,
	BASE_MUTUAL_SELF_CAP = 0x04,
	BASE_ENERGY_RATIO = 0x08,
	BASE_RXABS_BASELINE = 0x10,
	BASE_TXABS_BASELINE = 0x20,
} BASELINE_ERR;

typedef enum {
	SHIELD_PALM = 0x01,
	SHIELD_GRIP = 0x02,
	SHIELD_METAL = 0x04,
	SHIELD_MOISTURE = 0x08,
	SHIELD_ESD = 0x10,
} SHIELD_MODE;

typedef enum {
	RST_HARD = 0x01,
	RST_INST = 0x02,
	RST_PARITY = 0x04,
	RST_WD = 0x08,
	RST_OTHER = 0x10,
} RESET_REASON;

struct health_info {
	uint16_t grip_count;
	uint16_t grip_x;
	uint16_t grip_y;
	uint16_t freq_scan_count;
	uint16_t baseline_err;
	uint16_t curr_freq;
	uint16_t noise_state;
	uint16_t cid_im;
	uint16_t shield_mode;
	uint16_t reset_reason;
};

struct excep_count {
	uint16_t grip_count;
	//baseline error type
	uint16_t neg_finger_count;
	uint16_t cap_incons_count;
	uint16_t energy_ratio_count;
	uint16_t rx_baseline_count;
	uint16_t tx_baseline_count;
	//noise status
	uint16_t noise_count;
	//shield report fingers
	uint16_t shield_palm_count;
	uint16_t shield_edge_count;
	uint16_t shield_metal_count;
	uint16_t shield_water_count;
	uint16_t shield_esd_count;
	//exception reset count
	uint16_t hard_rst_count;
	uint16_t inst_rst_count;
	uint16_t parity_rst_count;
	uint16_t wd_rst_count;
	uint16_t other_rst_count;
};

struct image_header {
	/* 0x00 - 0x0f */
	unsigned char checksum[4];
	unsigned char reserved_04;
	unsigned char reserved_05;
	unsigned char options_firmware_id:1;
	unsigned char options_contain_bootloader:1;
	/* only available in s4322 , reserved in other, begin*/
	unsigned char options_guest_code:1;
	unsigned char options_tddi:1;
	unsigned char options_reserved:4;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char firmware_size[4];
	unsigned char config_size[4];
	/* 0x10 - 0x1f */
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE];
	unsigned char package_id[2];
	unsigned char package_id_revision[2];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
	/* 0x20 - 0x2f */
	/* only available in s4322 , reserved in other, begin*/
	unsigned char bootloader_addr[4];
	unsigned char bootloader_size[4];
	unsigned char ui_addr[4];
	unsigned char ui_size[4];
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x30 - 0x3f */
	unsigned char ds_id[16];
	/* 0x40 - 0x4f */
	/* only available in s4322 , reserved in other, begin*/
	union {
		struct {
			unsigned char dsp_cfg_addr[4];
			unsigned char dsp_cfg_size[4];
			unsigned char reserved_48_4f[8];
		};
	};
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x50 - 0x53 */
	unsigned char firmware_id[4];
};

struct image_header_data {
	bool contains_firmware_id;
	unsigned int firmware_id;
	unsigned int checksum;
	unsigned int firmware_size;
	unsigned int config_size;
	/* only available in s4322 , reserved in other, begin*/
	unsigned int disp_config_offset;
	unsigned int disp_config_size;
	unsigned int bootloader_offset;
	unsigned int bootloader_size;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
};

struct limit_block {
	char name[MAX_LIMIT_NAME_SIZE];
	int mode;
	int reserve[MAX_RESERVE_SIZE]; /*16*/
	int size;
	int16_t data;
};

struct limit_info {
	unsigned int magic1;
	unsigned int magic2;
	unsigned int count;
};

struct test_header {
	unsigned int magic1;
	unsigned int magic2;
	unsigned int withCBC;
	unsigned int array_limit_offset;
	unsigned int array_limit_size;
	unsigned int array_limitcbc_offset;
	unsigned int array_limitcbc_size;
};

struct test_header_new {
	uint32_t magic1;
	uint32_t magic2;
	uint64_t test_item;
};

struct syna_test_item_header {
	uint32_t    item_magic;
	uint32_t    item_size;
	uint16_t    item_bit;
	uint16_t    item_limit_type;
	uint32_t    top_limit_offset;
	uint32_t    floor_limit_offset;
	uint32_t    para_num;
};

enum test_item_bit {
	TYPE_TRX_SHORT          = 1,
	TYPE_TRX_OPEN           = 2,
	TYPE_TRXGND_SHORT       = 3,
	TYPE_FULLRAW_CAP        = 5,
	TYPE_DELTA_NOISE        = 10,
	TYPE_HYBRIDRAW_CAP      = 18,
	TYPE_RAW_CAP            = 22,
	TYPE_TREXSHORT_CUSTOM   = 25,
	TYPE_HYBRIDABS_DIFF_CBC = 26,
	TYPE_HYBRIDABS_NOSIE    = 29,
};

enum {
	LIMIT_TYPE_NO_DATA          = 0x00,     //means no limit data
	LIMIT_TYPE_CERTAIN_DATA     = 0x01,     //means all nodes limit data is a certain data
	LIMIT_TYPE_EACH_NODE_DATA   = 0x02,     //means all nodes have it's own limit
	LIMIT_TYPE_INVALID_DATA     = 0xFF,     //means wrong limit data type
};

struct syna_testdata{
	int TX_NUM;
	int RX_NUM;
	int fd;
	int irq_gpio;
	int key_TX;
	int key_RX;
	uint64_t  TP_FW;
	const struct firmware *fw;
	bool fd_support;
	bool fingerprint_underscreen_support;
	uint64_t test_item;
};

//import from "android/bootable/bootloader/lk/platform/msm_shared/include/msm_panel.h"
enum {
	OP16037JDI_R63452_1080P_CMD_PANEL = 13,
	OP16037SAMSUNG_S6E3FA5_1080P_CMD_PANEL = 14,
	UNKNOWN_PANEL
};

struct synaptics_proc_operations {
	void (*auto_test)    (struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata);
	void    (*set_touchfilter_state)  (void *chip_data, uint8_t range_size);
	uint8_t (*get_touchfilter_state)  (void *chip_data);
};

void synaptics_limit_read(struct seq_file *s, struct touchpanel_data *ts);
int  synaptics_create_proc(struct touchpanel_data *ts, struct synaptics_proc_operations *syna_ops);
void synaptics_parse_header(struct image_header_data *header, const unsigned char *fw_image);
int synaptics_parse_header_v2(struct image_info *image_info, const unsigned char *fw_image);
int synaptics_get_limit_data(char *type, const unsigned char *fw_image);

#endif
