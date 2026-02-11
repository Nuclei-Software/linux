// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Nucleisys Co., Ltd.
 * Based on imx_rpmsg.c
 */

#include <linux/circ_buf.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include "rpmsg_internal.h"

#define CIRC_ADD(idx, size, value)  (((idx) + (value)) & ((size) - 1))

struct nuclei_virdev {
	struct virtio_device vdev;
	unsigned int vring[2];
	struct virtqueue *vq[2];
	int base_vq_id;
	int num_of_vqs;
	struct nuclei_rpmsg_vproc *rpdev;
};

struct nuclei_rpmsg_vproc {
	struct mbox_client cl;
	struct mbox_chan *tx_ch;
	struct mbox_chan *rx_ch;
	int vdev_nums;
	u32 flags;
#define MAX_VDEV_NUMS  8
	struct nuclei_virdev *virdev[MAX_VDEV_NUMS];
	struct delayed_work rpmsg_work;
	struct circ_buf rx_buffer;
	raw_spinlock_t mu_lock;
	struct platform_device *pdev;
};

#define RPMSG_NUM_BUFS		(512)
#define RPMSG_BUF_SIZE		(512)
#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)
#define RPMSG_VRING_ALIGN	(4096)
#define RPMSG_RING_SIZE	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, \
				RPMSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

#define to_nuclei_virdev(vd) container_of(vd, struct nuclei_virdev, vdev)

/*
 * 1: indicated that remote processor is ready from re-initialization.
 * Clear this bit after the RPMSG restore is finished at master side.
 */
#define REMOTE_IS_READY			BIT(0)
/* 1: Use reserved memory region as DMA pool */
#define SPECIFIC_DMA_POOL		BIT(1)

struct nuclei_rpmsg_vq_info {
	__u16 num;	/* number of entries in the virtio_ring */
	__u16 vq_id;	/* a globaly unique index of this virtqueue */
	void *addr;	/* address where we mapped the virtio ring */
	struct nuclei_rpmsg_vproc *rpdev;
};

static u64 nuclei_rpmsg_get_features(struct virtio_device *vdev)
{
	/* VIRTIO_RPMSG_F_NS has been made private */
	return 1 << 0;
}

static int nuclei_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
	return 0;
}

/* kick the remote processor, and let it know which virtqueue to poke at */
static bool nuclei_rpmsg_notify(struct virtqueue *vq)
{
	int ret;
	unsigned long flags;
	unsigned int mu_rpmsg = 0;
	struct nuclei_rpmsg_vq_info *rpvq = vq->priv;
	struct nuclei_rpmsg_vproc *rpdev = rpvq->rpdev;

	mu_rpmsg = rpvq->vq_id;
	raw_spin_lock_irqsave(&rpdev->mu_lock, flags);
	rpdev->cl.tx_tout = 0;
	ret = mbox_send_message(rpdev->tx_ch, &mu_rpmsg);
	if (ret < 0)
		return false;
	raw_spin_unlock_irqrestore(&rpdev->mu_lock, flags);

	return true;
}

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned int index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name,
				    bool ctx)
{
	struct nuclei_virdev *virdev = to_nuclei_virdev(vdev);
	struct nuclei_rpmsg_vproc *rpdev = virdev->rpdev;
	struct platform_device *pdev = rpdev->pdev;
	struct device *dev = &pdev->dev;
	struct nuclei_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int err;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	rpvq->addr = (__force void *) ioremap_wc(virdev->vring[index],
							RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	memset_io(rpvq->addr, 0, RPMSG_RING_SIZE);

	dev_dbg(dev, "vring%d: phys 0x%x, virt 0x%p\n",
			index, virdev->vring[index], rpvq->addr);

	vq = vring_new_virtqueue(index, RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN,
			vdev, true, ctx,
			rpvq->addr,
			nuclei_rpmsg_notify, callback,
			name);
	if (!vq) {
		dev_err(dev, "vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	virdev->vq[index] = vq;
	vq->priv = rpvq;
	/* system-wide unique id for this virtqueue */
	rpvq->vq_id = virdev->base_vq_id + index;
	rpvq->rpdev = rpdev;

	return vq;

unmap_vring:
	/* iounmap normal memory, so make sparse happy */
	iounmap((__force void __iomem *) rpvq->addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(err);
}

static void nuclei_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct nuclei_rpmsg_vq_info *rpvq = vq->priv;

		iounmap(rpvq->addr);
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}
}

static int nuclei_rpmsg_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char * const names[],
		       const bool *ctx,
		       struct irq_affinity *desc)
{
	struct nuclei_virdev *virdev = to_nuclei_virdev(vdev);
	int i, err;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i],
				ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	virdev->num_of_vqs = nvqs;
	return 0;

error:
	nuclei_rpmsg_del_vqs(vdev);
	return err;
}

