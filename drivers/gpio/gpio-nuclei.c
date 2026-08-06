// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuclei GPIO controller driver
 *
 * Copyright (C) 2026 Nucleisys, Inc.
 *
 */ 

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/bitops.h>

#define GPIO_REG_IVAL            0x00
#define GPIO_REG_MODE0           0x08
#define GPIO_REG_MODE1           0x0c
#define GPIO_REG_OVAL            0x10
#define GPIO_REG_RISE_IE         0x14
#define GPIO_REG_RISE_IP         0x18
#define GPIO_REG_FALL_IE         0x1c
#define GPIO_REG_FALL_IP         0x20
#define GPIO_REG_HIGH_IE         0x24
#define GPIO_REG_HIGH_IP         0x28
#define GPIO_REG_LOW_IE          0x2c
#define GPIO_REG_LOW_IP          0x30
#define GPIO_REG_OUT_MASK        0x44
#define GPIO_REG_BIT_SET         0x48
#define GPIO_REG_BIT_RESET       0x4c
#define GPIO_REG_BIT_TOGGLE      0x50
#define GPIO_REG_PULL_MODE0      0x54
#define GPIO_REG_PULL_MODE1      0x58
#define GPIO_REG_IRQ_STATUS      0x90

#define NUCLEI_GPIO_NGPIO        32

struct nuclei_gpio {
	void __iomem *base;
	struct gpio_chip gc;
	int parent_irq;
	unsigned int parent_irqs[1];
	spinlock_t lock;
	u8 irq_type[32]; /* per-gpio irq type */
};

static inline u32 nuclei_readl(struct nuclei_gpio *ng, unsigned int reg)
{
	return readl(ng->base + reg);
}

static inline void nuclei_writel(struct nuclei_gpio *ng, unsigned int reg, u32 val)
{
	writel(val, ng->base + reg);
}

struct nuclei_gpio * nuclei_irq_data_get_gpio_data(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	return gpiochip_get_data(chip);
}

static int nuclei_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct nuclei_gpio *ng = gpiochip_get_data(gc);

	if (!test_bit(offset, gc->valid_mask))
		return -EINVAL;

	return !!(nuclei_readl(ng, GPIO_REG_IVAL) & BIT(offset));
}

static void nuclei_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct nuclei_gpio *ng = gpiochip_get_data(gc);
	unsigned long flags;

	if (!test_bit(offset, gc->valid_mask))
		return;

	spin_lock_irqsave(&ng->lock, flags);
	if (value)
		nuclei_writel(ng, GPIO_REG_BIT_SET, BIT(offset));
	else
		nuclei_writel(ng, GPIO_REG_BIT_RESET, BIT(offset));
	spin_unlock_irqrestore(&ng->lock, flags);
}

static int nuclei_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct nuclei_gpio *ng = gpiochip_get_data(gc);
	unsigned long flags;
	u32 m0, m1;

	if (!test_bit(offset, gc->valid_mask))
		return -EINVAL;

	spin_lock_irqsave(&ng->lock, flags);
	m0 = nuclei_readl(ng, GPIO_REG_MODE0);
	m1 = nuclei_readl(ng, GPIO_REG_MODE1);
	/* mode bits {MODE1, MODE0} = 00 -> High-z (input) */
	m0 &= ~BIT(offset);
	m1 &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_MODE0, m0);
	nuclei_writel(ng, GPIO_REG_MODE1, m1);
	spin_unlock_irqrestore(&ng->lock, flags);

	return 0;
}

static int nuclei_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
				     int value)
{
	struct nuclei_gpio *ng = gpiochip_get_data(gc);
	unsigned long flags;
	u32 m0, m1;

	if (!test_bit(offset, gc->valid_mask))
		return -EINVAL;

	nuclei_gpio_set(gc, offset, value);

	spin_lock_irqsave(&ng->lock, flags);
	m0 = nuclei_readl(ng, GPIO_REG_MODE0);
	m1 = nuclei_readl(ng, GPIO_REG_MODE1);
	/* {MODE1,MODE0} = 01 -> push-pull output */
	m0 |= BIT(offset);
	m1 &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_MODE0, m0);
	nuclei_writel(ng, GPIO_REG_MODE1, m1);
	spin_unlock_irqrestore(&ng->lock, flags);

	return 0;
}

