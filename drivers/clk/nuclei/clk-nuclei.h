// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#ifndef __CLK_NUCLEI_H
#define __CLK_NUCLEI_H

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/io.h>

enum nuclei_clk_id {
	CLK_CORE1_PLL,
	CLK_XUC_PLL,

	CLK_MUX_CORE1_PLL_IN,
	CLK_MUX_XUC_PLL_IN,
	CLK_MUX_CLUSTER1,
	CLK_MUX_CLUSTER2,
	CLK_MUX_NACC0,
	CLK_MUX_DISP_PIXEL,
	CLK_MUX_DCMI_PIXEL,
	CLK_MUX_XEC_SYS,
	CLK_MUX_XEC_RMII,
	CLK_MUX_XUC,
	CLK_MUX_SAI_S0,
	CLK_MUX_SAI_S1,
	CLK_MUX_SAI_S2,
	CLK_MUX_SAI_S3,
	CLK_MUX_SPDIFRX0,
	CLK_MUX_EFUSE,
	CLK_MUX_CTC,

	CLK_USART0,
	CLK_USART0_INTF,
	CLK_USART1,
	CLK_USART1_INTF,
	CLK_USART2,
	CLK_USART2_INTF,
	CLK_USART3,
	CLK_USART3_INTF,
	CLK_USART4,
	CLK_USART4_INTF,
	CLK_USART5,
	CLK_USART5_INTF,
	CLK_USART6,
	CLK_USART6_INTF,
	CLK_USART7,
	CLK_USART7_INTF,
	CLK_USART8,
	CLK_USART8_INTF,
	CLK_USART9,
	CLK_USART9_INTF,
	CLK_USART10,
	CLK_USART10_INTF,
	CLK_USART11,
	CLK_USART11_INTF,
	CLK_USART12,
	CLK_USART12_INTF,

	CLK_I2C0,
	CLK_I2C0_INTF,
	CLK_I2C1,
	CLK_I2C1_INTF,
	CLK_I2C2,
	CLK_I2C2_INTF,
	CLK_I2C3,
	CLK_I2C3_INTF,

	CLK_QSPI_XIP0,
	CLK_QSPI_XIP0_INTF,
	CLK_QSPI_XIP1,
	CLK_QSPI_XIP1_INTF,
	CLK_QSPI_XIP2,
	CLK_QSPI_XIP2_INTF,
	CLK_QSPI3,
	CLK_QSPI3_INTF,
	CLK_QSPI4,
	CLK_QSPI4_INTF,
	CLK_QSPI5,
	CLK_QSPI5_INTF,
	CLK_QSPI6,
	CLK_QSPI6_INTF,

	CLK_XKAN0,
	CLK_XKAN0_INTF,
	CLK_XKAN1,
	CLK_XKAN1_INTF,
	CLK_XKAN2,
	CLK_XKAN2_INTF,

	CLK_SAI0,
	CLK_SAI0_S0_INTF,
	CLK_SAI0_S1_INTF,
	CLK_SAI0_S2_INTF,

	CLK_DISP0,
	CLK_DISP0_INTF,
	CLK_DISP0_AON,
	CLK_DCMI0,
	CLK_DCMI0_INTF,
	CLK_DCMI0_AON,

	CLK_XEC_GEN20_SYS,
	CLK_XEC_GEN21_SYS,
	CLK_XEC_GEN20_RMII_REF,
	CLK_XEC_GEN20_PTP_REF,

	CLK_XUC0,
	CLK_XUC_PHY,

	CLK_USB_TOP0,
	CLK_USB_TOP_CORECORE,

	CLK_SDIO0,
	CLK_SDIO0_INTF,
	CLK_HS_SDIO0,
	CLK_HS_SDIO0_INTF,

