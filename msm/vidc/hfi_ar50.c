// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_common.h"
#include "hfi_io_common.h"
#include <linux/io.h>

#define VIDC_VBIF_BASE_OFFS_AR50			0x00080000

#define VIDC_CPU_BASE_OFFS_AR50			0x000C0000
#define VIDC_CPU_CS_BASE_OFFS_AR50		(VIDC_CPU_BASE_OFFS_AR50 + 0x00012000)
#define VIDC_CPU_IC_BASE_OFFS		(VIDC_CPU_BASE_OFFS_AR50 + 0x0001F000)

#define VIDC_CPU_CS_A2HSOFTINTCLR_AR50	(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x1C)
#define VIDC_CPU_CS_SCIACMD_AR50			(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x48)

/* HFI_CTRL_STATUS */
#define VIDC_CPU_CS_SCIACMDARG0_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x4C)
#define VIDC_CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_AR50	0xfe
#define VIDC_CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_AR50           0x100
#define VIDC_CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_AR50     0x40000000

/* HFI_QTBL_INFO */
#define VIDC_CPU_CS_SCIACMDARG1_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x50)

/* HFI_QTBL_ADDR */
#define VIDC_CPU_CS_SCIACMDARG2_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x54)

/* HFI_VERSION_INFO */
#define VIDC_CPU_CS_SCIACMDARG3_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x58)

/* VIDC_SFR_ADDR */
#define VIDC_CPU_CS_SCIBCMD_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x5C)

/* VIDC_MMAP_ADDR */
#define VIDC_CPU_CS_SCIBCMDARG0_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x60)

/* VIDC_UC_REGION_ADDR */
#define VIDC_CPU_CS_SCIBARG1_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x64)

/* VIDC_UC_REGION_ADDR */
#define VIDC_CPU_CS_SCIBARG2_AR50		(VIDC_CPU_CS_BASE_OFFS_AR50 + 0x68)

#define VIDC_CPU_IC_SOFTINT_AR50	(VIDC_CPU_IC_BASE_OFFS + 0x18)
#define VIDC_CPU_IC_SOFTINT_H2A_SHFT_AR50	0xF

/*
 * --------------------------------------------------------------------------
 * MODULE: vidc_wrapper
 * --------------------------------------------------------------------------
 */
#define VIDC_WRAPPER_BASE_OFFS_AR50		0x000E0000

#define VIDC_WRAPPER_HW_VERSION_AR50		(VIDC_WRAPPER_BASE_OFFS_AR50 + 0x00)

#define VIDC_WRAPPER_INTR_STATUS_AR50	(VIDC_WRAPPER_BASE_OFFS_AR50 + 0x0C)
#define VIDC_WRAPPER_INTR_STATUS_A2HWD_BMSK_AR50	0x10
#define VIDC_WRAPPER_INTR_STATUS_A2H_BMSK_AR50	0x4

#define VIDC_WRAPPER_INTR_CLEAR_AR50		(VIDC_WRAPPER_BASE_OFFS_AR50 + 0x14)

#define VIDC_CTRL_INIT_AR50		VIDC_CPU_CS_SCIACMD_AR50

#define VIDC_CTRL_STATUS_AR50	VIDC_CPU_CS_SCIACMDARG0_AR50
#define VIDC_CTRL_ERROR_STATUS__M_AR50 \
		VIDC_CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_AR50
#define VIDC_CTRL_INIT_IDLE_MSG_BMSK_AR50 \
		VIDC_CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_AR50

#define VIDC_QTBL_INFO_AR50		VIDC_CPU_CS_SCIACMDARG1_AR50

#define VIDC_QTBL_ADDR_AR50		VIDC_CPU_CS_SCIACMDARG2_AR50

#define VIDC_SFR_ADDR_AR50		VIDC_CPU_CS_SCIBCMD_AR50
#define VIDC_MMAP_ADDR_AR50		VIDC_CPU_CS_SCIBCMDARG0_AR50
#define VIDC_UC_REGION_ADDR_AR50	VIDC_CPU_CS_SCIBARG1_AR50
#define VIDC_UC_REGION_SIZE_AR50	VIDC_CPU_CS_SCIBARG2_AR50

