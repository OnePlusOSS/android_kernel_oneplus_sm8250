/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_MEMORY_ISOLATE__
#define __OPLUS_MEMORY_ISOLATE__

#define OPLUS2_ORDER 2

#define is_oplus2_order(order) \
		unlikely(OPLUS2_ORDER == order)

#define is_migrate_oplus2(mt) \
		unlikely(mt == MIGRATE_OPLUS2)

extern void setup_zone_migrate_oplus(struct zone *zone, int reserve_migratetype);

#endif /*__OPLUS_MEMORY_ISOLATE__*/
