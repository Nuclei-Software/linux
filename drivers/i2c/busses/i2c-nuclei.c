// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026, Nucleisys Co., Ltd.
 * based on ibm i2c driver and stm i2c dma driver.
 * this driver support I2C v3.0.2 HW IP
 *
 */
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/dma-direction.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/delay.h>

#define NUCLEI_I2C_DMA_LEN_MIN		32

/* I2C register offsets and bits */
#define I2C_RX_SIZE_OFFSET			0x4  /*!< RX buffer size bitfield in bytes */
#define I2C_RX_CFG_OFFSET			0x8  /*!< RX channel configuration  */
#define I2C_TX_SIZE_OFFSET			0x10 /*!< TX buffer size bitfield in bytes */
#define I2C_TX_CFG_OFFSET			0x14 /*!< TX channel configuration field */
#define I2C_STATUS_OFFSET			0x24 /*!< I2C status register */
#define I2C_TIMING_OFFSET			0x28 /*!< I2C timing prescaler */
#define I2C_SETUP_OFFSET			0x2c /*!< I2C setup configure register */
#define I2C_TXDATA_OFFSET			0x30 /*!< Transmit data register */
#define I2C_RXDATA_OFFSET			0x34 /*!< Receive data register */
#define I2C_INT_IE_OFFSET			0x38 /*!< Interrupt enable register */
#define I2C_SLAVE_ADDRESS_OFFSET	0x3c /*!< Slave address */
#define I2C_TXFIFO_WM_OFFSET		0x7c /*!< The TX-FIFO  watermark level */
#define I2C_RXFIFO_WM_OFFSET		0x80 /*!< The RX-FIFO  watermark level */
#define I2C_IP_VERSION_OFFSET		0x9c /*!< The IP version of I2C-Controller */
#define I2C_START_SETUP_TIME_OFFSET	0xa0
#define I2C_START_HOLD_TIME_OFFSET	0xa4
#define I2C_SDA_SETUP_TIME_OFFSET	0xa8
#define I2C_SDA_HOLD_TIME_OFFSET	0xac
#define I2C_SCL_HIGH_PERIOD_OFFSET	0xb0
#define I2C_SCL_LOW_PERIOD_OFFSET	0xb4
#define I2C_STOP_SETUP_TIME_OFFSET	0xb8
#define I2C_BUS_FREE_TIME_OFFSET	0xbc

#define I2C_INTR_EN_EOT				BIT(0)
#define I2C_INTR_EN_ARB_LOST		BIT(1)
#define I2C_INTR_EN_TXFIFO_WM		BIT(2)
#define I2C_INTR_EN_RXFIFO_WM		BIT(3)
#define I2C_INTR_EN_TIMEOUT			BIT(4)

#define I2C_STATUS_BUSY				BIT(0)
#define I2C_STATUS_ARB_LOST			BIT(1)
#define I2C_STATUS_NACK_FLAG		BIT(2)
#define I2C_STATUS_EOT				BIT(3)
#define I2C_STATUS_BYTE_ON_GOING	BIT(4)
#define I2C_STATUS_TIMEOUT			BIT(5)
#define I2C_STATUS_TXFIFO_EMPTY		BIT(12)
#define I2C_STATUS_TXFIFO_FULL		BIT(13)
#define I2C_STATUS_RXFIFO_EMPTY		BIT(15)
#define I2C_STATUS_RXFIFO_FULL		BIT(16)
#define I2C_STATUS_TXFIFO_WM		BIT(25)
#define I2C_STATUS_RXFIFO_WM		BIT(26)
#define I2C_STATUS_ADDR_ACK			BIT(27)
#define I2C_STATUS_MASTER_STATE	    GENMASK(31,29)
#define I2C_STATUS_MASTER_STATE_ACK BIT(31)

#define I2C_SETUP_WORK_MODE			BIT(0)
#define I2C_SETUP_ENABLE			BIT(1)
#define I2C_SETUP_START				BIT(2)
#define I2C_SETUP_STOP				BIT(3)
#define I2C_SETUP_TRANS_DIR			BIT(4)
#define I2C_SETUP_ACK				BIT(6)
#define I2C_SETUP_SOFT_RESET		BIT(7)
#define I2C_SETUP_10BIT_EN			BIT(9)
#define I2C_SETUP_ROLE_MODE			BIT(11)
#define I2C_SETUP_SCL_PULLUP		BIT(14)
#define I2C_SETUP_SDA_PULLUP		BIT(15)
#define I2C_SETUP_AUTO_END			BIT(22)

