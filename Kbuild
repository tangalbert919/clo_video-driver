# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(filter y,$(CONFIG_ARCH_LEMANS) $(CONFIG_QTI_QUIN_GVM)),)
KBUILD_CPPFLAGS += -DCONFIG_MSM_MMRM=1
endif

ifeq ($(CONFIG_QTI_QUIN_GVM), y)
export CONFIG_MSM_VIDC_V4L2=n
else
export CONFIG_MSM_VIDC_V4L2=m
endif

ifeq ($(CONFIG_ARCH_WAIPIO), y)
include $(VIDEO_ROOT)/config/waipio_video.conf
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/waipio_video.h \
                   -I$(VIDEO_ROOT)/driver/platform/waipio/inc
endif

ifeq ($(CONFIG_ARCH_KALAMA), y)
include $(VIDEO_ROOT)/config/kalama_video.conf
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/kalama_video.h \
                   -I$(VIDEO_ROOT)/driver/platform/kalama/inc
endif

ifeq ($(CONFIG_ARCH_PINEAPPLE), y)
include $(VIDEO_ROOT)/config/pineapple_video.conf
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/pineapple_video.h \
                   -I$(VIDEO_ROOT)/driver/platform/pineapple/inc
endif

ifeq ($(CONFIG_ARCH_ANORAK), y)
include $(VIDEO_ROOT)/config/anorak_video.conf
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/anorak_video.h \
                   -I$(VIDEO_ROOT)/driver/platform/anorak/inc
endif

ifeq ($(CONFIG_ARCH_LEMANS), y)
include $(VIDEO_ROOT)/config/lemans_video.conf
LINUXINCLUDE    += -include $(VIDEO_ROOT)/config/lemans_video.h \
                   -I$(VIDEO_ROOT)/driver/platform/lemans/inc
endif

LINUXINCLUDE    += -I$(VIDEO_ROOT)/driver/vidc/inc \
                   -I$(VIDEO_ROOT)/driver/platform/common/inc \
                   -I$(VIDEO_ROOT)/driver/variant/common/inc \
                   -I$(VIDEO_ROOT)/include/uapi/vidc

USERINCLUDE     += -I$(VIDEO_ROOT)/include/uapi/vidc/media \
                   -I$(VIDEO_ROOT)/include/uapi/vidc

obj-$(CONFIG_MSM_VIDC_V4L2) += msm_video.o

ifeq ($(CONFIG_MSM_VIDC_WAIPIO), y)
msm_video-objs += driver/platform/waipio/src/msm_vidc_waipio.o
endif

ifeq ($(CONFIG_MSM_VIDC_KALAMA), y)
msm_video-objs += driver/platform/kalama/src/msm_vidc_kalama.o
endif

ifeq ($(CONFIG_MSM_VIDC_PINEAPPLE), y)
msm_video-objs += driver/platform/pineapple/src/msm_vidc_pineapple.o
endif

ifeq ($(CONFIG_MSM_VIDC_ANORAK), y)
msm_video-objs += driver/platform/anorak/src/msm_vidc_anorak.o
endif

ifeq ($(CONFIG_MSM_VIDC_LEMANS), y)
msm_video-objs += driver/platform/lemans/src/msm_vidc_lemans.o
endif

ifeq ($(CONFIG_MSM_VIDC_IRIS2), y)
LINUXINCLUDE   += -I$(VIDEO_ROOT)/driver/variant/iris2/inc
msm_video-objs += driver/variant/iris2/src/msm_vidc_buffer_iris2.o \
                  driver/variant/iris2/src/msm_vidc_power_iris2.o \
                  driver/variant/iris2/src/msm_vidc_iris2.o
endif

ifeq ($(CONFIG_MSM_VIDC_IRIS3), y)
LINUXINCLUDE    += -I$(VIDEO_ROOT)/driver/variant/iris3/inc
msm_video-objs += driver/variant/iris3/src/msm_vidc_buffer_iris3.o \
                  driver/variant/iris3/src/msm_vidc_power_iris3.o \
                  driver/variant/iris3/src/msm_vidc_iris3.o
endif

ifeq ($(CONFIG_MSM_VIDC_IRIS33), y)
LINUXINCLUDE    += -I$(VIDEO_ROOT)/driver/variant/iris33/inc
msm_video-objs += driver/variant/iris33/src/msm_vidc_buffer_iris33.o \
                  driver/variant/iris33/src/msm_vidc_power_iris33.o \
                  driver/variant/iris33/src/msm_vidc_iris33.o
endif

msm_video-objs += driver/vidc/src/msm_vidc_v4l2.o \
                  driver/vidc/src/msm_vidc_vb2.o \
                  driver/vidc/src/msm_vidc.o \
                  driver/vidc/src/msm_vdec.o \
                  driver/vidc/src/msm_venc.o \
                  driver/vidc/src/msm_vidc_driver.o \
                  driver/vidc/src/msm_vidc_control.o \
                  driver/vidc/src/msm_vidc_control_ext.o \
                  driver/vidc/src/msm_vidc_buffer.o \
                  driver/vidc/src/msm_vidc_power.o \
                  driver/vidc/src/msm_vidc_probe.o \
                  driver/vidc/src/resources.o \
                  driver/vidc/src/firmware.o \
                  driver/vidc/src/msm_vidc_debug.o \
                  driver/vidc/src/msm_vidc_memory.o \
                  driver/vidc/src/msm_vidc_memory_ext.o \
                  driver/vidc/src/msm_vidc_fence.o \
                  driver/vidc/src/venus_hfi.o \
                  driver/vidc/src/venus_hfi_queue.o \
                  driver/vidc/src/hfi_packet.o \
                  driver/vidc/src/venus_hfi_response.o \
                  driver/platform/common/src/msm_vidc_platform.o \
                  driver/variant/common/src/msm_vidc_variant.o
