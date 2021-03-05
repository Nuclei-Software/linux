// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 SiFive
 * Copyright (C) 2019 Nuclei
 * 
 * Modified based on linux/drivers/gpio/gpio-sifive.c
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of_irq.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>

#define NUCLEI_GPIO_INPUT_VAL	0x00
#define NUCLEI_GPIO_INPUT_EN	0x04
#define NUCLEI_GPIO_OUTPUT_EN	0x08
#define NUCLEI_GPIO_OUTPUT_VAL	0x0C
#define NUCLEI_GPIO_RISE_IE	0x18
#define NUCLEI_GPIO_RISE_IP	0x1C
#define NUCLEI_GPIO_FALL_IE	0x20
#define NUCLEI_GPIO_FALL_IP	0x24
#define NUCLEI_GPIO_HIGH_IE	0x28
#define NUCLEI_GPIO_HIGH_IP	0x2C
#define NUCLEI_GPIO_LOW_IE	0x30
#define NUCLEI_GPIO_LOW_IP	0x34
#define NUCLEI_GPIO_OUTPUT_XOR	0x40

#define NUCLEI_GPIO_MAX		32
#define NUCLEI_GPIO_IRQ_OFFSET	1

struct nuclei_gpio {
	void __iomem		*base;
	struct gpio_chip	gc;
	struct regmap		*regs;
	unsigned long		irq_state;
	unsigned int		trigger[NUCLEI_GPIO_MAX];
	unsigned int		irq_parent[NUCLEI_GPIO_MAX];
};

static void nuclei_gpio_set_ie(struct nuclei_gpio *chip, unsigned int offset)
{
	unsigned long flags;
	unsigned int trigger;

	spin_lock_irqsave(&chip->gc.bgpio_lock, flags);
	trigger = (chip->irq_state & BIT(offset)) ? chip->trigger[offset] : 0;
	regmap_update_bits(chip->regs, NUCLEI_GPIO_RISE_IE, BIT(offset),
			   (trigger & IRQ_TYPE_EDGE_RISING) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, NUCLEI_GPIO_FALL_IE, BIT(offset),
			   (trigger & IRQ_TYPE_EDGE_FALLING) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, NUCLEI_GPIO_HIGH_IE, BIT(offset),
			   (trigger & IRQ_TYPE_LEVEL_HIGH) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, NUCLEI_GPIO_LOW_IE, BIT(offset),
			   (trigger & IRQ_TYPE_LEVEL_LOW) ? BIT(offset) : 0);
	spin_unlock_irqrestore(&chip->gc.bgpio_lock, flags);
}

static int nuclei_gpio_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nuclei_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d);

	if (offset < 0 || offset >= gc->ngpio)
		return -EINVAL;

	chip->trigger[offset] = trigger;
	nuclei_gpio_set_ie(chip, offset);
	return 0;
}

static void nuclei_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nuclei_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d) % NUCLEI_GPIO_MAX;
	u32 bit = BIT(offset);
	unsigned long flags;

	irq_chip_enable_parent(d);

	/* Switch to input */
	gc->direction_input(gc, offset);

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	/* Clear any sticky pending interrupts */
	regmap_write(chip->regs, NUCLEI_GPIO_RISE_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_FALL_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_HIGH_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_LOW_IP, bit);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	/* Enable interrupts */
	assign_bit(offset, &chip->irq_state, 1);
	nuclei_gpio_set_ie(chip, offset);
}

static void nuclei_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nuclei_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d) % NUCLEI_GPIO_MAX;

	assign_bit(offset, &chip->irq_state, 0);
	nuclei_gpio_set_ie(chip, offset);
	irq_chip_disable_parent(d);
}

static void nuclei_gpio_irq_eoi(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nuclei_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d) % NUCLEI_GPIO_MAX;
	u32 bit = BIT(offset);
	unsigned long flags;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	/* Clear all pending interrupts */
	regmap_write(chip->regs, NUCLEI_GPIO_RISE_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_FALL_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_HIGH_IP, bit);
	regmap_write(chip->regs, NUCLEI_GPIO_LOW_IP, bit);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	irq_chip_eoi_parent(d);
}

