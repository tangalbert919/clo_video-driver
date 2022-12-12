# SPDX-License-Identifier: GPL-2.0-only

# Build video kernel driver
ifeq ($(ENABLE_HYP),false)
$(warning "adding msm_vidc.ko to BOARD_VENDOR_KERNEL_MODULES")
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/msm-vidc.ko
endif