static int nuclei_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
			       unsigned long config)
{
	struct nuclei_gpio *ng = gpiochip_get_data(gc);
	enum pin_config_param param = pinconf_to_config_param(config);
	unsigned long flags;
	u32 p0, p1;

	if (!test_bit(offset, gc->valid_mask))
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		spin_lock_irqsave(&ng->lock, flags);
		p0 = nuclei_readl(ng, GPIO_REG_PULL_MODE0);
		p1 = nuclei_readl(ng, GPIO_REG_PULL_MODE1);
		/* pull-up: {PULL_MODE1,PULL_MODE0} = 01 -> PULL_UP=1 */
		p0 |= BIT(offset);
		p1 &= ~BIT(offset);
		nuclei_writel(ng, GPIO_REG_PULL_MODE0, p0);
		nuclei_writel(ng, GPIO_REG_PULL_MODE1, p1);
		spin_unlock_irqrestore(&ng->lock, flags);
		return 0;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		spin_lock_irqsave(&ng->lock, flags);
		p0 = nuclei_readl(ng, GPIO_REG_PULL_MODE0);
		p1 = nuclei_readl(ng, GPIO_REG_PULL_MODE1);
		/* pull-down: {PULL_MODE1,PULL_MODE0} = 11 -> PULL_DOWN=1 */
		p0 |= BIT(offset);
		p1 |= BIT(offset);
		nuclei_writel(ng, GPIO_REG_PULL_MODE0, p0);
		nuclei_writel(ng, GPIO_REG_PULL_MODE1, p1);
		spin_unlock_irqrestore(&ng->lock, flags);
		return 0;
	case PIN_CONFIG_BIAS_DISABLE:
		spin_lock_irqsave(&ng->lock, flags);
		p0 = nuclei_readl(ng, GPIO_REG_PULL_MODE0);
		p1 = nuclei_readl(ng, GPIO_REG_PULL_MODE1);
		/* disable pull: 00 */
		p0 &= ~BIT(offset);
		p1 &= ~BIT(offset);
		nuclei_writel(ng, GPIO_REG_PULL_MODE0, p0);
		nuclei_writel(ng, GPIO_REG_PULL_MODE1, p1);
		spin_unlock_irqrestore(&ng->lock, flags);
		return 0;
	default:
		return -ENOTSUPP;
	}
}

/* IRQ support: simple irq_chip that maps each GPIO to a virtual IRQ.
 * Parent IRQ is shared by all lines; chained handler demuxes by reading pending IP registers.
 */
static void nuclei_irq_mask(struct irq_data *d)
{
	struct nuclei_gpio *ng = nuclei_irq_data_get_gpio_data(d);
	unsigned int offset = d->hwirq;
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&ng->lock, flags);
	/* clear all IE bits for this GPIO */
	r = nuclei_readl(ng, GPIO_REG_RISE_IE);
	r &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_RISE_IE, r);
	r = nuclei_readl(ng, GPIO_REG_FALL_IE);
	r &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_FALL_IE, r);
	r = nuclei_readl(ng, GPIO_REG_HIGH_IE);
	r &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_HIGH_IE, r);
	r = nuclei_readl(ng, GPIO_REG_LOW_IE);
	r &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_LOW_IE, r);
	spin_unlock_irqrestore(&ng->lock, flags);
}

static void nuclei_irq_unmask(struct irq_data *d)
{
	struct nuclei_gpio *ng = nuclei_irq_data_get_gpio_data(d);
	unsigned int offset = d->hwirq;
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&ng->lock, flags);
	/* enable based on stored irq_type */
	if (ng->irq_type[offset] & IRQ_TYPE_EDGE_RISING) {
		r = nuclei_readl(ng, GPIO_REG_RISE_IE);
		r |= BIT(offset);
		nuclei_writel(ng, GPIO_REG_RISE_IE, r);
	}
	if (ng->irq_type[offset] & IRQ_TYPE_EDGE_FALLING) {
		r = nuclei_readl(ng, GPIO_REG_FALL_IE);
		r |= BIT(offset);
		nuclei_writel(ng, GPIO_REG_FALL_IE, r);
	}
	if (ng->irq_type[offset] & IRQ_TYPE_LEVEL_HIGH) {
		r = nuclei_readl(ng, GPIO_REG_HIGH_IE);
		r |= BIT(offset);
		nuclei_writel(ng, GPIO_REG_HIGH_IE, r);
	}
	if (ng->irq_type[offset] & IRQ_TYPE_LEVEL_LOW) {
		r = nuclei_readl(ng, GPIO_REG_LOW_IE);
		r |= BIT(offset);
		nuclei_writel(ng, GPIO_REG_LOW_IE, r);
	}
	spin_unlock_irqrestore(&ng->lock, flags);
}

static int nuclei_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct nuclei_gpio *ng = nuclei_irq_data_get_gpio_data(d);
	unsigned long flags;

	if (type & ~(IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
				 IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		return -EINVAL;

	spin_lock_irqsave(&ng->lock, flags);
	ng->irq_type[d->hwirq] = type;
	spin_unlock_irqrestore(&ng->lock, flags);

	return 0;
}

static void nuclei_irq_ack(struct irq_data *d)
{
	struct nuclei_gpio *ng = nuclei_irq_data_get_gpio_data(d);
	unsigned int offset = d->hwirq;
	unsigned long flags;
	u32 v;

	/* clear pending bits by writing 0 to IP registers */
	spin_lock_irqsave(&ng->lock, flags);
	v = nuclei_readl(ng, GPIO_REG_RISE_IP);
	v &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_RISE_IP, v);
	v = nuclei_readl(ng, GPIO_REG_FALL_IP);
	v &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_FALL_IP, v);
	v = nuclei_readl(ng, GPIO_REG_HIGH_IP);
	v &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_HIGH_IP, v);
	v = nuclei_readl(ng, GPIO_REG_LOW_IP);
	v &= ~BIT(offset);
	nuclei_writel(ng, GPIO_REG_LOW_IP, v);
	spin_unlock_irqrestore(&ng->lock, flags);
}

