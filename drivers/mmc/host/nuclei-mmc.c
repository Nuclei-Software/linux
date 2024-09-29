// SPDX-License-Identifier: GPL-2.0-only
/*
 * based on sunplus-mmc.c
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/dma-map-ops.h>

#define SDIO_MIN_CLK			400000
#define SDIO_MAX_CLK			50000000
#define SDIO_MAX_BLK_COUNT		65536
#define SDIO_TIMEOUT_US			500000
#define SDIO_POLL_DELAY_US		20

#define SDIO_DMA_TRANSF_MAXLEN	((1024-64)*1024)

#define SDIO_DMA_MODE 0
#define	SDIO_PIO_MODE 1

/* Registers */
#define SDIO_RX_SADDR				0x0
#define SDIO_RX_SIZE				0x4
#define SDIO_RX_CFG					0x8
#define SDIO_CR						0xC
#define SDIO_TX_SADDR				0x10
#define SDIO_TX_SIZE				0x14
#define SDIO_TX_CFG					0x18
#define SDIO_CMD_OP					0x20
#define SDIO_CMD_ARG				0x24
#define SDIO_DATA_SETUP				0x28
#define SDIO_START					0x2C
#define SDIO_RSP0					0x30
#define SDIO_RSP1					0x34
#define SDIO_RSP2					0x38
#define SDIO_RSP3					0x3C
#define SDIO_CLK_DIV				0x40
#define SDIO_STATUS					0x44
#define SDIO_STOP_CMD_OP			0x48
#define SDIO_STOP_CMD_ARG			0x4C
#define SDIO_DATA_TIMEOUT_CNT		0x50
#define SDIO_CMD_WAIT_RSP_CNT		0x58
#define SDIO_TX_DATA				0x60
#define SDIO_RX_DATA				0x64
#define	SDIO_TX_MARK				0x68
#define	SDIO_RX_MARK				0x6C
#define SDIO_IP						0x70
#define SDIO_IE						0x74
#define SDIO_SAMPLE_DDR				0x78

#define SDIO_DMA_INTR_EN			0x1C00
#define SDIO_DMA_INTR_STAT			0x1C04
#define SDIO_DMA_INTR_CLR			0x1C08

#define SDIO_DMA_INT_EN_RX_FTRANS	BIT(0)
#define SDIO_DMA_INT_EN_TX_FTRANS	BIT(3)
#define SDIO_DMA_INT_STAT_RX_FTRANS	BIT(0)
#define SDIO_DMA_INT_STAT_TX_FTRANS	BIT(3)
#define SDIO_DMA_INT_CLR_RX_FTRANS	BIT(0)
#define SDIO_DMA_INT_CLR_TX_FTRANS	BIT(3)

#define SDIO_RXFIFO_EMPTY			BIT(2)
#define SDIO_TXFIFO_FULL			BIT(3)

#define SDIO_DMA_TX_RX_EN			BIT(4)
#define SDIO_DMA_DATASIZE_MASK		GENMASK(2,1)
#define SDIO_DMA_DATASIZE_WORD		(2<<1)

#define SDIO_CR_DMA_EN				BIT(0)
#define SDIO_CR_CLK_STOP			BIT(5)
#define SDIO_CR_DATA_CRC_EN			BIT(7)

#define SDIO_IE_EOT					BIT(2)
#define SDIO_IE_ERR					BIT(3)
#define SDIO_IE_TX_UDF				BIT(4)
#define SDIO_IE_TX_OVF				BIT(5)
#define SDIO_IE_RX_UDF				BIT(6)
#define SDIO_IE_RX_OVF				BIT(7)


#define SDIO_STATUS_EOT				BIT(0)
#define SDIO_STATUS_ERR				BIT(1)
#define SDIO_STATUS_TXUDR_ERR		BIT(2)
#define SDIO_STATUS_TXOVF_ERR		BIT(3)
#define SDIO_STATUS_RXUDR_ERR		BIT(4)
#define SDIO_STATUS_RXOVF_ERR		BIT(5)
#define SDIO_STATUS_BUSY			BIT(6)
#define SDIO_STATUS_CLR_FIFO		BIT(7)

