/*
 * This driver is used to reserve kernel log in kernel
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/memremap.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/oem/boot_mode.h>
#include "wb_kernel_log.h"

static struct task_struct *tsk;
static int wb_page_num;
static int while_times;
static bool no_data;
static DECLARE_COMPLETION(klog_record_lock);
static u32 buf_offset;
static u32 log_buf_offset;
static u32 log_text_offset;

static void bio_done(struct bio *bio)
{
	bio_put(bio);
	complete(&klog_record_lock);
}

/* padding remain buf to full size content */
static size_t padding_remain_buf(void *buf, void *src, size_t max_size, size_t current_size)
{
	if (buf == NULL || src == NULL)
		return 0;

	memcpy(buf, src, max_size - current_size);

	return max_size - current_size;
}

static size_t add_change_line(char *buf)
{
	if (buf == NULL)
		return 0;

	*buf = '\n';
	return EOL_SIZE;
}

static char *log_text_dup(const struct printk_log_dup *msg)
{
	return (char *)msg + sizeof(struct printk_log_dup);
}

/* TODO: Handle read from head if kernel run out of printk buffer */
static size_t do_log_copy(void *dst, void *src, size_t size)
{
	struct printk_log_dup *msg;
	void *curr_dst, *curr_src;
	size_t total_cp_size;
	size_t cp_size;

	total_cp_size = 0;
	while (total_cp_size <= size) {
		msg = (struct printk_log_dup *)((char *)src + log_buf_offset);
		curr_dst = dst + total_cp_size;
		curr_src = log_text_dup(msg) + log_text_offset;

		/* out of printk log */
		if (msg->text_len == 0 && msg->len == 0)
			return total_cp_size;

		/* count cp size per round */
		if (log_text_offset == NEED_EOL) {
			total_cp_size += add_change_line((char *)curr_dst);
			log_text_offset = 0;
			log_buf_offset += msg->len;
			continue;
		} else
			cp_size = msg->text_len - log_text_offset;

		/* padding remain buf and record src text offset */
		if (total_cp_size + cp_size + EOL_SIZE > size) {
			log_text_offset += padding_remain_buf(curr_dst, curr_src, size, total_cp_size);

			/* checking spectial case of only lack a EOL */
			log_text_offset = (log_text_offset == msg->text_len)?NEED_EOL : log_text_offset;
			return size;
		}

		memcpy(curr_dst, curr_src, cp_size);

		log_buf_offset += msg->len;
		total_cp_size += cp_size;
		/* after memcpy curr_dst had change, re-count dst */
		total_cp_size += add_change_line((char *)dst + total_cp_size);
		log_text_offset = 0;
	}

	return total_cp_size;
}

static void wb_a_page_log(struct block_device *bdev, int start_segment, char *buf)
{
	struct bio *w_bio;
	struct gendisk *gd;
	struct block_device *bd;
	void *vaddr;
	dev_t di;
	int sector_offset;
	int dummy;
	u32 last_buf_offset;
	static first_write = 1;

	/*
	 * if ((while_times + 1) >= LOG_TIME)
	 *	pr_alert("Op_kernel_log: will sync finish\n");
	 */


	vaddr = (void *)log_buf_addr_get();
	di = bdev->bd_dev;

	if (di == 0) {
		pr_alert("Op_kernel_log: dev_t null\n");
		return;
	}

	gd = get_gendisk(di, &dummy);

	if (gd == NULL) {
		pr_alert("Op_kernel_log: gendisk null\n");
		return;
	}

	/* write full page content to block last time, clean the buf */
	if (buf_offset == 0)
		memset(buf, 0, LOG_PAGE_SIZE);

	last_buf_offset = buf_offset;

	/* copy printk buf to local buf from end of last */
	if(first_write) {
		first_write = 0;
		buf_offset += do_log_copy((void *)buf + buf_offset + 8, vaddr, LOG_PAGE_SIZE - buf_offset - 8);
		buf[0] = '\n';
		buf[1] = 'O';
		buf[2] = 'P';
		buf[3] = 'L';
		buf[4] = 'O';
		buf[5] = 'G';
		buf[6] = '0' + start_segment;
		buf[7] = '\n';
	} else
		buf_offset += do_log_copy((void *)buf + buf_offset, vaddr, LOG_PAGE_SIZE - buf_offset);

	if (last_buf_offset == buf_offset) {
		no_data = 1;
		return;
	}
	w_bio = bio_map_kern(gd->queue, buf, LOG_PAGE_SIZE, GFP_KERNEL);

	if (IS_ERR(w_bio))
		return;

	bd = blkdev_get_by_dev(bdev->bd_dev, FMODE_READ|FMODE_WRITE, NULL);
	sector_offset = ((HEADER_SHIFT + wb_page_num + start_segment * SEGMENT_SIZE) * 8);

	if (sector_offset >= SECTOR_OFFSET_MAXIMUM)
		return;

	w_bio->bi_iter.bi_sector =
		(bd->bd_part->start_sect) + sector_offset;
	w_bio->bi_disk = bd->bd_disk;
	w_bio->bi_end_io = bio_done;
	w_bio->bi_opf = REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH;
	submit_bio(w_bio);
	wait_for_completion(&klog_record_lock);
	blkdev_put(bd, FMODE_READ|FMODE_WRITE);

	/* write full page content to block, write next page of block next time */
	if (buf_offset == LOG_PAGE_SIZE) {
		wb_page_num++;
		buf_offset = 0;
	}
}

