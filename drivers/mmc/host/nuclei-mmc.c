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

#define SDIO_DMA_MODE			0
#define SDIO_PIO_MODE			1

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
#define SDIO_CMD_WAIT_EOT_CNT		0x5C
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
#define SDIO_CR_BUSY_CHK			BIT(4)
#define SDIO_CR_CLK_STOP			BIT(5)
#define SDIO_CR_DATA_CRC_EN			BIT(7)

#define SDIO_IE_TX_WM				BIT(0)
#define SDIO_IE_RX_WM				BIT(1)
#define SDIO_IE_EOT					BIT(2)
#define SDIO_IE_ERR					BIT(3)
#define SDIO_IE_TX_UDF				BIT(4)
#define SDIO_IE_TX_OVF				BIT(5)
#define SDIO_IE_RX_UDF				BIT(6)
#define SDIO_IE_RX_OVF				BIT(7)

#define SDIO_IP_TX_WM				BIT(0)
#define SDIO_IP_RX_WM				BIT(1)

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

#define SDIO_CMD_OP_CRC_EN			BIT(2)
#define SDIO_CMD_OP_POWER_EN		BIT(4)
#define SDIO_CMD_OP_CRC_CHECK_EN	BIT(5)
#define SDIO_CMD_OP_MASK			GENMASK(13,8)
#define SDIO_CMD_RSP_MASK			GENMASK(3,0)

#define SDIO_DATA_SETUP_EN			BIT(0)
#define SDIO_DATA_SETUP_RD			BIT(1)
#define SDIO_DATA_SETUP_MODE		GENMASK(3,2)
#define SDIO_DATA_SETUP_BLK_NUM		GENMASK(19, 4)
#define SDIO_DATA_SETUP_BLK_SIZE	GENMASK(31, 20)

#define NUCLEI_MMC_BUS_WIDTH_MASK	0x3
#define NUCLEI_MMC_BUS_WIDTH_1		0x0
#define NUCLEI_MMC_BUS_WIDTH_4		0x1
#define NUCLEI_MMC_BUS_WIDTH_8		0x2

/* FIFO is 64 word, 64*4 = 256 bytes*/
#define NUCLEI_SDIO_FIFO_DEPTH			64
#define NUCLEI_SDIO_RX_MARK_THRESHOLD	(NUCLEI_SDIO_FIFO_DEPTH - 16)

#define NUCLEI_SDIO_INTR_TYPE_DATA	1
#define NUCLEI_SDIO_INTR_TYPE_EOT	2

struct nclmmc_host {
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	struct mmc_host *mmc;
	struct mmc_request *mrq; /* current mrq */
	int irq;
	int bus_width;
	int cur_clk;
	uint32_t total_bytes_left;
	//uint32_t transfer_blks;
	//uint32_t blksz;
	uint32_t status;
	uint32_t intr_type;
	struct scatterlist *cur_sg;
	uint32_t sg_offset;
};

static inline int nclmmc_wait_finish(struct nclmmc_host *host, u32 *status)
{
	u32 state;
	int ret;

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

	value = readl(host->base + SDIO_CMD_OP);
	value &= ~SDIO_CMD_OP_MASK;
	value &= ~SDIO_CMD_RSP_MASK;
	value |= cmd->opcode << 8;
	value |= mmc_resp_type(cmd) & 0xf;
	//printk(KERN_ERR"cmd op:%x,arg:%x\n", value, cmd->arg);
	writel(value, host->base + SDIO_CMD_OP);

	writel(0, host->base + SDIO_DATA_SETUP);
}

