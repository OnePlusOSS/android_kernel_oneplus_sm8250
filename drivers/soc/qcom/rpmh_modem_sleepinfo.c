/***********************************************************
** Copyright (C), 2008-2019, OPLUS Mobile Comm Corp., Ltd.
** OPLUS_FEATURE_POWERINFO_RPMH
** File: - rpmh_modem_sleepinfo.c
** Description: Add a proc node to support the function that make
**              a buffer of modem subsystem sleep info
**
** Version: 1.0
** Date : 2019/12/31
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** zengyunqing 2019/12/31 1.0 build this module
****************************************************************/

#include <linux/file.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define MODEM_SLEEPINFO_BUFFER_SIZE (1024*100)
#define MODEM_SLEEPINFO_BUFFER_RESERVED (10)
struct modem_sleepinfo_buffer_desc {
	bool enabled;
	char *buf_base;
	unsigned int buf_size;
	unsigned int wr_offset;
	struct mutex buf_mlock;
};

static struct modem_sleepinfo_buffer_desc modeminfo_stats;
static struct proc_dir_entry *basic_procdir;

static struct proc_dir_entry *modem_sleepinfo_node;
static unsigned int proc_modemsleep_perms = S_IRUGO;

int rpmh_modem_sleepinfo_buffer_clear(void)
{
	pr_info("%s: wr_offset restart\n", __func__);
	mutex_lock(&modeminfo_stats.buf_mlock);
	modeminfo_stats.wr_offset = 0;
	snprintf(modeminfo_stats.buf_base, modeminfo_stats.buf_size, "restart state\n");
	mutex_unlock(&modeminfo_stats.buf_mlock);
	return 0;
}
EXPORT_SYMBOL(rpmh_modem_sleepinfo_buffer_clear);

static int inner_rpmh_modem_sleepinfo_buffer_clear(void)
{
	pr_info("%s: wr_offset restart\n", __func__);
	modeminfo_stats.wr_offset = 0;
	snprintf(modeminfo_stats.buf_base, modeminfo_stats.buf_size, "restart state\n");
	return 0;
}

static int modem_sleepinfo_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&modeminfo_stats.buf_mlock);
	if(modeminfo_stats.enabled) {
		seq_printf(m, "%s", modeminfo_stats.buf_base);
	} else {
		seq_printf(m, "%s", "init buffer error\n");
	}
	mutex_unlock(&modeminfo_stats.buf_mlock);
	return 0;
}

static char restart_string[] = "OPLUS_MARK_RESTART";
static char command_string_user[sizeof(restart_string)];
static ssize_t modem_sleepinfo_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	signed int cp_count = 0, cmd_real_len = 0;
	signed long ret_count = 0, real_write = 0;

	mutex_lock(&modeminfo_stats.buf_mlock);
	if(count == sizeof(restart_string)) {
		memset(command_string_user, 0, sizeof(command_string_user));
		cmd_real_len = sizeof(command_string_user) < count ? sizeof(command_string_user) : count ;
		if(!copy_from_user(command_string_user, buffer, cmd_real_len)) {
			if(!memcmp(restart_string, command_string_user, cmd_real_len - 1)) {
				inner_rpmh_modem_sleepinfo_buffer_clear();
				mutex_unlock(&modeminfo_stats.buf_mlock);
				return count;
			}
		} else {
			pr_info("%s: warning line%d\n", __func__, __LINE__);
		}
	}

	cp_count = count < modeminfo_stats.buf_size - modeminfo_stats.wr_offset ? count : modeminfo_stats.buf_size - modeminfo_stats.wr_offset;
	if(cp_count < 0)
		cp_count = 0;

	ret_count = copy_from_user(&modeminfo_stats.buf_base[modeminfo_stats.wr_offset], buffer, cp_count);
	if(ret_count >= 0) {
		real_write = cp_count - ret_count;
	}

	modeminfo_stats.wr_offset += real_write;
	if(modeminfo_stats.wr_offset > modeminfo_stats.buf_size) {
		modeminfo_stats.wr_offset = modeminfo_stats.buf_size;
	}

	modeminfo_stats.buf_base[modeminfo_stats.wr_offset] = '\0';
	mutex_unlock(&modeminfo_stats.buf_mlock);
	return count;
}

static int modem_sleepinfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, modem_sleepinfo_proc_show, PDE_DATA(inode));
}


static const struct file_operations modem_sleepinfo_proc_fops = {
	.open		= modem_sleepinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= modem_sleepinfo_proc_write,
};


static int __init rpmh_modem_sleepinfo_init(void)
{
	mutex_init(&modeminfo_stats.buf_mlock);

	basic_procdir = proc_mkdir("rpmh_modem", NULL);
	if(!basic_procdir) {
		pr_err("%s: failed to new basic_procdir\n", __func__);
		goto basic_procdir_null;
	}

	modeminfo_stats.buf_base = (char*)kzalloc(MODEM_SLEEPINFO_BUFFER_SIZE, GFP_KERNEL);
	if(!modeminfo_stats.buf_base) {
		pr_err("%s: malloc buffer failed\n", __func__);
		goto modeminfo_stats_buf_base_error;
	} else {
		modeminfo_stats.buf_size = MODEM_SLEEPINFO_BUFFER_SIZE - MODEM_SLEEPINFO_BUFFER_RESERVED;
		snprintf(modeminfo_stats.buf_base, modeminfo_stats.buf_size, "init_state\n");
		modeminfo_stats.wr_offset = 0;
		modeminfo_stats.enabled = true;
	}

	modem_sleepinfo_node = proc_create_data("sleepinfo", proc_modemsleep_perms,
	                       basic_procdir, &modem_sleepinfo_proc_fops, NULL);
	if (!modem_sleepinfo_node) {
		pr_err("%s: failed to create rpmh_modem/sleepinfo file\n", __func__);
		goto modem_sleepinfo_node_error;
	}

	return 0;

modem_sleepinfo_node_error:
	kfree(modeminfo_stats.buf_base);

modeminfo_stats_buf_base_error:

basic_procdir_null:

	return 0;
}

late_initcall(rpmh_modem_sleepinfo_init);
