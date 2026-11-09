/*
 *  drivers/clk/mmp/dvfs-pxa988.c
 *
 *  DVFS platform info for PXA986/PXA988 (dual Cortex-A9), backported from
 *  the 3.4 arch/arm/mach-mmp/dvfs-pxa988.c voltage/frequency tables.  The
 *  generic DVC machinery lives in dvfs-dvc.c; the PMIC (88PM822 BUCK1) is
 *  driven through the 88pm8xx-dvc driver, same as on PXA1088.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mfd/88pm80x.h>
#include <linux/clk/mmp.h>
#include <mach/addr-map.h>
#include <mach/dvfs-dvc.h>
#include <mach/cputype.h>

enum {
	CORE = 0,
	DDR,
	GC,
	VPU,
};

#define VL_MAX		4
#define NUM_PROFILES	9

#define APMU_GEU		0x068
#define UIMAINFUSE_31_00	0x410
#define BLOCK0_224_255		0x420

/* max frequency (KHz) each voltage level can support, from 3.4 freqs_cmb_ax */
static unsigned long freqs_cmb_988[][VL_MAX] = {
	{624000, 624000, 1066000, 1482000},	/* CORE */
	{312000, 312000, 312000, 533000},	/* DDR/AXI */
	{0, 416000, 624000, 624000},		/* GC */
	{0, 312000, 416000, 416000},		/* VPU */
};

/* 986 A0 SVC table (mV) per fuse profile, from 3.4 vm_millivolts_986ax_svc */
static int vm_mv_986_svc[NUM_PROFILES][VL_MAX] = {
	{1025, 1138, 1275, 1350},	/* profile 0 */
	{1025, 1100, 1100, 1188},	/* profile 1 */
	{1025, 1100, 1113, 1200},	/* profile 2 */
	{1025, 1100, 1138, 1213},	/* profile 3 */
	{1025, 1100, 1150, 1238},	/* profile 4 */
	{1025, 1100, 1175, 1263},	/* profile 5 */
	{1025, 1100, 1200, 1288},	/* profile 6 */
	{1025, 1138, 1225, 1313},	/* profile 7 */
	{1025, 1138, 1275, 1350},	/* profile 8 */
};

/* non-SVC parts, from 3.4 vm_millivolts_986z3_svc: fixed high voltage */
static int vm_mv_986_nosvc[NUM_PROFILES][VL_MAX] = {
	{1300, 1300, 1350, 1400},	/* profile 0 */
	{1300, 1300, 1300, 1300},	/* profile 1 */
	{1300, 1300, 1300, 1300},	/* profile 2 */
	{1300, 1300, 1325, 1325},	/* profile 3 */
	{1300, 1300, 1325, 1350},	/* profile 4 */
	{1300, 1300, 1325, 1375},	/* profile 5 */
	{1300, 1300, 1325, 1400},	/* profile 6 */
	{1300, 1300, 1325, 1400},	/* profile 7 */
	{1300, 1300, 1350, 1400},	/* profile 8 */
};

static struct dvfs_rail_component vm_rail_comp_tbl_dvc[] = {
	INIT_DVFS("cpu", true,
		  (AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P),
		  &freqs_cmb_988[CORE][0]),
	INIT_DVFS("ddr", true,
		  (AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P),
		  &freqs_cmb_988[DDR][0]),
	INIT_DVFS("GCCLK", true,
		  (AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P),
		  &freqs_cmb_988[GC][0]),
	INIT_DVFS("VPUCLK", true,
		  (AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P),
		  &freqs_cmb_988[VPU][0]),
};

static int set_pmic_volt(unsigned int lvl, unsigned int mv)
{
	return pm8xx_dvc_setvolt(PM800_ID_BUCK1, lvl, mv * mV2uV);
}

