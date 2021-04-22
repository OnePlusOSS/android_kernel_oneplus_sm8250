#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/oem/boot_mode.h>

static enum oem_boot_mode boot_mode = MSM_BOOT_MODE_NORMAL;
int oem_project = 0;
int small_board_1_absent = 0;
int small_board_2_absent = 0;
int hw_version = 0;
int rf_version = 0;
int prj_version = 0;

char *enum_ftm_mode[] = {"normal",
						 "fastboot",
						 "recovery",
						 "aging",
						 "ftm_at",
						 "ftm_rf",
						 "charger"
};

enum oem_boot_mode get_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(get_boot_mode);

static int __init boot_mode_init(char *str)
{

	pr_info("boot_mode_init %s\n", str);

	if (str) {
		if (strncmp(str, "ftm_at", 6) == 0)
			boot_mode = MSM_BOOT_MODE_FACTORY;
		else if (strncmp(str, "ftm_rf", 6) == 0)
			boot_mode = MSM_BOOT_MODE_RF;
		else if (strncmp(str, "ftm_recovery", 12) == 0)
			boot_mode = MSM_BOOT_MODE_RECOVERY;
		else if (strncmp(str, "ftm_aging", 9) == 0)
			boot_mode = MSM_BOOT_MODE_AGING;
	}

	pr_info("kernel boot_mode = %s[%d]\n",
			enum_ftm_mode[boot_mode], boot_mode);
	return 0;
}
__setup("androidboot.ftm_mode=", boot_mode_init);

static int __init boot_mode_init_normal(void)
{
	char *substrftm = strnstr(boot_command_line,
		"androidboot.ftm_mode=", strlen(boot_command_line));
	char *substrnormal = strnstr(boot_command_line,
		"androidboot.mode=", strlen(boot_command_line));
	char *substrftmstr = NULL;
	char *substrnormalstr = NULL;

	substrftmstr = substrftm + strlen("androidboot.ftm_mode=");
	substrnormalstr = substrnormal + strlen("androidboot.mode=");

	if (substrftm != NULL && substrftmstr != NULL) {

	} else if (substrnormal != NULL && substrnormalstr != NULL) {
		if (strncmp(substrnormalstr, "recovery", 8) == 0)
			boot_mode = MSM_BOOT_MODE_RECOVERY;
		else if (strncmp(substrnormalstr, "charger", 7) == 0)
			boot_mode = MSM_BOOT_MODE_CHARGE;
	}

	pr_info("kernel normal boot_mode = %s[%d]\n",
	enum_ftm_mode[boot_mode], boot_mode);

	return 0;
}
arch_initcall(boot_mode_init_normal);

int get_oem_project(void)
{
	return oem_project;
}
EXPORT_SYMBOL(get_oem_project);

static int __init get_oem_project_init(char *str)
{
	oem_project=simple_strtol(str, NULL, 0);
	pr_info("kernel oem_project %d\n",oem_project);
	return 0;
}

__setup("androidboot.project_name=", get_oem_project_init);

static int __init get_recovery_reason(char *str)
{
	pr_info("recovery mode reason:%s\n", str);
	return 0;
}

__setup("androidboot.recoveryreason=", get_recovery_reason);

/*wireless*/
int get_small_board_1_absent(void)
{
	return small_board_1_absent;
}
EXPORT_SYMBOL(get_small_board_1_absent);

static int __init get_small_board_1_absent_init(char *str)
{
	small_board_1_absent=simple_strtol(str, NULL, 0);
	pr_info("kernel small_board_1_absent %d\n",small_board_1_absent);
	return 0;
}
__setup("androidboot.small_board_1_absent=", get_small_board_1_absent_init);

/*camera*/
int get_small_board_2_absent(void)
{
	return small_board_2_absent;
}
EXPORT_SYMBOL(get_small_board_2_absent);

static int __init get_small_board_2_absent_init(char *str)
{
	small_board_2_absent=simple_strtol(str, NULL, 0);
	pr_info("kernel small_board_2_absent %d\n",small_board_2_absent);
	return 0;
}
__setup("androidboot.small_board_2_absent=", get_small_board_2_absent_init);

int get_hw_board_version(void)
{
       return hw_version;
}
EXPORT_SYMBOL(get_hw_board_version);

static int __init get_hw_version_init(char *str)
{
       hw_version=simple_strtol(str, NULL, 0);
       pr_info("kernel get_hw_version %d\n",hw_version);
       return 0;
}

__setup("androidboot.hw_version=", get_hw_version_init);

int get_rf_version(void)
{
       return rf_version;
}
EXPORT_SYMBOL(get_rf_version);

static int __init get_rf_version_init(char *str)
{
       rf_version=simple_strtol(str, NULL, 0);
       pr_info("kernel get_rf_version %d\n",rf_version);
       return 0;
}

__setup("androidboot.rf_version=", get_rf_version_init);

int get_prj_version(void)
{
       return prj_version;
}
EXPORT_SYMBOL(get_prj_version);

static int __init get_prj_version_init(char *str)
{
       prj_version=simple_strtol(str, NULL, 0);
       pr_info("kernel get_prj_version %d\n",prj_version);
       return 0;
}

__setup("androidboot.prj_version=", get_prj_version_init);