#define I2C_RX_DMA_EN				BIT(1)
#define I2C_TX_CONT					BIT(0)
#define I2C_TX_DMA_EN				BIT(1)
#define I2C_TX_CLR					BIT(3)

#define I2C_ADDR_MATCH_ACK			1
#define I2C_DATA_ACK				2
/**
 * struct nuclei_i2c_dma - DMA specific data
 * @chan_tx: dma channel for TX transfer
 * @chan_rx: dma channel for RX transfer
 * @chan_using: dma channel used for the current transfer (TX or RX)
 * @dma_buf: dma buffer
 * @dma_len: dma buffer len
 * @dma_transfer_dir: dma transfer direction indicator
 * @dma_data_dir: dma transfer mode indicator
 * @dma_complete: dma transfer completion
 */
struct nuclei_i2c_dma {
	struct dma_chan *chan_tx;
	struct dma_chan *chan_rx;
	struct dma_chan *chan_using;
	dma_addr_t dma_buf;
	unsigned int dma_len;
	enum dma_transfer_direction dma_transfer_dir;
	enum dma_data_direction dma_data_dir;
	struct completion dma_complete;
};

struct nuclei_i2c {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	int bus_clk_rate;
	struct i2c_adapter adap;
	struct i2c_msg *msg;
	int msg_idx;
	int msg_status;
	int is_last;
	u16 last_saddr;
	u16 restart_flag;
	struct nuclei_i2c_dma *dma;
};

enum i2c_sys_clk {
	I2C_SYS_CLK_8M    = 0,
	I2C_SYS_CLK_16M   = 1,
	I2C_SYS_CLK_32M   = 2,
	I2C_SYS_CLK_100M  = 3,
	I2C_SYS_CLK_MAX
};

enum i2c_speed {
	I2C_SPEED_100K    = 0,
	I2C_SPEED_400K    = 1,
	I2C_SPEED_1M      = 2,
	I2C_SPEED_4M      = 3,
	I2C_SPEED_MAX
};

struct i2c_timing_param {
	u16  presc;
	u16  start_setup_time;
	u16  start_hold_time;
	u16  sda_setup_time;
	u16  sda_hold_time;
	u16  scl_high_period;
	u16  scl_low_period;
	u16  stop_setup_time;
	u16  bus_free_time;
};

static struct i2c_timing_param timing_cfg[I2C_SYS_CLK_MAX][I2C_SPEED_MAX] = {
	{/* sys_clk = 8M*/
		{3, 9, 8, 1, 1, 9, 9, 8, 9},    /* i2c_speed = 100K */
		{1, 2, 2, 1, 1, 4, 4, 2, 3},    /* i2c_speed = 400K */
		{0, 2, 2, 1, 1, 3, 3, 2, 2},    /* i2c_speed = 1M */
		{}/* i2c_speed = 3.4M */
	},
	{/* sys_clk = 16M*/
		{4, 15, 13, 1, 1, 15, 15, 13, 16},
		{1,  4,  4, 1, 1,  9,  9,  4,  5},
		{0,  4,  4, 1, 1,  7,  7,  4,  5},
		{}
	},
	{/* sys_clk = 48M*/
		{9, 22, 19, 1, 1, 23, 23, 19, 23},
		{5,  4,  4, 1, 1,  9,  9,  4,  5},
		{3,  3,  3, 1, 1,  5,  5,  3,  4},
		{0,  8,  8, 1, 1,  4,  9,  8,  9}
	},
	{/* sys_clk = 100M*/
		{9, 47, 40, 2, 2, 49, 49, 40, 49},
		{5, 10, 10, 1, 1, 20, 20, 10, 11},
		{4,  5,  5, 1, 1,  9,  9,  5,  6},
		{1,  8,  8, 1, 1,  4,  8,  8,  9}
	}
};

static int i2c_nuclei_config_timing(struct nuclei_i2c *i2c,
	enum i2c_sys_clk clk, enum i2c_speed speed)
{
	struct i2c_timing_param *i2c_timing;

	if (!(clk >= I2C_SYS_CLK_8M && clk < I2C_SYS_CLK_MAX)) {
		dev_dbg(i2c->dev, "i2c timing sys clk param err\n");
		goto err;
	}
	if (!(speed >= I2C_SPEED_100K && speed < I2C_SPEED_MAX)) {
		dev_dbg(i2c->dev, "i2c timing speed param err\n");
		goto err;
	}

