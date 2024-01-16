// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * @Copyright 2023 Nucleisys corp.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#define MHZ (1000 * 1000)

/* Common bitfield definitions for PLL (lock)*/
#define PLL_CTRL_N_SHIFT        0
#define PLL_CTRL_N_MASK         (0xFF << PLL_CTRL_N_SHIFT)
#define PLL_CTRL_M_SHIFT        8
#define PLL_CTRL_M_MASK         (0x3FF << PLL_CTRL_M_SHIFT)
#define PLL_CTRL_OD_SHIFT       18
#define PLL_CTRL_OD_MASK        (0x3F << PLL_CTRL_OD_SHIFT)
#define PLL_CTRL_BP_SHIFT       24
#define PLL_CTRL_BP_MASK        (1 << PLL_CTRL_BP_SHIFT)
#define PLL_CTRL_LOCK_SHIFT     25
#define PLL_CTRL_LOCK_MASK      (1 << PLL_CTRL_LOCK_SHIFT)

#define PLL_CTRL_PWR_ON_MASK    (1 << 1)
#define PLL_CTRL_CG_ON_MASK     (1 << 2)
#define PLL_CTRL_ON_MASK        (1 << 3)

/* Clock registers on System Control Block */
/* pll configure register */
#define NUCLEI_PLL_CTRL0_SYS_CLK 0x78
#define NUCLEI_PLL_CTRL1_XEC_CLK 0x7C
#define NUCLEI_PLL_CTRL2_XDC_CLK 0x80
#define NUCLEI_PLL_CTRL3_XDC_BAK_CLK 0x84
#define NUCLEI_PLL_CTRL4_SYS_BAK_CLK 0x88
#define NUCLEI_PLL_CTRL5_XUC_CLK 0x8C

#define NUCLEI_PLL_MISC_CTRL4 0xC90
#define NUCLEI_PLL_MISC_CTRL5 0xC94
#define NUCLEI_PLL_MISC_CTRL6 0xC98
#define NUCLEI_PLL_MISC_CTRL7 0xC9C
#define NUCLEI_PLL_MISC_CTRL8 0xCA0

/* clock gate control register defination */
#define NUCLEI_GATE_CTRL0_CLK 0x40
#define NUCLEI_GATE_CTRL1_CLK 0x44
#define NUCLEI_GATE_CTRL2_CLK 0x48
#define NUCLEI_GATE_CTRL3_CLK 0x4C
#define NUCLEI_GATE_CTRL4_CLK 0x50
#define NUCLEI_GATE_CTRL5_CLK 0x54
#define NUCLEI_GATE_CTRL6_CLK 0x58
#define NUCLEI_GATE_CTRL7_CLK 0x5C

/* mux select control register defination */
#define NUCLEI_MUX_CTRL0_SYS_CLK 0x100
#define NUCLEI_MUX_CTRL1_XDC_CLK 0x104
#define NUCLEI_MUX_CTRL2_XUC_CLK 0x108
#define NUCLEI_MUX_CTRL3_CPU_CLK 0x10c
#define NUCLEI_MUX_CTRL4_DDR_FAB_CLK 0x110
#define NUCLEI_MUX_CTRL5_SAI_S_CLK 0x114
#define NUCLEI_MUX_CTRL6_DISP_CLK 0x118
#define NUCLEI_MUX_CTRL25_SDIO_CLK 0x164
#define NUCLEI_MUX_CTRL29_DDR_TOP0_CLK 0x174
#define NUCLEI_MUX_CTRL30_USB_TOP0_CLK 0x178
#define NUCLEI_MUX_CTRL32_USB_TOP0_CORE_CLK 0x180
#define NUCLEI_MUX_CTRL35_XUC0_CLK 0x18C
#define NUCLEI_MUX_CTRL38_XEC_CLK 0x198

