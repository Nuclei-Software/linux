// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Nucleisys Co., Ltd.
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
#include <linux/mailbox/nuclei_mailbox.h>

/* mailbox registers */
#define MAILBOX_CSR				0x8
#define MAILBOX_LINKID0			0x10
#define MAILBOX_LINKID1			0x14
#define MAILBOX_LOCKOUT0		0x18
#define MAILBOX_LOCKOUT1		0x1C
#define MAILBOX_LOCKOUT2		0x20
#define MAILBOX_LOCKOUT3		0x24
#define MAILBOX_LOCKOUT4		0x28
#define MAILBOX_LOCKOUT5		0x2C
#define MAILBOX_LOCKOUT6		0x30
#define MAILBOX_LOCKOUT7		0x34
#define MAILBOX_INT_EN			0x38
#define MAILBOX_INT_STAT		0x3C
/* sram offset */
#define MAILBOX_SRAM			0x2000

/* CSR register bit field */
#define MAILBOX_CSR_IN_FULL		BIT(0)
#define MAILBOX_CSR_OUT_FULL	BIT(1)
#define MAILBOX_CSR_LINK		BIT(2)
#define MAILBOX_CSR_UNLINK		BIT(3)

/* INT_EN register bit field */
#define MAILBOX_INT_EN_DONE		BIT(1)

#define MAILBOX_INT_STAT_DONE	BIT(1)

#define NOT_LINKED				0
#define LINKED					1
#define LINK_UNAVAILABLE		0
#define LINK_AVAILABLE			1

#define MAILBOX_MSG_SIZE		0x100

#define NUCLEI_MAX_CHANS		8

struct nuclei_mbox {
	void __iomem *regs;
	int irq;
	struct mbox_chan chan[NUCLEI_MAX_CHANS];
	struct mbox_controller controller;
};

extern void mbox_client_txdone(struct mbox_chan *chan, int r);

static struct nuclei_mbox *nuclei_link_mbox(struct mbox_chan *link)
{
	return container_of(link->mbox, struct nuclei_mbox, controller);
}

static u32 nuclei_link2chan(struct mbox_chan *link)
{
	struct nuclei_mbox *mb;

	mb = container_of(link->mbox, struct nuclei_mbox, controller);

	return link - &(mb->chan[0]);
}

static irqreturn_t nuclei_mbox_irq(int irq, void *dev_id)
{
	struct mbox_chan *link = dev_id;
	struct nuclei_mbox *mbox = nuclei_link_mbox(link);
	struct device *dev = mbox->controller.dev;
	u32 ch = nuclei_link2chan(link);
	u32 val;

	val = readl(mbox->regs + MAILBOX_INT_STAT);
	if (val & (MAILBOX_INT_STAT_DONE << (2 * ch))) {
		struct nuclei_mbox_msg msg;

		msg.data = (u8 *)mbox->regs + MAILBOX_SRAM +
					ch * MAILBOX_MSG_SIZE;
		dev_dbg(dev, "Reply %p\n", msg.data);

		msg.ctx = link->con_priv;
		link->con_priv = NULL;

		mbox_chan_received_data(link, &msg);

		/* clear interrupt */
		writel(val, mbox->regs + MAILBOX_INT_STAT);

		/* clear mbox out full */
		val = readl(mbox->regs + MAILBOX_CSR);
		val |= (MAILBOX_CSR_OUT_FULL << (4 * ch));
		writel(val, mbox->regs + MAILBOX_CSR);
		mbox_client_txdone(link, 0);
	}

	return IRQ_HANDLED;
}

static int nuclei_send_data(struct mbox_chan *link, void *data)
{
	struct nuclei_mbox *mbox = nuclei_link_mbox(link);
	u8* mbox_msg_buf;
	u32 ch;
	u32 val;
	struct nuclei_mbox_msg *msg = data;

	ch = nuclei_link2chan(link);
	mbox_msg_buf = (u8 *)mbox->regs + MAILBOX_SRAM + 
				 ch * MAILBOX_MSG_SIZE;
	memcpy((void*)mbox_msg_buf, msg->data, MAILBOX_MSG_SIZE);
	link->con_priv = msg->ctx;

	val = readl(mbox->regs + MAILBOX_CSR);
	val |= MAILBOX_CSR_IN_FULL << (4 * ch);
	writel(val, mbox->regs + MAILBOX_CSR);

	dev_dbg(mbox->controller.dev, "ch%d Request addr:%p\n", ch, msg->data);

	return 0;
}

