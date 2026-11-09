#ifndef __LINUX_QUICKLOGIC_VX_H
#define __LINUX_QUICKLOGIC_VX_H

#include <linux/types.h>
#include <linux/errno.h>

#ifdef CONFIG_QUICKLOGIC_BRIDGE
/* Write a 32-bit bridge CSR through the bridge's I2C control port */
int quicklogic_vx_write32(u16 reg, u32 val);
int quicklogic_vx_read32(u16 reg, u32 *val);
#else
static inline int quicklogic_vx_write32(u16 reg, u32 val)
{
	return -ENODEV;
}
static inline int quicklogic_vx_read32(u16 reg, u32 *val)
{
	return -ENODEV;
}
#endif

#endif
