// SPDX-License-Identifier: GPL-2.0-or-later
//
// Nuclei SPI controller driver (master mode only)
//
// Copyright 2021 Nuclei System Technology
//
// Based on code from SiFive SPI Driver
//

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/log2.h>

#define NUCLEI_SPI_DRIVER_NAME           "nuclei_spi"

#define NUCLEI_SPI_MAX_CS                32
#define NUCLEI_SPI_DEFAULT_DEPTH         8
#define NUCLEI_SPI_DEFAULT_MAX_BITS      8

/* register offsets */
#define NUCLEI_SPI_REG_SCKDIV            0x00 /* Serial clock divisor */
#define NUCLEI_SPI_REG_SCKMODE           0x04 /* Serial clock mode */
#define NUCLEI_SPI_REG_CSID              0x10 /* Chip select ID */
#define NUCLEI_SPI_REG_CSDEF             0x14 /* Chip select default */
#define NUCLEI_SPI_REG_CSMODE            0x18 /* Chip select mode */
#define NUCLEI_SPI_REG_DELAY0            0x28 /* Delay control 0 */
#define NUCLEI_SPI_REG_DELAY1            0x2c /* Delay control 1 */
#define NUCLEI_SPI_REG_FMT               0x40 /* Frame format */
#define NUCLEI_SPI_REG_TXDATA            0x48 /* Tx FIFO data */
#define NUCLEI_SPI_REG_RXDATA            0x4c /* Rx FIFO data */
#define NUCLEI_SPI_REG_TXMARK            0x50 /* Tx FIFO watermark */
#define NUCLEI_SPI_REG_RXMARK            0x54 /* Rx FIFO watermark */
#define NUCLEI_SPI_REG_FCTRL             0x60 /* SPI flash interface control */
#define NUCLEI_SPI_REG_FFMT              0x64 /* SPI flash instruction format */
#define NUCLEI_SPI_REG_IE                0x70 /* Interrupt Enable Register */
#define NUCLEI_SPI_REG_IP                0x74 /* Interrupt Pendings Register */

/* sckdiv bits */
#define NUCLEI_SPI_SCKDIV_DIV_MASK       0xfffU

/* sckmode bits */
#define NUCLEI_SPI_SCKMODE_PHA           BIT(0)
#define NUCLEI_SPI_SCKMODE_POL           BIT(1)
#define NUCLEI_SPI_SCKMODE_MODE_MASK     (NUCLEI_SPI_SCKMODE_PHA | \
                                          NUCLEI_SPI_SCKMODE_POL)

/* csmode bits */
#define NUCLEI_SPI_CSMODE_MODE_AUTO      0U
#define NUCLEI_SPI_CSMODE_MODE_HOLD      2U
#define NUCLEI_SPI_CSMODE_MODE_OFF       3U

/* delay0 bits */
#define NUCLEI_SPI_DELAY0_CSSCK(x)       ((u32)(x))
#define NUCLEI_SPI_DELAY0_CSSCK_MASK     0xffU
#define NUCLEI_SPI_DELAY0_SCKCS(x)       ((u32)(x) << 16)
#define NUCLEI_SPI_DELAY0_SCKCS_MASK     (0xffU << 16)

/* delay1 bits */
#define NUCLEI_SPI_DELAY1_INTERCS(x)     ((u32)(x))
#define NUCLEI_SPI_DELAY1_INTERCS_MASK   0xffU
#define NUCLEI_SPI_DELAY1_INTERXFR(x)    ((u32)(x) << 16)
#define NUCLEI_SPI_DELAY1_INTERXFR_MASK  (0xffU << 16)

/* fmt bits */
#define NUCLEI_SPI_FMT_PROTO_SINGLE      0U
#define NUCLEI_SPI_FMT_PROTO_DUAL        1U
#define NUCLEI_SPI_FMT_PROTO_QUAD        2U
#define NUCLEI_SPI_FMT_PROTO_MASK        3U
#define NUCLEI_SPI_FMT_ENDIAN            BIT(2)
#define NUCLEI_SPI_FMT_DIR               BIT(3)
#define NUCLEI_SPI_FMT_LEN(x)            ((u32)(x) << 16)
#define NUCLEI_SPI_FMT_LEN_MASK          (0xfU << 16)

/* txdata bits */
#define NUCLEI_SPI_TXDATA_DATA_MASK      0xffU
#define NUCLEI_SPI_TXDATA_FULL           BIT(31)