#define SDIO_STATUS_CMDERR_RSP_TO	BIT(16)
#define SDIO_STATUS_CMDERR_WrongDir	BIT(17)
#define SDIO_STATUS_CMDERR_BUSY_TO	BIT(18)
#define SDIO_STATUS_CMDERR_CRC		BIT(19)
#define SDIO_STATUS_CMDERR_TO		(SDIO_STATUS_CMDERR_RSP_TO | SDIO_STATUS_CMDERR_BUSY_TO)

#define SDIO_STATUS_DATAERR_RSP_TO	BIT(24)
#define SDIO_STATUS_DATAERR_BUSY_TO	BIT(25)
#define SDIO_STATUS_DATAERR_TO		GENMASK(25,24)
#define SDIO_STATUS_DATAERR_CRC		BIT(26)


#define SDIO_CMD_OP_CRC_EN 			BIT(2)
#define SDIO_CMD_OP_POWER_EN	 	BIT(4)
#define SDIO_CMD_OP_CRC_CHECK_EN 	BIT(5)
#define SDIO_DATA_SETUP_EN			BIT(0)
#define SDIO_DATA_SETUP_RD			BIT(1)
#define SDIO_DATA_SETUP_MODE		GENMASK(3,2)
#define SDIO_DATA_SETUP_BLK_NUM		GENMASK(19, 4)
#define SDIO_DATA_SETUP_BLK_SIZE	GENMASK(31, 20)

#define NUCLEI_MMC_BUS_WIDTH_MASK	0x3
#define NUCLEI_MMC_BUS_WIDTH_1		0x0
#define NUCLEI_MMC_BUS_WIDTH_4		0x1
#define NUCLEI_MMC_BUS_WIDTH_8		0x2


struct nclmmc_host {
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	struct mmc_host *mmc;
	struct mmc_request *mrq; /* current mrq */
	int irq;
	int dmapio_mode;
	int dma_use_int;
	int	bus_width;
	int cur_clk;
	//uint32_t total_bytes_left;
	uint32_t transfer_blks;
	uint32_t blksz;
	uint32_t status;
	//struct scatterlist	*cur_sg;
};

static inline int nclmmc_wait_finish(struct nclmmc_host *host, u32 *status)
{
	u32 state;
	int ret;

	//printk("w%d\n", host->mrq->cmd->opcode);
	ret = readl_poll_timeout(host->base + SDIO_STATUS, state,
					(state & SDIO_STATUS_EOT),
					SDIO_POLL_DELAY_US, SDIO_TIMEOUT_US);

	if (!ret) {
		if (status)
			*status = state;
		writel(state, host->base + SDIO_STATUS);
	} else {
		printk(KERN_ERR"pollret:%x time out:%x\n", ret, state);
	}
	return ret;
}

static void nclmmc_get_rsp(struct nclmmc_host *host, struct mmc_command *cmd)
{
	if (!(cmd->flags & MMC_RSP_PRESENT))
		return;
	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = readl(host->base + SDIO_RSP3);
		cmd->resp[1] = readl(host->base + SDIO_RSP2);
		cmd->resp[2] = readl(host->base + SDIO_RSP1);
		cmd->resp[3] = readl(host->base + SDIO_RSP0);
	} else {
		cmd->resp[0] = readl(host->base + SDIO_RSP0);
	}
}

static void nclmmc_set_bus_clk(struct nclmmc_host *host, int clk)
{
	unsigned int clk_div;
	u32 clk_src;

	if (clk > 0 && host->cur_clk != clk) {
		clk_src = clk_get_rate(host->clk);
		clk_div = clk_src / clk;
		printk(KERN_ERR"clk_src:%d clk:%d,clk_div:%d\n",clk_src, clk, clk_div);
		if (clk_src % clk)
			clk_div++;
		writel((clk_div >> 1) - 1, host->base + SDIO_CLK_DIV);
		host->cur_clk = clk;
	}
}

static void nclmmc_set_bus_width(struct nclmmc_host *host, int width)
{
	switch (width) {
	case MMC_BUS_WIDTH_8:
		host->bus_width = NUCLEI_MMC_BUS_WIDTH_8;
		break;
	case MMC_BUS_WIDTH_4:
		host->bus_width = NUCLEI_MMC_BUS_WIDTH_4;
		break;
	default:
		host->bus_width = NUCLEI_MMC_BUS_WIDTH_1;
		break;
	}
}

