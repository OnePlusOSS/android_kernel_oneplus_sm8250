// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <asm/arch_timer.h>
#include <linux/debugfs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "rpmh_master_stat.h"

#define RPM_STATS_NUM_REC	2
#define MSM_ARCH_TIMER_FREQ	19200000

#define GET_PDATA_OF_ATTR(attr) \
	(container_of(attr, struct msm_rpmstats_kobj_attr, ka)->pd)

struct msm_rpmstats_record {
	char name[32];
	u32 id;
	u32 val;
};

struct msm_rpmstats_platform_data {
	phys_addr_t phys_addr_base;
	u32 phys_size;
	u32 num_records;
};

struct msm_rpmstats_private_data {
	void __iomem *reg_base;
	u32 num_records;
	u32 read_idx;
	u32 len;
	char buf[480];
	struct msm_rpmstats_platform_data *platform_data;
};

struct msm_rpm_stats_data {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
#if defined(CONFIG_MSM_RPM_SMD)
	u32 client_votes;
	u32 reserved[3];
#endif

};

struct dentry *debugfs_root;

struct msm_rpmstats_kobj_attr {
	struct kobject *kobj;
	struct kobj_attribute ka;
	struct msm_rpmstats_platform_data *pd;
};

static inline u64 get_time_in_sec(u64 counter)
{
	do_div(counter, MSM_ARCH_TIMER_FREQ);

	return counter;
}

static inline u64 get_time_in_msec(u64 counter)
{
	do_div(counter, MSM_ARCH_TIMER_FREQ);
	counter *= MSEC_PER_SEC;

	return counter;
}

static inline u32 msm_rpmstats_read_long_register(void __iomem *regbase,
		int index, int offset)
{
	return readl_relaxed(regbase + offset +
			index * sizeof(struct msm_rpm_stats_data));
}

static inline u64 msm_rpmstats_read_quad_register(void __iomem *regbase,
		int index, int offset)
{
	u64 dst;

	memcpy_fromio(&dst,
		regbase + offset + index * sizeof(struct msm_rpm_stats_data),
		8);
	return dst;
}

#define TIMEOUT_FOR_DUMP (2*60*60)
#define SUBSYSTEM_COUNT 9
static struct msm_rpmstats_platform_data *pdata;
void rpmhstats_statistics(void)
{
	void __iomem *reg;
	static struct msm_rpm_stats_data data[RPM_STATS_NUM_REC];
	int i;
	u32 new_count;
	u64 time_since_last_mode;
	static u64 time;
	u64 time_in_last_mode;
	u64 actual_last_sleep;

	reg = ioremap_nocache(pdata->phys_addr_base,
				pdata->phys_size);
	if (!reg) {
		pr_err("%s: ERROR could not ioremap start=%pa, len=%u\n",
				__func__, &pdata->phys_addr_base,
				pdata->phys_size);
		return;
	}

	for (i = 0; i < pdata->num_records; i++) {
		if (time != 0) {
			new_count = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data, count));
			if (new_count == data[i].count) {
				time_since_last_mode = arch_counter_get_cntvct() - data->last_exited_at;
				time_since_last_mode = get_time_in_sec(time_since_last_mode);
				if (time_since_last_mode > TIMEOUT_FOR_DUMP) {
					time_in_last_mode = data->last_exited_at - data->last_entered_at;
					time_in_last_mode = get_time_in_msec(time_in_last_mode);
					actual_last_sleep = get_time_in_msec(data->accumulated);
					pr_err("Count:%d\n\ttime in last mode(msec):%llu\n"
						"time since last mode(sec):%llu\nactual last sleep(msec):%llu\n\n",
						data->count, time_in_last_mode,
						time_since_last_mode, actual_last_sleep);
					//get subsystem stats
					if (get_apps_stats(false) == false) {
						pr_err("The issue is because APPS");
						continue;
					} else {
						get_subsystem_stats(false);
						panic("Because AOP without entering PC");
					}
				} else if (get_time_in_sec(arch_counter_get_cntvct()) - time > TIMEOUT_FOR_DUMP)
					time = 0;
			} else if (get_time_in_sec(arch_counter_get_cntvct()) - time > TIMEOUT_FOR_DUMP)
				time = 0;
		} else {
			time = get_time_in_sec(arch_counter_get_cntvct());
			data[i].stat_type = msm_rpmstats_read_long_register(reg, i,
					offsetof(struct msm_rpm_stats_data,
					stat_type));
			data[i].count = msm_rpmstats_read_long_register(reg, i,
					offsetof(struct msm_rpm_stats_data, count));
			data[i].last_entered_at = msm_rpmstats_read_quad_register(reg,
					i, offsetof(struct msm_rpm_stats_data,
					last_entered_at));
			data[i].last_exited_at = msm_rpmstats_read_quad_register(reg,
					i, offsetof(struct msm_rpm_stats_data,
					last_exited_at));
			data[i].accumulated = msm_rpmstats_read_quad_register(reg,
					i, offsetof(struct msm_rpm_stats_data,
					accumulated));
			get_apps_stats(true);
			get_subsystem_stats(true);
		}
	}
}