/* rxdata bits */
#define NUCLEI_SPI_RXDATA_DATA_MASK      0xffU
#define NUCLEI_SPI_RXDATA_EMPTY          BIT(31)

/* ie and ip bits */
#define NUCLEI_SPI_IP_TXWM               BIT(0)
#define NUCLEI_SPI_IP_RXWM               BIT(1)

struct nuclei_spi {
	void __iomem      *regs;        /* virt. address of control registers */
	struct clk        *clk;         /* bus clock */
	unsigned int      fifo_depth;   /* fifo depth in words */
	u32               cs_inactive;  /* level of the CS pins when inactive */
	struct completion done;         /* wake-up from interrupt */
};

static void nuclei_spi_write(struct nuclei_spi *spi, int offset, u32 value)
{
	iowrite32(value, spi->regs + offset);
}

static u32 nuclei_spi_read(struct nuclei_spi *spi, int offset)
{
	return ioread32(spi->regs + offset);
}

static void nuclei_spi_init(struct nuclei_spi *spi)
{
	/* Watermark interrupts are disabled by default */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_IE, 0);

	/* Default watermark FIFO threshold values */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_TXMARK, 1);
	nuclei_spi_write(spi, NUCLEI_SPI_REG_RXMARK, 0);

	/* Set CS/SCK Delays and Inactive Time to defaults */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_DELAY0,
			 NUCLEI_SPI_DELAY0_CSSCK(1) |
			 NUCLEI_SPI_DELAY0_SCKCS(1));
	nuclei_spi_write(spi, NUCLEI_SPI_REG_DELAY1,
			 NUCLEI_SPI_DELAY1_INTERCS(1) |
			 NUCLEI_SPI_DELAY1_INTERXFR(0));

	/* Exit specialized memory-mapped SPI flash mode */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_FCTRL, 0);
}

static int
nuclei_spi_prepare_message(struct spi_master *master, struct spi_message *msg)
{
	struct nuclei_spi *spi = spi_master_get_devdata(master);
	struct spi_device *device = msg->spi;

	/* Update the chip select polarity */
	if (device->mode & SPI_CS_HIGH)
		spi->cs_inactive &= ~BIT(device->chip_select);
	else
		spi->cs_inactive |= BIT(device->chip_select);
	nuclei_spi_write(spi, NUCLEI_SPI_REG_CSDEF, spi->cs_inactive);

	/* Select the correct device */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_CSID, device->chip_select);

	/* Set clock mode */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_SCKMODE,
			 device->mode & NUCLEI_SPI_SCKMODE_MODE_MASK);

	return 0;
}

static void nuclei_spi_set_cs(struct spi_device *device, bool is_high)
{
	struct nuclei_spi *spi = spi_master_get_devdata(device->master);

	/* Reverse polarity is handled by SCMR/CPOL. Not inverted CS. */
	if (device->mode & SPI_CS_HIGH)
		is_high = !is_high;

	nuclei_spi_write(spi, NUCLEI_SPI_REG_CSMODE, is_high ?
			 NUCLEI_SPI_CSMODE_MODE_AUTO :
			 NUCLEI_SPI_CSMODE_MODE_HOLD);
}

static int
nuclei_spi_prep_transfer(struct nuclei_spi *spi, struct spi_device *device,
			 struct spi_transfer *t)
{
	u32 cr;
	unsigned int mode;

	/* Calculate and program the clock rate */
	cr = DIV_ROUND_UP(clk_get_rate(spi->clk) >> 1, t->speed_hz) - 1;
	cr &= NUCLEI_SPI_SCKDIV_DIV_MASK;
	nuclei_spi_write(spi, NUCLEI_SPI_REG_SCKDIV, cr);

	mode = max_t(unsigned int, t->rx_nbits, t->tx_nbits);

	/* Set frame format */
	cr = NUCLEI_SPI_FMT_LEN(t->bits_per_word);
	switch (mode) {
	case SPI_NBITS_QUAD:
		cr |= NUCLEI_SPI_FMT_PROTO_QUAD;
		break;
	case SPI_NBITS_DUAL:
		cr |= NUCLEI_SPI_FMT_PROTO_DUAL;
		break;
	default:
		cr |= NUCLEI_SPI_FMT_PROTO_SINGLE;
		break;
	}
	if (device->mode & SPI_LSB_FIRST)
		cr |= NUCLEI_SPI_FMT_ENDIAN;
	if (!t->rx_buf)
		cr |= NUCLEI_SPI_FMT_DIR;
	nuclei_spi_write(spi, NUCLEI_SPI_REG_FMT, cr);

