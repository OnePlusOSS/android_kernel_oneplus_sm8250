#ifndef __CPUFREQ_BOUNCING_H__
#define __CPUFREQ_BOUNCING_H__

#include <linux/cpufreq.h>

#ifdef CONFIG_CPUFREQ_BOUNCING
void cb_update(struct cpufreq_policy *pol, u64 time);
void cb_reset(int cpu, u64 time);
unsigned int cb_cap(struct cpufreq_policy *pol, unsigned int freq);
unsigned int cb_get_cap(int cpu);
#else
static inline void cb_reset(int cpu, u64 time) {}
static inline void cb_update(struct cpufreq_policy *pol, u64 time) {}
static inline unsigned int cb_cap(struct cpufreq_policy *pol, unsigned int freq) { return freq; }
static inline unsigned int cb_get_cap(int cpu) { return UINT_MAX; }
#endif

#endif