	CLK_CEC0,
	CLK_CEC0_INTF,
	CLK_DFSDM0,
	CLK_DFSDM0_INTF,
	CLK_CAPC0,
	CLK_CAPC0_INTF,
	CLK_SWPMI0,
	CLK_SWPMI0_INTF,
	CLK_I3C0,
	CLK_I3C0_INTF,
	CLK_LGPIO0,
	CLK_IOMUX,
	CLK_NACC0,
	CLK_G2D0,
	CLK_JPEG0,
	CLK_FILTER0,
	CLK_FLT_MACC0,
	CLK_FFT0,
	CLK_GMC0,
	CLK_PID0,
	CLK_IDU,
	CLK_SOC_GLUE,
	CLK_PCRC0,
	CLK_SYS_UDMA,
	CLK_ACC_UDMA0,
	CLK_ACC_UDMA1,
	CLK_FLT_MACC_UDMA,
	CLK_ATB2AXI,
	CLK_MDIOS0,
	CLK_CORDIC0,
	CLK_SPDIFRX0,
	CLK_SPDIFRX0_INTF,

	CLK_ADV_TIMER0,
	CLK_ADV_TIMER1,
	CLK_ADV_TIMER2,
	CLK_ADV_TIMER3,
	CLK_ADV_TIMER4,
	CLK_ADV_TIMER5,
	CLK_ADV_TIMER6,
	CLK_ADV_TIMER7,
	CLK_ADV_TIMER8,
	CLK_ADV_TIMER9,
	CLK_ADV_TIMER10,
	CLK_ADV_TIMER11,
	CLK_ADV_TIMER12,
	CLK_ADV_TIMER13,
	CLK_ADV_TIMER14,
	CLK_ADV_TIMER15,

	CLK_BASIC_TIMER0,
	CLK_ACRYP0,
	CLK_CRYP0,
	CLK_HASH0,
	CLK_TRNG0,
	CLK_TRNG0_SMP,
	CLK_WWDG0,
	CLK_WWDG1,
	CLK_BROM,
	CLK_TSENS_INTF,
	CLK_ADC,
	CLK_DAC,
	CLK_VREF,
	CLK_TSENS,
	CLK_PVD,
	CLK_COMP,
	CLK_OPAMP,
	CLK_CTC0,
	CLK_CTC0_REF,

	CLK_XEC_GEN21_RMII_REF,
	CLK_XEC_GEN21_PTP_REF,

	CLK_MAX,
};

/* pll control registersco */
#define PLL_CTRL0_SYS_PLL_CLK_OFS 0x078
#define PLL_CTRL1_CORE1_PLL_CLK_OFS 0x07C
#define PLL_CTRL2_XUC_PLL_CLK_OFS 0x080

/* clk mux registers */
#define CLK_CTRL0_SYS_PLL_CLK_S_OFS 0x100
#define CLK_CTRL1_SYS_PLL_CLK_DIV2_OFS 0x104
#define CLK_CTRL2_CORE1_PLL_CLK_S_OFS 0x108
#define CLK_CTRL3_XUC_PLL_CLK_S_OFS 0x10C
#define CLK_CTRL4_SYS_CLK_OFS 0x110
#define CLK_CTRL5_SYS_INTF_CLK_OFS 0x114
#define CLK_CTRL6_CLUSTER1_CLK_OFS 0x118
#define CLK_CTRL7_CLUSTER2_CLK_OFS 0x11C
#define CLK_CTRL8_NACCO_CLK_S_OFS 0x120
#define CLK_CTRL9_SAI_S0_CLK_S_OFS 0x124
#define CLK_CTRL10_SAI_S1_CLK_S_OFS 0x128
#define CLK_CTRL11_SAI_S2_CLK_S_OFS 0x12C
#define CLK_CTRL12_SAI_S3_CLK_S_OFS 0x130
#define CLK_CTRL13_SPDIFRX0_CLK_S_OFS 0x134
#define CLK_CTRL14_DISP_PIXEL_CLK_S_OFS 0x138
#define CLK_CTRL15_DCMI_PIXEL_CLK_S_OFS 0x13C
#define CLK_CTRL16_XEC_SYS_CLK_S_OFS 0x140
#define CLK_CTRL17_XEC_RMII_CLK_S_OFS 0x144
#define CLK_CTRL18_XUC_CLK_S_OFS 0x148
#define CLK_CTRL19_BKDR_EFUSE_CLK_S_OFS 0x14C
#define CLK_CTRL20_CTC_CLK_S_OFS 0x150

