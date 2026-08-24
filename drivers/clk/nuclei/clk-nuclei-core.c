// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-nuclei.h"

static const char *const pll_input_parents[] = {
	"hsi",
	"hse",
	"clk_in1",
	"clk_in2",
};

static const char *const cluster_parents[] = {
	"hsi", "hse", "sys_pll", "core1_pll", "xuc_pll",
};

static const char *const display_parents[] = {
	"hsi",	   "sys_pll", "sys_pll_div2", "core1_pll",
	"xuc_pll", "clk_in1", "clk_in2",
};

static struct nuclei_mux_desc clk_muxes[] = {
	{
		.name = "mux_core1_pll_in",
		.parents = pll_input_parents,
		.num_parents = 4,
		.mux_reg = CLK_CTRL2_CORE1_PLL_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 2,
		.flags = 0,
		.max_rate = 0,
		.id = CLK_MUX_CORE1_PLL_IN,
	},
	{
		.name = "mux_xuc_pll_in",
		.parents = pll_input_parents,
		.num_parents = 4,
		.mux_reg = CLK_CTRL3_XUC_PLL_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 2,
		.flags = 0,
		.max_rate = 0,
		.id = CLK_MUX_XUC_PLL_IN,
	},
	{
		.name = "mux_cluster1",
		.parents = cluster_parents,
		.num_parents = 5,
		.mux_reg = CLK_CTRL6_CLUSTER1_CLK_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 400000000,
		.id = CLK_MUX_CLUSTER1,
	},
	{
		.name = "mux_cluster2",
		.parents = cluster_parents,
		.num_parents = 5,
		.mux_reg = CLK_CTRL7_CLUSTER2_CLK_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 400000000,
		.id = CLK_MUX_CLUSTER2,
	},
	{
		.name = "mux_nacc0",
		.parents = cluster_parents,
		.num_parents = 5,
		.mux_reg = CLK_CTRL8_NACCO_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 500000000,
		.id = CLK_MUX_NACC0,
	},
	{
		.name = "mux_disp_pixel",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL14_DISP_PIXEL_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_DISP_PIXEL,
	},
	{
		.name = "mux_dcmi_pixel",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL15_DCMI_PIXEL_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_DCMI_PIXEL,
	},
	{
		.name = "mux_xec_sys",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL16_XEC_SYS_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 500000000,
		.id = CLK_MUX_XEC_SYS,
	},
	{
		.name = "mux_xec_rmii",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL17_XEC_RMII_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 500000000,
		.id = CLK_MUX_XEC_RMII,
	},
	{
		.name = "mux_xuc",
		.parents = cluster_parents,
		.num_parents = 5,
		.mux_reg = CLK_CTRL18_XUC_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 3,
		.flags = 0,
		.max_rate = 500000000,
		.id = CLK_MUX_XUC,
	},
	{
		.name = "mux_sai_s0",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL9_SAI_S0_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 4,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_SAI_S0,
	},
	{
		.name = "mux_sai_s1",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL10_SAI_S1_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 4,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_SAI_S1,
	},
	{
		.name = "mux_sai_s2",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL11_SAI_S2_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 4,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_SAI_S2,
	},
	{
		.name = "mux_sai_s3",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL12_SAI_S3_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 4,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_SAI_S3,
	},
	{
		.name = "mux_spdifrx0",
		.parents = display_parents,
		.num_parents = 7,
		.mux_reg = CLK_CTRL13_SPDIFRX0_CLK_S_OFS,
		.mux_shift = 0,
		.mux_width = 4,
		.flags = 0,
		.max_rate = 200000000,
		.id = CLK_MUX_SPDIFRX0,
	},
};