static void nclmmc_prepare_cmd(struct nclmmc_host *host, struct mmc_command *cmd)
{
	u32 value;

	writel(cmd->arg, host->base + SDIO_CMD_ARG);

	value = cmd->opcode << 8;
	value |= mmc_resp_type(cmd) & 0xf;
	//printk(KERN_ERR"cmd op:%x,arg:%x\n", value, cmd->arg);
	writel(value, host->base + SDIO_CMD_OP);

	writel(0, host->base + SDIO_DATA_SETUP);
}

static void nclmmc_prepare_data(struct nclmmc_host *host, struct mmc_data *data)
{
	u32 val;
	int blksz_bits;
	dma_addr_t dma_addr;
	u32 dma_len;

	if (!data)
		return;

	blksz_bits = ffs(data->blksz) - 1;
	BUG_ON(1 << blksz_bits != data->blksz);

	val = SDIO_DATA_SETUP_EN;
	if (data->flags & MMC_DATA_READ)
		val |= SDIO_DATA_SETUP_RD;
	val |= (host->bus_width & NUCLEI_MMC_BUS_WIDTH_MASK) << 2;

	host->dmapio_mode = SDIO_PIO_MODE;
	
	if ((data->blksz * data->blocks >= 1024)) {
		u32 reg;
		int count;

		host->dmapio_mode = SDIO_DMA_MODE;

		count = dma_map_sg(host->mmc->parent, data->sg, data->sg_len,
			   mmc_get_dma_dir(data));
		if (!count) {
			dev_err(mmc_dev(host->mmc),
				"Failed to map scatterlist for DMA operation\n");
			return;
		}
		//host->total_bytes_left = data->blocks * data->blksz;
		//host->cur_sg = data->sg;
		//dma_addr = sg_dma_address(host->cur_sg);
		//dma_len = sg_dma_len(host->cur_sg);
		dma_addr = sg_dma_address(data->sg);
		dma_len = sg_dma_len(data->sg);
		if (dma_len >  SDIO_DMA_TRANSF_MAXLEN) {
			dev_err(mmc_dev(host->mmc),
				"sg length %x more than SDIO_DMA_TRANSF_MAXLEN:%x\n", 
				dma_len, SDIO_DMA_TRANSF_MAXLEN);
			return;
		}

		if (data->flags & MMC_DATA_READ){
			/* here parepare first sg buffer to DMA */
			/* Config RX DMA */
			writel(dma_addr, host->base + SDIO_RX_SADDR);
			writel(dma_len, host->base + SDIO_RX_SIZE);
			/* Enable RX DMA */
			reg = readl(host->base + SDIO_RX_CFG);
			reg &= ~SDIO_DMA_DATASIZE_MASK;
			reg |= SDIO_DMA_DATASIZE_WORD | SDIO_DMA_TX_RX_EN;
			writel(reg, host->base  + SDIO_RX_CFG);
		} else if (data->flags & MMC_DATA_WRITE) {
			/* here parepare first sg buffer to DMA */
			/* Config TX DMA */
		    writel(dma_addr, host->base + SDIO_TX_SADDR);
			writel(dma_len, host->base + SDIO_TX_SIZE);
			/* Enable TX DMA */
			reg = readl(host->base + SDIO_TX_CFG);
			reg &= ~SDIO_DMA_DATASIZE_MASK;
			reg |= SDIO_DMA_DATASIZE_WORD | SDIO_DMA_TX_RX_EN;
			writel(reg, host->base  + SDIO_TX_CFG);
		}
		/* Enable DMA Mode*/
		reg = readl(host->base + SDIO_CR);
		reg |= SDIO_CR_DMA_EN;
		writel(reg, host->base + SDIO_CR);
		host->dma_use_int = 1;

		writel(SDIO_IE_EOT | SDIO_IE_ERR | SDIO_IE_TX_OVF |
			SDIO_IE_TX_UDF | SDIO_IE_RX_UDF | SDIO_IE_RX_OVF, 
			host->base + SDIO_IE);

		host->transfer_blks = dma_len/data->blksz;
		host->blksz = data->blksz;
		//host->total_bytes_left -= dma_len;
		val |= ((host->transfer_blks - 1) << 4) & SDIO_DATA_SETUP_BLK_NUM;
	} else {
		val |= ((data->blocks-1) << 4) & SDIO_DATA_SETUP_BLK_NUM;
	}
	val |= ((data->blksz-1) << 20) & SDIO_DATA_SETUP_BLK_SIZE;
	writel(val, host->base + SDIO_DATA_SETUP);
	//printk(KERN_ERR"M%d addr:%x len:%x-%x,ds:%x\n", host->dmapio_mode, dma_addr,dma_len, data->blocks*data->blksz, val);
}

