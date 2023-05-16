// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include "hfi_common.h"
#include "hfi_io_common.h"

int __interrupt_init_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, WRAPPER_INTR_MASK,
			WRAPPER_INTR_MASK_A2HVCODEC_BMSK, sid);

	return 0;
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