/* clk div registers */
#define CLK_CTRL25_G2D0_CLK_OFS 0x164
#define CLK_CTRL26_JPEG0_CLK_OFS 0x168
#define CLK_CTRL27_FLT_MACC0_CLK_OFS 0x16C
#define CLK_CTRL28_FFT0_CLK_OFS 0x170
#define CLK_CTRL29_IDU_CLK_OFS 0x174
#define CLK_CTRL30_SOC_GLUE_CLK_OFS 0x178
#define CLK_CTRL31_HSM_UDMA_CLK_OFS 0x17C
#define CLK_CTRL32_SYS_UDMA_CLK_OFS 0x180
#define CLK_CTRL33_ACC_UDMA0_CLK_OFS 0x184
#define CLK_CTRL34_ACC_UDMA1_CLK_OFS 0x188
#define CLK_CTRL35_FLT_MACC_UDMA_CLK_OFS 0x18C
#define CLK_CTRL36_ATB2AXI_CLK_OFS 0x190
#define CLK_CTRL37_HSM_RTC_CLK_OFS 0x194
#define CLK_CTRL38_NACC0_CLK_OFS 0x198
#define CLK_CTRL39_FILTER0_CLK_OFS 0x19C
#define CLK_CTRL40_IOMUX_CLK_OFS 0x1A0
#define CLK_CTRL41_USART0_CLK_OFS 0x1A4
#define CLK_CTRL42_USART0_INTF_CLK_I_OFS 0x1A8
#define CLK_CTRL43_USART1_CLK_OFS 0x1AC
#define CLK_CTRL44_USART1_INTF_CLK_I_OFS 0x1B0
#define CLK_CTRL45_USART2_CLK_OFS 0x1B4
#define CLK_CTRL46_USART2_INTF_CLK_I_OFS 0x1B8
#define CLK_CTRL47_USART3_CLK_OFS 0x1BC
#define CLK_CTRL48_USART3_INTF_CLK_I_OFS 0x1C0
#define CLK_CTRL49_USART4_CLK_OFS 0x1C4
#define CLK_CTRL50_USART4_INTF_CLK_I_OFS 0x1C8
#define CLK_CTRL51_USART5_CLK_OFS 0x1CC
#define CLK_CTRL52_USART5_INTF_CLK_I_OFS 0x1D0
#define CLK_CTRL53_USART6_CLK_OFS 0x1D4
#define CLK_CTRL54_USART6_INTF_CLK_I_OFS 0x1D8
#define CLK_CTRL55_USART7_CLK_OFS 0x1DC
#define CLK_CTRL56_USART7_INTF_CLK_I_OFS 0x1E0
#define CLK_CTRL57_USART8_CLK_OFS 0x1E4
#define CLK_CTRL58_USART8_INTF_CLK_I_OFS 0x1E8
#define CLK_CTRL59_USART9_CLK_OFS 0x1EC
#define CLK_CTRL60_USART9_INTF_CLK_I_OFS 0x1F0
#define CLK_CTRL61_USART10_CLK_OFS 0x1F4
#define CLK_CTRL62_USART10_INTF_CLK_I_OFS 0x1F8
#define CLK_CTRL63_USART11_CLK_OFS 0x1FC
#define CLK_CTRL64_USART11_INTF_CLK_I_OFS 0x200
#define CLK_CTRL65_USART12_CLK_OFS 0x204
#define CLK_CTRL66_USART12_INTF_CLK_I_OFS 0x208
#define CLK_CTRL67_PID0_CLK_OFS 0x20C
#define CLK_CTRL68_I2C0_CLK_OFS 0x210
#define CLK_CTRL69_I2C0_INTF_CLK_I_OFS 0x214
#define CLK_CTRL70_I2C1_CLK_OFS 0x218
#define CLK_CTRL71_I2C1_INTF_CLK_I_OFS 0x21C
#define CLK_CTRL72_I2C2_CLK_OFS 0x220
#define CLK_CTRL73_I2C2_INTF_CLK_I_OFS 0x224
#define CLK_CTRL74_I2C3_CLK_OFS 0x228
#define CLK_CTRL75_I2C3_INTF_CLK_I_OFS 0x22C
#define CLK_CTRL76_I3C0_CLK_OFS 0x230
#define CLK_CTRL77_I3C0_INTF_CLK_I_OFS 0x234
#define CLK_CTRL78_QSPI_XIP0_CLK_OFS 0x238
#define CLK_CTRL79_QSPI_XIP0_INTF_CLK_I_OFS 0x23C
#define CLK_CTRL80_QSPI_XIP1_CLK_OFS 0x240
#define CLK_CTRL81_QSPI_XIP1_INTF_CLK_I_OFS 0x244
#define CLK_CTRL82_QSPI_XIP2_CLK_OFS 0x248
#define CLK_CTRL83_QSPI_XIP2_INTF_CLK_I_OFS 0x24C
#define CLK_CTRL84_QSPI3_CLK_OFS 0x250
#define CLK_CTRL85_QSPI3_INTF_CLK_I_OFS 0x254
#define CLK_CTRL86_QSPI4_CLK_OFS 0x258
#define CLK_CTRL87_QSPI4_INTF_CLK_I_OFS 0x25C
#define CLK_CTRL88_QSPI5_CLK_OFS 0x260
#define CLK_CTRL89_QSPI5_INTF_CLK_I_OFS 0x264
#define CLK_CTRL90_QSPI6_CLK_OFS 0x268
#define CLK_CTRL91_QSPI6_INTF_CLK_I_OFS 0x26C
#define CLK_CTRL92_XKAN0_CLK_OFS 0x270
#define CLK_CTRL93_XKAN0_INTF_CLK_I_OFS 0x274
#define CLK_CTRL94_XKAN1_CLK_OFS 0x278
#define CLK_CTRL95_XKAN1_INTF_CLK_I_OFS 0x27C
#define CLK_CTRL96_XKAN2_CLK_OFS 0x280
#define CLK_CTRL97_XKAN2_INTF_CLK_I_OFS 0x284
#define CLK_CTRL98_SAI0_CLK_OFS 0x288
#define CLK_CTRL99_SAI0_S0_INTF_CLK_I_OFS 0x28C
#define CLK_CTRL100_SAI0_S1_INTF_CLK_I_OFS 0x290
#define CLK_CTRL101_SAI0_S2_INTF_CLK_I_OFS 0x294
#define CLK_CTRL102_SAI0_S3_INTF_CLK_I_OFS 0x298
#define CLK_CTRL103_LGPIO0_CLK_OFS 0x29C
#define CLK_CTRL104_ADVANCED_TIMER0_CLK_OFS 0x2A0
#define CLK_CTRL105_ADVANCED_TIMER1_CLK_OFS 0x2A4
#define CLK_CTRL106_ADVANCED_TIMER2_CLK_OFS 0x2A8
#define CLK_CTRL107_ADVANCED_TIMER3_CLK_OFS 0x2AC
#define CLK_CTRL108_ADVANCED_TIMER4_CLK_OFS 0x2B0
#define CLK_CTRL109_ADVANCED_TIMER5_CLK_OFS 0x2B4
#define CLK_CTRL110_ADVANCED_TIMER6_CLK_OFS 0x2B8
#define CLK_CTRL111_ADVANCED_TIMER7_CLK_OFS 0x2BC
#define CLK_CTRL112_ADVANCED_TIMER8_CLK_OFS 0x2C0
#define CLK_CTRL113_ADVANCED_TIMER9_CLK_OFS 0x2C4
#define CLK_CTRL114_ADVANCED_TIMER10_CLK_OFS 0x2C8
#define CLK_CTRL115_ADVANCED_TIMER11_CLK_OFS 0x2CC
#define CLK_CTRL116_ADVANCED_TIMER12_CLK_OFS 0x2D0
#define CLK_CTRL117_ADVANCED_TIMER13_CLK_OFS 0x2D4
#define CLK_CTRL118_ADVANCED_TIMER14_CLK_OFS 0x2D8
#define CLK_CTRL119_ADVANCED_TIMER15_CLK_OFS 0x2DC
#define CLK_CTRL120_PCRC0_CLK_OFS 0x2E0
#define CLK_CTRL121_DISP0_CLK_OFS 0x2E4
#define CLK_CTRL122_DISP0_INTF_CLK_I_OFS 0x2E8
#define CLK_CTRL123_DCMI0_CLK_OFS 0x2EC
#define CLK_CTRL124_DCMI0_INTF_CLK_I_OFS 0x2F0
#define CLK_CTRL125_GMC0_CLK_OFS 0x2F4
#define CLK_CTRL126_XEC_GEN20_SYS_CLK_OFS 0x2F8
#define CLK_CTRL127_XEC_GEN21_SYS_CLK_OFS 0x2FC
#define CLK_CTRL128_RMII_CLK_REF_OFS 0x300
#define CLK_CTRL129_PTP_REF_CLK_OFS 0x304
#define CLK_CTRL130_XUC0_CLK_OFS 0x308
#define CLK_CTRL131_XUC_CLK_PHY_OFS 0x30C
#define CLK_CTRL132_USB_TOP0_CLK_OFS 0x310
#define CLK_CTRL133_USB_TOP0_SCAN_CLK_480_OFS 0x314
#define CLK_CTRL134_USB_TOP0_EXTREFCLK_OFS 0x318
#define CLK_CTRL135_USB_TOP0_CLKCORE_OFS 0x31C
#define CLK_CTRL136_SDIO0_CLK_OFS 0x320
#define CLK_CTRL137_SDIO0_INTF_CLK_I_OFS 0x324
#define CLK_CTRL138_HS_SDIO0_CLK_OFS 0x328
#define CLK_CTRL139_HS_SDIO0_INTF_CLK_I_OFS 0x32C
#define CLK_CTRL140_MDIOS0_CLK_OFS 0x330
#define CLK_CTRL141_CORDIC0_CLK_OFS 0x334
#define CLK_CTRL142_SPDIFRX0_CLK_OFS 0x338
#define CLK_CTRL143_SPDIFRX0_INTF_CLK_I_OFS 0x33C
#define CLK_CTRL144_SWPMI0_CLK_OFS 0x340
#define CLK_CTRL145_SWPMI0_INTF_CLK_I_OFS 0x344
#define CLK_CTRL146_CEC0_CLK_OFS 0x348
#define CLK_CTRL147_CEC0_INTF_CLK_I_OFS 0x34C
#define CLK_CTRL148_DFSDM0_CLK_OFS 0x350
#define CLK_CTRL149_DFSDM0_INTF_CLK_I_OFS 0x354
#define CLK_CTRL150_CAPC0_CLK_OFS 0x358
#define CLK_CTRL151_CAPC0_INTF_CLK_I_OFS 0x35C
#define CLK_CTRL152_BASIC_TIMER0_CLK_OFS 0x360
#define CLK_CTRL153_ACRYP0_CLK_OFS 0x364
#define CLK_CTRL154_CRYP0_CLK_OFS 0x368
#define CLK_CTRL155_HASH0_CLK_OFS 0x36C
#define CLK_CTRL156_TRNG0_CLK_OFS 0x370
#define CLK_CTRL157_TRNG0_CLK_SMP_OFS 0x374
#define CLK_CTRL158_WWDG0_CLK_OFS 0x378
#define CLK_CTRL159_WWDG1_CLK_OFS 0x37C
#define CLK_CTRL160_BROM_CLK_OFS 0x380
#define CLK_CTRL161_TSENS_INTF_CLK_I_OFS 0x384
#define CLK_CTRL162_ADC_CLK_OFS 0x388
#define CLK_CTRL163_DAC_CLK_OFS 0x38C
#define CLK_CTRL164_VREF_CLK_OFS 0x390
#define CLK_CTRL165_TSENS_CLK_OFS 0x394
#define CLK_CTRL166_PVD_CLK_OFS 0x398
#define CLK_CTRL167_COMP_CLK_OFS 0x39C
#define CLK_CTRL168_OPAMP_CLK_OFS 0x3A0
#define CLK_CTRL169_CTC0_CLK_OFS 0x3A4
#define CLK_CTRL170_CTC0_REF_CLK_OFS 0x3A8