static struct nuclei_pll_desc clk_plls[] = {
	{
		.name = "core1_pll",
		.parent = "mux_core1_pll_in",
		.reg = PLL_CTRL1_CORE1_PLL_CLK_OFS,
		.n_shift = 0,
		.n_mask = 0xFF,
		.m_shift = 8,
		.m_mask = 0x3FF,
		.od_shift = 18,
		.od_mask = 0x3F,
		.bp_shift = 24,
		.lock_shift = 25,
		.min_rate = 400000000,
		.max_rate = 1600000000,
		/* DT boot config: core1-pll = <mux N M OD> */
		.dt_prop = "core1-pll",
		.input_mux_reg = CLK_CTRL2_CORE1_PLL_CLK_S_OFS,
		.input_mux_shift = 0,
		.input_mux_width = 2,
		.id = CLK_CORE1_PLL,
	},
	{
		.name = "xuc_pll",
		.parent = "mux_xuc_pll_in",
		.reg = PLL_CTRL2_XUC_PLL_CLK_OFS,
		.n_shift = 0,
		.n_mask = 0xFF,
		.m_shift = 8,
		.m_mask = 0x3FF,
		.od_shift = 18,
		.od_mask = 0x3F,
		.bp_shift = 24,
		.lock_shift = 25,
		.min_rate = 400000000,
		.max_rate = 1600000000,
		/* DT boot config: xuc-pll = <mux N M OD> */
		.dt_prop = "xuc-pll",
		.input_mux_reg = CLK_CTRL3_XUC_PLL_CLK_S_OFS,
		.input_mux_shift = 0,
		.input_mux_width = 2,
		.id = CLK_XUC_PLL,
	},
};

static struct nuclei_gated_div_desc clk_gated_divs[] = {
	/* ============================================================
     * NACC - max_rate=250M
     * ============================================================ */
	{ "nacc0_clk_i", "mux_nacc0", MISC_SYS, CLK_CTRL38_NACC0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 29, 250000000, CLK_SET_RATE_PARENT, CLK_NACC0 },

	/* ============================================================
     * adv timer - max_rate=100M
     * ============================================================ */
	{ "adv_timer0_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL104_ADVANCED_TIMER0_CLK_OFS, SUBM_CLK_CTRL1_OFS, 7, 100000000,
	  0, CLK_ADV_TIMER0 },
	{ "adv_timer1_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL105_ADVANCED_TIMER1_CLK_OFS, SUBM_CLK_CTRL1_OFS, 8, 100000000,
	  0, CLK_ADV_TIMER1 },
	{ "adv_timer2_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL106_ADVANCED_TIMER2_CLK_OFS, SUBM_CLK_CTRL1_OFS, 9, 100000000,
	  0, CLK_ADV_TIMER2 },
	{ "adv_timer3_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL107_ADVANCED_TIMER3_CLK_OFS, SUBM_CLK_CTRL1_OFS, 10,
	  100000000, 0, CLK_ADV_TIMER3 },
	{ "adv_timer4_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL108_ADVANCED_TIMER4_CLK_OFS, SUBM_CLK_CTRL1_OFS, 11,
	  100000000, 0, CLK_ADV_TIMER4 },
	{ "adv_timer5_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL109_ADVANCED_TIMER5_CLK_OFS, SUBM_CLK_CTRL1_OFS, 12,
	  100000000, 0, CLK_ADV_TIMER5 },
	{ "adv_timer6_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL110_ADVANCED_TIMER6_CLK_OFS, SUBM_CLK_CTRL1_OFS, 13,
	  100000000, 0, CLK_ADV_TIMER6 },
	{ "adv_timer7_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL111_ADVANCED_TIMER7_CLK_OFS, SUBM_CLK_CTRL1_OFS, 14,
	  100000000, 0, CLK_ADV_TIMER7 },
	{ "adv_timer8_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL112_ADVANCED_TIMER8_CLK_OFS, SUBM_CLK_CTRL1_OFS, 15,
	  100000000, 0, CLK_ADV_TIMER8 },
	{ "adv_timer9_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL113_ADVANCED_TIMER9_CLK_OFS, SUBM_CLK_CTRL1_OFS, 16,
	  100000000, 0, CLK_ADV_TIMER9 },
	{ "adv_timer10_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL114_ADVANCED_TIMER10_CLK_OFS, SUBM_CLK_CTRL1_OFS, 17,
	  100000000, 0, CLK_ADV_TIMER10 },
	{ "adv_timer11_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL115_ADVANCED_TIMER11_CLK_OFS, SUBM_CLK_CTRL1_OFS, 18,
	  100000000, 0, CLK_ADV_TIMER11 },
	{ "adv_timer12_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL116_ADVANCED_TIMER12_CLK_OFS, SUBM_CLK_CTRL1_OFS, 19,
	  100000000, 0, CLK_ADV_TIMER12 },
	{ "adv_timer13_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL117_ADVANCED_TIMER13_CLK_OFS, SUBM_CLK_CTRL1_OFS, 20,
	  100000000, 0, CLK_ADV_TIMER13 },
	{ "adv_timer14_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL118_ADVANCED_TIMER14_CLK_OFS, SUBM_CLK_CTRL1_OFS, 21,
	  100000000, 0, CLK_ADV_TIMER14 },
	{ "adv_timer15_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL119_ADVANCED_TIMER15_CLK_OFS, SUBM_CLK_CTRL1_OFS, 22,
	  100000000, 0, CLK_ADV_TIMER15 },

