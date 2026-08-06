// SPDX-License-Identifier: GPL-2.0+
/*
 * Core driver for the Nuclei iomux controller
 *
 * Copyright (C) 2026 Nucleisys, Inc.
 *
 * based on imx pinctrl
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "../pinctrl-utils.h"

#include "pinctrl-nuclei.h"

static inline const struct group_desc *
nuclei_pinctrl_find_group_by_name(struct pinctrl_dev *pctldev,
				  const char *name)
{
	const struct group_desc *grp = NULL;
	int i;

	for (i = 0; i < pctldev->num_groups; i++) {
		grp = pinctrl_generic_get_group(pctldev, i);
		if (grp && !strcmp(grp->name, name))
			return grp;
	}

	return NULL;
}

static void nuclei_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s,
				unsigned offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int nuclei_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map,
				 unsigned *num_maps)
{
	struct nuclei_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	struct nuclei_pin *pin;
	int map_num = 1;
	int i, j;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = nuclei_pinctrl_find_group_by_name(pctldev, np->name);
	if (!grp) {
		dev_err(npctl->dev, "unable to find group for node %pOFn\n",
			np);
		return -EINVAL;
	}

	for (i = 0; i < grp->num_pins; i++)
		map_num++;

	new_map = kmalloc_array(map_num, sizeof(struct pinctrl_map),
				GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		kfree(new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = j = 0; i < grp->num_pins; i++) {
		pin = &((struct nuclei_pin *)(grp->data))[i];

		new_map[j].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[j].data.configs.group_or_pin =
					pin_get_name(pctldev, MUX_PADID(pin->mux));

		new_map[j].data.configs.configs =&pin->config;
		new_map[j].data.configs.num_configs = 1;

		j++;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void nuclei_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map,
			       unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops nuclei_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= nuclei_pin_dbg_show,
	.dt_node_to_map		= nuclei_dt_node_to_map,
	.dt_free_map		= nuclei_dt_free_map,
};

static int nuclei_pmx_set_one_pin_mmio(struct nuclei_pinctrl *npctl,
				    struct nuclei_pin *pin)
{
	u32 data_dir, hs, groupid, padid, iof;
	u32 val;

	data_dir = MUX_DIR(pin->mux);
	hs = MUX_HS(pin->mux);
	groupid = MUX_GRPID(pin->mux);
	padid = MUX_PADID(pin->mux);
	iof = MUX_IOF(pin->mux);

	dev_dbg(npctl->dev,
		"padid:%d,gid:%d,hs:%d,iof:%d,dir:%d\n",
		padid, groupid, hs, iof, data_dir);

	if (data_dir & PIN_DIR_INPUT) {
		if (hs) {
			writel(hs, npctl->base + HS_CHNL_SEL_OFS(groupid) +
			       0x4 * iof);
			val = readl(npctl->base + CNTRL_SEL_OFS(groupid) +
				    0x4 * iof);
			val |= DO_SEL_HS | IE_SEL;
			writel(val, npctl->base + CNTRL_SEL_OFS(groupid) +
			       0x4 * iof);
		} else {
			writel(iof, npctl->base + LS_SRC_OVAL_SEL_OFS(groupid) +
			       0x4 * padid);
			writel(padid, npctl->base + LS_SRC_IVAL_SEL_OFS(groupid) +
			       0x4 * iof);
			val = readl(npctl->base + CNTRL_SEL_OFS(groupid) +
				    0x4 * padid);
			val |= IE_SEL;
			val &= ~OE_SEL_HS_CHANNEL;
			writel(val, npctl->base + CNTRL_SEL_OFS(groupid) +
			       0x4 * padid);
		}
	}

	if (data_dir & PIN_DIR_OUTPUT) {
		if (hs) {
			writel(hs, npctl->base + HS_CHNL_SEL_OFS(groupid) +
			       0x4 * iof);
			val = readl(npctl->base + CNTRL_SEL_OFS(groupid) +
				    0x4 * iof);
			val |= DO_SEL_HS | OE_SEL_HS_CHANNEL;
			writel(val, npctl->base + CNTRL_SEL_OFS(groupid) +
			       0x4 * iof);
		} else {
			writel(iof, npctl->base + LS_SRC_OVAL_SEL_OFS(groupid) +
			       0x4 * padid);
			writel(0, npctl->base + HS_CHNL_SEL_OFS(groupid) +
			       0x4 * padid);
			val = readl(npctl->base + CNTRL_SEL_OFS(groupid) +
				    0x4 * padid);
			val |= DO_SEL_HS | OE_SEL_HS_CHANNEL;
			writel(val, npctl->base + CNTRL_SEL_OFS(groupid) +
			       0x4 * padid);
		}
	}
	return 0;
}

static int nuclei_pmx_set(struct pinctrl_dev *pctldev, unsigned selector,
		       unsigned group)
{
	struct nuclei_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	struct nuclei_pin *pin;
	unsigned int npins;
	int i, err;

	/*
	 * Configure the mux mode for each pin in the group for a specific
	 * function.
	 */
	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	npins = grp->num_pins;

	dev_dbg(npctl->dev, "enable function %s group %s\n",
		func->name, grp->name);

	for (i = 0; i < npins; i++) {
		pin = &((struct nuclei_pin *)(grp->data))[i];
		err = nuclei_pmx_set_one_pin_mmio(npctl, pin);
		if (err)
			return err;
	}

	return 0;
}

