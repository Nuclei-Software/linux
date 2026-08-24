// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#include <linux/reset-controller.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <dt-bindings/reset/nuclei-reset.h>

#define RESET_CTRL0_OFS 0x20
#define RESET_CTRL1_OFS 0x24
#define RESET_CTRL2_OFS 0x28
#define RESET_CTRL3_OFS 0x2C

struct nuclei_reset_data {
	void __iomem *base[3];/* 0=SYS, 1=XEC, 2=USB */
	struct reset_controller_dev rcdev;
	spinlock_t lock;
};

static void nuclei_reset_id_to_reg(unsigned long id, unsigned int *reg_off,
				   unsigned int *bit, unsigned int *base_idx)
{
	unsigned int reg_index = id / 32;
	*bit = id % 32;
	*reg_off = RESET_CTRL0_OFS + reg_index * 4;
	*base_idx = (id == RST_XEC_GEN21) ? 1 : ((id == RST_USB_TOP0) ? 2 : 0);
}

static int nuclei_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct nuclei_reset_data *data =
		container_of(rcdev, struct nuclei_reset_data, rcdev);
	unsigned int reg_off, bit;
	u32 val, idx;

	nuclei_reset_id_to_reg(id, &reg_off, &bit, &idx);

	val = readl(data->base[idx] + reg_off);
	return !(val & BIT(bit)); /* 0=reset, 1=release */
}

static int nuclei_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct nuclei_reset_data *data =
		container_of(rcdev, struct nuclei_reset_data, rcdev);
	unsigned int reg_off, bit;
	unsigned long flags;
	u32 val, idx;

	nuclei_reset_id_to_reg(id, &reg_off, &bit, &idx);

	spin_lock_irqsave(&data->lock, flags);
	val = readl(data->base[idx] + reg_off);
	val &= ~BIT(bit); /* write 0 to reset */
	writel(val, data->base[idx] + reg_off);
	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int nuclei_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct nuclei_reset_data *data =
		container_of(rcdev, struct nuclei_reset_data, rcdev);
	unsigned int reg_off, bit;
	unsigned long flags;
	u32 val, idx;

	nuclei_reset_id_to_reg(id, &reg_off, &bit, &idx);

	spin_lock_irqsave(&data->lock, flags);
	val = readl(data->base[idx] + reg_off);
	val |= BIT(bit); /* write 1 to release */
	writel(val, data->base[idx] + reg_off);
	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static const struct reset_control_ops nuclei_reset_ops = {
	.status = nuclei_reset_status,
	.assert = nuclei_reset_assert,
	.deassert = nuclei_reset_deassert,
};

int nuclei_reset_register(struct device *dev, void __iomem **base)
{
	struct nuclei_reset_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base[0] = base[0];
	data->base[1] = base[1];
	data->base[2] = base[2];

	spin_lock_init(&data->lock);

	data->rcdev.ops = &nuclei_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = 128;
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}