static struct irq_chip nuclei_gpio_irqchip = {
	.name		= "nuclei-gpio",
	.irq_set_type	= nuclei_gpio_irq_set_type,
	.irq_mask	= irq_chip_mask_parent,
	.irq_unmask	= irq_chip_unmask_parent,
	.irq_enable	= nuclei_gpio_irq_enable,
	.irq_disable	= nuclei_gpio_irq_disable,
	.irq_eoi	= nuclei_gpio_irq_eoi,
};

static int nuclei_gpio_child_to_parent_hwirq(struct gpio_chip *gc,
					     unsigned int child,
					     unsigned int child_type,
					     unsigned int *parent,
					     unsigned int *parent_type)
{
	*parent_type = IRQ_TYPE_NONE;
	*parent = child + NUCLEI_GPIO_IRQ_OFFSET;
	return 0;
}

static const struct regmap_config nuclei_gpio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.disable_locking = true,
};

static int nuclei_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *irq_parent;
	struct irq_domain *parent;
	struct gpio_irq_chip *girq;
	struct nuclei_gpio *chip;
	int ret, ngpio;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base)) {
		dev_err(dev, "failed to allocate device memory\n");
		return PTR_ERR(chip->base);
	}

	chip->regs = devm_regmap_init_mmio(dev, chip->base,
					   &nuclei_gpio_regmap_config);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	ngpio = of_irq_count(node);
	if (ngpio > NUCLEI_GPIO_MAX) {
		dev_err(dev, "Too many GPIO interrupts (max=%d)\n",
			NUCLEI_GPIO_MAX);
		return -ENXIO;
	}

	irq_parent = of_irq_find_parent(node);
	if (!irq_parent) {
		dev_err(dev, "no IRQ parent node\n");
		return -ENODEV;
	}
	parent = irq_find_host(irq_parent);
	if (!parent) {
		dev_err(dev, "no IRQ parent domain\n");
		return -ENODEV;
	}

	ret = bgpio_init(&chip->gc, dev, 4,
			 chip->base + NUCLEI_GPIO_INPUT_VAL,
			 chip->base + NUCLEI_GPIO_OUTPUT_VAL,
			 NULL,
			 chip->base + NUCLEI_GPIO_OUTPUT_EN,
			 chip->base + NUCLEI_GPIO_INPUT_EN,
			 0);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}

	/* Disable all GPIO interrupts before enabling parent interrupts */
	regmap_write(chip->regs, NUCLEI_GPIO_RISE_IE, 0);
	regmap_write(chip->regs, NUCLEI_GPIO_FALL_IE, 0);
	regmap_write(chip->regs, NUCLEI_GPIO_HIGH_IE, 0);
	regmap_write(chip->regs, NUCLEI_GPIO_LOW_IE, 0);
	chip->irq_state = 0;

	chip->gc.base = -1;
	chip->gc.ngpio = NUCLEI_GPIO_MAX;
	chip->gc.label = dev_name(dev);
	chip->gc.parent = dev;
	chip->gc.owner = THIS_MODULE;
	girq = &chip->gc.irq;
	girq->chip = &nuclei_gpio_irqchip;
	girq->fwnode = of_node_to_fwnode(node);
	girq->parent_domain = parent;
	girq->child_to_parent_hwirq = nuclei_gpio_child_to_parent_hwirq;
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;

	platform_set_drvdata(pdev, chip);
	return gpiochip_add_data(&chip->gc, chip);
}

static const struct of_device_id nuclei_gpio_match[] = {
	{ .compatible = "nuclei,gpio0" },
	{ },
};

static struct platform_driver nuclei_gpio_driver = {
	.probe		= nuclei_gpio_probe,
	.driver = {
		.name	= "nuclei_gpio",
		.of_match_table = of_match_ptr(nuclei_gpio_match),
	},
};

builtin_platform_driver(nuclei_gpio_driver)