static inline void nclmmc_trigger_transaction(struct nclmmc_host *host)
{
	writel(1, host->base + SDIO_START);
}

static void nclmmc_send_stop_cmd(struct nclmmc_host *host)
{
	struct mmc_command stop = {};

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1;
	nclmmc_prepare_cmd(host, &stop);
	nclmmc_trigger_transaction(host);
	nclmmc_wait_finish(host, NULL);
}

static int nclmmc_check_error(struct nclmmc_host *host, struct mmc_request *mrq, u32 status)
{
	int ret = 0;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;

	if (status & SDIO_STATUS_ERR) {
		printk(KERN_ERR"cmd%d err status:0x%x\n",cmd->opcode, status);
		printk(KERN_ERR"op:%x,args:%x,ds:%x\n",readl(host->base + SDIO_CMD_OP), readl(host->base + SDIO_CMD_ARG),
				readl(host->base + SDIO_DATA_SETUP));
		ret = -ETIMEDOUT;
			
		if (status & (SDIO_STATUS_CMDERR_WrongDir |
			SDIO_STATUS_CMDERR_CRC | SDIO_STATUS_DATAERR_CRC)) {
			ret = -ECOMM;
		}
		cmd->error = ret;
		if (data) {
			data->error = ret;
			data->bytes_xfered = 0;
		}
	} else if (status & (SDIO_STATUS_TXUDR_ERR |
	   SDIO_STATUS_TXOVF_ERR | SDIO_STATUS_RXUDR_ERR 
	   | SDIO_STATUS_RXOVF_ERR)){
		printk(KERN_ERR"cmd%d OVF/UDF status:0x%x\n",cmd->opcode, status);
		data->error = -ECOMM;
	} else if (data) {
		data->error = 0;
		data->bytes_xfered = data->blocks * data->blksz;
	} else
		cmd->error = 0;

	return ret;
}

void dump_data(char *buf, int len)
{
#if 0
	int i;

	for(i = 0; i < len; i++) {
		printk(KERN_CONT "%02x ", buf[i]);
		if ((i+1)%16 ==0)
			printk("\n");
	}
#endif
}

static void nclmmc_xfer_data_pio(struct nclmmc_host *host, struct mmc_data *data)
{
	u32 *buf;
	int data_left = data->blocks * data->blksz;
	int consumed, remain;
	u32 reg;
	static int idx;

	struct sg_mapping_iter sg_miter;
	unsigned int flags = 0;


	if (data->flags & MMC_DATA_WRITE)
		flags |= SG_MITER_FROM_SG;
	else
		flags |= SG_MITER_TO_SG;
	//printk("len:%x\n",data->sg_len);
	sg_miter_start(&sg_miter, data->sg, data->sg_len, flags);
	//printk("dlen:0x%x,sta:0x%x,ip:0x%x\n",data_left, readl(host->base + SDIO_STATUS),
	//	readl(host->base + SDIO_IP));
	idx = 0;
	while (data_left > 0) {
		consumed = 0;
		if (!sg_miter_next(&sg_miter))
			break;
		buf = sg_miter.addr;
		remain = sg_miter.length;
		//printk("remain:0x%x\n", remain);
		do {
			if (data->flags & MMC_DATA_WRITE) {
				reg = readl(host->base + SDIO_IP);
				if (!(reg & SDIO_TXFIFO_FULL)) {
					//printk("t%d\n",idx++);
					idx++;
					writel(*buf, host->base + SDIO_TX_DATA);
				} else {
					//printk("full%d,reg:%x\n",idx,reg);
					udelay(20);
					continue;
				}
			} else {
				reg = readl(host->base + SDIO_IP);
				if (!(reg & SDIO_RXFIFO_EMPTY)) {
					*buf = readl(host->base + SDIO_RX_DATA);
				} else {
					udelay(20);
					continue;
				}
			}
			buf++;
			/* tx/rx 4 bytes one time in pio mode */
			consumed += 4;
			remain -= 4;
		} while (remain > 0);
		sg_miter.consumed = consumed;
		data_left -= consumed;
	}

	#if 0
	if (sg_miter.length > 0) {
		printk("cpu dump first %x:\n", sg_miter.length);
		dump_data(sg_miter.addr, sg_miter.length);
	}
	#endif
	sg_miter_stop(&sg_miter);
}