/*
 * --------------------------------------------------------------------------
 * MODULE: vcodec noc error log registers
 * --------------------------------------------------------------------------
 */
#define VCODEC_CORE0_VIDEO_NOC_BASE_OFFS		0x00004000
#define CVP_NOC_BASE_OFFS				0x0000C000
#define VCODEC_COREX_VIDEO_NOC_ERR_SWID_LOW_OFFS	0x0500
#define VCODEC_COREX_VIDEO_NOC_ERR_SWID_HIGH_OFFS	0x0504
#define VCODEC_COREX_VIDEO_NOC_ERR_MAINCTL_LOW_OFFS	0x0508
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRVLD_LOW_OFFS	0x0510
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRCLR_LOW_OFFS	0x0518
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG0_LOW_OFFS	0x0520
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG0_HIGH_OFFS	0x0524
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG1_LOW_OFFS	0x0528
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG1_HIGH_OFFS	0x052C
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG2_LOW_OFFS	0x0530
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG2_HIGH_OFFS	0x0534
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG3_LOW_OFFS	0x0538
#define VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG3_HIGH_OFFS	0x053C

static inline int __prepare_enable_clks(struct venus_hfi_device *device, u32 sid);

static void setup_dsp_uc_memmap_ar50(struct venus_hfi_device *device)
{
        /* initialize DSP QTBL & UCREGION with CPU queues */
        __write_register(device, HFI_DSP_QTBL_ADDR,
                        (u32)device->iface_q_table.align_device_addr, DEFAULT_SID);
        __write_register(device, HFI_DSP_UC_REGION_ADDR,
                        (u32)device->iface_q_table.align_device_addr, DEFAULT_SID);
        __write_register(device, HFI_DSP_UC_REGION_SIZE, SHARED_QSIZE, DEFAULT_SID);
}

void __interrupt_init_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, WRAPPER_INTR_MASK,
			WRAPPER_INTR_MASK_A2HVCODEC_BMSK, sid);
}

int __enable_regulators_ar50(struct venus_hfi_device *device)
{
	int rc = 0;

	d_vpr_h("Enabling regulators\n");

	rc = __enable_regulator_by_name(device, "venus");
	if (rc) {
		d_vpr_e("Failed to enable regualtor venus, rc = %d\n", rc);
		goto fail_regulator;
	}
	rc = __enable_regulator_by_name(device, "venus-core0");
	if (rc) {
		d_vpr_e("Failed to enable regualtor venus-core0, rc = %d\n", rc);
		goto fail_regulator_core;
	}

	return 0;

fail_regulator_core:
	__disable_regulator_by_name(device, "venus");
fail_regulator:
	return rc;
}

int __disable_regulators_ar50(struct venus_hfi_device *device)
{
	int rc = 0;

	d_vpr_h("Disabling regulators\n");

	rc = __disable_regulator_by_name(device, "venus-core0");
	if (rc) {
		d_vpr_e("%s: disable regulator venus-core0 failed, rc = %d\n", __func__, rc);
		rc = 0;
	}
	rc = __disable_regulator_by_name(device, "venus");
	if (rc) {
		d_vpr_e("%s: disable regulator venus failed, rc = %d\n", __func__, rc);
	}

	return rc;
}

void __setup_ucregion_memory_map_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, VIDC_UC_REGION_ADDR_AR50,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, VIDC_UC_REGION_SIZE_AR50, SHARED_QSIZE, sid);
	__write_register(device, VIDC_QTBL_ADDR_AR50,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, VIDC_QTBL_INFO_AR50, 0x01, sid);
	if (device->sfr.align_device_addr)
		__write_register(device, VIDC_SFR_ADDR_AR50,
				(u32)device->sfr.align_device_addr, sid);
	if (device->qdss.align_device_addr)
		__write_register(device, VIDC_MMAP_ADDR_AR50,
				(u32)device->qdss.align_device_addr, sid);
	setup_dsp_uc_memmap_ar50(device);
}