/* dividor control register defination */
#define NUCLEI_DIV_CTRL7_DDR_FAB_CLK 0x11C
#define NUCLEI_DIV_CTRL8_MAIN_FAB_CLK 0x120
#define NUCLEI_DIV_CTRL9_I2C2ICB0_CLK 0x124
#define NUCLEI_DIV_CTRL10_UART0_CLK 0x128
#define NUCLEI_DIV_CTRL11_I2C0_CLK 0x12C
#define NUCLEI_DIV_CTRL12_QSPI1_CLK 0x130
#define NUCLEI_DIV_CTRL13_QSPI2_CLK 0x134
#define NUCLEI_DIV_CTRL14_QSPI_XIP0_CLK 0x138
#define NUCLEI_DIV_CTRL15_GPIO0_CLK 0x13C
#define NUCLEI_DIV_CTRL16_RTC0_CLK 0x140
#define NUCLEI_DIV_CTRL17_PMU_CLK 0x144
#define NUCLEI_DIV_CTRL18_GMC0_CLK 0x148
#define NUCLEI_DIV_CTRL19_DISP0_CLK 0x14C
#define NUCLEI_DIV_CTRL20_DISP_PIX_CLK 0x150
#define NUCLEI_DIV_CTRL21_IOMUX_CLK 0x154
#define NUCLEI_DIV_CTRL22_SAI0_CLK 0x158
#define NUCLEI_DIV_CTRL23_SAI0_S0_CLK 0x15C
#define NUCLEI_DIV_CTRL24_SAI0_S1_CLK 0x160
#define NUCLEI_DIV_CTRL26_SDIO0_CLK 0x168
#define NUCLEI_DIV_CTRL27_SDIO0_DATA_CLK 0x16C
#define NUCLEI_DIV_CTRL28_DLYB_ANA0_CLK 0x170
#define NUCLEI_DIV_CTRL31_USB_TOP0_CLK 0x17C
#define NUCLEI_DIV_CTRL33_USB_TOP0_CLKCORE_CLK 0x184
#define NUCLEI_DIV_CTRL36_XUC0_CLK 0x190
#define NUCLEI_DIV_CTRL37_XUC0_PHY_CLK 0x194
#define NUCLEI_DIV_CTRL39_RMII_REF_CLK 0x19C
#define NUCLEI_DIV_CTRL40_XEC0_CLK 0x1A0
#define NUCLEI_DIV_CTRL41_XEC1_CLK 0x1A4
#define NUCLEI_DIV_CTRL42_PTP_REF_CLK 0x1A8

#define CLK_VADDR(_x) (nuclei_clk_base + (_x))
#define to_nuclei_pll(_hw) container_of(_hw, struct nuclei_pll, hw)

static void __iomem *nuclei_clk_base;

enum nuclei_clk_ctrl0_field{
	CLK_GATE_DMA0=0,
	CLK_GATE_UART0,
	CLK_GATE_QSPI_XIP0,
	CLK_GATE_QSPI1,
	CLK_GATE_QSPI2,
	CLK_GATE_RTC0,
	CLK_GATE_I2C2ICB0,
	CLK_GATE_LGPIO,
	CLK_GATE_SDIO0,
	CLK_GATE_SDIO0_DMA,
	CLK_GATE_DLYB_ANA0,
	CLK_GATE_GMC0,
	CLK_GATE_XUC0,
	CLK_GATE_USB_TOP0,
	CLK_GATE_XEC_GEN20,
	CLK_GATE_XEC_GEN21,
	CLK_GATE_DDR_TOP0,
	CLK_GATE_IOMUX=18,
	CLK_GATE_SAI0,
	CLK_GATE_DISP0,
	CLK_GATE_I2C0,
	CLK_GATE_ATB2AXI0,
};

struct nuclei_pll_rate_table {
	unsigned int rate;
	unsigned int n_val;
	unsigned int m_val;
	unsigned int od_val;
};

struct nuclei_pll {
	struct clk_hw hw;
	void __iomem *pll_ctrl;
	void __iomem *pll_status;
	spinlock_t *lock;
	const struct nuclei_pll_rate_table *rate_table;
	int rate_count;
};