static int nuclei_startup(struct mbox_chan *link)
{
	u32 ch;
	u32 val;

	struct nuclei_mbox *mbox = nuclei_link_mbox(link);

	ch = nuclei_link2chan(link);

	/* check lockout status TBD */
	val = readl(mbox->regs + MAILBOX_LOCKOUT0 + (ch * 4));
	printk("mailbox%d lockout val:%d\n", ch, val);

	/* check link available */
	val = readl(mbox->regs + MAILBOX_CSR);
	if ((val & (MAILBOX_CSR_UNLINK << (4 * ch))) == LINK_UNAVAILABLE)
		return -EBUSY;

	/* set link */
	val |= MAILBOX_CSR_LINK << (4 * ch);
	writel(val, mbox->regs + MAILBOX_CSR);

	writel(3, mbox->regs + MAILBOX_INT_STAT);

	val = request_irq(mbox->irq, nuclei_mbox_irq,
			  IRQF_SHARED, "nuclei_mbox", link);
	if (val) {
		dev_err(mbox->controller.dev,
			"Unable to request IRQ %d\n", mbox->irq);
		return val;
	}
	/* enable rx interrupt for mailbox ch */
	val = readl(mbox->regs + MAILBOX_INT_EN);
	val &=~(0x3 << (4 * ch));
	val |= MAILBOX_INT_EN_DONE;
	writel(val, mbox->regs + MAILBOX_INT_EN);

	return 0;
}

static void nuclei_shutdown(struct mbox_chan *link)
{
	u32 ch;
	u32 val;
	struct nuclei_mbox *mbox = nuclei_link_mbox(link);

	ch = nuclei_link2chan(link);
	/* set unlink */
	val = readl(mbox->regs + MAILBOX_CSR);
	val |= MAILBOX_CSR_UNLINK << (4 * ch);
	writel(val, mbox->regs + MAILBOX_CSR);

	/* disable rx interrupt for mailbox ch */
	val = readl(mbox->regs + MAILBOX_INT_EN);
	val &=~(0x3 << (4 * ch));
	writel(val, mbox->regs + MAILBOX_INT_EN);
}

static bool nuclei_last_tx_done(struct mbox_chan *link)
{
	struct nuclei_mbox *mbox = nuclei_link_mbox(link);
	u32 val;
	u32 ch; 

	ch = nuclei_link2chan(link);
	val = readl(mbox->regs + MAILBOX_CSR);
	if (val & (MAILBOX_CSR_IN_FULL << (4 * ch)))
		return false;
	else
		return true;
}

static const struct mbox_chan_ops nuclei_mbox_chan_ops = {
	.send_data = nuclei_send_data,
	.startup = nuclei_startup,
	.shutdown = nuclei_shutdown,
	.last_tx_done = nuclei_last_tx_done
};

static struct mbox_chan *nuclei_mbox_index_xlate(struct mbox_controller *mbox,
		    const struct of_phandle_args *sp)
{
	if (sp->args_count != 1)
		return ERR_PTR(-EINVAL);

	return &mbox->chans[0];
}

static int nuclei_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct nuclei_mbox *mbox;

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

	mbox->controller.txdone_poll = true;
	mbox->controller.txpoll_period = 5;
	mbox->controller.ops = &nuclei_mbox_chan_ops;
	mbox->controller.of_xlate = &nuclei_mbox_index_xlate;
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

static const struct of_device_id nuclei_mbox_of_match[] = {
	{ .compatible = "nuclei,nuclei-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, nuclei_mbox_of_match);

static struct platform_driver nuclei_mbox_driver = {
	.driver = {
		.name = "nuclei-mbox",
		.of_match_table = nuclei_mbox_of_match,
	},
	.probe = nuclei_mbox_probe,
};
module_platform_driver(nuclei_mbox_driver);

MODULE_DESCRIPTION("nuclei mailbox IPC driver");
MODULE_LICENSE("GPL v2");

