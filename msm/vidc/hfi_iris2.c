// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023. Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>
#include "msm_vidc_debug.h"
#include "hfi_common.h"

#define VBIF_BASE_OFFS_IRIS2			0x00080000

#define VCODEC_BASE_OFFS_IRIS2                 0x00000000
#define CPU_BASE_OFFS_IRIS2			0x000A0000
#define AON_MVP_NOC_RESET                       0x0001F000
#define AON_BASE_OFFS			0x000E0000
#define CPU_CS_BASE_OFFS_IRIS2		(CPU_BASE_OFFS_IRIS2)
#define CPU_IC_BASE_OFFS_IRIS2		(CPU_BASE_OFFS_IRIS2)

#define CPU_CS_A2HSOFTINTCLR_IRIS2	(CPU_CS_BASE_OFFS_IRIS2 + 0x1C)
#define CPU_CS_VCICMD_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x20)
#define CPU_CS_VCICMDARG0_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x24)
#define CPU_CS_VCICMDARG1_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x28)
#define CPU_CS_VCICMDARG2_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x2C)
#define CPU_CS_VCICMDARG3_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x30)
#define CPU_CS_VMIMSG_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x34)
#define CPU_CS_VMIMSGAG0_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x38)
#define CPU_CS_VMIMSGAG1_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x3C)
#define CPU_CS_SCIACMD_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x48)
#define CPU_CS_H2XSOFTINTEN_IRIS2	(CPU_CS_BASE_OFFS_IRIS2 + 0x148)

/* HFI_CTRL_STATUS */
#define CPU_CS_SCIACMDARG0_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x4C)
#define CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS2	0xfe
#define CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS2           0x100
#define CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS2     0x40000000

/* HFI_QTBL_INFO */
#define CPU_CS_SCIACMDARG1_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x50)

/* HFI_QTBL_ADDR */
#define CPU_CS_SCIACMDARG2_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x54)

/* HFI_VERSION_INFO */
#define CPU_CS_SCIACMDARG3_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x58)

/* SFR_ADDR */
#define CPU_CS_SCIBCMD_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x5C)

/* MMAP_ADDR */
#define CPU_CS_SCIBCMDARG0_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x60)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG1_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x64)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG2_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x68)

#define CPU_CS_AHB_BRIDGE_SYNC_RESET            (CPU_CS_BASE_OFFS_IRIS2 + 0x160)
#define CPU_CS_AHB_BRIDGE_SYNC_RESET_STATUS     (CPU_CS_BASE_OFFS_IRIS2 + 0x164)

/* FAL10 Feature Control */
#define CPU_CS_X2RPMh_IRIS2		(CPU_CS_BASE_OFFS_IRIS2 + 0x168)
#define CPU_CS_X2RPMh_MASK0_BMSK_IRIS2	0x1
#define CPU_CS_X2RPMh_MASK0_SHFT_IRIS2	0x0
#define CPU_CS_X2RPMh_MASK1_BMSK_IRIS2	0x2
#define CPU_CS_X2RPMh_MASK1_SHFT_IRIS2	0x1
#define CPU_CS_X2RPMh_SWOVERRIDE_BMSK_IRIS2	0x4
#define CPU_CS_X2RPMh_SWOVERRIDE_SHFT_IRIS2	0x3

#define CPU_IC_SOFTINT_IRIS2		(CPU_IC_BASE_OFFS_IRIS2 + 0x150)
#define CPU_IC_SOFTINT_H2A_SHFT_IRIS2	0x0

#define AON_WRAPPER_MVP_NOC_RESET_REQ   (AON_MVP_NOC_RESET + 0x000)
#define AON_WRAPPER_MVP_NOC_RESET_ACK   (AON_MVP_NOC_RESET + 0x004)

/*
 * --------------------------------------------------------------------------
 * MODULE: wrapper
 * --------------------------------------------------------------------------
 */
#define WRAPPER_BASE_OFFS_IRIS2		0x000B0000
#define WRAPPER_INTR_STATUS_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x0C)
#define WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2	0x8
#define WRAPPER_INTR_STATUS_A2H_BMSK_IRIS2	0x4

#define WRAPPER_INTR_MASK_IRIS2		(WRAPPER_BASE_OFFS_IRIS2 + 0x10)
#define WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS2	0x8
#define WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS2	0x4

