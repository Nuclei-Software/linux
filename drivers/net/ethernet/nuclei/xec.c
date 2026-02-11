// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * based on drivers/net/ethernet/nxp/lpc_eth.c
 *
 * Copyright (C) 2023 Nuclei
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/of_reserved_mem.h>

#define MODNAME "nuclei-xec"
#define DRV_VERSION "2.00"

#define ENET_MAXF_SIZE            1536
#define ENET_RX_DESC              64
#define ENET_TX_DESC              64

#define NAPI_WEIGHT               64

#define lower32(x)                ((u32)((x) & 0xffffffff))
#define upper32(x)                ((u32)(((u64)(x) >> 32) & 0xffffffff))

#define XEC_RRD_UPDT              (1 << 31)

/* xec register define */
#define XEC_CTRL(x)               (x + 0x0)
#define XEC_IPG(x)                (x + 0x4)
#define XEC_HALF_CTRL(x)          (x + 0x8)
#define XEC_MTU(x)                (x + 0xC)
#define XEC_STAD_LO(x)            (x + 0x10)
#define XEC_STAD_HI(x)            (x + 0x14)
#define XEC_LPI(x)                (x + 0x18)
#define XEC_HASH_TAB_LO(x)        (x + 0x1C)
#define XEC_HASH_TAB_HI(x)        (x + 0x20)
#define XEC_SRAM_CTRL6(x)         (x + 0x38)
#define XEC_DESC_CTRL1(x)         (x + 0x3C)
#define XEC_DESC_CTRL2(x)         (x + 0x40)
#define XEC_DESC_CTRL3(x)         (x + 0x44)
#define XEC_DESC_CTRL4(x)         (x + 0x48)
#define XEC_DESC_CTRL5(x)         (x + 0x4C)
#define XEC_DESC_CTRL6(x)         (x + 0x50)
#define XEC_DESC_CTRL7(x)         (x + 0x54)
#define XEC_CMB_ADDR_HI(x)        (x + 0x58)
#define XEC_CMB_ADDR_LO(x)        (x + 0x5C)
#define XEC_DMA_BURST_CTRL1(x)    (x + 0x60)
#define XEC_DMA_BURST_CTRL2(x)    (x + 0x64)
#define XEC_FLOW_CTRL_WM(x)       (x + 0x68)
#define XEC_CMB_CTRL(x)           (x + 0x6C)
#define XEC_MAILBOX1(x)           (x + 0x70)
#define XEC_MAILBOX2(x)           (x + 0x74)
#define XEC_INT_STATUS(x)         (x + 0x78)
#define XEC_INT_MASK(x)           (x + 0x7C)
#define XEC_INT_TIMER2(x)         (x + 0x80)
/* RX statistics info */
#define XEC_RX_OK(x)              (x + 0x84)
#define XEC_RX_BCAST(x)           (x + 0x88)
#define XEC_RX_MCAST(x)           (x + 0x8C)
#define XEC_RX_PAUSE(x)           (x + 0x90)

/* TX statistics info */
#define XEC_TX_OK(x)              (x + 0xE0)
#define XEC_TX_BYTE_CNT(x)        (x + 0xF4)
#define XEC_TX_MULT_COL(x)        (x + 0x118)
#define XEC_TX_LATE_COL(x)        (x + 0x11C)
#define XEC_TX_ABORT_COL(x)       (x + 0x120)
#define XEC_TX_UNDERRUN(x)        (x + 0x124)
/* MDIO  */
#define XEC_MDIO_CTRL1(x)         (x + 0x138)
#define XEC_MDIO_CTRL2(x)         (x + 0x13C)
#define XEC_MDIO_STATUS(x)        (x + 0x140)
/* MISC */
#define XEC_STATUS(x)             (x + 0x150)
#define XEC_DELAY_SEL(x)          (x + 0x154)
#define XEC_SVLAN(x)              (x + 0x158)
#define XEC_IP_VERSION(x)         (x + 0x174)

#define MDIO_RD_WR                (1 << 21)
#define MDIO_START                (1 << 23)
#define MDIO_STATUS_BUSY          (1 << 16)

union xec_ctrl_reg_t {
	u32 val;
	struct {
		u32 txen :1;
		u32 rxen :1;
		u32 txfc :1;
		u32 rxfc :1;
		u32 loopback :1;
		u32 fullduplex :1;
		u32 crce :1;
		u32 flchk :1;
		u32 prlen :4;
		u32 reserved :1;
		u32 vlan_strip :1;
		u32 prom_mode :1;
		u32 tx_ip_sum_en :1;
		u32 tx_icmp_sum_en :1;
		u32 tx_udp_sum_en :1;
		u32 speed :2;
		u32 mii_mode :2;
		u32 tx_parser_en :1;
		u32 rx_chksum_en :1;
		u32 multi_all :1;
		u32 rx_hash_en :1;
		u32 broad_en :1;
		u32 debug_mode :1;
		u32 sys_clk_125 :1;
		u32 sys_clk_25 :1;
		u32 rmii_mode :1;
		u32 magic_frame_en :1;
	} bits;
};

union xec_int_reg_t {
	u32 val;
	struct {
		u32 rxfifo_of_int :1;
		u32 rfd_ur_int :1;
		u32 txf_ur_int :1;
		u32 dmar_to_int :1;
		u32 dmaw_to_int :1;
		u32 tx_pkt_int :1;
		u32 rx_pkt_int :1;
		u32 dmaw_bus_err_int :1;
		u32 dmar_bus_err_int :1;
		u32 cmb_int :1;
		u32 magic_frame_int :1;
		u32 rx_ptp_event_int :1;
		u32 tx_ptp_event_int :1;
		u32 reserved :18;
		u32 dis_int :1;
	} bits;
};