static void do_wb_logs(struct block_device *bdev, int start_segment)
{
	char *buf = NULL;

	pr_alert("Op_kernel_log: start syncing\n");

	buf = kmalloc(LOG_PAGE_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("Op_kernel_log: allocate fail\n");
		return;
	}
	while (wb_page_num < SEGMENT_SIZE) {
		while (!no_data) {
			wb_a_page_log(bdev, start_segment, buf);
			msleep(20);
		}
		no_data = 0;
		while_times++;
		//pr_alert("Op_kernel_log: times %d\n", while_times);
		if (while_times >= LOG_TIME) {
			pr_alert("Op_kernel_log: sync finish\n");
			break;
		}
		msleep(500);
	}
	if (buf != NULL) {
		kfree(buf);
		buf = NULL;
	}
}


static void wb_header(struct block_device *bdev, char *buf)
{
	struct bio *w_bio;
	struct gendisk *gd;
	struct block_device *bd;
	dev_t di;
	int dummy;

	di = bdev->bd_dev;

	if (di == 0) {
		pr_alert("Op_kernel_log: dev_t null\n");
		return;
	}

	gd = get_gendisk(di, &dummy);

	if (gd == NULL) {
		pr_alert("Op_kernel_log: gendisk null\n");
		return;
	}

	w_bio = bio_map_kern(gd->queue, buf, LOG_PAGE_SIZE, GFP_KERNEL);

	if (IS_ERR(w_bio)) {
		kfree(buf);
		return;
	}

	bd = blkdev_get_by_dev(bdev->bd_dev, FMODE_READ|FMODE_WRITE, NULL);

	w_bio->bi_iter.bi_sector = bd->bd_part->start_sect;
	w_bio->bi_disk = bd->bd_disk;
	w_bio->bi_end_io = bio_done;
	w_bio->bi_opf = REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH;
	submit_bio(w_bio);
	wait_for_completion(&klog_record_lock);
	blkdev_put(bd, FMODE_READ|FMODE_WRITE);
}