enum nuclei_clk {
	/* clk for device */
	cpu_clk_i=0,
	ddr_fab_clk_i_cg,
	main_fab_clk_i,
	ddr_top0_clk,
	disp0_clk_i_cg,
	disp_pix_clk_cg,
	gmc0_clk_i_cg,
	i2c2icb_clk_i_cg,
	iomux0_clk_i_cg,
	lgpio0_clk_i_cg,
	pmu_clk_i_cg,
	qspi_xip0_clk_i_cg,
	qspi1_clk_i_cg,
	qspi2_clk_i_cg,
	rtc0_clk_i_cg,
	sai0_clk_i_cg,
	sdio0_clk_i_cg,
	sdio0_data_clk_i,
	i2c0_clk_i_cg,
	usart0_clk_i_cg,
	udma0_clk_i_cg,
	rmii_ref_clk_i_cg,
	ptp_ref_clk_i_cg,
	xec0_clk_i_cg,
	xec1_clk_i_cg,
	xuc0_clk_i_cg,
	xuc_clk_phy_i_cg,
	usb_top0_clkcore_i_cg,
	usb_top0_clk_i_cg,
	/* system clocks */
	sys_clk_pll,
	sys_clk_bk_pll,
	xec_clk_pll,
	xdc_clk_pll, 
	xdc_clk_bk_pll, 
	xuc_clk_pll,
	/*mux clk*/
	sys_clk_in, 
	xdc_clk_in, 
	xuc_clk_in,
	ddr_fab_clk,
	sai_s_clk,
	disp_clk, 
	sdio_clk,
	xec_clk, 
	xuc0_clk, 
	usb_top0_clkcore, 
	usb_top0_clk,
	/*dividor clk*/
	ddr_fab_clk_i,
	sai0_s0_clk,
	sai0_s1_clk,
	xec0_clk_i,
	xec1_clk_i, 
	rmii_ref_clk_i, 
	ptp_ref_clk_i,
	xuc0_clk_i, 
	xuc_clk_phy_i, 
	usb_top0_CLKCORE_i,
	usb_top0_clk_i, 
	disp0_clk_i, 
	gmc0_clk_i,
	i2c2icb_clk_i, 
	iomux0_clk_i, 
	lgpio0_clk_i,
	pmu_clk_i, 
	qspi_xip0_clk_i, 
	qspi1_clk_i,
	qspi2_clk_i, 
	rtc0_clk_i, 
	sai0_clk_i,
	sai1_clk_i, 
	sdio0_clk_i, 
	i2c0_clk_i,
	usart0_clk_i,
	dlyb_ana0_clk_i, 
	atb2axi0_clk_i,
	clk_max
};

static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;

static const char *const sys_clk_in_parents[] __initconst = {
	"osc_clk_16m","osc_clk_25m"};
static const char *const xuc_clk_in_parents[] __initconst = {
	"osc_clk_16m","osc_clk_24m"};
static const char *const ddr_fab_clk_parents[] __initconst = {
	"osc_clk_25m", "sys_clk_pll", "sys_clk_bk_pll",
	"xec_clk_pll", "xuc_clk_pll", "pad_clk_25m", "pad_clk_48m"};

#define cpu_clk_parents ddr_fab_clk_parents
#define sai_s_clk_parents ddr_fab_clk_parents
#define disp_clk_parents ddr_fab_clk_parents

static const char *const sdio_clk_parents[] __initconst = {
	"osc_clk_25m", "sys_clk_pll", "sys_clk_bk_pll",
	"xec_clk_pll", "xuc_clk_pll"};

static const char *const ddr_top0_clk_parents[] __initconst = {
	"osc_clk_25m", "xdc_clk_pll", "xdc_clk_bk_pll"};
	
static const char *const xec_clk_parents[] __initconst = {
	"osc_clk_25m", "xec_clk_pll", "xuc_clk_pll",
	"sys_clk_pll", "sys_clk_bk_pll", "pad_clk_25m"};

static const char *const xuc0_clk_parents[] __initconst = {
	"osc_clk_24m", "xuc_clk_pll", "sys_clk_pll",
	"sys_clk_bk_pll", "xec_clk_pll", "pad_clk_48m"};

static const char *const usb_top0_core_clk_parents[] __initconst = {
	"osc_clk_24m", "xuc_clk_pll", "pad_clk_48m"};

#define usb_top0_clk_parents xuc0_clk_parents

#define PLL_RATE(_fin, _n, _m, _od) \
	((u64)(_fin) * ((_m)/(_n)/(2<<(_od))))