/* Transmit Package Desc , 16byte aligned*/
struct tp_desc_t {
	u32 cfg0;
	u32 cfg1;
	u32 buf_addr_lo;
	u32 buf_addr_hi;
};

/* Receive Free Desc, 8byte aligned */
struct rf_desc_t {
	u32 buf_addr_lo;
	u32 buf_addr_hi;
};

/* Receive Return Desc, 8byte aligned.
 * received packet summary information
 */
struct rr_desc_t {
	u32 status0;
	u32 status1;
};

/*
 * tx rx buf for xec send data or receive data,
 * each rx buffer maxlen is ENET_MAXF_SIZE.
 * this buffer will be set noncachable,
 * which is used to improve performance of xec when syscache enable on NUCLEI P0 SOC
 */
struct tx_rx_buf_pool {
	dma_addr_t rx_buf_p;
	void *rx_buf_v;
	dma_addr_t tx_buf_p;
	void *tx_buf_v;
	unsigned long rx_total_num;
	unsigned long rx_free_num;
	unsigned long rx_cust_num;
	unsigned long tx_total_len;
	unsigned long tx_cust_offset;
};

/*
 * Device driver data structure
 */
struct netdata_local {
	struct platform_device	*pdev;
	struct net_device	*ndev;
	struct device_node	*phy_node;
	spinlock_t			lock;
	void __iomem		*net_base;
	u32					msg_enable;
	unsigned int		last_tx_idx;
	unsigned int		num_used_tx_buffs;
	struct mii_bus		*mii_bus;
	struct clk			*clk;
	u32					desc_memtype;
	dma_addr_t			dma_buff_base_p;
	void				*dma_buff_base_v;
	size_t				dma_buff_size;
	struct tp_desc_t	*tp_desc_v;
	/* record tx sk buffer address */
	struct sk_buff		*tp_buff_v[ENET_TX_DESC];
	struct rf_desc_t	*rf_desc_v;
	/* record rx sk buffer address */
	struct sk_buff		*rf_buff_v[ENET_RX_DESC];
	struct rr_desc_t	*rr_desc_v;
	/* tx rx non-cachable buffer for xec performance when syscache enable */
	struct tx_rx_buf_pool tx_rx_nc_buf;
	/* recv data index */
	int					rx_idx;
	int					link;
	int					speed;
	int					duplex;
	struct napi_struct	napi;
};

static int init_tx_rx_buf_pool(struct netdata_local *pldat, struct device_node *np)
{
	struct device_node *node;
	struct reserved_mem *rmem;
	unsigned long rx_buf_size;

	node = of_parse_phandle(np, "txrx_buf", 0);
	if (node) {
		rmem = of_reserved_mem_lookup(node);
		of_node_put(node);
		if (!rmem) {
			dev_err(&pldat->pdev->dev, "unable to resolve desc_mem\n");
			return -EINVAL;
		}
	}

	pldat->tx_rx_nc_buf.rx_total_num = ENET_RX_DESC;
	pldat->tx_rx_nc_buf.rx_free_num = ENET_RX_DESC;
	pldat->tx_rx_nc_buf.rx_cust_num = 0;
	rx_buf_size = pldat->tx_rx_nc_buf.rx_total_num * ENET_MAXF_SIZE;
	if (rmem->size < rx_buf_size + 32*1024) {
		dev_err(&pldat->pdev->dev, "reserved size:0x%llx is less than rx tx buffer size:0x%lx\n",
			rmem->size, rx_buf_size + 32*1024);
		return -EINVAL;
	}
	pldat->tx_rx_nc_buf.rx_buf_p = (dma_addr_t)rmem->base;
	pldat->tx_rx_nc_buf.rx_buf_v = ioremap(pldat->tx_rx_nc_buf.rx_buf_p, rmem->size);
	pldat->tx_rx_nc_buf.tx_buf_p = pldat->tx_rx_nc_buf.rx_buf_p + rx_buf_size;
	pldat->tx_rx_nc_buf.tx_buf_v = pldat->tx_rx_nc_buf.rx_buf_v + rx_buf_size;
	pldat->tx_rx_nc_buf.tx_total_len = rmem->size - rx_buf_size;
	pldat->tx_rx_nc_buf.tx_cust_offset = 0;

	return 0;
}

static void put_rx_buf_to_pool(struct tx_rx_buf_pool *nc_buf)
{
	nc_buf->rx_free_num++;
}

static dma_addr_t get_rx_buf_from_pool(struct tx_rx_buf_pool *nc_buf)
{
	dma_addr_t addr = (dma_addr_t) -1;

	if (nc_buf->rx_free_num > 0) {
		addr = nc_buf->rx_buf_p + nc_buf->rx_cust_num * ENET_MAXF_SIZE;
		nc_buf->rx_cust_num += 1;
		if (nc_buf->rx_cust_num == nc_buf->rx_total_num)
			nc_buf->rx_cust_num = 0;
		nc_buf->rx_free_num--;
	} else {
		printk("%s no rx buf\n", __func__);
	}

	return addr;
}

/* Without considering data overwriting scenarios */
static void * get_tx_buf_from_pool(struct tx_rx_buf_pool *nc_buf, size_t len)
{
	void *addr;

	if (nc_buf->tx_cust_offset + len <= nc_buf->tx_total_len) {
		addr = nc_buf->tx_buf_v + nc_buf->tx_cust_offset;
		nc_buf->tx_cust_offset += len;
	} else {
		addr = nc_buf->tx_buf_v;
		nc_buf->tx_cust_offset = 0;
		nc_buf->tx_cust_offset += len;
	}

	return addr;
}

