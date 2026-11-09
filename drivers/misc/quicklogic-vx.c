/*
 * QuickLogic VX5B3D MIPI-DSI to LVDS bridge, I2C control port.
 *
 * Backported from the 3.4 drivers/misc/quicklogic driver.  The bridge CSRs
 * can be written either with DSI generic long writes or, while the DSI link
 * is busy streaming video, through this I2C port using the same
 * generic-write framing (control byte 0x09, DI 0x29, vendor id 05 01).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_data/quicklogic-vx.h>

#define QL_CONTROL_BYTE_GEN	0x09
#define QL_DI_GEN_LWRITE	0x29
#define QL_CMD_CSR_WRITE	0x40
#define QL_CMD_CSR_LEN		0x41
#define QL_GEN_READ_DI		0x24

static struct i2c_client *ql_client;
static DEFINE_MUTEX(ql_lock);

int quicklogic_vx_write32(u16 reg, u32 val)
{
	u8 buf[11] = { QL_CONTROL_BYTE_GEN, QL_DI_GEN_LWRITE, 0x05, 0x01,
		       QL_CMD_CSR_WRITE };
	int ret;

	if (!ql_client)
		return -ENODEV;

	buf[5] = reg & 0xff;
	buf[6] = reg >> 8;
	buf[7] = val & 0xff;
	buf[8] = (val >> 8) & 0xff;
	buf[9] = (val >> 16) & 0xff;
	buf[10] = (val >> 24) & 0xff;

	mutex_lock(&ql_lock);
	ret = i2c_master_send(ql_client, buf, sizeof(buf));
	udelay(10);
	mutex_unlock(&ql_lock);

	return ret == sizeof(buf) ? 0 : (ret < 0 ? ret : -EIO);
}
EXPORT_SYMBOL(quicklogic_vx_write32);

int quicklogic_vx_read32(u16 reg, u32 *val)
{
	u8 req[9] = { QL_CONTROL_BYTE_GEN, QL_DI_GEN_LWRITE, 0x05, 0x01,
		      QL_CMD_CSR_LEN };
	u8 rd[4] = { QL_CONTROL_BYTE_GEN, QL_GEN_READ_DI, 0x05, 0x01 };
	u8 rx[4];
	int ret;

	if (!ql_client)
		return -ENODEV;

	req[5] = reg & 0xff;
	req[6] = reg >> 8;
	req[7] = 4;		/* length LS */
	req[8] = 0;		/* length MS */

	mutex_lock(&ql_lock);
	ret = i2c_master_send(ql_client, req, sizeof(req));
	if (ret == sizeof(req))
		ret = i2c_master_send(ql_client, rd, sizeof(rd));
	if (ret == sizeof(rd))
		ret = i2c_master_recv(ql_client, rx, sizeof(rx));
	mutex_unlock(&ql_lock);

	if (ret != sizeof(rx))
		return ret < 0 ? ret : -EIO;

	*val = rx[0] | (rx[1] << 8) | (rx[2] << 16) | (rx[3] << 24);
	return 0;
}
EXPORT_SYMBOL(quicklogic_vx_read32);

static int quicklogic_vx_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	ql_client = client;
	dev_info(&client->dev, "QuickLogic VX bridge control port ready\n");
	return 0;
}

static int quicklogic_vx_remove(struct i2c_client *client)
{
	ql_client = NULL;
	return 0;
}

static const struct i2c_device_id quicklogic_vx_id[] = {
	{ "QuickBridge", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, quicklogic_vx_id);

static const struct of_device_id quicklogic_vx_of_match[] = {
	{ .compatible = "quicklogic,vx5b3d" },
	{ }
};
MODULE_DEVICE_TABLE(of, quicklogic_vx_of_match);

static struct i2c_driver quicklogic_vx_driver = {
	.driver = {
		.name = "QuickBridge",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(quicklogic_vx_of_match),
	},
	.probe = quicklogic_vx_probe,
	.remove = quicklogic_vx_remove,
	.id_table = quicklogic_vx_id,
};
module_i2c_driver(quicklogic_vx_driver);

MODULE_DESCRIPTION("QuickLogic VX5B3D bridge I2C control port");
MODULE_LICENSE("GPL");