static void nclmmc_controller_init(struct nclmmc_host *host)
{
	int ret = reset_control_assert(host->rstc);

	if (!ret) {
		usleep_range(1000, 1250);
		ret = reset_control_deassert(host->rstc);
	}

	writel(0xffffffff, host->base + SDIO_DATA_TIMEOUT_CNT);
	writel(0xffff, host->base + SDIO_CMD_WAIT_RSP_CNT);

	writel(0, host->base + SDIO_IE);

	//ret = readl(host->base + SDIO_CR);
	//ret &= ~SDIO_CR_CLK_STOP;
	//writel(ret, host->base + SDIO_CR);

	writel(SDIO_DMA_INT_EN_RX_FTRANS | SDIO_DMA_INT_EN_TX_FTRANS, 
		host->base + SDIO_DMA_INTR_EN);
}

/*
 * 1. unmap scatterlist if needed;
 * 2. get response & check error conditions;
 * 3. notify mmc layer the request is done
 */
static void nclmmc_finish_request(struct nclmmc_host *host, struct mmc_request *mrq, u32 status)
{
	struct mmc_command *cmd;
	struct mmc_data *data;
	//u32 cmdop;
	//u32 cmdarg;
	u32 val;

	if (!mrq)
		return;

	cmd = mrq->cmd;
	data = mrq->data;

	if (data && (SDIO_DMA_MODE == host->dmapio_mode) && host->dma_use_int) {
		if (data->flags & MMC_DATA_READ) {
			while(!(readl(host->base + SDIO_DMA_INTR_STAT) & SDIO_DMA_INT_STAT_RX_FTRANS));
			val = readl(host->base + SDIO_RX_CFG);
			if (val & BIT(4))
				printk(KERN_ERR"rx cfg:%x\n", val);
			//udelay(1000);
			writel(SDIO_DMA_INT_CLR_RX_FTRANS, host->base + SDIO_DMA_INTR_CLR);
		} else {
			if (readl(host->base + SDIO_DMA_INTR_STAT) & SDIO_DMA_INT_STAT_TX_FTRANS)
				writel(SDIO_DMA_INT_CLR_TX_FTRANS, host->base + SDIO_DMA_INTR_CLR);
		}
		dma_unmap_sg(host->mmc->parent, data->sg, data->sg_len, mmc_get_dma_dir(data));
	}

	nclmmc_get_rsp(host, cmd);
	nclmmc_check_error(host, mrq, status);
	if (mrq->stop) {
		/* stop cmd use poll mode, avoid recursion*/
		val = readl(host->base + SDIO_IE);
		writel(0 , host->base + SDIO_IE);
		nclmmc_send_stop_cmd(host);
		writel(val , host->base + SDIO_IE);
	}
#if 0
	/* continue to transfer */
	if (data && host->total_bytes_left) {
		dma_addr_t dma_addr;
		u32 dma_len;
	
		host->cur_sg = sg_next(host->cur_sg);
		dma_addr = sg_dma_address(host->cur_sg);
		dma_len = sg_dma_len(host->cur_sg);
		host->total_bytes_left -= dma_len;
		printk(KERN_ERR"cur dma addr:%x len:%x left:%x\n", dma_addr, dma_len, host->total_bytes_left);
		/* prepare next dma tranfer data */
		writel(cmdop, host->base + SDIO_CMD_OP);
		cmdarg += host->transfer_blks * host->blksz / 512;
		writel(cmdarg, host->base + SDIO_CMD_ARG);

		val = SDIO_DATA_SETUP_EN;
		if (data->flags & MMC_DATA_READ)
			val |= SDIO_DATA_SETUP_RD;
		val |= (host->bus_width & NUCLEI_MMC_BUS_WIDTH_MASK) << 2;
		
		host->transfer_blks = dma_len/data->blksz;
		host->blksz = data->blksz;
		val |= ((host->transfer_blks - 1) << 4) & SDIO_DATA_SETUP_BLK_NUM;
		val |= ((data->blksz-1) << 20) & SDIO_DATA_SETUP_BLK_SIZE;
		writel(val, host->base + SDIO_DATA_SETUP);

		if (data->flags & MMC_DATA_READ){
			/* here parepare first sg buffer to DMA */
			/* Config RX DMA */
		    writel(dma_addr, host->base + SDIO_RX_SADDR);
			writel(dma_len, host->base + SDIO_RX_SIZE);
			
			/* Enable RX DMA */
			val = readl(host->base + SDIO_RX_CFG);
			val &= ~SDIO_DMA_DATASIZE_MASK;
			val |= SDIO_DMA_DATASIZE_WORD | SDIO_DMA_TX_RX_EN;
			writel(val, host->base  + SDIO_RX_CFG);
		} else if (data->flags & MMC_DATA_WRITE) {
			/* here parepare first sg buffer to DMA */
			/* Config TX DMA */
		    writel(dma_addr, host->base + SDIO_TX_SADDR);
			writel(dma_len, host->base + SDIO_TX_SIZE);
			
			/* Enable TX DMA */
			val = readl(host->base + SDIO_TX_CFG);
			val &= ~SDIO_DMA_DATASIZE_MASK;
			val |= SDIO_DMA_DATASIZE_WORD | SDIO_DMA_TX_RX_EN;
			writel(val, host->base  + SDIO_TX_CFG);
		}
		writel(SDIO_IE_EOT, host->base  + SDIO_IE);
		
		/* trigger dma again */
		nclmmc_trigger_transaction(host);
	} else 
#endif
	{

		if (data && host->dma_use_int) {
//			printk("dma dump first %x:\n", data->blksz * data->blocks);
			dump_data(phys_to_virt(data->sg->dma_address), data->blksz * data->blocks);
			host->dma_use_int = 0;
			val = readl(host->base + SDIO_CR);
			val &= ~SDIO_CR_DMA_EN;
			writel(val, host->base + SDIO_CR);
		}

		host->mrq = NULL;
		mmc_request_done(host->mmc, mrq);
	}
}