/* clk enable registers */
#define SUBM_CLK_CTRL0_OFS 0x040
#define SUBM_CLK_CTRL1_OFS 0x044
#define SUBM_CLK_CTRL2_OFS 0x048
#define SUBM_CLK_CTRL3_OFS 0x04C

enum nuclei_misc_type {
    MISC_SYS = 0,
    MISC_XEC = 1,
    MISC_USB = 2,
};

struct nuclei_gated_div_desc {
	const char *name;
	const char *parent;
	enum nuclei_misc_type misc;
	unsigned int div_reg;
	unsigned int gate_reg;
	unsigned int gate_bit;
	unsigned long max_rate;
	unsigned long flags;
	unsigned int id;
};

struct nuclei_mux_desc {
	const char *name;
	const char *const *parents;
	unsigned int num_parents;
	unsigned int mux_reg;
	unsigned int mux_shift;
	unsigned int mux_width;
	unsigned long flags;
	unsigned long max_rate;
	unsigned int id;
};

struct nuclei_pll_desc {
	const char *name;
	const char *parent;
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

	/* DT boot-time configuration (optional):
	 * dt_prop        - property name on the CCU node, e.g. "core1-pll";
	 *                  NULL = do not parse. Property format: <mux N M OD>
	 *                  mux: 0=hsi, 1=xtal, 2=clk_in1, 3=clk_in2
	 *                  N: 1~255, M: 1~1023, OD: 0~63
	 * input_mux_reg  - PLL input mux register (0 = no mux config)
	 * input_mux_shift/input_mux_width - mux field bit info
	 */
	const char *dt_prop;
	unsigned int input_mux_reg;
	unsigned int input_mux_shift;
	unsigned int input_mux_width;
	unsigned int id;
};

