// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Nucleisys Co., Ltd.
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define DMAC_MEM_CH_CFG_WIDTH			0x1C
#define DMAC_MEM_CH_IRQ_WIDTH			0xC
#define DMAC_SLAVE_CH_CFG_WIDTH			0x18
#define DMAC_SLAVE_CH_IRQ_WIDTH			0xC

#define DMAC_MEM_CH_CFG_OFF(_ch)		(DMAC_MEM_CH_CFG_WIDTH * (_ch) + 0x8)
#define DMAC_MEM_CH_IRQ_OFF(_ch)		(DMAC_MEM_CH_IRQ_WIDTH * (_ch) + 0x800)

#define DMAC_SLAVE_CH_CFG_OFF(_ch)		(DMAC_SLAVE_CH_CFG_WIDTH * (_ch) + 0x400)
#define DMAC_SLAVE_CH_IRQ_OFF(_ch)		(DMAC_SLAVE_CH_IRQ_WIDTH * (_ch) + 0xA00)

#define DMAC_CH_IRQ_EN_OFF				0x0
#define DMAC_CH_IRQ_STAT_OFF			0x4
#define DMAC_CH_IRQ_CLR_OFF				0x8

#define DMAC_MEM_CH_SRC_LBASE_OFF		0x0
#define DMAC_MEM_CH_DST_LBASE_OFF		0x4
#define DMAC_MEM_CH_CTRL_OFF			0x8
#define DMAC_MEM_CH_TSIZE_OFF			0x10
#define DMAC_MEM_CH_SRC_HBASE_OFF		0x14
#define DMAC_MEM_CH_DST_HBASE_OFF		0x18

#define DMAC_PA_CH_SRC_LBASE_OFF		0x0
#define DMAC_PA_CH_DST_LBASE_OFF		0x4
#define DMAC_PA_CH_CTRL_OFF				0x8
#define DMAC_PA_CH_TSIZE_OFF 			0xC
#define DMAC_PA_CH_SRC_HBASE_OFF		0x10
#define DMAC_PA_CH_DST_HBASE_OFF		0x14

/* full transfer interrupt mask */
#define DMAC_IRQ_FTI_MASK				BIT(0)
#define DMAC_IRQ_HTI_MASK				BIT(1)
/* err transfer interrupt mask */
#define DMAC_IRQ_ETI_MASK				BIT(2)

#define DMAC_ADDR_MODE_INC				0
#define DMAC_ADDR_MODE_FIXED			1

#define DMAC_MEM_TRANS_EN_SHIFT			0
#define DMAC_MEM_TRANS_STATUS_SHIFT		1
#define DMAC_MEM_TRANS_MODE_SHIFT		6
#define DMAC_MEM_TRANS_PRIO_SHIFT		8
#define DMAC_MEM_DST_ADDR_MODE_SHIFT	12
#define DMAC_MEM_SRC_ADDR_MODE_SHIFT	13
#define DMAC_MEM_DST_WIDTH_SHIFT		16
#define DMAC_MEM_SRC_WIDTH_SHIFT		21
#define DMAC_MEM_DST_BURST_SHIFT		24
#define DMAC_MEM_SRC_BURST_SHIFT		28

#define DMAC_PA_TRANS_EN_SHIFT			0
#define DMAC_PA_TRANS_PER_SEL_SHIFT		24
#define DMAC_PA_TRANS_MODE_SHIFT		6
#define DMAC_PA_DST_ADDR_MODE_SHIFT		12
#define DMAC_PA_SRC_ADDR_MODE_SHIFT		13
#define DMAC_PA_DATA_WIDTH_SHIFT		16

/* cut lower bit for maintain alignment of maximum transfer size
 * 16byte width, 16 time transfer per brust 
 */
#define DMAC_MEM_MAX_TRANS_SIZE			(GENMASK(24, 0) & ~GENMASK(7, 0))

#define DMAC_PA_MAX_TRANS_SIZE			GENMASK(24, 0)

#define NUCLEI_DMAC_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))
	 
#define NUCLEI_MAX_BURST 16

typedef struct {
	uint8_t ctrl_id;        /* 0: ACC_UDMA0, 1: ACC_UDMA1 */
	uint8_t phy_chan_id;    /* physic channel id */
	uint16_t periph_sel;    /* peripheral select value per channel */
} dma_chan_info_t;

/*******************************************************************************
 * dma virtual channel id, DMA Client should use this value in dts
 ******************************************************************************/
enum {
	/* ========== ACC_UDMA0 mem2mem channel ========== */
	DMA_U0_MEM2MEM_0 = 0,
	DMA_U0_MEM2MEM_1 = 1,

	/* ========== ACC_UDMA0 periph channel ========== */
	/* USART0 */
	DMA_U0_USART0_TX = 2,
	DMA_U0_USART0_RX = 3,

	/* USART1 */
	DMA_U0_USART1_TX = 4,
	DMA_U0_USART1_RX = 5,

	/* USART2 */
	DMA_U0_USART2_TX = 6,
	DMA_U0_USART2_RX = 7,

	/* USART3 */
	DMA_U0_USART3_TX = 8,
	DMA_U0_USART3_RX = 9,

	/* USART4 */
	DMA_U0_USART4_TX = 10,
	DMA_U0_USART4_RX = 11,

	/* USART5 */
	DMA_U0_USART5_TX = 12,
	DMA_U0_USART5_RX = 13,

	/* USART6 */
	DMA_U0_USART6_TX = 14,
	DMA_U0_USART6_RX = 15,

	/* I2C0 */
	DMA_U0_I2C0_TX = 16,
	DMA_U0_I2C0_RX = 17,

	/* I2C1 */
	DMA_U0_I2C1_TX = 18,
	DMA_U0_I2C1_RX = 19,

	/* QSPI XIP0 */
	DMA_U0_QSPI_XIP0_TX = 20,
	DMA_U0_QSPI_XIP0_RX = 21,

	/* QSPI XIP1 */
	DMA_U0_QSPI_XIP1_TX = 22,
	DMA_U0_QSPI_XIP1_RX = 23,

	/* QSPI XIP2 */
	DMA_U0_QSPI_XIP2_TX = 24,
	DMA_U0_QSPI_XIP2_RX = 25,

	/* QSPI3 */
	DMA_U0_QSPI3_TX = 26,
	DMA_U0_QSPI3_RX = 27,

	/* DAC */
	DMA_U0_DAC_CH1 = 28,
	DMA_U0_DAC_CH2 = 29,

	/* DFSDM0 */
	DMA_U0_DFSDM0_DMA0 = 30,
	DMA_U0_DFSDM0_DMA1 = 31,
	DMA_U0_DFSDM0_DMA2 = 32,

	/* SAI0/SAI1 */
	DMA_U0_SAI0_A_TX = 33,
	DMA_U0_SAI0_B_TX = 34,
	DMA_U0_SAI1_A_TX = 35,
	DMA_U0_SAI1_B_TX = 36,
	DMA_U0_SAI0_A_RX = 37,
	DMA_U0_SAI0_B_RX = 38,
	DMA_U0_SAI1_A_RX = 39,
	DMA_U0_SAI1_B_RX = 40,

	/* CORDIC0 */
	DMA_U0_CORDIC0_TX = 41,
	DMA_U0_CORDIC0_RX = 42,

	/* SWPMI0 */
	DMA_U0_SWPMI0_TX = 43,
	DMA_U0_SWPMI0_RX = 44,

	/* SPDIFRX0 */
	DMA_U0_SPDIFRX0_CTRL = 45,
	DMA_U0_SPDIFRX0_DAT = 46,

	/* Advanced Timer 0 */
	DMA_U0_TIM0_COM = 47,
	DMA_U0_TIM0_TRG = 48,
	DMA_U0_TIM0_UEV = 49,
	DMA_U0_TIM0_CC1 = 50,
	DMA_U0_TIM0_CC2 = 51,
	DMA_U0_TIM0_CC3 = 52,
	DMA_U0_TIM0_CC4 = 53,

	/* Advanced Timer 1 */
	DMA_U0_TIM1_COM = 54,
	DMA_U0_TIM1_TRG = 55,
	DMA_U0_TIM1_UEV = 56,
	DMA_U0_TIM1_CC1 = 57,
	DMA_U0_TIM1_CC2 = 58,
	DMA_U0_TIM1_CC3 = 59,
	DMA_U0_TIM1_CC4 = 60,

	/* Advanced Timer 2 */
	DMA_U0_TIM2_COM = 61,
	DMA_U0_TIM2_TRG = 62,
	DMA_U0_TIM2_UEV = 63,
	DMA_U0_TIM2_CC1 = 64,
	DMA_U0_TIM2_CC2 = 65,
	DMA_U0_TIM2_CC3 = 66,
	DMA_U0_TIM2_CC4 = 67,

	/* Advanced Timer 3 */
	DMA_U0_TIM3_COM = 68,
	DMA_U0_TIM3_TRG = 69,
	DMA_U0_TIM3_UEV = 70,
	DMA_U0_TIM3_CC1 = 71,
	DMA_U0_TIM3_CC2 = 72,
	DMA_U0_TIM3_CC3 = 73,
	DMA_U0_TIM3_CC4 = 74,