static phy_interface_t xec_phy_interface_mode(struct device *dev)
{
	if (dev && dev->of_node) {
		const char *mode = of_get_property(dev->of_node,
							"phy-mode", NULL);
		if (mode && !strcmp(mode, "gmii"))
			return PHY_INTERFACE_MODE_GMII;
		else if (mode && !strcmp(mode, "rgmii"))
			return PHY_INTERFACE_MODE_RGMII;
		else if (mode && !strcmp(mode, "mii"))
			return PHY_INTERFACE_MODE_MII;
		else if (mode && !strcmp(mode, "rmii"))
			return PHY_INTERFACE_MODE_RMII;
	}
	return PHY_INTERFACE_MODE_GMII;
}

/*
 * MAC support functions
 */
static void __xec_set_mac(struct netdata_local *pldat, const u8 *mac)
{
	u32 tmp;

	/* Set station address */
	tmp = mac[5] | ((u32)mac[4] << 8) |
		((u32)mac[3] << 16) | ((u32)mac[2] << 24);
	writel(tmp, XEC_STAD_LO(pldat->net_base));
	tmp = ((u32)mac[0] << 8) | mac[1];
	writel(tmp, XEC_STAD_HI(pldat->net_base));

	netdev_dbg(pldat->ndev, "Ethernet MAC address %pM\n", mac);
}

static void __xec_get_mac(struct netdata_local *pldat, u8 *mac)
{
	/* reserve for generate mac */
#ifdef XEC_USE_TEST_MAC
	/*fix mac addr for debug*/
	mac[0] = 0x00;
	mac[1] = 0x2b;
	mac[2] = 0x20;
	mac[3] = 0x21;
	mac[4] = 0x03;
	mac[5] = 0x23;
#endif
}

static void __xec_params_setup(struct netdata_local *pldat)
{
	union xec_ctrl_reg_t reg;

	reg.val = readl(XEC_CTRL(pldat->net_base));
	if (pldat->duplex == DUPLEX_FULL) {
		reg.bits.fullduplex = 1;
	} else {
		reg.bits.fullduplex = 0;
	}

	if (pldat->speed == SPEED_1000) {
		reg.bits.speed = 2;
		reg.bits.sys_clk_125 = 1;
		reg.bits.sys_clk_25 = 0;
	}
	else if (pldat->speed == SPEED_100) {
		reg.bits.speed = 1;
		reg.bits.sys_clk_125 = 0;
		reg.bits.sys_clk_25 = 1;
	}
	else {
		reg.bits.speed = 0;
		reg.bits.sys_clk_125 = 0;
		reg.bits.sys_clk_25 = 1;
	}

	writel(reg.val, XEC_CTRL(pldat->net_base));
}

static void __xec_eth_reset(struct netdata_local *pldat)
{
	/* Reset all MAC logic */

}

static inline phys_addr_t __va_to_pa(void *addr, struct netdata_local *pldat)
{
	phys_addr_t phaddr;

	phaddr = addr - pldat->dma_buff_base_v;
	phaddr += pldat->dma_buff_base_p;

	return phaddr;
}

static void xec_eth_enable_int(void __iomem *regbase)
{
	union xec_int_reg_t reg;

	reg.val = 0x80001FFF;
	reg.bits.tx_pkt_int = 0;
	reg.bits.rx_pkt_int = 0;
	reg.bits.dis_int = 0;
	writel(reg.val, XEC_INT_MASK(regbase));
}

static void xec_eth_disable_int(void __iomem *regbase)
{
	union xec_int_reg_t reg;

	reg.val = readl(XEC_INT_MASK(regbase));
	reg.bits.dis_int = 1;
	writel(reg.val, XEC_INT_MASK(regbase));
}

static void __xec_free_rx_skb(struct netdata_local *pldat)
{
	int i;

	for (i = 0; i < ENET_RX_DESC; i++) {
		if (pldat->rf_buff_v[i]) {
			dev_kfree_skb(pldat->rf_buff_v[i]);
			pldat->rf_buff_v[i] = NULL;
		}
	}
}

static void __xec_free_tx_skb(struct netdata_local *pldat)
{
	int i;

	for (i = 0; i < ENET_TX_DESC; i++) {
		if (pldat->tp_buff_v[i]) {
			dev_kfree_skb(pldat->tp_buff_v[i]);
			pldat->tp_buff_v[i] = NULL;
		}
	}
}

/* Setup TX/RX descriptors */
static int __xec_txrx_desc_setup(struct netdata_local *pldat)
{
	u32 val;
	void *tbuff;
	int i;
	struct tp_desc_t *ptpdesc;
	struct rf_desc_t *prfdesc;
	dma_addr_t dma_addr;

	tbuff = PTR_ALIGN(pldat->dma_buff_base_v, 16);

	/* Setup TX descriptors, status, and buffers */
	pldat->tp_desc_v = tbuff;
	tbuff += sizeof(struct tp_desc_t) * ENET_TX_DESC;

	/* Setup RX descriptors, status, and buffers */
	tbuff = PTR_ALIGN(tbuff, 16);
	pldat->rf_desc_v = tbuff;
	tbuff += sizeof(struct rf_desc_t) * ENET_RX_DESC;

	tbuff = PTR_ALIGN(tbuff, 16);
	pldat->rr_desc_v = tbuff;
	tbuff += sizeof(struct rr_desc_t) * ENET_RX_DESC;

	/* Map the TX descriptors to the TX buffers in hardware */
	for (i = 0; i < ENET_TX_DESC; i++) {
		ptpdesc = &pldat->tp_desc_v[i];
		ptpdesc->buf_addr_lo = 0;
		ptpdesc->buf_addr_hi = 0;
		ptpdesc->cfg0 = 0;
		ptpdesc->cfg1 = 0;
	}

	/* Map the RX descriptors to the RX buffers in hardware */
	for (i = 0; i < ENET_RX_DESC; i++) {
		pldat->rf_buff_v[i] = NULL;

		prfdesc = &pldat->rf_desc_v[i];
		//dma_addr = dma_map_single(pldat->ndev->dev.parent,
		//		skb->data, ENET_MAXF_SIZE, DMA_TO_DEVICE);
		#if 0
		dma_addr = __pa(skb->data);
		#else
		dma_addr = get_rx_buf_from_pool(&pldat->tx_rx_nc_buf);
		#endif
		prfdesc->buf_addr_lo = lower32(dma_addr);
		prfdesc->buf_addr_hi = upper32(dma_addr);

		/* clear rr desc */
		memset(&pldat->rr_desc_v[i], 0, sizeof(struct rr_desc_t));
	}

	/* Setup base addresses in hardware to point to buffers and
	 * descriptors
	 */
	writel(lower32(__va_to_pa(pldat->tp_desc_v, pldat)),
		XEC_DESC_CTRL7(pldat->net_base));
	writel(upper32(__va_to_pa(pldat->tp_desc_v, pldat)),
		XEC_DESC_CTRL6(pldat->net_base));
	writel(ENET_TX_DESC << 16,
		XEC_DESC_CTRL5(pldat->net_base));
	writel(lower32(__va_to_pa(pldat->rf_desc_v, pldat)),
		XEC_DESC_CTRL2(pldat->net_base));
	writel(upper32(__va_to_pa(pldat->rf_desc_v, pldat)),
		XEC_DESC_CTRL1(pldat->net_base));
	writel(lower32(__va_to_pa(pldat->rr_desc_v, pldat)),
		XEC_DESC_CTRL3(pldat->net_base));
	val = ENET_RX_DESC & 0xFFF;
	val |= ENET_MAXF_SIZE << 16;
	writel(val, XEC_DESC_CTRL4(pldat->net_base));

	return 0;
}