static int nuclei_gpio_request_enable(struct pinctrl_dev *pctldev,
		  struct pinctrl_gpio_range *range, unsigned int offset)
{
	struct nuclei_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	struct nuclei_pin tmp_pin;
	unsigned pad_id;
	int grpid, gpioiof;

	if (!npctl || !range) {
		pr_err("Invalid parameters\n");
		return -EINVAL;
	}

	pad_id = offset;

	if (pad_id >= npctl->info->npins) {
		dev_err(npctl->dev, "Invalid pin number: %u\n", offset);
		return -EINVAL;
	}

	tmp_pin.config = 0;
	grpid = nuclei_pinctrl_get_grpid(pad_id);
	gpioiof = nuclei_pinctrl_get_gpioiof(range->id);

	if (grpid < 0) {
		dev_err(npctl->dev, "Invalid pad group for pin %u\n", pad_id);
		return -EINVAL;
	}

	if (gpioiof < 0) {
		dev_err(npctl->dev, "Invalid GPIO IOF for GPIO %u\n", range->id);
		return -EINVAL;
	}

	tmp_pin.mux = MUX_PACK(pad_id, grpid, 0, gpioiof, PIN_DIR_IO);

	nuclei_pmx_set_one_pin_mmio(npctl, &tmp_pin);

	return 0;
}

struct pinmux_ops nuclei_pmx_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = nuclei_pmx_set,
	.gpio_request_enable = nuclei_gpio_request_enable,
	.strict = true,
};

static int nuclei_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned pin_id, unsigned long *config)
{
	return 0;
}

static int nuclei_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned pin_id, unsigned long *configs,
			   unsigned num_configs)
{
	struct nuclei_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	int i;
	u32 val, grp_id;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param;
		u32 arg;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			dev_dbg(npctl->dev, "pin:%d drive strength config\n", pin_id);
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			dev_dbg(npctl->dev, "pin:%d bias disable config\n", pin_id);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:

			dev_dbg(npctl->dev, "pin:%d bias pullup config\n", pin_id);

			grp_id = nuclei_pinctrl_get_grpid(pin_id);
			if (grp_id < 0) {
				dev_err(npctl->dev, "Invalid pin id for bias config: %u\n", pin_id);
				return -EINVAL;
			}
			val = readl(npctl->base + PHY_CNTRL_OFS(grp_id) + 0x4 * pin_id);
			val |= PU;
			writel(val, npctl->base + PHY_CNTRL_OFS(grp_id) + 0x4 * pin_id);

			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			dev_dbg(npctl->dev, "pin:%d bias pulldown config\n", pin_id);
			break;
		case PIN_CONFIG_MAX:
			break;
		default:
			dev_dbg(npctl->dev, "pin conf:%d is not supported\n", param);
			break;
		}
	}

	return 0;
}

