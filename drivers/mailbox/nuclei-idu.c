// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Nucleisys Co., Ltd.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#define NUCLEI_MAX_CHANS					1

#define NUCLEI_IDU_SEMAPHORE_MASK			0x3FF
#define NUCLEI_IDU_ICI_STATUS_MAX_BIT		23

#define NUCLEI_IDU_CLUSTER1_CORE_ID			1
#define NUCLEI_IDU_CLUSTER2_CORE_ID			2

/* IDU reg map */
#define NUCLEI_IDU_CLUSTER1_ICI_STATUS		0x4
#define NUCLEI_IDU_CLUSTER2_ICI_STATUS		0x8
#define NUCLEI_IDU_SEMAPHORE0				0x80
#define NUCLEI_IDU_REG_ICI					0x3FFC

struct nuclei_idu {
	void __iomem *regs;
	spinlock_t lock;
	int irq;
	struct mbox_chan chan[NUCLEI_MAX_CHANS];
	struct mbox_controller controller;
};

extern void mbox_client_txdone(struct mbox_chan *chan, int r);

static struct nuclei_idu *nuclei_chan2idu(struct mbox_chan *cur_chan)
{
	return container_of(cur_chan->mbox, struct nuclei_idu, controller);
}

static irqreturn_t nuclei_idu_irq(int irq, void *dev_id)
{
	struct mbox_chan *cur_chan = dev_id;
	struct nuclei_idu *mbox = nuclei_chan2idu(cur_chan);
	struct device *dev = mbox->controller.dev;
	unsigned long val;
	unsigned long core_idx;

	val = readl(mbox->regs + NUCLEI_IDU_CLUSTER1_ICI_STATUS);
	for_each_set_bit(core_idx, &val, NUCLEI_IDU_ICI_STATUS_MAX_BIT) {
		u32 msg;

		msg = (((u32)val) >> 24);
		dev_dbg(dev, "recv msg:%d from core%ld\n", msg, core_idx);

		mbox_chan_received_data(cur_chan, &msg);

		/* clear interrupt */
		writel(1 << core_idx, mbox->regs + NUCLEI_IDU_CLUSTER1_ICI_STATUS);
	}

	return IRQ_HANDLED;
}

/* use IDU semaphore0 as tx semaphore to protect idu reg_ici memory register */
static int nuclei_acquire_tx_semaphore(struct nuclei_idu *mbox)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100); /* 100ms timeout */
	u32 val;

	while (time_before(jiffies, timeout)) {
		spin_lock(&mbox->lock);

		val = readl(mbox->regs + NUCLEI_IDU_SEMAPHORE0);
		if ((val & NUCLEI_IDU_SEMAPHORE_MASK) == NUCLEI_IDU_SEMAPHORE_MASK) {
			val &= ~NUCLEI_IDU_SEMAPHORE_MASK;
			val |= NUCLEI_IDU_CLUSTER1_CORE_ID << 4 | smp_processor_id();
			writel(val, mbox->regs + NUCLEI_IDU_SEMAPHORE0);
			spin_unlock(&mbox->lock);
			return 0;
		}

		spin_unlock(&mbox->lock);
		usleep_range(100, 200);	 /* retry after sleep 100us~200us  */
	}

	dev_err(mbox->controller.dev, "Timeout acquiring hw semaphore0\n");
	return -ETIMEDOUT;
}

static void nuclei_release_tx_semaphore(struct nuclei_idu *mbox)
{
	u32 val;

	spin_lock(&mbox->lock);
	val = readl(mbox->regs + NUCLEI_IDU_SEMAPHORE0);
	val |= NUCLEI_IDU_SEMAPHORE_MASK;
	writel(val, mbox->regs + NUCLEI_IDU_SEMAPHORE0);
	spin_unlock(&mbox->lock);
}

static int nuclei_send_data(struct mbox_chan *cur_chan, void *data)
{
	struct nuclei_idu *mbox = nuclei_chan2idu(cur_chan);
	u32 val;

	/* get semaphore0 before using reg_ici to send inter-core msg */
	val = nuclei_acquire_tx_semaphore(mbox);
	if (val) {
		dev_dbg(mbox->controller.dev, "idu is busy, can not send data\n");
		return val;
	}
	/* write reg_ici to trigger ici interrupt */
	val = (((*(u32*)data) & 0xFF) << 24) | ((NUCLEI_IDU_CLUSTER1_CORE_ID &
			0xFF) << 16) | (NUCLEI_IDU_CLUSTER2_CORE_ID & 0xFFFF);
	writel(val, mbox->regs + NUCLEI_IDU_REG_ICI);

	/* release semaphore0 */
	nuclei_release_tx_semaphore(mbox);

	return 0;
}

static bool nuclei_last_tx_done(struct mbox_chan *cur_chan)
{
	struct nuclei_idu *mbox = nuclei_chan2idu(cur_chan);
	u32 val;

	val = readl(mbox->regs + NUCLEI_IDU_CLUSTER2_ICI_STATUS);
	if (val & (1 << NUCLEI_IDU_CLUSTER2_CORE_ID))
		return false;
	else
		return true;
}

static const struct mbox_chan_ops nuclei_idu_chan_ops = {
	.send_data = nuclei_send_data,
	.last_tx_done = nuclei_last_tx_done
};

static int nuclei_idu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct nuclei_idu *mbox;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (mbox == NULL)
		return -ENOMEM;

	mbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->regs)) {
		ret = PTR_ERR(mbox->regs);
		return ret;
	}

	mbox->irq = platform_get_irq(pdev, 0);
	if (mbox->irq < 0) {
		dev_err(dev, "mailbox IRQ Err: %d\n", mbox->irq);
		return -ENODEV;
	}

	ret = request_irq(mbox->irq, nuclei_idu_irq,
			  IRQF_ONESHOT, "nuclei_idu", &mbox->chan[0]);
	if (ret) {
		dev_err(dev,
			"Unable to request IRQ %d\n", mbox->irq);
		return ret;
	}

	mbox->controller.txdone_poll = true;
	mbox->controller.txpoll_period = 5;
	mbox->controller.ops = &nuclei_idu_chan_ops;
	mbox->controller.dev = dev;
	mbox->controller.chans = &mbox->chan[0];
	mbox->controller.num_chans = NUCLEI_MAX_CHANS;

	ret = devm_mbox_controller_register(dev, &mbox->controller);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "mailbox enabled\n");

	return ret;
}

static const struct of_device_id nuclei_idu_of_match[] = {
	{ .compatible = "nuclei,idu", },
	{},
};
MODULE_DEVICE_TABLE(of, nuclei_idu_of_match);

static struct platform_driver nuclei_idu_driver = {
	.driver = {
		.name = "nuclei-idu",
		.of_match_table = nuclei_idu_of_match,
	},
	.probe = nuclei_idu_probe,
};
module_platform_driver(nuclei_idu_driver);

MODULE_DESCRIPTION("nuclei idu inter-core-interrupt driver");
MODULE_LICENSE("GPL v2");