int __reset_ahb2axi_bridge_ar50(struct venus_hfi_device *device,
		u32 sid)
{
	int rc, i;

	if (!device) {
		d_vpr_e("NULL device\n");
		return -EINVAL;
	}

	for (i = 0; i < device->res->reset_set.count; i++) {

	    rc = __handle_reset_clk(device->res, i, ASSERT, sid);
	    if (rc) {
		    d_vpr_e("failed to assert reset clocks\n");
		    return rc;
	    }

	    /* wait for deassert */
	    usleep_range(150, 250);

	    rc = __handle_reset_clk(device->res, i, DEASSERT, sid);
	    if (rc) {
		    d_vpr_e("failed to deassert reset clocks\n");
		    return rc;
	    }
	}

	return 0;
}

int __prepare_pc_ar50(struct venus_hfi_device *device)
{
	int rc = 0;
	struct hfi_cmd_sys_pc_prep_packet pkt;

	rc = call_hfi_pkt_op(device, sys_pc_prep, &pkt);
	if (rc) {
		d_vpr_e("Failed to create sys pc prep pkt\n");
		goto err_pc_prep;
	}

	if (__iface_cmdq_write(device, &pkt, DEFAULT_SID))
		rc = -ENOTEMPTY;
	if (rc)
		d_vpr_e("Failed to prepare venus for power off");
err_pc_prep:
	return rc;
}

void __noc_error_info_ar50(struct venus_hfi_device *device)
{
	u32 noc_base_offs, val;

	if (!device) {
		d_vpr_e("%s: null device\n", __func__);
		return;
	}

	noc_base_offs =	VCODEC_CORE0_VIDEO_NOC_BASE_OFFS;

	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_SWID_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_SWID_LOW:     %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_SWID_HIGH_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_SWID_HIGH:    %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_MAINCTL_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_MAINCTL_LOW:  %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG0_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG0_LOW:  %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG0_HIGH_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG0_HIGH: %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG1_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG1_LOW:  %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG1_HIGH_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG1_HIGH: %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG2_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG2_LOW:  %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG2_HIGH_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG2_HIGH: %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG3_LOW_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG3_LOW:  %#x\n", val);
	val = __read_register(device, noc_base_offs +
			VCODEC_COREX_VIDEO_NOC_ERR_ERRLOG3_HIGH_OFFS, DEFAULT_SID);
	d_vpr_e("CORE%d_NOC_ERR_ERRLOG3_HIGH: %#x\n", val);
}

void __core_clear_interrupt_ar50(struct venus_hfi_device *device)
{
	u32 intr_status = 0, mask = 0;

	if (!device) {
		d_vpr_e("%s: NULL device\n", __func__);
		return;
	}

	intr_status = __read_register(device, VIDC_WRAPPER_INTR_STATUS_AR50, DEFAULT_SID);
	mask = (VIDC_WRAPPER_INTR_STATUS_A2H_BMSK_AR50 |
		VIDC_WRAPPER_INTR_STATUS_A2HWD_BMSK_AR50 |
		VIDC_CTRL_INIT_IDLE_MSG_BMSK_AR50);

	if (intr_status & mask) {
		device->intr_status |= intr_status;
		device->reg_count++;
		d_vpr_l("INTERRUPT for device: %pK: times: %d interrupt_status: %d\n",
			device, device->reg_count, intr_status);
	} else {
		device->spur_count++;
	}

	__write_register(device, VIDC_CPU_CS_A2HSOFTINTCLR_AR50, 1, DEFAULT_SID);
	__write_register(device, VIDC_WRAPPER_INTR_CLEAR_AR50, intr_status, DEFAULT_SID);
}