struct nuclei_clk_data {
	void __iomem *base[3];/* 0=SYS, 1=XEC, 2=USB */
	struct clk_hw_onecell_data *hw_data;
	struct device *dev;
	spinlock_t lock;
};

static inline u32 nuclei_clk_readl(struct nuclei_clk_data *data,
				   unsigned int reg)
{
	return readl(data->base[MISC_SYS] + reg);
}

static inline u32 nuclei_clk_readl_type(struct nuclei_clk_data *data,
				   unsigned int reg, enum nuclei_misc_type type)
{
	return readl(data->base[type] + reg);
}

static inline void nuclei_clk_writel(struct nuclei_clk_data *data,
				     unsigned int reg, u32 val)
{
	writel(val, data->base[MISC_SYS] + reg);
}

static inline void nuclei_clk_update_bits(struct nuclei_clk_data *data,
					  unsigned int reg, u32 mask, u32 val)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&data->lock, flags);
	tmp = readl(data->base[MISC_SYS] + reg);
	tmp &= ~mask;
	tmp |= val & mask;
	writel(tmp, data->base[MISC_SYS] + reg);
	spin_unlock_irqrestore(&data->lock, flags);
}

static inline void nuclei_clk_update_bits_type(struct nuclei_clk_data *data,
					  unsigned int reg, u32 mask, u32 val,
					  enum nuclei_misc_type type)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&data->lock, flags);
	tmp = readl(data->base[type] + reg);
	tmp &= ~mask;
	tmp |= val & mask;
	writel(tmp, data->base[type] + reg);
	spin_unlock_irqrestore(&data->lock, flags);
}

int nuclei_clk_register_gated_divs(struct device *dev,
				   struct nuclei_gated_div_desc *descs,
				   unsigned int num,
				   struct nuclei_clk_data *data,
				   struct clk_hw **hws);

int nuclei_clk_register_muxes(struct device *dev, struct nuclei_mux_desc *descs,
			      unsigned int num, struct nuclei_clk_data *data,
			      struct clk_hw **hws);

int nuclei_clk_register_plls(struct device *dev, struct nuclei_pll_desc *descs,
			     unsigned int num, struct nuclei_clk_data *data,
			     struct clk_hw **hws);

int nuclei_reset_register(struct device *dev, void __iomem *base);

#endif /* __CLK_NUCLEI_H */