#define WRAPPER_CPU_CLOCK_CONFIG_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x2000)
#define WRAPPER_CPU_CGC_DIS_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x2010)
#define WRAPPER_CPU_STATUS_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x2014)

#define WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x54)
#define WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2	(WRAPPER_BASE_OFFS_IRIS2 + 0x58)
#define WRAPPER_CORE_POWER_STATUS		(WRAPPER_BASE_OFFS_IRIS2 + 0x80)
#define WRAPPER_CORE_CLOCK_CONFIG_IRIS2		(WRAPPER_BASE_OFFS_IRIS2 + 0x88)
/*
 * --------------------------------------------------------------------------
 * MODULE: tz_wrapper
 * --------------------------------------------------------------------------
 */
#define WRAPPER_TZ_BASE_OFFS	0x000C0000
#define WRAPPER_TZ_CPU_CLOCK_CONFIG	(WRAPPER_TZ_BASE_OFFS)
#define WRAPPER_TZ_CPU_STATUS	(WRAPPER_TZ_BASE_OFFS + 0x10)
#define WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG	(WRAPPER_TZ_BASE_OFFS + 0x14)
#define WRAPPER_TZ_QNS4PDXFIFO_RESET	(WRAPPER_TZ_BASE_OFFS + 0x18)

#define CTRL_INIT_IRIS2		CPU_CS_SCIACMD_IRIS2

#define CTRL_STATUS_IRIS2	CPU_CS_SCIACMDARG0_IRIS2
#define CTRL_ERROR_STATUS__M_IRIS2 \
		CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS2
#define CTRL_INIT_IDLE_MSG_BMSK_IRIS2 \
		CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS2
#define CTRL_STATUS_PC_READY_IRIS2 \
		CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS2


#define QTBL_INFO_IRIS2		CPU_CS_SCIACMDARG1_IRIS2

#define QTBL_ADDR_IRIS2		CPU_CS_SCIACMDARG2_IRIS2

#define VERSION_INFO_IRIS2	    CPU_CS_SCIACMDARG3_IRIS2

#define SFR_ADDR_IRIS2		    CPU_CS_SCIBCMD_IRIS2
#define MMAP_ADDR_IRIS2		CPU_CS_SCIBCMDARG0_IRIS2
#define UC_REGION_ADDR_IRIS2	CPU_CS_SCIBARG1_IRIS2
#define UC_REGION_SIZE_IRIS2	CPU_CS_SCIBARG2_IRIS2

#define AON_WRAPPER_MVP_NOC_LPI_CONTROL	(AON_BASE_OFFS)
#define AON_WRAPPER_MVP_NOC_LPI_STATUS	(AON_BASE_OFFS + 0x4)

#define VCODEC_SS_IDLE_STATUSn           (VCODEC_BASE_OFFS_IRIS2 + 0x70)

/*
 * --------------------------------------------------------------------------
 * MODULE: vcodec noc error log registers (iris2)
 * --------------------------------------------------------------------------
 */
#define VCODEC_NOC_VIDEO_A_NOC_BASE_OFFS		0x00010000
#define VCODEC_NOC_ERL_MAIN_SWID_LOW			0x00011200
#define VCODEC_NOC_ERL_MAIN_SWID_HIGH			0x00011204
#define VCODEC_NOC_ERL_MAIN_MAINCTL_LOW			0x00011208
#define VCODEC_NOC_ERL_MAIN_ERRVLD_LOW			0x00011210
#define VCODEC_NOC_ERL_MAIN_ERRCLR_LOW			0x00011218
#define VCODEC_NOC_ERL_MAIN_ERRLOG0_LOW			0x00011220
#define VCODEC_NOC_ERL_MAIN_ERRLOG0_HIGH		0x00011224
#define VCODEC_NOC_ERL_MAIN_ERRLOG1_LOW			0x00011228
#define VCODEC_NOC_ERL_MAIN_ERRLOG1_HIGH		0x0001122C
#define VCODEC_NOC_ERL_MAIN_ERRLOG2_LOW			0x00011230
#define VCODEC_NOC_ERL_MAIN_ERRLOG2_HIGH		0x00011234
#define VCODEC_NOC_ERL_MAIN_ERRLOG3_LOW			0x00011238
#define VCODEC_NOC_ERL_MAIN_ERRLOG3_HIGH		0x0001123C