static inline int msm_rpmstats_append_data_to_buf(char *buf,
		struct msm_rpm_stats_data *data, int buflength)
{
	char stat_type[5];
	u64 time_in_last_mode;
	u64 time_since_last_mode;
	u64 actual_last_sleep;

	stat_type[4] = 0;
	memcpy(stat_type, &data->stat_type, sizeof(u32));

	time_in_last_mode = data->last_exited_at - data->last_entered_at;
	time_in_last_mode = get_time_in_msec(time_in_last_mode);
	time_since_last_mode = arch_counter_get_cntvct() - data->last_exited_at;
	time_since_last_mode = get_time_in_sec(time_since_last_mode);
	actual_last_sleep = get_time_in_msec(data->accumulated);

#if defined(CONFIG_MSM_RPM_SMD)
	return scnprintf(buf, buflength,
		"RPM Mode:%s\n\t count:%d\ntime in last mode(msec):%llu\n"
		"time since last mode(sec):%llu\nactual last sleep(msec):%llu\n"
		"client votes: %#010x\n\n",
		stat_type, data->count, time_in_last_mode,
		time_since_last_mode, actual_last_sleep,
		data->client_votes);
#else
	return scnprintf(buf, buflength,
		"RPM Mode:%s\n\t count:%d\ntime in last mode(msec):%llu\n"
		"time since last mode(sec):%llu\nactual last sleep(msec):%llu\n\n",
		stat_type, data->count, time_in_last_mode,
		time_since_last_mode, actual_last_sleep);
#endif
}

static inline int msm_rpmstats_copy_stats(
			struct msm_rpmstats_private_data *prvdata)
{
	void __iomem *reg;
	struct msm_rpm_stats_data data;
	int i, length;

	reg = prvdata->reg_base;

	for (i = 0, length = 0; i < prvdata->num_records; i++) {
		data.stat_type = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data,
					stat_type));
		data.count = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data, count));
		data.last_entered_at = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					last_entered_at));
		data.last_exited_at = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					last_exited_at));
		data.accumulated = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					accumulated));
#if defined(CONFIG_MSM_RPM_SMD)
		data.client_votes = msm_rpmstats_read_long_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					client_votes));
#endif

		length += msm_rpmstats_append_data_to_buf(prvdata->buf + length,
				&data, sizeof(prvdata->buf) - length);
		prvdata->read_idx++;
	}

	return length;
}

#ifdef CONFIG_DEBUG_FS
static int rpmh_stats_show(struct seq_file *s, void *data)
{

	struct msm_rpmstats_private_data prvdata;
	struct msm_rpmstats_platform_data *pdata = NULL;

	pdata = s->private;

	prvdata.reg_base = ioremap_nocache(pdata->phys_addr_base,
					pdata->phys_size);
	if (!prvdata.reg_base) {
		pr_err("%s: ERROR could not ioremap start=%pa, len=%u\n",
			__func__, &pdata->phys_addr_base,
			pdata->phys_size);
		return -EBUSY;
	}

	prvdata.read_idx = prvdata.len = 0;
	prvdata.platform_data = pdata;
	prvdata.num_records = RPM_STATS_NUM_REC;

	if (prvdata.read_idx < prvdata.num_records)
		prvdata.len = msm_rpmstats_copy_stats(&prvdata);

	seq_printf(s, "%s", prvdata.buf);
	return 0;
}

