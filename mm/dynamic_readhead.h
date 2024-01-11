#include <linux/healthinfo/fg.h>
#include <linux/sched/rt.h>
#include <linux/fs.h>

#ifdef OPLUS_FEATURE_SCHED_ASSIST
extern bool test_task_ux(struct task_struct *task);
#endif

void adjust_readaround(struct file_ra_state *ra, pgoff_t offset);
unsigned long adjust_readahead(struct file_ra_state *ra, unsigned long max_pages);

