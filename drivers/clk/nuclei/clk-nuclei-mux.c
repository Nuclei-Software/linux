// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "clk-nuclei.h"

struct nuclei_mux_hw {
	struct clk_hw hw;
	struct nuclei_clk_data *data;
	unsigned int mux_reg;
	unsigned int mux_shift;
	unsigned int mux_width;
	unsigned long max_rate;
	unsigned long flags;
};

#define to_nuclei_mux(_hw) container_of(_hw, struct nuclei_mux_hw, hw)

static u8 nuclei_mux_get_parent(struct clk_hw *hw)
{
	struct nuclei_mux_hw *mux = to_nuclei_mux(hw);
	u32 val;

	val = nuclei_clk_readl(mux->data, mux->mux_reg);
	val = (val >> mux->mux_shift) & ((1 << mux->mux_width) - 1);

	return val;
}

static int nuclei_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct nuclei_mux_hw *mux = to_nuclei_mux(hw);
	unsigned int mask = (1 << mux->mux_width) - 1;

	nuclei_clk_update_bits(mux->data, mux->mux_reg, mask << mux->mux_shift,
			       index << mux->mux_shift);

	return 0;
}

static const struct clk_ops nuclei_mux_ops = {
	.get_parent = nuclei_mux_get_parent,
	.set_parent = nuclei_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

int nuclei_clk_register_muxes(struct device *dev, struct nuclei_mux_desc *descs,
			      unsigned int num, struct nuclei_clk_data *data,
			      struct clk_hw **hws)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num; i++) {
		struct nuclei_mux_hw *mux;
		struct clk_init_data init;
		struct clk_hw *hw;
		struct nuclei_mux_desc *d = &descs[i];

		mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
		if (!mux) {
			ret = -ENOMEM;
			goto err;
		}

		memset(&init, 0, sizeof(init));
		init.name = d->name;
		init.ops = &nuclei_mux_ops;
		init.parent_names = d->parents;
		init.num_parents = d->num_parents;
		init.flags = d->flags;

		mux->hw.init = &init;
		mux->data = data;
		mux->mux_reg = d->mux_reg;
		mux->mux_shift = d->mux_shift;
		mux->mux_width = d->mux_width;
		mux->max_rate = d->max_rate;
		mux->flags = d->flags;

		hw = &mux->hw;
		ret = clk_hw_register(dev, hw);
		if (ret) {
			devm_kfree(dev, mux);
			goto err;
		}

		if (hws)
			hws[d->id] = hw;
	}

	return 0;

err:
	while (i--)
		clk_hw_unregister(hws[descs[i].id]);
	return ret;
}