	/* Advanced Timer 4 */
	DMA_U0_TIM4_COM = 75,
	DMA_U0_TIM4_TRG = 76,
	DMA_U0_TIM4_UEV = 77,
	DMA_U0_TIM4_CC1 = 78,
	DMA_U0_TIM4_CC2 = 79,
	DMA_U0_TIM4_CC3 = 80,
	DMA_U0_TIM4_CC4 = 81,

	/* Advanced Timer 5 */
	DMA_U0_TIM5_COM = 82,
	DMA_U0_TIM5_TRG = 83,
	DMA_U0_TIM5_UEV = 84,
	DMA_U0_TIM5_CC1 = 85,
	DMA_U0_TIM5_CC2 = 86,
	DMA_U0_TIM5_CC3 = 87,
	DMA_U0_TIM5_CC4 = 88,

	/* Advanced Timer 6 */
	DMA_U0_TIM6_COM = 89,
	DMA_U0_TIM6_TRG = 90,
	DMA_U0_TIM6_UEV = 91,
	DMA_U0_TIM6_CC1 = 92,
	DMA_U0_TIM6_CC2 = 93,
	DMA_U0_TIM6_CC3 = 94,
	DMA_U0_TIM6_CC4 = 95,

	/* Advanced Timer 7 */
	DMA_U0_TIM7_COM = 96,
	DMA_U0_TIM7_TRG = 97,
	DMA_U0_TIM7_UEV = 98,
	DMA_U0_TIM7_CC1 = 99,
	DMA_U0_TIM7_CC2 = 100,
	DMA_U0_TIM7_CC3 = 101,
	DMA_U0_TIM7_CC4 = 102,

	/* ========== ACC_UDMA1 mem2mem channel ========== */
	DMA_U0_MAX_REQ_ID = 103,
	DMA_U1_MEM2MEM_0 = DMA_U0_MAX_REQ_ID,
	DMA_U1_MEM2MEM_1 = 104,

	/* ========== ACC_UDMA1 periph channel========== */
	/* USART7 */
	DMA_U1_USART7_TX = 105,
	DMA_U1_USART7_RX = 106,

	/* USART8 */
	DMA_U1_USART8_TX = 107,
	DMA_U1_USART8_RX = 108,

	/* USART9 */
	DMA_U1_USART9_TX = 109,
	DMA_U1_USART9_RX = 110,

	/* USART10 */
	DMA_U1_USART10_TX = 111,
	DMA_U1_USART10_RX = 112,

	/* USART11 */
	DMA_U1_USART11_TX = 113,
	DMA_U1_USART11_RX = 114,

	/* USART12 */
	DMA_U1_USART12_TX = 115,
	DMA_U1_USART12_RX = 116,

	/* I2C2 */
	DMA_U1_I2C2_TX = 117,
	DMA_U1_I2C2_RX = 118,

	/* I2C3 */
	DMA_U1_I2C3_TX = 119,
	DMA_U1_I2C3_RX = 120,

	/* QSPI4 */
	DMA_U1_QSPI4_TX = 121,
	DMA_U1_QSPI4_RX = 122,

	/* QSPI5 */
	DMA_U1_QSPI5_TX = 123,
	DMA_U1_QSPI5_RX = 124,

	/* QSPI6 */
	DMA_U1_QSPI6_TX = 125,
	DMA_U1_QSPI6_RX = 126,

	/* SAI0 - SAI2/SAI3 */
	DMA_U1_SAI2_A_TX = 127,
	DMA_U1_SAI2_B_TX = 128,
	DMA_U1_SAI3_A_TX = 129,
	DMA_U1_SAI3_B_TX = 130,
	DMA_U1_SAI2_A_RX = 131,
	DMA_U1_SAI2_B_RX = 132,
	DMA_U1_SAI3_A_RX = 133,
	DMA_U1_SAI3_B_RX = 134,

	/* ADC */
	DMA_U1_ADC0 = 135,
	DMA_U1_ADC1 = 136,
	DMA_U1_ADC2 = 137,

	/* DFSDM0 */
	DMA_U1_DFSDM0_DMA3 = 138,
	DMA_U1_DFSDM0_DMA4 = 139,
	DMA_U1_DFSDM0_DMA5 = 140,

	/* Advanced Timer 8 */
	DMA_U1_TIM8_COM = 141,
	DMA_U1_TIM8_TRG = 142,
	DMA_U1_TIM8_UEV = 143,
	DMA_U1_TIM8_CC1 = 144,
	DMA_U1_TIM8_CC2 = 145,
	DMA_U1_TIM8_CC3 = 146,
	DMA_U1_TIM8_CC4 = 147,

	/* Advanced Timer 9 */
	DMA_U1_TIM9_COM = 148,
	DMA_U1_TIM9_TRG = 149,
	DMA_U1_TIM9_UEV = 150,
	DMA_U1_TIM9_CC1 = 151,
	DMA_U1_TIM9_CC2 = 152,
	DMA_U1_TIM9_CC3 = 153,
	DMA_U1_TIM9_CC4 = 154,

	/* Advanced Timer 10 */
	DMA_U1_TIM10_COM = 155,
	DMA_U1_TIM10_TRG = 156,
	DMA_U1_TIM10_UEV = 157,
	DMA_U1_TIM10_CC1 = 158,
	DMA_U1_TIM10_CC2 = 159,
	DMA_U1_TIM10_CC3 = 160,
	DMA_U1_TIM10_CC4 = 161,

	/* Advanced Timer 11 */
	DMA_U1_TIM11_COM = 162,
	DMA_U1_TIM11_TRG = 163,
	DMA_U1_TIM11_UEV = 164,
	DMA_U1_TIM11_CC1 = 165,
	DMA_U1_TIM11_CC2 = 166,
	DMA_U1_TIM11_CC3 = 167,
	DMA_U1_TIM11_CC4 = 168,

	/* Advanced Timer 12 */
	DMA_U1_TIM12_COM = 169,
	DMA_U1_TIM12_TRG = 170,
	DMA_U1_TIM12_UEV = 171,
	DMA_U1_TIM12_CC1 = 172,
	DMA_U1_TIM12_CC2 = 173,
	DMA_U1_TIM12_CC3 = 174,
	DMA_U1_TIM12_CC4 = 175,

	/* Advanced Timer 13 */
	DMA_U1_TIM13_COM = 176,
	DMA_U1_TIM13_TRG = 177,
	DMA_U1_TIM13_UEV = 178,
	DMA_U1_TIM13_CC1 = 179,
	DMA_U1_TIM13_CC2 = 180,
	DMA_U1_TIM13_CC3 = 181,
	DMA_U1_TIM13_CC4 = 182,

	/* Advanced Timer 14 */
	DMA_U1_TIM14_COM = 183,
	DMA_U1_TIM14_TRG = 184,
	DMA_U1_TIM14_UEV = 185,
	DMA_U1_TIM14_CC1 = 186,
	DMA_U1_TIM14_CC2 = 187,
	DMA_U1_TIM14_CC3 = 188,
	DMA_U1_TIM14_CC4 = 189,

	/* Advanced Timer 15 */
	DMA_U1_TIM15_COM = 190,
	DMA_U1_TIM15_TRG = 191,
	DMA_U1_TIM15_UEV = 192,
	DMA_U1_TIM15_CC1 = 193,
	DMA_U1_TIM15_CC2 = 194,
	DMA_U1_TIM15_CC3 = 195,
	DMA_U1_TIM15_CC4 = 196,

	DMA_MAX_REQ_ID = 197,
};

/*******************************************************************************
 * DMA channel info mapping table
 ******************************************************************************/
