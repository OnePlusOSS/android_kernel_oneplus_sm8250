#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/oem/ratp.h>

static int ratp_enable = 0;
static int ratp_gmod = 0;
static int ratp_allowmost = 0;

static int ratp_enable_store(const char *buf, const struct kernel_param *kp)
{
       int val;

       if (sscanf(buf, "%d\n", &val) <= 0)
               return 0;

       if (ratp_enable == val)
               return 0;

       ratp_enable = val;

       return 0;
}

static int ratp_enable_show(char *buf, const struct kernel_param *kp)
{
       return snprintf(buf, PAGE_SIZE, "%d\n", ratp_enable);
}

static struct kernel_param_ops ratp_enable_ops = {
       .set = ratp_enable_store,
       .get = ratp_enable_show,
};

module_param_cb(ratp_enable, &ratp_enable_ops, NULL, 0664);

bool is_ratp_enable(void)
{
       return ratp_enable;
}

static int ratp_gmod_store(const char *buf, const struct kernel_param *kp)
{
       int val;

       if (sscanf(buf, "%d\n", &val) <= 0)
               return 0;

       if (ratp_gmod == val)
               return 0;

       ratp_gmod = val;

       return 0;
}

static int ratp_gmod_show(char *buf, const struct kernel_param *kp)
{
       return snprintf(buf, PAGE_SIZE, "%d\n", ratp_gmod);
}

static struct kernel_param_ops ratp_gmod_ops = {
       .set = ratp_gmod_store,
       .get = ratp_gmod_show,
};

module_param_cb(ratp_gmod, &ratp_gmod_ops, NULL, 0664);

bool is_gmod_enable(void)
{
       return ratp_gmod;
}

static int ratp_allowmost_store(const char *buf, const struct kernel_param *kp)
{
       int val;

       if (sscanf(buf, "%d\n", &val) <= 0)
               return 0;

       if (ratp_allowmost == val)
               return 0;

       ratp_allowmost = val;

       return 0;
}

static int ratp_allowmost_show(char *buf, const struct kernel_param *kp)
{
       return snprintf(buf, PAGE_SIZE, "%d\n", ratp_allowmost);
}

static struct kernel_param_ops ratp_allowmost_ops = {
       .set = ratp_allowmost_store,
       .get = ratp_allowmost_show,
};

bool is_allowmost_enable(void)
{
       return ratp_allowmost;
}

module_param_cb(ratp_allowmost, &ratp_allowmost_ops, NULL, 0664);