static void nuclei_rpmsg_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

static u8 nuclei_rpmsg_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void nuclei_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "%s new status: %d\n", __func__, status);
}

static void nuclei_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static struct virtio_config_ops nuclei_rpmsg_config_ops = {
	.get_features	= nuclei_rpmsg_get_features,
	.finalize_features = nuclei_rpmsg_finalize_features,
	.find_vqs	= nuclei_rpmsg_find_vqs,
	.del_vqs	= nuclei_rpmsg_del_vqs,
	.reset		= nuclei_rpmsg_reset,
	.set_status	= nuclei_rpmsg_set_status,
	.get_status	= nuclei_rpmsg_get_status,
};

static const struct of_device_id nuclei_rpmsg_dt_ids[] = {
	{ .compatible = "nuclei,rpmsg",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nuclei_rpmsg_dt_ids);

static int set_vring_phy_buf(struct platform_device *pdev,
		       struct nuclei_rpmsg_vproc *rpdev, int vdev_nums)
{
	struct resource *res;
	resource_size_t size;
	unsigned int start, end;
	int i, ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		start = res->start;
		end = res->start + size;
		for (i = 0; i < vdev_nums; i++) {
			rpdev->virdev[i] = kzalloc(sizeof(struct nuclei_virdev),
							GFP_KERNEL);
			if (!rpdev->virdev[i])
				return -ENOMEM;

			rpdev->virdev[i]->vring[0] = start;
			rpdev->virdev[i]->vring[1] = start + 0x8000;
			start += 0x10000;
			if (start > end) {
				dev_err(&pdev->dev,
					"Too small memory size %x!\n",
					(u32)size);
				ret = -EINVAL;
				break;
			}
		}
	} else {
		return -ENOMEM;
	}

	return ret;
}

static void rpmsg_work_handler(struct work_struct *work)
{
	u32 message;
	unsigned long flags;
	struct nuclei_virdev *virdev;
	struct delayed_work *dwork = to_delayed_work(work);
	struct nuclei_rpmsg_vproc *rpdev = container_of(dwork,
			struct nuclei_rpmsg_vproc, rpmsg_work);
	struct circ_buf *cb = &rpdev->rx_buffer;
	struct platform_device *pdev = rpdev->pdev;
	struct device *dev = &pdev->dev;

	raw_spin_lock_irqsave(&rpdev->mu_lock, flags);
	/* handle all incoming inter-core message */
	while (CIRC_CNT(cb->head, cb->tail, PAGE_SIZE)) {
		raw_spin_unlock_irqrestore(&rpdev->mu_lock, flags);
		message =  (u32)cb->buf[cb->tail];
		virdev = rpdev->virdev[(message & 0xFF) / 2];

		dev_dbg(dev, "%s msg: 0x%x\n", __func__, message);
		message = message & 0xFF;
		message -= virdev->base_vq_id;

		/*
		 * Currently both PENDING_MSG and explicit-virtqueue-index
		 * messaging are supported.
		 * Whatever approach is taken, at this point message contains
		 * the index of the vring which was just triggered.
		 */
		if (message  < virdev->num_of_vqs)
			vring_interrupt(message, virdev->vq[message]);
		raw_spin_lock_irqsave(&rpdev->mu_lock, flags);
		cb->tail = CIRC_ADD(cb->tail, PAGE_SIZE, 4);
	}
	raw_spin_unlock_irqrestore(&rpdev->mu_lock, flags);
}

static void nuclei_rpmsg_rx_callback(struct mbox_client *c, void *msg)
{
	int buf_space;
	unsigned long flags;
	u32 *data = msg;
	struct nuclei_rpmsg_vproc *rpdev = container_of(c,
			struct nuclei_rpmsg_vproc, cl);
	struct circ_buf *cb = &rpdev->rx_buffer;

	raw_spin_lock_irqsave(&rpdev->mu_lock, flags);
	buf_space = CIRC_SPACE(cb->head, cb->tail, PAGE_SIZE);
	raw_spin_unlock_irqrestore(&rpdev->mu_lock, flags);
	if (unlikely(!buf_space)) {
		dev_err(c->dev, "RPMSG RX overflow!\n");
		return;
	}
	raw_spin_lock_irqsave(&rpdev->mu_lock, flags);
	cb->buf[cb->head] = *data;
	cb->head = CIRC_ADD(cb->head, PAGE_SIZE, 4);
	raw_spin_unlock_irqrestore(&rpdev->mu_lock, flags);

	schedule_delayed_work(&(rpdev->rpmsg_work), 0);
}

