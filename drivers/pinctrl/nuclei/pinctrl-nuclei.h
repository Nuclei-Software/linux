// SPDX-License-Identifier: GPL-2.0+
/*
 * Core driver for the Nuclei iomux controller
 *
 * Copyright (C) 2026 Nucleisys, Inc.
 *
 * based on imx pinctrl
 */

#ifndef __DRIVERS_PINCTRL_NUCLEI_H
#define __DRIVERS_PINCTRL_NUCLEI_H

#include <linux/pinctrl/pinmux.h>

#define LS_SRC_OVAL_SEL_OFS(grp_id)               (0x0000 + (grp_id) * 0x20000)
#define HS_CHNL_SEL_OFS(grp_id)                   (0x2000 + (grp_id) * 0x20000)
#define LS_SRC_IVAL_SEL_OFS(grp_id)               (0x4000 + (grp_id) * 0x20000)
#define PHY_CNTRL_OFS(grp_id)                     (0x6000 + (grp_id) * 0x20000)
#define CNTRL_SEL_OFS(grp_id)                     (0x8000 + (grp_id) * 0x20000)

#define DO_SEL_OE               BIT(6)
#define DO_SEL_HS               BIT(5)
#define OE_SEL_HS_CHANNEL       BIT(3)
#define IE_SEL                  BIT(0)

#define   PU                    BIT(18)

/*=============================================================================
 * Bitfield Definitions
 *=============================================================================*/
#define PIN_PAD_ID_SHIFT        23
#define PIN_GRP_ID_SHIFT        19
#define PIN_HS_SHIFT            15
#define PIN_IOF_SHIFT           4
#define PIN_DIR_SHIFT           2
#define PIN_RESV_SHIFT          0

#define PIN_PAD_ID_MASK         0x1FF
#define PIN_GRP_ID_MASK         0xF
#define PIN_HS_MASK             0xF
#define PIN_IOF_MASK            0x7FF
#define PIN_DIR_MASK            0x3
#define PIN_RESV_MASK           0x3

#define MUX_PADID(x)            (((x) >> PIN_PAD_ID_SHIFT) & PIN_PAD_ID_MASK)
#define MUX_GRPID(x)            (((x) >> PIN_GRP_ID_SHIFT) & PIN_GRP_ID_MASK)
#define MUX_HS(x)               (((x) >> PIN_HS_SHIFT) & PIN_HS_MASK)
#define MUX_IOF(x)              (((x) >> PIN_IOF_SHIFT) & PIN_IOF_MASK)
#define MUX_DIR(x)              (((x) >> PIN_DIR_SHIFT) & PIN_DIR_MASK)

#define MUX_PACK(padid, grpid, hs, iof, dir) \
	(((padid) << PIN_PAD_ID_SHIFT) | \
	 ((grpid) << PIN_GRP_ID_SHIFT) | \
	 ((hs) << PIN_HS_SHIFT) | \
	 ((iof) << PIN_IOF_SHIFT) | \
	 ((dir) << PIN_DIR_SHIFT))

/* Special PAD_ID value for group wildcard (all PADs in the GRP) */
#define PIN_PAD_ID_GROUP        0x1FF

/*=============================================================================
 * Direction & HS Definitions
 *=============================================================================*/
#define PIN_DIR_INVALID         0
#define PIN_DIR_INPUT           1
#define PIN_DIR_OUTPUT          2
#define PIN_DIR_IO              3

#define PIN_HS_LS               0
#define PIN_HS_HS0              1
#define PIN_HS_HS1              2
#define PIN_HS_HS2              3

/**
 * struct nuclei_pin - describes a single NUCLEI SOC pin
 */
struct nuclei_pin {
	u32 mux;
	/* config bias pull-up or pull-down etc. */
	unsigned long config;
};

/**
 * @dev: a pointer back to containing device
 * @base: the offset to the controller in virtual memory
 */
struct nuclei_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	const struct nuclei_pinctrl_soc_info *info;
	unsigned int group_index;
	struct mutex mutex;
};

struct nuclei_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int flags;

	int (*gpio_set_direction)(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset,
				  bool input);
	int (*nuclei_pinconf_get)(struct pinctrl_dev *pctldev, unsigned int pin_id,
			       unsigned long *config);
	int (*nuclei_pinconf_set)(struct pinctrl_dev *pctldev, unsigned int pin_id,
			       unsigned long *configs, unsigned int num_configs);
	void (*nuclei_pinctrl_parse_pin)(struct nuclei_pinctrl *ipctl,
				      unsigned int *pin_id, struct nuclei_pin *pin,
				      const __be32 **list_p);
};

int nuclei_pinctrl_probe(struct platform_device *pdev,
			const struct nuclei_pinctrl_soc_info *info);

int nuclei_pinctrl_get_grpid(unsigned int padid);
int nuclei_pinctrl_get_gpioiof(unsigned int gpio);

#endif /* __DRIVERS_PINCTRL_NUCLEI_H */