#define MSM_VIDC_CLOCK_SOURCE_SCALING_RATIO 3

void __interrupt_init_iris2(struct venus_hfi_device *device, u32 sid)
{
	u32 mask_val = 0;

	/* All interrupts should be disabled initially 0x1F6 : Reset value */
	mask_val = __read_register(device, WRAPPER_INTR_MASK_IRIS2, sid);

	/* Write 0 to unmask CPU and WD interrupts */
	mask_val &= ~(WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS2|
			WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS2);
	__write_register(device, WRAPPER_INTR_MASK_IRIS2, mask_val, sid);
}

void __setup_ucregion_memory_map_iris2(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, UC_REGION_ADDR_IRIS2,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, UC_REGION_SIZE_IRIS2, SHARED_QSIZE, sid);
	__write_register(device, QTBL_ADDR_IRIS2,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, QTBL_INFO_IRIS2, 0x01, sid);
	if (device->sfr.align_device_addr)
		__write_register(device, SFR_ADDR_IRIS2,
				(u32)device->sfr.align_device_addr, sid);
	if (device->qdss.align_device_addr)
		__write_register(device, MMAP_ADDR_IRIS2,
				(u32)device->qdss.align_device_addr, sid);
	/* update queues vaddr for debug purpose */
	__write_register(device, CPU_CS_VCICMDARG0_IRIS2,
		(u32)((u64)device->iface_q_table.align_virtual_addr), sid);
	__write_register(device, CPU_CS_VCICMDARG1_IRIS2,
		(u32)((u64)device->iface_q_table.align_virtual_addr >> 32),
		sid);
}

static int __disable_unprepare_clock_iris2(struct venus_hfi_device *device,
                const char *clk_name)
{
        int rc = 0;
	u32 sid = DEFAULT_SID;
        struct clock_info *cl;
        bool found;

        if (!device || !clk_name) {
                d_vpr_e("%s: invalid params\n", __func__);
                return -EINVAL;
        }

        found = false;
        venus_hfi_for_each_clock(device, cl) {
                if (!cl->clk) {
                        d_vpr_e("%s: invalid clock %s\n", __func__, cl->name);
                        return -EINVAL;
                }
                if (strcmp(cl->name, clk_name))
                        continue;
                found = true;
                clk_disable_unprepare(cl->clk);
                if (cl->has_scaling)
                        __set_clk_rate(device, cl, 0, sid);
                d_vpr_h("%s: clock %s disable unprepared\n", __func__, cl->name);
                break;
        }
        if (!found) {
                d_vpr_e("%s: clock %s not found\n", __func__, clk_name);
                return -EINVAL;
        }

        return rc;
}

static int __prepare_enable_clock_iris2(struct venus_hfi_device *device,
		const char *clk_name)
{
	int rc = 0;
	struct clock_info *cl;
	bool found;
	u64 rate = 0;
	u32 sid = DEFAULT_SID;

	if (!device || !clk_name) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	found = false;
	venus_hfi_for_each_clock(device, cl) {
		if (!cl->clk) {
			d_vpr_e("%s: invalid clock\n", __func__);
			return -EINVAL;
		}
		if (strcmp(cl->name, clk_name))
			continue;
		found = true;
		/*
		 * For the clocks we control, set the rate prior to preparing
		 * them.  Since we don't really have a load at this point, scale
		 * it to the lowest frequency possible
		 */
		if (cl->has_scaling) {
			rate = clk_round_rate(cl->clk, 0);
			/**
			 * source clock is already multipled with scaling ratio and __set_clk_rate
			 * attempts to multiply again. So divide scaling ratio before calling
			 * __set_clk_rate.
			 */
			rate = rate / MSM_VIDC_CLOCK_SOURCE_SCALING_RATIO;
			__set_clk_rate(device, cl, rate, sid);
		}

		rc = clk_prepare_enable(cl->clk);
		if (rc) {
			d_vpr_e("%s: failed to enable clock %s\n",
				__func__, cl->name);
			return rc;
		}
		if (!__clk_is_enabled(cl->clk)) {
			d_vpr_e("%s: clock %s not enabled\n",
				__func__, cl->name);
			clk_disable_unprepare(cl->clk);
			if (cl->has_scaling)
				__set_clk_rate(device, cl, 0, sid);
			return -EINVAL;
		}
		d_vpr_h("%s: clock %s prepare enabled\n", __func__, cl->name);
		break;
	}
	if (!found) {
		d_vpr_e("%s: clock %s not found\n", __func__, clk_name);
		return -EINVAL;
	}

	return rc;
}

