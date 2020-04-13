#ifndef _DSI_IRIS5_IOCTL_H_
#define _DSI_IRIS5_IOCTL_H_

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

int iris_configure(u32 display, u32 type, u32 value);
int iris_configure_ex(u32 display, u32 type, u32 count, u32 *values);
int iris_configure_get(u32 display, u32 type, u32 count, u32 *values);
int iris_adb_type_debugfs_init(struct dsi_display *display);

#endif // _DSI_IRIS5_IOCTL_H_