	i2c_timing = &timing_cfg[clk][speed];
	writel_relaxed(i2c_timing->presc, i2c->base + I2C_TIMING_OFFSET);
	writel_relaxed(i2c_timing->start_setup_time, i2c->base + I2C_START_SETUP_TIME_OFFSET);
	writel_relaxed(i2c_timing->start_hold_time, i2c->base + I2C_START_HOLD_TIME_OFFSET);
	writel_relaxed(i2c_timing->sda_setup_time, i2c->base + I2C_SDA_SETUP_TIME_OFFSET);
	writel_relaxed(i2c_timing->sda_hold_time, i2c->base + I2C_SDA_HOLD_TIME_OFFSET);
	writel_relaxed(i2c_timing->scl_high_period, i2c->base + I2C_SCL_HIGH_PERIOD_OFFSET);
	writel_relaxed(i2c_timing->scl_low_period, i2c->base + I2C_SCL_LOW_PERIOD_OFFSET);
	writel_relaxed(i2c_timing->stop_setup_time, i2c->base + I2C_STOP_SETUP_TIME_OFFSET);
	writel_relaxed(i2c_timing->bus_free_time, i2c->base + I2C_BUS_FREE_TIME_OFFSET);
	mb();

	return 0;
err:
	return -1;
}

static int i2c_nuclei_init(struct nuclei_i2c *i2c)
{
	u32 val;
	enum i2c_speed speed;

	val = readl(i2c->base + I2C_SETUP_OFFSET);
	/* master mode */
	val &= ~I2C_SETUP_ROLE_MODE;
	/* SCL SDA pullup */
	val |= I2C_SETUP_SCL_PULLUP;
	val |= I2C_SETUP_SDA_PULLUP;
	/* 7bit as default */
	val &= ~I2C_SETUP_10BIT_EN;
	/* default cpu mode */
	val &= ~I2C_SETUP_WORK_MODE;
	/* i2c enable */
	val |= I2C_SETUP_ENABLE;

	/* set timing for scl clk */
	switch (i2c->bus_clk_rate){
	case I2C_MAX_FAST_MODE_PLUS_FREQ:
		speed = I2C_SPEED_1M;
		break;
	case I2C_MAX_FAST_MODE_FREQ:
		speed = I2C_SPEED_400K;
		break;
	case I2C_MAX_STANDARD_MODE_FREQ:
		speed = I2C_SPEED_100K;
		break;
	default:
		speed = I2C_SPEED_100K;
		break;
	};
	i2c_nuclei_config_timing(i2c, I2C_SYS_CLK_8M, speed);

	writel(val, i2c->base + I2C_SETUP_OFFSET);

	return 0;
}

static void i2c_nuclei_reset(struct nuclei_i2c *i2c)
{
	u32 val;

	/* reset i2c */
	val = readl(i2c->base + I2C_SETUP_OFFSET);
	val |= I2C_SETUP_SOFT_RESET;
	writel(val, i2c->base + I2C_SETUP_OFFSET);
	val &= ~I2C_SETUP_SOFT_RESET;
	writel(val, i2c->base + I2C_SETUP_OFFSET);
	/* reinit i2c */
	i2c_nuclei_init(i2c);
}

static int i2c_nuclei_clear_arb(struct nuclei_i2c *i2c)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/*
	 * If the transfer needs to abort for some reason, we'll try to
	 * force a stop condition to clear any pending bus conditions
	 */
	u32 status;

	status = readl(i2c->base + I2C_SETUP_OFFSET);
	status |= I2C_SETUP_STOP;
	writel(status, i2c->base + I2C_SETUP_OFFSET);

	/* Wait for status change */
	while (readl(i2c->base + I2C_STATUS_OFFSET) & I2C_STATUS_BUSY) {
		if (time_after(jiffies, timeout)) {
			/* Bus was not idle, try to reset adapter */
			i2c_nuclei_reset(i2c);
			return -EBUSY;
		}

		cpu_relax();
	}

	return 0;
}

static int i2c_nuclei_wait_status(struct nuclei_i2c *i2c, u32 status_bit, u32 expect_val)
{
	unsigned long x, val;
	u32 ret;

	ret = 0;
	x = jiffies + i2c->adap.timeout;
	while (((val = readl(i2c->base + I2C_STATUS_OFFSET)) & status_bit) != expect_val){
		if (unlikely(time_after(jiffies, x))){
			dev_dbg(i2c->dev,"poll status %x timeout, real status:%lx\n", status_bit, val);
			ret = -ETIMEDOUT;
			break;
		}
	}
	return ret;
}

