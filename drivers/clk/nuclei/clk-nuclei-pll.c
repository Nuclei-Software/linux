// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "clk-nuclei.h"

#define PLL_LOCK_TIMEOUT_US 100000

struct nuclei_pll_hw {
	struct clk_hw hw;
	struct nuclei_clk_data *data;
	unsigned int reg;
	unsigned int n_shift;
	unsigned int n_mask;
	unsigned int m_shift;
	unsigned int m_mask;
	unsigned int od_shift;
	unsigned int od_mask;
	unsigned int bp_shift;
	unsigned int lock_shift;
	unsigned long min_rate;
	unsigned long max_rate;
};

#define to_nuclei_pll(_hw) container_of(_hw, struct nuclei_pll_hw, hw)

static unsigned long nuclei_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct nuclei_pll_hw *pll = to_nuclei_pll(hw);
	u32 val;
	unsigned int n, m, od;
	unsigned long rate;

	val = nuclei_clk_readl(pll->data, pll->reg);

	if (val & BIT(pll->bp_shift))
		return parent_rate;

	n = (val >> pll->n_shift) & pll->n_mask;
	m = (val >> pll->m_shift) & pll->m_mask;
	od = (val >> pll->od_shift) & pll->od_mask;

	if (n == 0 || m == 0)
		return 0;

	/* out = M / (N * 2^OD) * IN */
	rate = div_u64((u64)parent_rate * m, n << od);

	return rate;
}

/*
 * Wait for the PLL to lock (lock bit = 1), with timeout.
 * Returns 0 on lock, -ETIMEDOUT on timeout.
 */
static int nuclei_pll_wait_lock(struct device *dev, struct nuclei_pll_desc *d,
				struct nuclei_clk_data *data)
{
	unsigned int timeout = PLL_LOCK_TIMEOUT_US;
	u32 val;

	do {
		val = nuclei_clk_readl(data, d->reg);
		if (val & BIT(d->lock_shift))
			return 0;
		udelay(1);
	} while (--timeout);

	dev_err(dev, "PLL %s lock timeout\n", d->name);
	return -ETIMEDOUT;
}

/*
 * Read PLL configuration from DTS and apply it at boot time.
 * DT property format: <mux N M OD>
 *   mux: 0=hsi, 1=xtal, 2=clk_in1, 3=clk_in2
 *   N: 1~255, M: 1~1023, OD: 0~63
 * Flow:
 *   1. Read current mux / N / M / OD from hardware
 *   2. Compare with the DTS values; do nothing if identical
 *   3. Otherwise validate and program the registers per DTS
 * If the PLL property is missing in DTS or the parameters are
 * invalid, the PLL is left untouched.
 */
static void nuclei_pll_apply_dt_config(struct device *dev,
				       struct nuclei_pll_desc *d,
				       struct nuclei_clk_data *data)
{
	struct device_node *np = dev->of_node;
	u32 dt_mux, dt_n, dt_m, dt_od;
	u32 hw_mux, hw_n, hw_m, hw_od;
	u32 reg;

	if (!np || !d->dt_prop)
		return;

	/* PLL config not defined in DTS -> do nothing */
	if (of_property_read_u32_index(np, d->dt_prop, 0, &dt_mux) ||
	    of_property_read_u32_index(np, d->dt_prop, 1, &dt_n) ||
	    of_property_read_u32_index(np, d->dt_prop, 2, &dt_m) ||
	    of_property_read_u32_index(np, d->dt_prop, 3, &dt_od))
		return;

	/* Validate parameters */
	if (dt_mux >= (1U << d->input_mux_width)) {
		dev_warn(dev, "PLL %s: invalid mux=%u\n", d->name, dt_mux);
		return;
	}
	if (!dt_n || dt_n > d->n_mask) {
		dev_warn(dev, "PLL %s: invalid N=%u\n", d->name, dt_n);
		return;
	}
	if (!dt_m || dt_m > d->m_mask) {
		dev_warn(dev, "PLL %s: invalid M=%u\n", d->name, dt_m);
		return;
	}
	if (dt_od > d->od_mask) {
		dev_warn(dev, "PLL %s: invalid OD=%u\n", d->name, dt_od);
		return;
	}