/* Interrupt Service Routine */
static irqreturn_t nclmmc_irq(int irq, void *dev_id)
{
	struct nclmmc_host * host = (struct nclmmc_host *)dev_id;
	u32 status;

	status = readl(host->base + SDIO_STATUS);

	if (status & SDIO_STATUS_EOT) {
		host->status = status;
		writel(status, host->base + SDIO_STATUS);
		writel(0, host->base + SDIO_IE);

		return IRQ_WAKE_THREAD;
	} else {
		if (status & SDIO_STATUS_ERR)
			printk(KERN_ERR "sdio err\n");
		else if (status & SDIO_STATUS_TXOVF_ERR)
			printk(KERN_ERR "sdio tx overflow\n");
		else if (status & SDIO_STATUS_TXUDR_ERR)
			printk(KERN_ERR "sdio tx underflow\n");
		else if (status & SDIO_STATUS_RXOVF_ERR)
			printk(KERN_ERR "sdio rx overflow\n");
		else if (status & SDIO_STATUS_RXUDR_ERR)
			printk(KERN_ERR "sdio rx underflow\n");
		else
			printk(KERN_ERR "sdio err irq:%d\n", irq);
	}

	return IRQ_HANDLED;
}

static void nclmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct nclmmc_host *host = mmc_priv(mmc);
	struct mmc_data *data;
	struct mmc_command *cmd;
	u32 status=0;

	host->mrq = mrq;
	data = mrq->data;
	cmd = mrq->cmd;

	nclmmc_prepare_cmd(host, cmd);
	/* we need manually read response R2. */
	if (cmd->flags & MMC_RSP_136) {
		nclmmc_trigger_transaction(host);
		nclmmc_wait_finish(host, &status);
		nclmmc_get_rsp(host, cmd);
		nclmmc_check_error(host, mrq, status);
		host->mrq = NULL;
		mmc_request_done(host->mmc, mrq);
	} else {
		if (data)
			nclmmc_prepare_data(host, data);

		if (host->dmapio_mode == SDIO_PIO_MODE && data) {
			/* pio data transfer do not use interrupt */
			writel(0, host->base + SDIO_IE);
			nclmmc_trigger_transaction(host);
			nclmmc_xfer_data_pio(host, data);
			nclmmc_wait_finish(host, &status);
			nclmmc_finish_request(host, mrq, status);
		} else {
			if (host->dma_use_int) {
				nclmmc_trigger_transaction(host);
			} else {
				nclmmc_trigger_transaction(host);
				nclmmc_wait_finish(host, &status);
				//printk(KERN_ERR"poll wait\n");
				nclmmc_finish_request(host, mrq, status);
			}
		}
	}
}