static const dma_chan_info_t dma_chan_info_tab[DMA_MAX_REQ_ID] = {
	/* ========== ACC_UDMA0 (ctrl_id = 0) ========== */
	/* USART0 */
	[DMA_U0_USART0_TX] = {.phy_chan_id = 0, .periph_sel = 0, .ctrl_id = 0},
	[DMA_U0_USART0_RX] = {.phy_chan_id = 1, .periph_sel = 8, .ctrl_id = 0},

	/* USART1 */
	[DMA_U0_USART1_TX] = {.phy_chan_id = 2, .periph_sel = 1, .ctrl_id = 0},
	[DMA_U0_USART1_RX] = {.phy_chan_id = 3, .periph_sel = 9, .ctrl_id = 0},

	/* USART2 */
	[DMA_U0_USART2_TX] = {.phy_chan_id = 0, .periph_sel = 2, .ctrl_id = 0},
	[DMA_U0_USART2_RX] = {.phy_chan_id = 1, .periph_sel = 10, .ctrl_id = 0},

	/* USART3 */
	[DMA_U0_USART3_TX] = {.phy_chan_id = 2, .periph_sel = 3, .ctrl_id = 0},
	[DMA_U0_USART3_RX] = {.phy_chan_id = 3, .periph_sel = 11, .ctrl_id = 0},

	/* USART4 */
	[DMA_U0_USART4_TX] = {.phy_chan_id = 0, .periph_sel = 4, .ctrl_id = 0},
	[DMA_U0_USART4_RX] = {.phy_chan_id = 1, .periph_sel = 12, .ctrl_id = 0},

	/* USART5 */
	[DMA_U0_USART5_TX] = {.phy_chan_id = 2, .periph_sel = 5, .ctrl_id = 0},
	[DMA_U0_USART5_RX] = {.phy_chan_id = 3, .periph_sel = 13, .ctrl_id = 0},

	/* USART6 */
	[DMA_U0_USART6_TX] = {.phy_chan_id = 0, .periph_sel = 6, .ctrl_id = 0},
	[DMA_U0_USART6_RX] = {.phy_chan_id = 1, .periph_sel = 14, .ctrl_id = 0},

	/* I2C0 */
	[DMA_U0_I2C0_TX] = {.phy_chan_id = 2, .periph_sel = 16, .ctrl_id = 0},
	[DMA_U0_I2C0_RX] = {.phy_chan_id = 3, .periph_sel = 24, .ctrl_id = 0},

	/* I2C1 */
	[DMA_U0_I2C1_TX] = {.phy_chan_id = 4, .periph_sel = 17, .ctrl_id = 0},
	[DMA_U0_I2C1_RX] = {.phy_chan_id = 5, .periph_sel = 25, .ctrl_id = 0},

	/* QSPI XIP0 */
	[DMA_U0_QSPI_XIP0_TX] = {.phy_chan_id = 2, .periph_sel = 18, .ctrl_id = 0},
	[DMA_U0_QSPI_XIP0_RX] = {.phy_chan_id = 3, .periph_sel = 26, .ctrl_id = 0},

	/* QSPI XIP1 */
	[DMA_U0_QSPI_XIP1_TX] = {.phy_chan_id = 2, .periph_sel = 19, .ctrl_id = 0},
	[DMA_U0_QSPI_XIP1_RX] = {.phy_chan_id = 3, .periph_sel = 27, .ctrl_id = 0},

	/* QSPI XIP2 */
	[DMA_U0_QSPI_XIP2_TX] = {.phy_chan_id = 2, .periph_sel = 20, .ctrl_id = 0},
	[DMA_U0_QSPI_XIP2_RX] = {.phy_chan_id = 3, .periph_sel = 28, .ctrl_id = 0},

	/* QSPI3 */
	[DMA_U0_QSPI3_TX] = {.phy_chan_id = 4, .periph_sel = 23, .ctrl_id = 0},
	[DMA_U0_QSPI3_RX] = {.phy_chan_id = 5, .periph_sel = 31, .ctrl_id = 0},

	/* DAC */
	[DMA_U0_DAC_CH1] = {.phy_chan_id = 2, .periph_sel = 21, .ctrl_id = 0},
	[DMA_U0_DAC_CH2] = {.phy_chan_id = 3, .periph_sel = 29, .ctrl_id = 0},

	/* DFSDM0 */
	[DMA_U0_DFSDM0_DMA0] = {.phy_chan_id = 2, .periph_sel = 22, .ctrl_id = 0},
	[DMA_U0_DFSDM0_DMA1] = {.phy_chan_id = 3, .periph_sel = 30, .ctrl_id = 0},
	[DMA_U0_DFSDM0_DMA2] = {.phy_chan_id = 12, .periph_sel = 96, .ctrl_id = 0},

	/* SAI0 */
	[DMA_U0_SAI0_A_TX] = {.phy_chan_id = 4, .periph_sel = 32, .ctrl_id = 0},
	[DMA_U0_SAI0_B_TX] = {.phy_chan_id = 4, .periph_sel = 33, .ctrl_id = 0},
	[DMA_U0_SAI1_A_TX] = {.phy_chan_id = 4, .periph_sel = 34, .ctrl_id = 0},
	[DMA_U0_SAI1_B_TX] = {.phy_chan_id = 4, .periph_sel = 35, .ctrl_id = 0},
	[DMA_U0_SAI0_A_RX] = {.phy_chan_id = 12, .periph_sel = 100, .ctrl_id = 0},
	[DMA_U0_SAI0_B_RX] = {.phy_chan_id = 12, .periph_sel = 101, .ctrl_id = 0},
	[DMA_U0_SAI1_A_RX] = {.phy_chan_id = 12, .periph_sel = 102, .ctrl_id = 0},
	[DMA_U0_SAI1_B_RX] = {.phy_chan_id = 12, .periph_sel = 103, .ctrl_id = 0},

	/* CORDIC0 */
	[DMA_U0_CORDIC0_TX] = {.phy_chan_id = 4, .periph_sel = 36, .ctrl_id = 0},
	[DMA_U0_CORDIC0_RX] = {.phy_chan_id = 12, .periph_sel = 97, .ctrl_id = 0},

	/* SWPMI0 */
	[DMA_U0_SWPMI0_TX] = {.phy_chan_id = 4, .periph_sel = 37, .ctrl_id = 0},
	[DMA_U0_SWPMI0_RX] = {.phy_chan_id = 12, .periph_sel = 98, .ctrl_id = 0},

	/* SPDIFRX0 */
	[DMA_U0_SPDIFRX0_CTRL] = {.phy_chan_id = 4, .periph_sel = 38, .ctrl_id = 0},
	[DMA_U0_SPDIFRX0_DAT] = {.phy_chan_id = 12, .periph_sel = 99, .ctrl_id = 0},

	/* Advanced Timer 0 */
	[DMA_U0_TIM0_COM] = {.phy_chan_id = 5, .periph_sel = 40, .ctrl_id = 0},
	[DMA_U0_TIM0_TRG] = {.phy_chan_id = 6, .periph_sel = 48, .ctrl_id = 0},
	[DMA_U0_TIM0_UEV] = {.phy_chan_id = 7, .periph_sel = 56, .ctrl_id = 0},
	[DMA_U0_TIM0_CC1] = {.phy_chan_id = 8, .periph_sel = 64, .ctrl_id = 0},
	[DMA_U0_TIM0_CC2] = {.phy_chan_id = 9, .periph_sel = 72, .ctrl_id = 0},
	[DMA_U0_TIM0_CC3] = {.phy_chan_id = 10, .periph_sel = 80, .ctrl_id = 0},
	[DMA_U0_TIM0_CC4] = {.phy_chan_id = 11, .periph_sel = 88, .ctrl_id = 0},

	/* Advanced Timer 1 */
	[DMA_U0_TIM1_COM] = {.phy_chan_id = 5, .periph_sel = 41, .ctrl_id = 0},
	[DMA_U0_TIM1_TRG] = {.phy_chan_id = 6, .periph_sel = 49, .ctrl_id = 0},
	[DMA_U0_TIM1_UEV] = {.phy_chan_id = 7, .periph_sel = 57, .ctrl_id = 0},
	[DMA_U0_TIM1_CC1] = {.phy_chan_id = 8, .periph_sel = 65, .ctrl_id = 0},
	[DMA_U0_TIM1_CC2] = {.phy_chan_id = 9, .periph_sel = 73, .ctrl_id = 0},
	[DMA_U0_TIM1_CC3] = {.phy_chan_id = 10, .periph_sel = 81, .ctrl_id = 0},
	[DMA_U0_TIM1_CC4] = {.phy_chan_id = 11, .periph_sel = 89, .ctrl_id = 0},

	/* Advanced Timer 2 */
	[DMA_U0_TIM2_COM] = {.phy_chan_id = 5, .periph_sel = 42, .ctrl_id = 0},
	[DMA_U0_TIM2_TRG] = {.phy_chan_id = 6, .periph_sel = 50, .ctrl_id = 0},
	[DMA_U0_TIM2_UEV] = {.phy_chan_id = 7, .periph_sel = 58, .ctrl_id = 0},
	[DMA_U0_TIM2_CC1] = {.phy_chan_id = 8, .periph_sel = 66, .ctrl_id = 0},
	[DMA_U0_TIM2_CC2] = {.phy_chan_id = 9, .periph_sel = 74, .ctrl_id = 0},
	[DMA_U0_TIM2_CC3] = {.phy_chan_id = 10, .periph_sel = 82, .ctrl_id = 0},
	[DMA_U0_TIM2_CC4] = {.phy_chan_id = 11, .periph_sel = 90, .ctrl_id = 0},

	/* Advanced Timer 3 */
	[DMA_U0_TIM3_COM] = {.phy_chan_id = 5, .periph_sel = 43, .ctrl_id = 0},
	[DMA_U0_TIM3_TRG] = {.phy_chan_id = 6, .periph_sel = 51, .ctrl_id = 0},
	[DMA_U0_TIM3_UEV] = {.phy_chan_id = 7, .periph_sel = 59, .ctrl_id = 0},
	[DMA_U0_TIM3_CC1] = {.phy_chan_id = 8, .periph_sel = 67, .ctrl_id = 0},
	[DMA_U0_TIM3_CC2] = {.phy_chan_id = 9, .periph_sel = 75, .ctrl_id = 0},
	[DMA_U0_TIM3_CC3] = {.phy_chan_id = 10, .periph_sel = 83, .ctrl_id = 0},
	[DMA_U0_TIM3_CC4] = {.phy_chan_id = 11, .periph_sel = 91, .ctrl_id = 0},

	/* Advanced Timer 4 */
	[DMA_U0_TIM4_COM] = {.phy_chan_id = 5, .periph_sel = 44, .ctrl_id = 0},
	[DMA_U0_TIM4_TRG] = {.phy_chan_id = 6, .periph_sel = 52, .ctrl_id = 0},
	[DMA_U0_TIM4_UEV] = {.phy_chan_id = 7, .periph_sel = 60, .ctrl_id = 0},
	[DMA_U0_TIM4_CC1] = {.phy_chan_id = 8, .periph_sel = 68, .ctrl_id = 0},
	[DMA_U0_TIM4_CC2] = {.phy_chan_id = 9, .periph_sel = 76, .ctrl_id = 0},
	[DMA_U0_TIM4_CC3] = {.phy_chan_id = 10, .periph_sel = 84, .ctrl_id = 0},
	[DMA_U0_TIM4_CC4] = {.phy_chan_id = 11, .periph_sel = 92, .ctrl_id = 0},

	/* Advanced Timer 5 */
	[DMA_U0_TIM5_COM] = {.phy_chan_id = 5, .periph_sel = 45, .ctrl_id = 0},
	[DMA_U0_TIM5_TRG] = {.phy_chan_id = 6, .periph_sel = 53, .ctrl_id = 0},
	[DMA_U0_TIM5_UEV] = {.phy_chan_id = 7, .periph_sel = 61, .ctrl_id = 0},
	[DMA_U0_TIM5_CC1] = {.phy_chan_id = 8, .periph_sel = 69, .ctrl_id = 0},
	[DMA_U0_TIM5_CC2] = {.phy_chan_id = 9, .periph_sel = 77, .ctrl_id = 0},
	[DMA_U0_TIM5_CC3] = {.phy_chan_id = 10, .periph_sel = 85, .ctrl_id = 0},
	[DMA_U0_TIM5_CC4] = {.phy_chan_id = 11, .periph_sel = 93, .ctrl_id = 0},

	/* Advanced Timer 6 */
	[DMA_U0_TIM6_COM] = {.phy_chan_id = 5, .periph_sel = 46, .ctrl_id = 0},
	[DMA_U0_TIM6_TRG] = {.phy_chan_id = 6, .periph_sel = 54, .ctrl_id = 0},
	[DMA_U0_TIM6_UEV] = {.phy_chan_id = 7, .periph_sel = 62, .ctrl_id = 0},
	[DMA_U0_TIM6_CC1] = {.phy_chan_id = 8, .periph_sel = 70, .ctrl_id = 0},
	[DMA_U0_TIM6_CC2] = {.phy_chan_id = 9, .periph_sel = 78, .ctrl_id = 0},
	[DMA_U0_TIM6_CC3] = {.phy_chan_id = 10, .periph_sel = 86, .ctrl_id = 0},
	[DMA_U0_TIM6_CC4] = {.phy_chan_id = 11, .periph_sel = 94, .ctrl_id = 0},

	/* Advanced Timer 7 */
	[DMA_U0_TIM7_COM] = {.phy_chan_id = 5, .periph_sel = 47, .ctrl_id = 0},
	[DMA_U0_TIM7_TRG] = {.phy_chan_id = 6, .periph_sel = 55, .ctrl_id = 0},
	[DMA_U0_TIM7_UEV] = {.phy_chan_id = 7, .periph_sel = 63, .ctrl_id = 0},
	[DMA_U0_TIM7_CC1] = {.phy_chan_id = 8, .periph_sel = 71, .ctrl_id = 0},
	[DMA_U0_TIM7_CC2] = {.phy_chan_id = 9, .periph_sel = 79, .ctrl_id = 0},
	[DMA_U0_TIM7_CC3] = {.phy_chan_id = 10, .periph_sel = 87, .ctrl_id = 0},
	[DMA_U0_TIM7_CC4] = {.phy_chan_id = 11, .periph_sel = 95, .ctrl_id = 0},

	/* ========== ACC_UDMA1 (ctrl_id = 1) ========== */
	/* USART7 */
	[DMA_U1_USART7_TX] = {.phy_chan_id = 3, .periph_sel = 14, .ctrl_id = 1},
	[DMA_U1_USART7_RX] = {.phy_chan_id = 9, .periph_sel = 91, .ctrl_id = 1},

	/* USART8 */
	[DMA_U1_USART8_TX] = {.phy_chan_id = 0, .periph_sel = 0, .ctrl_id = 1},
	[DMA_U1_USART8_RX] = {.phy_chan_id = 1, .periph_sel = 8, .ctrl_id = 1},

	/* USART9 */
	[DMA_U1_USART9_TX] = {.phy_chan_id = 2, .periph_sel = 1, .ctrl_id = 1},
	[DMA_U1_USART9_RX] = {.phy_chan_id = 3, .periph_sel = 9, .ctrl_id = 1},

	/* USART10 */
	[DMA_U1_USART10_TX] = {.phy_chan_id = 0, .periph_sel = 2, .ctrl_id = 1},
	[DMA_U1_USART10_RX] = {.phy_chan_id = 1, .periph_sel = 10, .ctrl_id = 1},

	/* USART11 */
	[DMA_U1_USART11_TX] = {.phy_chan_id = 2, .periph_sel = 3, .ctrl_id = 1},
	[DMA_U1_USART11_RX] = {.phy_chan_id = 3, .periph_sel = 11, .ctrl_id = 1},

	/* USART12 */
	[DMA_U1_USART12_TX] = {.phy_chan_id = 0, .periph_sel = 4, .ctrl_id = 1},
	[DMA_U1_USART12_RX] = {.phy_chan_id = 1, .periph_sel = 12, .ctrl_id = 1},

	/* I2C2 */
	[DMA_U1_I2C2_TX] = {.phy_chan_id = 0, .periph_sel = 5, .ctrl_id = 1},
	[DMA_U1_I2C2_RX] = {.phy_chan_id = 1, .periph_sel = 13, .ctrl_id = 1},

	/* I2C3 */
	[DMA_U1_I2C3_TX] = {.phy_chan_id = 2, .periph_sel = 6, .ctrl_id = 1},
	[DMA_U1_I2C3_RX] = {.phy_chan_id = 9, .periph_sel = 90, .ctrl_id = 1},

	/* QSPI4 */
	[DMA_U1_QSPI4_TX] = {.phy_chan_id = 0, .periph_sel = 7, .ctrl_id = 1},
	[DMA_U1_QSPI4_RX] = {.phy_chan_id = 1, .periph_sel = 15, .ctrl_id = 1},

	/* QSPI5 */
	[DMA_U1_QSPI5_TX] = {.phy_chan_id = 4, .periph_sel = 16, .ctrl_id = 1},
	[DMA_U1_QSPI5_RX] = {.phy_chan_id = 5, .periph_sel = 24, .ctrl_id = 1},

	/* QSPI6 */
	[DMA_U1_QSPI6_TX] = {.phy_chan_id = 2, .periph_sel = 17, .ctrl_id = 1},
	[DMA_U1_QSPI6_RX] = {.phy_chan_id = 3, .periph_sel = 25, .ctrl_id = 1},

	/* SAI0 - SAI2/SAI3 */
	[DMA_U1_SAI2_A_TX] = {.phy_chan_id = 2, .periph_sel = 18, .ctrl_id = 1},
	[DMA_U1_SAI2_B_TX] = {.phy_chan_id = 2, .periph_sel = 19, .ctrl_id = 1},
	[DMA_U1_SAI3_A_TX] = {.phy_chan_id = 2, .periph_sel = 20, .ctrl_id = 1},
	[DMA_U1_SAI3_B_TX] = {.phy_chan_id = 2, .periph_sel = 21, .ctrl_id = 1},
	[DMA_U1_SAI2_A_RX] = {.phy_chan_id = 3, .periph_sel = 26, .ctrl_id = 1},
	[DMA_U1_SAI2_B_RX] = {.phy_chan_id = 3, .periph_sel = 27, .ctrl_id = 1},
	[DMA_U1_SAI3_A_RX] = {.phy_chan_id = 3, .periph_sel = 28, .ctrl_id = 1},
	[DMA_U1_SAI3_B_RX] = {.phy_chan_id = 3, .periph_sel = 29, .ctrl_id = 1},

	/* ADC */
	[DMA_U1_ADC0] = {.phy_chan_id = 2, .periph_sel = 22, .ctrl_id = 1},
	[DMA_U1_ADC1] = {.phy_chan_id = 3, .periph_sel = 30, .ctrl_id = 1},
	[DMA_U1_ADC2] = {.phy_chan_id = 2, .periph_sel = 23, .ctrl_id = 1},

	/* DFSDM0 */
	[DMA_U1_DFSDM0_DMA3] = {.phy_chan_id = 3, .periph_sel = 31, .ctrl_id = 1},
	[DMA_U1_DFSDM0_DMA4] = {.phy_chan_id = 11, .periph_sel = 92, .ctrl_id = 1},
	[DMA_U1_DFSDM0_DMA5] = {.phy_chan_id = 11, .periph_sel = 93, .ctrl_id = 1},

	/* Advanced Timer 8 */
	[DMA_U1_TIM8_COM] = {.phy_chan_id = 4, .periph_sel = 32, .ctrl_id = 1},
	[DMA_U1_TIM8_TRG] = {.phy_chan_id = 5, .periph_sel = 40, .ctrl_id = 1},
	[DMA_U1_TIM8_UEV] = {.phy_chan_id = 6, .periph_sel = 48, .ctrl_id = 1},
	[DMA_U1_TIM8_CC1] = {.phy_chan_id = 7, .periph_sel = 56, .ctrl_id = 1},
	[DMA_U1_TIM8_CC2] = {.phy_chan_id = 8, .periph_sel = 64, .ctrl_id = 1},
	[DMA_U1_TIM8_CC3] = {.phy_chan_id = 9, .periph_sel = 72, .ctrl_id = 1},
	[DMA_U1_TIM8_CC4] = {.phy_chan_id = 10, .periph_sel = 80, .ctrl_id = 1},

	/* Advanced Timer 9 */
	[DMA_U1_TIM9_COM] = {.phy_chan_id = 4, .periph_sel = 33, .ctrl_id = 1},
	[DMA_U1_TIM9_TRG] = {.phy_chan_id = 5, .periph_sel = 41, .ctrl_id = 1},
	[DMA_U1_TIM9_UEV] = {.phy_chan_id = 6, .periph_sel = 49, .ctrl_id = 1},
	[DMA_U1_TIM9_CC1] = {.phy_chan_id = 7, .periph_sel = 57, .ctrl_id = 1},
	[DMA_U1_TIM9_CC2] = {.phy_chan_id = 8, .periph_sel = 65, .ctrl_id = 1},
	[DMA_U1_TIM9_CC3] = {.phy_chan_id = 9, .periph_sel = 73, .ctrl_id = 1},
	[DMA_U1_TIM9_CC4] = {.phy_chan_id = 10, .periph_sel = 81, .ctrl_id = 1},

	/* Advanced Timer 10 */
	[DMA_U1_TIM10_COM] = {.phy_chan_id = 4, .periph_sel = 34, .ctrl_id = 1},
	[DMA_U1_TIM10_TRG] = {.phy_chan_id = 5, .periph_sel = 42, .ctrl_id = 1},
	[DMA_U1_TIM10_UEV] = {.phy_chan_id = 6, .periph_sel = 50, .ctrl_id = 1},
	[DMA_U1_TIM10_CC1] = {.phy_chan_id = 7, .periph_sel = 58, .ctrl_id = 1},
	[DMA_U1_TIM10_CC2] = {.phy_chan_id = 8, .periph_sel = 66, .ctrl_id = 1},
	[DMA_U1_TIM10_CC3] = {.phy_chan_id = 9, .periph_sel = 74, .ctrl_id = 1},
	[DMA_U1_TIM10_CC4] = {.phy_chan_id = 10, .periph_sel = 82, .ctrl_id = 1},

	/* Advanced Timer 11 */
	[DMA_U1_TIM11_COM] = {.phy_chan_id = 4, .periph_sel = 35, .ctrl_id = 1},
	[DMA_U1_TIM11_TRG] = {.phy_chan_id = 5, .periph_sel = 43, .ctrl_id = 1},
	[DMA_U1_TIM11_UEV] = {.phy_chan_id = 6, .periph_sel = 51, .ctrl_id = 1},
	[DMA_U1_TIM11_CC1] = {.phy_chan_id = 7, .periph_sel = 59, .ctrl_id = 1},
	[DMA_U1_TIM11_CC2] = {.phy_chan_id = 8, .periph_sel = 67, .ctrl_id = 1},
	[DMA_U1_TIM11_CC3] = {.phy_chan_id = 9, .periph_sel = 75, .ctrl_id = 1},
	[DMA_U1_TIM11_CC4] = {.phy_chan_id = 10, .periph_sel = 83, .ctrl_id = 1},

	/* Advanced Timer 12 */
	[DMA_U1_TIM12_COM] = {.phy_chan_id = 4, .periph_sel = 36, .ctrl_id = 1},
	[DMA_U1_TIM12_TRG] = {.phy_chan_id = 5, .periph_sel = 44, .ctrl_id = 1},
	[DMA_U1_TIM12_UEV] = {.phy_chan_id = 6, .periph_sel = 52, .ctrl_id = 1},
	[DMA_U1_TIM12_CC1] = {.phy_chan_id = 7, .periph_sel = 60, .ctrl_id = 1},
	[DMA_U1_TIM12_CC2] = {.phy_chan_id = 8, .periph_sel = 68, .ctrl_id = 1},
	[DMA_U1_TIM12_CC3] = {.phy_chan_id = 9, .periph_sel = 76, .ctrl_id = 1},
	[DMA_U1_TIM12_CC4] = {.phy_chan_id = 10, .periph_sel = 84, .ctrl_id = 1},

	/* Advanced Timer 13 */
	[DMA_U1_TIM13_COM] = {.phy_chan_id = 4, .periph_sel = 37, .ctrl_id = 1},
	[DMA_U1_TIM13_TRG] = {.phy_chan_id = 5, .periph_sel = 45, .ctrl_id = 1},
	[DMA_U1_TIM13_UEV] = {.phy_chan_id = 6, .periph_sel = 53, .ctrl_id = 1},
	[DMA_U1_TIM13_CC1] = {.phy_chan_id = 7, .periph_sel = 61, .ctrl_id = 1},
	[DMA_U1_TIM13_CC2] = {.phy_chan_id = 8, .periph_sel = 69, .ctrl_id = 1},
	[DMA_U1_TIM13_CC3] = {.phy_chan_id = 9, .periph_sel = 77, .ctrl_id = 1},
	[DMA_U1_TIM13_CC4] = {.phy_chan_id = 10, .periph_sel = 85, .ctrl_id = 1},

	/* Advanced Timer 14 */
	[DMA_U1_TIM14_COM] = {.phy_chan_id = 4, .periph_sel = 38, .ctrl_id = 1},
	[DMA_U1_TIM14_TRG] = {.phy_chan_id = 5, .periph_sel = 46, .ctrl_id = 1},
	[DMA_U1_TIM14_UEV] = {.phy_chan_id = 6, .periph_sel = 54, .ctrl_id = 1},
	[DMA_U1_TIM14_CC1] = {.phy_chan_id = 7, .periph_sel = 62, .ctrl_id = 1},
	[DMA_U1_TIM14_CC2] = {.phy_chan_id = 8, .periph_sel = 70, .ctrl_id = 1},
	[DMA_U1_TIM14_CC3] = {.phy_chan_id = 9, .periph_sel = 78, .ctrl_id = 1},
	[DMA_U1_TIM14_CC4] = {.phy_chan_id = 10, .periph_sel = 86, .ctrl_id = 1},

	/* Advanced Timer 15 */
	[DMA_U1_TIM15_COM] = {.phy_chan_id = 4, .periph_sel = 39, .ctrl_id = 1},
	[DMA_U1_TIM15_TRG] = {.phy_chan_id = 5, .periph_sel = 47, .ctrl_id = 1},
	[DMA_U1_TIM15_UEV] = {.phy_chan_id = 6, .periph_sel = 55, .ctrl_id = 1},
	[DMA_U1_TIM15_CC1] = {.phy_chan_id = 7, .periph_sel = 63, .ctrl_id = 1},
	[DMA_U1_TIM15_CC2] = {.phy_chan_id = 8, .periph_sel = 71, .ctrl_id = 1},
	[DMA_U1_TIM15_CC3] = {.phy_chan_id = 9, .periph_sel = 79, .ctrl_id = 1},
	[DMA_U1_TIM15_CC4] = {.phy_chan_id = 10, .periph_sel = 87, .ctrl_id = 1},
};

