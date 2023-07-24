#ifndef _ZRAM_DRV_INTERNAL_H_
#define _ZRAM_DRV_INTERNAL_H_

#include "zram_drv.h"

#ifdef BIT
#undef BIT
#define BIT(nr)		(1lu << (nr))
#endif

#define zram_slot_lock(zram, index) (bit_spin_lock(ZRAM_LOCK, &zram->table[index].flags))

#define zram_slot_unlock(zram, index) (bit_spin_unlock(ZRAM_LOCK, &zram->table[index].flags))

#define init_done(zram)  (zram->disksize)

#define dev_to_zram(dev) ((struct zram *)dev_to_disk(dev)->private_data)

#define zram_get_handle(zram, index) ((unsigned long)(zram->table[index].entry))

#define zram_set_handle(zram, index, handle_val) (zram->table[index].entry = (struct zram_entry *)handle_val)

#define zram_test_flag(zram, index,  flag) (zram->table[index].flags & BIT(flag))

#define zram_set_flag(zram, index, flag) (zram->table[index].flags |= BIT(flag))

#define zram_clear_flag(zram, index, flag) (zram->table[index].flags &= ~BIT(flag))

#define zram_set_element(zram, index, element) (zram->table[index].element = element)

#define zram_get_obj_size(zram, index) (zram->table[index].flags & (BIT(ZRAM_FLAG_SHIFT) - 1))

#define zram_set_obj_size(zram, index, size) do {\
	unsigned long flags = zram->table[index].flags >> ZRAM_FLAG_SHIFT; \
	zram->table[index].flags = (flags << ZRAM_FLAG_SHIFT) | size; \
} while(0)

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
extern int async_compress_page(struct zram *zram, struct page* page);
extern void update_zram_index(struct zram *zram, u32 index, unsigned long page);
#endif
#endif /* _ZRAM_DRV_INTERNAL_H_ */
