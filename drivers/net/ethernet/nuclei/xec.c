// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Author: Huaqi Fang <hqfang@nucleisys.com>
 *
 * Copyright (C) 2021 Nuclei
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

// Uncomment to enable xec debug messages
// #define DEBUG

#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define MODNAME "nuclei-xec"
#define DRV_VERSION "1.00"

#define ENET_MAXF_SIZE 1536
#define ENET_RX_DESC 32
#define ENET_TX_DESC 32

#define NAPI_WEIGHT 16


/*
 * XEC IOC Register offsets
 */
#define XEC_IOC_ID(base)			(base + 0x000)
#define XEC_IOC_VERSION(base)			(base + 0x004)
#define XEC_IOC_CONFIG(base)			(base + 0x008)
#define XEC_IOC_SYS_RD_CONFIG(base)		(base + 0x00C)
#define XEC_IOC_SYS_WR_CONFIG(base)		(base + 0x010)
#define XEC_IOC_TX_CONFIG(base)			(base + 0x014)
#define XEC_IOC_RX_CONFIG(base)			(base + 0x018)

/*
 * XEC IOC Channel Register offsets
 */
#define XEC_IOC_CH_TX_CTRL(base)		(base + 0x000)
#define XEC_IOC_CH_RX_CTRL(base)		(base + 0x004)
#define XEC_IOC_CH_TX_LIST_HADDR(base)		(base + 0x008)
#define XEC_IOC_CH_TX_LIST_LADDR(base)		(base + 0x00C)
#define XEC_IOC_CH_RX_LIST_HADDR(base)		(base + 0x010)
#define XEC_IOC_CH_RX_LIST_LADDR(base)		(base + 0x014)
#define XEC_IOC_CH_TX_TAIL_POINTER(base)	(base + 0x018)
#define XEC_IOC_CH_RX_TAIL_POINTER(base)	(base + 0x01C)
#define XEC_IOC_CH_INTERRUPT_ENABLE(base)	(base + 0x020)
#define XEC_IOC_CH_INTERRUPT(base)		(base + 0x024)
#define XEC_IOC_CH_TX_HEAD_POINTER(base)	(base + 0x028)
#define XEC_IOC_CH_RX_HEAD_POINTER(base)	(base + 0x02C)

/*
 * XEC MAC Register offsets
 */
#define XEC_MAC_ID(base)			(base + 0x000)
#define XEC_MAC_VERSION(base)			(base + 0x004)
#define XEC_MAC_ADDR_LO(base)			(base + 0x008)
#define XEC_MAC_ADDR_HI(base)			(base + 0x00C)
#define XEC_MAC_CONFIGURE_0(base)		(base + 0x010)
#define XEC_MAC_CONFIGURE_1(base)		(base + 0x014)
#define XEC_MAC_CONFIGURE_2(base)		(base + 0x018)
#define XEC_MAC_CONFIGURE_3(base)		(base + 0x01C)
#define XEC_MAC_TX_CONFIGURE_0(base)		(base + 0x020)
#define XEC_MAC_TX_CONFIGURE_1(base)		(base + 0x024)
#define XEC_MAC_TX_CONFIGURE_2(base)		(base + 0x028)
#define XEC_MAC_TX_CONFIGURE_3(base)		(base + 0x02C)
#define XEC_MAC_RX_CONFIGURE_0(base)		(base + 0x030)
#define XEC_MAC_RX_CONFIGURE_1(base)		(base + 0x034)
#define XEC_MAC_RX_CONFIGURE_2(base)		(base + 0x038)
#define XEC_MAC_RX_CONFIGURE_3(base)		(base + 0x03C)
#define XEC_MAC_TX_CONTROL_0(base)		(base + 0x040)
#define XEC_MAC_TX_CONTROL_1(base)		(base + 0x044)
#define XEC_MAC_TX_CONTROL_2(base)		(base + 0x048)
#define XEC_MAC_TX_CONTROL_3(base)		(base + 0x04C)
#define XEC_MAC_RX_CONTROL_0(base)		(base + 0x050)
#define XEC_MAC_RX_CONTROL_1(base)		(base + 0x054)
#define XEC_MAC_RX_CONTROL_2(base)		(base + 0x058)
#define XEC_MAC_RX_CONTROL_3(base)		(base + 0x05C)
#define XEC_MAC_MDIO_DATA(base)			(base + 0x060)
#define XEC_MAC_MDIO_CONTROL_STATUS(base)	(base + 0x064)
#define XEC_MAC_TX_INTERRUPT_CTRL(base)		(base + 0x068)
#define XEC_MAC_RX_INTERRUPT_CTRL(base)		(base + 0x06C)
#define XEC_MAC_TX_INTERRUPT(base)		(base + 0x070)
#define XEC_MAC_RX_INTERRUPT(base)		(base + 0x074)
#define XEC_MAC_TX_STATUS_0(base)		(base + 0x090)
#define XEC_MAC_TX_STATUS_1(base)		(base + 0x094)
#define XEC_MAC_TX_STATUS_2(base)		(base + 0x098)
#define XEC_MAC_TX_STATUS_3(base)		(base + 0x09C)
#define XEC_MAC_RX_STATUS_0(base)		(base + 0x0A0)
#define XEC_MAC_RX_STATUS_1(base)		(base + 0x0A4)
#define XEC_MAC_RX_STATUS_2(base)		(base + 0x0A8)
#define XEC_MAC_RX_STATUS_3(base)		(base + 0x0AC)

/*
 * XEC MMC Register offsets
 */
#define XEC_MMC_CONTROL(base)			(base + 0x000)
#define XEC_MMC_TX_INTERRUPT_ENABLE(base)	(base + 0x004)
#define XEC_MMC_RX_INTERRUPT_ENABLE(base)	(base + 0x008)
#define XEC_MMC_TX_INTERRUPT(base)		(base + 0x00C)
#define XEC_MMC_RX_INTERRUPT(base)		(base + 0x010)
#define XEC_MMC_TX_CNT(base, num)		(base + 0x014 + (0x4 * (num)))
#define XEC_MMC_RX_CNT(base, num)		(base + 0x07C + (0x4 * (num)))


/*
 * XEC SWT Register offsets
 */
#define XEC_SWT_ID(base)			(base + 0x000)
#define XEC_SWT_VERSION(base)			(base + 0x004)
#define XEC_SWT_LUT_ADDR(base)			(base + 0x008)
#define XEC_SWT_LUT_ENTRY_H(base)		(base + 0x00C)
#define XEC_SWT_LUT_ENTRY_MH(base)		(base + 0x010)
#define XEC_SWT_LUT_ENTRY_ML(base)		(base + 0x014)
#define XEC_SWT_LUT_ENTRY_L(base)		(base + 0x018)


/*
 * XEC MAC register definitions
 */
#define MII_PORT_SEL_MASK			(0x3 << 0)
#define MII_PORT_SEL_MII			(0x0 << 0)
#define MII_PORT_SEL_RMII			(0x1 << 0)
#define MII_PORT_SEL_GMII			(0x2 << 0)
#define MII_PORT_SEL_RGMII			(0x3 << 0)

#define MII_SPEED_SEL_MASK			(0x3 << 2)
#define MII_SPEED_SEL_10MBPS			(0x0 << 2)
#define MII_SPEED_SEL_100MBPS			(0x1 << 2)
#define MII_SPEED_SEL_1000MBPS			(0x2 << 2)
#define MII_SPEED_SEL_2500MBPS			(0x3 << 2)

#define REF_CLK_SEL_MASK			(0x3 << 4)
#define REF_CLK_SEL_25MHZ			(0x0 << 4)
#define REF_CLK_SEL_50MHZ			(0x1 << 4)
#define REF_CLK_SEL_125MHZ			(0x2 << 4)
#define REF_CLK_SEL_312P5MHZ			(0x3 << 4)

#define HDX_MODE_MASK				(0x1 << 6)
#define HDX_MODE_FULL_DUPLEX			(0x0 << 6)
#define HDX_MODE_HALF_DUPLEX			(0x1 << 6)

#define MDC_EN_MASK					(0x1 << 15)
#define MDC_EN_ENABLE				(0x1 << 15)
#define MDC_EN_DISABLE				(0x0 << 15)

#define TX_MTU_PRG_SEL_MASK			(0x3 << 1)
#define TX_MTU_PRG_SEL_BASIC			(0x0 << 1)
#define TX_MTU_PRG_SEL_2K			(0x1 << 1)
#define TX_MTU_PRG_SEL_9K			(0x2 << 1)
#define TX_MTU_PRG_SEL_16K			(0x3 << 1)

#define TX_MTU_ENF_EN_MASK			(0x1 << 0)
#define TX_MTU_ENF_EN_ENABLE			(0x1 << 0)
#define TX_MTU_ENF_EN_DISABLE			(0x0 << 0)

#define RX_MTU_PRG_SEL_MASK			(0x3 << 9)
#define RX_MTU_PRG_SEL_BASIC			(0x0 << 9)
#define RX_MTU_PRG_SEL_2K			(0x1 << 9)
#define RX_MTU_PRG_SEL_9K			(0x2 << 9)
#define RX_MTU_PRG_SEL_16K			(0x3 << 9)

#define RX_MTU_ENF_EN_MASK			(0x1 << 8)
#define RX_MTU_ENF_EN_ENABLE			(0x1 << 8)
#define RX_MTU_ENF_EN_DISABLE			(0x0 << 8)

#define XEC_MDIO_BUSY				(0x1 << 31)