struct nuclei_dmac_desc_node {
	dma_addr_t src;
	dma_addr_t dst;
	/* data transfer width */
	u32 data_width;
	/* transfer num of a burst,only used by mem2mem */
	u32 trans_num;
	/* one dma transfer size */
	u32 trans_size;
};

struct nuclei_dmac_desc {
	struct virt_dma_desc vd;
	unsigned int nr_node;
	unsigned int cur_node;
	enum dma_transfer_direction dir;
	struct nuclei_dmac_desc_node nodes[];
};

struct nuclei_dmac_pchan {
	struct nuclei_dmac_vchan *vchan;
	void __iomem *reg_ch_cfg_base;
	void __iomem *reg_ch_irq_base;
	/* channel phy id */
	int id;
};

struct nuclei_dmac_vchan {
	struct virt_dma_chan vc;
	struct nuclei_dmac_device *xdev;
	struct nuclei_dmac_desc *xd;
	struct nuclei_dmac_pchan *pchan;
	struct dma_slave_config sconfig;
	/* req id used as TRANS_PER_SEL, only used by slave device */
	int reqid;
	/* whether this channel is a device (slave) or for memcpy */
	bool slave;
};

struct nuclei_dmac_device {
	struct dma_device ddev_slave;
	struct dma_device ddev_memcpy;
	void __iomem *reg_base;
	struct clk *clk;
	int nr_pchans_slave;
	int nr_pchans_memcpy;
	int nr_pchans;
	struct nuclei_dmac_pchan *pchans;
	int nr_vchans;
	struct nuclei_dmac_vchan *vchans;
	int dmac_ctrl_id;
};