static int get_pmic_volt(unsigned int lvl)
{
	int uv = 0, ret;

	ret = pm8xx_dvc_getvolt(PM800_ID_BUCK1, lvl, &uv);
	if (ret < 0)
		return ret;
	return DIV_ROUND_UP(uv, mV2uV);
}

static struct dvc_plat_info dvc_pxa988_info = {
	.comps = vm_rail_comp_tbl_dvc,
	.num_comps = ARRAY_SIZE(vm_rail_comp_tbl_dvc),
	.num_volts = VL_MAX,
	/* 3.4: CP/MSA modem on 986 A0 needs at least VL2, 988 needs VL1 */
	.cp_pmudvc_lvl = VL2,
	.dp_pmudvc_lvl = VL2,
	.dvc_pin_switch = 1,
	.set_vccmain_volt = set_pmic_volt,
	.get_vccmain_volt = get_pmic_volt,
	.pmic_rampup_step = 12500,
	.dbglvl = 1,
	.regname = "BUCK1",
	.no_extra_timer = true,
};

static unsigned int uichipprofile;
static int svc_capable;

/* same fuse -> profile decoding as 3.4 acpuclock-pxa988.c */
static unsigned int convert_fuses_to_profile(unsigned int uifuses)
{
	unsigned int uiprofile = 0, uitemp = 3, i;

	for (i = 0; i < NUM_PROFILES; i++) {
		uitemp |= 3 << (i * 2);
		if (uitemp == uifuses)
			uiprofile = i + 1;
	}
	return uiprofile;
}

static int __init __init_read_profile_988(void)
{
	void __iomem *apmu_base, *geu_base;
	unsigned int uigeustatus, uifuses;

	if (!cpu_is_pxa986() && !cpu_is_pxa988())
		return 0;

	apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
	geu_base = ioremap(AXI_PHYS_BASE + 0x1000, SZ_4K);
	if (!apmu_base || !geu_base) {
		pr_err("dvfs-pxa988: failed to map GEU/APMU\n");
		if (apmu_base)
			iounmap(apmu_base);
		if (geu_base)
			iounmap(geu_base);
		return -ENOMEM;
	}

	/* GEU clock must be on to read the fuses */
	uigeustatus = __raw_readl(apmu_base + APMU_GEU);
	if (!(uigeustatus & 0x08)) {
		__raw_writel(uigeustatus | 0x09, apmu_base + APMU_GEU);
		udelay(10);
	}
	uifuses = __raw_readl(geu_base + BLOCK0_224_255);
	__raw_writel(uigeustatus, apmu_base + APMU_GEU);

	/* bit 238/239 set => SVC capable part, bits 240..255 => profile */
	svc_capable = (((uifuses >> 14) & 0x3) == 0x3);
	uichipprofile = convert_fuses_to_profile((uifuses >> 16) & 0xffff);

	pr_info("dvfs-pxa988: profile %u, svc %d\n", uichipprofile,
		svc_capable);

	iounmap(apmu_base);
	iounmap(geu_base);
	return 0;
}
pure_initcall(__init_read_profile_988);

static int __init setup_pxa988_dvfs_platinfo(void)
{
	void __iomem *hwdvc_base;

	if (!cpu_is_pxa986() && !cpu_is_pxa988())
		return 0;

	hwdvc_base = ioremap(APB_PHYS_BASE + 0x50000, SZ_4K);
	if (hwdvc_base == NULL) {
		pr_err("error to ioremap hwdvc base\n");
		return -EINVAL;
	}

	if (uichipprofile >= NUM_PROFILES)
		uichipprofile = 0;
	dvc_pxa988_info.millivolts = svc_capable ?
		vm_mv_986_svc[uichipprofile] : vm_mv_986_nosvc[uichipprofile];
	dvc_pxa988_info.dvc_reg_base = hwdvc_base;
	return dvfs_setup_dvcplatinfo(&dvc_pxa988_info);
}
core_initcall_sync(setup_pxa988_dvfs_platinfo);