#define XEC_IOC_CH_INT_RX_PKT_INTR_EN	(0x1 << 16)
#define XEC_IOC_CH_INT_TX_PKT_INTR_EN	(0x1 << 0)
#define XEC_IOC_CH_INT_RX_PKT_INTR		(0x1 << 16)
#define XEC_IOC_CH_INT_TX_PKT_INTR		(0x1 << 0)

#define XEC_IOC_CH_INT_EN_MASK			(XEC_IOC_CH_INT_RX_PKT_INTR_EN|XEC_IOC_CH_INT_TX_PKT_INTR_EN)
#define XEC_IOC_CH_INT_EN   			(XEC_IOC_CH_INT_RX_PKT_INTR|XEC_IOC_CH_INT_TX_PKT_INTR)

#define XEC_SWT_LUT_MAXNUM				(0x3)

#define XEC_SWT_LUT_BUSY				(0x1 << 31)
#define XEC_SWT_LUT_ISSUE				(0x1 << 31)
#define XEC_SWT_LUT_OP_WRITE			(0x1 << 30)
#define XEC_SWT_LUT_ACT					(0x7 << 1)
#define XEC_SWT_LUT_CE_ENABLE			(0x1 << 0)
#define XEC_SWT_LUT_CE_DISABLE			(0x0 << 0)

#define XEC_SWT_LUT_OP_READ				(0x0 << 30)
#define XEC_SWT_LUT_ADDR_MASK			(0x3FFFFFFF)

#define XEC_TX_PKTOP_MASK				(0x14)  // CRC insert, PAD_EN
#define XEC_TX_SUMM_MASK				(0xD)   // Buffer0 Valid, First, Last Descriptor
#define XEC_RX_SUMM_MASK				(0x1)   // Buffer0 Valid

#define XEC_DESC_DINFO_NONE				(0x0)	// No HW, NXTD, INTR
#define XEC_DESC_DINFO_TX_MASK			(0x1)	// HW set
/// TODO TX maybe no need to cause interrupt
// #define XEC_DESC_DINFO_TX_MASK			(0x5)	// HW set, Interrupt enable
#define XEC_DESC_DINFO_RX_MASK			(0x5)	// HW set, Interrupt enable

#define XEC_DESC_DINFO_HWSET			(0x1)	// HW set


/* Transmit Status information  */
#define TXSTATUS_PKT_COMPLETE		(0x1 << 2)
#define TXSTATUS_DESC_FETCH_ERROR	(0x1 << 0)
#define TXSTATUS_DATA_FETCH_ERROR	(0x1 << 1)
#define TXSTATUS_ERROR				(TXSTATUS_DESC_FETCH_ERROR | TXSTATUS_DATA_FETCH_ERROR)

/* TX/RX status xmit or receive status */
#define MAC_TX_STATUS_0_TX_XMIT_ON	(0x1 << 0)
#define MAC_RX_STATUS_0_RX_RCV_ON	(0x1 << 0)

static phy_interface_t xec_phy_interface_mode(struct device *dev)
{
	phy_interface_t interface = PHY_INTERFACE_MODE_MII;
	if (dev && dev->of_node) {
		of_get_phy_mode(dev->of_node, &interface);
	}
	return interface;
}

/*
 * Structure of XEC Descriptors
 */

typedef struct xec_desc_t {
	u32 dtype_spec_0;
	u32 dtype_spec_1;
	u32 dtype_spec_2;
	u32 dtype_spec_3;
} xec_desc_t;

typedef struct xec_generic_desc_t {
	u32 dtype_spec_0;
	u32 dtype_spec_1;
	u32 dtype_spec_2;
	u32 dtype_spec_3 : 24;
	u32 dinfo : 4;
	u32 dtype : 4;
} xec_generic_desc_t;

typedef struct xec_txbuff_64_desc_t {
	u64 baddr0;
	u16 bsize0;
	u16 bsize1;
	u32 pktop : 10;
	u32 pktid : 10;
	u32 summ  : 4;
	u32 dinfo : 4;
	u32 dtype : 4;
} xec_txbuff_64_desc_t;

struct xec_txbuff_32_desc_t {
	u32 baddr0;
	u32 baddr1;
	u16 bsize0;
	u16 bsize1;
	u32 pktop : 10;
	u32 pktid : 10;
	u32 summ  : 4;
	u32 dinfo : 4;
	u32 dtype : 4;
};
typedef struct xec_txsts0_desc_t {
	u32 txsts : 12;
	u32 rsvd0 : 20;
	u32 rsvd1 : 32;
	u32 rsvd2 : 32;
	u32 rsvd3 : 12;
	u32 summ : 12;
	u32 dinfo : 4;
	u32 dtype : 4;
} xec_txsts0_desc_t;

struct xec_rxbuff_64_desc_t {
	u64 baddr0;
	u64 baddr1 : 52;
	u64 summ : 4;
	u64 dinfo : 4;
	u64 dtype : 4;
};

typedef struct xec_rxbuff_32_desc_t {
	u32 baddr0;
	u32 rsvd0;
	u32 baddr1;
	u32 rsvd1 : 20;
	u32 summ : 4;
	u32 dinfo : 4;
	u32 dtype : 4;
} xec_rxbuff_32_desc_t;

typedef union {
	u32 value;
	struct {
		u32 flenv : 1;
		u32 ferr : 1;
		u32 rut_vld : 1;
		u32 lut_rut : 3;
		u32 lut_idx : 5;
		u32 frame_error : 4;
		u32 tsv : 1;
		u32 csv : 1;
		u32 databus_err : 1;
		u32 rsvd0 : 14;
	} bits;
} xec_rxsts_t;

typedef struct xec_rxsts0_desc_t {
	u16 bsize0;
	u16 flen;
	xec_rxsts_t rxsts;
	u32 rsvd0;
	u32 pktid : 10;
	u32 rsvd1 : 2;
	u32 summ : 12;
	u32 dinfo : 4;
	u32 dtype : 4;
} xec_rxsts0_desc_t;

/*
 * Device driver data structure
 */
struct netdata_local {
	struct platform_device	*pdev;
	struct net_device	*ndev;
	struct device_node	*phy_node;
	spinlock_t		lock;
	void __iomem		*net_base;
	void __iomem		*net_mac_base;
	void __iomem		*net_ioc_base;
	void __iomem		*net_ioc_ch_base;
	void __iomem		*net_mmc_base;
	void __iomem		*net_swt_base;
	u32			msg_enable;
	unsigned int		txdesc_sz;
	unsigned int		rxdesc_sz;
	unsigned int		*skblen;
	unsigned int		last_tx_idx;
	unsigned int		num_used_tx_buffs;
	struct mii_bus		*mii_bus;
	struct clk		*clk;
	dma_addr_t		dma_buff_base_p;
	void			*dma_buff_base_v;
	size_t			dma_buff_size;
	struct xec_generic_desc_t	*tx_desc_v;
	void			*tx_buff_v;
	struct xec_generic_desc_t	*rx_desc_v;
	void			*rx_buff_v;
	int			link;
	int			speed;
	int			duplex;
	int			phymode;
	struct napi_struct	napi;
};