static void i2c_nuclei_clear_status(struct nuclei_i2c *i2c, u32 status)
{
	int val; 

	val = readl(i2c->base + I2C_STATUS_OFFSET);
	val |= status;
	writel(val, i2c->base + I2C_STATUS_OFFSET);
}

static void i2c_nuclei_set_10bit_mode(struct nuclei_i2c *i2c, u32 enable)
{
	u32 val;

	val = readl(i2c->base + I2C_SETUP_OFFSET);
	if (enable)
		val |= I2C_SETUP_10BIT_EN;
	else
		val &= ~I2C_SETUP_10BIT_EN;
	writel(val, i2c->base + I2C_SETUP_OFFSET);
}

static void i2c_nuclei_lowlevel_write_addr(struct nuclei_i2c *i2c, u8 byte, u32 addr_ack)
{
	u32 val;
	u32 ret;

	do {
		if (i2c->restart_flag == 0) {
			/* txdata first, then start */
			writel(byte, i2c->base + I2C_TXDATA_OFFSET);
			val = readl(i2c->base + I2C_SETUP_OFFSET);
			val &= ~I2C_SETUP_TRANS_DIR;
			val &= ~I2C_SETUP_WORK_MODE;
			val |= I2C_SETUP_START;
			writel(val, i2c->base + I2C_SETUP_OFFSET);
		} else {
			/* restart first, then txdata */
			val = readl(i2c->base + I2C_SETUP_OFFSET);
			val &= ~I2C_SETUP_TRANS_DIR;
			val &= ~I2C_SETUP_WORK_MODE;
			val |= I2C_SETUP_START;
			writel(val, i2c->base + I2C_SETUP_OFFSET);
			writel(byte, i2c->base + I2C_TXDATA_OFFSET);
			i2c->restart_flag = 0;
		}
		if (addr_ack == I2C_ADDR_MATCH_ACK)
			ret = i2c_nuclei_wait_status(i2c, I2C_STATUS_ADDR_ACK, I2C_STATUS_ADDR_ACK);
		else
			ret = i2c_nuclei_wait_status(i2c, I2C_STATUS_NACK_FLAG, 0);
	} while(ret);
	if (addr_ack == I2C_ADDR_MATCH_ACK)
		i2c_nuclei_clear_status(i2c, I2C_STATUS_ADDR_ACK);
}

static void i2c_nuclei_set_restart_flag(struct nuclei_i2c *i2c)
{
	/* To resolve the order of TX data and the start command, which is hw requirement*/
	i2c->restart_flag = 1;
}

static void i2c_nuclei_write_addr(struct nuclei_i2c *i2c, struct i2c_msg *msg)
{
	u8 addr;

	if (msg->flags & I2C_M_TEN){
		i2c_nuclei_set_10bit_mode(i2c, 1);
		/* First byte is 11110XX0 where XX is upper 2 bits */
		addr = 0xF0 | ((msg->addr & 0x300) >> 7);
		i2c_nuclei_lowlevel_write_addr(i2c, addr, I2C_ADDR_MATCH_ACK);
		/* Second byte is the remaining 8 bits */
		addr = msg->addr & 0xFF;
		i2c_nuclei_lowlevel_write_addr(i2c, addr, I2C_DATA_ACK);
		if (msg->flags & I2C_M_RD) {
			/* For read, send restart without stop condition */
			i2c_nuclei_set_restart_flag(i2c);
			/* Then re-send the first byte with the read bit set */
			addr = 0xF0 | ((msg->addr & 0x300) >> 7) | 0x01;
			i2c_nuclei_lowlevel_write_addr(i2c, addr, I2C_ADDR_MATCH_ACK);
		}
		i2c_nuclei_set_10bit_mode(i2c, 0);
	} else {
		addr = i2c_8bit_addr_from_msg(msg);
		i2c_nuclei_lowlevel_write_addr(i2c, addr, I2C_ADDR_MATCH_ACK);
	}
	i2c->last_saddr = addr;
}

static void i2c_nuclei_lowlevel_write(struct nuclei_i2c *i2c, u8 byte)
{
	int ret;

	/* wait tx fifo is not full */
	ret = i2c_nuclei_wait_status(i2c, I2C_STATUS_TXFIFO_FULL, 0);
	if (!ret)
		writel(byte, i2c->base + I2C_TXDATA_OFFSET);
}