static void __xec_eth_init(struct netdata_local *pldat)
{
	union xec_ctrl_reg_t ctrl_reg;
	u32 val;

	/* evalsoc rgmii 100M negative edge sample */
	val = readl(XEC_IPG(pldat->net_base));
	val |= BIT(17) | BIT(18);
	writel(val, XEC_IPG(pldat->net_base));

	ctrl_reg.val = readl(XEC_CTRL(pldat->net_base));
	ctrl_reg.bits.txen = 0;
	ctrl_reg.bits.rxen = 0;

	/* MAC Init Configure */
	/* disable loopback */
	ctrl_reg.bits.loopback = 0;
	/* full duplex mode */
	ctrl_reg.bits.fullduplex = 1;
	/* enable CRC*/
	ctrl_reg.bits.crce = 1;
	/* frame length check */
	ctrl_reg.bits.flchk = 0;
	/* Preamble length, 0x07 standard defination */
	ctrl_reg.bits.prlen = 7;
	/* remove VLAN Tag automatically for the Rx packets*/
	ctrl_reg.bits.vlan_strip = 1;
	ctrl_reg.bits.prom_mode = 0;
	/* speed select 100M */
	ctrl_reg.bits.speed = 1;
	ctrl_reg.bits.tx_parser_en = 0;
	/* Rx checksum enable */
	ctrl_reg.bits.rx_chksum_en = 1;
	/* Multicast address is legal */
	ctrl_reg.bits.multi_all = 0;
	/* Multicast address filter disable */
	ctrl_reg.bits.rx_hash_en = 0;

	/* receive boardcast frame enable */
	ctrl_reg.bits.broad_en = 1;
	ctrl_reg.bits.debug_mode = 0;
	ctrl_reg.bits.magic_frame_en = 0;

	writel(ctrl_reg.val, XEC_CTRL(pldat->net_base));

	/* set max frame length by bytes */
	writel(ENET_MAXF_SIZE, XEC_MTU(pldat->net_base));

	__xec_params_setup(pldat);

	/* Setup TX and RX descriptors */
	__xec_txrx_desc_setup(pldat);

	/* Get the next TX buffer output index */
	pldat->num_used_tx_buffs = 0;
	pldat->last_tx_idx =
		readl(XEC_MAILBOX2(pldat->net_base)) >> 16;

	/*
	 * init recv data idx , which indicate
	 * the real read pointer of the RX DESC ARRAY
	 */
	pldat->rx_idx = 0;

	/* Clear and enable interrupts */
	xec_eth_enable_int(pldat->net_base);

	/* Enable controller */
	ctrl_reg.val = readl(XEC_CTRL(pldat->net_base));
	ctrl_reg.bits.txen = 1;
	ctrl_reg.bits.rxen = 1;
	writel(ctrl_reg.val, XEC_CTRL(pldat->net_base));

	/* reset SRAM */
	writel(1, XEC_SRAM_CTRL6(pldat->net_base));

	/* prepare half RX DESC number to hardware when init */
	writel(ENET_RX_DESC >> 1, XEC_MAILBOX1(pldat->net_base));
}

static void __xec_eth_shutdown(struct netdata_local *pldat)
{
	/* Reset ethernet and power down PHY */
	__xec_eth_reset(pldat);
}

/*
 * MAC<--->PHY support functions
 */
static int xec_mdio_read(struct mii_bus *bus, int phy_id, int phyreg)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 val;

	val = readl(XEC_MDIO_CTRL2(pldat->net_base));
	val &= ~(0x1F << 26);
	val |= phy_id << 26;
	writel(val, XEC_MDIO_CTRL2(pldat->net_base));

	val = readl(XEC_MDIO_CTRL1(pldat->net_base));
	/* clear MDIO_DATA, MDIO_REG_ADDR, MDIO_RD_WR */
	val &= ~0x3FFFFF;
	val |= phyreg << 16;
	val |= MDIO_RD_WR;/* mdio read operation */
	val |= MDIO_START;/* start mdio */
	writel(val, XEC_MDIO_CTRL1(pldat->net_base));

	/* Wait for unbusy status */
	while ((val = readl(XEC_MDIO_STATUS(pldat->net_base))) & MDIO_STATUS_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return val & 0xFFFF;
}