static const struct pinconf_ops nuclei_pinconf_ops = {
	.pin_config_get = nuclei_pinconf_get,
	.pin_config_set = nuclei_pinconf_set,
	.is_generic = true,
};

/*
 * Pin mux encoding for "nuclei,pins" property (32-bit):
 *   bits 31:23 - pad_id (0-511, 0x1FF = all pads in group)
 *   bits 22:19 - grp_id (0-7)
 *   bits 18:15 - hs (0=LS, 1=HS0, 2=HS1, 3=HS2)
 *   bits 14:4  - iof (function number, see SoC pinfunc header)
 *   bits 3:2   - dir (1=I, 2=O, 3=IO)
 *   bits 1:0   - reserved (must be zero)
 *
 * all pin mux function is listed in nuclei-t0-pinfunc.h
 */
 
#define NUCLEI_PIN_SIZE 4

static void nuclei_pinctrl_parse_pin_mux(struct nuclei_pinctrl *npctl,
				       unsigned int *pin_id, struct nuclei_pin *pin,
				       const __be32 **list_p,
				       struct device_node *np)
{
	const struct nuclei_pinctrl_soc_info *info = npctl->info;

	const __be32 *list = *list_p;

	u32 raw = be32_to_cpu(*list++);

	if ((MUX_DIR(raw) & PIN_DIR_MASK) == 0) {
		dev_err(npctl->dev, "dts pin config err\n");
		return;
	}

	*pin_id = MUX_PADID(raw);
	pin->mux = raw;

	*list_p = list;

	dev_dbg(npctl->dev, "%s: 0x%px", info->pins[*pin_id].name, pin);
}

static u32 nuclei_pinconf_to_hw_config(enum pin_config_param param,
									u32 arg,
									u32 current_config)
{
	u32 config = current_config;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_DISABLE:
		config |= param;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
	case PIN_CONFIG_DRIVE_STRENGTH:
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
	case PIN_CONFIG_SLEW_RATE:
	case PIN_CONFIG_INPUT_ENABLE:
	default:
		pr_debug("  Unhandled pin config param: %d\n", param);
		break;
	}

	return config;
}

static int nuclei_pinctrl_parse_pin_config(struct device_node *np,
										struct nuclei_pinctrl *npctl,
										unsigned long *config)
{
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	int ret, i;
	u32 hw_config = 0;

	ret = pinconf_generic_parse_dt_config(np, npctl->pctl,
					      &configs, &num_configs);
	if (ret) {
		dev_err(npctl->dev, "Failed to parse pin config for %pOFn: %d\n",
				np, ret);
		return ret;
	}

	if (num_configs == 0) {
		dev_dbg(npctl->dev, "No pin config properties found in %pOFn\n",
			np);
		kfree(configs);
		return 0;
	}

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param;
		u32 arg;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		dev_dbg(npctl->dev, "  Config[%d]: param=%d, arg=%d\n",
				i, param, arg);

		hw_config = nuclei_pinconf_to_hw_config(param, arg, hw_config);
	}

	*config = hw_config;

	dev_dbg(npctl->dev, "  Final hw config: 0x%08x\n", hw_config);

	kfree(configs);
	return 0;
}