static int nuclei_rpmsg_probe(struct platform_device *pdev)
{
	int j, ret = 0;
	char *buf;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct nuclei_rpmsg_vproc *rpdev;
	struct mbox_client *cl;

	buf = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rpdev = devm_kzalloc(dev, sizeof(*rpdev), GFP_KERNEL);
	if (!rpdev)
		return -ENOMEM;

	rpdev->rx_buffer.buf = buf;
	rpdev->rx_buffer.head = 0;
	rpdev->rx_buffer.tail = 0;

	cl = &rpdev->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = false;
	cl->rx_callback = nuclei_rpmsg_rx_callback;

	rpdev->tx_ch = mbox_request_channel(cl, 0);
	if (IS_ERR(rpdev->tx_ch)) {
		ret = PTR_ERR(rpdev->tx_ch);
		goto err_chl;
	}

	raw_spin_lock_init(&rpdev->mu_lock);
	INIT_DELAYED_WORK(&(rpdev->rpmsg_work), rpmsg_work_handler);
	ret = of_property_read_u32(np, "vdev-nums", &rpdev->vdev_nums);
	if (ret)
		rpdev->vdev_nums = 1;
	if (rpdev->vdev_nums > MAX_VDEV_NUMS) {
		dev_err(dev, "vdev-nums exceed the max %d\n", MAX_VDEV_NUMS);
		ret = -EINVAL;
		goto err_chl;
	}

	ret = set_vring_phy_buf(pdev, rpdev, rpdev->vdev_nums);
	if (ret) {
		dev_err(dev, "No vring buffer.\n");
		ret = -ENOMEM;
		goto err_chl;
	}
	if (of_reserved_mem_device_init(dev)) {
		dev_dbg(dev, "dev doesn't have specific DMA pool.\n");
		rpdev->flags &= (~SPECIFIC_DMA_POOL);
	} else {
		rpdev->flags |= SPECIFIC_DMA_POOL;
	}

	for (j = 0; j < rpdev->vdev_nums; j++) {
		dev_dbg(dev, "%s rpdev vdev%d: vring0 0x%x, vring1 0x%x\n",
			 __func__, rpdev->vdev_nums,
			 rpdev->virdev[j]->vring[0],
			 rpdev->virdev[j]->vring[1]);
		rpdev->virdev[j]->vdev.id.device = VIRTIO_ID_RPMSG;
		rpdev->virdev[j]->vdev.config = &nuclei_rpmsg_config_ops;
		rpdev->pdev = pdev;
		rpdev->virdev[j]->vdev.dev.parent = &pdev->dev;
		rpdev->virdev[j]->vdev.dev.release = nuclei_rpmsg_vproc_release;
		rpdev->virdev[j]->base_vq_id = j * 2;
		rpdev->virdev[j]->rpdev = rpdev;

		ret = register_virtio_device(&rpdev->virdev[j]->vdev);
		if (ret) {
			dev_err(dev, "%s failed to register rpdev: %d\n",
					__func__, ret);
			goto err_out;
		}
	}

	return ret;

err_out:
	if (rpdev->flags & SPECIFIC_DMA_POOL)
		of_reserved_mem_device_release(dev);
err_chl:
	if (!IS_ERR(rpdev->tx_ch))
		mbox_free_channel(rpdev->tx_ch);
	return ret;
}

static struct platform_driver nuclei_rpmsg_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "nuclei,nuclei-rpmsg",
		   .of_match_table = nuclei_rpmsg_dt_ids,
		   },
	.probe = nuclei_rpmsg_probe,
};

static int __init nuclei_rpmsg_init(void)
{
	int ret;

	ret = platform_driver_register(&nuclei_rpmsg_driver);
	if (ret)
		pr_err("Unable to initialize rpmsg driver\n");
	else
		pr_info("nuclei rpmsg driver is registered.\n");

	return ret;
}

MODULE_DESCRIPTION("Nuclei remote processor messaging virtio device");
MODULE_LICENSE("GPL v2");
arch_initcall(nuclei_rpmsg_init);