static int xec_mdio_write(struct mii_bus *bus, int phy_id, int phyreg,
			u16 phydata)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 val;

	val = readl(XEC_MDIO_CTRL2(pldat->net_base));
	val &= ~(0x1F << 26);
	val |= phy_id << 26;
	writel(val, XEC_MDIO_CTRL2(pldat->net_base));

	/* start mdio */
	val = readl(XEC_MDIO_CTRL1(pldat->net_base));
	/* clear MDIO_DATA, MDIO_REG_ADDR, MDIO_RD_WR */
	val &= ~0x3FFFFF;
	val |= phyreg << 16;
	val |= phydata; /* mdio write data */
	val |= MDIO_START; /* start mdio */
	writel(val, XEC_MDIO_CTRL1(pldat->net_base));

	/* Wait for unbusy status */
	while (readl(XEC_MDIO_STATUS(pldat->net_base)) & MDIO_STATUS_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return 0;
}

static int xec_mdio_reset(struct mii_bus *bus)
{
	int val;

	xec_mdio_write(bus, 0, MII_BMCR, BMCR_RESET);
	do {
		val = xec_mdio_read(bus, 0, MII_BMCR);
	} while (val & BMCR_RESET);

	return 0;
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

	if (status_change)
		__xec_params_setup(pldat);
}

static int xec_mii_probe(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct phy_device *phydev;

	/* Attach to the PHY */
	if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_GMII)
		netdev_info(ndev, "using GMII interface\n");
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_RGMII)
		netdev_info(ndev, "using RGMII interface\n");
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_MII)
		netdev_info(ndev, "using MII interface\n");
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_RMII)
		netdev_info(ndev, "using RMII interface\n");
	else
		netdev_info(ndev, "using RGMII interface\n");

	if (pldat->phy_node)
		phydev =  of_phy_find_device(pldat->phy_node);
	else
		phydev = phy_find_first(pldat->mii_bus);
	if (!phydev) {
		netdev_err(ndev, "no PHY found\n");
		return -ENODEV;
	}
	phydev = phy_connect(ndev, phydev_name(phydev),
				&xec_handle_link_change,
				xec_phy_interface_mode(&pldat->pdev->dev));
	if (IS_ERR(phydev)) {
		netdev_err(ndev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	phy_set_max_speed(phydev, SPEED_1000);

	pldat->link = 0;
	pldat->speed = 0;
	pldat->duplex = -1;

	phy_attached_info(phydev);

	return 0;
}

static int xec_mii_init(struct netdata_local *pldat)
{
	struct device_node *node;
	union xec_ctrl_reg_t reg;
	int err = -ENXIO;
	int val;

	pldat->mii_bus = mdiobus_alloc();
	if (!pldat->mii_bus) {
		err = -ENOMEM;
		goto err_out;
	}
	reg.val = readl(XEC_CTRL(pldat->net_base));

	/* Setup MII mode */
	if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_GMII)
		reg.bits.mii_mode = 0;
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_RGMII)
		reg.bits.mii_mode = 1;
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_MII)
		reg.bits.mii_mode = 2;
	else if (xec_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_RMII)
		reg.bits.mii_mode = 3;
	else
		reg.bits.mii_mode = 0;

	writel(reg.val, XEC_CTRL(pldat->net_base));

	val = readl(XEC_MDIO_CTRL1(pldat->net_base));
	/* MDIO CLK SEL to 32DIV*/
	val &= ~(0x7 << 24);
	val |= 0x4 << 24;
	writel(val, XEC_MDIO_CTRL1(pldat->net_base));

	pldat->mii_bus->name = "xec_mii_bus";
	pldat->mii_bus->read = &xec_mdio_read;
	pldat->mii_bus->write = &xec_mdio_write;
	pldat->mii_bus->reset = &xec_mdio_reset;
	snprintf(pldat->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pldat->pdev->name, pldat->pdev->id);
	pldat->mii_bus->priv = pldat;
	pldat->mii_bus->parent = &pldat->pdev->dev;

	node = of_get_child_by_name(pldat->pdev->dev.of_node, "mdio");
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
	u32 txcidx;
	struct sk_buff *skbptr;

	txcidx = readl(XEC_MAILBOX2(pldat->net_base)) >> 16;
	while (pldat->last_tx_idx != txcidx) {
		skbptr = pldat->tp_buff_v[pldat->last_tx_idx];
		dev_kfree_skb(skbptr);
		pldat->tp_buff_v[pldat->last_tx_idx] = NULL;
		/* Next buffer and decrement used buffer counter */
		pldat->num_used_tx_buffs--;
		pldat->last_tx_idx++;
		if (pldat->last_tx_idx >= ENET_TX_DESC)
			pldat->last_tx_idx = 0;

		/* Update collision counter */
		ndev->stats.collisions += readl(XEC_TX_ABORT_COL(pldat->net_base)) & 0xFFFFFF;
		ndev->stats.tx_fifo_errors += readl(XEC_TX_UNDERRUN(pldat->net_base));
		ndev->stats.tx_aborted_errors += readl(XEC_TX_LATE_COL(pldat->net_base));
		ndev->stats.tx_aborted_errors += readl(XEC_TX_MULT_COL(pldat->net_base));
		ndev->stats.tx_packets += readl(XEC_TX_OK(pldat->net_base));
		ndev->stats.tx_bytes += readl(XEC_TX_BYTE_CNT(pldat->net_base));

		txcidx = readl(XEC_MAILBOX2(pldat->net_base)) >> 16;
	}

	if (pldat->num_used_tx_buffs <= ENET_TX_DESC/2) {
		if (netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
	}
}