static int rpmh_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpmh_stats_show, inode->i_private);
}

#endif

static const struct file_operations rpmh_stats_fops = {
#ifdef CONFIG_DEBUG_FS
	.open		= rpmh_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
#endif
};

static const struct file_operations rpmh_master_stats_fops = {
#ifdef CONFIG_DEBUG_FS
	.open	   = rpmh_master_stats_open,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
#endif
};

static ssize_t stats_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	struct msm_rpmstats_private_data prvdata;

	prvdata.reg_base = ioremap_nocache(pdata->phys_addr_base,
					pdata->phys_size);

	if (!prvdata.reg_base) {
		pr_err("%s: ERROR could not ioremap start=%pa, len=%u\n",
			__func__, &pdata->phys_addr_base,
			pdata->phys_size);
		return -EBUSY;
	}

	prvdata.read_idx = prvdata.len = 0;
	prvdata.platform_data = pdata;
	prvdata.num_records = RPM_STATS_NUM_REC;

	if (prvdata.read_idx < prvdata.num_records)
		prvdata.len = msm_rpmstats_copy_stats(&prvdata);

	return snprintf(buf, 480, "%s", prvdata.buf);
}

static struct kobj_attribute stats_attribute =
__ATTR_RO(stats);

static struct kobj_attribute master_stats_attribute =
__ATTR_RO(master_stats);

static struct attribute *rpmh_attrs[] = {
	 &stats_attribute.attr,
	 &master_stats_attribute.attr,
	 NULL,
};

static struct attribute_group rpmh_attr_group = {
	.name = "rpmh",
	.attrs = rpmh_attrs,
};

static int msm_rpmstats_probe(struct platform_device *pdev)
{
	//struct msm_rpmstats_platform_data *pdata;
	struct resource *res = NULL, *offset = NULL;
	u32 offset_addr = 0;
	void __iomem *phys_ptr = NULL;
	char *key;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	key = "phys_addr_base";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res)
		return -EINVAL;

	key = "offset_addr";
	offset = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (offset) {
		/* Remap the rpm-stats pointer */
		phys_ptr = ioremap_nocache(offset->start, SZ_4);
		if (!phys_ptr) {
			pr_err("Failed to ioremap offset address\n");
			return -ENODEV;
		}
		offset_addr = readl_relaxed(phys_ptr);
		iounmap(phys_ptr);
	}

	pdata->phys_addr_base  = res->start + offset_addr;
	pdata->phys_size = resource_size(res);

	key = "qcom,num-records";
	if (of_property_read_u32(pdev->dev.of_node, key, &pdata->num_records))
		pdata->num_records = RPM_STATS_NUM_REC;

	debugfs_root = debugfs_create_dir("rpmh", NULL);
	if (!debugfs_root) {
		pr_err("%s: Cannot create rpmh dir\n", __func__);
		return -ENOMEM;
	}

	debugfs_create_file("stats", 0444, debugfs_root, pdata,
			    &rpmh_stats_fops);

	debugfs_create_file("master_stats", 0444, debugfs_root, NULL,
		&rpmh_master_stats_fops);

	ret = sysfs_create_group(power_kobj, &rpmh_attr_group);

	return 0;
}

static int msm_rpmstats_remove(struct platform_device *pdev)
{
	struct msm_rpmstats_kobj_attr *rpms_ka;

	if (!pdev)
		return -EINVAL;

	rpms_ka = (struct msm_rpmstats_kobj_attr *)
			platform_get_drvdata(pdev);

	sysfs_remove_file(rpms_ka->kobj, &rpms_ka->ka.attr);
	kobject_put(rpms_ka->kobj);
	platform_set_drvdata(pdev, NULL);

	return 0;
}


static const struct of_device_id rpm_stats_table[] = {
	{ .compatible = "qcom,rpm-stats" },
	{ },
};

static struct platform_driver msm_rpmstats_driver = {
	.probe = msm_rpmstats_probe,
	.remove = msm_rpmstats_remove,
	.driver = {
		.name = "msm_rpm_stat",
		.owner = THIS_MODULE,
		.of_match_table = rpm_stats_table,
	},
};
builtin_platform_driver(msm_rpmstats_driver);