static u8 all_ffs[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static u8 all_zeros[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
static u8 all_bc_mac[] = {0x01, 0x80, 0xC0, 0x00, 0x00, 0x00};
static u8 all_bc_mask[] = {0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF};

static bool get_sram_for_net(struct netdata_local *pldat)
{
	u64 sram_addr, sram_sz;
	struct device *dev = &pldat->pdev->dev;
	if (dev && dev->of_node) {
		if (of_property_read_u64_index(dev->of_node, "sram", 0, &sram_addr) != 0) {
			return false;
		}
		if (of_property_read_u64_index(dev->of_node, "sram", 1, &sram_sz) != 0) {
			return false;
		}
		pldat->dma_buff_base_p = sram_addr;
		pldat->dma_buff_size = sram_sz;
		pldat->dma_buff_base_v = ioremap(sram_addr, sram_sz);
		return true;
	}
	return false;
}

/*
 * MAC support functions
 */

static void __xec_set_phy_interface_mode(struct netdata_local *pldat, phy_interface_t mode)
{
	u32 tmp = readl(XEC_MAC_CONFIGURE_0(pldat->net_mac_base));
	tmp &= ~MII_PORT_SEL_MASK;
	if (mode == PHY_INTERFACE_MODE_MII)
		tmp |= MII_PORT_SEL_MII;
	else if (mode == PHY_INTERFACE_MODE_RMII)
		tmp |= MII_PORT_SEL_RMII;
	else if (mode == PHY_INTERFACE_MODE_GMII)
		tmp |= MII_PORT_SEL_GMII;
	else if (mode == PHY_INTERFACE_MODE_RGMII)
		tmp |= MII_PORT_SEL_RGMII;
	else
		tmp |= MII_PORT_SEL_MII;

	writel(tmp, XEC_MAC_CONFIGURE_0(pldat->net_mac_base));
}

static void __xec_set_swt_lut(struct netdata_local *pldat, u32 lutaddr, u8 *mac, u8 *mask, u32 ce)
{
	u32 tmp;

	if (mac) {
		dev_dbg(&pldat->pdev->dev, "__xec_set_swt_lut: mac %x:%x:%x:%x:%x:%x\n", \
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
	if (mask) {
		dev_dbg(&pldat->pdev->dev, "__xec_set_swt_lut: mask %x:%x:%x:%x:%x:%x\n", \
			mask[0], mask[1], mask[2], mask[3], mask[4], mask[5]);
	}

	dev_dbg(&pldat->pdev->dev, "__xec_set_swt_lut: lutaddr %d, ce %d", \
			lutaddr, ce, mac);

	/* Wait for busy bit is reset */
	do {
		tmp = readl(XEC_SWT_LUT_ADDR(pldat->net_swt_base));
	} while(tmp & XEC_SWT_LUT_BUSY);

	/* set ENTRY_L */
	if (mac != NULL) {
		tmp = mac[5] | ((u32)mac[4] << 8) | ((u32)mac[3] << 16) | ((u32)mac[2] << 24);
	} else {
		tmp = 0;
	}
	writel(tmp, XEC_SWT_LUT_ENTRY_L(pldat->net_swt_base));
	/* set MASK_L and ENTRY_H */
	tmp = 0;
	if (mac != NULL) {
		tmp = (tmp & 0xFFFF0000) | (mac[1] | ((u32)mac[0] << 8));
	}
	if (mask != NULL) {
		tmp = (tmp & 0xFFFF) | ((u32)mask[5] << 16) | ((u32)mask[4] << 24);
	}
	writel(tmp, XEC_SWT_LUT_ENTRY_ML(pldat->net_swt_base));
	/* set MASK_H */
	if (mask != NULL) {
		tmp = mask[3] | ((u32)mask[2] << 8) | ((u32)mask[1] << 16) | ((u32)mask[0] << 24);
	} else {
		tmp = 0;
	}
	writel(tmp, XEC_SWT_LUT_ENTRY_MH(pldat->net_swt_base));

	/* Set CE to ce */
	writel(ce | XEC_SWT_LUT_ACT, XEC_SWT_LUT_ENTRY_H(pldat->net_swt_base));

	tmp = XEC_SWT_LUT_ISSUE | XEC_SWT_LUT_OP_WRITE | (lutaddr & XEC_SWT_LUT_ADDR_MASK);
	writel(tmp, XEC_SWT_LUT_ADDR(pldat->net_swt_base));

	/* Wait for busy bit is reset */
	do {
		tmp = readl(XEC_SWT_LUT_ADDR(pldat->net_swt_base));
	} while(tmp & XEC_SWT_LUT_BUSY);
}

static void __xec_set_swt_lut_da_only(struct netdata_local *pldat, u32 lutaddr, u8 *damac, u8 *damask)
{
	if (lutaddr >= XEC_SWT_LUT_MAXNUM) {
		dev_dbg(&pldat->pdev->dev, "__xec_set_swt_lut: can't set lut addr %d, max %d luts", \
			lutaddr, XEC_SWT_LUT_MAXNUM);
		return;
	}
	__xec_set_swt_lut(pldat, lutaddr, damac, damask, XEC_SWT_LUT_CE_DISABLE);
}

static void __xec_set_swt_lut_da_sa(struct netdata_local *pldat, u32 lutaddr, u8 *damac, u8 *damask, u8 *samac, u8 *samask)
{
	if (lutaddr > (XEC_SWT_LUT_MAXNUM)) { /* Combined mode can use extra 1 entry */
		dev_dbg(&pldat->pdev->dev, "__xec_set_swt_lut: can't set lut addr %d, max %d luts", \
			lutaddr, XEC_SWT_LUT_MAXNUM);
		return;
	}
	__xec_set_swt_lut(pldat, lutaddr, damac, damask, XEC_SWT_LUT_CE_ENABLE);
	__xec_set_swt_lut(pldat, lutaddr + 1, samac, samask, XEC_SWT_LUT_CE_DISABLE);
}

static void _xec_set_txbuff64_desc(struct xec_generic_desc_t *desc, void *bufaddr, u32 bufsz, u32 pktop, u32 pktid, u32 summ, u32 dinfo)
{
	struct xec_txbuff_64_desc_t *xec_txbuff_desc;

	if (desc == NULL)
		return;

	xec_txbuff_desc = (struct xec_txbuff_64_desc_t *)((void *)desc);

	xec_txbuff_desc->baddr0 = (u64)bufaddr;
	xec_txbuff_desc->bsize0 = bufsz;
	xec_txbuff_desc->pktop = pktop;
	xec_txbuff_desc->pktid = pktid;
	xec_txbuff_desc->summ = summ;
	xec_txbuff_desc->dinfo = dinfo;
	xec_txbuff_desc->dtype = 0x0; // TXBUFF type
}

static void _xec_dump_desc(struct netdata_local *pldat, void *desc)
{
	struct xec_desc_t *desc_t = (struct xec_desc_t *)desc;

	dev_dbg(&pldat->pdev->dev, "xec descriptor: 0x%x 0x%x 0x%x 0x%x\n", \
			desc_t->dtype_spec_0, desc_t->dtype_spec_1, desc_t->dtype_spec_2, desc_t->dtype_spec_3);
}

static void _xec_txbuff64_desc_init(struct xec_generic_desc_t *desc, void *bufaddr, u32 bufsz, u32 pktop, u32 pktid, u32 summ)
{
	_xec_set_txbuff64_desc(desc, bufaddr, bufsz, pktop, pktid, summ, XEC_DESC_DINFO_NONE);
}

static void _xec_set_rxbuff64_desc(struct xec_generic_desc_t *desc, void *bufaddr0, void *bufaddr1, u32 summ, u32 dinfo)
{
	struct xec_rxbuff_64_desc_t *xec_rxbuff_desc;

	if (desc == NULL)
		return;

	xec_rxbuff_desc = (struct xec_rxbuff_64_desc_t *)((void *)desc);

	xec_rxbuff_desc->baddr0 = (u64)bufaddr0;
	xec_rxbuff_desc->baddr1 = (u64)bufaddr1;

	xec_rxbuff_desc->summ = summ;
	xec_rxbuff_desc->dinfo = dinfo;
	xec_rxbuff_desc->dtype = 0x0; // RXBUFF type
}

static void _xec_rxbuff64_desc_init(struct xec_generic_desc_t *desc, void *bufaddr0, void *bufaddr1, u32 summ)
{
	_xec_set_rxbuff64_desc(desc, bufaddr0, bufaddr1, summ, XEC_DESC_DINFO_NONE);
}

static void _xec_desc_handover(struct xec_generic_desc_t *desc, u32 dinfo)
{
	if (desc == NULL)
		return;
	desc->dinfo = dinfo;
}

static void __xec_set_mac(struct netdata_local *pldat, u8 *mac)
{
	u32 tmp;

	/* Set station address */
	tmp = mac[0] | ((u32)mac[1] << 8) | ((u32)mac[2] << 16) | ((u32)mac[3] << 24);
	writel(tmp, XEC_MAC_ADDR_LO(pldat->net_mac_base));
	tmp = mac[4] | ((u32)mac[5] << 8);
	writel(tmp, XEC_MAC_ADDR_HI(pldat->net_mac_base));

	netdev_dbg(pldat->ndev, "Ethernet MAC address %pM\n", mac);
}

static void __xec_get_mac(struct netdata_local *pldat, u8 *mac)
{
	u32 tmp;

	/* Get station address */
	tmp = readl(XEC_MAC_ADDR_LO(pldat->net_mac_base));
	mac[0] = tmp & 0xFF;
	mac[1] = (tmp >> 8) & 0xFF;
	mac[2] = (tmp >> 16) & 0xFF;
	mac[3] = (tmp >> 24) & 0xFF;
	tmp = readl(XEC_MAC_ADDR_HI(pldat->net_mac_base));
	mac[4] = tmp & 0xFF;
	mac[5] = (tmp >> 8) & 0xFF;
}

static void __nuclei_xec_txrx_control(struct netdata_local *pldat, bool tx_enable, bool rx_enable)
{
	if (tx_enable) {
		/* Enable TX  */
		writel(0x1, XEC_IOC_CH_TX_CTRL(pldat->net_ioc_ch_base));
		// TX_CLK_EN enable, TX_XMIT_EN enable
		writel(0x5, XEC_MAC_TX_CONTROL_0(pldat->net_mac_base));
	} else {
		// TX_CLK_EN disable, TX_XMIT_EN disable
		writel(0x0, XEC_MAC_TX_CONTROL_0(pldat->net_mac_base));
		/* Disable TX  */
		writel(0x0, XEC_IOC_CH_TX_CTRL(pldat->net_ioc_ch_base));
	}
	if (rx_enable) {
		/* Enable RX  */
		writel(0x1, XEC_IOC_CH_RX_CTRL(pldat->net_ioc_ch_base));
		// RX_CLK_EN enable, RX_RCV_EN enable
		writel(0x3, XEC_MAC_RX_CONTROL_0(pldat->net_mac_base));
	} else {
		// RX_CLK_EN disable, RX_RCV_EN disable
		writel(0x0, XEC_MAC_RX_CONTROL_0(pldat->net_mac_base));
		/* Disable TX  */
		writel(0x0, XEC_IOC_CH_RX_CTRL(pldat->net_ioc_ch_base));
	}
}

static void _nuclei_xec_phy_dumpregs(struct netdata_local *pldat)
{
	struct phy_device *phydev = pldat->ndev->phydev;

	dev_dbg(&pldat->pdev->dev, "phy regs: 0x0: 0x%x, 0x1: 0x%x, 0x11: 0x%x, 0x18: 0x%x, 0x19: 0x%x, 0x1A: 0x%x", \
		phy_read(phydev, 0x0), phy_read(phydev, 0x1), phy_read(phydev, 0x11), phy_read(phydev, 0x18), \
		phy_read(phydev, 0x19), phy_read(phydev, 0x1A));
}

static void _nuclei_xec_phy_reset(struct netdata_local *pldat)
{
	struct phy_device *phydev = pldat->ndev->phydev;
	int regval;

	// Reset Phy, force to MDI mode
	regval = phy_read(phydev, 0x18);
	regval = regval | ((0x3<<8));
	phy_write(phydev, 0x18, regval);
	phy_write(phydev, 0x0, 0x1<<15);
	do {
		regval = phy_read(phydev, 0x0);
	} while(regval & (0x1<<15));
	// Wait until link ready
	do {
		regval = phy_read(phydev, 0x1A);
	} while(regval & (0x1<<2));
	_nuclei_xec_phy_dumpregs(pldat);
}

static void __xec_params_setup(struct netdata_local *pldat)
{
	u32 tmp;

	/* Set duplex, phy-mode, speed-mode */
	tmp = readl(XEC_MAC_CONFIGURE_0(pldat->net_mac_base));
	/* set duplex mode */
	tmp &= ~HDX_MODE_MASK;
	if (pldat->duplex == DUPLEX_FULL) {
		tmp |= HDX_MODE_FULL_DUPLEX;
	} else if (pldat->duplex == DUPLEX_HALF) {
		tmp |= HDX_MODE_HALF_DUPLEX;
	} else {
		tmp |= HDX_MODE_FULL_DUPLEX;
	}
	/* set phy-mode */
	tmp &= ~MII_PORT_SEL_MASK;
	if (pldat->phymode == PHY_INTERFACE_MODE_MII)
		tmp |= MII_PORT_SEL_MII;
	else if (pldat->phymode == PHY_INTERFACE_MODE_RMII)
		tmp |= MII_PORT_SEL_RMII;
	else if (pldat->phymode == PHY_INTERFACE_MODE_GMII)
		tmp |= MII_PORT_SEL_GMII;
	else if (pldat->phymode == PHY_INTERFACE_MODE_RGMII)
		tmp |= MII_PORT_SEL_RGMII;
	else
		tmp |= MII_PORT_SEL_MII;
	/* set speed mode */
	tmp &= ~MII_SPEED_SEL_MASK;
	tmp &= ~REF_CLK_SEL_MASK;
	if (pldat->speed == SPEED_100) {
		tmp |= MII_SPEED_SEL_100MBPS;
		if (pldat->phymode == PHY_INTERFACE_MODE_MII) {
			tmp |= REF_CLK_SEL_25MHZ;
		} else if (pldat->phymode == PHY_INTERFACE_MODE_RMII) {
			tmp |= REF_CLK_SEL_50MHZ;
		} else {
			tmp |= REF_CLK_SEL_25MHZ;
		}
	} else if (pldat->speed == SPEED_1000) {
		tmp |= MII_SPEED_SEL_1000MBPS;
		tmp |= REF_CLK_SEL_125MHZ;
	} else if (pldat->speed == SPEED_2500) {
		tmp |= MII_SPEED_SEL_2500MBPS;
		tmp |= REF_CLK_SEL_312P5MHZ;
	} else {
		tmp |= MII_SPEED_SEL_10MBPS;
		tmp |= REF_CLK_SEL_25MHZ;
	}
	dev_dbg(&pldat->pdev->dev, "__xec_params_setup duplex %d, phymode %d, speed %d, conf-val 0x%x\n", \
		pldat->duplex, pldat->phymode, pldat->speed, tmp);
	writel(tmp, XEC_MAC_CONFIGURE_0(pldat->net_mac_base));
	if (pldat->speed != 0) {
		/* Enable RX transfer */
		writel(0x1, XEC_IOC_CH_RX_CTRL(pldat->net_ioc_ch_base));
		writel(0x3, XEC_MAC_RX_CONTROL_0(pldat->net_mac_base));
		/* Enable TX/RX Interrupt */
		writel(XEC_IOC_CH_INT_EN_MASK, XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
	} else {
		/* Disable TX/RX Interrupt to stop transfer, don't disable tx/rx here, it might cause xec enter uncontrolled state */
		writel(0, XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
	}
}

static void __nuclei_xec_reset(struct netdata_local *pldat)
{
	/* For XEC, don't reset any mac control register if still in transfer state */
}

static int __xec_mii_mngt_reset(struct netdata_local *pldat)
{
	/* Reset MII management hardware */

	return 0;
}

static inline phys_addr_t __va_to_pa(void *addr, struct netdata_local *pldat)
{
	phys_addr_t phaddr;

	phaddr = addr - pldat->dma_buff_base_v;
	phaddr += pldat->dma_buff_base_p;

	return phaddr;
}

static inline phys_addr_t __get_tx_buff_p(struct netdata_local *pldat, u32 idx)
{
	void *va = pldat->tx_buff_v + idx * ENET_MAXF_SIZE;
	return __va_to_pa(va, pldat);
}

static inline phys_addr_t __get_rx_buff_p(struct netdata_local *pldat, u32 idx)
{
	void *va = pldat->rx_buff_v + idx * ENET_MAXF_SIZE;
	return __va_to_pa(va, pldat);
}

static void nuclei_xec_enable_int(struct netdata_local *pldat, u32 mask)
{
	u32 tmp;
	tmp = readl(XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
	tmp = tmp | (mask);
	writel(tmp, XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
}

static void nuclei_xec_disable_int(struct netdata_local *pldat, u32 mask)
{
	u32 tmp;
	tmp = readl(XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
	tmp = tmp & (~mask);
	writel(tmp, XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
}

static int nuclei_xec_safely_disable_txrx(struct netdata_local *pldat)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	while (readl(XEC_MAC_TX_STATUS_0(pldat->net_ioc_ch_base)) & MAC_TX_STATUS_0_TX_XMIT_ON) {
		cpu_relax();
		if (time_after(jiffies, timeout))
			return -1;		
	}
	writel(0x0, XEC_IOC_CH_TX_CTRL(pldat->net_ioc_ch_base));
	while (readl(XEC_MAC_RX_STATUS_0(pldat->net_ioc_ch_base)) & MAC_RX_STATUS_0_RX_RCV_ON) {
		cpu_relax();
		if (time_after(jiffies, timeout))
			return -1;		
	}
	writel(0x0, XEC_IOC_CH_RX_CTRL(pldat->net_ioc_ch_base));
	return 0;
}


/* Setup TX/RX descriptors */
static void __xec_txrx_desc_setup(struct netdata_local *pldat)
{
	void *desc_buff_v;
	int i;
	struct xec_generic_desc_t *tx_desc_t, *rx_desc_t;
	struct xec_txbuff_64_desc_t *xec_txbuff_desc;
	struct xec_rxbuff_64_desc_t *xec_rxbuff_desc;
	phys_addr_t phaddr;
	unsigned int value;

	desc_buff_v = PTR_ALIGN(pldat->dma_buff_base_v, 16);

	/* Setup TX/RX descriptor pointor */
	pldat->tx_desc_v = desc_buff_v;
	desc_buff_v += sizeof(struct xec_generic_desc_t) * pldat->txdesc_sz;

	pldat->rx_desc_v = desc_buff_v;
	desc_buff_v += sizeof(struct xec_generic_desc_t) * pldat->rxdesc_sz;

	pldat->tx_buff_v = desc_buff_v;
	pldat->rx_buff_v = desc_buff_v + ENET_MAXF_SIZE * pldat->txdesc_sz;

	dev_dbg(&pldat->pdev->dev, "__xec_txrx_desc_setup tx_desc virtual addr 0x%x, tx_buff 0x%x\n", pldat->tx_desc_v, pldat->tx_buff_v);
	dev_dbg(&pldat->pdev->dev, "__xec_txrx_desc_setup rx_desc virtual addr 0x%x, rx_buff 0x%x\n", pldat->rx_desc_v, pldat->rx_buff_v);
	dev_dbg(&pldat->pdev->dev, "__xec_txrx_desc_setup tx_desc phys addr 0x%x, tx_buff 0x%x\n", \
		__va_to_pa(pldat->tx_desc_v, pldat), __va_to_pa(pldat->tx_buff_v, pldat));
	dev_dbg(&pldat->pdev->dev, "__xec_txrx_desc_setup rx_desc phys addr 0x%x, rx_buff 0x%x\n", \
		__va_to_pa(pldat->rx_desc_v, pldat), __va_to_pa(pldat->rx_buff_v, pldat));
	/* Setup TX descriptors */
	for (i = 0; i < pldat->txdesc_sz; i ++) {
		tx_desc_t = &(pldat->tx_desc_v[i]);
		// Buffer0 valid, FD and LD set, HW NXTD INTR disabled
		_xec_set_txbuff64_desc(tx_desc_t, (void *)__get_tx_buff_p(pldat, i), 0, 0, i, XEC_TX_SUMM_MASK, XEC_DESC_DINFO_NONE);
	}

	/* Setup RX descriptors */
	for (i = 0; i < pldat->rxdesc_sz; i ++) {
		rx_desc_t = &(pldat->rx_desc_v[i]);

		// Buffer0 valid, HW NXTD INTR Enabled
		_xec_set_rxbuff64_desc(rx_desc_t, (void *)__get_rx_buff_p(pldat, i), NULL, XEC_RX_SUMM_MASK, XEC_DESC_DINFO_RX_MASK);
	}

	/* Setup base addresses in hardware to point to buffers and
	 * descriptors
	 */
	xec_txbuff_desc = (struct xec_txbuff_64_desc_t *)((void *)&(pldat->tx_desc_v[0]));
	phaddr = __va_to_pa(pldat->tx_desc_v, pldat);
	writel((u32)phaddr, XEC_IOC_CH_TX_LIST_LADDR(pldat->net_ioc_ch_base));
	writel((u32)(phaddr >> 32), XEC_IOC_CH_TX_LIST_HADDR(pldat->net_ioc_ch_base));
	xec_rxbuff_desc = (struct xec_rxbuff_64_desc_t *)((void *)&(pldat->rx_desc_v[0]));
	phaddr = __va_to_pa(pldat->rx_desc_v, pldat);
	writel((u32)(phaddr), XEC_IOC_CH_RX_LIST_LADDR(pldat->net_ioc_ch_base));
	writel((u32)(phaddr >> 32), XEC_IOC_CH_RX_LIST_HADDR(pldat->net_ioc_ch_base));

	/* Set TX/RX Config */
	writel(pldat->txdesc_sz - 1, XEC_IOC_TX_CONFIG(pldat->net_ioc_base));
	writel((pldat->rxdesc_sz - 1) | (ENET_MAXF_SIZE << 16), XEC_IOC_RX_CONFIG(pldat->net_ioc_base));

	/* Set TX/RX Head */
	value = readl(XEC_IOC_CH_TX_HEAD_POINTER(pldat->net_ioc_ch_base));
	writel(value, XEC_IOC_CH_TX_TAIL_POINTER(pldat->net_ioc_ch_base));
	value = readl(XEC_IOC_CH_RX_HEAD_POINTER(pldat->net_ioc_ch_base));
	value = (value + sizeof(struct xec_generic_desc_t) * (pldat->rxdesc_sz - 1)) % (sizeof(struct xec_generic_desc_t) * (pldat->rxdesc_sz));
	writel(value, XEC_IOC_CH_RX_TAIL_POINTER(pldat->net_ioc_ch_base));

	dev_dbg(&pldat->pdev->dev, "tx head 0x%x, tail 0x%x", \
		readl(XEC_IOC_CH_TX_HEAD_POINTER(pldat->net_ioc_ch_base)), \
		readl(XEC_IOC_CH_TX_TAIL_POINTER(pldat->net_ioc_ch_base)));
	dev_dbg(&pldat->pdev->dev, "rx head 0x%x, tail 0x%x", \
		readl(XEC_IOC_CH_RX_HEAD_POINTER(pldat->net_ioc_ch_base)), \
		readl(XEC_IOC_CH_RX_TAIL_POINTER(pldat->net_ioc_ch_base)));
}

static void __nuclei_xec_init(struct netdata_local *pldat)
{
	u32 tmp;

	dev_dbg(&pldat->pdev->dev, "__nuclei_xec_init\n");

	/* Clear interrupts and disable interrupt */
	writel(0, XEC_IOC_CH_INTERRUPT(pldat->net_ioc_ch_base));
	writel(0, XEC_IOC_CH_INTERRUPT_ENABLE(pldat->net_ioc_ch_base));
	nuclei_xec_disable_int(pldat, XEC_IOC_CH_INT_EN_MASK);
	/* set XEC_MAC_CONFIGURE_0 */
	tmp =	0x7<<18	|
			0<<16 |        // MDIO_CL45_MODE disabled
			1<<15 |        // MDC Clock Enable
			0<<8 |         //  MDC_DIV_RATIO 0
			0<<7 |         //  LOOPBACK_MODE disabled
			0<<6 |         //  HDX_MODE full
			0x0<<4 |       // REF_CLK_SEL 25MHz
			0x0<<2 |       // MII_SPEED_SEL   10M
			0x0<<0;        // MII_PORT_SEL   MII
	writel(tmp, XEC_MAC_CONFIGURE_0(pldat->net_mac_base));
	/* set XEC_MAC_TX_CONFIGURE_0 */
	tmp =	0x0<<0 |       // TX_PAUSE_EN Disabled
			0x1<<2 |       // TX_CRC_CTRL CRC Insertion
			0x1<<4 |       // TX_PAD_EN ON
			0x6<<5 |       // Tx Preamble Size 7 bytes
			0x1f<<9;      // TX_IFG_PRG_SIZE
	writel(tmp, XEC_MAC_TX_CONFIGURE_0(pldat->net_mac_base));
	/* set XEC_MAC_TX_CONFIGURE_1 */
	tmp =	0x0<<0 |       // TX_MTU_ENF_EN Disabled
			0x0<<1 |       // TX_MTU_PRG_SEL 1500
			0x0<<3 |       // TX_RETRY_EN OFF
			0x1<<4 |       // TX_MAX_RETRY 2 times
			0x0<<16;       // TX_PAUSE_TIME	0
	writel(tmp, XEC_MAC_TX_CONFIGURE_1(pldat->net_mac_base));
	/* set XEC_MAC_RX_CONFIGURE_0 */
	tmp =	0x1<<0 |       // RX_PAUSE_EN ON
			0x1<<1 |       // RX_CRC_CHECK_EN ON
			0x1<<2 |       // RX_CRC_STRIP_EN ON
			0x1<<3 |       // RX_PAD_STRIP_EN ON
			0x0<<5 |       // RX_MGK_EN OFF
			0x0<<8 |       // RX_MTU_ENF_EN OFF
			0x0<<9 |       // TX_MTU_PRG_SEL 1500
			0x0<<12;       // RX_UNI_PAUSE_EN OFF
	writel(tmp, XEC_MAC_RX_CONFIGURE_0(pldat->net_mac_base));

	/* Setup TX and RX descriptors */
	__xec_txrx_desc_setup(pldat);

	/* Setup packet filtering, enable own mac and broad cast */
	__xec_set_swt_lut_da_only(pldat, 0x0, pldat->ndev->dev_addr, all_zeros);
	__xec_set_swt_lut_da_only(pldat, 0x1, all_ffs, all_zeros);

	/* Get the next TX buffer output index */
	pldat->num_used_tx_buffs = 0;
	pldat->last_tx_idx = readl(XEC_IOC_CH_TX_HEAD_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));

	/* Disable TX/RX  */
	__nuclei_xec_txrx_control(pldat, false, false);
}

static void __nuclei_xec_shutdown(struct netdata_local *pldat)
{
	/* Just disable interrupt to disable XEC */
	/* Don't disable xec tx/rx, it might still in transfer state */
	nuclei_xec_disable_int(pldat, XEC_IOC_CH_INT_EN_MASK);
}

/*
 * MAC<--->PHY support functions
 */
static int xec_mdio_read(struct mii_bus *bus, int phy_id, int phyreg)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	int lps;
	u32 tmp;

	// dev_dbg(&pldat->pdev->dev, "xec_mdio_read id %d, reg %d\n", phy_id, phyreg);

	tmp = (phy_id << 16) | // PHYAD
		(phyreg << 0) | // REGAD
		(0x3 << 26)   | // OP READ
		(0x2 << 29)   | // PRE_SUP_SEL 32bits preamble
		(0x1 << 31);    // BUSY trigger OP start 

	writel(tmp, XEC_MAC_MDIO_CONTROL_STATUS(pldat->net_mac_base));

	/* Wait for unbusy status */
	while (readl(XEC_MAC_MDIO_CONTROL_STATUS(pldat->net_mac_base)) & XEC_MDIO_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	lps = readl(XEC_MAC_MDIO_DATA(pldat->net_mac_base));
	// dev_dbg(&pldat->pdev->dev, "xec_mdio_read id %d, reg %d, value %d\n", phy_id, phyreg, lps);

	return lps;
}

static int xec_mdio_write(struct mii_bus *bus, int phy_id, int phyreg,
			u16 phydata)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 tmp;

	dev_dbg(&pldat->pdev->dev, "xec_mdio_write id %d, reg %d, data %d\n", phy_id, phyreg, phydata);

	tmp = (phy_id << 16) | // PHYAD
		(phyreg << 0) | // REGAD
		(0x1 << 26)   | // OP WRITE
		(0x2 << 29)   | // PRE_SUP_SEL 32bits preamble
		(0x1 << 31);    // BUSY trigger OP start 

	writel(tmp, XEC_MAC_MDIO_CONTROL_STATUS(pldat->net_mac_base));
	writel(phydata, XEC_MAC_MDIO_DATA(pldat->net_mac_base));

	/* Wait for completion */
	while (readl(XEC_MAC_MDIO_CONTROL_STATUS(pldat->net_mac_base)) & XEC_MDIO_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}
	dev_dbg(&pldat->pdev->dev, "xec_mdio_write id %d, reg %d successfully\n", phy_id, phyreg);

	return 0;
}

static int xec_mdio_reset(struct mii_bus *bus)
{
	return __xec_mii_mngt_reset((struct netdata_local *)bus->priv);
}

static void xec_handle_link_change(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	unsigned long flags;

	bool status_change = false;

	spin_lock_irqsave(&pldat->lock, flags);

	if (phydev->link) {
		if ((pldat->speed != phydev->speed) ||
		    (pldat->duplex != phydev->duplex)) {
			pldat->speed = phydev->speed;
			pldat->duplex = phydev->duplex;
			status_change = true;
		}
	}

	if (phydev->link != pldat->link) {
		if (!phydev->link) {
			pldat->speed = 0;
			pldat->duplex = -1;
		}
		pldat->link = phydev->link;

		status_change = true;
	}

	spin_unlock_irqrestore(&pldat->lock, flags);
	netdev_info(ndev, "link change speed %d, duplex %d\n", pldat->speed, pldat->duplex);

	if (status_change) {
		__xec_params_setup(pldat);
	}
}

static void xec_phy_ready(struct net_device *ndev)
{
	ndev->phydev->link = 1;
	ndev->phydev->speed = 100;
	ndev->phydev->duplex = DUPLEX_FULL;
	xec_handle_link_change(ndev);
}

static int xec_mii_probe(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct phy_device *phydev;

	/* Attach to the PHY */
	netdev_info(ndev, "using %s interface, phy_node 0x%x\n", \
		phy_modes(pldat->phymode), pldat->phy_node);

	if (pldat->phy_node)
		phydev = of_phy_find_device(pldat->phy_node);
	else
		phydev = phy_find_first(pldat->mii_bus);
	if (!phydev) {
		netdev_err(ndev, "no PHY found\n");
		return -ENODEV;
	}

	phydev = phy_connect(ndev, phydev_name(phydev),
			     &xec_handle_link_change,
			     pldat->phymode);
	if (IS_ERR(phydev)) {
		netdev_err(ndev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	phy_set_max_speed(phydev, SPEED_100);

	pldat->link = 0;
	pldat->speed = 0;
	pldat->duplex = -1;

	phy_attached_info(phydev);

	return 0;
}

static int xec_mii_init(struct netdata_local *pldat)
{
	struct device_node *node;
	int err = -ENXIO;

	pldat->mii_bus = mdiobus_alloc();
	if (!pldat->mii_bus) {
		err = -ENOMEM;
		goto err_out;
	}

	pldat->mii_bus->name = "xec_mii_bus";
	pldat->mii_bus->read = &xec_mdio_read;
	pldat->mii_bus->write = &xec_mdio_write;
	pldat->mii_bus->reset = &xec_mdio_reset;
	snprintf(pldat->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pldat->pdev->name, pldat->pdev->id);
	pldat->mii_bus->priv = pldat;
	pldat->mii_bus->parent = &pldat->pdev->dev;

	node = of_get_child_by_name(pldat->pdev->dev.of_node, "mdio");
	dev_dbg(&pldat->pdev->dev, "xec_mii_init mdio node 0x%x\n", node);
	err = of_mdiobus_register(pldat->mii_bus, node);
	of_node_put(node);
	if (err)
		goto err_out_unregister_bus;

	err = xec_mii_probe(pldat->ndev);
	if (err)
		goto err_out_unregister_bus;

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(pldat->mii_bus);
	mdiobus_free(pldat->mii_bus);
err_out:
	return err;
}

static void __xec_handle_xmit(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct xec_txsts0_desc_t *p_txstat_desc;
	u32 txcidx, txstat;

	txcidx = readl(XEC_IOC_CH_TX_HEAD_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));
	dev_dbg(&pldat->pdev->dev, "__xec_handle_xmit %s: txcidx %d, last_txidx %d, used %d\n", \
		ndev->name, txcidx, pldat->last_tx_idx, pldat->num_used_tx_buffs);

	while (pldat->num_used_tx_buffs > 0) {
		unsigned int skblen = pldat->skblen[pldat->last_tx_idx];
		/* A buffer is available, get buffer status */
		p_txstat_desc = (struct xec_txsts0_desc_t *)((void *)&(pldat->tx_desc_v[pldat->last_tx_idx]));
		_xec_dump_desc(pldat, (void *)p_txstat_desc);
		/* Next buffer and decrement used buffer counter */
		if ((p_txstat_desc->dinfo & XEC_DESC_DINFO_HWSET) || (p_txstat_desc->rsvd3 != 0)) {
			dev_dbg(&pldat->pdev->dev, "tx descriptor not yet handled %s\n", ndev->name);
			break;
		} else {
			if (pldat->num_used_tx_buffs > 0) {
				pldat->num_used_tx_buffs--;
			}
			txstat = p_txstat_desc->txsts;
			dev_dbg(&pldat->pdev->dev, "tx descriptor processed %d, txcidx %d: txsts 0x%x, used %d\n", \
				pldat->last_tx_idx, txcidx, txstat, pldat->num_used_tx_buffs);
			if (txstat & TXSTATUS_ERROR) { /* error occurred */
				ndev->stats.tx_errors++;
				if (txstat & TXSTATUS_DATA_FETCH_ERROR) {
					ndev->stats.tx_carrier_errors++;
				} else if (txstat & TXSTATUS_DESC_FETCH_ERROR) {
					ndev->stats.tx_fifo_errors++;
				}
			} else {
				ndev->stats.tx_packets++;
				ndev->stats.tx_bytes += skblen;
			}
		}

		pldat->last_tx_idx++;
		if (pldat->last_tx_idx >= pldat->txdesc_sz) {
			pldat->last_tx_idx = 0;
		}
		txcidx = readl(XEC_IOC_CH_TX_HEAD_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));
		if (pldat->last_tx_idx == txcidx) {
			// break if we have go through all list
			break;
		}

		/* Update collision counter */
		// ndev->stats.collisions += TXSTATUS_COLLISIONS_GET(txstat);

		/* Any errors occurred? */
		// if (txstat & TXSTATUS_ERROR) {
		// 	if (txstat & TXSTATUS_UNDERRUN) {
		// 		/* FIFO underrun */
		// 		ndev->stats.tx_fifo_errors++;
		// 	}
		// 	if (txstat & TXSTATUS_LATECOLL) {
		// 		/* Late collision */
		// 		ndev->stats.tx_aborted_errors++;
		// 	}
		// 	if (txstat & TXSTATUS_EXCESSCOLL) {
		// 		/* Excessive collision */
		// 		ndev->stats.tx_aborted_errors++;
		// 	}
		// 	if (txstat & TXSTATUS_EXCESSDEFER) {
		// 		/* Defer limit */
		// 		ndev->stats.tx_aborted_errors++;
		// 	}
		// 	ndev->stats.tx_errors++;
		// } else {
		// 	/* Update stats */
		// 	ndev->stats.tx_packets++;
		// 	ndev->stats.tx_bytes += skblen;
		// }

	}

}

static int __xec_handle_recv(struct net_device *ndev, int budget)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct sk_buff *skb;
	u32 rxconsidx, rxprodidx, len;
	xec_rxsts_t rcvsts;
	struct xec_rxsts0_desc_t *p_rxstat_desc;
	int rx_done = 0;

	/* Get the current RX buffer indexes */
	rxprodidx = readl(XEC_IOC_CH_RX_HEAD_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));
	rxconsidx = readl(XEC_IOC_CH_RX_TAIL_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));

	rxconsidx += 1;
	if (rxconsidx >= pldat->rxdesc_sz) {
		rxconsidx = 0;
	}
	dev_dbg(&pldat->pdev->dev, "__xec_handle_recv %s start, proc %d, cons %d, budget %d\n", ndev->name, rxprodidx, rxconsidx, budget);

	while (rx_done < budget && rxconsidx != rxprodidx) {
		/* Get pointer to receive status */
		p_rxstat_desc = (struct xec_rxsts0_desc_t *)((void *)&(pldat->rx_desc_v[rxconsidx]));
		_xec_dump_desc(pldat, (void *)p_rxstat_desc);

		if ((p_rxstat_desc->dinfo & XEC_DESC_DINFO_HWSET) == 0) {
			dev_dbg(&pldat->pdev->dev, "rx descriptor %d, rxprodidx %d, received: flen %d, rxsts 0x%x, pktid %d\n", \
				rxconsidx, rxprodidx, p_rxstat_desc->flen, p_rxstat_desc->rxsts.value, p_rxstat_desc->pktid);
			len = p_rxstat_desc->flen + 1;

			/* RX Status? */
			rcvsts = p_rxstat_desc->rxsts;

			if (rcvsts.bits.flenv && rcvsts.bits.frame_error == 0) { // Frame no error
				if (rcvsts.bits.rut_vld) { /* Packet matched LUT */
				// if (1) { /* Packet matched LUT */
					dev_dbg(&pldat->pdev->dev, "recv data: matched lut %d, len %d, %*ph\n", rcvsts.bits.lut_idx, len, len, pldat->rx_buff_v + rxconsidx * ENET_MAXF_SIZE);
					/* Packet is good */
					skb = dev_alloc_skb(len);
					if (!skb) {
						ndev->stats.rx_dropped++;
					} else {
						/* Copy packet from buffer */
						skb_put_data(skb,
								pldat->rx_buff_v + rxconsidx * ENET_MAXF_SIZE,
								len);

						/* Pass to upper layer */
						skb->protocol = eth_type_trans(skb, ndev);
						netif_receive_skb(skb);
						ndev->stats.rx_packets++;
						ndev->stats.rx_bytes += len;
					}
				} else {
					dev_dbg(&pldat->pdev->dev, "recv data discard: len %d\n", len);
					ndev->stats.rx_dropped++;
				}
			} else {
				int si = rcvsts.bits.frame_error;
				/* Check statuses */
				if (si == 0x1) {
					/* Overrun error */
					ndev->stats.rx_fifo_errors++;
				} else if (si == 0x4) {
					/* CRC error */
					ndev->stats.rx_crc_errors++;
				} else if (si == 0x6) {
					/* Length error */
					ndev->stats.rx_length_errors++;
				} else {
					/* Other error */
					ndev->stats.rx_frame_errors++;
				}
				ndev->stats.rx_errors++;
			}

			/* Set rxbuff */
			_xec_set_rxbuff64_desc((struct xec_generic_desc_t *)(void *)p_rxstat_desc, \
				(void *)__get_rx_buff_p(pldat, rxconsidx), \
				NULL, XEC_RX_SUMM_MASK, XEC_DESC_DINFO_RX_MASK);
			writel(rxconsidx * (sizeof(struct xec_generic_desc_t)),
				XEC_IOC_CH_RX_TAIL_POINTER(pldat->net_ioc_ch_base));
		} else {
			dev_dbg(&pldat->pdev->dev, "rx descriptor not yet handled %s\n", ndev->name);
		}
		/* Increment consume index */
		rxconsidx = rxconsidx + 1;
		if (rxconsidx >= pldat->rxdesc_sz)
			rxconsidx = 0;
		rx_done++;
		rxprodidx = readl(XEC_IOC_CH_RX_HEAD_POINTER(pldat->net_ioc_ch_base)) / (sizeof(struct xec_generic_desc_t));
	}
	dev_dbg(&pldat->pdev->dev, "__xec_handle_recv %s end, proc %d, cons %d, done %d\n", ndev->name, rxprodidx, rxconsidx, rx_done);

	return rx_done;
}

static int nuclei_xec_poll(struct napi_struct *napi, int budget)
{
	struct netdata_local *pldat = container_of(napi,
			struct netdata_local, napi);
	struct net_device *ndev = pldat->ndev;
	int rx_done = 0;
	struct netdev_queue *txq = netdev_get_tx_queue(ndev, 0);

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_poll %s start\n", ndev->name);
	__netif_tx_lock(txq, smp_processor_id());
	__xec_handle_xmit(ndev);
	__netif_tx_unlock(txq);
	rx_done = __xec_handle_recv(ndev, budget);

	if (rx_done < budget) {
		napi_complete_done(napi, rx_done);
		nuclei_xec_enable_int(pldat, XEC_IOC_CH_INT_EN_MASK);
	}
	dev_dbg(&pldat->pdev->dev, "nuclei_xec_poll %s end\n", ndev->name);

	return rx_done;
}

static irqreturn_t __nuclei_xec_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 tmp;

	spin_lock(&pldat->lock);

	tmp = readl(XEC_IOC_CH_INTERRUPT(pldat->net_ioc_ch_base));
	/* Clear all interrupts */
	writel(0, XEC_IOC_CH_INTERRUPT(pldat->net_ioc_ch_base));
	if (tmp & XEC_IOC_CH_INT_EN_MASK) { // Received a packet
		// TODO may not need to disable interrupt
		nuclei_xec_disable_int(pldat, XEC_IOC_CH_INT_EN_MASK);
		if (likely(napi_schedule_prep(&pldat->napi))) {
			dev_dbg(&pldat->pdev->dev, "__nuclei_xec_interrupt __napi_schedule %s\n", ndev->name);
			__napi_schedule(&pldat->napi);
		} else {
			dev_dbg(&pldat->pdev->dev, "__nuclei_xec_interrupt not ready %s\n", ndev->name);
		}
	}

	spin_unlock(&pldat->lock);

	return IRQ_HANDLED;
}

static int nuclei_xec_close(struct net_device *ndev)
{
	unsigned long flags;
	struct netdata_local *pldat = netdev_priv(ndev);

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_close %s\n", ndev->name);
	if (netif_msg_ifdown(pldat))
		dev_dbg(&pldat->pdev->dev, "shutting down %s\n", ndev->name);

	napi_disable(&pldat->napi);
	netif_stop_queue(ndev);

	if (ndev->phydev)
		phy_stop(ndev->phydev);

	spin_lock_irqsave(&pldat->lock, flags);
	__nuclei_xec_reset(pldat);
	__nuclei_xec_shutdown(pldat);

	netif_carrier_off(ndev);

	spin_unlock_irqrestore(&pldat->lock, flags);

	clk_disable_unprepare(pldat->clk);

	return 0;
}

static netdev_tx_t nuclei_xec_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 len, txidx;
	struct xec_generic_desc_t *ptxdesc;

	len = skb->len;

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_hard_start_xmit %s\n", ndev->name);
	spin_lock_irq(&pldat->lock);

	_nuclei_xec_phy_dumpregs(pldat);

	/* Get the next TX descriptor index */
	txidx = readl(XEC_IOC_CH_TX_TAIL_POINTER(pldat->net_ioc_ch_base)) / sizeof(struct xec_generic_desc_t);

	/* Setup control for the transfer */
	ptxdesc = &pldat->tx_desc_v[txidx];

	/* Check whether the tx descriptor is transmitted by xec */
	if (ptxdesc->dinfo & XEC_DESC_DINFO_HWSET) {
		dev_dbg(&pldat->pdev->dev, "nuclei_xec_hard_start_xmit %s, desc %d is not yet processed\n", ndev->name, txidx);
		spin_unlock_irq(&pldat->lock);
		return NETDEV_TX_BUSY;
	}

	/* Copy data to the DMA buffer */
	memcpy(pldat->tx_buff_v + txidx * ENET_MAXF_SIZE, skb->data, len);

	/* Save the buffer and increment the buffer counter */
	pldat->skblen[txidx] = len;
	pldat->num_used_tx_buffs++;

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_hard_start_xmit idx %d, used tx buffers: %d\n", txidx, pldat->num_used_tx_buffs);
	dev_dbg(&pldat->pdev->dev, "xmit data: len %d, %*ph\n", len, len, pldat->tx_buff_v + txidx * ENET_MAXF_SIZE);
	_xec_txbuff64_desc_init(ptxdesc, (void *)__get_tx_buff_p(pldat, txidx), len - 1, XEC_TX_PKTOP_MASK, txidx, XEC_TX_SUMM_MASK);
	_xec_dump_desc(pldat, (void *)ptxdesc);

	txidx++;
	if (txidx >= pldat->txdesc_sz)
		txidx = 0;
	writel(txidx * sizeof(struct xec_generic_desc_t), XEC_IOC_CH_TX_TAIL_POINTER(pldat->net_ioc_ch_base));
	/* Start transmit */
	_xec_desc_handover(ptxdesc, XEC_DESC_DINFO_TX_MASK);

	/* For first transmit, ioc tx start need to be set, and only can be set when tx descriptors prepared */
	__nuclei_xec_txrx_control(pldat, true, true);

	spin_unlock_irq(&pldat->lock);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int xec_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	struct netdata_local *pldat = netdev_priv(ndev);
	unsigned long flags;

	dev_dbg(&pldat->pdev->dev, "xec_set_mac_address %s\n", ndev->name);
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(ndev->dev_addr, addr->sa_data, ETH_ALEN);

	spin_lock_irqsave(&pldat->lock, flags);

	/* Set station address */
	__xec_set_mac(pldat, ndev->dev_addr);

	spin_unlock_irqrestore(&pldat->lock, flags);

	return 0;
}

static void nuclei_xec_set_multicast_list(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	unsigned long flags;
	int lut_idx = 0;
	int macaddrn = 0;

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_set_multicast_list %s, flags 0x%x\n", ndev->name, ndev->flags);
	spin_lock_irqsave(&pldat->lock, flags);

	/* Count unicast and multicast macaddr numbers */
	macaddrn = netdev_uc_count(ndev) + netdev_mc_count(ndev);

	/* Set station address */
	__xec_set_mac(pldat, ndev->dev_addr);

	if ((ndev->flags & IFF_PROMISC) || (macaddrn > XEC_SWT_LUT_MAXNUM)) { /* Receive all packets */
		__xec_set_swt_lut_da_only(pldat, lut_idx, NULL, all_ffs);
		lut_idx ++;
	} else {
		/* Receive packets belongs to my own mac */
		__xec_set_swt_lut_da_only(pldat, lut_idx, ndev->dev_addr, all_zeros);
		lut_idx ++;
		if (ndev->flags & IFF_ALLMULTI) {
			__xec_set_swt_lut_da_only(pldat, lut_idx, all_bc_mac, all_bc_mask);
			lut_idx ++;
		} else {
			/* Set broadcast address lut */
			if (ndev->flags & IFF_BROADCAST) {
				__xec_set_swt_lut_da_only(pldat, lut_idx, all_ffs, all_zeros);
				lut_idx ++;
			}
			/* Set multicast address and unicast address luts */
			if (macaddrn > 0) {
				netdev_for_each_mc_addr(ha, ndev) {
					__xec_set_swt_lut_da_only(pldat, lut_idx, ha->addr, all_zeros);
					lut_idx ++;
				}
				netdev_for_each_uc_addr(ha, ndev) {
					__xec_set_swt_lut_da_only(pldat, lut_idx, ha->addr, all_zeros);
					lut_idx ++;
				}
			}
		}
	}

	/* Clear remaining lut addresses */
	while (lut_idx < XEC_SWT_LUT_MAXNUM) {
		__xec_set_swt_lut_da_only(pldat, lut_idx, NULL, NULL);
		lut_idx ++;
	}

	spin_unlock_irqrestore(&pldat->lock, flags);
}

static int nuclei_xec_open(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	int ret;

	dev_dbg(&pldat->pdev->dev, "nuclei_xec_open %s\n", ndev->name);
	if (netif_msg_ifup(pldat)) {
		dev_dbg(&pldat->pdev->dev, "enabling %s\n", ndev->name);
	}

	ret = clk_prepare_enable(pldat->clk);
	if (ret)
		return ret;

	/* Do phy soft reset */
	genphy_soft_reset(ndev->phydev);
	phy_init_hw(ndev->phydev);
	/* Suspended PHY makes XEC ethernet core block, so resume now */
	phy_resume(ndev->phydev);

	/* Reset but don't reinitialize xec */
	__nuclei_xec_reset(pldat);


	/* schedule a link state check */
	phy_start(ndev->phydev);
	netif_start_queue(ndev);
	napi_enable(&pldat->napi);

	/* Just enable interrupt to start xec */
	nuclei_xec_enable_int(pldat, XEC_IOC_CH_INT_EN_MASK);

	return 0;
}

/*
 * Ethtool ops
 */
static void nuclei_xec_ethtool_getdrvinfo(struct net_device *ndev,
	struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static u32 nuclei_xec_ethtool_getmsglevel(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	return pldat->msg_enable;
}

static void nuclei_xec_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	pldat->msg_enable = level;
}

static const struct ethtool_ops nuclei_xec_ethtool_ops = {
	.get_drvinfo	= nuclei_xec_ethtool_getdrvinfo,
	.get_msglevel	= nuclei_xec_ethtool_getmsglevel,
	.set_msglevel	= nuclei_xec_ethtool_setmsglevel,
	.get_link	= ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops xec_netdev_ops = {
	.ndo_open		= nuclei_xec_open,
	.ndo_stop		= nuclei_xec_close,
	.ndo_start_xmit		= nuclei_xec_hard_start_xmit,
	.ndo_set_rx_mode	= nuclei_xec_set_multicast_list,
	.ndo_do_ioctl		= phy_do_ioctl_running,
	.ndo_set_mac_address	= xec_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static int nuclei_xec_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct netdata_local *pldat;
	struct net_device *ndev;
	dma_addr_t dma_handle;
	struct resource *res;
	int irq, ret;

	/* Get platform resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		dev_err(dev, "error getting resources.\n");
		ret = -ENXIO;
		goto err_exit;
	}

	/* Allocate net driver data structure */
	ndev = alloc_etherdev(sizeof(struct netdata_local));
	if (!ndev) {
		dev_err(dev, "could not allocate device.\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	SET_NETDEV_DEV(ndev, dev);

	pldat = netdev_priv(ndev);

	pldat->pdev = pdev;
	pldat->ndev = ndev;

	spin_lock_init(&pldat->lock);

	/* Save resources */
	ndev->irq = irq;

	/* Get clock for the device */
	pldat->clk = clk_get(dev, NULL);
	if (IS_ERR(pldat->clk)) {
		dev_err(dev, "error getting clock.\n");
		ret = PTR_ERR(pldat->clk);
		goto err_out_free_dev;
	}

	/* Enable network clock */
	ret = clk_prepare_enable(pldat->clk);
	if (ret)
		goto err_out_clk_put;

	/* Map IO space */
	pldat->net_base = ioremap(res->start, resource_size(res));
	if (!pldat->net_base) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENOMEM;
		goto err_out_disable_clocks;
	}
	pldat->net_mmc_base = pldat->net_base + 0x0100;
	pldat->net_ioc_base = pldat->net_base + 0x0800;
	pldat->net_ioc_ch_base = pldat->net_base + 0x0820;
	pldat->net_mac_base = pldat->net_base + 0x0C00;
	pldat->net_swt_base = pldat->net_base + 0x1000;

	ret = devm_request_irq(dev, ndev->irq, __nuclei_xec_interrupt, 0,
			  ndev->name, ndev);
	if (ret) {
		dev_err(dev, "error requesting interrupt.\n");
		goto err_out_iounmap;
	}

	/* Setup driver functions */
	ndev->netdev_ops = &xec_netdev_ops;
	ndev->ethtool_ops = &nuclei_xec_ethtool_ops;
	ndev->watchdog_timeo = msecs_to_jiffies(2500);

	if (get_sram_for_net(pldat)) {
		if (PAGE_ALIGNED(pldat->dma_buff_size) != 0) {
			pldat->dma_buff_size = PAGE_ALIGN(pldat->dma_buff_size) - PAGE_SIZE;
		}

		pldat->txdesc_sz = pldat->dma_buff_size  / 2 / (ENET_MAXF_SIZE + sizeof(struct xec_generic_desc_t));
		pldat->rxdesc_sz = pldat->txdesc_sz;
		if (pldat->txdesc_sz < 2) {
			ret = -ENOMEM;
			goto err_out_free_irq;
		}
		netdev_info(ndev, "Using SRAM as TX/RX descriptor and TX/RX packet buffer\n");
	} else {
		/* Get size of DMA buffers/descriptors region */
		pldat->txdesc_sz = ENET_TX_DESC;
		pldat->rxdesc_sz = ENET_RX_DESC;

		pldat->dma_buff_size = (pldat->txdesc_sz + pldat->rxdesc_sz) * (ENET_MAXF_SIZE +
			+ sizeof(struct xec_generic_desc_t));

		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret)
			goto err_out_free_irq;

		pldat->dma_buff_size = PAGE_ALIGN(pldat->dma_buff_size);

		/* Allocate a chunk of memory for the DMA ethernet buffers
			and descriptors */
		pldat->dma_buff_base_v =
			dma_alloc_coherent(dev,
						pldat->dma_buff_size, &dma_handle,
						GFP_KERNEL);
		if (pldat->dma_buff_base_v == NULL) {
			ret = -ENOMEM;
			goto err_out_free_irq;
		}
		pldat->dma_buff_base_p = dma_handle;
		netdev_info(ndev, "Using System RAM as TX/RX descriptor and TX/RX packet buffer\n");
	}

	netdev_info(ndev, "IO address space     :%pR\n", res);
	netdev_info(ndev, "IO address size      :%zd\n",
			(size_t)resource_size(res));
	netdev_info(ndev, "IO address (mapped)  :0x%p\n",
			pldat->net_base);
	netdev_info(ndev, "IRQ number           :%d\n", ndev->irq);
	netdev_info(ndev, "DMA buffer size      :%zd\n", pldat->dma_buff_size);
	netdev_info(ndev, "DMA buffer P address :%pad\n",
			&pldat->dma_buff_base_p);
	netdev_info(ndev, "DMA buffer V address :0x%p\n",
			pldat->dma_buff_base_v);
	netdev_info(ndev, "TX Desc count : %d\n",
			pldat->txdesc_sz);
	netdev_info(ndev, "RX Desc count : %d\n",
			pldat->rxdesc_sz);

	pldat->phy_node = of_parse_phandle(np, "phy-handle", 0);
	pldat->skblen = kmalloc(sizeof(unsigned int) * pldat->txdesc_sz, GFP_KERNEL);
	if (pldat->skblen == NULL) {
		ret = -ENOMEM;
		goto err_out_free_irq;
	}

	/* Get MAC address from current HW setting (POR state is all zeros) */
	__xec_get_mac(pldat, ndev->dev_addr);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		const char *macaddr = of_get_mac_address(np);
		if (!IS_ERR(macaddr))
			ether_addr_copy(ndev->dev_addr, macaddr);
	}
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* then shut everything down to save power */
	__nuclei_xec_shutdown(pldat);

	/* Set default parameters */
	pldat->msg_enable = NETIF_MSG_LINK;

	/* Force an MII interface reset and clock setup */
	__xec_mii_mngt_reset(pldat);

	/* Force default PHY interface setup in chip, this will probably be
	   changed by the PHY driver */
	pldat->link = 0;
	pldat->speed = SPEED_100;
	pldat->duplex = DUPLEX_FULL;
	pldat->phymode = xec_phy_interface_mode(dev);
	__nuclei_xec_init(pldat);

	netif_napi_add(ndev, &pldat->napi, nuclei_xec_poll, NAPI_WEIGHT);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(dev, "Cannot register net device, aborting.\n");
		goto err_out_dma_unmap;
	}
	platform_set_drvdata(pdev, ndev);

	ret = xec_mii_init(pldat);
	if (ret)
		goto err_out_unregister_netdev;

	netdev_info(ndev, "XEC mac at 0x%08lx irq %d\n",
	       (unsigned long)res->start, ndev->irq);

	device_init_wakeup(dev, 1);
	device_set_wakeup_enable(dev, 0);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(ndev);
err_out_dma_unmap:
	dma_free_coherent(dev, pldat->dma_buff_size,
				  pldat->dma_buff_base_v,
				  pldat->dma_buff_base_p);
err_out_free_irq:
	devm_free_irq(dev, ndev->irq, ndev);
err_out_iounmap:
	iounmap(pldat->net_base);
err_out_disable_clocks:
	clk_disable_unprepare(pldat->clk);
err_out_clk_put:
	clk_put(pldat->clk);
err_out_free_dev:
	free_netdev(ndev);
err_exit:
	pr_err("%s: not found (%d).\n", MODNAME, ret);
	return ret;
}

static int nuclei_xec_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	unregister_netdev(ndev);

	dma_free_coherent(&pldat->pdev->dev, pldat->dma_buff_size,
				  pldat->dma_buff_base_v,
				  pldat->dma_buff_base_p);
	devm_free_irq(&pdev->dev, ndev->irq, ndev);
	iounmap(pldat->net_base);
	mdiobus_unregister(pldat->mii_bus);
	mdiobus_free(pldat->mii_bus);
	clk_disable_unprepare(pldat->clk);
	clk_put(pldat->clk);
	free_netdev(ndev);
	if (pldat->skblen) {
		kfree(pldat->skblen);
	}

	return 0;
}