static int __xec_handle_recv(struct net_device *ndev, int budget)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct sk_buff *new_skb;
	u32 rxconsidx, len, ethst;
	int rx_done = 0;
	u32 rxprodidx;
	struct rr_desc_t *prxstat;
	u32 tmp;

	/* Get the current RRD indexes */
	prxstat = &pldat->rr_desc_v[pldat->rx_idx];
	/*
	 * Maybe RRD is in external stroage, fence ops to assure
	 * RRD value is vaild before using.
	 */
	rmb();
	while (rx_done < budget && (prxstat->status1 & XEC_RRD_UPDT)) {
		len = (prxstat->status1 >> 16) & 0x3FFF;
		ethst = (prxstat->status0 >> 24) & 0x3;

		if (ethst) {
			ndev->stats.rx_errors++;
		} else {
			/* Packet is good */
			new_skb = netdev_alloc_skb(ndev, ENET_MAXF_SIZE);
			if (!new_skb) {
				ndev->stats.rx_dropped++;
			} else {
				dma_addr_t dma_addr;
				void* rx_data_addr;

				rx_data_addr = (void *)(((dma_addr_t)(pldat->rf_desc_v[pldat->rx_idx].buf_addr_hi) << 32) |
						pldat->rf_desc_v[pldat->rx_idx].buf_addr_lo);
				rx_data_addr = (dma_addr_t)rx_data_addr - pldat->tx_rx_nc_buf.rx_buf_p + pldat->tx_rx_nc_buf.rx_buf_v;
				/* Pass to upper layer */
				skb_put_data(new_skb, rx_data_addr, len - ETH_FCS_LEN);
				put_rx_buf_to_pool(&pldat->tx_rx_nc_buf);
				new_skb->protocol = eth_type_trans(new_skb, ndev);
				netif_receive_skb(new_skb);
				ndev->stats.rx_packets++;
				ndev->stats.rx_bytes += len;
				/* put new rx buf into descriptor */
				dma_addr = get_rx_buf_from_pool(&pldat->tx_rx_nc_buf);
				pldat->rf_desc_v[pldat->rx_idx].buf_addr_lo = lower32(dma_addr);
				pldat->rf_desc_v[pldat->rx_idx].buf_addr_hi = upper32(dma_addr);
			}
		}
		/* set UPDT to zero indicate hardware can use this RRD,RFD */
		prxstat->status1 &= ~XEC_RRD_UPDT;
		wmb();

		rx_done++;
		/* update product index */
		tmp = readl(XEC_MAILBOX1(pldat->net_base));
		rxprodidx = tmp & 0xFFF;
		rxconsidx = (tmp >> 15) & 0xFFF;

		if (rxprodidx + 1 >= ENET_RX_DESC) {
			if (rxconsidx > 0)
				rxprodidx = 0;
		} else {
			if ((rxprodidx + 1) != rxconsidx)
				rxprodidx++ ;
		}
		writel(rxprodidx, XEC_MAILBOX1(pldat->net_base));
		/* update receive data index */
		pldat->rx_idx++;
		if (pldat->rx_idx >= ENET_RX_DESC)
			pldat->rx_idx = 0;
		prxstat = &pldat->rr_desc_v[pldat->rx_idx];
		rmb();
	}

	return rx_done;
}

static int xec_eth_poll(struct napi_struct *napi, int budget)
{
	struct netdata_local *pldat = container_of(napi,
			struct netdata_local, napi);
	struct net_device *ndev = pldat->ndev;
	int rx_done = 0;
	struct netdev_queue *txq = netdev_get_tx_queue(ndev, 0);

	__netif_tx_lock(txq, smp_processor_id());
	__xec_handle_xmit(ndev);
	__netif_tx_unlock(txq);
	rx_done = __xec_handle_recv(ndev, budget);

	if (rx_done < budget) {
		napi_complete_done(napi, rx_done);
		xec_eth_enable_int(pldat->net_base);
	}

	return rx_done;
}

static irqreturn_t __xec_eth_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 val;

	spin_lock(&pldat->lock);

	val = readl(XEC_INT_STATUS(pldat->net_base));
	/* Clear interrupts */
	writel(val, XEC_INT_STATUS(pldat->net_base));

	xec_eth_disable_int(pldat->net_base);
	if (likely(napi_schedule_prep(&pldat->napi)))
		__napi_schedule(&pldat->napi);

	spin_unlock(&pldat->lock);

	return IRQ_HANDLED;
}

static int xec_eth_close(struct net_device *ndev)
{
	unsigned long flags;
	struct netdata_local *pldat = netdev_priv(ndev);

	if (netif_msg_ifdown(pldat))
		dev_dbg(&pldat->pdev->dev, "shutting down %s\n", ndev->name);

	napi_disable(&pldat->napi);
	netif_stop_queue(ndev);

	spin_lock_irqsave(&pldat->lock, flags);
	__xec_eth_reset(pldat);
	netif_carrier_off(ndev);
	__xec_free_rx_skb(pldat);
	__xec_free_tx_skb(pldat);
	spin_unlock_irqrestore(&pldat->lock, flags);

	if (ndev->phydev)
		phy_stop(ndev->phydev);
	clk_disable_unprepare(pldat->clk);

	return 0;
}