static struct nuclei_dmac_vchan *
to_nuclei_dmac_vchan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct nuclei_dmac_vchan, vc);
}

static struct nuclei_dmac_desc *
to_nuclei_dmac_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct nuclei_dmac_desc, vd);
}

/* xc->vc.lock must be held by caller */
static struct nuclei_dmac_desc *
nuclei_dmac_next_desc(struct nuclei_dmac_vchan *xc)
{
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&xc->vc);
	if (!vd)
		return NULL;

	list_del(&vd->node);

	return to_nuclei_dmac_desc(vd);
}

static struct nuclei_dmac_pchan * nuclei_dmac_find_free_pchan(
	struct nuclei_dmac_vchan *vc)
{
	struct nuclei_dmac_pchan *pch = NULL;
	int i;
	int val;

	if (vc->slave == 0) {
		for (i = 0; i < vc->xdev->nr_pchans_memcpy; i++) {
			val = readl(vc->xdev->pchans[i].reg_ch_cfg_base + 
				DMAC_MEM_CH_CTRL_OFF);
			if (val & 0x3)
				continue;
			else {
				pch = vc->xdev->pchans + i;
				break;
			}
		}
	} else {
		/*
		 * reqid is bind to phy channel by hardware.
		 */
		if (((vc->reqid >= DMA_U0_MEM2MEM_0) && (vc->reqid < DMA_U0_MAX_REQ_ID)) ||
		((vc->reqid >= 0) && (vc->reqid < DMA_U0_MAX_REQ_ID - DMA_U0_MAX_REQ_ID))) {
			int tab_idx;

			tab_idx = vc->reqid + ((vc->xdev->dmac_ctrl_id) ? DMA_U0_MAX_REQ_ID : 0);
			i = dma_chan_info_tab[tab_idx].phy_chan_id + vc->xdev->nr_pchans_memcpy;
			val = readl(vc->xdev->pchans[i].reg_ch_cfg_base + 
					DMAC_PA_CH_CTRL_OFF);
			if (!(val & 0x3))
				pch = vc->xdev->pchans + i;
		}
	}