int __read_register_with_poll_timeout(struct venus_hfi_device *device,
	u32 reg, u32 mask, u32 exp_val, u32 sleep_us, u32 timeout_us)
{
	int rc = 0;
	u32 val = 0;
	u8 *addr;

	if (!device) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (!device->power_enabled) {
		d_vpr_e("%s failed: Power is OFF\n", __func__);
		return -EINVAL;
	}

	addr = (u8 *)device->hal_data->register_base + reg;

	rc = readl_relaxed_poll_timeout(addr, val, ((val & mask) == exp_val), sleep_us, timeout_us);
	/*
	 * Memory barrier to make sure value is read correctly from the
	 * register.
	 */
	rmb();
	d_vpr_l(
		"regread(%pK + %#x) = %#x. rc %d, mask %#x, exp_val %#x, cond %u, sleep %u, timeout %u\n",
		device->hal_data->register_base, reg, val, rc, mask, exp_val,
		((val & mask) == exp_val), sleep_us, timeout_us);

	return rc;
}

static bool is_iris2_hw_power_collapsed(struct venus_hfi_device *device)
{
	u32 value = 0, pwr_status = 0;
	u32 sid = DEFAULT_SID;

	value = __read_register(device, WRAPPER_CORE_POWER_STATUS, sid);

	/* if BIT(1) is 1 then video hw power is on else off */
	pwr_status = value & BIT(1);
	return pwr_status ? false : true;
}

static int __power_off_iris2_hardware(struct venus_hfi_device *device)
{
	int rc = 0, i;
	u32 value = 0;
	u32 sid = DEFAULT_SID;
	bool pwr_collapsed = false;

	/*
	 * Incase hw power control is enabled, for both CPU WD, video
	 * hw unresponsive cases, check for power status to decide on
	 * executing NOC reset sequence before disabling power. If there
	 * is no CPU WD and hw_power_control is enabled, fw is expected
	 * to power collapse video hw always.
	 */
	if (device->hw_power_control) {
		pwr_collapsed = is_iris2_hw_power_collapsed(device);
		if (device->cpu_watchdog || device->video_unresponsive) {
			if (pwr_collapsed) {
				d_vpr_e("%s: video hw power collapsed %d, %d\n",
					__func__, device->cpu_watchdog, device->video_unresponsive);
				goto disable_power;
			} else {
				d_vpr_e("%s: video hw is power ON %d, %d\n",
					__func__, device->cpu_watchdog, device->video_unresponsive);
			}
		} else {
			if (!pwr_collapsed)
				d_vpr_e("%s: video hw is not power collapsed\n", __func__);

			goto disable_power;
		}
	}

	/*
	 * check to make sure core clock branch enabled else
	 * we cannot read vcodec top idle register
	 */
	value = __read_register(device, WRAPPER_CORE_CLOCK_CONFIG_IRIS2, sid);

	if (value) {
		d_vpr_h("%s: core clock config not enabled, enabling it to read vcodec registers\n",
			__func__);
		__write_register(device, WRAPPER_CORE_CLOCK_CONFIG_IRIS2, 0, sid);
	}

	/*
	 * add MNoC idle check before collapsing MVS0 per HPG update
	 * poll for NoC DMA idle -> HPG 6.1.1
	 */
	//for (i = 0; i < core->capabilities[NUM_VPP_PIPE].value; i++) {
	for (i = 0; i < 4; i++) {
		rc = __read_register_with_poll_timeout(device, VCODEC_SS_IDLE_STATUSn + 4*i,
				0x400000, 0x400000, 2000, 20000);
		if (rc)
			d_vpr_h("%s: VCODEC_SS_IDLE_STATUSn (%d) is not idle (%#x)\n",
				__func__, i, value);
	}

	/* Apply partial reset on MSF interface and wait for ACK */
	__write_register(device, AON_WRAPPER_MVP_NOC_RESET_REQ, 0x3, sid);

	rc = __read_register_with_poll_timeout(device, AON_WRAPPER_MVP_NOC_RESET_ACK,
			0x3, 0x3, 200, 2000);
	if (rc)
		d_vpr_h("%s: AON_WRAPPER_MVP_NOC_RESET assert failed\n", __func__);

	/* De-assert partial reset on MSF interface and wait for ACK */
	__write_register(device, AON_WRAPPER_MVP_NOC_RESET_REQ, 0x0i, sid);

	rc = __read_register_with_poll_timeout(device, AON_WRAPPER_MVP_NOC_RESET_ACK,
			0x3, 0x0, 200, 2000);
	if (rc)
		d_vpr_h("%s: AON_WRAPPER_MVP_NOC_RESET de-assert failed\n", __func__);

	/*
	 * Reset both sides of 2 ahb2ahb_bridges (TZ and non-TZ)
	 * do we need to check status register here?
	 */
	__write_register(device, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x3, sid);
	__write_register(device, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x2, sid);
	__write_register(device, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x0, sid);

disable_power:
	/* power down process */
	rc = __disable_regulator_by_name(device, "vcodec");
	if (rc) {
		d_vpr_e("%s: disable regulator vcodec failed\n", __func__);
		rc = 0;
	}
	rc = __disable_unprepare_clock_iris2(device, "vcodec_clk");
	if (rc) {
		d_vpr_e("%s: disable unprepare vcodec_clk failed\n", __func__);
		rc = 0;
	}

	return rc;
}