static netdev_tx_t xec_eth_hard_start_xmit(struct sk_buff *skb,
						struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 txidx;
	struct tp_desc_t *txdesc;
	u64 txbuf_addr;

	spin_lock_irq(&pldat->lock);

	if (pldat->num_used_tx_buffs >= (ENET_TX_DESC - 1)) {
		/* This function should never be called when there are no
		   buffers */
		netif_stop_queue(ndev);
		spin_unlock_irq(&pldat->lock);
		WARN(1, "BUG! TX request when no free TX buffers!\n");
		return NETDEV_TX_BUSY;
	}

	/* Get the next TX descriptor index */
	txidx = readl(XEC_MAILBOX2(pldat->net_base)) & 0xFFFF;
	pldat->tp_buff_v[txidx] = skb;
	/* Setup control for the transfer */
	txdesc = &pldat->tp_desc_v[txidx];

	/*flush skb buffer, fill skb buffer address to TX DESC */
	#if 0
	txbuf_addr =  dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	#else
	void *txbuf_addr_va = get_tx_buf_from_pool(&pldat->tx_rx_nc_buf, skb->len);
	memcpy(txbuf_addr_va, skb->data, skb->len);
	txbuf_addr = txbuf_addr_va - pldat->tx_rx_nc_buf.tx_buf_v + pldat->tx_rx_nc_buf.tx_buf_p;
	#endif
	txdesc->cfg0 = skb->len & 0xFFFF;
	txdesc->cfg1 = 1 << 31;
	txdesc->buf_addr_lo = lower32(txbuf_addr);
	txdesc->buf_addr_hi = upper32(txbuf_addr);
	wmb();
	/* increment the buffer counter */
	pldat->num_used_tx_buffs++;

	/* Start transmit */
	txidx++;
	if (txidx >= ENET_TX_DESC)
		txidx = 0;
	writel(txidx, XEC_MAILBOX2(pldat->net_base));

	/* Stop queue if no more TX buffers */
	if (pldat->num_used_tx_buffs >= (ENET_TX_DESC - 1))
		netif_stop_queue(ndev);

	spin_unlock_irq(&pldat->lock);

	return NETDEV_TX_OK;
}

static int xec_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	struct netdata_local *pldat = netdev_priv(ndev);
	unsigned long flags;

	if (netif_running(ndev))
		return -EBUSY;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	eth_hw_addr_set(ndev, addr->sa_data);

	spin_lock_irqsave(&pldat->lock, flags);

	/* Set station address */
	__xec_set_mac(pldat, ndev->dev_addr);

	spin_unlock_irqrestore(&pldat->lock, flags);

	return 0;
}

static void xec_eth_set_multicast_list(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct netdev_hw_addr_list *mcptr = &ndev->mc;
	struct netdev_hw_addr *ha;
	u32 hash_val, hashlo, hashhi;
	unsigned long flags;
	union xec_ctrl_reg_t ctrl_reg;

	spin_lock_irqsave(&pldat->lock, flags);

	/* Set station address */
	__xec_set_mac(pldat, ndev->dev_addr);

	ctrl_reg.val = readl(XEC_CTRL(pldat->net_base));
	ctrl_reg.bits.broad_en = 1;

	if (ndev->flags & IFF_PROMISC)
		ctrl_reg.bits.prom_mode = 1;
	if (ndev->flags & IFF_ALLMULTI)
		ctrl_reg.bits.multi_all = 1;

	if (netdev_hw_addr_list_count(mcptr))
		ctrl_reg.bits.rx_hash_en = 1;

	writel(ctrl_reg.val, XEC_CTRL(pldat->net_base));

	/* Set initial hash table */
	hashlo = 0x0;
	hashhi = 0x0;

	/* 64 bits : multicast address in hash table */
	netdev_hw_addr_list_for_each(ha, mcptr) {
		hash_val = (ether_crc(6, ha->addr) >> 23) & 0x3F;

		if (hash_val >= 32)
			hashhi |= 1 << (hash_val - 32);
		else
			hashlo |= 1 << hash_val;
	}

	writel(hashlo, XEC_HASH_TAB_LO(pldat->net_base));
	writel(hashhi, XEC_HASH_TAB_HI(pldat->net_base));

	spin_unlock_irqrestore(&pldat->lock, flags);
}

static int xec_eth_open(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	int ret;

	if (netif_msg_ifup(pldat))
		dev_dbg(&pldat->pdev->dev, "enabling %s\n", ndev->name);

	ret = clk_prepare_enable(pldat->clk);
	if (ret)
		return ret;

	phy_resume(ndev->phydev);

	/* Reset and initialize */
	__xec_eth_reset(pldat);

	__xec_set_mac(pldat, ndev->dev_addr);
	__xec_eth_init(pldat);

	/* schedule a link state check */
	phy_start(ndev->phydev);
	netif_start_queue(ndev);
	napi_enable(&pldat->napi);

	return 0;
}

/*
 * Ethtool ops
 */
static void xec_eth_ethtool_getdrvinfo(struct net_device *ndev,
	struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static u32 xec_eth_ethtool_getmsglevel(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	return pldat->msg_enable;
}

static void xec_eth_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	pldat->msg_enable = level;
}