	return pch;
}

/* xc->vc.lock must be held by caller */
static void nuclei_dmac_chan_start(struct nuclei_dmac_vchan *ndvch,
				      struct nuclei_dmac_desc *xd)
{
	u32 src_mode, src_width;
	u32 dst_mode, dst_width;
	dma_addr_t src_addr, dst_addr;
	u32 val, tnum, tsize;
	u32 offset;

	/* select phy channel to execute vchan DMA request */
	if (ndvch->pchan == NULL) {
		ndvch->pchan = nuclei_dmac_find_free_pchan(ndvch);
		if (!ndvch->pchan) {
			dev_info(NULL, "no free phy channel\n");
			return ;
		}
	}
	/* wait free if dma busy */
	offset = ndvch->slave ? DMAC_PA_CH_CTRL_OFF : DMAC_MEM_CH_CTRL_OFF;
	val = readl(ndvch->pchan->reg_ch_cfg_base + offset);
	while (val & 0x1) {};

	ndvch->pchan->vchan = ndvch;
	/* get vchan desc data */
	src_addr = xd->nodes[xd->cur_node].src;
	dst_addr = xd->nodes[xd->cur_node].dst;

	if (xd->dir == DMA_DEV_TO_MEM) {
		src_mode = DMAC_ADDR_MODE_FIXED;
		src_width = ndvch->sconfig.src_addr_width;
	} else {
		src_mode = DMAC_ADDR_MODE_INC;
		src_width = xd->nodes[xd->cur_node].data_width;
	}

	if (xd->dir == DMA_MEM_TO_DEV) {
		dst_mode = DMAC_ADDR_MODE_FIXED;
		dst_width = ndvch->sconfig.dst_addr_width;
	} else {
		dst_mode = DMAC_ADDR_MODE_INC;
		dst_width = xd->nodes[xd->cur_node].data_width;
	}