#ifdef CONFIG_PM
/* TODO Not yet tested and developed */
static int nuclei_xec_drv_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			__nuclei_xec_shutdown(pldat);
			clk_disable_unprepare(pldat->clk);

			/*
			 * Reset again now clock is disable to be sure
			 * EMC_MDC is down
			 */
			__nuclei_xec_reset(pldat);
		}
	}

	return 0;
}

static int nuclei_xec_drv_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat;

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			pldat = netdev_priv(ndev);

			/* Enable interface clock */
			clk_enable(pldat->clk);

			/* Reset and initialize */
			__nuclei_xec_reset(pldat);
			__nuclei_xec_init(pldat);

			netif_device_attach(ndev);
		}
	}

	return 0;
}
#endif

static const struct of_device_id nuclei_xec_match[] = {
	{ .compatible = "nuclei,xec-1.0.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, nuclei_xec_match);

static struct platform_driver nuclei_xec_driver = {
	.probe		= nuclei_xec_drv_probe,
	.remove		= nuclei_xec_drv_remove,
#ifdef CONFIG_PM
	.suspend	= nuclei_xec_drv_suspend,
	.resume		= nuclei_xec_drv_resume,
#endif
	.driver		= {
		.name	= MODNAME,
		.of_match_table = nuclei_xec_match,
	},
};

module_platform_driver(nuclei_xec_driver);

MODULE_AUTHOR("Huaqi Fang <hqfang@nucleisys.com>");
MODULE_DESCRIPTION("Nuclei Ethernet Driver");
MODULE_LICENSE("GPL");