static int i2c_nuclei_lowlevel_read(struct nuclei_i2c *i2c, u8 *byte)
{
	int ret;

	/* wait rx fifo is not empty */
	ret = i2c_nuclei_wait_status(i2c, I2C_STATUS_RXFIFO_EMPTY, 0);
	if (!ret)
		*byte = readl(i2c->base + I2C_RXDATA_OFFSET);

	return ret;
}

/* Functions for DMA support */
struct nuclei_i2c_dma *nuclei_i2c_dma_request(struct device *dev,
						dma_addr_t phy_addr,
						u32 txdr_offset,
						u32 rxdr_offset)
{
	struct nuclei_i2c_dma *dma;
	struct dma_slave_config dma_sconfig;
	int ret;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return ERR_PTR(-ENOMEM);

	/* Request and configure I2C TX dma channel */
	dma->chan_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		ret = PTR_ERR(dma->chan_tx);
		if (ret != -ENODEV)
			ret = dev_err_probe(dev, ret,
						"can't request DMA tx channel\n");
		goto fail_al;
	}

	memset(&dma_sconfig, 0, sizeof(dma_sconfig));
	dma_sconfig.dst_addr = phy_addr + txdr_offset;
	dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_sconfig.dst_maxburst = 1;
	dma_sconfig.direction = DMA_MEM_TO_DEV;
	ret = dmaengine_slave_config(dma->chan_tx, &dma_sconfig);
	if (ret < 0) {
		dev_err(dev, "can't configure tx channel\n");
		goto fail_tx;
	}

	/* Request and configure I2C RX dma channel */
	dma->chan_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		ret = PTR_ERR(dma->chan_rx);
		if (ret != -ENODEV)
			ret = dev_err_probe(dev, ret,
					    "can't request DMA rx channel\n");

		goto fail_tx;
	}

	memset(&dma_sconfig, 0, sizeof(dma_sconfig));
	dma_sconfig.src_addr = phy_addr + rxdr_offset;
	dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_sconfig.src_maxburst = 1;
	dma_sconfig.direction = DMA_DEV_TO_MEM;
	ret = dmaengine_slave_config(dma->chan_rx, &dma_sconfig);
	if (ret < 0) {
		dev_err(dev, "can't configure rx channel\n");
		goto fail_rx;
	}

	init_completion(&dma->dma_complete);

	dev_info(dev, "using %s (tx) and %s (rx) for DMA transfers\n",
		 dma_chan_name(dma->chan_tx), dma_chan_name(dma->chan_rx));

	return dma;

fail_rx:
	dma_release_channel(dma->chan_rx);
fail_tx:
	dma_release_channel(dma->chan_tx);
fail_al:
	devm_kfree(dev, dma);

	return ERR_PTR(ret);
}

void nuclei_i2c_dma_free(struct nuclei_i2c_dma *dma)
{
	dma->dma_buf = 0;
	dma->dma_len = 0;

	dma_release_channel(dma->chan_tx);
	dma->chan_tx = NULL;

	dma_release_channel(dma->chan_rx);
	dma->chan_rx = NULL;

	dma->chan_using = NULL;
}

int nuclei_i2c_prep_dma_xfer(struct device *dev,struct nuclei_i2c_dma *dma,
			    bool rd_wr, u32 len, u8 *buf,
			    dma_async_tx_callback callback,
			    void *dma_async_param)
{
	struct dma_async_tx_descriptor *txdesc;
	struct device *chan_dev;
	int ret;

	if (rd_wr) {
		dma->chan_using = dma->chan_rx;
		dma->dma_transfer_dir = DMA_DEV_TO_MEM;
		dma->dma_data_dir = DMA_FROM_DEVICE;
	} else {
		dma->chan_using = dma->chan_tx;
		dma->dma_transfer_dir = DMA_MEM_TO_DEV;
		dma->dma_data_dir = DMA_TO_DEVICE;
	}

	dma->dma_len = len;
	chan_dev = dma->chan_using->device->dev;

	dma->dma_buf = dma_map_single(chan_dev, buf, dma->dma_len,
					dma->dma_data_dir);
	if (dma_mapping_error(chan_dev, dma->dma_buf)) {
		dev_err(dev, "DMA mapping failed\n");
		return -EINVAL;
	}

	txdesc = dmaengine_prep_slave_single(dma->chan_using, dma->dma_buf,
						dma->dma_len,
						dma->dma_transfer_dir,
						DMA_PREP_INTERRUPT);
	if (!txdesc) {
		dev_err(dev, "Not able to get desc for DMA xfer\n");
		ret = -EINVAL;
		goto err;
	}

	reinit_completion(&dma->dma_complete);

	txdesc->callback = callback;
	txdesc->callback_param = dma_async_param;
	ret = dma_submit_error(dmaengine_submit(txdesc));
	if (ret < 0) {
		dev_err(dev, "DMA submit failed\n");
		goto err;
	}

	dma_async_issue_pending(dma->chan_using);

	return 0;

err:
	dma_unmap_single(chan_dev, dma->dma_buf, dma->dma_len,
			 dma->dma_data_dir);
	return ret;
}