#define PLL_VALID_RATE(_fin, _fout, _n, _m, _od) ((_fout) + \
	BUILD_BUG_ON_ZERO(PLL_RATE(_fin, _n, _m, _od) != (u64)(_fout)))

#define PLL_NUCLEI_PLL_RATE(_fin, _rate, _n, _m, _od)		\
	{							\
		.rate	=	PLL_RATE(_fin, _n, _m, _od),	\
		.n_val	=	(_n),				\
		.m_val	=	(_m),				\
		.od_val	=	(_od),				\
	}

static struct nuclei_pll_rate_table sys_clk_pll_tbl[] =  {
	PLL_NUCLEI_PLL_RATE(16 * MHZ, 1600 * MHZ, 2, 200, 0),
	PLL_NUCLEI_PLL_RATE(16 * MHZ, 800 * MHZ, 2, 200, 1),
	PLL_NUCLEI_PLL_RATE(16 * MHZ, 600 * MHZ, 2, 150, 1),
	PLL_NUCLEI_PLL_RATE(16 * MHZ, 400 * MHZ, 4, 200, 1),
	PLL_NUCLEI_PLL_RATE(16 * MHZ, 100 * MHZ, 4, 200, 3),
	{ },
};

static struct nuclei_pll_rate_table xec_clk_pll_tbl[] =  {
	/*TBD*/
	{ },
};

static struct nuclei_pll_rate_table xuc_clk_pll_tbl[] =  {
	/*TBD*/
	{ },
};

static struct nuclei_pll_rate_table xdc_clk_pll_tbl[] =  {
	/*TBD*/
	{ },
};

static DEFINE_SPINLOCK(sys_clk_pll_lock);
static DEFINE_SPINLOCK(xec_clk_pll_lock);
static DEFINE_SPINLOCK(xuc_clk_pll_lock);
static DEFINE_SPINLOCK(xdc_clk_pll_lock);
static DEFINE_SPINLOCK(ddr_fab_clk_lock);
static DEFINE_SPINLOCK(ddr_top0_clk_lock);
static DEFINE_SPINLOCK(cpu_clk_lock);
static DEFINE_SPINLOCK(sai_s_clk_lock);
static DEFINE_SPINLOCK(disp_clk_lock);
static DEFINE_SPINLOCK(sdio_clk_lock);
static DEFINE_SPINLOCK(sys_clk_in_lock);
static DEFINE_SPINLOCK(xdc_clk_in_lock);
static DEFINE_SPINLOCK(xuc_clk_in_lock);
static DEFINE_SPINLOCK(main_fab_clk_lock);

/**
 * nuclei_pll_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long nuclei_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct nuclei_pll *pll = to_nuclei_pll(hw);
	const struct nuclei_pll_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++) {
		if (rate >= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

static const struct nuclei_pll_rate_table *nuclei_get_pll_settings(
				struct nuclei_pll *pll, unsigned long rate)
{
	const struct nuclei_pll_rate_table  *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}

	return NULL;
}

static int nuclei_pll_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long prate)
{
	struct nuclei_pll *pll = to_nuclei_pll(hw);
	const struct nuclei_pll_rate_table *rate_setting;

	/* Get required rate settings from table */
	rate_setting = nuclei_get_pll_settings(pll, rate);
	if (!rate_setting) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			rate, clk_hw_get_name(hw));
		return -EINVAL;
	}
	/*
	 * we need to deal with many case, because pll changes
	 * may have an effect on other master controller, 
	 */
	/* TBD*/
	return 0;
}

/**
 * nuclei_pll_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long nuclei_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct nuclei_pll *clk = to_nuclei_pll(hw);
	u32 m_val;
	u32 n_val;
	u32 od_val;

	n_val = (readl(clk->pll_ctrl) & PLL_CTRL_N_MASK) >> PLL_CTRL_N_SHIFT;
	m_val = (readl(clk->pll_ctrl) & PLL_CTRL_M_MASK) >> PLL_CTRL_M_SHIFT;
	od_val = (readl(clk->pll_ctrl) & PLL_CTRL_OD_MASK) >> PLL_CTRL_OD_SHIFT;

	return parent_rate * (m_val/n_val/(1<<od_val));
}

/**
 * nuclei_pll_is_enabled - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 1 if the clock is enabled, 0 otherwise.
 *
 * Not sure this is a good idea, but since disabled means bypassed for
 * this clock implementation we say we are always enabled.
 */