	/* ============================================================
     * G2D/JPEG/FFT/IDU/FILTER - max_rate=100M
     * ============================================================ */
	{ "g2d0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL25_G2D0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 27, 100000000, 0, CLK_G2D0 },
	{ "jpeg0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL26_JPEG0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 28, 100000000, 0, CLK_JPEG0 },
	{ "fft0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL28_FFT0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 0, 100000000, 0, CLK_FFT0 },
	{ "idu_clk_i", "sys_clk", MISC_SYS, CLK_CTRL29_IDU_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 8, 100000000, 0, CLK_IDU },
	{ "filter0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL39_FILTER0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 30, 100000000, 0, CLK_FILTER0 },
	{ "flt_macc0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL27_FLT_MACC0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 31, 100000000, 0, CLK_FLT_MACC0 },

	/* ============================================================
     * USART0-12 - max_rate=100M
     * ============================================================ */
	{ "usart0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL41_USART0_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 9, 100000000, 0, CLK_USART0 },
	{ "usart0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL42_USART0_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 9, 100000000, 0,
	  CLK_USART0_INTF },
	{ "usart1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL43_USART1_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 10, 100000000, 0, CLK_USART1 },
	{ "usart1_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL44_USART1_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 10, 100000000,
	  0, CLK_USART1_INTF },
	{ "usart2_clk_i", "sys_clk", MISC_SYS, CLK_CTRL45_USART2_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 11, 100000000, 0, CLK_USART2 },
	{ "usart2_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL46_USART2_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 11, 100000000,
	  0, CLK_USART2_INTF },
	{ "usart3_clk_i", "sys_clk", MISC_SYS, CLK_CTRL47_USART3_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 12, 100000000, 0, CLK_USART3 },
	{ "usart3_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL48_USART3_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 12, 100000000,
	  0, CLK_USART3_INTF },
	{ "usart4_clk_i", "sys_clk", MISC_SYS, CLK_CTRL49_USART4_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 13, 100000000, 0, CLK_USART4 },
	{ "usart4_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL50_USART4_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 13, 100000000,
	  0, CLK_USART4_INTF },
	{ "usart5_clk_i", "sys_clk", MISC_SYS, CLK_CTRL51_USART5_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 14, 100000000, 0, CLK_USART5 },
	{ "usart5_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL52_USART5_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 14, 100000000,
	  0, CLK_USART5_INTF },
	{ "usart6_clk_i", "sys_clk", MISC_SYS, CLK_CTRL53_USART6_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 15, 100000000, 0, CLK_USART6 },
	{ "usart6_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL54_USART6_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 15, 100000000,
	  0, CLK_USART6_INTF },
	{ "usart7_clk_i", "sys_clk", MISC_SYS, CLK_CTRL55_USART7_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 16, 100000000, 0, CLK_USART7 },
	{ "usart7_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL56_USART7_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 16, 100000000,
	  0, CLK_USART7_INTF },
	{ "usart8_clk_i", "sys_clk", MISC_SYS, CLK_CTRL57_USART8_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 17, 100000000, 0, CLK_USART8 },
	{ "usart8_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL58_USART8_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 17, 100000000,
	  0, CLK_USART8_INTF },
	{ "usart9_clk_i", "sys_clk", MISC_SYS, CLK_CTRL59_USART9_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 18, 100000000, 0, CLK_USART9 },
	{ "usart9_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL60_USART9_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 18, 100000000,
	  0, CLK_USART9_INTF },
	{ "usart10_clk_i", "sys_clk", MISC_SYS, CLK_CTRL61_USART10_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 19, 100000000, 0, CLK_USART10 },
	{ "usart10_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL62_USART10_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 19, 100000000,
	  0, CLK_USART10_INTF },
	{ "usart11_clk_i", "sys_clk", MISC_SYS, CLK_CTRL63_USART11_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 20, 100000000, 0, CLK_USART11 },
	{ "usart11_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL64_USART11_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 20, 100000000,
	  0, CLK_USART11_INTF },
	{ "usart12_clk_i", "sys_clk", MISC_SYS, CLK_CTRL65_USART12_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 21, 100000000, 0, CLK_USART12 },
	{ "usart12_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL66_USART12_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 21, 100000000,
	  0, CLK_USART12_INTF },

	/* ============================================================
     * I2C0-3 - max_rate=100M
     * ============================================================ */
	{ "i2c0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL68_I2C0_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 22, 100000000, 0, CLK_I2C0 },
	{ "i2c0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL69_I2C0_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 22, 100000000, 0,
	  CLK_I2C0_INTF },
	{ "i2c1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL70_I2C1_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 23, 100000000, 0, CLK_I2C1 },
	{ "i2c1_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL71_I2C1_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 23, 100000000, 0,
	  CLK_I2C1_INTF },
	{ "i2c2_clk_i", "sys_clk", MISC_SYS, CLK_CTRL72_I2C2_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 24, 100000000, 0, CLK_I2C2 },
	{ "i2c2_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL73_I2C2_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 24, 100000000, 0,
	  CLK_I2C2_INTF },
	{ "i2c3_clk_i", "sys_clk", MISC_SYS, CLK_CTRL74_I2C3_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 25, 100000000, 0, CLK_I2C3 },
	{ "i2c3_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL75_I2C3_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 25, 100000000, 0,
	  CLK_I2C3_INTF },

	/* ============================================================
     * QSPI_XIP0-2 - max_rate=100M
     * ============================================================ */
	{ "qspi_xip0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL78_QSPI_XIP0_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 28, 100000000, 0, CLK_QSPI_XIP0 },
	{ "qspi_xip0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL79_QSPI_XIP0_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 28,
	  100000000, 0, CLK_QSPI_XIP0_INTF },
	{ "qspi_xip1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL80_QSPI_XIP1_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 29, 100000000, 0, CLK_QSPI_XIP1 },
	{ "qspi_xip1_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL81_QSPI_XIP1_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 29,
	  100000000, 0, CLK_QSPI_XIP1_INTF },
	{ "qspi_xip2_clk_i", "sys_clk", MISC_SYS, CLK_CTRL82_QSPI_XIP2_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 30, 100000000, 0, CLK_QSPI_XIP2 },
	{ "qspi_xip2_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL83_QSPI_XIP2_INTF_CLK_I_OFS, SUBM_CLK_CTRL0_OFS, 30,
	  100000000, 0, CLK_QSPI_XIP2_INTF },

	/* ============================================================
     * QSPI3-6 (SPI) - max_rate=100M
     * ============================================================ */
	{ "qspi3_clk_i", "sys_clk", MISC_SYS, CLK_CTRL84_QSPI3_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 0, 100000000, 0, CLK_QSPI3 },
	{ "qspi3_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL85_QSPI3_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 0, 100000000, 0,
	  CLK_QSPI3_INTF },
	{ "qspi4_clk_i", "sys_clk", MISC_SYS, CLK_CTRL86_QSPI4_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 0, 100000000, 0, CLK_QSPI4 },
	{ "qspi4_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL87_QSPI4_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 0, 100000000, 0,
	  CLK_QSPI4_INTF },
	{ "qspi5_clk_i", "sys_clk", MISC_SYS, CLK_CTRL88_QSPI5_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 1, 100000000, 0, CLK_QSPI5 },
	{ "qspi5_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL89_QSPI5_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 1, 100000000, 0,
	  CLK_QSPI5_INTF },
	{ "qspi6_clk_i", "sys_clk", MISC_SYS, CLK_CTRL90_QSPI6_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 2, 100000000, 0, CLK_QSPI6 },
	{ "qspi6_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL91_QSPI6_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 2, 100000000, 0,
	  CLK_QSPI6_INTF },

	/* ============================================================
     * XKAN0-2 - max_rate=100M
     * ============================================================ */
	{ "xkan0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL92_XKAN0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 3, 100000000, 0, CLK_XKAN0 },
	{ "xkan0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL93_XKAN0_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 3, 100000000, 0,
	  CLK_XKAN0_INTF },
	{ "xkan1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL94_XKAN1_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 4, 100000000, 0, CLK_XKAN1 },
	{ "xkan1_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL95_XKAN1_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 4, 100000000, 0,
	  CLK_XKAN1_INTF },
	{ "xkan2_clk_i", "sys_clk", MISC_SYS, CLK_CTRL96_XKAN2_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 5, 100000000, 0, CLK_XKAN2 },
	{ "xkan2_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL97_XKAN2_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 5, 100000000, 0,
	  CLK_XKAN2_INTF },

	/* ============================================================
     * SAI - max_rate=100M
     * ============================================================ */
	{ "sai0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL98_SAI0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 6, 100000000, 0, CLK_SAI0 },
	{ "sai0_s0_intf_clk_i", "mux_sai_s0", MISC_SYS,
	  CLK_CTRL99_SAI0_S0_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 6, 100000000,
	  CLK_SET_RATE_PARENT, CLK_SAI0_S0_INTF },
	{ "sai0_s1_intf_clk_i", "mux_sai_s1", MISC_SYS,
	  CLK_CTRL100_SAI0_S1_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 6, 100000000,
	  CLK_SET_RATE_PARENT, CLK_SAI0_S1_INTF },
	{ "sai0_s2_intf_clk_i", "mux_sai_s2", MISC_SYS,
	  CLK_CTRL101_SAI0_S2_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 6, 100000000,
	  CLK_SET_RATE_PARENT, CLK_SAI0_S2_INTF },

	/* ============================================================
     * SDIO - max_rate=200M
     * ============================================================ */
	{ "sdio0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL136_SDIO0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 21, 200000000, 0, CLK_SDIO0 },
	{ "sdio0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL137_SDIO0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 21, 200000000,
	  0, CLK_SDIO0_INTF },
	{ "hs_sdio0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL138_HS_SDIO0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 22, 200000000, 0, CLK_HS_SDIO0 },
	{ "hs_sdio0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL139_HS_SDIO0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 22,
	  200000000, 0, CLK_HS_SDIO0_INTF },

	/* ============================================================
     * XEC - max_rate=125M
     * ============================================================ */
	{ "xec_gen20_sys_clk_i", "mux_xec_sys", MISC_SYS,
	  CLK_CTRL126_XEC_GEN20_SYS_CLK_OFS, SUBM_CLK_CTRL2_OFS, 2, 25000000, 0,
	  CLK_XEC_GEN20_SYS },
	{ "xec_gen21_sys_clk_i", "mux_xec_sys", MISC_XEC,
	  CLK_CTRL127_XEC_GEN21_SYS_CLK_OFS, SUBM_CLK_CTRL2_OFS, 3, 125000000,
	  0, CLK_XEC_GEN21_SYS },
	{ "xec_gen20_rmii_clk_ref_i", "mux_xec_rmii", MISC_SYS,
	  CLK_CTRL128_RMII_CLK_REF_OFS, SUBM_CLK_CTRL2_OFS, 2, 50000000, 0,
	  CLK_XEC_GEN20_RMII_REF },
	{ "xec_gen21_rmii_clk_ref_i", "mux_xec_rmii", MISC_XEC,
	  CLK_CTRL128_RMII_CLK_REF_OFS, SUBM_CLK_CTRL2_OFS, 3, 50000000, 0,
	  CLK_XEC_GEN21_RMII_REF },
	{ "xec_gen20_ptp_ref_clk_i", "mux_xec_sys", MISC_SYS,
	  CLK_CTRL129_PTP_REF_CLK_OFS, SUBM_CLK_CTRL2_OFS, 2, 125000000, 0,
	  CLK_XEC_GEN20_PTP_REF },
	{ "xec_gen21_ptp_ref_clk_i", "mux_xec_sys", MISC_XEC,
	  CLK_CTRL129_PTP_REF_CLK_OFS, SUBM_CLK_CTRL2_OFS, 3, 125000000, 0,
	  CLK_XEC_GEN21_PTP_REF },

	/* ============================================================
     * XUC special frequency
     * ============================================================ */
	{ "xuc0_clk_i", "mux_xuc", MISC_SYS, CLK_CTRL130_XUC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 4, 120000000, 0, CLK_XUC0 },
	{ "xuc_clk_phy_i", "mux_xuc", MISC_SYS, CLK_CTRL131_XUC_CLK_PHY_OFS,
	  SUBM_CLK_CTRL2_OFS, 4, 60000000, 0, CLK_XUC_PHY },

	/* ============================================================
     * USB special frequency
     * ============================================================ */
	{ "usb_top0_clk_i", "mux_xuc", MISC_USB, CLK_CTRL132_USB_TOP0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 5, 160000000, 0, CLK_USB_TOP0 },
	{ "usb_top_clkcore_i", "mux_xuc", MISC_USB,
	  CLK_CTRL135_USB_TOP0_CLKCORE_OFS, SUBM_CLK_CTRL2_OFS, 5, 20000000, 0,
	  CLK_USB_TOP_CORECORE },

	/* ============================================================
     * DISP - max_rate=100M/200M
     * ============================================================ */
	{ "disp0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL121_DISP0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 25, 100000000, 0, CLK_DISP0 },
	{ "disp0_intf_clk_i", "mux_disp_pixel", MISC_SYS,
	  CLK_CTRL122_DISP0_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 25, 200000000,
	  CLK_SET_RATE_PARENT, CLK_DISP0_INTF },
	{ "disp0_aon_clk_i", "sys_clk", MISC_SYS, CLK_CTRL121_DISP0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 25, 100000000, 0, CLK_DISP0_AON },

	/* ============================================================
     * DCMI - max_rate=100M/200M
     * ============================================================ */
	{ "dcmi0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL123_DCMI0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 26, 100000000, 0, CLK_DCMI0 },
	{ "dcmi0_intf_clk_i", "mux_dcmi_pixel", MISC_SYS,
	  CLK_CTRL124_DCMI0_INTF_CLK_I_OFS, SUBM_CLK_CTRL1_OFS, 26, 200000000,
	  CLK_SET_RATE_PARENT, CLK_DCMI0_INTF },
	{ "dcmi0_aon_clk_i", "sys_clk", MISC_SYS, CLK_CTRL123_DCMI0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 26, 100000000, 0, CLK_DCMI0_AON },

	/* ============================================================
     * others - max_rate=100M
     * ============================================================ */
	{ "lgpio0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL103_LGPIO0_CLK_OFS,
	  SUBM_CLK_CTRL1_OFS, 23, 100000000, 0, CLK_LGPIO0 },
	{ "iomux_clk_i", "sys_clk", MISC_SYS, CLK_CTRL40_IOMUX_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 20, 100000000, 0, CLK_IOMUX },
	{ "gmc0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL125_GMC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 1, 100000000, 0, CLK_GMC0 },
	{ "pcrc0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL120_PCRC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 10, 100000000, 0, CLK_PCRC0 },
	{ "pid0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL67_PID0_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 0, 100000000, 0, CLK_PID0 },
	{ "soc_glue_clk_i", "sys_clk", MISC_SYS, CLK_CTRL30_SOC_GLUE_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 9, 100000000, 0, CLK_SOC_GLUE },
	{ "sys_udma_clk_i", "sys_clk", MISC_SYS, CLK_CTRL32_SYS_UDMA_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 19, 100000000, 0, CLK_SYS_UDMA },
	{ "acc_udma0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL33_ACC_UDMA0_CLK_OFS,
	  SUBM_CLK_CTRL3_OFS, 0, 100000000, 0, CLK_ACC_UDMA0 },
	{ "acc_udma1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL34_ACC_UDMA1_CLK_OFS,
	  SUBM_CLK_CTRL3_OFS, 2, 100000000, 0, CLK_ACC_UDMA1 },
	{ "flt_macc_udma_clk_i", "sys_clk", MISC_SYS,
	  CLK_CTRL35_FLT_MACC_UDMA_CLK_OFS, SUBM_CLK_CTRL3_OFS, 1, 100000000, 0,
	  CLK_FLT_MACC_UDMA },
	{ "atb2axi_clk_i", "sys_clk", MISC_SYS, CLK_CTRL36_ATB2AXI_CLK_OFS,
	  SUBM_CLK_CTRL3_OFS, 4, 100000000, 0, CLK_ATB2AXI },
	{ "mdios0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL140_MDIOS0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 24, 100000000, 0, CLK_MDIOS0 },
	{ "cordic0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL141_CORDIC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 25, 100000000, 0, CLK_CORDIC0 },

	/* ============================================================
     * CEC/DFSDM/CAPC/SWPMI/I3C - max_rate=100M
     * ============================================================ */
	{ "cec0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL146_CEC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 28, 100000000, 0, CLK_CEC0 },
	{ "cec0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL147_CEC0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 28, 100000000, 0,
	  CLK_CEC0_INTF },
	{ "dfsdm0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL148_DFSDM0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 29, 100000000, 0, CLK_DFSDM0 },
	{ "dfsdm0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL149_DFSDM0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 29, 100000000,
	  0, CLK_DFSDM0_INTF },
	{ "capc0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL150_CAPC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 30, 100000000, 0, CLK_CAPC0 },
	{ "capc0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL151_CAPC0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 30, 100000000,
	  0, CLK_CAPC0_INTF },
	{ "swpmi0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL144_SWPMI0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 27, 100000000, 0, CLK_SWPMI0 },
	{ "swpmi0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL145_SWPMI0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 27, 100000000,
	  0, CLK_SWPMI0_INTF },
	{ "i3c0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL76_I3C0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 23, 100000000, 0, CLK_I3C0 },
	{ "i3c0_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL77_I3C0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 23, 100000000, 0,
	  CLK_I3C0_INTF },

	/* ============================================================
     * SPDIFRX - max_rate=100M
     * ============================================================ */
	{ "spdifrx0_clk_i", "sys_clk", CLK_CTRL142_SPDIFRX0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 26, 100000000, 0, CLK_SPDIFRX0 },
	{ "spdifrx0_intf_clk_i", "mux_spdifrx0",
	  CLK_CTRL143_SPDIFRX0_INTF_CLK_I_OFS, SUBM_CLK_CTRL2_OFS, 26,
	  100000000, CLK_SET_RATE_PARENT, CLK_SPDIFRX0_INTF },

	/* ============================================================
     * others - gate 待确认，max_rate=100M
     * ============================================================ */
	{ "wwdg0_clk_i", "sys_clk", MISC_SYS, CLK_CTRL158_WWDG0_CLK_OFS, 0, 0,
	  100000000, 0, CLK_WWDG0 },
	{ "wwdg1_clk_i", "sys_clk", MISC_SYS, CLK_CTRL159_WWDG1_CLK_OFS, 0, 0,
	  100000000, 0, CLK_WWDG1 },
	{ "tsens_intf_clk_i", "sys_intf_clk", MISC_SYS,
	  CLK_CTRL161_TSENS_INTF_CLK_I_OFS, 0, 0, 100000000, 0,
	  CLK_TSENS_INTF },
	{ "pvd_clk_i", "sys_clk", MISC_SYS, CLK_CTRL166_PVD_CLK_OFS, 0, 0,
	  100000000, 0, CLK_PVD },

	/* ============================================================
     * ADC/DAC/VREF/TSENS/COMP/OPAMP - max_rate=100M
     * ============================================================ */
	{ "adc_clk_i", "sys_clk", MISC_SYS, CLK_CTRL162_ADC_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 1, 100000000, 0, CLK_ADC },
	{ "dac_clk_i", "sys_clk", MISC_SYS, CLK_CTRL163_DAC_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 3, 100000000, 0, CLK_DAC },
	{ "vref_clk_i", "sys_clk", MISC_SYS, CLK_CTRL164_VREF_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 4, 100000000, 0, CLK_VREF },
	{ "tsens_clk_i", "sys_clk", MISC_SYS, CLK_CTRL165_TSENS_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 2, 100000000, 0, CLK_TSENS },
	{ "comp_clk_i", "sys_clk", MISC_SYS, CLK_CTRL167_COMP_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 5, 100000000, 0, CLK_COMP },
	{ "opamp_clk_i", "sys_clk", MISC_SYS, CLK_CTRL168_OPAMP_CLK_OFS,
	  SUBM_CLK_CTRL0_OFS, 6, 100000000, 0, CLK_OPAMP },

	/* ============================================================
     * CTC - max_rate=100M
     * ============================================================ */
	{ "ctc0_clk_i", "mux_ctc", MISC_SYS, CLK_CTRL169_CTC0_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 31, 100000000, 0, CLK_CTC0 },
	{ "ctc0_ref_clk_i", "mux_ctc", MISC_SYS, CLK_CTRL170_CTC0_REF_CLK_OFS,
	  SUBM_CLK_CTRL2_OFS, 31, 100000000, 0, CLK_CTC0_REF },
};

static int nuclei_ccu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuclei_clk_data *data;
	struct clk_hw_onecell_data *hw_data;
	void __iomem *base[3];
	int ret;

	base[MISC_SYS] = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base[MISC_SYS]))
		return PTR_ERR(base[MISC_SYS]);

	base[MISC_XEC] = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base[MISC_XEC]))
		return PTR_ERR(base[MISC_XEC]);

	base[MISC_USB] = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(base[MISC_USB]))
		return PTR_ERR(base[MISC_USB]);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	hw_data = devm_kzalloc(dev, struct_size(hw_data, hws, CLK_MAX),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;
	data->base[MISC_SYS] = base[MISC_SYS];
	data->base[MISC_XEC] = base[MISC_XEC];
	data->base[MISC_USB] = base[MISC_USB];

	hw_data->num = CLK_MAX;
	data->hw_data = hw_data;
	data->dev = dev;
	spin_lock_init(&data->lock);

	platform_set_drvdata(pdev, data);

	ret = nuclei_clk_register_plls(dev, clk_plls, ARRAY_SIZE(clk_plls),
				       data, hw_data->hws);
	if (ret) {
		dev_err(dev, "Failed to register PLL clocks: %d\n", ret);
		return ret;
	}

	ret = nuclei_clk_register_muxes(dev, clk_muxes, ARRAY_SIZE(clk_muxes),
					data, hw_data->hws);
	if (ret) {
		dev_err(dev, "Failed to register MUX clocks: %d\n", ret);
		return ret;
	}

	ret = nuclei_clk_register_gated_divs(dev, clk_gated_divs,
					     ARRAY_SIZE(clk_gated_divs), data,
					     hw_data->hws);
	if (ret) {
		dev_err(dev, "Failed to register gated dividers: %d\n", ret);
		return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, hw_data);
	if (ret) {
		dev_err(dev, "Failed to add clock provider: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Nuclei CCU registered, %d clock slots\n", hw_data->num);

	ret = nuclei_reset_register(dev, data->base);
	if (ret)
		dev_warn(dev, "Failed to register reset controller: %d\n", ret);

	return 0;
}

static const struct of_device_id nuclei_ccu_of_match[] = {
	{ .compatible = "nuclei,ccu" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, nuclei_ccu_of_match);

static struct platform_driver nuclei_ccu_driver = {
    .probe = nuclei_ccu_probe,
    .driver = {
        .name = "nuclei-ccu",
        .of_match_table = nuclei_ccu_of_match,
    },
};

static int __init nuclei_ccu_init(void)
{
	return platform_driver_register(&nuclei_ccu_driver);
}
core_initcall(nuclei_ccu_init);

static void __exit nuclei_ccu_exit(void)
{
	platform_driver_unregister(&nuclei_ccu_driver);
}
module_exit(nuclei_ccu_exit);

MODULE_DESCRIPTION("NUCLEI CCU Clock Driver");
MODULE_LICENSE("GPL v2");