static int parser_log_head(char *buf)
{
	char opheader[HEADER_SIZE];
	unsigned char bootcount = 0;
	const char start_count = 0;
	const char op_log_header[11] = "OPKERNELLOG";
	int offset;
	int ret;

	offset = offsetof(struct log_segment_status, OPlogheader);
	memcpy(opheader, buf + offset, sizeof(((struct log_segment_status *)0)->OPlogheader));
	ret = memcmp(opheader, op_log_header, HEADER_SIZE);
	if (ret == 0) {
		pr_err("Op_kernel_log: found header\n");
		offset = offsetof(struct log_segment_status, klog_boot_count);
		memcpy(&bootcount, (buf + offset), sizeof(((struct log_segment_status *)0)->klog_boot_count));
		bootcount++;
		pr_err("Op_kernel_log: bootcount %d\n", (int)bootcount);
		memcpy((buf + offset), &bootcount, sizeof(((struct log_segment_status *)0)->klog_boot_count));
		return (bootcount % RECORD_MAXIMUM);

	} else {
		pr_err("Op_kernel_log: not find header\n");
		memset(buf, 0, LOG_SECTOR_SIZE);
		offset = offsetof(struct log_segment_status, OPlogheader);
		memcpy(buf, op_log_header, sizeof(((struct log_segment_status *)0)->OPlogheader));
		offset = offsetof(struct log_segment_status, klog_boot_count);
		memcpy(buf + offset, &start_count, sizeof(((struct log_segment_status *)0)->klog_boot_count));
		return 0;
	}
	return 0;
}

static char *read_log_header(struct block_device *bdev)
{
	struct bio *r_bio;
	struct gendisk *gd;
	struct block_device *bd;
	dev_t di;
	char *buf = NULL;
	int dummy;

	di = bdev->bd_dev;

	pr_alert("Op_kernel_log: read log header\n");
	if (di == 0) {
		pr_err("Op_kernel_log: dev_t null\n");
		return NULL;
	}

	gd = get_gendisk(di, &dummy);

	if (gd == NULL) {
		pr_err("Op_kernel_log: gendisk null\n");
		return NULL;
	}

	buf = kmalloc(LOG_SECTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return NULL;

	memset(buf, 0, LOG_SECTOR_SIZE);
	r_bio = bio_map_kern(gd->queue, buf, LOG_SECTOR_SIZE, GFP_KERNEL);

	bd = blkdev_get_by_dev(bdev->bd_dev, FMODE_READ|FMODE_WRITE, NULL);
	r_bio->bi_iter.bi_sector = (bd->bd_part->start_sect);
	r_bio->bi_disk = bd->bd_disk;
	r_bio->bi_end_io = bio_done;
	r_bio->bi_opf = READ | REQ_SYNC;

	submit_bio(r_bio);
	wait_for_completion(&klog_record_lock);
	blkdev_put(bd, FMODE_READ|FMODE_WRITE);
	return buf;
}


static int kernel_log_wb_main(void *arg)
{
	struct block_device *log_partition = NULL;
	char *buf = NULL;
	int start_segment;
	int retry_count = 20;

	pr_alert("Op_kernel_log: find reserve partition\n");
	while (log_partition == NULL && retry_count > 0) {
		retry_count--;
		pr_err("Op_kernel_log: bdev null, retry count %d\n", retry_count);
		msleep(20);
		log_partition = find_reserve_partition();
	}

	if(retry_count <= 0 || log_partition == NULL) {
		pr_err("Op_kernel_log: bdev null and retry count = 0, stop record log\n");
		return 0;
	}

	buf = read_log_header(log_partition);

	if (buf == NULL)
		return 0;

	start_segment = parser_log_head(buf);
	pr_alert("Op_kernel_log: start_segment %d\n", start_segment);
	wb_header(log_partition, buf);
	do_wb_logs(log_partition, start_segment);

	if (tsk)
		kthread_stop(tsk);
	tsk = NULL;
	return 1;
}


static int kernel_log_wb_init(void)
{
	if (get_boot_mode() == MSM_BOOT_MODE_FASTBOOT
		|| get_boot_mode() == MSM_BOOT_MODE_RECOVERY) {
			pr_alert("Op_kernel_log: No logging mode %d\n", get_boot_mode());
		return 0;
	}
	wb_page_num = 0;
	while_times = 0;
	no_data = 0;
	buf_offset = 0;
	log_buf_offset = 0;
	log_text_offset = 0;

	pr_alert("Op_kernel_log: kernel_log_wb_int1\n");
	tsk = kthread_run(kernel_log_wb_main, NULL, "Op_kernel_log");
	if (!tsk)
		pr_err("Op_kernel_log: kthread init failed\n");
	pr_alert("Op_kernel_log: init done\n");
	return 0;
}

module_init(kernel_log_wb_init);