	/* mem2mem tranfer */
	if (ndvch->slave == false) {
		tnum = xd->nodes[xd->cur_node].trans_num;
		tsize = xd->nodes[xd->cur_node].trans_size;
		val = 0;
		//val |= (ffs(dst_width)-1) << DMAC_MEM_DST_WIDTH_SHIFT;
		val |= (ffs(src_width)-1) << DMAC_MEM_SRC_WIDTH_SHIFT;
		val |= src_mode << DMAC_MEM_SRC_ADDR_MODE_SHIFT;
		val |= dst_mode << DMAC_MEM_DST_ADDR_MODE_SHIFT;
		val |= ((tnum-1) & 0xF) << DMAC_MEM_DST_BURST_SHIFT;
		val |= ((tnum-1) & 0xF) << DMAC_MEM_SRC_BURST_SHIFT;
		writel(val, ndvch->pchan->reg_ch_cfg_base + DMAC_MEM_CH_CTRL_OFF);
		/* config channel addr,transfer size */
		writel(lower_32_bits(src_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_MEM_CH_SRC_LBASE_OFF);
		writel(upper_32_bits(src_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_MEM_CH_SRC_HBASE_OFF);
		writel(lower_32_bits(dst_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_MEM_CH_DST_LBASE_OFF);
		writel(upper_32_bits(dst_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_MEM_CH_DST_HBASE_OFF);
		writel(tsize, ndvch->pchan->reg_ch_cfg_base + DMAC_MEM_CH_TSIZE_OFF);

		/* enable interrupt */
		writel(DMAC_IRQ_FTI_MASK | DMAC_IRQ_ETI_MASK,
			ndvch->pchan->reg_ch_irq_base + DMAC_CH_IRQ_EN_OFF);
		/* start DMA transfer */
		val = readl(ndvch->pchan->reg_ch_cfg_base + DMAC_MEM_CH_CTRL_OFF);
		val |= 1 << DMAC_MEM_TRANS_EN_SHIFT;
		writel(val, ndvch->pchan->reg_ch_cfg_base + DMAC_MEM_CH_CTRL_OFF);
	} else {
	/* pa2mem tranfer */
		BUG_ON(dst_width != src_width);

		tsize = xd->nodes[xd->cur_node].trans_size;
		/* config channel addr,transfer size */
		writel(lower_32_bits(src_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_PA_CH_SRC_LBASE_OFF);
		writel(upper_32_bits(src_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_PA_CH_SRC_HBASE_OFF);
		writel(lower_32_bits(dst_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_PA_CH_DST_LBASE_OFF);
		writel(upper_32_bits(dst_addr), ndvch->pchan->reg_ch_cfg_base + 
			DMAC_PA_CH_DST_HBASE_OFF);
		writel(tsize, ndvch->pchan->reg_ch_cfg_base + DMAC_PA_CH_TSIZE_OFF);
		/* enable interrupt */
		writel(DMAC_IRQ_FTI_MASK | DMAC_IRQ_ETI_MASK,
			ndvch->pchan->reg_ch_irq_base + DMAC_CH_IRQ_EN_OFF);
		/* start DMA transfer */
		val = 0;
		val |= ((ffs(dst_width)-1) & 0x7) << DMAC_PA_DATA_WIDTH_SHIFT;
		val |= (src_mode & 0x1) << DMAC_PA_SRC_ADDR_MODE_SHIFT;
		val |= (dst_mode & 0x1) << DMAC_PA_DST_ADDR_MODE_SHIFT;
		val |= ((dma_chan_info_tab[ndvch->reqid].periph_sel) & 0xFF) << DMAC_PA_TRANS_PER_SEL_SHIFT;
		val |= 1 << DMAC_PA_TRANS_EN_SHIFT;
		writel(val, ndvch->pchan->reg_ch_cfg_base + DMAC_PA_CH_CTRL_OFF);
	}
}

/* ndvch->vc.lock must be held by caller */
static int nuclei_dmac_chan_stop(struct nuclei_dmac_vchan *ndvch)
{
	u32 val;
	u32 offset;

	if (ndvch->pchan == NULL) {
		dev_warn(ndvch->xdev->ddev_slave.dev, "pchan is null,cannot stop\n");
		return -1;
	}
	if (ndvch->slave == false) {
		offset = DMAC_MEM_CH_CTRL_OFF;
	} else {
		offset = DMAC_PA_CH_CTRL_OFF;
	}

	/* disable interrupt */
	val = readl(ndvch->pchan->reg_ch_irq_base + DMAC_CH_IRQ_EN_OFF);
	val &= ~(DMAC_IRQ_FTI_MASK | DMAC_IRQ_ETI_MASK);
	writel(val, ndvch->pchan->reg_ch_irq_base + DMAC_CH_IRQ_EN_OFF);
	/* stop DMA */
	val = readl(ndvch->pchan->reg_ch_cfg_base + offset);
	val &= ~(1 << DMAC_MEM_TRANS_EN_SHIFT);
	writel(val, ndvch->pchan->reg_ch_cfg_base + offset);

	return 0;
}

/* ndvch->vc.lock must be held by caller */
static void nuclei_dmac_start(struct nuclei_dmac_vchan *ndvch)
{
	struct nuclei_dmac_desc *xd;

	xd = nuclei_dmac_next_desc(ndvch);
	if (xd)
		nuclei_dmac_chan_start(ndvch, xd);

	/* set desc to chan regardless of xd is null */
	ndvch->xd = xd;
	if (xd == NULL)
		ndvch->pchan = NULL;
}

static void nuclei_dmac_chan_irq(struct nuclei_dmac_pchan *pchan)
{
	u32 stat;
	int ret;
	struct nuclei_dmac_vchan *vch;

	vch = pchan->vchan;
	/* phy channel have no dma transfer*/
	if (vch==NULL)
		return;

	spin_lock(&vch->vc.lock);

	stat = readl(pchan->reg_ch_irq_base + DMAC_CH_IRQ_STAT_OFF);
	if (stat & DMAC_IRQ_ETI_MASK) {
		ret = nuclei_dmac_chan_stop(vch);
		if (ret)
			dev_err(vch->xdev->ddev_slave.dev,
				"DMA transfer error with aborting issue\n");
		else
			dev_err(vch->xdev->ddev_slave.dev,
				"DMA transfer error\n");
		/* write bits to clear */
		writel(stat, pchan->reg_ch_irq_base + DMAC_CH_IRQ_CLR_OFF);

	} else if ((stat & DMAC_IRQ_FTI_MASK) && vch->xd) {
		vch->xd->cur_node++;
		/* write bits to clear */
		writel(stat, pchan->reg_ch_irq_base + DMAC_CH_IRQ_CLR_OFF);
		if (vch->xd->cur_node >= vch->xd->nr_node) {
			vchan_cookie_complete(&vch->xd->vd);
			nuclei_dmac_start(vch);
		} else {
			nuclei_dmac_chan_start(vch, vch->xd);
		}
	}

	spin_unlock(&vch->vc.lock);
}

static irqreturn_t nuclei_dmac_irq_handler(int irq, void *dev_id)
{
	struct nuclei_dmac_device *xdev = dev_id;
	int i;

	for (i = 0; i < xdev->nr_pchans; i++)
		nuclei_dmac_chan_irq(&xdev->pchans[i]);

	return IRQ_HANDLED;
}

static void nuclei_dmac_free_chan_resources(struct dma_chan *chan)
{
	vchan_free_chan_resources(to_virt_chan(chan));
}

static struct dma_async_tx_descriptor *
nuclei_dmac_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dst,
			       dma_addr_t src, size_t len, unsigned long flags)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct nuclei_dmac_vchan *ndvch = to_nuclei_dmac_vchan(vc);
	struct nuclei_dmac_desc *xd;
	unsigned int nr;
	int i=0;

	ndvch->slave=0;
	ndvch->reqid=-1;

	nr = len/DMAC_MEM_MAX_TRANS_SIZE +
			((len%DMAC_MEM_MAX_TRANS_SIZE > 0) ? 1 : 0);

	xd = kzalloc(struct_size(xd, nodes, nr), GFP_NOWAIT);
	if (!xd)
		return NULL;

	xd->dir = DMA_MEM_TO_MEM;
	xd->nr_node = nr;
	xd->cur_node = 0;

	for (i = 0; len >= DMAC_MEM_MAX_TRANS_SIZE; i++) {
		xd->nodes[i].trans_size = DMAC_MEM_MAX_TRANS_SIZE;
		xd->nodes[i].src = src;
		xd->nodes[i].dst = dst;
		xd->nodes[i].data_width = DMA_SLAVE_BUSWIDTH_16_BYTES;
		xd->nodes[i].trans_num = 16;
		src += DMAC_MEM_MAX_TRANS_SIZE;
		dst += DMAC_MEM_MAX_TRANS_SIZE;
		len -= DMAC_MEM_MAX_TRANS_SIZE;
	}

	if (len) {
		xd->nodes[i].trans_size = len;
		xd->nodes[i].src = src;
		xd->nodes[i].dst = dst;
		xd->nodes[i].data_width = DMA_SLAVE_BUSWIDTH_16_BYTES;
		xd->nodes[i].trans_num = 16;
	}

	return vchan_tx_prep(vc, &xd->vd, flags);
}

static struct dma_async_tx_descriptor *
nuclei_dmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			     unsigned int sg_len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct nuclei_dmac_vchan *xc = to_nuclei_dmac_vchan(vc);
	struct nuclei_dmac_desc *xd;
	struct scatterlist *sg;
	enum dma_slave_buswidth buswidth;
	int i;

	if (!is_slave_direction(direction))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		buswidth = xc->sconfig.src_addr_width;
	} else {
		buswidth = xc->sconfig.dst_addr_width;
	}

	xd = kzalloc(struct_size(xd, nodes, sg_len), GFP_NOWAIT);
	if (!xd)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		xd->nodes[i].src = (direction == DMA_DEV_TO_MEM)
			? xc->sconfig.src_addr : sg_dma_address(sg);
		xd->nodes[i].dst = (direction == DMA_MEM_TO_DEV)
			? xc->sconfig.dst_addr : sg_dma_address(sg);
		xd->nodes[i].data_width = buswidth;
		xd->nodes[i].trans_size = sg_dma_len(sg);
		/*
		 * Currently transfer that size doesn't align the unit size
		 * (the number of burst words * bus-width) is not allowed,
		 * because the driver does not support the way to transfer
		 * residue size. As a matter of fact, in order to transfer
		 * arbitrary size, 'src_maxburst' or 'dst_maxburst' of
		 * dma_slave_config must be 1.
		 */
		if (sg_dma_len(sg) % xd->nodes[i].data_width) {
			dev_err(xc->xdev->ddev_slave.dev,
				"Unaligned transfer size: %d,%d", sg_dma_len(sg), 
				xd->nodes[i].data_width);
			kfree(xd);
			return NULL;
		}

		if (xd->nodes[i].trans_size > DMAC_PA_MAX_TRANS_SIZE) {
			dev_err(xc->xdev->ddev_slave.dev,
				"Exceed maximum transfer size %d,%ld", 
				xd->nodes[i].trans_size, DMAC_PA_MAX_TRANS_SIZE);
			kfree(xd);
			return NULL;
		}
	}

	xd->dir = direction;
	xd->nr_node = sg_len;
	xd->cur_node = 0;

	return vchan_tx_prep(vc, &xd->vd, flags);
}

static int nuclei_dmac_slave_config(struct dma_chan *chan,
				       struct dma_slave_config *config)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct nuclei_dmac_vchan *xc = to_nuclei_dmac_vchan(vc);

	memcpy(&xc->sconfig, config, sizeof(*config));

	return 0;
}

static int nuclei_dmac_terminate_all(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct nuclei_dmac_vchan *ndvch = to_nuclei_dmac_vchan(vc);
	unsigned long flags;
	int ret = 0;
	LIST_HEAD(head);

	spin_lock_irqsave(&vc->lock, flags);

	if (ndvch->xd) {
		vchan_terminate_vdesc(&ndvch->xd->vd);
		ndvch->xd = NULL;
		ret = nuclei_dmac_chan_stop(ndvch);
	}

	vchan_get_all_descriptors(vc, &head);

	spin_unlock_irqrestore(&vc->lock, flags);

	vchan_dma_desc_free_list(vc, &head);

	return ret;
}

static void nuclei_dmac_synchronize(struct dma_chan *chan)
{
	vchan_synchronize(to_virt_chan(chan));
}

static void nuclei_dmac_issue_pending(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct nuclei_dmac_vchan *ndvch = to_nuclei_dmac_vchan(vc);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);

	if (vchan_issue_pending(vc) && !ndvch->xd)
		nuclei_dmac_start(ndvch);

	spin_unlock_irqrestore(&vc->lock, flags);
}

static void nuclei_dmac_desc_free(struct virt_dma_desc *vd)
{
	kfree(to_nuclei_dmac_desc(vd));
}

static void nuclei_dmac_pchan_init(struct nuclei_dmac_device *xdev,
				     int ch)
{
	struct nuclei_dmac_pchan *ndpch = &xdev->pchans[ch];

	memset(ndpch, 0, sizeof(struct nuclei_dmac_pchan));
	if (ch < xdev->nr_pchans_memcpy) {
		ndpch->reg_ch_cfg_base = xdev->reg_base + DMAC_MEM_CH_CFG_OFF(ch);
		ndpch->reg_ch_irq_base = xdev->reg_base + DMAC_MEM_CH_IRQ_OFF(ch);
		ndpch->id = ch;
	} else {
		ndpch->reg_ch_cfg_base = xdev->reg_base + 
			DMAC_SLAVE_CH_CFG_OFF(ch - xdev->nr_pchans_memcpy);
		ndpch->reg_ch_irq_base = xdev->reg_base + 
			DMAC_SLAVE_CH_IRQ_OFF(ch - xdev->nr_pchans_memcpy);
		/* pa2mem channel id start from 0 */
		ndpch->id = ch - xdev->nr_pchans_memcpy;
	}
}