static const struct ethtool_ops xec_eth_ethtool_ops = {
	.get_drvinfo	= xec_eth_ethtool_getdrvinfo,
	.get_msglevel	= xec_eth_ethtool_getmsglevel,
	.set_msglevel	= xec_eth_ethtool_setmsglevel,
	.get_link	= ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops xec_netdev_ops = {
	.ndo_open		= xec_eth_open,
	.ndo_stop		= xec_eth_close,
	.ndo_start_xmit		= xec_eth_hard_start_xmit,
	.ndo_set_rx_mode	= xec_eth_set_multicast_list,
	.ndo_do_ioctl		= phy_do_ioctl_running,
	.ndo_set_mac_address	= xec_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static int xec_eth_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct netdata_local *pldat;
	struct net_device *ndev;
	struct resource *res;
	u8 addr[ETH_ALEN];
	int irq, ret;
	struct device_node *node;
	struct reserved_mem *rmem;

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
	ret = request_irq(ndev->irq, __xec_eth_interrupt, 0,
			  ndev->name, ndev);
	if (ret) {
		dev_err(dev, "error requesting interrupt.\n");
		goto err_out_iounmap;
	}

	/* Setup driver functions */
	ndev->netdev_ops = &xec_netdev_ops;
	ndev->ethtool_ops = &xec_eth_ethtool_ops;

	/* Get size of DMA descriptors region */
	pldat->dma_buff_size = 
		ENET_TX_DESC * (sizeof(struct tp_desc_t)) +
		ENET_RX_DESC * (sizeof(struct rf_desc_t) + sizeof(struct rr_desc_t));

	/* Allocate a chunk of non-cachable memory for the descriptors */
	node = of_parse_phandle(np, "desc_mem", 0);
	if (node) {
		rmem = of_reserved_mem_lookup(node);
		of_node_put(node);
		if (!rmem) {
			dev_err(dev, "unable to resolve desc_mem\n");
			return -EINVAL;
		}
		if (pldat->dma_buff_size > rmem->size) {
			dev_err(dev, "reserved size:0x%llx is less than desc size:0x%lx\n",
				rmem->size, pldat->dma_buff_size);
			return -EINVAL;
		}
		pldat->dma_buff_base_p = (dma_addr_t)rmem->base;
		pldat->dma_buff_base_v = ioremap(pldat->dma_buff_base_p, rmem->size);
		if (pldat->dma_buff_base_v == NULL) {
			ret = -ENOMEM;
			goto err_out_free_irq;
		}
		pldat->desc_memtype = 1;
	} else {
		dma_addr_t dma_handle;

		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret)
			goto err_out_free_irq;

		pldat->dma_buff_size = PAGE_ALIGN(pldat->dma_buff_size);
		/* Allocate a chunk of memory for the DMA ethernet buffers
		   and descriptors */
		pldat->dma_buff_base_v = dma_alloc_coherent(dev,
						   pldat->dma_buff_size, &dma_handle,
						   GFP_KERNEL);
		if (pldat->dma_buff_base_v == NULL) {
			ret = -ENOMEM;
			goto err_out_free_irq;
		}
		pldat->desc_memtype = 0;
		pldat->dma_buff_base_p = dma_handle;
	}
	netdev_dbg(ndev, "IO address space     :%pR\n", res);
	netdev_dbg(ndev, "IO address size      :%zd\n",
			(size_t)resource_size(res));
	netdev_dbg(ndev, "IO address (mapped)  :0x%p\n",
			pldat->net_base);
	netdev_dbg(ndev, "IRQ number           :%d\n", ndev->irq);
	netdev_dbg(ndev, "DMA buffer size      :%zd\n", pldat->dma_buff_size);
	netdev_dbg(ndev, "DMA buffer P address :%pad\n",
			&pldat->dma_buff_base_p);
	netdev_dbg(ndev, "DMA buffer V address :0x%p\n",
			pldat->dma_buff_base_v);

	pldat->rx_idx = 0;

	/* allocate non-cachable tx rx buffer for improving xec performance when syscache enabled */
	init_tx_rx_buf_pool(pldat, np);

	pldat->phy_node = of_parse_phandle(np, "phy-handle", 0);

	/* Get MAC address from current HW setting (POR state is all zeros) */
	__xec_get_mac(pldat, addr);
	eth_hw_addr_set(ndev, addr);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		of_get_ethdev_address(np, ndev);
	}
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* Set default parameters */
	pldat->msg_enable = NETIF_MSG_LINK;

	/* Force default PHY interface setup in chip, this will probably be
	   changed by the PHY driver */
	pldat->link = 0;
	pldat->speed = SPEED_1000;
	pldat->duplex = DUPLEX_FULL;

	netif_napi_add_weight(ndev, &pldat->napi, xec_eth_poll, NAPI_WEIGHT);

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
	if (pldat->desc_memtype)
		iounmap(pldat->dma_buff_base_v);
	else
		dma_free_coherent(dev, pldat->dma_buff_size,
					pldat->dma_buff_base_v,
					pldat->dma_buff_base_p);
err_out_free_irq:
	free_irq(ndev->irq, ndev);
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

static int xec_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (pldat->desc_memtype)
		iounmap(pldat->dma_buff_base_v);
	else
		dma_free_coherent(&pldat->pdev->dev, pldat->dma_buff_size,
					pldat->dma_buff_base_v,
					pldat->dma_buff_base_p);
	free_irq(ndev->irq, ndev);
	iounmap(pldat->net_base);
	mdiobus_unregister(pldat->mii_bus);
	mdiobus_free(pldat->mii_bus);
	clk_disable_unprepare(pldat->clk);
	clk_put(pldat->clk);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int xec_eth_drv_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			__xec_eth_shutdown(pldat);
			clk_disable_unprepare(pldat->clk);

			/*
			 * Reset again now clock is disable to be sure
			 * EMC_MDC is down
			 */
			__xec_eth_reset(pldat);
		}
	}

	return 0;
}

static int xec_eth_drv_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat;
	int ret;

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			pldat = netdev_priv(ndev);

			/* Enable interface clock */
			ret = clk_enable(pldat->clk);
			if (ret)
				return ret;

			/* Reset and initialize */
			__xec_eth_reset(pldat);
			__xec_eth_init(pldat);

			netif_device_attach(ndev);
		}
	}

	return 0;
}
#endif

static const struct of_device_id xec_eth_match[] = {
	{ .compatible = "nuclei,xec" },
	{ }
};
MODULE_DEVICE_TABLE(of, xec_eth_match);

static struct platform_driver xec_eth_driver = {
	.probe		= xec_eth_drv_probe,
	.remove		= xec_eth_drv_remove,
#ifdef CONFIG_PM
	.suspend	= xec_eth_drv_suspend,
	.resume		= xec_eth_drv_resume,
#endif
	.driver		= {
		.name	= MODNAME,
		.of_match_table = xec_eth_match,
	},
};

module_platform_driver(xec_eth_driver);

MODULE_DESCRIPTION("NUCLEI XEC Ethernet Driver");
MODULE_LICENSE("GPL");