static void nuclei_i2c_disable_dma_req(struct nuclei_i2c *i2c)
{
	u32 val;

	/* switch work mode to cpu */
	val = readl(i2c->base + I2C_SETUP_OFFSET);
	val &= ~I2C_SETUP_WORK_MODE;
	writel(val, i2c->base + I2C_SETUP_OFFSET);

	/* disable RX/TX DMA */
	if (i2c->msg->flags & I2C_M_RD) {
		val = readl(i2c->base + I2C_SETUP_OFFSET);
		val |= I2C_SETUP_ACK;
		val &= ~I2C_SETUP_AUTO_END;
		writel(val, i2c->base + I2C_SETUP_OFFSET);

		val = readl(i2c->base + I2C_RX_CFG_OFFSET);
		val &=~I2C_RX_DMA_EN;
		writel(val, i2c->base + I2C_RX_CFG_OFFSET);
	} else {
		val = readl(i2c->base + I2C_TX_CFG_OFFSET);
		val &=~I2C_TX_DMA_EN;
		writel(val, i2c->base + I2C_TX_CFG_OFFSET);

		val = readl(i2c->base + I2C_SETUP_OFFSET);
		val &= ~I2C_SETUP_AUTO_END;
		writel(val, i2c->base + I2C_SETUP_OFFSET);
	}
}

static void nuclei_i2c_config_dma_req(struct nuclei_i2c *i2c)
{
	u32 val;

	if (i2c->msg->flags & I2C_M_RD) {
		val = readl(i2c->base + I2C_SETUP_OFFSET);
		val &= ~I2C_SETUP_ACK;
		val |= I2C_SETUP_AUTO_END;
		writel(val, i2c->base + I2C_SETUP_OFFSET);

		writel(i2c->msg->len, i2c->base + I2C_RX_SIZE_OFFSET);
	} else {
		val = readl(i2c->base + I2C_SETUP_OFFSET);
		val |= I2C_SETUP_AUTO_END;
		writel(val, i2c->base + I2C_SETUP_OFFSET);

		writel(i2c->msg->len, i2c->base + I2C_TX_SIZE_OFFSET);
	}
}

static void nuclei_i2c_enable_dma_req(struct nuclei_i2c *i2c)
{
	u32 val;

	if (i2c->msg->flags & I2C_M_RD) {
		val = readl(i2c->base + I2C_RX_CFG_OFFSET);
		val |=I2C_RX_DMA_EN;
		writel(val, i2c->base + I2C_RX_CFG_OFFSET);
	} else {
		val = readl(i2c->base + I2C_TX_CFG_OFFSET);
		val |=I2C_TX_DMA_EN;
		writel(val, i2c->base + I2C_TX_CFG_OFFSET);
	}

	/* switch work mode to DMA */
	val = readl(i2c->base + I2C_SETUP_OFFSET);
	val |=I2C_SETUP_WORK_MODE;
	writel(val, i2c->base + I2C_SETUP_OFFSET);
}

static void nuclei_i2c_dma_callback(void *arg)
{
	struct nuclei_i2c *i2c = (struct nuclei_i2c *)arg;
	struct nuclei_i2c_dma *dma = i2c->dma;
	struct device *dev = dma->chan_using->device->dev;

	i2c_nuclei_wait_status(i2c, I2C_STATUS_EOT, I2C_STATUS_EOT);
	i2c_nuclei_clear_status(i2c, I2C_STATUS_EOT);

	nuclei_i2c_disable_dma_req(i2c);

	dma_unmap_single(dev, dma->dma_buf, dma->dma_len, dma->dma_data_dir);
	complete(&dma->dma_complete);
}