static void nclmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct nclmmc_host *host = (struct nclmmc_host *)mmc_priv(mmc);

	nclmmc_set_bus_clk(host, ios->clock);
	nclmmc_set_bus_width(host, ios->bus_width);
}

static const struct mmc_host_ops nclmmc_ops = {
	.request = nclmmc_request,
	.set_ios = nclmmc_set_ios,
};

static irqreturn_t nclmmc_func_irq_worker(int irq, void *dev_id)
{
	struct nclmmc_host *host = dev_id;

	nclmmc_finish_request(host, host->mrq, host->status);

	return IRQ_HANDLED;
}

static int nclmmc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct resource *res;
	struct nclmmc_host *host;
	int ret = 0;

	mmc = devm_mmc_alloc_host(&pdev->dev, sizeof(struct nclmmc_host));
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dmapio_mode = SDIO_DMA_MODE;//SDIO_PIO_MODE;
	//host->dma_int_threshold = 1024;

	host->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->clk = devm_clk_get(&pdev->dev, "sdio_data_clk");
	if (IS_ERR(host->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->clk), "clk get fail\n");

	host->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(host->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->rstc), "rst get fail\n");
#if 1
	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	ret = devm_request_threaded_irq(&pdev->dev, host->irq,
					nclmmc_irq, nclmmc_func_irq_worker, IRQF_SHARED,
			NULL, host);
	if (ret)
		return ret;
#endif
	ret = clk_prepare_enable(host->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable clk\n");

	ret = mmc_of_parse(mmc);
	if (ret)
		goto clk_disable;

	mmc->ops = &nclmmc_ops;
	mmc->f_min = SDIO_MIN_CLK;
	if (mmc->f_max > SDIO_MAX_CLK)
		mmc->f_max = SDIO_MAX_CLK;

	if (!mmc->ocr_avail)
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->max_req_size = SDIO_MAX_BLK_COUNT * 512;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = SDIO_MAX_BLK_COUNT;
	mmc->max_seg_size = SDIO_DMA_TRANSF_MAXLEN;
	dev_set_drvdata(&pdev->dev, host);
	nclmmc_controller_init(host);

	ret = mmc_add_host(mmc);
	if (ret)
		goto clk_disable;

	return 0;

clk_disable:
	clk_disable_unprepare(host->clk);
	return ret;
}

static void nclmmc_drv_remove(struct platform_device *dev)
{
	struct nclmmc_host *host = platform_get_drvdata(dev);

	mmc_remove_host(host->mmc);
	clk_disable_unprepare(host->clk);
}

static const struct of_device_id nclmmc_of_table[] = {
	{
		.compatible = "nuclei,mmc",
	},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, nclmmc_of_table);

static struct platform_driver nclmmc_driver = {
	.probe = nclmmc_drv_probe,
	.remove_new = nclmmc_drv_remove,
	.driver = {
		.name = "nuclei-mmc",
		.of_match_table = nclmmc_of_table,
	},
};
module_platform_driver(nclmmc_driver);

MODULE_DESCRIPTION("Nuclei MMC controller driver");
MODULE_LICENSE("GPL");