static int __power_off_iris2_controller(struct venus_hfi_device *device)
{
	int rc = 0;
	u32 sid = DEFAULT_SID;

	/*
	 * mask fal10_veto QLPAC error since fal10_veto can go 1
	 * when pwwait == 0 and clamped to 0 -> HPG 6.1.2
	 */
	__write_register(device, CPU_CS_X2RPMh_IRIS2, 0x3, sid);

	/* set MNoC to low power, set PD_NOC_QREQ (bit 0) */
	__write_register_masked(device, AON_WRAPPER_MVP_NOC_LPI_CONTROL,
			0x1, BIT(0), sid);

	rc = __read_register_with_poll_timeout(device, AON_WRAPPER_MVP_NOC_LPI_STATUS,
			0x1, 0x1, 200, 2000);
	if (rc)
		d_vpr_h("%s: AON_WRAPPER_MVP_NOC_LPI_CONTROL failed\n", __func__);

	/* Set Debug bridge Low power */
	 __write_register(device, WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2, 0x7, sid);

	rc = __read_register_with_poll_timeout(device, WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2,
			0x7, 0x7, 200, 2000);
	if (rc)
		d_vpr_h("%s: debug bridge low power failed\n", __func__);

	/* Debug bridge LPI release */
	__write_register(device, WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2, 0x0, sid);

	rc = __read_register_with_poll_timeout(device, WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2,
			0xffffffff, 0x0, 200, 2000);
	if (rc)
		d_vpr_h("%s: debug bridge release failed\n", __func__);

	/* Turn off MVP MVS0C core clock */
	rc = __disable_unprepare_clock_iris2(device, "core_clk");
	if (rc) {
		d_vpr_e("%s: disable unprepare core_clk failed\n", __func__);
		rc = 0;
	}

	/* Disable GCC_VIDEO_AXI0_CLK clock */
	rc = __disable_unprepare_clock_iris2(device, "gcc_video_axi0");
	if (rc) {
		d_vpr_e("%s: disable unprepare gcc_video_axi0 failed\n", __func__);
		rc = 0;
	}

	/* power down process */
	rc = __disable_regulator_by_name(device, "iris-ctl");
	if (rc) {
		d_vpr_e("%s: disable regulator iris-ctl failed\n", __func__);
		rc = 0;
	}

	return rc;
}


void __power_off_iris2(struct venus_hfi_device *device)
{
	int rc = 0;
	u32 sid = DEFAULT_SID;

	if (!device || !device->power_enabled)
		return;

	/**
	 * Reset video_cc_mvs0_clk_src value to resolve MMRM high video
	 * clock projection issue.
	 */
	rc = __set_clocks(device, 0, sid);
	if (rc)
		d_vpr_e("%s: resetting clocks failed\n", __func__);

	if (__power_off_iris2_hardware(device))
		d_vpr_e("%s: failed to power off hardware\n", __func__);

	if (__power_off_iris2_controller(device))
		d_vpr_e("%s: failed to power off controller\n", __func__);

	if (__unvote_buses(device, sid))
		d_vpr_e("%s: failed to unvote buses\n", __func__);

	if (!(device->intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2))
		disable_irq_nosync(device->hal_data->irq);
	device->intr_status = 0;

	device->power_enabled = false;
}