int __boot_firmware_ar50(struct venus_hfi_device *device, u32 sid)
{
	int rc = 0;
	u32 ctrl_init_val = 0, ctrl_status = 0, count = 0, max_tries = 1000;

	ctrl_init_val = BIT(0);

	__write_register(device, VIDC_CTRL_INIT_AR50, ctrl_init_val, sid);
	while (!ctrl_status && count < max_tries) {
		ctrl_status = __read_register(device, VIDC_CTRL_STATUS_AR50, sid);
		if ((ctrl_status & VIDC_CTRL_ERROR_STATUS__M_AR50) == 0x4) {
			d_vpr_e("invalid setting for UC_REGION\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		d_vpr_e("Error booting up vidc firmware\n");
		rc = -ETIME;
	}
	return rc;
}

bool __watchdog_ar50(u32 intr_status)
{
	bool rc = false;

	if (intr_status & VIDC_WRAPPER_INTR_STATUS_A2HWD_BMSK_AR50)
		rc = true;

        return rc;
}

static inline int __prepare_enable_clks(struct venus_hfi_device *device,
	u32 sid)
{
	struct clock_info *cl = NULL, *cl_fail = NULL;
	int rc = 0, c = 0;

	if (!device) {
		s_vpr_e(sid, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	venus_hfi_for_each_clock(device, cl) {
		/*
		 * For the clocks we control, set the rate prior to preparing
		 * them.  Since we don't really have a load at this point, scale
		 * it to the lowest frequency possible
		 */
		if (cl->has_scaling)
			__set_clk_rate(device, cl,
					clk_round_rate(cl->clk, 0), sid);

		if (__clk_is_enabled(cl->clk))
			s_vpr_e(sid, "%s: clock %s already enabled\n",
				__func__, cl->name);

		rc = clk_prepare_enable(cl->clk);
		if (rc) {
			s_vpr_e(sid, "Failed to enable clocks\n");
			cl_fail = cl;
			goto fail_clk_enable;
		}

		if (!__clk_is_enabled(cl->clk))
			s_vpr_e(sid, "%s: clock %s not enabled\n",
				__func__, cl->name);

		c++;
		s_vpr_h(sid, "Clock: %s prepared and enabled\n", cl->name);
	}

	call_venus_op(device, clock_config_on_enable, device, sid);
	return rc;

fail_clk_enable:
	venus_hfi_for_each_clock_reverse_continue(device, cl, c) {
		s_vpr_e(sid, "Clock: %s disable and unprepare\n",
			cl->name);
		clk_disable_unprepare(cl->clk);
	}

	return rc;
}

void __raise_interrupt_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, VIDC_CPU_IC_SOFTINT_AR50,
		1 << VIDC_CPU_IC_SOFTINT_H2A_SHFT_AR50, sid);

}

int __power_on_ar50(struct venus_hfi_device *device)
{
		int rc = 0;
		u32 sid = DEFAULT_SID;

		if (device->power_enabled)
			return 0;

		device->power_enabled = true;
		/* Vote for all hardware resources */
		rc = __vote_buses(device, INT_MAX, INT_MAX, sid);
		if (rc) {
			d_vpr_e("Failed to vote buses, err: %d\n", rc);
			goto fail_vote_buses;
		}

		rc = __enable_regulators_ar50(device);
		if (rc) {
			d_vpr_e("Failed to enable GDSC, err = %d\n", rc);
			goto fail_enable_gdsc;
		}

		rc = __reset_ahb2axi_bridge_ar50(device, sid);
		if (rc) {
			d_vpr_e("Failed to enable ahb2axi: %d\n", rc);
			goto fail_enable_clks;
		}

		rc = __prepare_enable_clks(device, sid);
		if (rc) {
			d_vpr_e("Failed to enable clocks: %d\n", rc);
			goto fail_enable_clks;
		}

		rc = __scale_clocks(device, sid);
		if (rc) {
			d_vpr_l(
					"Failed to scale clocks, performance might be affected\n");
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

	fail_enable_clks:
		__disable_regulators_ar50(device);
	fail_enable_gdsc:
		__unvote_buses(device, sid);
	fail_vote_buses:
		device->power_enabled = false;
		return rc;

}

void __power_off_ar50(struct venus_hfi_device *device)
{
	u32 sid = DEFAULT_SID;

	if (!device->power_enabled)
		return;

	if (!(device->intr_status & VIDC_WRAPPER_INTR_STATUS_A2HWD_BMSK_AR50))
		disable_irq_nosync(device->hal_data->irq);
	device->intr_status = 0;

	__disable_unprepare_clks(device);

	if (__disable_regulators_ar50(device))
		d_vpr_l("Failed to disable regulators\n");

	if (__unvote_buses(device, sid))
		d_vpr_l("Failed to unvote for buses\n");
	device->power_enabled = false;
}