static int nuclei_process_msg(struct nuclei_i2c *i2c, int msgidx)
{
	int i;
	int val;
	int ret = 0;
	int time_left;
	u8 addr;

	addr = i2c_8bit_addr_from_msg(i2c->msg);
	dev_dbg(i2c->dev, "i2c transfer to addr:%x msgidx%d\n", addr, msgidx);
	if (!msgidx) {
		i2c_nuclei_write_addr(i2c, i2c->msg);
	} else if (i2c->last_saddr != addr) {
		/* different slave addr or read,write
		 * send restart and slave address to transfer.
		 */
		i2c_nuclei_set_restart_flag(i2c);
		i2c_nuclei_write_addr(i2c, i2c->msg);
	}

	if (i2c->msg->len >= NUCLEI_I2C_DMA_LEN_MIN && i2c->dma) {
		nuclei_i2c_config_dma_req(i2c);
		/* dma transfer data */
		ret = nuclei_i2c_prep_dma_xfer(i2c->dev, i2c->dma,
						i2c->msg->flags & I2C_M_RD,
						i2c->msg->len, i2c->msg->buf,
						nuclei_i2c_dma_callback,
						i2c);
		if (ret) {
			dev_warn(i2c->dev, "can't use DMA\n");
			goto fallback_pio;
		}
		dev_dbg(i2c->dev, "dma transfer %d\n",i2c->msg->len);
		nuclei_i2c_enable_dma_req(i2c);
		time_left = wait_for_completion_timeout(&i2c->dma->dma_complete,
				msecs_to_jiffies(10000));
		if (!time_left) 
			dev_warn(i2c->dev, "i2c dma transfer timeout.\n");

		return 0;
	}
fallback_pio:
	dev_dbg(i2c->dev, "pio transfer %d\n",i2c->msg->len);
	/* switch work mode to cpu */
	val = readl(i2c->base + I2C_SETUP_OFFSET);
	val &= ~I2C_SETUP_WORK_MODE;
	/* pio transfer data */
	if (i2c->msg->flags & I2C_M_RD) {
		/* enable master ack for data */
		val &= ~I2C_SETUP_ACK;
		val |= I2C_SETUP_TRANS_DIR;
		writel(val, i2c->base + I2C_SETUP_OFFSET);
		for (i = 0; i < i2c->msg->len; i++) {
			if (i == (i2c->msg->len -1)) {
				val = readl(i2c->base + I2C_SETUP_OFFSET);
				val |= I2C_SETUP_ACK;
				//val |= I2C_SETUP_STOP;
				/* wait for controller exit from ack stage*/
				while((readl(i2c->base + I2C_STATUS_OFFSET) &
					I2C_STATUS_MASTER_STATE) == I2C_STATUS_MASTER_STATE_ACK);
				writel(val, i2c->base + I2C_SETUP_OFFSET);
			}
			ret = i2c_nuclei_lowlevel_read(i2c, &i2c->msg->buf[i]);
			if (ret) {
				dev_warn(i2c->dev, "i2c read fail.\n");
				return ret;
			}
		}
	} else {
		val &= ~I2C_SETUP_TRANS_DIR;
		writel(val, i2c->base + I2C_SETUP_OFFSET);
		writel(i2c->msg->len, i2c->base + I2C_TX_SIZE_OFFSET);
		for (i = 0; i < i2c->msg->len; i++) {
			i2c_nuclei_lowlevel_write(i2c, i2c->msg->buf[i]);
		}
		i2c_nuclei_wait_status(i2c, I2C_STATUS_EOT, I2C_STATUS_EOT);
		i2c_nuclei_clear_status(i2c, I2C_STATUS_EOT);
	}

	/* if last msg, pio stop transfer */
	if (i2c->is_last /*&& !(i2c->msg->flags & I2C_M_RD)*/) {
		dev_dbg(i2c->dev, "i2c pio stop\n");
		val = readl(i2c->base + I2C_SETUP_OFFSET);
		val |= I2C_SETUP_STOP;
		writel(val, i2c->base + I2C_SETUP_OFFSET);
		while(readl(i2c->base + I2C_SETUP_OFFSET) & I2C_SETUP_STOP){};
	}

	return 0;
}