static void nclmmc_prepare_data(struct nclmmc_host *host, struct mmc_data *data)
{
	u32 val;
	int blksz_bits;

	if (!data)
		return;

	blksz_bits = ffs(data->blksz) - 1;
	BUG_ON(1 << blksz_bits != data->blksz);

	val = SDIO_DATA_SETUP_EN;
	if (data->flags & MMC_DATA_READ)
		val |= SDIO_DATA_SETUP_RD;
	val |= (host->bus_width & NUCLEI_MMC_BUS_WIDTH_MASK) << 2;
	val |= ((data->blocks-1) << 4) & SDIO_DATA_SETUP_BLK_NUM;
	val |= ((data->blksz-1) << 20) & SDIO_DATA_SETUP_BLK_SIZE;
	writel(val, host->base + SDIO_DATA_SETUP);
	/* enable stop clk when transfer data */
	val = readl(host->base + SDIO_CR);
	val |= SDIO_CR_CLK_STOP;
	writel(val, host->base + SDIO_CR);

	host->total_bytes_left = data->blocks * data->blksz;
	host->cur_sg = data->sg;
	host->sg_offset = 0;
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
		if (cmd->opcode == 38) {
			printk(KERN_WARNING"wait cmd38");
			while(readl(host->base + SDIO_STATUS) & BIT(6));
			printk(KERN_WARNING"cmd38 finished");
			cmd->error = 0;
			return 0;
		}
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

static void nclmmc_controller_init(struct nclmmc_host *host)
{
	int ret;

	if (!IS_ERR(host->rstc)) {
		ret = reset_control_assert(host->rstc);
		if (!ret) {
			usleep_range(1000, 1250);
			ret = reset_control_deassert(host->rstc);
		}
	}

	writel(0xffffffff, host->base + SDIO_DATA_TIMEOUT_CNT);
	writel(0xffff, host->base + SDIO_CMD_WAIT_RSP_CNT);
	writel(0xffff, host->base + SDIO_CMD_WAIT_EOT_CNT);
	writel(0, host->base + SDIO_IE);
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

	if (!mrq)
		return;

	/*
	 * check interrupt type
	 * 1. if EOT type, get rsp, check status error,
	 * 2. if Data type, push or pull data from sdio hw
	 */
	cmd = mrq->cmd;
	data = mrq->data;

	if (host->intr_type == NUCLEI_SDIO_INTR_TYPE_EOT) {
		nclmmc_get_rsp(host, cmd);
		nclmmc_check_error(host, mrq, status);
		if (mrq->stop) {
			/* stop cmd use poll mode, avoid recursion*/
			nclmmc_send_stop_cmd(host);
		}
		host->mrq = NULL;
		mmc_request_done(host->mmc, mrq);
	} else if (host->intr_type == NUCLEI_SDIO_INTR_TYPE_DATA) {
		int i;
		u32 len;
		u32 *virt_addr;
		u32 cur_sg_len;
		/* read data */
		if (data->flags & MMC_DATA_READ) {
			u32 rx_watermark = readl(host->base + SDIO_RX_MARK);

			if (rx_watermark == 0) {
				len = host->total_bytes_left;
			} else {
				len = rx_watermark * 4;
			}
			while(len) {
				virt_addr = sg_virt(host->cur_sg) + host->sg_offset;
				cur_sg_len = host->cur_sg->length - host->sg_offset;
				if (cur_sg_len & 0x3)
					printk(KERN_ERR"rx sg is not 4 byte aliged");
				if (len > cur_sg_len) {
					for(i = 0; i < cur_sg_len; i+=4) {
						if (rx_watermark == 0)
							while(readl(host->base + SDIO_IP) & SDIO_RXFIFO_EMPTY);

						*virt_addr++ = readl(host->base + SDIO_RX_DATA);
					}
					host->cur_sg = sg_next(host->cur_sg);
					host->sg_offset = 0;
					len -= cur_sg_len;
				} else {
					for(i = 0; i < len; i+=4) {
						if (rx_watermark == 0)
							while(readl(host->base + SDIO_IP) & SDIO_RXFIFO_EMPTY);

						*virt_addr++ = readl(host->base + SDIO_RX_DATA);
					}
					host->sg_offset +=len;
					if (host->sg_offset == host->cur_sg->length) {
						host->cur_sg = sg_next(host->cur_sg);
						host->sg_offset = 0;
					}
					len = 0;
				}
			}

			if (rx_watermark == 0) {
				/* wait eot irq occur */
				host->total_bytes_left = 0;
				writel(SDIO_IE_EOT, host->base + SDIO_IE);
			} else {
				host->total_bytes_left -= rx_watermark * 4;
				/* change rx wm to 0 if rx data len is less than rx threshold */
				if (host->total_bytes_left <= rx_watermark * 4)
					writel(0, host->base + SDIO_RX_MARK);

				writel(SDIO_IE_RX_WM, host->base + SDIO_IE);
			}
		} else {
			/* write data from sglist to fifo */
			if (host->total_bytes_left > NUCLEI_SDIO_FIFO_DEPTH * 4)
				len = NUCLEI_SDIO_FIFO_DEPTH * 4;
			else {
				len = host->total_bytes_left;
			}

			host->total_bytes_left -= len;
			while(len) {
				virt_addr = sg_virt(host->cur_sg) + host->sg_offset;
				cur_sg_len = host->cur_sg->length - host->sg_offset;
				if (cur_sg_len & 0x3)
					printk(KERN_ERR"tx sg is not 4 byte aliged");
				if (len > cur_sg_len) {
					for(i = 0; i < cur_sg_len; i+=4)
						writel(*virt_addr++, host->base + SDIO_TX_DATA);

					len -= cur_sg_len;
					host->cur_sg = sg_next(host->cur_sg);
					host->sg_offset = 0;
				} else {
					for(i = 0; i < len; i+=4)
						writel(*virt_addr++, host->base + SDIO_TX_DATA);

					host->sg_offset +=len;
					if (host->sg_offset == host->cur_sg->length) {
						host->cur_sg = sg_next(host->cur_sg);
						host->sg_offset = 0;
					}
					len = 0;
				}
			}

			if (host->total_bytes_left > 0)
				writel(SDIO_IE_TX_WM, host->base + SDIO_IE);
			else {
				/* disable tx watermark */
				writel(0, host->base + SDIO_TX_MARK);
				writel(SDIO_IE_EOT, host->base + SDIO_IE);
			}
		}
	}
}

/* Interrupt Service Routine */
static irqreturn_t nclmmc_irq(int irq, void *dev_id)
{
	struct nclmmc_host * host = (struct nclmmc_host *)dev_id;
	struct mmc_request *req = host->mrq;
	u32 status;
	u32 pending;

	pending = readl(host->base + SDIO_IP);
	status = readl(host->base + SDIO_STATUS);

	if (req->data) {
		if (pending & (SDIO_IP_RX_WM | SDIO_IP_TX_WM)) {
			host->intr_type = NUCLEI_SDIO_INTR_TYPE_DATA;
			/*
			 * rx/eot or tx/eot need to disable,
			 * because eot maybe coming after current irq
			 */
			writel(0, host->base + SDIO_IE);

			return IRQ_WAKE_THREAD;
		}
	}

	if (status & SDIO_STATUS_EOT) {
		host->intr_type = NUCLEI_SDIO_INTR_TYPE_EOT;
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
	u32 intr_en = 0;

	host->mrq = mrq;
	data = mrq->data;
	cmd = mrq->cmd;

	nclmmc_prepare_cmd(host, cmd);

	if (data) {
		nclmmc_prepare_data(host, data);
		if (data->flags & MMC_DATA_READ) {
			//enable rx wm interrupt
			if (host->total_bytes_left >  NUCLEI_SDIO_RX_MARK_THRESHOLD * 4)
				writel(NUCLEI_SDIO_RX_MARK_THRESHOLD, host->base + SDIO_RX_MARK);
			else
				writel(0, host->base + SDIO_RX_MARK);
			intr_en = SDIO_IE_RX_WM;
		} else if (data->flags & MMC_DATA_WRITE) {
			//enable tx wm interrupt
			writel(1, host->base + SDIO_TX_MARK);
			intr_en = SDIO_IE_TX_WM;
		}
	} else {
		intr_en = SDIO_IE_EOT;
	}
	writel(intr_en, host->base + SDIO_IE);
	nclmmc_trigger_transaction(host);
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

	host->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->clk = devm_clk_get(&pdev->dev, "sdio_data_clk");
	if (IS_ERR(host->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->clk), "clk get fail\n");

	host->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	ret = devm_request_threaded_irq(&pdev->dev, host->irq,
					nclmmc_irq, nclmmc_func_irq_worker, IRQF_SHARED,
			NULL, host);
	if (ret)
		return ret;

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