	/* Read current hardware values */
	hw_mux = (nuclei_clk_readl(data, d->input_mux_reg) >>
		  d->input_mux_shift) &
		 ((1U << d->input_mux_width) - 1);
	reg = nuclei_clk_readl(data, d->reg);
	hw_n = (reg >> d->n_shift) & d->n_mask;
	hw_m = (reg >> d->m_shift) & d->m_mask;
	hw_od = (reg >> d->od_shift) & d->od_mask;

	/* Same as DTS -> do nothing */
	if (hw_mux == dt_mux && hw_n == dt_n && hw_m == dt_m &&
	    hw_od == dt_od) {
		dev_info(dev,
			 "PLL %s: HW matches DTS (mux=%u N=%u M=%u OD=%u)\n",
			 d->name, hw_mux, hw_n, hw_m, hw_od);
		return;
	}

	dev_info(
		dev,
		"PLL %s: HW(mux=%u N=%u M=%u OD=%u) -> DTS(mux=%u N=%u M=%u OD=%u)\n",
		d->name, hw_mux, hw_n, hw_m, hw_od, dt_mux, dt_n, dt_m, dt_od);

	/* Different -> program per DTS: bypass first, then mux and divider
     * fields, finally de-bypass.
     */
	nuclei_clk_update_bits(data, d->reg, BIT(d->bp_shift),
			       BIT(d->bp_shift));

	nuclei_clk_update_bits(data, d->input_mux_reg,
			       ((1U << d->input_mux_width) - 1)
				       << d->input_mux_shift,
			       dt_mux << d->input_mux_shift);

	nuclei_clk_update_bits(data, d->reg, d->n_mask << d->n_shift,
			       dt_n << d->n_shift);
	nuclei_clk_update_bits(data, d->reg, d->m_mask << d->m_shift,
			       dt_m << d->m_shift);
	nuclei_clk_update_bits(data, d->reg, d->od_mask << d->od_shift,
			       dt_od << d->od_shift);

	/* De-bypass and wait for PLL lock */
	nuclei_clk_update_bits(data, d->reg, BIT(d->bp_shift), 0);
	nuclei_pll_wait_lock(dev, d, data);
}

/* The PLL is configured at boot time from DTS
 * (nuclei_pll_apply_dt_config); at runtime CCF only needs
 * recalc_rate to read and report the frequency.
 */
static const struct clk_ops nuclei_pll_ops = {
	.recalc_rate = nuclei_pll_recalc_rate,
};

int nuclei_clk_register_plls(struct device *dev, struct nuclei_pll_desc *descs,
			     unsigned int num, struct nuclei_clk_data *data,
			     struct clk_hw **hws)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num; i++) {
		struct nuclei_pll_hw *pll;
		struct clk_init_data init;
		struct clk_hw *hw;
		struct nuclei_pll_desc *d = &descs[i];

		pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
		if (!pll) {
			ret = -ENOMEM;
			goto err;
		}

		memset(&init, 0, sizeof(init));
		init.name = d->name;
		init.ops = &nuclei_pll_ops;
		init.parent_names = &d->parent;
		init.num_parents = 1;
		init.flags = 0;

		pll->hw.init = &init;
		pll->data = data;
		pll->reg = d->reg;
		pll->n_shift = d->n_shift;
		pll->n_mask = d->n_mask;
		pll->m_shift = d->m_shift;
		pll->m_mask = d->m_mask;
		pll->od_shift = d->od_shift;
		pll->od_mask = d->od_mask;
		pll->bp_shift = d->bp_shift;
		pll->lock_shift = d->lock_shift;
		pll->min_rate = d->min_rate;
		pll->max_rate = d->max_rate;

		hw = &pll->hw;
		ret = clk_hw_register(dev, hw);
		if (ret) {
			devm_kfree(dev, pll);
			goto err;
		}

		if (hws)
			hws[d->id] = hw;

		/* Apply PLL boot-time config from DTS (mux/N/M/OD) */
		nuclei_pll_apply_dt_config(dev, d, data);
	}

	return 0;

err:
	while (i--)
		clk_hw_unregister(hws[descs[i].id]);
	return ret;
}
