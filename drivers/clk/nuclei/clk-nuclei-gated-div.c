// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "clk-nuclei.h"

#define DIV_MAX_VAL 255
/* max divider value; register stores (div - 1) */
#define DIV_MAX_DIV (DIV_MAX_VAL + 1)

struct nuclei_gated_div_hw {
	struct clk_hw hw;
	struct nuclei_clk_data *data;
	unsigned int div_reg;
	unsigned int gate_reg;
	unsigned int gate_bit;
	unsigned long max_rate;
};

#define to_nuclei_gated_div(_hw) \
	container_of(_hw, struct nuclei_gated_div_hw, hw)

static int nuclei_gated_div_enable(struct clk_hw *hw)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);

	if (!div->gate_reg)
		return 0;

	nuclei_clk_update_bits(div->data, div->gate_reg, BIT(div->gate_bit),
			       BIT(div->gate_bit));
	return 0;
}

static void nuclei_gated_div_disable(struct clk_hw *hw)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);

	if (!div->gate_reg)
		return;
	nuclei_clk_update_bits(div->data, div->gate_reg, BIT(div->gate_bit), 0);
}

static int nuclei_gated_div_is_enabled(struct clk_hw *hw)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);
	u32 val;

	if (!div->gate_reg)
		return 1;

	val = nuclei_clk_readl(div->data, div->gate_reg);
	return !!(val & BIT(div->gate_bit));
}

static unsigned long nuclei_gated_div_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);
	u32 val;
	unsigned int div_val;

	val = nuclei_clk_readl(div->data, div->div_reg);
	div_val = (val & 0xFF) + 1;

	return parent_rate / div_val;
}

static long nuclei_gated_div_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);
	unsigned long parent = *parent_rate;
	unsigned int div_val;

	if (rate == 0 || parent == 0)
		return -EINVAL;

	/* 1. Must not exceed the maximum frequency */
	if (div->max_rate && rate > div->max_rate)
		rate = div->max_rate;

	/* 2. Compute the divider; register stores (div - 1), max div = 256 */
	div_val = DIV_ROUND_UP(parent, rate);
	if (div_val < 1)
		div_val = 1;
	if (div_val > DIV_MAX_DIV)
		div_val = DIV_MAX_DIV;

	return parent / div_val;
}

static int nuclei_gated_div_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);
	unsigned int div_val;

	if (rate == 0 || parent_rate == 0)
		return -EINVAL;

	/* 1. Must not exceed the maximum frequency */
	if (div->max_rate && rate > div->max_rate)
		rate = div->max_rate;

	/* 2. Compute the divider; register stores (div - 1), max div = 256 */
	div_val = DIV_ROUND_UP(parent_rate, rate);
	if (div_val < 1)
		div_val = 1;
	if (div_val > DIV_MAX_DIV)
		div_val = DIV_MAX_DIV;

	nuclei_clk_writel(div->data, div->div_reg, div_val - 1);

	return 0;
}

static int nuclei_gated_div_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	struct nuclei_gated_div_hw *div = to_nuclei_gated_div(hw);
	struct clk_hw *mux = clk_hw_get_parent(hw);
	unsigned long rate = req->rate;
	unsigned long best_rate = 0, best_parent_rate = 0;
	int j, num;

	if (!mux || rate == 0)
		return -EINVAL;

	if (div->max_rate && rate > div->max_rate)
		rate = div->max_rate;

	num = clk_hw_get_num_parents(mux);
	for (j = 0; j < num; j++) {
		struct clk_hw *src = clk_hw_get_parent_by_index(mux, j);
		unsigned long p_rate, div_val, out_rate;

		if (!src)
			continue;

		p_rate = clk_hw_get_rate(src);
		if (!p_rate)
			continue;

		div_val = DIV_ROUND_UP(p_rate, rate);
		if (div_val < 1)
			div_val = 1;
		if (div_val > DIV_MAX_DIV)
			div_val = DIV_MAX_DIV;

		out_rate = p_rate / div_val;
		if (out_rate <= rate && out_rate > best_rate) {
			best_rate = out_rate;
			best_parent_rate = p_rate;
		}
	}

	/* Fall back to the current mux rate if nothing better is found */
	if (!best_rate) {
		unsigned long cur = clk_hw_get_rate(mux);

		if (!cur)
			return -EINVAL;
		best_parent_rate = cur;
		best_rate = cur / DIV_ROUND_UP(cur, rate);
	}

	req->rate = best_rate;
	req->best_parent_hw = mux;
	req->best_parent_rate = best_parent_rate;

	return 0;
}

static const struct clk_ops nuclei_gated_div_ops = {
	.enable = nuclei_gated_div_enable,
	.disable = nuclei_gated_div_disable,
	.is_enabled = nuclei_gated_div_is_enabled,
	.recalc_rate = nuclei_gated_div_recalc_rate,
	.determine_rate = nuclei_gated_div_determine_rate,
	.round_rate = nuclei_gated_div_round_rate,
	.set_rate = nuclei_gated_div_set_rate,
};

int nuclei_clk_register_gated_divs(struct device *dev,
				   struct nuclei_gated_div_desc *descs,
				   unsigned int num,
				   struct nuclei_clk_data *data,
				   struct clk_hw **hws)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num; i++) {
		struct nuclei_gated_div_hw *div;
		struct clk_init_data init;
		struct clk_hw *hw;
		struct nuclei_gated_div_desc *d = &descs[i];

		div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
		if (!div) {
			ret = -ENOMEM;
			goto err;
		}

		memset(&init, 0, sizeof(init));
		init.name = d->name;
		init.ops = &nuclei_gated_div_ops;
		init.parent_names = &d->parent;
		init.num_parents = 1;
		init.flags = d->flags;

		div->hw.init = &init;
		div->data = data;
		div->div_reg = d->div_reg;
		div->gate_reg = d->gate_reg;
		div->gate_bit = d->gate_bit;
		div->max_rate = d->max_rate;

		hw = &div->hw;
		ret = clk_hw_register(dev, hw);
		if (ret) {
			devm_kfree(dev, div);
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