static struct dma_chan *of_dma_nuclei_xlate(struct of_phandle_args *dma_spec,
					      struct of_dma *ofdma)
{
	struct nuclei_dmac_device *xdev = ofdma->of_dma_data;
	int req_id = dma_spec->args[1];
	int slave = dma_spec->args[0];

	/* 
	 * args define: 
	 * args[0]:channel type(MEMORY,SLAVE),
	 * args[1]:request id for SLAVE or MEMORY
	 */
	if (dma_spec->args_count != 2)
		return NULL;
	if (req_id < DMA_U0_MEM2MEM_0 || req_id >= DMA_MAX_REQ_ID)
		return NULL;
	/* get real req_id */
	req_id = req_id - ((xdev->dmac_ctrl_id) ? DMA_U0_MAX_REQ_ID : 0);
	xdev->vchans[req_id].reqid = req_id;
	xdev->vchans[req_id].slave = slave;
	xdev->vchans[req_id].xdev = xdev;

	return dma_get_slave_channel(&xdev->vchans[req_id].vc.chan);
}

static int nuclei_dmac_probe(struct platform_device *pdev)
{
	struct nuclei_dmac_device *xdev;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct dma_device *ddev;
	int irq;
	int i, ret, nr_mem_channels, nr_channels, nr_requests;

	xdev = devm_kzalloc(dev, sizeof(struct nuclei_dmac_device),
			    GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->reg_base))
		return PTR_ERR(xdev->reg_base);

	ret = of_property_read_u32(np, "dma-ctrl-id", &xdev->dmac_ctrl_id);
	if (ret) {
		dev_warn(dev, "can't get dma ctrl id, use udma0 as default\n");
		/* default value is 0 */
		xdev->dmac_ctrl_id = 0;
	}

	ret = of_property_read_u32(np, "dma-memcpy", &nr_mem_channels);
	if (ret) {
		dev_err(dev, "can't get dma-requests\n");
		return ret;
	}

	ret = of_property_read_u32(np, "dma-channels", &nr_channels);
	if (ret) {
		dev_err(dev, "can't get dma-channels\n");
		return ret;
	}

	ret = of_property_read_u32(np, "dma-requests", &nr_requests);
	if (ret) {
		dev_err(dev, "can't get dma-requests\n");
		return ret;
	}

	dev_info(dev, "dma-channels %d, dma-requests %d\n",
		 nr_channels, nr_requests);

	ddev = &xdev->ddev_memcpy;
	ddev->dev = dev;
	dma_cap_zero(ddev->cap_mask);
	dma_cap_set(DMA_MEMCPY, ddev->cap_mask);
	ddev->src_addr_widths = NUCLEI_DMAC_BUSWIDTHS;
	ddev->dst_addr_widths = NUCLEI_DMAC_BUSWIDTHS;
	ddev->directions = BIT(DMA_MEM_TO_MEM);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	ddev->max_burst = NUCLEI_MAX_BURST;
	ddev->device_free_chan_resources = nuclei_dmac_free_chan_resources;
	ddev->device_prep_dma_memcpy = nuclei_dmac_prep_dma_memcpy;
	ddev->device_config = nuclei_dmac_slave_config;
	ddev->device_terminate_all = nuclei_dmac_terminate_all;
	ddev->device_synchronize = nuclei_dmac_synchronize;
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = nuclei_dmac_issue_pending;
	INIT_LIST_HEAD(&ddev->channels);

	/* dma slave device */
	ddev = &xdev->ddev_slave;
	ddev->dev = dev;
	dma_cap_zero(ddev->cap_mask);
	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	ddev->src_addr_widths = NUCLEI_DMAC_BUSWIDTHS;
	ddev->dst_addr_widths = NUCLEI_DMAC_BUSWIDTHS;
	ddev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	ddev->max_burst = NUCLEI_MAX_BURST;
	ddev->device_free_chan_resources = nuclei_dmac_free_chan_resources;
	ddev->device_prep_slave_sg = nuclei_dmac_prep_slave_sg;
	ddev->device_config = nuclei_dmac_slave_config;
	ddev->device_terminate_all = nuclei_dmac_terminate_all;
	ddev->device_synchronize = nuclei_dmac_synchronize;
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = nuclei_dmac_issue_pending;
	INIT_LIST_HEAD(&ddev->channels);

	/*
	 * demoddr soc have 2 memch DMA request line 
	 * and 18 slave DMA request line.
	 *
	 */
	xdev->nr_pchans_memcpy = nr_mem_channels;
	xdev->nr_pchans_slave = nr_channels;
	xdev->nr_pchans = xdev->nr_pchans_memcpy + xdev->nr_pchans_slave;
	xdev->nr_vchans = nr_requests;
	/* init phy channel */
	xdev->pchans = devm_kcalloc(dev, xdev->nr_pchans,
				  sizeof(struct nuclei_dmac_pchan), GFP_KERNEL);
	if (!xdev->pchans)
		return -ENOMEM;


	for (i = 0; i < xdev->nr_pchans; i++)
		nuclei_dmac_pchan_init(xdev, i);

	/* Init virtual channel */
	xdev->vchans = devm_kcalloc(dev, xdev->nr_vchans,
				  sizeof(struct nuclei_dmac_vchan), GFP_KERNEL);
	if (!xdev->vchans)
		return -ENOMEM;

	for (i = 0; i < xdev->nr_vchans; i++) {
		struct nuclei_dmac_vchan *vchan = &xdev->vchans[i];

		vchan->vc.desc_free = nuclei_dmac_desc_free;
		vchan->xdev = xdev;
		if (i < xdev->nr_pchans_memcpy) {
			vchan->slave = 0;
			vchan->reqid = -1;
			vchan_init(&vchan->vc, &xdev->ddev_memcpy);
		} else {
			vchan->slave = 1;
			vchan->reqid = i;
			vchan_init(&vchan->vc, &xdev->ddev_slave);
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, nuclei_dmac_irq_handler,
			       IRQF_ONESHOT, "dmac", xdev);
	if (ret) {
		dev_err(dev, "Failed to request IRQ\n");
		return ret;
	}

	xdev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(xdev->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(xdev->clk);
	}
	ret = clk_prepare_enable(xdev->clk);
	if (ret < 0) {
		dev_err(dev, "clk_prep_enable error: %d\n", ret);
		return ret;
	}

	ret = dma_async_device_register(&xdev->ddev_memcpy);
	if (ret) {
		dev_err(dev, "Failed to register DMA memory device\n");
		goto err_register_memcpy;
	}

	ret = dma_async_device_register(&xdev->ddev_slave);
	if (ret) {
		dev_err(dev, "Failed to register DMA slave device\n");
		goto err_register_slave;
	}

	ret = of_dma_controller_register(dev->of_node,
					 of_dma_nuclei_xlate, xdev);
	if (ret) {
		dev_err(dev, "Failed to register DMA controller\n");
		goto err_register_dma_helper;
	}

	platform_set_drvdata(pdev, xdev);

	dev_info(&pdev->dev, "NUCLEI DMAC driver (%d vchan) (%d pchan)\n",
		 xdev->nr_vchans, xdev->nr_pchans);

	return 0;

err_register_dma_helper:
	dma_async_device_unregister(&xdev->ddev_slave);
err_register_slave:
	dma_async_device_unregister(&xdev->ddev_memcpy);
err_register_memcpy:
	clk_disable_unprepare(xdev->clk);

	return ret;
}

static int nuclei_dmac_remove(struct platform_device *pdev)
{
	struct nuclei_dmac_device *xdev = platform_get_drvdata(pdev);
	struct dma_device *ddev;
	struct dma_chan *chan;
	int ret;

	/*
	 * Before reaching here, almost all descriptors have been freed by the
	 * ->device_free_chan_resources() hook. However, each channel might
	 * be still holding one descriptor that was on-flight at that moment.
	 * Terminate it to make sure this hardware is no longer running. Then,
	 * free the channel resources once again to avoid memory leak.
	 */
	ddev = &xdev->ddev_memcpy;
	list_for_each_entry(chan, &ddev->channels, device_node) {
		ret = dmaengine_terminate_sync(chan);
		if (ret)
			return ret;
		nuclei_dmac_free_chan_resources(chan);
	}
	dma_async_device_unregister(ddev);

	ddev = &xdev->ddev_slave;
	list_for_each_entry(chan, &ddev->channels, device_node) {
		ret = dmaengine_terminate_sync(chan);
		if (ret)
			return ret;
		nuclei_dmac_free_chan_resources(chan);
	}
	dma_async_device_unregister(ddev);
	of_dma_controller_free(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id nuclei_dmac_match[] = {
	{ .compatible = "nuclei,dmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nuclei_dmac_match);

static struct platform_driver nuclei_dmac_driver = {
	.probe = nuclei_dmac_probe,
	.remove = nuclei_dmac_remove,
	.driver = {
		.name = "nuclei-dmac",
		.of_match_table = nuclei_dmac_match,
	},
};
module_platform_driver(nuclei_dmac_driver);

MODULE_DESCRIPTION("NUCLEI DMA controller driver");
MODULE_LICENSE("GPL v2");
