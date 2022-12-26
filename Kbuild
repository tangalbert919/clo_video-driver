# SPDX-License-Identifier: GPL-2.0-only

# auto-detect subdirs
ifeq (y, $(findstring y, $(CONFIG_ARCH_SA8155) $(CONFIG_ARCH_SA8195)))
include $(VIDEO_ROOT)/config/gen3autovid.conf
endif

ifeq (y, $(findstring y, $(CONFIG_ARCH_SA8155) $(CONFIG_ARCH_SA8195)))
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/gen3autovidconf.h
endif

LINUXINCLUDE    += -I$(VIDEO_ROOT)/include \
                   -I$(VIDEO_ROOT)/include/uapi

USERINCLUDE     += -I$(VIDEO_ROOT)/include/uapi

obj-$(CONFIG_MSM_VIDC_V4L2) := msm-vidc.o

msm-vidc-objs := msm/vidc/msm_v4l2_vidc.o \
                 msm/vidc/msm_v4l2_private.o \
                 msm/vidc/msm_vidc_platform.o \
                 msm/vidc/msm_vidc_common.o \
                 msm/vidc/msm_vidc.o \
                 msm/vidc/msm_vdec.o \
                 msm/vidc/msm_venc.o \
                 msm/vidc/msm_cvp_internal.o \
                 msm/vidc/msm_smem.o \
                 msm/vidc/msm_vidc_debug.o \
                 msm/vidc/msm_vidc_res_parse.o \
                 msm/vidc/hfi_common.o \
                 msm/vidc/hfi_ar50.o \
                 msm/vidc/hfi_ar50_lt.o \
                 msm/vidc/hfi_iris1.o \
                 msm/vidc/hfi_iris2.o \
                 msm/vidc/hfi_response_handler.o \
                 msm/vidc/hfi_packetization.o \
                 msm/vidc/vidc_hfi.o \
                 msm/vidc/msm_vidc_clocks.o \
                 msm/vidc/msm_vidc_bus_ar50lite.o\
                 msm/vidc/msm_vidc_bus_iris1.o \
                 msm/vidc/msm_vidc_bus_iris2.o \
                 msm/vidc/msm_vidc_buffer_calculations.o