static struct irq_chip nuclei_irqchip = {
	.name = "nuclei-gpio",
	.irq_mask = nuclei_irq_mask,
	.irq_unmask = nuclei_irq_unmask,
	.irq_set_type = nuclei_irq_set_type,
	.irq_ack = nuclei_irq_ack,
	.flags =  IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void nuclei_gpio_clear_intr_pending(struct nuclei_gpio *ng, u32 status)
{
	u32 val;

	val = nuclei_readl(ng, GPIO_REG_RISE_IP);
	if (val & status) {
		val &= ~status;
		nuclei_writel(ng, GPIO_REG_RISE_IP, val);
	}

	val = nuclei_readl(ng, GPIO_REG_FALL_IP);
	if (val & status) {
		val &= ~status;
		nuclei_writel(ng, GPIO_REG_FALL_IP, val);
	}

	val = nuclei_readl(ng, GPIO_REG_HIGH_IP);
	if (val & status) {
		val &= ~status;
		nuclei_writel(ng, GPIO_REG_HIGH_IP, val);
	}

	val = nuclei_readl(ng, GPIO_REG_LOW_IP);
	if (val & status) {
		val &= ~status;
		nuclei_writel(ng, GPIO_REG_LOW_IP, val);
	}
}

static void nuclei_gpio_irq_handler(struct irq_desc *desc)
{
	struct nuclei_gpio *ng = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long status;
	u32 hw_bit;
	int virq;

	chained_irq_enter(chip, desc);

	status = (unsigned long)nuclei_readl(ng, GPIO_REG_IRQ_STATUS);

	if (ng->gc.valid_mask)
		status &= *(ng->gc.valid_mask);

	for_each_set_bit(hw_bit, &status, ng->gc.ngpio) {
		dev_dbg(ng->gc.parent, "handle irq:%d\n", hw_bit);
		virq = irq_find_mapping(ng->gc.irq.domain, hw_bit);
		generic_handle_domain_irq(ng->gc.irq.domain, virq);
		nuclei_gpio_clear_intr_pending(ng, BIT(hw_bit));
	}

	chained_irq_exit(chip, desc);
}

static int nuclei_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuclei_gpio *ng;
	struct gpio_irq_chip *girq;
	int ret;

	ng = devm_kzalloc(dev, sizeof(*ng), GFP_KERNEL);
	if (!ng)
		return -ENOMEM;

	ng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ng->base))
		return PTR_ERR(ng->base);

	ng->parent_irq = platform_get_irq(pdev, 0);
	if (ng->parent_irq < 0 && ng->parent_irq != -ENXIO)
		return ng->parent_irq;

	spin_lock_init(&ng->lock);

	/* setup gpio_chip */
	ng->gc.label = dev_name(dev);
	ng->gc.parent = dev;
	ng->gc.owner = THIS_MODULE;
	ng->gc.request = gpiochip_generic_request;
	ng->gc.free = gpiochip_generic_free;
	ng->gc.get = nuclei_gpio_get;
	ng->gc.set = nuclei_gpio_set;
	ng->gc.direction_input = nuclei_gpio_direction_input;
	ng->gc.direction_output = nuclei_gpio_direction_output;
	ng->gc.base = -1;
	ng->gc.ngpio = NUCLEI_GPIO_NGPIO;
	ng->gc.can_sleep = false;
	ng->gc.set_config = nuclei_gpio_set_config;
	ng->gc.of_gpio_n_cells = 2;

	if (ng->parent_irq > 0) {
		ng->parent_irqs[0] = ng->parent_irq;
		girq = &ng->gc.irq;
		gpio_irq_chip_set_chip(girq, &nuclei_irqchip);
		girq->parent_handler = nuclei_gpio_irq_handler;
		girq->parent_handler_data = ng;
		girq->num_parents = 1;
		girq->parents = ng->parent_irqs;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
	}

	ret = devm_gpiochip_add_data(dev, &ng->gc, ng);
	if (ret) {
		dev_err(dev, "failed to add gpiochip: %d\n", ret);
		return ret;
	}

	dev_info(dev, "probed, ngpio=%u, mask=0x%x, parent_irq=%d\n",
		 ng->gc.ngpio, ng->gc.valid_mask? *(u32*)(ng->gc.valid_mask) : 0, ng->parent_irq);
	return 0;
}

static const struct of_device_id nuclei_of_match[] = {
	{ .compatible = "nuclei,gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, nuclei_of_match);

static struct platform_driver nuclei_gpio_driver = {
	.probe = nuclei_gpio_probe,
	.driver = {
		.name = "gpio-nuclei",
		.of_match_table = nuclei_of_match,
	},
};

module_platform_driver(nuclei_gpio_driver);

MODULE_DESCRIPTION("Nuclei GPIO driver");
MODULE_LICENSE("GPL v2");