static int nuclei_pinctrl_parse_groups(struct device_node *np,
				    struct group_desc *grp,
				    struct nuclei_pinctrl *npctl,
				    u32 index)
{
	struct nuclei_pin *pin;
	int size, pin_size;
	const __be32 *list;
	int i, ret;
	unsigned long group_config = -1;

	dev_dbg(npctl->dev, "group(%d): %pOFn\n", index, np);

	ret = nuclei_pinctrl_parse_pin_config(np, npctl, &group_config);
	if (ret)
		return ret;

	pin_size = NUCLEI_PIN_SIZE;

	/* Initialise group */
	grp->name = np->name;

	list = of_get_property(np, "nuclei,pins", &size);
	if (!list) {
		dev_err(npctl->dev,
			"no nuclei,pins property in node %pOF\n", np);
		return -EINVAL;
	}

	/* we do not check return since it's safe node passed down */
	if (!size || size % pin_size) {
		dev_err(npctl->dev,
			"Invalid nuclei,pins property in node %pOF\n", np);
		return -EINVAL;
	}

	grp->num_pins = size / pin_size;
	grp->data = devm_kcalloc(npctl->dev,
				 grp->num_pins, sizeof(struct nuclei_pin),
				 GFP_KERNEL);
	if (!grp->data)
		return -ENOMEM;

	grp->pins = devm_kcalloc(npctl->dev, grp->num_pins,
					sizeof(unsigned int), GFP_KERNEL);
	if (!grp->pins)
		return -ENOMEM;

	for (i = 0; i < grp->num_pins; i++) {
		pin = &((struct nuclei_pin *)(grp->data))[i];
		pin->config = group_config;
		nuclei_pinctrl_parse_pin_mux(npctl, &grp->pins[i], pin, &list, np);
	}

	return 0;
}

static int nuclei_pinctrl_parse_functions(struct device_node *np,
				       struct nuclei_pinctrl *npctl,
				       u32 index)
{
	struct pinctrl_dev *pctl = npctl->pctl;
	struct device_node *child;
	struct function_desc *func;
	struct group_desc *grp;
	const char **group_names;
	u32 i;

	dev_dbg(pctl->dev, "parse function(%d): %pOFn\n", index, np);

	func = pinmux_generic_get_function(pctl, index);
	if (!func)
		return -EINVAL;

	/* Initialise function */
	func->name = np->name;
	func->num_group_names = of_get_child_count(np);
	if (func->num_group_names == 0) {
		dev_info(npctl->dev, "no groups defined in %pOF\n", np);
		return -EINVAL;
	}

	group_names = devm_kcalloc(npctl->dev, func->num_group_names,
				   sizeof(char *), GFP_KERNEL);
	if (!group_names)
		return -ENOMEM;
	i = 0;
	for_each_child_of_node(np, child)
		group_names[i++] = child->name;
	func->group_names = group_names;

	i = 0;
	for_each_child_of_node(np, child) {
		grp = devm_kzalloc(npctl->dev, sizeof(struct group_desc),
				   GFP_KERNEL);
		if (!grp) {
			of_node_put(child);
			return -ENOMEM;
		}

		mutex_lock(&npctl->mutex);
		radix_tree_insert(&pctl->pin_group_tree,
				  npctl->group_index++, grp);
		mutex_unlock(&npctl->mutex);

		nuclei_pinctrl_parse_groups(child, grp, npctl, i++);
	}

	return 0;
}

/*
 * Check if the DT contains pins in the direct child nodes. This indicates the
 * newer DT format to store pins. This function returns true if the first found
 * nuclei,pins property is in a child of np. Otherwise false is returned.
 */
static bool nuclei_pinctrl_dt_is_flat_functions(struct device_node *np)
{
	struct device_node *function_np;
	struct device_node *pinctrl_np;

	for_each_child_of_node(np, function_np) {
		if (of_property_read_bool(function_np, "nuclei,pins")) {
			of_node_put(function_np);
			return true;
		}

		for_each_child_of_node(function_np, pinctrl_np) {
			if (of_property_read_bool(pinctrl_np, "nuclei,pins")) {
				of_node_put(pinctrl_np);
				of_node_put(function_np);
				return false;
			}
		}
	}

	return true;
}