	/* We will want to poll if the time we need to wait is
	 * less than the context switching time.
	 * Let's call that threshold 5us. The operation will take:
	 *    (8/mode) * fifo_depth / hz <= 5 * 10^-6
	 *    1600000 * fifo_depth <= hz * mode
	 */

	return 1600000 * spi->fifo_depth <= t->speed_hz * mode;
}

static irqreturn_t nuclei_spi_irq(int irq, void *dev_id)
{
	struct nuclei_spi *spi = dev_id;
	u32 ip = nuclei_spi_read(spi, NUCLEI_SPI_REG_IP);

	if (ip & (NUCLEI_SPI_IP_TXWM | NUCLEI_SPI_IP_RXWM)) {
		/* Disable interrupts until next transfer */
		nuclei_spi_write(spi, NUCLEI_SPI_REG_IE, 0);
		complete(&spi->done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void nuclei_spi_wait(struct nuclei_spi *spi, u32 bit, int poll)
{
	if (poll) {
		u32 cr;

		do {
			cr = nuclei_spi_read(spi, NUCLEI_SPI_REG_IP);
		} while (!(cr & bit));
	} else {
		reinit_completion(&spi->done);
		nuclei_spi_write(spi, NUCLEI_SPI_REG_IE, bit);
		wait_for_completion(&spi->done);
	}
}

static void nuclei_spi_tx(struct nuclei_spi *spi, const u8 *tx_ptr)
{
	WARN_ON_ONCE((nuclei_spi_read(spi, NUCLEI_SPI_REG_TXDATA)
				& NUCLEI_SPI_TXDATA_FULL) != 0);
	nuclei_spi_write(spi, NUCLEI_SPI_REG_TXDATA,
			 *tx_ptr & NUCLEI_SPI_TXDATA_DATA_MASK);
}

static void nuclei_spi_rx(struct nuclei_spi *spi, u8 *rx_ptr)
{
	u32 data = nuclei_spi_read(spi, NUCLEI_SPI_REG_RXDATA);

	WARN_ON_ONCE((data & NUCLEI_SPI_RXDATA_EMPTY) != 0);
	*rx_ptr = data & NUCLEI_SPI_RXDATA_DATA_MASK;
}

static int
nuclei_spi_transfer_one(struct spi_master *master, struct spi_device *device,
			struct spi_transfer *t)
{
	struct nuclei_spi *spi = spi_master_get_devdata(master);
	int poll = nuclei_spi_prep_transfer(spi, device, t);
	const u8 *tx_ptr = t->tx_buf;
	u8 *rx_ptr = t->rx_buf;
	unsigned int remaining_words = t->len;

	while (remaining_words) {
		unsigned int n_words = min(remaining_words, spi->fifo_depth);
		unsigned int i;

		/* Enqueue n_words for transmission */
		for (i = 0; i < n_words; i++)
			nuclei_spi_tx(spi, tx_ptr++);

		if (rx_ptr) {
			/* Wait for transmission + reception to complete */
			nuclei_spi_write(spi, NUCLEI_SPI_REG_RXMARK,
					 n_words - 1);
			nuclei_spi_wait(spi, NUCLEI_SPI_IP_RXWM, poll);

			/* Read out all the data from the RX FIFO */
			for (i = 0; i < n_words; i++)
				nuclei_spi_rx(spi, rx_ptr++);
		} else {
			/* Wait for transmission to complete */
			nuclei_spi_wait(spi, NUCLEI_SPI_IP_TXWM, poll);
		}

		remaining_words -= n_words;
	}

	return 0;
}

static int nuclei_spi_probe(struct platform_device *pdev)
{
	struct nuclei_spi *spi;
	int ret, irq, num_cs;
	u32 cs_bits, max_bits_per_word;
	struct spi_master *master;

	master = spi_alloc_master(&pdev->dev, sizeof(struct nuclei_spi));
	if (!master) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	spi = spi_master_get_devdata(master);
	init_completion(&spi->done);
	platform_set_drvdata(pdev, master);

	spi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi->regs)) {
		ret = PTR_ERR(spi->regs);
		goto put_master;
	}

	spi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		dev_err(&pdev->dev, "Unable to find bus clock\n");
		ret = PTR_ERR(spi->clk);
		goto put_master;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto put_master;
	}

	/* Optional parameters */
	ret =
	  of_property_read_u32(pdev->dev.of_node, "nuclei,fifo-depth",
			       &spi->fifo_depth);
	if (ret < 0)
		spi->fifo_depth = NUCLEI_SPI_DEFAULT_DEPTH;

	ret =
	  of_property_read_u32(pdev->dev.of_node, "nuclei,max-bits-per-word",
			       &max_bits_per_word);

	if (!ret && max_bits_per_word < 8) {
		dev_err(&pdev->dev, "Only 8bit SPI words supported by the driver\n");
		ret = -EINVAL;
		goto put_master;
	}

	/* Spin up the bus clock before hitting registers */
	ret = clk_prepare_enable(spi->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable bus clock\n");
		goto put_master;
	}

	/* probe the number of CS lines */
	spi->cs_inactive = nuclei_spi_read(spi, NUCLEI_SPI_REG_CSDEF);
	nuclei_spi_write(spi, NUCLEI_SPI_REG_CSDEF, 0xffffffffU);
	cs_bits = nuclei_spi_read(spi, NUCLEI_SPI_REG_CSDEF);
	nuclei_spi_write(spi, NUCLEI_SPI_REG_CSDEF, spi->cs_inactive);
	if (!cs_bits) {
		dev_err(&pdev->dev, "Could not auto probe CS lines\n");
		ret = -EINVAL;
		goto disable_clk;
	}

	num_cs = ilog2(cs_bits) + 1;
	if (num_cs > NUCLEI_SPI_MAX_CS) {
		dev_err(&pdev->dev, "Invalid number of spi slaves\n");
		ret = -EINVAL;
		goto disable_clk;
	}

	/* Define our master */
	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = pdev->id;
	master->num_chipselect = num_cs;
	master->mode_bits = SPI_CPHA | SPI_CPOL
			  | SPI_CS_HIGH | SPI_LSB_FIRST
			  | SPI_TX_DUAL | SPI_TX_QUAD
			  | SPI_RX_DUAL | SPI_RX_QUAD;
	/* TODO: add driver support for bits_per_word < 8
	 * we need to "left-align" the bits (unless SPI_LSB_FIRST)
	 */
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->flags = SPI_CONTROLLER_MUST_TX | SPI_MASTER_GPIO_SS;
	master->prepare_message = nuclei_spi_prepare_message;
	master->set_cs = nuclei_spi_set_cs;
	master->transfer_one = nuclei_spi_transfer_one;

	pdev->dev.dma_mask = NULL;
	/* Configure the SPI master hardware */
	nuclei_spi_init(spi);

	/* Register for SPI Interrupt */
	ret = devm_request_irq(&pdev->dev, irq, nuclei_spi_irq, 0,
			       dev_name(&pdev->dev), spi);
	if (ret) {
		dev_err(&pdev->dev, "Unable to bind to interrupt\n");
		goto disable_clk;
	}

	dev_info(&pdev->dev, "mapped; irq=%d, cs=%d\n",
		 irq, master->num_chipselect);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	clk_disable_unprepare(spi->clk);
put_master:
	spi_master_put(master);

	return ret;
}

static int nuclei_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct nuclei_spi *spi = spi_master_get_devdata(master);

	/* Disable all the interrupts just in case */
	nuclei_spi_write(spi, NUCLEI_SPI_REG_IE, 0);
	clk_disable_unprepare(spi->clk);

	return 0;
}

static const struct of_device_id nuclei_spi_of_match[] = {
	{ .compatible = "nuclei,spi0", },
	{}
};
MODULE_DEVICE_TABLE(of, nuclei_spi_of_match);

static struct platform_driver nuclei_spi_driver = {
	.probe = nuclei_spi_probe,
	.remove = nuclei_spi_remove,
	.driver = {
		.name = NUCLEI_SPI_DRIVER_NAME,
		.of_match_table = nuclei_spi_of_match,
	},
};
module_platform_driver(nuclei_spi_driver);

MODULE_AUTHOR("Nuclei System Technology <contact@nuclei.com>");
MODULE_DESCRIPTION("Nuclei SPI driver");
MODULE_LICENSE("GPL");