static int __power_on_iris2_hardware(struct venus_hfi_device *device)
{
	int rc = 0;

	rc = __enable_regulator_by_name(device, "vcodec");
	if (rc)
		goto fail_regulator;

	rc = __prepare_enable_clock_iris2(device, "vcodec_clk");
	if (rc)
		goto fail_clk_controller;

	return 0;

fail_clk_controller:
	__disable_regulator_by_name(device, "vcodec");
fail_regulator:
	return rc;
}

static int __power_on_iris2_controller(struct venus_hfi_device *device)
{
	int rc = 0;
	u32 sid = DEFAULT_SID;

	rc = __enable_regulator_by_name(device, "iris-ctl");
	if (rc)
		goto fail_regulator;

	rc = call_venus_op(device, reset_ahb2axi_bridge, device, sid);
	if (rc)
		goto fail_reset_ahb2axi;

	rc = __prepare_enable_clock_iris2(device, "gcc_video_axi0");
	if (rc)
		goto fail_clk_axi;

	rc = __prepare_enable_clock_iris2(device, "core_clk");
	if (rc)
		goto fail_clk_controller;

	return 0;

fail_clk_controller:
	__disable_unprepare_clock_iris2(device, "gcc_video_axi0");
fail_clk_axi:
fail_reset_ahb2axi:
	__disable_regulator_by_name(device, "iris-ctl");
fail_regulator:
	return rc;
}

int __power_on_iris2(struct venus_hfi_device *device)
{
	int rc = 0;
	u32 sid = DEFAULT_SID;

	if (device->power_enabled)
		return 0;

	/* Vote for all hardware resources */
	rc = __vote_buses(device, INT_MAX, INT_MAX, sid);
	if (rc) {
		d_vpr_e("%s: failed to vote buses, rc %d\n", __func__, rc);
		goto fail_vote_buses;
	}

	rc = __power_on_iris2_controller(device);
	if (rc) {
		d_vpr_e("%s: failed to power on iris3 controller\n", __func__);
		goto fail_power_on_controller;
	}

	rc = __power_on_iris2_hardware(device);
	if (rc) {
		d_vpr_e("%s: failed to power on iris3 hardware\n", __func__);
		goto fail_power_on_hardware;
	}
	/* video controller and hardware powered on successfully */
	device->power_enabled = true;

	rc = __scale_clocks(device, sid);
	if (rc) {
		d_vpr_e("%s: failed to scale clocks\n", __func__);
		rc = 0;
	}
	/*
	 * Re-program all of the registers that get reset as a result of
	 * regulator_disable() and _enable()
	 */
	__set_registers(device, sid);

	call_venus_op(device, interrupt_init, device, sid);
	device->intr_status = 0;
	enable_irq(device->hal_data->irq);
	return rc;

fail_power_on_hardware:
	__power_off_iris2_controller(device);
fail_power_on_controller:
	__unvote_buses(device, sid);
fail_vote_buses:
	device->power_enabled = false;
	return rc;
}