static int i2c_nuclei_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			  int msg_num)
{
	struct nuclei_i2c *i2c = i2c_get_adapdata(adap);
	int ret, i;
	u32 stat;

	/* Check for bus idle condition */
	stat = readl(i2c->base + I2C_STATUS_OFFSET);
	if (stat & I2C_STATUS_BUSY) {
		/* Something is holding the bus, try to clear it */
		i2c_nuclei_clear_arb(i2c);
		return 0;
	}

	/* Process a single message at a time */
	for (i = 0; i < msg_num; i++) {
		/* Save message pointer and current message data index */
		i2c->msg = &msgs[i];
		i2c->msg_idx = 0;
		i2c->msg_status = -EBUSY;
		i2c->is_last = (i == (msg_num - 1)) ? 1 : 0;
		dev_dbg(i2c->dev, "i:%d,msg_num:%d\n", i, msg_num);
		ret = nuclei_process_msg(i2c, i);
		if (ret)
			return ret;
	}

	return msg_num;
}

static u32 i2c_nuclei_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL_ALL;
}

static const struct i2c_algorithm i2c_nuclei_algorithm = {
	.master_xfer	= i2c_nuclei_xfer,
	.functionality	= i2c_nuclei_functionality,
};

static int i2c_nuclei_probe(struct platform_device *pdev)
{
	struct nuclei_i2c *i2c;
	struct resource *res;
	dma_addr_t phy_addr;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);
	phy_addr = (dma_addr_t)res->start;
	i2c->dev = &pdev->dev;

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
					&i2c->bus_clk_rate);
	if (ret)
		i2c->bus_clk_rate = I2C_MAX_STANDARD_MODE_FREQ;

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "error getting clock\n");
		return PTR_ERR(i2c->clk);
	}

	ret = clk_prepare_enable(i2c->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock.");
		return ret;
	}

	/* Init i2c ip */
	i2c_nuclei_init(i2c);

	/* Init DMA config if supported */
	i2c->dma = nuclei_i2c_dma_request(i2c->dev, phy_addr,
					     I2C_TXDATA_OFFSET,
					     I2C_RXDATA_OFFSET);
	if (IS_ERR(i2c->dma)) {
		ret = PTR_ERR(i2c->dma);
		dev_dbg(i2c->dev, "No DMA option: fallback using PIO\n");
		i2c->dma = NULL;
	}

	platform_set_drvdata(pdev, i2c);

	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.owner = THIS_MODULE;
	strscpy(i2c->adap.name, "nuclei I2C adapter", sizeof(i2c->adap.name));
	i2c->adap.algo = &i2c_nuclei_algorithm;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;
	i2c->adap.timeout = msecs_to_jiffies(5);

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0)
		goto fail_clk;

	dev_info(&pdev->dev, "nuclei I2C adapter\n");

	return 0;

fail_clk:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int i2c_nuclei_remove(struct platform_device *dev)
{
	struct nuclei_i2c *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);
	if (i2c->dma) {
		nuclei_i2c_dma_free(i2c->dma);
		i2c->dma = NULL;
	}
	clk_disable_unprepare(i2c->clk);

	return 0;
}

#ifdef CONFIG_PM
static int i2c_nuclei_suspend(struct device *dev)
{
	struct nuclei_i2c *i2c = dev_get_drvdata(dev);

	clk_disable(i2c->clk);

	return 0;
}

static int i2c_nuclei_resume(struct device *dev)
{
	struct nuclei_i2c *i2c = dev_get_drvdata(dev);

	clk_enable(i2c->clk);
	i2c_nuclei_reset(i2c);

	return 0;
}

static const struct dev_pm_ops i2c_nuclei_dev_pm_ops = {
	.suspend_noirq = i2c_nuclei_suspend,
	.resume_noirq = i2c_nuclei_resume,
};

#define I2C_nuclei_DEV_PM_OPS (&i2c_nuclei_dev_pm_ops)
#else
#define I2C_nuclei_DEV_PM_OPS NULL
#endif

static const struct of_device_id nuclei_i2c_match[] = {
	{ .compatible = "nuclei,i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, nuclei_i2c_match);

static struct platform_driver i2c_nuclei_driver = {
	.probe	= i2c_nuclei_probe,
	.remove	= i2c_nuclei_remove,
	.driver	= {
		.name	= "nuclei-i2c",
		.pm		= I2C_nuclei_DEV_PM_OPS,
		.of_match_table	= nuclei_i2c_match,
	},
};
module_platform_driver(i2c_nuclei_driver);

MODULE_DESCRIPTION("I2C driver for Nuclei devices");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuclei-i2c");