static int nuclei_pinctrl_probe_dt(struct platform_device *pdev,
				struct nuclei_pinctrl *npctl)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct pinctrl_dev *pctl = npctl->pctl;
	u32 nfuncs = 0;
	u32 i = 0;
	bool flat_funcs;

	if (!np)
		return -ENODEV;

	flat_funcs = nuclei_pinctrl_dt_is_flat_functions(np);
	if (flat_funcs) {
		nfuncs = 1;
	} else {
		nfuncs = of_get_child_count(np);
		if (nfuncs == 0) {
			dev_err(&pdev->dev, "no functions defined\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < nfuncs; i++) {
		struct function_desc *function;

		function = devm_kzalloc(&pdev->dev, sizeof(*function),
					GFP_KERNEL);
		if (!function)
			return -ENOMEM;

		mutex_lock(&npctl->mutex);
		radix_tree_insert(&pctl->pin_function_tree, i, function);
		mutex_unlock(&npctl->mutex);
	}
	pctl->num_functions = nfuncs;

	npctl->group_index = 0;
	if (flat_funcs) {
		pctl->num_groups = of_get_child_count(np);
	} else {
		pctl->num_groups = 0;
		for_each_child_of_node(np, child)
			pctl->num_groups += of_get_child_count(child);
	}

	if (flat_funcs) {
		nuclei_pinctrl_parse_functions(np, npctl, 0);
	} else {
		i = 0;
		for_each_child_of_node(np, child)
			nuclei_pinctrl_parse_functions(child, npctl, i++);
	}

	return 0;
}

int nuclei_pinctrl_probe(struct platform_device *pdev,
		      const struct nuclei_pinctrl_soc_info *info)
{
	struct pinctrl_desc *nuclei_pinctrl_desc;
	struct nuclei_pinctrl *npctl;
	int ret;

	if (!info || !info->pins || !info->npins) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	/* Create state holders etc for this driver */
	npctl = devm_kzalloc(&pdev->dev, sizeof(*npctl), GFP_KERNEL);
	if (!npctl)
		return -ENOMEM;

	npctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(npctl->base))
		return PTR_ERR(npctl->base);

	nuclei_pinctrl_desc = devm_kzalloc(&pdev->dev,
					   sizeof(*nuclei_pinctrl_desc),
					   GFP_KERNEL);
	if (!nuclei_pinctrl_desc)
		return -ENOMEM;

	nuclei_pinctrl_desc->name = dev_name(&pdev->dev);
	nuclei_pinctrl_desc->pins = info->pins;
	nuclei_pinctrl_desc->npins = info->npins;
	nuclei_pinctrl_desc->pctlops = &nuclei_pctrl_ops;
	nuclei_pinctrl_desc->pmxops = &nuclei_pmx_ops;
	nuclei_pinctrl_desc->confops = &nuclei_pinconf_ops;
	nuclei_pinctrl_desc->owner = THIS_MODULE;

	/* platform specific callback */
	nuclei_pmx_ops.gpio_set_direction = info->gpio_set_direction;

	mutex_init(&npctl->mutex);

	npctl->info = info;
	npctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, npctl);
	ret = devm_pinctrl_register_and_init(&pdev->dev,
					     nuclei_pinctrl_desc, npctl,
					     &npctl->pctl);
	if (ret) {
		dev_err(&pdev->dev, "could not register NUCLEI pinctrl driver\n");
		return ret;
	}

	ret = nuclei_pinctrl_probe_dt(pdev, npctl);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		return ret;
	}

	dev_info(&pdev->dev, "initialized NUCLEI pinctrl driver\n");

	return pinctrl_enable(npctl->pctl);
}
EXPORT_SYMBOL_GPL(nuclei_pinctrl_probe);

static int __maybe_unused nuclei_pinctrl_suspend(struct device *dev)
{
	struct nuclei_pinctrl *npctl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(npctl->pctl);
}

static int __maybe_unused nuclei_pinctrl_resume(struct device *dev)
{
	struct nuclei_pinctrl *npctl = dev_get_drvdata(dev);

	return pinctrl_force_default(npctl->pctl);
}

const struct dev_pm_ops nuclei_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(nuclei_pinctrl_suspend,
					nuclei_pinctrl_resume)
};
EXPORT_SYMBOL_GPL(nuclei_pinctrl_pm_ops);

MODULE_DESCRIPTION("NUCLEI common pinctrl driver");
MODULE_LICENSE("GPL v2");