int __prepare_pc_iris2(struct venus_hfi_device *device)
{
	int rc = 0;
	u32 wfi_status = 0, idle_status = 0, pc_ready = 0;
	u32 ctrl_status = 0;
	int count = 0;
	const int max_tries = 10;

	ctrl_status = __read_register(device, CTRL_STATUS_IRIS2, DEFAULT_SID);
	pc_ready = ctrl_status & CTRL_STATUS_PC_READY_IRIS2;
	idle_status = ctrl_status & BIT(30);

	if (pc_ready) {
		d_vpr_h("Already in pc_ready state\n");
		return 0;
	}

	wfi_status = BIT(0) & __read_register(device, WRAPPER_TZ_CPU_STATUS,
							DEFAULT_SID);
	if (!wfi_status || !idle_status) {
		d_vpr_e("Skipping PC, wfi status not set\n");
		goto skip_power_off;
	}

	rc = __prepare_pc(device);
	if (rc) {
		d_vpr_e("Failed __prepare_pc %d\n", rc);
		goto skip_power_off;
	}

	while (count < max_tries) {
		wfi_status = BIT(0) & __read_register(device,
				WRAPPER_TZ_CPU_STATUS, DEFAULT_SID);
		ctrl_status = __read_register(device,
				CTRL_STATUS_IRIS2, DEFAULT_SID);
		if (wfi_status && (ctrl_status & CTRL_STATUS_PC_READY_IRIS2))
			break;
		usleep_range(150, 250);
		count++;
	}

	if (count == max_tries) {
		d_vpr_e("Skip PC. Core is not in right state\n");
		goto skip_power_off;
	}

	return rc;

skip_power_off:
	d_vpr_e("Skip PC, wfi=%#x, idle=%#x, pcr=%#x, ctrl=%#x)\n",
		wfi_status, idle_status, pc_ready, ctrl_status);
	return -EAGAIN;
}

void __raise_interrupt_iris2(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, CPU_IC_SOFTINT_IRIS2,
				1 << CPU_IC_SOFTINT_H2A_SHFT_IRIS2, sid);
}

bool __watchdog_iris2(u32 intr_status)
{
	bool rc = false;

	if (intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2)
		rc = true;

	return rc;
}

void __noc_error_info_iris2(struct venus_hfi_device *device)
{
	u32 val = 0;
	u32 sid = DEFAULT_SID;

	if (device->res->vpu_ver == VPU_VERSION_IRIS2_1)
		return;
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_SWID_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_SWID_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_SWID_HIGH, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_SWID_HIGH:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_MAINCTL_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_MAINCTL_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRVLD_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRVLD_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRCLR_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRCLR_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG0_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG0_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG0_HIGH, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG0_HIGH:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG1_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG1_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG1_HIGH, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG1_HIGH:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG2_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG2_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG2_HIGH, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG2_HIGH:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG3_LOW, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG3_LOW:     %#x\n", val);
	val = __read_register(device, VCODEC_NOC_ERL_MAIN_ERRLOG3_HIGH, sid);
	d_vpr_e("VCODEC_NOC_ERL_MAIN_ERRLOG3_HIGH:     %#x\n", val);
}

void __core_clear_interrupt_iris2(struct venus_hfi_device *device)
{
	u32 intr_status = 0, mask = 0;

	if (!device) {
		d_vpr_e("%s: NULL device\n", __func__);
		return;
	}

	intr_status = __read_register(device, WRAPPER_INTR_STATUS_IRIS2,
						DEFAULT_SID);
	mask = (WRAPPER_INTR_STATUS_A2H_BMSK_IRIS2|
		WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2|
		CTRL_INIT_IDLE_MSG_BMSK_IRIS2);

	if (intr_status & mask) {
		device->intr_status |= intr_status;
		device->reg_count++;
		d_vpr_l("INTERRUPT: times: %d interrupt_status: %d\n",
			device->reg_count, intr_status);
	} else {
		device->spur_count++;
	}

	__write_register(device, CPU_CS_A2HSOFTINTCLR_IRIS2, 1, DEFAULT_SID);
}

int __boot_firmware_iris2(struct venus_hfi_device *device, u32 sid)
{
	int rc = 0;
	u32 ctrl_init_val = 0, ctrl_status = 0, count = 0, max_tries = 1000;

	ctrl_init_val = BIT(0);

	__write_register(device, CTRL_INIT_IRIS2, ctrl_init_val, sid);
	while (!ctrl_status && count < max_tries) {
		ctrl_status = __read_register(device, CTRL_STATUS_IRIS2, sid);
		if ((ctrl_status & CTRL_ERROR_STATUS__M_IRIS2) == 0x4) {
			s_vpr_e(sid, "invalid setting for UC_REGION\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		s_vpr_e(sid, "Error booting up vidc firmware\n");
		rc = -ETIME;
	}

	/* Enable interrupt before sending commands to venus */
	__write_register(device, CPU_CS_H2XSOFTINTEN_IRIS2, 0x1, sid);
	__write_register(device, CPU_CS_X2RPMh_IRIS2, 0x0, sid);

	return rc;
}
