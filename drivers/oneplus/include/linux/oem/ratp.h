#ifndef __RATP_H__
#define __RATP_H__

#ifdef CONFIG_RATP
extern bool is_ratp_enable(void);
extern bool is_allowmost_enable(void);
extern bool is_gmod_enable(void);
#else
static inline bool is_ratp_enable(void) { return false; }
static inline bool is_allowmost_enable(void) { return false; }
static inline bool is_gmod_enable(void) { return false; }
#endif

#endif
