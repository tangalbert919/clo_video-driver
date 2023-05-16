// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include "hfi_common.h"
#include "hfi_io_common.h"

int __interrupt_init_iris1(struct venus_hfi_device *device, u32 sid)
{
	u32 mask_val = 0;

	/* All interrupts should be disabled initially 0x1F6 : Reset value */
	mask_val = __read_register(device, WRAPPER_INTR_MASK, sid);

	/* Write 0 to unmask CPU and WD interrupts */
	mask_val &= ~(WRAPPER_INTR_MASK_A2HWD_BMSK |
			WRAPPER_INTR_MASK_A2HCPU_BMSK);
	__write_register(device, WRAPPER_INTR_MASK, mask_val, sid);

	return 0;
}

int __setup_ucregion_memory_map_iris1(struct venus_hfi_device *device, u32 sid)
{
	/* initialize CPU QTBL & UCREGION */
	__write_register(device, UC_REGION_ADDR,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, UC_REGION_SIZE, SHARED_QSIZE, sid);
	__write_register(device, QTBL_ADDR,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, QTBL_INFO, 0x01, sid);
	if (device->sfr.align_device_addr)
		__write_register(device, SFR_ADDR,
				(u32)device->sfr.align_device_addr, sid);
	if (device->qdss.align_device_addr)
		__write_register(device, MMAP_ADDR,
				(u32)device->qdss.align_device_addr, sid);

	/* initialize DSP QTBL & UCREGION with CPU queues by default */
	__write_register(device, HFI_DSP_QTBL_ADDR,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, HFI_DSP_UC_REGION_ADDR,
			(u32)device->iface_q_table.align_device_addr, sid);
	__write_register(device, HFI_DSP_UC_REGION_SIZE, SHARED_QSIZE, sid);
	if (device->res->cvp_internal) {
		/* initialize DSP QTBL & UCREGION with DSP queues */
		__write_register(device, HFI_DSP_QTBL_ADDR,
			(u32)device->dsp_iface_q_table.align_device_addr, sid);
		__write_register(device, HFI_DSP_UC_REGION_ADDR,
			(u32)device->dsp_iface_q_table.align_device_addr, sid);
		__write_register(device, HFI_DSP_UC_REGION_SIZE,
			device->dsp_iface_q_table.mem_data.size, sid);
	}

	return 0;
}

int  __clock_config_on_enable_iris1(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, WRAPPER_CPU_CGC_DIS, 0, sid);
	__write_register(device, WRAPPER_CPU_CLOCK_CONFIG, 0, sid);

	return 0;
}

int __enable_regulators_iris1(struct venus_hfi_device *device)
{
	int rc = 0;

	d_vpr_h("Enabling regulators\n");

	rc = __enable_regulator_by_name(device, "iris-ctl");
	if (rc) {
		d_vpr_e("Failed to enable regualtor iris-ctl, rc = %d\n", rc);
		goto fail_regulator;
	}
	rc = __enable_regulator_by_name(device, "vcodec");
	if (rc) {
		d_vpr_e("Failed to enable regualtor vcodec, rc = %d\n", rc);
		goto fail_regulator_vcodec;
	}
	rc = __enable_regulator_by_name(device, "cvp");
	if (rc) {
		d_vpr_e("Failed to enable regualtor cvp, rc = %d\n", rc);
		goto fail_regulator_cvp;
	}

	return 0;

fail_regulator_cvp:
	__disable_regulator_by_name(device, "vcodec");
fail_regulator_vcodec:
	__disable_regulator_by_name(device, "iris-ctl");
fail_regulator:
	return rc;

}

int __disable_regulators_iris1(struct venus_hfi_device *device)
{
	int rc = 0;

	d_vpr_h("Disabling regulators\n");

	rc = __disable_regulator_by_name(device, "cvp");
	if (rc) {
		d_vpr_e("%s: disable regulator vcodec failed, rc = %d\n", __func__, rc);
		rc = 0;
	}
	rc = __disable_regulator_by_name(device, "vcodec");
	if (rc) {
		d_vpr_e("%s: disable regulator vcodec failed, rc = %d\n", __func__, rc);
		rc = 0;
	}
	rc = __disable_regulator_by_name(device, "iris-ctl");
	if (rc) {
		d_vpr_e("%s: disable regulator iris-ctl failed rc = %d\n", __func__, rc);
	}

	return rc;
}