static int nuclei_pll_is_enabled(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	u32 ret = false;
	struct nuclei_pll *clk = to_nuclei_pll(hw);

	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->pll_status);
	if ((reg & PLL_CTRL_PWR_ON_MASK) && (reg & PLL_CTRL_CG_ON_MASK) &&
		(reg & PLL_CTRL_ON_MASK)) {
		reg = readl(clk->pll_ctrl);
		if ((reg & PLL_CTRL_BP_MASK) == 0)
			ret = true;
	}

	spin_unlock_irqrestore(clk->lock, flags);

	return ret;
}

/**
 * nuclei_pll_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static int nuclei_pll_enable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct nuclei_pll *clk = to_nuclei_pll(hw);

	if (nuclei_pll_is_enabled(hw))
		return 0;

	pr_info("PLL: enable\n");

	/* Power up PLL and wait for lock */
	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->pll_status);
	reg |= PLL_CTRL_PWR_ON_MASK | PLL_CTRL_CG_ON_MASK | PLL_CTRL_ON_MASK;
	writel(reg, clk->pll_status);

	reg = readl(clk->pll_ctrl);
	reg &= ~PLL_CTRL_BP_MASK;
	writel(reg, clk->pll_ctrl);

	while (!(readl(clk->pll_ctrl) & PLL_CTRL_LOCK_MASK))
		;
	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * nuclei_pll_disable - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static void nuclei_pll_disable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct nuclei_pll *clk = to_nuclei_pll(hw);

	if (!nuclei_pll_is_enabled(hw))
		return;

	pr_info("PLL: shutdown\n");

	/* shut down PLL */
	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->pll_status);
	reg &= ~(PLL_CTRL_PWR_ON_MASK | PLL_CTRL_CG_ON_MASK | PLL_CTRL_ON_MASK);
	writel(reg, clk->pll_status);

	reg = readl(clk->pll_ctrl);
	reg |= PLL_CTRL_BP_MASK;
	writel(reg, clk->pll_ctrl);

	spin_unlock_irqrestore(clk->lock, flags);
}

static const struct clk_ops nuclei_pll_ops = {
	.enable = nuclei_pll_enable,
	.disable = nuclei_pll_disable,
	.is_enabled = nuclei_pll_is_enabled,
	.round_rate = nuclei_pll_round_rate,
	.set_rate = nuclei_pll_set_rate,
	.recalc_rate = nuclei_pll_recalc_rate
};

/**
 * clk_register_nuclei_pll() - Register PLL with the clock framework
 * @name	PLL name
 * @parent	Parent clock name
 * @pll_ctrl	Pointer to PLL control register
 * @lock	Register lock
 * Returns handle to the registered clock.
 */
struct clk *clk_register_nuclei_pll(const char *name, const char *parent,
		void __iomem *pll_ctrl, void __iomem *pll_status, spinlock_t *lock,
		struct nuclei_pll_rate_table *rate_table)
{
	struct nuclei_pll *pll;
	struct clk *clk;
	const char *parent_arr[1] = {parent};
	struct clk_init_data initd = {
		.name = name,
		.parent_names = parent_arr,
		.ops = &nuclei_pll_ops,
		.num_parents = 1,
		.flags = 0
	};
	u32 len;

	for (len = 0; rate_table[len].rate != 0; )
		len++;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/* Populate the struct */
	pll->hw.init = &initd;
	pll->pll_ctrl = pll_ctrl;
	pll->pll_status = pll_status;
	pll->lock = lock;
	pll->rate_table = rate_table;
	pll->rate_count = len;

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		goto free_pll;

	return clk;

free_pll:
	kfree(pll);

	return clk;
}

