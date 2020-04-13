#ifndef _BOOT_MODE_H_
#define _BOOT_MODE_H_ 1

enum oem_boot_mode {
	MSM_BOOT_MODE_NORMAL,
	MSM_BOOT_MODE_FASTBOOT,
	MSM_BOOT_MODE_RECOVERY,
	MSM_BOOT_MODE_AGING,
	MSM_BOOT_MODE_FACTORY,
	MSM_BOOT_MODE_RF,
	MSM_BOOT_MODE_CHARGE,
};

enum oem_boot_mode get_boot_mode(void);

enum oem_projcet {
	OEM_PROJECT_MAX,
};

int get_oem_project(void);
int get_small_board_1_absent(void);
int get_small_board_2_absent(void);
int get_hw_board_version(void);
int get_rf_version(void);
int get_prj_version(void);
#endif