static void __init nuclei_clk_setup(struct device_node *np)
{
	int i;

	pr_info("nuclei clock init\n");

	nuclei_clk_base = of_iomap(np, 0);
	if (!nuclei_clk_base) {
		pr_err("failed to map system control block registers\n");
		return;
	}

	clks[sys_clk_in] = clk_register_mux(NULL, "sys_clk_in",
			sys_clk_in_parents, 2, CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL0_SYS_CLK), 16, 1, 0, &sys_clk_in_lock);
	clks[xdc_clk_in] = clk_register_mux(NULL, "xdc_clk_in",
			sys_clk_in_parents, 2, CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL1_XDC_CLK), 16, 1, 0, &xdc_clk_in_lock);
	clks[xuc_clk_in] = clk_register_mux(NULL, "xuc_clk_in",
			xuc_clk_in_parents, 2, CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL2_XUC_CLK), 16, 1, 0, &xuc_clk_in_lock);
	/* PLLs */
	clks[sys_clk_pll] = clk_register_nuclei_pll("sys_clk_pll", "sys_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL0_SYS_CLK), 
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL4),
						&sys_clk_pll_lock, &sys_clk_pll_tbl[0]);

	clks[sys_clk_bk_pll] = clk_register_nuclei_pll("sys_clk_bk_pll", "sys_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL4_SYS_BAK_CLK), 
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL4),
						&sys_clk_pll_lock, &sys_clk_pll_tbl[0]);

	clks[xec_clk_pll] = clk_register_nuclei_pll("xec_clk_pll", "sys_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL1_XEC_CLK),
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL5),
						&xec_clk_pll_lock, &xec_clk_pll_tbl[0]);

	clks[xdc_clk_pll] = clk_register_nuclei_pll("xdc_clk_pll", "xdc_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL2_XDC_CLK), 
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL7),
						&xdc_clk_pll_lock, &xdc_clk_pll_tbl[0]);

	clks[xdc_clk_bk_pll] = clk_register_nuclei_pll("xdc_clk_bk_pll", "xdc_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL3_XDC_BAK_CLK), 
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL8), 
						&xdc_clk_pll_lock, &xdc_clk_pll_tbl[0]);

	clks[xuc_clk_pll] = clk_register_nuclei_pll("xuc_clk_pll", "xuc_clk_in", 
						CLK_VADDR(NUCLEI_PLL_CTRL5_XUC_CLK),
						CLK_VADDR(NUCLEI_PLL_MISC_CTRL6),
						&xuc_clk_pll_lock, &xuc_clk_pll_tbl[0]);
	/* ddr bus clk */
	clks[ddr_fab_clk] = clk_register_mux(NULL, "ddr_fab_clk",
			ddr_fab_clk_parents, ARRAY_SIZE(ddr_fab_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL4_DDR_FAB_CLK), 16, 3, 0, &ddr_fab_clk_lock);
	clks[ddr_fab_clk_i] = clk_register_divider(NULL, "ddr_fab_clk_i", "ddr_fab_clk", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL7_DDR_FAB_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &ddr_fab_clk_lock);
	clks[ddr_fab_clk_i_cg] = clk_register_gate(NULL, "ddr_fab_clk_i_cg",
			"ddr_fab_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_DDR_TOP0, 0, &ddr_fab_clk_lock);

	/* ddr top0 clk */
	clks[ddr_top0_clk] = clk_register_mux(NULL, "ddr_top0_clk",
			ddr_top0_clk_parents, ARRAY_SIZE(ddr_top0_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL29_DDR_TOP0_CLK), 16, 2, 0, &ddr_top0_clk_lock);

	/* main fab clk */
	clks[main_fab_clk_i] = clk_register_divider(NULL, "main_fab_clk_i", "ddr_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL8_MAIN_FAB_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);

	/* disp bus clk */
	clks[disp0_clk_i] = clk_register_divider(NULL, "disp0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL19_DISP0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[disp0_clk_i_cg] = clk_register_gate(NULL, "disp0_clk_i_cg",
			"disp0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_DISP0, 0, &main_fab_clk_lock);
	/* gmc clk */
	clks[gmc0_clk_i] = clk_register_divider(NULL, "gmc0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL18_GMC0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[gmc0_clk_i_cg] = clk_register_gate(NULL, "gmc0_clk_i_cg",
			"gmc0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_GMC0, 0, &main_fab_clk_lock);
	/* iomux clk */
	clks[iomux0_clk_i] = clk_register_divider(NULL, "iomux0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL21_IOMUX_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[iomux0_clk_i_cg] = clk_register_gate(NULL, "iomux0_clk_i_cg",
			"iomux0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_IOMUX, 0, &main_fab_clk_lock);

	/* lgpio clk */
	clks[lgpio0_clk_i] = clk_register_divider(NULL, "lgpio0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL15_GPIO0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[lgpio0_clk_i_cg] = clk_register_gate(NULL, "lgpio0_clk_i_cg",
			"lgpio0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_LGPIO, 0, &main_fab_clk_lock);
			
	/* uart clk */
	clks[usart0_clk_i] = clk_register_divider(NULL, "usart0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL10_UART0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[usart0_clk_i_cg] = clk_register_gate(NULL, "usart0_clk_i_cg",
			"usart0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_UART0, 0, &main_fab_clk_lock);

	clks[udma0_clk_i_cg] = clk_register_gate(NULL, "udma0_clk_i_cg",
			"main_fab_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_DMA0, 0, &main_fab_clk_lock);

	/* cpu clk */
	clks[cpu_clk_i] = clk_register_mux(NULL, "cpu_clk_i",
			cpu_clk_parents, ARRAY_SIZE(cpu_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL3_CPU_CLK), 16, 3, 0, &cpu_clk_lock);

	/* sai clk */
	clks[sai_s_clk] = clk_register_mux(NULL, "sai_s_clk",
			sai_s_clk_parents, ARRAY_SIZE(sai_s_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL5_SAI_S_CLK), 16, 3, 0, &sai_s_clk_lock);

	/* disp pix clk */
	clks[disp_clk] = clk_register_mux(NULL, "disp_clk",
			disp_clk_parents, ARRAY_SIZE(disp_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL6_DISP_CLK), 16, 3, 0, &disp_clk_lock);
	clks[disp_pix_clk_cg] = clk_register_divider(NULL, "disp_pix_clk_cg", "disp_clk", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL20_DISP_PIX_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &disp_clk_lock);

	/* i2c0 bus clk */
	clks[i2c0_clk_i] = clk_register_divider(NULL, "i2c0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL11_I2C0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[i2c0_clk_i_cg] = clk_register_gate(NULL, "i2c0_clk_i_cg",
			"i2c0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_I2C0, 0, &main_fab_clk_lock);
	/* pmu clk */

	/* sdio0 bus clk */
	clks[sdio0_clk_i] = clk_register_divider(NULL, "sdio0_clk_i", "main_fab_clk_i", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL26_SDIO0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &main_fab_clk_lock);
	clks[sdio0_clk_i_cg] = clk_register_gate(NULL, "sdio0_clk_i_cg",
			"sdio0_clk_i", 0, CLK_VADDR(NUCLEI_GATE_CTRL0_CLK),
			CLK_GATE_SDIO0, 0, &main_fab_clk_lock);
	/* sdio0 data clk */
	clks[sdio_clk] = clk_register_mux(NULL, "sdio_clk",
			sdio_clk_parents, ARRAY_SIZE(sdio_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL25_SDIO_CLK), 16, 3, 0, &sdio_clk_lock);
	clks[sdio0_data_clk_i] = clk_register_divider(NULL, "sdio0_data_clk_i", "sdio_clk", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL27_SDIO0_DATA_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &sdio_clk_lock);
	/* xec clk */
	clks[xec_clk] = clk_register_mux(NULL, "xec_clk",
			xec_clk_parents, ARRAY_SIZE(xec_clk_parents), CLK_SET_RATE_NO_REPARENT,
			CLK_VADDR(NUCLEI_MUX_CTRL38_XEC_CLK), 16, 3, 0, &xec_clk_pll_lock);
	clks[xec0_clk_i_cg] = clk_register_divider(NULL, "xec0_clk_i_cg", "xec_clk", 0,
			CLK_VADDR(NUCLEI_DIV_CTRL40_XEC0_CLK), 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &xec_clk_pll_lock);

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (IS_ERR(clks[i])) {
			pr_err("nuclei clk %d: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
			//BUG();
		}
	}

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

CLK_OF_DECLARE(nuclei_clkc, "nuclei,clkc", nuclei_clk_setup);
