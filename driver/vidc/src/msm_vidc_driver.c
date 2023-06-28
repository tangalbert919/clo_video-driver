// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iommu.h>
#include <linux/workqueue.h>
#include "msm_media_info.h"

#include "msm_vidc_driver.h"
#include "msm_vidc_platform.h"
#include "msm_vidc_internal.h"
#include "msm_vidc_control.h"
#include "msm_vidc_memory.h"
#include "msm_vidc_power.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_power.h"
#include "msm_vidc.h"
#include "msm_vdec.h"
#include "msm_venc.h"
#include "msm_vidc_fence.h"
#include "venus_hfi.h"
#include "venus_hfi_response.h"
#include "hfi_packet.h"
#include "msm_vidc_events.h"

extern struct msm_vidc_core *g_core;

#define is_odd(val) ((val) % 2 == 1)
#define in_range(val, min, max) (((min) <= (val)) && ((val) <= (max)))
#define COUNT_BITS(a, out) {       \
	while ((a) >= 1) {          \
		(out) += (a) & (1); \
		(a) >>= (1);        \
	}                           \
}

#define SSR_TYPE 0x0000000F
#define SSR_TYPE_SHIFT 0
#define SSR_SUB_CLIENT_ID 0x000000F0
#define SSR_SUB_CLIENT_ID_SHIFT 4
#define SSR_ADDR_ID 0xFFFFFFFF00000000
#define SSR_ADDR_SHIFT 32

#define STABILITY_TYPE 0x0000000F
#define STABILITY_TYPE_SHIFT 0
#define STABILITY_SUB_CLIENT_ID 0x000000F0
#define STABILITY_SUB_CLIENT_ID_SHIFT 4
#define STABILITY_PAYLOAD_ID 0xFFFFFFFF00000000
#define STABILITY_PAYLOAD_SHIFT 32

/* do not modify the cap names as it is used in test scripts */
static const char * const cap_name_arr[] =
	FOREACH_CAP(GENERATE_STRING);

const char *cap_name(enum msm_vidc_inst_capability_type cap_id)
{
	const char *name = "UNKNOWN CAP";

	if (cap_id >= ARRAY_SIZE(cap_name_arr))
		goto exit;

	name = cap_name_arr[cap_id];

exit:
	return name;
}

static const char * const buf_type_name_arr[] =
	FOREACH_BUF_TYPE(GENERATE_STRING);

const char *buf_name(enum msm_vidc_buffer_type type)
{
	const char *name = "UNKNOWN BUF";

	if (type >= ARRAY_SIZE(buf_type_name_arr))
		goto exit;

	name = buf_type_name_arr[type];

exit:
	return name;
}

static const char * const inst_allow_name_arr[] =
	FOREACH_ALLOW(GENERATE_STRING);

const char *allow_name(enum msm_vidc_allow allow)
{
	const char *name = "UNKNOWN";

	if (allow >= ARRAY_SIZE(inst_allow_name_arr))
		goto exit;

	name = inst_allow_name_arr[allow];

exit:
	return name;
}

/* do not modify the state names as it is used in test scripts */
static const char * const state_name_arr[] =
	FOREACH_STATE(GENERATE_STRING);

const char *state_name(enum msm_vidc_state state)
{
	const char *name = "UNKNOWN STATE";

	if (state >= ARRAY_SIZE(state_name_arr))
		goto exit;

	name = state_name_arr[state];

exit:
	return name;
}

const char *sub_state_name(enum msm_vidc_sub_state sub_state)
{
	switch (sub_state) {
	case MSM_VIDC_DRAIN:               return "DRAIN ";
	case MSM_VIDC_DRC:                 return "DRC ";
	case MSM_VIDC_DRAIN_LAST_BUFFER:   return "DRAIN_LAST_BUFFER ";
	case MSM_VIDC_DRC_LAST_BUFFER:     return "DRC_LAST_BUFFER ";
	case MSM_VIDC_INPUT_PAUSE:         return "INPUT_PAUSE ";
	case MSM_VIDC_OUTPUT_PAUSE:        return "OUTPUT_PAUSE ";
	}

	return "SUB_STATE_NONE";
}

static const char * const core_state_name_arr[] =
	FOREACH_CORE_STATE(GENERATE_STRING);

const char *core_state_name(enum msm_vidc_core_state state)
{
	const char *name = "UNKNOWN STATE";

	if (state >= ARRAY_SIZE(core_state_name_arr))
		goto exit;

	name = core_state_name_arr[state];

exit:
	return name;
}

const char *core_sub_state_name(enum msm_vidc_core_sub_state sub_state)
{
	switch (sub_state) {
	case CORE_SUBSTATE_NONE:                 return "NONE ";
	case CORE_SUBSTATE_GDSC_HANDOFF:         return "GDSC_HANDOFF ";
	case CORE_SUBSTATE_PM_SUSPEND:           return "PM_SUSPEND ";
	case CORE_SUBSTATE_FW_PWR_CTRL:          return "FW_PWR_CTRL ";
	case CORE_SUBSTATE_POWER_ENABLE:         return "POWER_ENABLE ";
	case CORE_SUBSTATE_PAGE_FAULT:           return "PAGE_FAULT ";
	case CORE_SUBSTATE_CPU_WATCHDOG:         return "CPU_WATCHDOG ";
	case CORE_SUBSTATE_VIDEO_UNRESPONSIVE:   return "VIDEO_UNRESPONSIVE ";
	case CORE_SUBSTATE_MAX:                  return "MAX ";
	}

	return "UNKNOWN ";
}

const char *v4l2_type_name(u32 port)
{
	switch (port) {
	case INPUT_MPLANE:      return "INPUT";
	case OUTPUT_MPLANE:     return "OUTPUT";
	case INPUT_META_PLANE:  return "INPUT_META";
	case OUTPUT_META_PLANE: return "OUTPUT_META";
	}

	return "UNKNOWN";
}

const char *v4l2_pixelfmt_name(struct msm_vidc_inst *inst, u32 pixfmt)
{
	struct msm_vidc_core *core;
	const struct codec_info *codec_info;
	const struct color_format_info *color_format_info;
	u32 i, size;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		goto exit;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		goto exit;
	}
	codec_info = core->platform->data.format_data->codec_info;
	size = core->platform->data.format_data->codec_info_size;

	for (i = 0; i < size; i++) {
		if (codec_info[i].v4l2_codec == pixfmt)
			return codec_info[i].pixfmt_name;
	}

	color_format_info = core->platform->data.format_data->color_format_info;
	size = core->platform->data.format_data->color_format_info_size;

	for (i = 0; i < size; i++) {
		if (color_format_info[i].v4l2_color_format == pixfmt)
			return color_format_info[i].pixfmt_name;
	}

exit:
	return "UNKNOWN";
}

void print_vidc_buffer(u32 tag, const char *tag_str, const char *str, struct msm_vidc_inst *inst,
		struct msm_vidc_buffer *vbuf)
{
	struct dma_buf *dbuf;
	struct inode *f_inode;
	unsigned long inode_num = 0;
	long ref_count = -1;

	if (!inst || !vbuf || !tag_str || !str)
		return;

	dbuf = (struct dma_buf *)vbuf->dmabuf;
	if (dbuf && dbuf->file) {
		f_inode = file_inode(dbuf->file);
		if (f_inode) {
			inode_num = f_inode->i_ino;
			ref_count = file_count(dbuf->file);
		}
	}

	dprintk_inst(tag, tag_str, inst,
		"%s: %s: idx %2d fd %3d off %d daddr %#llx inode %8lu ref %2ld size %8d filled %8d flags %#x ts %8lld attr %#x dbuf_get %d attach %d map %d counts(etb ebd ftb fbd) %4llu %4llu %4llu %4llu\n",
		str, buf_name(vbuf->type),
		vbuf->index, vbuf->fd, vbuf->data_offset,
		vbuf->device_addr, inode_num, ref_count, vbuf->buffer_size,
		vbuf->data_size, vbuf->flags, vbuf->timestamp, vbuf->attr,
		vbuf->dbuf_get, vbuf->attach ? 1 : 0, vbuf->sg_table ? 1 : 0,
		inst->debug_count.etb, inst->debug_count.ebd,
		inst->debug_count.ftb, inst->debug_count.fbd);

	trace_msm_v4l2_vidc_buffer_event_log(inst, str, buf_name(vbuf->type), vbuf,
		inode_num, ref_count);
}

void print_vb2_buffer(const char *str, struct msm_vidc_inst *inst,
		struct vb2_buffer *vb2)
{
	if (!inst || !vb2)
		return;

	if (vb2->type == INPUT_MPLANE || vb2->type == OUTPUT_MPLANE) {
		i_vpr_e(inst,
			"%s: %s: idx %2d fd %d off %d size %d filled %d\n",
			str, vb2->type == INPUT_MPLANE ? "INPUT" : "OUTPUT",
			vb2->index, vb2->planes[0].m.fd,
			vb2->planes[0].data_offset, vb2->planes[0].length,
			vb2->planes[0].bytesused);
	} else if (vb2->type == INPUT_META_PLANE || vb2->type == OUTPUT_META_PLANE) {
		i_vpr_e(inst,
			"%s: %s: idx %2d fd %d off %d size %d filled %d\n",
			str, vb2->type == INPUT_MPLANE ? "INPUT_META" : "OUTPUT_META",
			vb2->index, vb2->planes[0].m.fd,
			vb2->planes[0].data_offset, vb2->planes[0].length,
			vb2->planes[0].bytesused);
	}
}

static void print_buffer_stats(u32 tag, const char *tag_str, struct msm_vidc_inst *inst,
		struct msm_vidc_buffer_stats *stats)
{
	if (!tag_str || !inst || !stats)
		return;

	/* skip flushed buffer stats */
	if (!stats->etb_time_ms || !stats->ebd_time_ms ||
	    !stats->ftb_time_ms || !stats->fbd_time_ms)
		return;

	dprintk_inst(tag, tag_str, inst,
		"f.no %4u ts %16llu (etb ebd ftb fbd)ms %6u %6u %6u %6u (ebd-etb fbd-etb etb-ftb)ms %4d %4d %4d size %8u attr %#x\n",
		stats->frame_num, stats->timestamp, stats->etb_time_ms, stats->ebd_time_ms,
		stats->ftb_time_ms, stats->fbd_time_ms, stats->ebd_time_ms - stats->etb_time_ms,
		stats->fbd_time_ms - stats->etb_time_ms, stats->etb_time_ms - stats->ftb_time_ms,
		stats->data_size, stats->flags);
}

static void __fatal_error(bool fatal)
{
	WARN_ON(fatal);
}

static int __strict_check(struct msm_vidc_core *core, const char *function)
{
	bool fatal = !mutex_is_locked(&core->lock);

	__fatal_error(fatal);

	if (fatal)
		d_vpr_e("%s: strict check failed\n", function);

	return fatal ? -EINVAL : 0;
}

static u32 msm_vidc_get_buffer_stats_flag(struct msm_vidc_inst *inst)
{
	u32 flags = 0;

	if (inst->hfi_frame_info.data_corrupt)
		flags |= MSM_VIDC_STATS_FLAG_CORRUPT;

	if (inst->hfi_frame_info.overflow)
		flags |= MSM_VIDC_STATS_FLAG_OVERFLOW;

	if (inst->hfi_frame_info.no_output)
		flags |= MSM_VIDC_STATS_FLAG_NO_OUTPUT;

	return flags;
}

int msm_vidc_suspend_locked(struct msm_vidc_core *core)
{
	int rc = 0;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = venus_hfi_suspend(core);
	if (rc)
		return rc;

	return rc;
}

static int msm_vidc_try_suspend(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;
	int rc = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	core_lock(core, __func__);
	if (list_empty(&core->instances) && list_empty(&core->dangling_instances)) {
		i_vpr_h(inst, "%s: closed last open session. suspend video core\n", __func__);
		msm_vidc_suspend_locked(core);
	}
	core_unlock(core, __func__);

	return rc;
}

int msm_vidc_add_buffer_stats(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf)
{
	struct msm_vidc_buffer_stats *stats = NULL;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* stats applicable only to input & output buffers */
	if (buf->type != MSM_VIDC_BUF_INPUT && buf->type != MSM_VIDC_BUF_OUTPUT)
		return -EINVAL;

	/* update start timestamp */
	buf->start_time_ms = (ktime_get_ns() / 1000 - inst->initial_time_us) / 1000;

	/* add buffer stats only in ETB path */
	if (buf->type != MSM_VIDC_BUF_INPUT)
		return 0;

	stats = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_BUF_STATS);
	if (!stats)
		return -ENOMEM;
	INIT_LIST_HEAD(&stats->list);
	list_add_tail(&stats->list, &inst->buffer_stats_list);

	stats->frame_num = inst->debug_count.etb;
	stats->timestamp = buf->timestamp;
	stats->etb_time_ms = buf->start_time_ms;
	if (is_decode_session(inst))
		stats->data_size =  buf->data_size;

	return 0;
}

int msm_vidc_remove_buffer_stats(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf)
{
	struct msm_vidc_buffer_stats *stats = NULL, *dummy_stats = NULL;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* stats applicable only to input & output buffers */
	if (buf->type != MSM_VIDC_BUF_INPUT && buf->type != MSM_VIDC_BUF_OUTPUT)
		return -EINVAL;

	/* update end timestamp */
	buf->end_time_ms = (ktime_get_ns() / 1000 - inst->initial_time_us) / 1000;

	list_for_each_entry_safe(stats, dummy_stats, &inst->buffer_stats_list, list) {
		if (stats->timestamp == buf->timestamp) {
			if (buf->type == MSM_VIDC_BUF_INPUT) {
				/* skip - already updated(multiple input - single output case) */
				if (stats->ebd_time_ms)
					continue;

				/* ebd: update end ts and return */
				stats->ebd_time_ms = buf->end_time_ms;
				stats->flags |= msm_vidc_get_buffer_stats_flag(inst);

				/* remove entry - no output attached */
				if (stats->flags & MSM_VIDC_STATS_FLAG_NO_OUTPUT) {
					list_del_init(&stats->list);
					msm_vidc_pool_free(inst, stats);
				}
			} else if (buf->type == MSM_VIDC_BUF_OUTPUT) {
				/* skip - ebd not arrived(single input - multiple output case) */
				if (!stats->ebd_time_ms)
					continue;

				/* fbd: update end ts and remove entry */
				list_del_init(&stats->list);
				stats->ftb_time_ms = buf->start_time_ms;
				stats->fbd_time_ms = buf->end_time_ms;
				stats->flags |= msm_vidc_get_buffer_stats_flag(inst);
				if (is_encode_session(inst))
					stats->data_size = buf->data_size;

				print_buffer_stats(VIDC_STAT, "stat", inst, stats);

				msm_vidc_pool_free(inst, stats);
			}
		}
	}

	return 0;
}

int msm_vidc_flush_buffer_stats(struct msm_vidc_inst *inst)
{
	struct msm_vidc_buffer_stats *stats, *dummy_stats;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	i_vpr_l(inst, "%s: flush buffer_stats list\n", __func__);
	list_for_each_entry_safe(stats, dummy_stats, &inst->buffer_stats_list, list) {
		list_del_init(&stats->list);
		msm_vidc_pool_free(inst, stats);
	}

	/* reset initial ts as well to avoid huge delta */
	inst->initial_time_us = ktime_get_ns() / 1000;

	return 0;
}

enum msm_vidc_buffer_type v4l2_type_to_driver(u32 type, const char *func)
{
	enum msm_vidc_buffer_type buffer_type = 0;

	switch (type) {
	case INPUT_MPLANE:
		buffer_type = MSM_VIDC_BUF_INPUT;
		break;
	case OUTPUT_MPLANE:
		buffer_type = MSM_VIDC_BUF_OUTPUT;
		break;
	case INPUT_META_PLANE:
		buffer_type = MSM_VIDC_BUF_INPUT_META;
		break;
	case OUTPUT_META_PLANE:
		buffer_type = MSM_VIDC_BUF_OUTPUT_META;
		break;
	default:
		d_vpr_e("%s: invalid v4l2 buffer type %#x\n", func, type);
		break;
	}
	return buffer_type;
}

u32 v4l2_type_from_driver(enum msm_vidc_buffer_type buffer_type,
	const char *func)
{
	u32 type = 0;

	switch (buffer_type) {
	case MSM_VIDC_BUF_INPUT:
		type = INPUT_MPLANE;
		break;
	case MSM_VIDC_BUF_OUTPUT:
		type = OUTPUT_MPLANE;
		break;
	case MSM_VIDC_BUF_INPUT_META:
		type = INPUT_META_PLANE;
		break;
	case MSM_VIDC_BUF_OUTPUT_META:
		type = OUTPUT_META_PLANE;
		break;
	default:
		d_vpr_e("%s: invalid driver buffer type %d\n",
			func, buffer_type);
		break;
	}
	return type;
}

enum msm_vidc_codec_type v4l2_codec_to_driver(struct msm_vidc_inst *inst,
	u32 v4l2_codec, const char *func)
{
	struct msm_vidc_core *core;
	const struct codec_info *codec_info;
	u32 i, size;
	enum msm_vidc_codec_type codec = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	codec_info = core->platform->data.format_data->codec_info;
	size = core->platform->data.format_data->codec_info_size;

	for (i = 0; i < size; i++) {
		if (codec_info[i].v4l2_codec == v4l2_codec)
			return codec_info[i].vidc_codec;
	}

	d_vpr_h("%s: invalid v4l2 codec %#x\n", func, v4l2_codec);
	return codec;
}

u32 v4l2_codec_from_driver(struct msm_vidc_inst *inst,
	enum msm_vidc_codec_type codec, const char *func)
{
	struct msm_vidc_core *core;
	const struct codec_info *codec_info;
	u32 i, size;
	u32 v4l2_codec = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	codec_info = core->platform->data.format_data->codec_info;
	size = core->platform->data.format_data->codec_info_size;

	for (i = 0; i < size; i++) {
		if (codec_info[i].vidc_codec == codec)
			return codec_info[i].v4l2_codec;
	}

	d_vpr_e("%s: invalid driver codec %#x\n", func, codec);
	return v4l2_codec;
}

enum msm_vidc_colorformat_type v4l2_colorformat_to_driver(
	struct msm_vidc_inst *inst,
	u32 v4l2_colorformat, const char *func)
{
	struct msm_vidc_core *core;
	const struct color_format_info *color_format_info;
	u32 i, size;
	enum msm_vidc_colorformat_type colorformat = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	color_format_info = core->platform->data.format_data->color_format_info;
	size = core->platform->data.format_data->color_format_info_size;

	for (i = 0; i < size; i++) {
		if (color_format_info[i].v4l2_color_format == v4l2_colorformat)
			return color_format_info[i].vidc_color_format;
	}

	d_vpr_e("%s: invalid v4l2 color format %#x\n", func, v4l2_colorformat);
	return colorformat;
}

u32 v4l2_colorformat_from_driver(struct msm_vidc_inst *inst,
	enum msm_vidc_colorformat_type colorformat,
	const char *func)
{
	struct msm_vidc_core *core;
	const struct color_format_info *color_format_info;
	u32 i, size;
	u32 v4l2_colorformat = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	color_format_info = core->platform->data.format_data->color_format_info;
	size = core->platform->data.format_data->color_format_info_size;

	for (i = 0; i < size; i++) {
		if (color_format_info[i].vidc_color_format == colorformat)
			return color_format_info[i].v4l2_color_format;
	}

	d_vpr_e("%s: invalid driver color format %#x\n", func, colorformat);
	return v4l2_colorformat;
}

u32 v4l2_color_primaries_to_driver(struct msm_vidc_inst *inst,
	u32 v4l2_primaries, const char *func)
{
	struct msm_vidc_core *core;
	const struct color_primaries_info *color_prim_info;
	u32 i, size;
	u32 vidc_color_primaries = MSM_VIDC_PRIMARIES_RESERVED;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	color_prim_info = core->platform->data.format_data->color_prim_info;
	size = core->platform->data.format_data->color_prim_info_size;

	for (i = 0; i < size; i++) {
		if (color_prim_info[i].v4l2_color_primaries == v4l2_primaries)
			return color_prim_info[i].vidc_color_primaries;
	}

	i_vpr_e(inst, "%s: invalid v4l2 color primaries %d\n",
		func, v4l2_primaries);

	return vidc_color_primaries;
}

u32 v4l2_color_primaries_from_driver(struct msm_vidc_inst *inst,
	u32 vidc_color_primaries, const char *func)
{
	struct msm_vidc_core *core;
	const struct color_primaries_info *color_prim_info;
	u32 i, size;
	u32 v4l2_primaries = V4L2_COLORSPACE_DEFAULT;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	color_prim_info = core->platform->data.format_data->color_prim_info;
	size = core->platform->data.format_data->color_prim_info_size;

	for (i = 0; i < size; i++) {
		if (color_prim_info[i].vidc_color_primaries == vidc_color_primaries)
			return color_prim_info[i].v4l2_color_primaries;
	}

	i_vpr_e(inst, "%s: invalid hfi color primaries %d\n",
		func, vidc_color_primaries);

	return v4l2_primaries;
}

u32 v4l2_transfer_char_to_driver(struct msm_vidc_inst *inst,
	u32 v4l2_transfer_char, const char *func)
{
	struct msm_vidc_core *core;
	const struct transfer_char_info *transfer_char_info;
	u32 i, size;
	u32 vidc_transfer_char = MSM_VIDC_TRANSFER_RESERVED;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	transfer_char_info = core->platform->data.format_data->transfer_char_info;
	size = core->platform->data.format_data->transfer_char_info_size;

	for (i = 0; i < size; i++) {
		if (transfer_char_info[i].v4l2_transfer_char == v4l2_transfer_char)
			return transfer_char_info[i].vidc_transfer_char;
	}

	i_vpr_e(inst, "%s: invalid v4l2 transfer char %d\n",
		func, v4l2_transfer_char);

	return vidc_transfer_char;
}

u32 v4l2_transfer_char_from_driver(struct msm_vidc_inst *inst,
	u32 vidc_transfer_char, const char *func)
{
	struct msm_vidc_core *core;
	const struct transfer_char_info *transfer_char_info;
	u32 i, size;
	u32  v4l2_transfer_char = V4L2_XFER_FUNC_DEFAULT;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	transfer_char_info = core->platform->data.format_data->transfer_char_info;
	size = core->platform->data.format_data->transfer_char_info_size;

	for (i = 0; i < size; i++) {
		if (transfer_char_info[i].vidc_transfer_char == vidc_transfer_char)
			return transfer_char_info[i].v4l2_transfer_char;
	}

	i_vpr_e(inst, "%s: invalid hfi transfer char %d\n",
			func, vidc_transfer_char);

	return v4l2_transfer_char;
}

u32 v4l2_matrix_coeff_to_driver(struct msm_vidc_inst *inst,
	u32 v4l2_matrix_coeff, const char *func)
{
	struct msm_vidc_core *core;
	const struct matrix_coeff_info *matrix_coeff_info;
	u32 i, size;
	u32 vidc_matrix_coeff = MSM_VIDC_MATRIX_COEFF_RESERVED;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	matrix_coeff_info = core->platform->data.format_data->matrix_coeff_info;
	size = core->platform->data.format_data->matrix_coeff_info_size;

	for (i = 0; i < size; i++) {
		if (matrix_coeff_info[i].v4l2_matrix_coeff == v4l2_matrix_coeff)
			return matrix_coeff_info[i].vidc_matrix_coeff;
	}

	i_vpr_e(inst, "%s: invalid v4l2 matrix coeff %d\n",
		func, v4l2_matrix_coeff);

	return vidc_matrix_coeff;
}

u32 v4l2_matrix_coeff_from_driver(struct msm_vidc_inst *inst,
	u32 vidc_matrix_coeff, const char *func)
{
	struct msm_vidc_core *core;
	const struct matrix_coeff_info *matrix_coeff_info;
	u32 i, size;
	u32 v4l2_matrix_coeff = V4L2_YCBCR_ENC_DEFAULT;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->platform || !core->platform->data.format_data) {
		d_vpr_e("%s: invalid core platform\n", __func__);
		return -EINVAL;
	}
	matrix_coeff_info = core->platform->data.format_data->matrix_coeff_info;
	size = core->platform->data.format_data->matrix_coeff_info_size;

	for (i = 0; i < size; i++) {
		if (matrix_coeff_info[i].vidc_matrix_coeff == vidc_matrix_coeff)
			return matrix_coeff_info[i].v4l2_matrix_coeff;
	}

	i_vpr_e(inst, "%s: invalid hfi matrix coeff %d\n",
		func, vidc_matrix_coeff);

	return v4l2_matrix_coeff;
}

int v4l2_type_to_driver_port(struct msm_vidc_inst *inst, u32 type,
	const char *func)
{
	int port;

	if (type == INPUT_MPLANE) {
		port = INPUT_PORT;
	} else if (type == INPUT_META_PLANE) {
		port = INPUT_META_PORT;
	} else if (type == OUTPUT_MPLANE) {
		port = OUTPUT_PORT;
	} else if (type == OUTPUT_META_PLANE) {
		port = OUTPUT_META_PORT;
	} else {
		i_vpr_e(inst, "%s: port not found for v4l2 type %d\n",
			func, type);
		port = -EINVAL;
	}

	return port;
}

struct msm_vidc_buffers *msm_vidc_get_buffers(
	struct msm_vidc_inst *inst, enum msm_vidc_buffer_type buffer_type,
	const char *func)
{
	switch (buffer_type) {
	case MSM_VIDC_BUF_INPUT:
		return &inst->buffers.input;
	case MSM_VIDC_BUF_INPUT_META:
		return &inst->buffers.input_meta;
	case MSM_VIDC_BUF_OUTPUT:
		return &inst->buffers.output;
	case MSM_VIDC_BUF_OUTPUT_META:
		return &inst->buffers.output_meta;
	case MSM_VIDC_BUF_READ_ONLY:
		return &inst->buffers.read_only;
	case MSM_VIDC_BUF_BIN:
		return &inst->buffers.bin;
	case MSM_VIDC_BUF_ARP:
		return &inst->buffers.arp;
	case MSM_VIDC_BUF_COMV:
		return &inst->buffers.comv;
	case MSM_VIDC_BUF_NON_COMV:
		return &inst->buffers.non_comv;
	case MSM_VIDC_BUF_LINE:
		return &inst->buffers.line;
	case MSM_VIDC_BUF_DPB:
		return &inst->buffers.dpb;
	case MSM_VIDC_BUF_PERSIST:
		return &inst->buffers.persist;
	case MSM_VIDC_BUF_VPSS:
		return &inst->buffers.vpss;
	case MSM_VIDC_BUF_PARTIAL_DATA:
		return &inst->buffers.partial_data;
	case MSM_VIDC_BUF_QUEUE:
		return NULL;
	default:
		i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
			func, buffer_type);
		return NULL;
	}
}

struct msm_vidc_mappings *msm_vidc_get_mappings(
	struct msm_vidc_inst *inst, enum msm_vidc_buffer_type buffer_type,
	const char *func)
{
	switch (buffer_type) {
	case MSM_VIDC_BUF_BIN:
		return &inst->mappings.bin;
	case MSM_VIDC_BUF_ARP:
		return &inst->mappings.arp;
	case MSM_VIDC_BUF_COMV:
		return &inst->mappings.comv;
	case MSM_VIDC_BUF_NON_COMV:
		return &inst->mappings.non_comv;
	case MSM_VIDC_BUF_LINE:
		return &inst->mappings.line;
	case MSM_VIDC_BUF_DPB:
		return &inst->mappings.dpb;
	case MSM_VIDC_BUF_PERSIST:
		return &inst->mappings.persist;
	case MSM_VIDC_BUF_VPSS:
		return &inst->mappings.vpss;
	case MSM_VIDC_BUF_PARTIAL_DATA:
		return &inst->mappings.partial_data;
	default:
		i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
			func, buffer_type);
		return NULL;
	}
}

struct msm_vidc_allocations *msm_vidc_get_allocations(
	struct msm_vidc_inst *inst, enum msm_vidc_buffer_type buffer_type,
	const char *func)
{
	switch (buffer_type) {
	case MSM_VIDC_BUF_BIN:
		return &inst->allocations.bin;
	case MSM_VIDC_BUF_ARP:
		return &inst->allocations.arp;
	case MSM_VIDC_BUF_COMV:
		return &inst->allocations.comv;
	case MSM_VIDC_BUF_NON_COMV:
		return &inst->allocations.non_comv;
	case MSM_VIDC_BUF_LINE:
		return &inst->allocations.line;
	case MSM_VIDC_BUF_DPB:
		return &inst->allocations.dpb;
	case MSM_VIDC_BUF_PERSIST:
		return &inst->allocations.persist;
	case MSM_VIDC_BUF_VPSS:
		return &inst->allocations.vpss;
	case MSM_VIDC_BUF_PARTIAL_DATA:
		return &inst->allocations.partial_data;
	default:
		i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
			func, buffer_type);
		return NULL;
	}
}

bool res_is_greater_than(u32 width, u32 height,
	u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs > NUM_MBS_PER_FRAME(ref_height, ref_width) ||
		width > max_side ||
		height > max_side)
		return true;
	else
		return false;
}

bool res_is_greater_than_or_equal_to(u32 width, u32 height,
	u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs >= NUM_MBS_PER_FRAME(ref_height, ref_width) ||
		width >= max_side ||
		height >= max_side)
		return true;
	else
		return false;
}

bool res_is_less_than(u32 width, u32 height,
	u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs < NUM_MBS_PER_FRAME(ref_height, ref_width) &&
		width < max_side &&
		height < max_side)
		return true;
	else
		return false;
}

bool res_is_less_than_or_equal_to(u32 width, u32 height,
	u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs <= NUM_MBS_PER_FRAME(ref_height, ref_width) &&
		width <= max_side &&
		height <= max_side)
		return true;
	else
		return false;
}

int signal_session_msg_receipt(struct msm_vidc_inst *inst,
	enum signal_session_response cmd)
{
	if (cmd < MAX_SIGNAL)
		complete(&inst->completions[cmd]);
	return 0;
}

int msm_vidc_change_core_state(struct msm_vidc_core *core,
	enum msm_vidc_core_state request_state, const char *func)
{
	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	d_vpr_h("%s: core state changed to %s from %s\n",
		func, core_state_name(request_state),
		core_state_name(core->state));
	core->state = request_state;
	return 0;
}

int msm_vidc_change_core_sub_state(struct msm_vidc_core *core,
		enum msm_vidc_core_sub_state clear_sub_state,
		enum msm_vidc_core_sub_state set_sub_state, const char *func)
{
	int i = 0;
	enum msm_vidc_core_sub_state prev_sub_state;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/* final value will not change */
	if (clear_sub_state == set_sub_state)
		return 0;

	/* sanitize clear & set value */
	if (set_sub_state > CORE_SUBSTATE_MAX ||
		clear_sub_state > CORE_SUBSTATE_MAX) {
		d_vpr_e("%s: invalid sub states. clear %#x or set %#x\n",
			func, clear_sub_state, set_sub_state);
		return -EINVAL;
	}

	prev_sub_state = core->sub_state;
	core->sub_state |= set_sub_state;
	core->sub_state &= ~clear_sub_state;

	/* print substates only when there is a change */
	if (core->sub_state != prev_sub_state) {
		strscpy(core->sub_state_name, "\0", sizeof(core->sub_state_name));
		for (i = 0; BIT(i) < CORE_SUBSTATE_MAX; i++) {
			if (core->sub_state == CORE_SUBSTATE_NONE) {
				strscpy(core->sub_state_name, "CORE_SUBSTATE_NONE",
					sizeof(core->sub_state_name));
				break;
			}
			if (core->sub_state & BIT(i))
				strlcat(core->sub_state_name, core_sub_state_name(BIT(i)),
					sizeof(core->sub_state_name));
		}
		d_vpr_h("%s: core sub state changed to %s\n", func, core->sub_state_name);
	}

	return 0;
}

int msm_vidc_change_state(struct msm_vidc_inst *inst,
		enum msm_vidc_state request_state, const char *func)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (!request_state) {
		i_vpr_e(inst, "%s: invalid request state\n", func);
		return -EINVAL;
	}

	if (is_session_error(inst)) {
		i_vpr_h(inst,
			"%s: inst is in bad state, can not change state to %s\n",
			func, state_name(request_state));
		return 0;
	}

	if (request_state == MSM_VIDC_ERROR)
		i_vpr_e(inst, FMT_STRING_STATE_CHANGE,
		   func, state_name(request_state), state_name(inst->state));
	else
		i_vpr_h(inst, FMT_STRING_STATE_CHANGE,
		   func, state_name(request_state), state_name(inst->state));

	trace_msm_vidc_common_state_change(inst, func, state_name(inst->state),
			state_name(request_state));

	inst->state = request_state;

	return 0;
}

int msm_vidc_change_sub_state(struct msm_vidc_inst *inst,
		enum msm_vidc_sub_state clear_sub_state,
		enum msm_vidc_sub_state set_sub_state, const char *func)
{
	int i = 0;
	enum msm_vidc_sub_state prev_sub_state;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (is_session_error(inst)) {
		i_vpr_h(inst,
			"%s: inst is in bad state, can not change sub state\n", func);
		return 0;
	}

	if (!clear_sub_state && !set_sub_state)
		return 0;

	if ((clear_sub_state & set_sub_state) ||
		(set_sub_state > MSM_VIDC_MAX_SUB_STATE_VALUE) ||
		(clear_sub_state > MSM_VIDC_MAX_SUB_STATE_VALUE)) {
		i_vpr_e(inst, "%s: invalid sub states to clear %#x or set %#x\n",
			func, clear_sub_state, set_sub_state);
		return -EINVAL;
	}

	prev_sub_state = inst->sub_state;
	inst->sub_state |= set_sub_state;
	inst->sub_state &= ~clear_sub_state;

	/* print substates only when there is a change */
	if (inst->sub_state != prev_sub_state) {
		strlcpy(inst->sub_state_name, "\0", sizeof(inst->sub_state_name));
		for (i = 0; i < MSM_VIDC_MAX_SUB_STATES; i++) {
			if (inst->sub_state == MSM_VIDC_SUB_STATE_NONE) {
				strlcpy(inst->sub_state_name, "SUB_STATE_NONE",
					sizeof(inst->sub_state_name));
				break;
			}
			if (inst->sub_state & BIT(i))
				strlcat(inst->sub_state_name, sub_state_name(BIT(i)),
					sizeof(inst->sub_state_name));
		}
		i_vpr_h(inst, "%s: sub state changed to %s\n", func, inst->sub_state_name);
	}

	return 0;
}

bool msm_vidc_allow_s_fmt(struct msm_vidc_inst *inst, u32 type)
{
	bool allow = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	if (is_state(inst, MSM_VIDC_OPEN)) {
		allow = true;
		goto exit;
	}
	if (type == OUTPUT_MPLANE || type == OUTPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_INPUT_STREAMING)) {
			allow = true;
			goto exit;
		}
	}
	if (type == INPUT_MPLANE || type == INPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_OUTPUT_STREAMING)) {
			allow = true;
			goto exit;
		}
	}

exit:
	if (!allow)
		i_vpr_e(inst, "%s: type %d not allowed in state %s\n",
				__func__, type, state_name(inst->state));
	return allow;
}

bool msm_vidc_allow_s_ctrl(struct msm_vidc_inst *inst,
	enum msm_vidc_inst_capability_type cap_id)
{
	bool allow = false;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	if (is_state(inst, MSM_VIDC_OPEN)) {
		allow = true;
		goto exit;
	}

	if (!inst->capabilities->cap[cap_id].cap_id ||
	    !inst->capabilities->cap[cap_id].v4l2_id) {
		allow = false;
		goto exit;
	}

	if (is_decode_session(inst)) {
		if (!inst->bufq[INPUT_PORT].vb2q->streaming) {
			allow = true;
			goto exit;
		}
		if (inst->bufq[INPUT_PORT].vb2q->streaming) {
			if (inst->capabilities->cap[cap_id].flags &
			    CAP_FLAG_DYNAMIC_ALLOWED)
				allow = true;
		}
	} else if (is_encode_session(inst)) {
		if (!inst->bufq[OUTPUT_PORT].vb2q->streaming) {
			allow = true;
			goto exit;
		}
		if (inst->bufq[OUTPUT_PORT].vb2q->streaming) {
			if (inst->capabilities->cap[cap_id].flags &
			    CAP_FLAG_DYNAMIC_ALLOWED)
				allow = true;
		}
	}

exit:
	if (!allow)
		i_vpr_e(inst, "%s: cap_id %#x not allowed in state %s\n",
			__func__, cap_id, state_name(inst->state));
	return allow;
}

bool msm_vidc_allow_metadata_delivery(struct msm_vidc_inst *inst, u32 cap_id,
	u32 port)
{
	return true;
}

bool msm_vidc_allow_metadata_subscription(struct msm_vidc_inst *inst, u32 cap_id,
	u32 port)
{
	bool is_allowed = true;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}

	if (port == INPUT_PORT) {
		switch (cap_id) {
		case META_BUF_TAG:
		case META_BITSTREAM_RESOLUTION:
		case META_CROP_OFFSETS:
		case META_SEI_MASTERING_DISP:
		case META_SEI_CLL:
		case META_HDR10PLUS:
			if (!is_meta_rx_inp_enabled(inst, META_OUTBUF_FENCE)) {
				i_vpr_h(inst,
					"%s: cap: %24s not allowed as output buffer fence is disabled\n",
					__func__, cap_name(cap_id));
				is_allowed = false;
			}
			break;
		default:
			is_allowed = true;
			break;
		}
	} else if (port == OUTPUT_PORT) {
		switch (cap_id) {
		case META_DPB_TAG_LIST:
			if (!is_ubwc_colorformat(inst->capabilities->cap[PIX_FMTS].value)) {
				i_vpr_h(inst,
					"%s: cap: %24s not allowed for split mode\n",
					__func__, cap_name(cap_id));
				is_allowed = false;
			}
			break;
		default:
			is_allowed = true;
			break;
		}
	} else {
		i_vpr_e(inst, "%s: invalid port %d\n", __func__, port);
		is_allowed = false;
	}

	return is_allowed;
}

bool msm_vidc_allow_property(struct msm_vidc_inst *inst, u32 hfi_id)
{
	bool is_allowed = true;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}

	switch (hfi_id) {
	case HFI_PROP_WORST_COMPRESSION_RATIO:
	case HFI_PROP_WORST_COMPLEXITY_FACTOR:
	case HFI_PROP_PICTURE_TYPE:
		is_allowed = true;
		break;
	case HFI_PROP_DPB_LIST:
		if (!is_ubwc_colorformat(inst->capabilities->cap[PIX_FMTS].value)) {
			i_vpr_h(inst,
				"%s: cap: %24s not allowed for split mode\n",
				__func__, cap_name(DPB_LIST));
			is_allowed = false;
		}
		break;
	case HFI_PROP_FENCE:
		if (!is_meta_rx_inp_enabled(inst, META_OUTBUF_FENCE)) {
			i_vpr_h(inst,
				"%s: cap: %24s not enabled, hence not allowed to subscribe\n",
				__func__, cap_name(META_OUTBUF_FENCE));
			is_allowed = false;
		}
		break;
	default:
		is_allowed = true;
		break;
	}

	return is_allowed;
}

int msm_vidc_update_property_cap(struct msm_vidc_inst *inst, u32 hfi_id,
	bool allow)
{
	int rc = 0;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	switch (hfi_id) {
	case HFI_PROP_WORST_COMPRESSION_RATIO:
	case HFI_PROP_WORST_COMPLEXITY_FACTOR:
	case HFI_PROP_PICTURE_TYPE:
		break;
	case HFI_PROP_DPB_LIST:
		if (!allow)
			memset(inst->dpb_list_payload, 0, MAX_DPB_LIST_ARRAY_SIZE);
		msm_vidc_update_cap_value(inst, DPB_LIST, allow, __func__);
		break;
	default:
		break;
	}

	return rc;
}

bool msm_vidc_allow_reqbufs(struct msm_vidc_inst *inst, u32 type)
{
	bool allow = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	if (is_state(inst, MSM_VIDC_OPEN)) {
		allow = true;
		goto exit;
	}
	if (type == OUTPUT_MPLANE || type == OUTPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_INPUT_STREAMING)) {
			allow = true;
			goto exit;
		}
	}
	if (type == INPUT_MPLANE || type == INPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_OUTPUT_STREAMING)) {
			allow = true;
			goto exit;
		}
	}

exit:
	if (!allow)
		i_vpr_e(inst, "%s: type %d not allowed in state %s\n",
				__func__, type, state_name(inst->state));
	return allow;
}

enum msm_vidc_allow msm_vidc_allow_stop(struct msm_vidc_inst *inst)
{
	enum msm_vidc_allow allow = MSM_VIDC_DISALLOW;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return allow;
	}

	/* allow stop (drain) if input port is streaming */
	if (is_state(inst, MSM_VIDC_INPUT_STREAMING) ||
		is_state(inst, MSM_VIDC_STREAMING)) {
		/* do not allow back to back drain */
		if (!(is_sub_state(inst, MSM_VIDC_DRAIN)))
			allow = MSM_VIDC_ALLOW;
	} else if (is_state(inst, MSM_VIDC_OPEN)) {
		allow = MSM_VIDC_IGNORE;
		i_vpr_e(inst, "%s: ignored in state %s, sub state %s\n",
			__func__, state_name(inst->state), inst->sub_state_name);
	} else {
		i_vpr_e(inst, "%s: not allowed in state %s, sub state %s\n",
			__func__, state_name(inst->state), inst->sub_state_name);
	}

	return allow;
}

bool msm_vidc_allow_start(struct msm_vidc_inst *inst)
{
	bool allow = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return allow;
	}

	/* client would call start (resume) to complete DRC/drain sequence */
	if (inst->state == MSM_VIDC_INPUT_STREAMING ||
		inst->state == MSM_VIDC_OUTPUT_STREAMING ||
		inst->state == MSM_VIDC_STREAMING) {
		if ((is_sub_state(inst, MSM_VIDC_DRC) &&
			is_sub_state(inst, MSM_VIDC_DRC_LAST_BUFFER)) ||
			(is_sub_state(inst, MSM_VIDC_DRAIN) &&
			is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER)))
			allow = true;
	}
	if (!allow)
		i_vpr_e(inst, "%s: not allowed in state %s, sub state %s\n",
			__func__, state_name(inst->state), inst->sub_state_name);
	return allow;
}

bool msm_vidc_allow_streamon(struct msm_vidc_inst *inst, u32 type)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	if (type == INPUT_MPLANE || type == INPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_OPEN) ||
			is_state(inst, MSM_VIDC_OUTPUT_STREAMING))
			return true;
	} else if (type == OUTPUT_MPLANE || type == OUTPUT_META_PLANE) {
		if (is_state(inst, MSM_VIDC_OPEN) ||
			is_state(inst, MSM_VIDC_INPUT_STREAMING))
			return true;
	}

	i_vpr_e(inst, "%s: type %d not allowed in state %s\n",
			__func__, type, state_name(inst->state));
	return false;
}

enum msm_vidc_allow msm_vidc_allow_streamoff(struct msm_vidc_inst *inst, u32 type)
{
	enum msm_vidc_allow allow = MSM_VIDC_ALLOW;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return MSM_VIDC_DISALLOW;
	}
	if (type == INPUT_MPLANE) {
		if (!inst->bufq[INPUT_PORT].vb2q->streaming)
			allow = MSM_VIDC_IGNORE;
	} else if (type == INPUT_META_PLANE) {
		if (inst->bufq[INPUT_PORT].vb2q->streaming)
			allow = MSM_VIDC_DISALLOW;
		else if (!inst->bufq[INPUT_META_PORT].vb2q->streaming)
			allow = MSM_VIDC_IGNORE;
	} else if (type == OUTPUT_MPLANE) {
		if (!inst->bufq[OUTPUT_PORT].vb2q->streaming)
			allow = MSM_VIDC_IGNORE;
	} else if (type == OUTPUT_META_PLANE) {
		if (inst->bufq[OUTPUT_PORT].vb2q->streaming)
			allow = MSM_VIDC_DISALLOW;
		else if (!inst->bufq[OUTPUT_META_PORT].vb2q->streaming)
			allow = MSM_VIDC_IGNORE;
	}
	if (allow != MSM_VIDC_ALLOW)
		i_vpr_e(inst, "%s: type %d is %s in state %s\n",
				__func__, type, allow_name(allow),
				state_name(inst->state));

	return allow;
}

enum msm_vidc_allow msm_vidc_allow_qbuf(struct msm_vidc_inst *inst, u32 type)
{
	int port = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return MSM_VIDC_DISALLOW;
	}

	port = v4l2_type_to_driver_port(inst, type, __func__);
	if (port < 0)
		return MSM_VIDC_DISALLOW;

	/* defer queuing if streamon not completed */
	if (!inst->bufq[port].vb2q->streaming)
		return MSM_VIDC_DEFER;

	if (type == INPUT_META_PLANE || type == OUTPUT_META_PLANE)
		return MSM_VIDC_DEFER;

	if (type == INPUT_MPLANE) {
		if (is_state(inst, MSM_VIDC_OPEN) ||
			is_state(inst, MSM_VIDC_OUTPUT_STREAMING))
			return MSM_VIDC_DEFER;
		else
			return MSM_VIDC_ALLOW;
	} else if (type == OUTPUT_MPLANE) {
		if (is_state(inst, MSM_VIDC_OPEN) ||
			is_state(inst, MSM_VIDC_INPUT_STREAMING))
			return MSM_VIDC_DEFER;
		else
			return MSM_VIDC_ALLOW;
	} else {
		i_vpr_e(inst, "%s: unknown buffer type %d\n", __func__, type);
		return MSM_VIDC_DISALLOW;
	}

	return MSM_VIDC_DISALLOW;
}

enum msm_vidc_allow msm_vidc_allow_input_psc(struct msm_vidc_inst *inst)
{
	enum msm_vidc_allow allow = MSM_VIDC_ALLOW;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return MSM_VIDC_DISALLOW;
	}

	/*
	 * if drc sequence is not completed by client, fw is not
	 * expected to raise another ipsc
	 */
	if (is_sub_state(inst, MSM_VIDC_DRC)) {
		i_vpr_e(inst, "%s: not allowed in sub state %s\n",
			__func__, inst->sub_state_name);
		return MSM_VIDC_DISALLOW;
	}

	return allow;
}

bool msm_vidc_allow_drain_last_flag(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}

	/*
	 * drain last flag is expected only when DRAIN, INPUT_PAUSE
	 * is set and DRAIN_LAST_BUFFER is not set
	 */
	if (is_sub_state(inst, MSM_VIDC_DRAIN) &&
		is_sub_state(inst, MSM_VIDC_INPUT_PAUSE) &&
		!is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER))
		return true;

	i_vpr_e(inst, "%s: not allowed in sub state %s\n",
			__func__, inst->sub_state_name);
	return false;
}

bool msm_vidc_allow_psc_last_flag(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}

	/*
	 * drc last flag is expected only when DRC, INPUT_PAUSE
	 * is set and DRC_LAST_BUFFER is not set
	 */
	if (is_sub_state(inst, MSM_VIDC_DRC) &&
		is_sub_state(inst, MSM_VIDC_INPUT_PAUSE) &&
		!is_sub_state(inst, MSM_VIDC_DRC_LAST_BUFFER))
		return true;

	i_vpr_e(inst, "%s: not allowed in sub state %s\n",
			__func__, inst->sub_state_name);

	return false;
}

bool msm_vidc_allow_pm_suspend(struct msm_vidc_core *core)
{
	if (!core) {
		d_vpr_e("%s: invalid param\n", __func__);
		return false;
	}

	/* core must be in valid state to do pm_suspend */
	if (!core_in_valid_state(core)) {
		d_vpr_e("%s: invalid core state %s\n",
			__func__, core_state_name(core->state));
		return false;
	}

	/* check if power is enabled */
	if (!is_core_sub_state(core, CORE_SUBSTATE_POWER_ENABLE)) {
		d_vpr_e("%s: Power already disabled\n", __func__);
		return false;
	}

	return true;
}

bool is_hevc_10bit_decode_session(struct msm_vidc_inst *inst)
{
	bool is10bit = false;
	enum msm_vidc_colorformat_type colorformat;

	/* in case of encoder session return false */
	if (!is_decode_session(inst))
		return false;

	colorformat = v4l2_colorformat_to_driver(inst,
		inst->fmts[OUTPUT_PORT].fmt.pix_mp.pixelformat, __func__);

	if (colorformat == MSM_VIDC_FMT_TP10C || colorformat == MSM_VIDC_FMT_P010)
		is10bit = true;

	return inst->domain == MSM_VIDC_DECODER &&
				inst->codec == MSM_VIDC_HEVC &&
				is10bit;
}

int msm_vidc_state_change_streamon(struct msm_vidc_inst *inst,
		enum msm_vidc_port_type port)
{
	int rc = 0;
	enum msm_vidc_state new_state = MSM_VIDC_ERROR;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (port == INPUT_META_PORT || port == OUTPUT_META_PORT)
		return 0;

	if (port == INPUT_PORT) {
		if (is_state(inst, MSM_VIDC_OPEN))
			new_state = MSM_VIDC_INPUT_STREAMING;
		else if (is_state(inst, MSM_VIDC_OUTPUT_STREAMING))
			new_state = MSM_VIDC_STREAMING;
	} else if (port == OUTPUT_PORT) {
		if (is_state(inst, MSM_VIDC_OPEN))
			new_state = MSM_VIDC_OUTPUT_STREAMING;
		else if (is_state(inst, MSM_VIDC_INPUT_STREAMING))
			new_state = MSM_VIDC_STREAMING;
	}

	rc = msm_vidc_change_state(inst, new_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_state_change_streamoff(struct msm_vidc_inst *inst,
		enum msm_vidc_port_type port)
{
	int rc = 0;
	enum msm_vidc_state new_state = MSM_VIDC_ERROR;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (port == INPUT_META_PORT || port == OUTPUT_META_PORT)
		return 0;

	if (port == INPUT_PORT) {
		if (is_state(inst, MSM_VIDC_INPUT_STREAMING)) {
			new_state = MSM_VIDC_OPEN;
		} else if (is_state(inst, MSM_VIDC_STREAMING)) {
			new_state = MSM_VIDC_OUTPUT_STREAMING;
		}
	} else if (port == OUTPUT_PORT) {
		if (is_state(inst, MSM_VIDC_OUTPUT_STREAMING)) {
			new_state = MSM_VIDC_OPEN;
		} else if (is_state(inst, MSM_VIDC_STREAMING)) {
			new_state = MSM_VIDC_INPUT_STREAMING;
		}
	}
	rc = msm_vidc_change_state(inst, new_state, __func__);
	if (rc)
		goto exit;

exit:
	return rc;
}

int msm_vidc_process_drain(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = venus_hfi_session_drain(inst, INPUT_PORT);
	if (rc)
		return rc;
	rc = msm_vidc_change_sub_state(inst, 0, MSM_VIDC_DRAIN, __func__);
	if (rc)
		return rc;

	msm_vidc_scale_power(inst, true);

	return rc;
}

int msm_vidc_process_resume(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state clear_sub_state = MSM_VIDC_SUB_STATE_NONE;
	bool drain_pending = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	msm_vidc_scale_power(inst, true);

	/* first check DRC pending else check drain pending */
	if (is_sub_state(inst, MSM_VIDC_DRC) &&
		is_sub_state(inst, MSM_VIDC_DRC_LAST_BUFFER)) {
		clear_sub_state = MSM_VIDC_DRC | MSM_VIDC_DRC_LAST_BUFFER;
		/*
		 * if drain sequence is not completed then do not resume here.
		 * client will eventually complete drain sequence in which ports
		 * will be resumed.
		 */
		drain_pending = is_sub_state(inst, MSM_VIDC_DRAIN) &&
			is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER);
		if (!drain_pending) {
			if (is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
				rc = venus_hfi_session_resume(inst, INPUT_PORT,
						HFI_CMD_SETTINGS_CHANGE);
				if (rc)
					return rc;
				clear_sub_state |= MSM_VIDC_INPUT_PAUSE;
			}
			if (is_sub_state(inst, MSM_VIDC_OUTPUT_PAUSE)) {
				rc = venus_hfi_session_resume(inst, OUTPUT_PORT,
						HFI_CMD_SETTINGS_CHANGE);
				if (rc)
					return rc;
				clear_sub_state |= MSM_VIDC_OUTPUT_PAUSE;
			}
		}
	} else if (is_sub_state(inst, MSM_VIDC_DRAIN) &&
			   is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER)) {
		clear_sub_state = MSM_VIDC_DRAIN | MSM_VIDC_DRAIN_LAST_BUFFER;
		if (is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
			rc = venus_hfi_session_resume(inst, INPUT_PORT, HFI_CMD_DRAIN);
			if (rc)
				return rc;
			clear_sub_state |= MSM_VIDC_INPUT_PAUSE;
		}
		if (is_sub_state(inst, MSM_VIDC_OUTPUT_PAUSE)) {
			rc = venus_hfi_session_resume(inst, OUTPUT_PORT, HFI_CMD_DRAIN);
			if (rc)
				return rc;
			clear_sub_state |= MSM_VIDC_OUTPUT_PAUSE;
		}
	}

	rc = msm_vidc_change_sub_state(inst, clear_sub_state, 0, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_process_streamon_input(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state clear_sub_state = MSM_VIDC_SUB_STATE_NONE;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	msm_vidc_scale_power(inst, true);

	rc = venus_hfi_start(inst, INPUT_PORT);
	if (rc)
		return rc;

	/* clear input pause substate immediately */
	if (is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
		rc = msm_vidc_change_sub_state(inst, MSM_VIDC_INPUT_PAUSE, 0, __func__);
		if (rc)
			return rc;
	}

	/*
	 * if DRC sequence is not completed by the client then PAUSE
	 * firmware input port to avoid firmware raising IPSC again.
	 * When client completes DRC or DRAIN sequences, firmware
	 * input port will be resumed.
	 */
	if (is_sub_state(inst, MSM_VIDC_DRC) ||
		is_sub_state(inst, MSM_VIDC_DRAIN)) {
		if (!is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
			rc = venus_hfi_session_pause(inst, INPUT_PORT);
			if (rc)
				return rc;
			set_sub_state = MSM_VIDC_INPUT_PAUSE;
		}
	}

	rc = msm_vidc_state_change_streamon(inst, INPUT_PORT);
	if (rc)
		return rc;

	rc = msm_vidc_change_sub_state(inst, clear_sub_state, set_sub_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_process_streamon_output(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state clear_sub_state = MSM_VIDC_SUB_STATE_NONE;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;
	bool drain_pending = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	msm_vidc_scale_power(inst, true);

	/*
	 * client completed drc sequence, reset DRC and
	 * MSM_VIDC_DRC_LAST_BUFFER substates
	 */
	if (is_sub_state(inst, MSM_VIDC_DRC) &&
		is_sub_state(inst, MSM_VIDC_DRC_LAST_BUFFER)) {
		clear_sub_state = MSM_VIDC_DRC | MSM_VIDC_DRC_LAST_BUFFER;
	}
	/*
	 * Client is completing port reconfiguration, hence reallocate
	 * input internal buffers before input port is resumed.
	 * Drc sub-state cannot be checked because DRC sub-state will
	 * not be set during initial port reconfiguration.
	 */
	if (is_decode_session(inst) &&
		is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
		rc = msm_vidc_alloc_and_queue_input_internal_buffers(inst);
		if (rc)
			return rc;
		rc = msm_vidc_set_stage(inst, STAGE);
		if (rc)
			return rc;
		rc = msm_vidc_set_pipe(inst, PIPE);
		if (rc)
			return rc;
	}

	/*
	 * fw input port is paused due to ipsc. now that client
	 * completed drc sequence, resume fw input port provided
	 * drain is not pending and input port is streaming.
	 */
	drain_pending = is_sub_state(inst, MSM_VIDC_DRAIN) &&
		is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER);
	if (!drain_pending && is_state(inst, MSM_VIDC_INPUT_STREAMING)) {
		if (is_sub_state(inst, MSM_VIDC_INPUT_PAUSE)) {
			rc = venus_hfi_session_resume(inst, INPUT_PORT,
					HFI_CMD_SETTINGS_CHANGE);
			if (rc)
				return rc;
			clear_sub_state |= MSM_VIDC_INPUT_PAUSE;
		}
	}

	rc = venus_hfi_start(inst, OUTPUT_PORT);
	if (rc)
		return rc;

	/* clear output pause substate immediately */
	if (is_sub_state(inst, MSM_VIDC_OUTPUT_PAUSE)) {
		rc = msm_vidc_change_sub_state(inst, MSM_VIDC_OUTPUT_PAUSE, 0, __func__);
		if (rc)
			return rc;
	}

	rc = msm_vidc_state_change_streamon(inst, OUTPUT_PORT);
	if (rc)
		return rc;

	rc = msm_vidc_change_sub_state(inst, clear_sub_state, set_sub_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_process_stop_done(struct msm_vidc_inst *inst,
		enum signal_session_response signal_type)
{
	int rc = 0;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (signal_type == SIGNAL_CMD_STOP_INPUT) {
		set_sub_state = MSM_VIDC_INPUT_PAUSE;
		/*
		 * FW is expected to return DRC LAST flag before input
		 * stop done if DRC sequence is pending
		 */
		if (is_sub_state(inst, MSM_VIDC_DRC) &&
			!is_sub_state(inst, MSM_VIDC_DRC_LAST_BUFFER)) {
			i_vpr_e(inst, "%s: drc last flag pkt not received\n", __func__);
			msm_vidc_change_state(inst, MSM_VIDC_ERROR, __func__);
		}
		/*
		 * for a decode session, FW is expected to return
		 * DRAIN LAST flag before input stop done if
		 * DRAIN sequence is pending
		 */
		if (is_decode_session(inst) &&
			is_sub_state(inst, MSM_VIDC_DRAIN) &&
			!is_sub_state(inst, MSM_VIDC_DRAIN_LAST_BUFFER)) {
			i_vpr_e(inst, "%s: drain last flag pkt not received\n", __func__);
			msm_vidc_change_state(inst, MSM_VIDC_ERROR, __func__);
		}
	} else if (signal_type == SIGNAL_CMD_STOP_OUTPUT) {
		set_sub_state = MSM_VIDC_OUTPUT_PAUSE;
	}

	rc = msm_vidc_change_sub_state(inst, 0, set_sub_state, __func__);
	if (rc)
		return rc;

	signal_session_msg_receipt(inst, signal_type);
	return rc;
}

int msm_vidc_process_drain_done(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (is_sub_state(inst, MSM_VIDC_DRAIN)) {
		rc = msm_vidc_change_sub_state(inst, 0, MSM_VIDC_INPUT_PAUSE, __func__);
		if (rc)
			return rc;
	} else {
		i_vpr_e(inst, "%s: unexpected drain done\n", __func__);
	}

	return rc;
}

int msm_vidc_process_drain_last_flag(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct v4l2_event event = {0};

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = msm_vidc_state_change_drain_last_flag(inst);
	if (rc)
		return rc;

	if (is_decode_session(inst) &&
		!inst->capabilities->cap[LAST_FLAG_EVENT_ENABLE].value) {
		i_vpr_h(inst, "%s: last flag event not enabled\n", __func__);
		return 0;
	}

	event.type = V4L2_EVENT_EOS;
	v4l2_event_queue_fh(&inst->event_handler, &event);

	return rc;
}

int msm_vidc_process_psc_last_flag(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct v4l2_event event = {0};

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = msm_vidc_state_change_psc_last_flag(inst);
	if (rc)
		return rc;

	if (is_decode_session(inst) &&
		!inst->capabilities->cap[LAST_FLAG_EVENT_ENABLE].value) {
		i_vpr_h(inst, "%s: last flag event not enabled\n", __func__);
		return 0;
	}

	event.type = V4L2_EVENT_EOS;
	v4l2_event_queue_fh(&inst->event_handler, &event);

	return rc;
}

int msm_vidc_state_change_input_psc(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/*
	 * if output port is not streaming, then do not set DRC substate
	 * because DRC_LAST_FLAG is not going to be received. Update
	 * INPUT_PAUSE substate only
	 */
	if (is_state(inst, MSM_VIDC_INPUT_STREAMING) ||
		is_state(inst, MSM_VIDC_OPEN))
		set_sub_state = MSM_VIDC_INPUT_PAUSE;
	else
		set_sub_state = MSM_VIDC_DRC | MSM_VIDC_INPUT_PAUSE;

	rc = msm_vidc_change_sub_state(inst, 0, set_sub_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_state_change_drain_last_flag(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	set_sub_state = MSM_VIDC_DRAIN_LAST_BUFFER | MSM_VIDC_OUTPUT_PAUSE;
	rc = msm_vidc_change_sub_state(inst, 0, set_sub_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_state_change_psc_last_flag(struct msm_vidc_inst *inst)
{
	int rc = 0;
	enum msm_vidc_sub_state set_sub_state = MSM_VIDC_SUB_STATE_NONE;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	set_sub_state = MSM_VIDC_DRC_LAST_BUFFER | MSM_VIDC_OUTPUT_PAUSE;
	rc = msm_vidc_change_sub_state(inst, 0, set_sub_state, __func__);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_get_fence_fd(struct msm_vidc_inst *inst, int *fence_fd)
{
	int rc = 0;
	struct msm_vidc_fence *fence, *dummy_fence;
	bool found = false;

	*fence_fd = INVALID_FD;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	list_for_each_entry_safe(fence, dummy_fence, &inst->fence_list, list) {
		if (fence->dma_fence.seqno ==
			(u64)inst->capabilities->cap[FENCE_ID].value) {
			found = true;
			break;
		}
	}

	if (!found) {
		i_vpr_h(inst, "%s: could not find matching fence for fence id: %d\n",
			__func__, inst->capabilities->cap[FENCE_ID].value);
		goto exit;
	}

	if (fence->fd == INVALID_FD) {
		rc = msm_vidc_create_fence_fd(inst, fence);
		if (rc)
			goto exit;
	}

	*fence_fd = fence->fd;

exit:
	return rc;
}

int msm_vidc_get_control(struct msm_vidc_inst *inst, struct v4l2_ctrl *ctrl)
{
	int rc = 0;
	enum msm_vidc_inst_capability_type cap_id;

	if (!inst || !ctrl) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	cap_id = msm_vidc_get_cap_id(inst, ctrl->id);
	if (!is_valid_cap_id(cap_id)) {
		i_vpr_e(inst, "%s: could not find cap_id for ctrl %s\n",
		__func__, ctrl->name);
		return -EINVAL;
	}

	switch (cap_id) {
	case MIN_BUFFERS_OUTPUT:
		ctrl->val = inst->buffers.output.min_count +
			inst->buffers.output.extra_count;
		i_vpr_h(inst, "g_min: output buffers %d\n", ctrl->val);
		break;
	case MIN_BUFFERS_INPUT:
		ctrl->val = inst->buffers.input.min_count +
			inst->buffers.input.extra_count;
		i_vpr_h(inst, "g_min: input buffers %d\n", ctrl->val);
		break;
	case FILM_GRAIN:
		ctrl->val = inst->capabilities->cap[FILM_GRAIN].value;
		i_vpr_h(inst, "%s: film grain present: %d\n",
			 __func__, ctrl->val);
		break;
	case FENCE_FD:
		rc = msm_vidc_get_fence_fd(inst, &ctrl->val);
		if (!rc)
			i_vpr_l(inst, "%s: fence fd: %d\n",
				__func__, ctrl->val);
		break;
	default:
		i_vpr_e(inst, "invalid ctrl %s id %d\n",
			ctrl->name, ctrl->id);
		return -EINVAL;
	}

	return rc;
}

int msm_vidc_get_mbs_per_frame(struct msm_vidc_inst *inst)
{
	int height = 0, width = 0;
	struct v4l2_format *inp_f;

	if (is_decode_session(inst)) {
		inp_f = &inst->fmts[INPUT_PORT];
		width = max(inp_f->fmt.pix_mp.width, inst->crop.width);
		height = max(inp_f->fmt.pix_mp.height, inst->crop.height);
	} else if (is_encode_session(inst)) {
		width = inst->crop.width;
		height = inst->crop.height;
	}

	return NUM_MBS_PER_FRAME(height, width);
}

int msm_vidc_get_fps(struct msm_vidc_inst *inst)
{
	int fps;
	u32 frame_rate, operating_rate;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	frame_rate = msm_vidc_get_frame_rate(inst);
	operating_rate = msm_vidc_get_operating_rate(inst);

	if (operating_rate > frame_rate)
		fps = operating_rate ? operating_rate : 1;
	else
		fps = frame_rate;

	return fps;
}

int msm_vidc_num_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type type, enum msm_vidc_buffer_attributes attr)
{
	int count = 0;
	struct msm_vidc_buffer *vbuf;
	struct msm_vidc_buffers *buffers;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return count;
	}
	if (type == MSM_VIDC_BUF_OUTPUT) {
		buffers = &inst->buffers.output;
	} else if (type == MSM_VIDC_BUF_INPUT) {
		buffers = &inst->buffers.input;
	} else {
		i_vpr_e(inst, "%s: invalid buffer type %#x\n",
				__func__, type);
		return count;
	}

	list_for_each_entry(vbuf, &buffers->list, list) {
		if (vbuf->type != type)
			continue;
		if (!(vbuf->attr & attr))
			continue;
		count++;
	}

	return count;
}

int vb2_buffer_to_driver(struct vb2_buffer *vb2,
	struct msm_vidc_buffer *buf)
{
	int rc = 0;
	struct vb2_v4l2_buffer *vbuf;

	if (!vb2 || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	vbuf = to_vb2_v4l2_buffer(vb2);

	buf->fd = vb2->planes[0].m.fd;
	buf->data_offset = vb2->planes[0].data_offset;
	buf->data_size = vb2->planes[0].bytesused - vb2->planes[0].data_offset;
	buf->buffer_size = vb2->planes[0].length;
	buf->timestamp = vb2->timestamp;
	buf->flags = vbuf->flags;
	buf->attr = 0;
	buf->fence_id = 0;

	return rc;
}

int msm_vidc_process_readonly_buffers(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf)
{
	int rc = 0;
	struct msm_vidc_buffer *ro_buf, *dummy;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (!is_decode_session(inst) || !is_output_buffer(buf->type))
		return 0;

	/*
	 * check if read_only buffer is present in read_only list
	 * if present: add ro flag to buf provided buffer is not
	 * pending release
	 */
	list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
		if (ro_buf->device_addr != buf->device_addr)
			continue;
		if (ro_buf->attr & MSM_VIDC_ATTR_READ_ONLY &&
			!(ro_buf->attr & MSM_VIDC_ATTR_PENDING_RELEASE)) {
			/* add READ_ONLY to the buffer going to the firmware */
			buf->attr |= MSM_VIDC_ATTR_READ_ONLY;
			/*
			 * remove READ_ONLY on the read_only list buffer so that
			 * it will get removed from the read_only list below
			 */
			ro_buf->attr &= ~MSM_VIDC_ATTR_READ_ONLY;
			break;
		}
	}

	/* remove ro buffers if not required anymore */
	list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
		/* if read only buffer do not remove */
		if (ro_buf->attr & MSM_VIDC_ATTR_READ_ONLY)
			continue;

		print_vidc_buffer(VIDC_LOW, "low ", "ro buf removed", inst, ro_buf);
		/* unmap the buffer if driver holds mapping */
		if (ro_buf->sg_table && ro_buf->attach) {
			call_mem_op(core, dma_buf_unmap_attachment, core,
				ro_buf->attach, ro_buf->sg_table);
			call_mem_op(core, dma_buf_detach, core,
				ro_buf->dmabuf, ro_buf->attach);
			ro_buf->dmabuf = NULL;
			ro_buf->attach = NULL;
		}
		if (ro_buf->dbuf_get) {
			call_mem_op(core, dma_buf_put, inst, ro_buf->dmabuf);
			ro_buf->dmabuf = NULL;
			ro_buf->dbuf_get = 0;
		}

		list_del_init(&ro_buf->list);
		msm_vidc_pool_free(inst, ro_buf);
	}

	return rc;
}

int msm_vidc_set_auto_framerate(struct msm_vidc_inst *inst, u64 timestamp)
{
	struct msm_vidc_core *core;
	struct msm_vidc_timestamp *ts;
	struct msm_vidc_timestamp *prev = NULL;
	u32 counter = 0, prev_fr = 0, curr_fr = 0;
	u64 time_us = 0;
	int rc = 0;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	if (!core->capabilities[ENC_AUTO_FRAMERATE].value ||
			is_image_session(inst) || msm_vidc_is_super_buffer(inst) ||
			!inst->capabilities->cap[TIME_DELTA_BASED_RC].value)
		goto exit;

	rc = msm_vidc_update_timestamp_rate(inst, timestamp);
	if (rc)
		goto exit;

	list_for_each_entry(ts, &inst->timestamps.list, sort.list) {
		if (prev) {
			time_us = ts->sort.val - prev->sort.val;
			prev_fr = curr_fr;
			curr_fr = time_us ? DIV64_U64_ROUND_CLOSEST(USEC_PER_SEC, time_us) << 16 :
					inst->auto_framerate;
			if (curr_fr > inst->capabilities->cap[FRAME_RATE].max)
				curr_fr = inst->capabilities->cap[FRAME_RATE].max;
		}
		prev = ts;
		counter++;
	}

	if (counter < ENC_FPS_WINDOW)
		goto exit;

	/* if framerate changed and stable for 2 frames, set to firmware */
	if (curr_fr == prev_fr && curr_fr != inst->auto_framerate) {
		i_vpr_l(inst, "%s: updated fps:  %u -> %u\n", __func__,
				inst->auto_framerate >> 16, curr_fr >> 16);
		rc = venus_hfi_session_property(inst,
				HFI_PROP_FRAME_RATE,
				HFI_HOST_FLAGS_NONE,
				HFI_PORT_BITSTREAM,
				HFI_PAYLOAD_Q16,
				&curr_fr,
				sizeof(u32));
		if (rc) {
			i_vpr_e(inst, "%s: set auto frame rate failed\n",
				__func__);
			goto exit;
		}
		inst->auto_framerate = curr_fr;
	}
exit:
	return rc;
}

int msm_vidc_update_input_rate(struct msm_vidc_inst *inst, u64 time_us)
{
	struct msm_vidc_input_timer *input_timer;
	struct msm_vidc_input_timer *prev_timer = NULL;
	struct msm_vidc_core *core;
	u64 counter = 0;
	u64 input_timer_sum_us = 0;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	input_timer = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_BUF_TIMER);
	if (!input_timer)
		return -ENOMEM;

	input_timer->time_us = time_us;
	INIT_LIST_HEAD(&input_timer->list);
	list_add_tail(&input_timer->list, &inst->input_timer_list);
	list_for_each_entry(input_timer, &inst->input_timer_list, list) {
		if (prev_timer) {
			input_timer_sum_us += input_timer->time_us - prev_timer->time_us;
			counter++;
		}
		prev_timer = input_timer;
	}

	if (input_timer_sum_us && counter >= INPUT_TIMER_LIST_SIZE)
		inst->capabilities->cap[INPUT_RATE].value =
			(s32)(DIV64_U64_ROUND_CLOSEST(counter * 1000000,
				input_timer_sum_us) << 16);

	/* delete the first entry once counter >= INPUT_TIMER_LIST_SIZE */
	if (counter >= INPUT_TIMER_LIST_SIZE) {
		input_timer = list_first_entry(&inst->input_timer_list,
				struct msm_vidc_input_timer, list);
		list_del_init(&input_timer->list);
		msm_vidc_pool_free(inst, input_timer);
	}

	return 0;
}

int msm_vidc_flush_input_timer(struct msm_vidc_inst *inst)
{
	struct msm_vidc_input_timer *input_timer, *dummy_timer;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	i_vpr_l(inst, "%s: flush input_timer list\n", __func__);
	list_for_each_entry_safe(input_timer, dummy_timer, &inst->input_timer_list, list) {
		list_del_init(&input_timer->list);
		msm_vidc_pool_free(inst, input_timer);
	}
	return 0;
}

int msm_vidc_get_input_rate(struct msm_vidc_inst *inst)
{
	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return 0;
	}

	return inst->capabilities->cap[INPUT_RATE].value >> 16;
}

int msm_vidc_get_timestamp_rate(struct msm_vidc_inst *inst)
{
	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return 0;
	}

	return inst->capabilities->cap[TIMESTAMP_RATE].value >> 16;
}

int msm_vidc_get_frame_rate(struct msm_vidc_inst *inst)
{
	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return 0;
	}

	return inst->capabilities->cap[FRAME_RATE].value >> 16;
}

int msm_vidc_get_operating_rate(struct msm_vidc_inst *inst)
{
	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return 0;
	}

	return inst->capabilities->cap[OPERATING_RATE].value >> 16;
}

static int msm_vidc_insert_sort(struct list_head *head,
	struct msm_vidc_sort *entry)
{
	struct msm_vidc_sort *first, *node;
	struct msm_vidc_sort *prev = NULL;
	bool is_inserted = false;

	if (!head || !entry) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (list_empty(head)) {
		list_add(&entry->list, head);
		return 0;
	}

	first = list_first_entry(head, struct msm_vidc_sort, list);
	if (entry->val < first->val) {
		list_add(&entry->list, head);
		return 0;
	}

	list_for_each_entry(node, head, list) {
		if (prev &&
			entry->val >= prev->val && entry->val <= node->val) {
			list_add(&entry->list, &prev->list);
			is_inserted = true;
			break;
		}
		prev = node;
	}

	if (!is_inserted && prev)
		list_add(&entry->list, &prev->list);

	return 0;
}

static struct msm_vidc_timestamp *msm_vidc_get_least_rank_ts(struct msm_vidc_inst *inst)
{
	struct msm_vidc_timestamp *ts, *final = NULL;
	u64 least_rank = INT_MAX;

	if (!inst) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return NULL;
	}

	list_for_each_entry(ts, &inst->timestamps.list, sort.list) {
		if (ts->rank < least_rank) {
			least_rank = ts->rank;
			final = ts;
		}
	}

	return final;
}

int msm_vidc_flush_ts(struct msm_vidc_inst *inst)
{
	struct msm_vidc_timestamp *temp, *ts = NULL;
	struct msm_vidc_core *core;

	if (!inst || !inst->core ) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	list_for_each_entry_safe(ts, temp, &inst->timestamps.list, sort.list) {
		i_vpr_l(inst, "%s: flushing ts: val %llu, rank %llu\n",
			__func__, ts->sort.val, ts->rank);
		list_del(&ts->sort.list);
		msm_vidc_pool_free(inst, ts);
	}
	inst->timestamps.count = 0;
	inst->timestamps.rank = 0;

	return 0;
}

int msm_vidc_update_timestamp_rate(struct msm_vidc_inst *inst, u64 timestamp)
{
	struct msm_vidc_timestamp *ts, *prev = NULL;
	struct msm_vidc_core *core;
	int rc = 0;
	u32 window_size = 0;
	u32 timestamp_rate = 0;
	u64 ts_ms = 0;
	u32 counter = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	ts = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_TIMESTAMP);
	if (!ts) {
		i_vpr_e(inst, "%s: ts alloc failed\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ts->sort.list);
	ts->sort.val = timestamp;
	ts->rank = inst->timestamps.rank++;
	rc = msm_vidc_insert_sort(&inst->timestamps.list, &ts->sort);
	if (rc)
		return rc;
	inst->timestamps.count++;

	if (is_encode_session(inst))
		window_size = ENC_FPS_WINDOW;
	else
		window_size = DEC_FPS_WINDOW;

	/* keep sliding window */
	if (inst->timestamps.count > window_size) {
		ts = msm_vidc_get_least_rank_ts(inst);
		if (!ts) {
			i_vpr_e(inst, "%s: least rank ts is NULL\n", __func__);
			return -EINVAL;
		}
		inst->timestamps.count--;
		list_del(&ts->sort.list);
		msm_vidc_pool_free(inst, ts);
	}

	/* Calculate timestamp rate */
	list_for_each_entry(ts, &inst->timestamps.list, sort.list) {
		if (prev) {
			if (ts->sort.val == prev->sort.val)
				continue;
			ts_ms += div_u64(ts->sort.val - prev->sort.val, 1000000);
			counter++;
		}
		prev = ts;
	}
	if (ts_ms)
		timestamp_rate = (u32)div_u64((u64)counter * 1000, ts_ms);

	msm_vidc_update_cap_value(inst, TIMESTAMP_RATE, timestamp_rate << 16, __func__);

	return 0;
}

int msm_vidc_ts_reorder_insert_timestamp(struct msm_vidc_inst *inst, u64 timestamp)
{
	struct msm_vidc_timestamp *ts;
	struct msm_vidc_core *core;
	int rc = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* allocate ts from pool */
	ts = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_TIMESTAMP);
	if (!ts) {
		i_vpr_e(inst, "%s: ts alloc failed\n", __func__);
		return -ENOMEM;
	}

	/* initialize ts node */
	INIT_LIST_HEAD(&ts->sort.list);
	ts->sort.val = timestamp;
	rc = msm_vidc_insert_sort(&inst->ts_reorder.list, &ts->sort);
	if (rc)
		return rc;
	inst->ts_reorder.count++;

	return 0;
}

int msm_vidc_ts_reorder_remove_timestamp(struct msm_vidc_inst *inst, u64 timestamp)
{
	struct msm_vidc_timestamp *ts, *temp;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* remove matching node */
	list_for_each_entry_safe(ts, temp, &inst->ts_reorder.list, sort.list) {
		if (ts->sort.val == timestamp) {
			list_del_init(&ts->sort.list);
			inst->ts_reorder.count--;
			msm_vidc_pool_free(inst, ts);
			break;
		}
	}

	return 0;
}

int msm_vidc_ts_reorder_get_first_timestamp(struct msm_vidc_inst *inst, u64 *timestamp)
{
	struct msm_vidc_timestamp *ts;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !timestamp) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* check if list empty */
	if (list_empty(&inst->ts_reorder.list)) {
		i_vpr_e(inst, "%s: list empty. ts %lld\n", __func__, *timestamp);
		return -EINVAL;
	}

	/* get 1st node from reorder list */
	ts = list_first_entry(&inst->ts_reorder.list,
		struct msm_vidc_timestamp, sort.list);
	list_del_init(&ts->sort.list);

	/* copy timestamp */
	*timestamp = ts->sort.val;

	inst->ts_reorder.count--;
	msm_vidc_pool_free(inst, ts);

	return 0;
}

int msm_vidc_ts_reorder_flush(struct msm_vidc_inst *inst)
{
	struct msm_vidc_timestamp *temp, *ts = NULL;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* flush all entries */
	list_for_each_entry_safe(ts, temp, &inst->ts_reorder.list, sort.list) {
		i_vpr_l(inst, "%s: flushing ts: val %lld\n", __func__, ts->sort.val);
		list_del(&ts->sort.list);
		msm_vidc_pool_free(inst, ts);
	}
	inst->ts_reorder.count = 0;

	return 0;
}

struct msm_vidc_buffer *msm_vidc_get_driver_buf(struct msm_vidc_inst *inst,
	struct vb2_buffer *vb2)
{
	int rc = 0;
	struct msm_vidc_buffer *buf;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !vb2) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return NULL;
	}
	core = inst->core;

	buf = msm_vidc_fetch_buffer(inst, vb2);
	if (!buf) {
		i_vpr_e(inst, "%s: failed to fetch buffer\n", __func__);
		return NULL;
	}

	rc = vb2_buffer_to_driver(vb2, buf);
	if (rc)
		return NULL;

	/* treat every buffer as deferred buffer initially */
	buf->attr |= MSM_VIDC_ATTR_DEFERRED;

	if (is_decode_session(inst) && is_output_buffer(buf->type)) {
		/* get a reference */
		if (!buf->dbuf_get) {
			buf->dmabuf = call_mem_op(core, dma_buf_get, inst, buf->fd);
			if (!buf->dmabuf)
				return NULL;
			buf->dbuf_get = 1;
		}
	}

	/* update start timestamp */
	msm_vidc_add_buffer_stats(inst, buf);

	return buf;
}

int msm_vidc_allocate_buffers(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buf_type, u32 num_buffers)
{
	int rc = 0;
	int idx = 0;
	struct msm_vidc_buffer *buf = NULL;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	buffers = msm_vidc_get_buffers(inst, buf_type, __func__);
	if (!buffers)
		return -EINVAL;

	for (idx = 0; idx < num_buffers; idx++) {
		buf = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_BUFFER);
		if (!buf) {
			i_vpr_e(inst, "%s: alloc failed\n", __func__);
			return -EINVAL;
		}
		INIT_LIST_HEAD(&buf->list);
		list_add_tail(&buf->list, &buffers->list);
		buf->type = buf_type;
		buf->index = idx;
		buf->region = call_mem_op(core, buffer_region, inst, buf_type);
	}
	i_vpr_h(inst, "%s: allocated %d buffers for type %s\n",
		__func__, num_buffers, buf_name(buf_type));

	return rc;
}

int msm_vidc_free_buffers(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buf_type)
{
	int rc = 0;
	int buf_count = 0;
	struct msm_vidc_buffer *buf, *dummy;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	buffers = msm_vidc_get_buffers(inst, buf_type, __func__);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
		buf_count++;
		print_vidc_buffer(VIDC_LOW, "low ", "free buffer", inst, buf);
		list_del_init(&buf->list);
		msm_vidc_pool_free(inst, buf);
	}
	i_vpr_h(inst, "%s: freed %d buffers for type %s\n",
		__func__, buf_count, buf_name(buf_type));

	return rc;
}

struct msm_vidc_buffer *msm_vidc_fetch_buffer(struct msm_vidc_inst *inst,
	struct vb2_buffer *vb2)
{
	struct msm_vidc_buffer *buf = NULL;
	struct msm_vidc_buffers *buffers;
	enum msm_vidc_buffer_type buf_type;
	bool found = false;

	if (!inst || !vb2) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	buf_type = v4l2_type_to_driver(vb2->type, __func__);
	if (!buf_type)
		return NULL;

	buffers = msm_vidc_get_buffers(inst, buf_type, __func__);
	if (!buffers)
		return NULL;

	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->index == vb2->index) {
			found = true;
			break;
		}
	}

	if (!found) {
		i_vpr_e(inst, "%s: buffer not found for index %d for vb2 buffer type %s\n",
			__func__, vb2->index, v4l2_type_name(vb2->type));
		return NULL;
	}

	return buf;
}

struct msm_vidc_buffer *get_meta_buffer(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf)
{
	struct msm_vidc_buffer *mbuf;
	struct msm_vidc_buffers *buffers;
	bool found = false;

	if (!inst || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	if (buf->type == MSM_VIDC_BUF_INPUT) {
		buffers = &inst->buffers.input_meta;
	} else if (buf->type == MSM_VIDC_BUF_OUTPUT) {
		buffers = &inst->buffers.output_meta;
	} else {
		i_vpr_e(inst, "%s: invalid buffer type %d\n",
			__func__, buf->type);
		return NULL;
	}
	list_for_each_entry(mbuf, &buffers->list, list) {
		if (mbuf->index == buf->index) {
			found = true;
			break;
		}
	}
	if (!found)
		return NULL;

	return mbuf;
}

bool msm_vidc_is_super_buffer(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst_capability *capability = NULL;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return false;
	}

	capability = inst->capabilities;

	return !!capability->cap[SUPER_FRAME].value;
}

static bool is_single_session(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;
	u32 count = 0;

	if (!inst) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return false;
	}
	core = inst->core;

	core_lock(core, __func__);
	list_for_each_entry(inst, &core->instances, list)
		count++;
	core_unlock(core, __func__);

	return count == 1;
}

void msm_vidc_allow_dcvs(struct msm_vidc_inst *inst)
{
	bool allow = false;
	struct msm_vidc_core *core;
	u32 fps;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: Invalid args: %pK\n", __func__, inst);
		return;
	}
	core = inst->core;

	allow = !msm_vidc_clock_voting;
	if (!allow) {
		i_vpr_h(inst, "%s: core_clock_voting is set\n", __func__);
		goto exit;
	}

	allow = core->capabilities[DCVS].value;
	if (!allow) {
		i_vpr_h(inst, "%s: core doesn't support dcvs\n", __func__);
		goto exit;
	}

	allow = !inst->decode_batch.enable;
	if (!allow) {
		i_vpr_h(inst, "%s: decode_batching enabled\n", __func__);
		goto exit;
	}

	allow = !msm_vidc_is_super_buffer(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: encode_batching(super_buffer) enabled\n", __func__);
		goto exit;
	}

	allow = !is_thumbnail_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: thumbnail session\n", __func__);
		goto exit;
	}

	allow = is_realtime_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: non-realtime session\n", __func__);
		goto exit;
	}

	allow = !is_critical_priority_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: critical priority session\n", __func__);
		goto exit;
	}

	allow = !is_image_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: image session\n", __func__);
		goto exit;
	}

	allow = !is_lowlatency_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: lowlatency session\n", __func__);
		goto exit;
	}

	fps =  msm_vidc_get_fps(inst);
	if (is_decode_session(inst) &&
			fps >= inst->capabilities->cap[FRAME_RATE].max) {
		allow = false;
		i_vpr_h(inst, "%s: unsupported fps %d\n", __func__, fps);
		goto exit;
	}

exit:
	i_vpr_hp(inst, "%s: dcvs: %s\n", __func__, allow ? "enabled" : "disabled");

	inst->power.dcvs_flags = 0;
	inst->power.dcvs_mode = allow;
}

bool msm_vidc_allow_decode_batch(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst_capability *capability;
	struct msm_vidc_core *core;
	bool allow = false;
	u32 value = 0;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	core = inst->core;
	capability = inst->capabilities;

	allow = inst->decode_batch.enable;
	if (!allow) {
		i_vpr_h(inst, "%s: batching already disabled\n", __func__);
		goto exit;
	}

	allow = core->capabilities[DECODE_BATCH].value;
	if (!allow) {
		i_vpr_h(inst, "%s: core doesn't support batching\n", __func__);
		goto exit;
	}

	allow = is_single_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: multiple sessions running\n", __func__);
		goto exit;
	}

	allow = is_decode_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: not a decoder session\n", __func__);
		goto exit;
	}

	allow = !is_thumbnail_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: thumbnail session\n", __func__);
		goto exit;
	}

	allow = !is_image_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: image session\n", __func__);
		goto exit;
	}

	allow = is_realtime_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: non-realtime session\n", __func__);
		goto exit;
	}

	allow = !is_lowlatency_session(inst);
	if (!allow) {
		i_vpr_h(inst, "%s: lowlatency session\n", __func__);
		goto exit;
	}

	value = msm_vidc_get_fps(inst);
	allow = value < capability->cap[BATCH_FPS].value;
	if (!allow) {
		i_vpr_h(inst, "%s: unsupported fps %u, max %u\n", __func__,
			value, capability->cap[BATCH_FPS].value);
		goto exit;
	}

	value = msm_vidc_get_mbs_per_frame(inst);
	allow = value < capability->cap[BATCH_MBPF].value;
	if (!allow) {
		i_vpr_h(inst, "%s: unsupported mbpf %u, max %u\n", __func__,
			value, capability->cap[BATCH_MBPF].value);
		goto exit;
	}

exit:
	i_vpr_hp(inst, "%s: batching: %s\n", __func__, allow ? "enabled" : "disabled");

	return allow;
}

static void msm_vidc_update_input_cr(struct msm_vidc_inst *inst, u32 idx, u32 cr)
{
	struct msm_vidc_input_cr_data *temp = NULL, *next = NULL;
	bool found = false;

	list_for_each_entry_safe(temp, next, &inst->enc_input_crs, list) {
		if (temp->index == idx) {
			temp->input_cr = cr;
			found = true;
			break;
		}
	}
	if (!found) {
		temp = NULL;
		if (msm_vidc_vmem_alloc(sizeof(*temp), (void **)&temp, __func__))
			return;

		temp->index = idx;
		temp->input_cr = cr;
		list_add_tail(&temp->list, &inst->enc_input_crs);
	}
}

static void msm_vidc_free_input_cr_list(struct msm_vidc_inst *inst)
{
	struct msm_vidc_input_cr_data *temp, *next;

	list_for_each_entry_safe(temp, next, &inst->enc_input_crs, list) {
		list_del(&temp->list);
		msm_vidc_vmem_free((void **)&temp);
	}
	INIT_LIST_HEAD(&inst->enc_input_crs);
}

void msm_vidc_update_stats(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf, enum msm_vidc_debugfs_event etype)
{
	if (!inst || !buf || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	if ((is_decode_session(inst) && etype == MSM_VIDC_DEBUGFS_EVENT_ETB) ||
		(is_encode_session(inst) && etype == MSM_VIDC_DEBUGFS_EVENT_FBD))
		inst->stats.data_size += buf->data_size;

	msm_vidc_debugfs_update(inst, etype);
}

void msm_vidc_print_stats(struct msm_vidc_inst *inst)
{
	u32 frame_rate, operating_rate, achieved_fps, priority, etb, ebd, ftb, fbd, dt_ms;
	u64 bitrate_kbps = 0, time_ms = ktime_get_ns() / 1000 / 1000;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	etb = inst->debug_count.etb - inst->stats.count.etb;
	ebd = inst->debug_count.ebd - inst->stats.count.ebd;
	ftb = inst->debug_count.ftb - inst->stats.count.ftb;
	fbd = inst->debug_count.fbd - inst->stats.count.fbd;
	frame_rate = inst->capabilities->cap[FRAME_RATE].value >> 16;
	operating_rate = inst->capabilities->cap[OPERATING_RATE].value >> 16;
	priority =  inst->capabilities->cap[PRIORITY].value;

	dt_ms = time_ms - inst->stats.time_ms;
	achieved_fps = (fbd * 1000) / dt_ms;
	bitrate_kbps = (inst->stats.data_size * 8 * 1000) / (dt_ms * 1024);

	i_vpr_hs(inst,
		"stats: counts (etb,ebd,ftb,fbd): %u %u %u %u (total %llu %llu %llu %llu), achieved bitrate %lldKbps fps %u/s, frame rate %u, operating rate %u, priority %u, dt %ums\n",
		etb, ebd, ftb, fbd, inst->debug_count.etb, inst->debug_count.ebd,
		inst->debug_count.ftb, inst->debug_count.fbd,
		bitrate_kbps, achieved_fps, frame_rate, operating_rate, priority, dt_ms);

	inst->stats.count = inst->debug_count;
	inst->stats.data_size = 0;
	inst->stats.time_ms = time_ms;
}

int schedule_stats_work(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/**
	 * Hfi session is already closed and inst also going to be
	 * closed soon. So skip scheduling new stats_work to avoid
	 * use-after-free issues with close sequence.
	 */
	if (!inst->packet) {
		i_vpr_e(inst, "skip scheduling stats_work\n");
		return 0;
	}
	core = inst->core;
	mod_delayed_work(inst->workq, &inst->stats_work,
		msecs_to_jiffies(core->capabilities[STATS_TIMEOUT_MS].value));

	return 0;
}

int cancel_stats_work_sync(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: Invalid arguments\n", __func__);
		return -EINVAL;
	}
	cancel_delayed_work_sync(&inst->stats_work);

	return 0;
}

void msm_vidc_stats_handler(struct work_struct *work)
{
	struct msm_vidc_inst *inst;

	inst = container_of(work, struct msm_vidc_inst, stats_work.work);
	inst = get_inst_ref(g_core, inst);
	if (!inst || !inst->packet) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	inst_lock(inst, __func__);
	msm_vidc_print_stats(inst);
	schedule_stats_work(inst);
	inst_unlock(inst, __func__);

	put_inst(inst);
}

static int msm_vidc_queue_buffer(struct msm_vidc_inst *inst, struct msm_vidc_buffer *buf)
{
	struct msm_vidc_buffer *meta;
	enum msm_vidc_debugfs_event etype;
	int rc = 0;
	u32 cr = 0;

	if (!inst || !buf || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (is_encode_session(inst) && is_input_buffer(buf->type)) {
		cr = inst->capabilities->cap[ENC_IP_CR].value;
		msm_vidc_update_input_cr(inst, buf->index, cr);
		msm_vidc_update_cap_value(inst, ENC_IP_CR, 0, __func__);
	}

	if (is_decode_session(inst) && is_input_buffer(buf->type) &&
		inst->capabilities->cap[CODEC_CONFIG].value) {
		buf->flags |= MSM_VIDC_BUF_FLAG_CODECCONFIG;
		msm_vidc_update_cap_value(inst, CODEC_CONFIG, 0, __func__);
	}

	if (is_decode_session(inst) && is_output_buffer(buf->type)) {
		rc = msm_vidc_process_readonly_buffers(inst, buf);
		if (rc)
			return rc;
	}

	print_vidc_buffer(VIDC_HIGH, "high", "qbuf", inst, buf);
	meta = get_meta_buffer(inst, buf);
	if (meta && meta->attr & MSM_VIDC_ATTR_DEFERRED)
		print_vidc_buffer(VIDC_LOW, "low ", "qbuf", inst, meta);
	else
		meta = NULL;

	if (!meta && is_meta_enabled(inst, buf->type)) {
		print_vidc_buffer(VIDC_ERR, "err ", "missing meta for", inst, buf);
		return -EINVAL;
	}

	if (msm_vidc_is_super_buffer(inst) && is_input_buffer(buf->type))
		rc = venus_hfi_queue_super_buffer(inst, buf, meta);
	else
		rc = venus_hfi_queue_buffer(inst, buf, meta);
	if (rc)
		return rc;

	buf->attr &= ~MSM_VIDC_ATTR_DEFERRED;
	buf->attr |= MSM_VIDC_ATTR_QUEUED;
	if (meta) {
		meta->attr &= ~MSM_VIDC_ATTR_DEFERRED;
		meta->attr |= MSM_VIDC_ATTR_QUEUED;
	}

	/* insert timestamp for ts_reorder enable case */
	if (is_ts_reorder_allowed(inst) && is_input_buffer(buf->type)) {
		rc = msm_vidc_ts_reorder_insert_timestamp(inst, buf->timestamp);
		if (rc)
			i_vpr_e(inst, "%s: insert timestamp failed\n", __func__);
	}

	if (is_input_buffer(buf->type))
		inst->power.buffer_counter++;

	if (is_input_buffer(buf->type))
		etype = MSM_VIDC_DEBUGFS_EVENT_ETB;
	else
		etype = MSM_VIDC_DEBUGFS_EVENT_FTB;

	msm_vidc_update_stats(inst, buf, etype);

	return 0;
}

int msm_vidc_alloc_and_queue_input_internal_buffers(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = msm_vdec_get_input_internal_buffers(inst);
	if (rc)
		return rc;

	rc = msm_vdec_release_input_internal_buffers(inst);
	if (rc)
		return rc;

	rc = msm_vdec_create_input_internal_buffers(inst);
	if (rc)
		return rc;

	rc = msm_vdec_queue_input_internal_buffers(inst);
	if (rc)
		return rc;

	return rc;
}

int msm_vidc_queue_deferred_buffers(struct msm_vidc_inst *inst, enum msm_vidc_buffer_type buf_type)
{
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	int rc = 0;

	if (!inst || !buf_type) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	buffers = msm_vidc_get_buffers(inst, buf_type, __func__);
	if (!buffers)
		return -EINVAL;

	msm_vidc_scale_power(inst, true);

	list_for_each_entry(buf, &buffers->list, list) {
		if (!(buf->attr & MSM_VIDC_ATTR_DEFERRED))
			continue;
		rc = msm_vidc_queue_buffer(inst, buf);
		if (rc)
			return rc;
	}

	return 0;
}

int msm_vidc_queue_buffer_single(struct msm_vidc_inst *inst, struct vb2_buffer *vb2)
{
	int rc = 0;
	struct msm_vidc_buffer *buf = NULL;
	struct msm_vidc_fence *fence = NULL;
	enum msm_vidc_allow allow;

	if (!inst || !vb2 || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	buf = msm_vidc_get_driver_buf(inst, vb2);
	if (!buf)
		return -EINVAL;

	if (is_meta_rx_inp_enabled(inst, META_OUTBUF_FENCE) &&
		is_output_buffer(buf->type)) {
		fence = msm_vidc_fence_create(inst);
		if (!fence)
			return rc;
		buf->fence_id = fence->dma_fence.seqno;
	}

	allow = msm_vidc_allow_qbuf(inst, vb2->type);
	if (allow == MSM_VIDC_DISALLOW) {
		i_vpr_e(inst, "%s: qbuf not allowed\n", __func__);
		rc = -EINVAL;
		goto exit;
	} else if (allow == MSM_VIDC_DEFER) {
		print_vidc_buffer(VIDC_LOW, "low ", "qbuf deferred", inst, buf);
		rc = 0;
		goto exit;
	}

	msm_vidc_scale_power(inst, is_input_buffer(buf->type));

	rc = msm_vidc_queue_buffer(inst, buf);
	if (rc)
		goto exit;

exit:
	if (rc) {
		i_vpr_e(inst, "%s: qbuf failed\n", __func__);
		if (fence)
			msm_vidc_fence_destroy(inst, (u32)fence->dma_fence.seqno);
	}
	return rc;
}

int msm_vidc_destroy_internal_buffer(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buffer)
{
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_allocations *allocations;
	struct msm_vidc_mappings *mappings;
	struct msm_vidc_alloc *alloc, *alloc_dummy;
	struct msm_vidc_map  *map, *map_dummy;
	struct msm_vidc_buffer *buf, *dummy;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (!is_internal_buffer(buffer->type)) {
		i_vpr_e(inst, "%s: type: %s is not internal\n",
			__func__, buf_name(buffer->type));
		return 0;
	}

	i_vpr_h(inst, "%s: destroy: type: %8s, size: %9u, device_addr %#llx\n", __func__,
		buf_name(buffer->type), buffer->buffer_size, buffer->device_addr);

	buffers = msm_vidc_get_buffers(inst, buffer->type, __func__);
	if (!buffers)
		return -EINVAL;
	allocations = msm_vidc_get_allocations(inst, buffer->type, __func__);
	if (!allocations)
		return -EINVAL;
	mappings = msm_vidc_get_mappings(inst, buffer->type, __func__);
	if (!mappings)
		return -EINVAL;

	list_for_each_entry_safe(map, map_dummy, &mappings->list, list) {
		if (map->dmabuf == buffer->dmabuf) {
			call_mem_op(core, memory_unmap, core, map);
			list_del(&map->list);
			msm_vidc_pool_free(inst, map);
			break;
		}
	}

	list_for_each_entry_safe(alloc, alloc_dummy, &allocations->list, list) {
		if (alloc->dmabuf == buffer->dmabuf) {
			call_mem_op(core, memory_free, core, alloc);
			list_del(&alloc->list);
			msm_vidc_pool_free(inst, alloc);
			break;
		}
	}

	list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
		if (buf->dmabuf == buffer->dmabuf) {
			list_del(&buf->list);
			msm_vidc_pool_free(inst, buf);
			break;
		}
	}

	buffers->size = 0;
	buffers->min_count = buffers->extra_count = buffers->actual_count = 0;

	return 0;
}

int msm_vidc_get_internal_buffers(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type)
{
	u32 buf_size;
	u32 buf_count;
	struct msm_vidc_core *core;
	struct msm_vidc_buffers *buffers;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	buf_size = call_session_op(core, buffer_size,
		inst, buffer_type);

	buf_count = call_session_op(core, min_count,
		inst, buffer_type);

	buffers = msm_vidc_get_buffers(inst, buffer_type, __func__);
	if (!buffers)
		return -EINVAL;

	if (buf_size <= buffers->size &&
		buf_count <= buffers->min_count) {
		buffers->reuse = true;
	} else {
		buffers->reuse = false;
		buffers->size = buf_size;
		buffers->min_count = buf_count;
	}
	return 0;
}

int msm_vidc_create_internal_buffer(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type, u32 index)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_allocations *allocations;
	struct msm_vidc_mappings *mappings;
	struct msm_vidc_buffer *buffer;
	struct msm_vidc_alloc *alloc;
	struct msm_vidc_map *map;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;
	if (!is_internal_buffer(buffer_type)) {
		i_vpr_e(inst, "%s: type %s is not internal\n",
			__func__, buf_name(buffer_type));
		return 0;
	}

	buffers = msm_vidc_get_buffers(inst, buffer_type, __func__);
	if (!buffers)
		return -EINVAL;
	allocations = msm_vidc_get_allocations(inst, buffer_type, __func__);
	if (!allocations)
		return -EINVAL;
	mappings = msm_vidc_get_mappings(inst, buffer_type, __func__);
	if (!mappings)
		return -EINVAL;

	if (!buffers->size)
		return 0;

	buffer = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_BUFFER);
	if (!buffer) {
		i_vpr_e(inst, "%s: buf alloc failed\n", __func__);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&buffer->list);
	buffer->type = buffer_type;
	buffer->index = index;
	buffer->buffer_size = buffers->size;
	list_add_tail(&buffer->list, &buffers->list);

	alloc = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_ALLOC);
	if (!alloc) {
		i_vpr_e(inst, "%s: alloc failed\n", __func__);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&alloc->list);
	alloc->type = buffer_type;
	alloc->region = call_mem_op(core, buffer_region, inst, buffer_type);
	alloc->size = buffer->buffer_size;
	alloc->secure = is_secure_region(alloc->region);
	rc = call_mem_op(core, memory_alloc, core, alloc);
	if (rc)
		return -ENOMEM;
	list_add_tail(&alloc->list, &allocations->list);

	map = msm_vidc_pool_alloc(inst, MSM_MEM_POOL_MAP);
	if (!map) {
		i_vpr_e(inst, "%s: map alloc failed\n", __func__);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&map->list);
	map->type = alloc->type;
	map->region = alloc->region;
	map->dmabuf = alloc->dmabuf;
	rc = call_mem_op(core, memory_map, core, map);
	if (rc)
		return -ENOMEM;
	list_add_tail(&map->list, &mappings->list);

	buffer->dmabuf = alloc->dmabuf;
	buffer->device_addr = map->device_addr;
	buffer->region = map->region;
	i_vpr_h(inst, "%s: create: type: %8s, size: %9u, device_addr %#llx\n", __func__,
		buf_name(buffer_type), buffers->size, buffer->device_addr);

	return 0;
}

int msm_vidc_create_internal_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type buffer_type)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	int i;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	buffers = msm_vidc_get_buffers(inst, buffer_type, __func__);
	if (!buffers)
		return -EINVAL;

	if (buffers->reuse) {
		i_vpr_l(inst, "%s: reuse enabled for %s\n", __func__, buf_name(buffer_type));
		return 0;
	}

	for (i = 0; i < buffers->min_count; i++) {
		rc = msm_vidc_create_internal_buffer(inst, buffer_type, i);
		if (rc)
			return rc;
	}

	return rc;
}

int msm_vidc_queue_internal_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type buffer_type)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buffer, *dummy;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!is_internal_buffer(buffer_type)) {
		i_vpr_e(inst, "%s: %s is not internal\n", __func__, buf_name(buffer_type));
		return 0;
	}

	/*
	 * Set HFI_PROP_COMV_BUFFER_COUNT to firmware even if COMV buffer
	 * is reused.
	 */
	if (is_decode_session(inst) && buffer_type == MSM_VIDC_BUF_COMV) {
		rc = msm_vdec_set_num_comv(inst);
		if (rc)
			return rc;
	}

	buffers = msm_vidc_get_buffers(inst, buffer_type, __func__);
	if (!buffers)
		return -EINVAL;

	if (buffers->reuse) {
		i_vpr_l(inst, "%s: reuse enabled for %s buf\n",
			__func__, buf_name(buffer_type));
		return 0;
	}

	list_for_each_entry_safe(buffer, dummy, &buffers->list, list) {
		/* do not queue pending release buffers */
		if (buffer->flags & MSM_VIDC_ATTR_PENDING_RELEASE)
			continue;
		/* do not queue already queued buffers */
		if (buffer->attr & MSM_VIDC_ATTR_QUEUED)
			continue;
		rc = venus_hfi_queue_buffer(inst, buffer, NULL);
		if (rc)
			return rc;
		/* mark queued */
		buffer->attr |= MSM_VIDC_ATTR_QUEUED;

		i_vpr_h(inst, "%s: queue: type: %8s, size: %9u, device_addr %#llx\n", __func__,
			buf_name(buffer->type), buffer->buffer_size, buffer->device_addr);
	}

	return 0;
}

int msm_vidc_alloc_and_queue_session_internal_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type buffer_type)
{
	int rc = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (buffer_type != MSM_VIDC_BUF_ARP &&
		buffer_type != MSM_VIDC_BUF_PERSIST) {
		i_vpr_e(inst, "%s: invalid buffer type: %s\n",
			__func__, buf_name(buffer_type));
		rc = -EINVAL;
		goto exit;
	}

	rc = msm_vidc_get_internal_buffers(inst, buffer_type);
	if (rc)
		goto exit;

	rc = msm_vidc_create_internal_buffers(inst, buffer_type);
	if (rc)
		goto exit;

	rc = msm_vidc_queue_internal_buffers(inst, buffer_type);
	if (rc)
		goto exit;

exit:
	return rc;
}

int msm_vidc_release_internal_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type buffer_type)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buffer, *dummy;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!is_internal_buffer(buffer_type)) {
		i_vpr_e(inst, "%s: %s is not internal\n",
			__func__, buf_name(buffer_type));
		return 0;
	}

	buffers = msm_vidc_get_buffers(inst, buffer_type, __func__);
	if (!buffers)
		return -EINVAL;

	if (buffers->reuse) {
		i_vpr_l(inst, "%s: reuse enabled for %s buf\n",
			__func__, buf_name(buffer_type));
		return 0;
	}

	list_for_each_entry_safe(buffer, dummy, &buffers->list, list) {
		/* do not release already pending release buffers */
		if (buffer->attr & MSM_VIDC_ATTR_PENDING_RELEASE)
			continue;
		/* release only queued buffers */
		if (!(buffer->attr & MSM_VIDC_ATTR_QUEUED))
			continue;
		rc = venus_hfi_release_buffer(inst, buffer);
		if (rc)
			return rc;
		/* mark pending release */
		buffer->attr |= MSM_VIDC_ATTR_PENDING_RELEASE;

		i_vpr_h(inst, "%s: release: type: %8s, size: %9u, device_addr %#llx\n", __func__,
			buf_name(buffer->type), buffer->buffer_size, buffer->device_addr);
	}

	return 0;
}

int msm_vidc_vb2_buffer_done(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buf)
{
	int type, port, state;
	struct vb2_queue *q;
	struct vb2_buffer *vb2;
	struct vb2_v4l2_buffer *vbuf;
	bool found;

	if (!inst || !inst->capabilities || !buf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	type = v4l2_type_from_driver(buf->type, __func__);
	if (!type)
		return -EINVAL;
	port = v4l2_type_to_driver_port(inst, type, __func__);
	if (port < 0)
		return -EINVAL;

	q = inst->bufq[port].vb2q;
	if (!q->streaming) {
		i_vpr_e(inst, "%s: port %d is not streaming\n",
			__func__, port);
		return -EINVAL;
	}

	found = false;
	list_for_each_entry(vb2, &q->queued_list, queued_entry) {
		if (vb2->state != VB2_BUF_STATE_ACTIVE)
			continue;
		if (vb2->index == buf->index) {
			found = true;
			break;
		}
	}
	if (!found) {
		print_vidc_buffer(VIDC_ERR, "err ", "vb2 not found for", inst, buf);
		return -EINVAL;
	}
	/**
	 * v4l2 clears buffer state related flags. For driver errors
	 * send state as error to avoid skipping V4L2_BUF_FLAG_ERROR
	 * flag at v4l2 side.
	 */
	if (buf->flags & MSM_VIDC_BUF_FLAG_ERROR)
		state = VB2_BUF_STATE_ERROR;
	else
		state = VB2_BUF_STATE_DONE;

	vbuf = to_vb2_v4l2_buffer(vb2);
	vbuf->flags = buf->flags;
	vb2->timestamp = buf->timestamp;
	vb2->planes[0].bytesused = buf->data_size + vb2->planes[0].data_offset;
	vb2_buffer_done(vb2, state);

	return 0;
}

int msm_vidc_event_queue_init(struct msm_vidc_inst *inst)
{
	int rc = 0;
	int index;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (is_decode_session(inst))
		index = 0;
	else if (is_encode_session(inst))
		index = 1;
	else
		return -EINVAL;

	v4l2_fh_init(&inst->event_handler, &core->vdev[index].vdev);
	inst->event_handler.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->event_handler);

	return rc;
}

int msm_vidc_event_queue_deinit(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/* do not deinit, if not already inited */
	if (!inst->event_handler.vdev) {
		i_vpr_e(inst, "%s: already not inited\n", __func__);
		return 0;
	}

	v4l2_fh_del(&inst->event_handler);
	v4l2_fh_exit(&inst->event_handler);

	return rc;
}

static int vb2q_init(struct msm_vidc_inst *inst,
	struct vb2_queue *q, enum v4l2_buf_type type)
{
	int rc = 0;
	struct msm_vidc_core *core;

	if (!inst || !q || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	q->type = type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	q->ops = core->vb2_ops;
	q->mem_ops = core->vb2_mem_ops;
	q->drv_priv = inst;
	q->allow_zero_bytesused = 1;
	q->copy_timestamp = 1;
	rc = vb2_queue_init(q);
	if (rc)
		i_vpr_e(inst, "%s: vb2_queue_init failed for type %d\n",
				__func__, type);
	return rc;
}

static int m2m_queue_init(void *priv, struct vb2_queue *src_vq,
	struct vb2_queue *dst_vq)
{
	int rc = 0;
	struct msm_vidc_inst *inst = priv;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !src_vq || !dst_vq) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;
	if (!core->capabilities) {
		d_vpr_e("%s: invalid core capabilities\n", __func__);
		return -EINVAL;
	}

	src_vq->supports_requests = core->capabilities[SUPPORTS_REQUESTS].value;
	src_vq->lock = &inst->request_lock;
	src_vq->dev = &core->pdev->dev;
	rc = vb2q_init(inst, src_vq, INPUT_MPLANE);
	if (rc)
		goto fail_input_vb2q_init;
	inst->bufq[INPUT_PORT].vb2q = src_vq;

	dst_vq->lock = src_vq->lock;
	dst_vq->dev = &core->pdev->dev;
	rc = vb2q_init(inst, dst_vq, OUTPUT_MPLANE);
	if (rc)
		goto fail_out_vb2q_init;
	inst->bufq[OUTPUT_PORT].vb2q = dst_vq;
	return rc;

fail_out_vb2q_init:
	vb2_queue_release(inst->bufq[INPUT_PORT].vb2q);
fail_input_vb2q_init:
	return rc;
}

int msm_vidc_vb2_queue_init(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (inst->vb2q_init) {
		i_vpr_h(inst, "%s: vb2q already inited\n", __func__);
		return 0;
	}

	inst->m2m_dev = v4l2_m2m_init(core->v4l2_m2m_ops);
	if (IS_ERR(inst->m2m_dev)) {
		i_vpr_e(inst, "%s: failed to initialize v4l2 m2m device\n", __func__);
		rc = PTR_ERR(inst->m2m_dev);
		goto fail_m2m_init;
	}

	/* v4l2_m2m_ctx_init will do input & output queues initialization */
	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, m2m_queue_init);
	if (!inst->m2m_ctx) {
		i_vpr_e(inst, "%s: v4l2_m2m_ctx_init failed\n", __func__);
		goto fail_m2m_ctx_init;
	}
	inst->event_handler.m2m_ctx = inst->m2m_ctx;

	rc = msm_vidc_vmem_alloc(sizeof(struct vb2_queue),
			(void **)&inst->bufq[INPUT_META_PORT].vb2q, "input meta port");
	if (rc)
		goto fail_in_meta_alloc;

	/* do input meta port queues initialization */
	rc = vb2q_init(inst, inst->bufq[INPUT_META_PORT].vb2q, INPUT_META_PLANE);
	if (rc)
		goto fail_in_meta_vb2q_init;

	rc = msm_vidc_vmem_alloc(sizeof(struct vb2_queue),
			(void **)&inst->bufq[OUTPUT_META_PORT].vb2q, "output meta port");
	if (rc)
		goto fail_out_meta_alloc;

	/* do output meta port queues initialization */
	rc = vb2q_init(inst, inst->bufq[OUTPUT_META_PORT].vb2q, OUTPUT_META_PLANE);
	if (rc)
		goto fail_out_meta_vb2q_init;
	inst->vb2q_init = true;

	return 0;

fail_out_meta_vb2q_init:
	msm_vidc_vmem_free((void **)&inst->bufq[OUTPUT_META_PORT].vb2q);
	inst->bufq[OUTPUT_META_PORT].vb2q = NULL;
fail_out_meta_alloc:
	vb2_queue_release(inst->bufq[INPUT_META_PORT].vb2q);
fail_in_meta_vb2q_init:
	msm_vidc_vmem_free((void **)&inst->bufq[INPUT_META_PORT].vb2q);
	inst->bufq[INPUT_META_PORT].vb2q = NULL;
fail_in_meta_alloc:
	v4l2_m2m_ctx_release(inst->m2m_ctx);
	inst->bufq[OUTPUT_PORT].vb2q = NULL;
	inst->bufq[INPUT_PORT].vb2q = NULL;
fail_m2m_ctx_init:
	v4l2_m2m_release(inst->m2m_dev);
fail_m2m_init:
	return rc;
}

int msm_vidc_vb2_queue_deinit(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!inst->vb2q_init) {
		i_vpr_h(inst, "%s: vb2q already deinited\n", __func__);
		return 0;
	}

	vb2_queue_release(inst->bufq[OUTPUT_META_PORT].vb2q);
	msm_vidc_vmem_free((void **)&inst->bufq[OUTPUT_META_PORT].vb2q);
	inst->bufq[OUTPUT_META_PORT].vb2q = NULL;
	vb2_queue_release(inst->bufq[INPUT_META_PORT].vb2q);
	msm_vidc_vmem_free((void **)&inst->bufq[INPUT_META_PORT].vb2q);
	inst->bufq[INPUT_META_PORT].vb2q = NULL;
	/*
	 * vb2_queue_release() for input and output queues
	 * is called from v4l2_m2m_ctx_release()
	 */
	v4l2_m2m_ctx_release(inst->m2m_ctx);
	inst->bufq[OUTPUT_PORT].vb2q = NULL;
	inst->bufq[INPUT_PORT].vb2q = NULL;
	v4l2_m2m_release(inst->m2m_dev);
	inst->vb2q_init = false;

	return rc;
}

int msm_vidc_add_session(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_inst *i;
	struct msm_vidc_core *core;
	u32 count = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (!core->capabilities) {
		i_vpr_e(inst, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core_lock(core, __func__);
	if (core->state != MSM_VIDC_CORE_INIT) {
		i_vpr_e(inst, "%s: invalid state %s\n",
			__func__, core_state_name(core->state));
		rc = -EINVAL;
		goto unlock;
	}
	list_for_each_entry(i, &core->instances, list)
		count++;

	if (count < core->capabilities[MAX_SESSION_COUNT].value) {
		list_add_tail(&inst->list, &core->instances);
	} else {
		i_vpr_e(inst, "%s: max limit %d already running %d sessions\n",
			__func__, core->capabilities[MAX_SESSION_COUNT].value, count);
		rc = -EINVAL;
	}
unlock:
	core_unlock(core, __func__);

	return rc;
}

int msm_vidc_remove_session(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst *i, *temp;
	struct msm_vidc_core *core;
	u32 count = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	core_lock(core, __func__);
	list_for_each_entry_safe(i, temp, &core->instances, list) {
		if (i->session_id == inst->session_id) {
			list_move_tail(&i->list, &core->dangling_instances);
			i_vpr_h(inst, "%s: removed session %#x\n",
				__func__, i->session_id);
		}
	}
	list_for_each_entry(i, &core->instances, list)
		count++;
	i_vpr_h(inst, "%s: remaining sessions %d\n", __func__, count);
	core_unlock(core, __func__);

	return 0;
}

static int msm_vidc_remove_dangling_session(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst *i, *temp;
	struct msm_vidc_core *core;
	u32 count = 0, dcount = 0;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	core_lock(core, __func__);
	list_for_each_entry_safe(i, temp, &core->dangling_instances, list) {
		if (i->session_id == inst->session_id) {
			list_del_init(&i->list);
			i_vpr_h(inst, "%s: removed dangling session %#x\n",
				__func__, i->session_id);
			break;
		}
	}
	list_for_each_entry(i, &core->instances, list)
		count++;
	list_for_each_entry(i, &core->dangling_instances, list)
		dcount++;
	i_vpr_h(inst, "%s: remaining sessions. active %d, dangling %d\n",
		__func__, count, dcount);
	core_unlock(core, __func__);

	return 0;
}

int msm_vidc_session_open(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	inst->packet_size = 4096;
	rc = msm_vidc_vmem_alloc(inst->packet_size, (void **)&inst->packet, __func__);
	if (rc)
		return rc;

	rc = venus_hfi_session_open(inst);
	if (rc)
		goto error;

	return 0;
error:
	i_vpr_e(inst, "%s(): session open failed\n", __func__);
	msm_vidc_vmem_free((void **)&inst->packet);
	inst->packet = NULL;
	return rc;
}

int msm_vidc_session_set_codec(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = venus_hfi_session_set_codec(inst);
	if (rc)
		return rc;

	return 0;
}

int msm_vidc_session_set_secure_mode(struct msm_vidc_inst *inst)
{
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = venus_hfi_session_set_secure_mode(inst);
	if (rc)
		return rc;

	return 0;
}

int msm_vidc_session_set_default_header(struct msm_vidc_inst *inst)
{
	int rc = 0;
	u32 default_header = false;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	default_header = inst->capabilities->cap[DEFAULT_HEADER].value;
	i_vpr_h(inst, "%s: default header: %d", __func__, default_header);
	rc = venus_hfi_session_property(inst,
			HFI_PROP_DEC_DEFAULT_HEADER,
			HFI_HOST_FLAGS_NONE,
			get_hfi_port(inst, INPUT_PORT),
			HFI_PAYLOAD_U32,
			&default_header,
			sizeof(u32));
	if (rc)
		i_vpr_e(inst, "%s: set property failed\n", __func__);
	return rc;
}

int msm_vidc_session_streamoff(struct msm_vidc_inst *inst,
	enum msm_vidc_port_type port)
{
	int rc = 0;
	int count = 0;
	struct msm_vidc_core *core;
	enum signal_session_response signal_type;
	enum msm_vidc_buffer_type buffer_type;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (port == INPUT_PORT) {
		signal_type = SIGNAL_CMD_STOP_INPUT;
		buffer_type = MSM_VIDC_BUF_INPUT;
	} else if (port == OUTPUT_PORT) {
		signal_type = SIGNAL_CMD_STOP_OUTPUT;
		buffer_type = MSM_VIDC_BUF_OUTPUT;
	} else {
		i_vpr_e(inst, "%s: invalid port: %d\n", __func__, port);
		return -EINVAL;
	}

	rc = venus_hfi_stop(inst, port);
	if (rc)
		goto error;

	rc = msm_vidc_state_change_streamoff(inst, port);
	if (rc)
		goto error;

	core = inst->core;
	i_vpr_h(inst, "%s: wait on port: %d for time: %d ms\n",
		__func__, port, core->capabilities[HW_RESPONSE_TIMEOUT].value);
	inst_unlock(inst, __func__);
	rc = wait_for_completion_timeout(
			&inst->completions[signal_type],
			msecs_to_jiffies(
			core->capabilities[HW_RESPONSE_TIMEOUT].value));
	if (!rc) {
		i_vpr_e(inst, "%s: session stop timed out for port: %d\n",
				__func__, port);
		rc = -ETIMEDOUT;
		msm_vidc_inst_timeout(inst);
	} else {
		rc = 0;
	}
	inst_lock(inst, __func__);

	if(rc)
		goto error;

	if (port == INPUT_PORT) {
		/* flush input timer list */
		msm_vidc_flush_input_timer(inst);
	}

	/* no more queued buffers after streamoff */
	count = msm_vidc_num_buffers(inst, buffer_type, MSM_VIDC_ATTR_QUEUED);
	if (!count) {
		i_vpr_h(inst, "%s: stop successful on port: %d\n",
			__func__, port);
	} else {
		i_vpr_e(inst,
			"%s: %d buffers pending with firmware on port: %d\n",
			__func__, count, port);
		rc = -EINVAL;
		goto error;
	}

	/* flush deferred buffers */
	msm_vidc_flush_buffers(inst, buffer_type);
	msm_vidc_flush_read_only_buffers(inst, buffer_type);
	return 0;

error:
	msm_vidc_kill_session(inst);
	msm_vidc_flush_buffers(inst, buffer_type);
	msm_vidc_flush_read_only_buffers(inst, buffer_type);
	return rc;
}

int msm_vidc_session_close(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = venus_hfi_session_close(inst);
	if (rc)
		return rc;

	/* we are not supposed to send any more commands after close */
	i_vpr_h(inst, "%s: free session packet data\n", __func__);
	msm_vidc_vmem_free((void **)&inst->packet);
	inst->packet = NULL;

	core = inst->core;
	i_vpr_h(inst, "%s: wait on close for time: %d ms\n",
		__func__, core->capabilities[HW_RESPONSE_TIMEOUT].value);
	inst_unlock(inst, __func__);
	rc = wait_for_completion_timeout(
			&inst->completions[SIGNAL_CMD_CLOSE],
			msecs_to_jiffies(
			core->capabilities[HW_RESPONSE_TIMEOUT].value));
	if (!rc) {
		i_vpr_e(inst, "%s: session close timed out\n", __func__);
		rc = -ETIMEDOUT;
		msm_vidc_inst_timeout(inst);
	} else {
		rc = 0;
		i_vpr_h(inst, "%s: close successful\n", __func__);
	}
	inst_lock(inst, __func__);

	msm_vidc_change_state(inst, MSM_VIDC_CLOSE, __func__);
	inst->sub_state = MSM_VIDC_SUB_STATE_NONE;
	strlcpy(inst->sub_state_name, "SUB_STATE_NONE", sizeof(inst->sub_state_name));

	return rc;
}

int msm_vidc_kill_session(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!inst->session_id) {
		i_vpr_e(inst, "%s: already killed\n", __func__);
		return 0;
	}

	i_vpr_e(inst, "%s: killing session\n", __func__);
	msm_vidc_session_close(inst);
	msm_vidc_change_state(inst, MSM_VIDC_ERROR, __func__);

	return 0;
}

int msm_vidc_get_inst_capability(struct msm_vidc_inst *inst)
{
	int rc = 0;
	int i;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	for (i = 0; i < core->codecs_count; i++) {
		if (core->inst_caps[i].domain == inst->domain &&
			core->inst_caps[i].codec == inst->codec) {
			i_vpr_h(inst,
				"%s: copied capabilities with %#x codec, %#x domain\n",
				__func__, inst->codec, inst->domain);
			memcpy(inst->capabilities, &core->inst_caps[i],
				sizeof(struct msm_vidc_inst_capability));
		}
	}

	return rc;
}

int msm_vidc_deinit_core_caps(struct msm_vidc_core *core)
{
	int rc = 0;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	msm_vidc_vmem_free((void **)&core->capabilities);
	core->capabilities = NULL;
	d_vpr_h("%s: Core capabilities freed\n", __func__);

	return rc;
}

int msm_vidc_init_core_caps(struct msm_vidc_core *core)
{
	int rc = 0;
	int i, num_platform_caps;
	struct msm_platform_core_capability *platform_data;

	if (!core || !core->platform) {
		d_vpr_e("%s: invalid params\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	platform_data = core->platform->data.core_data;
	if (!platform_data) {
		d_vpr_e("%s: platform core data is NULL\n",
				__func__);
			rc = -EINVAL;
			goto exit;
	}

	rc = msm_vidc_vmem_alloc((sizeof(struct msm_vidc_core_capability) *
		(CORE_CAP_MAX + 1)), (void **)&core->capabilities, __func__);
	if (rc)
		goto exit;

	num_platform_caps = core->platform->data.core_data_size;

	/* loop over platform caps */
	for (i = 0; i < num_platform_caps && i < CORE_CAP_MAX; i++) {
		core->capabilities[platform_data[i].type].type = platform_data[i].type;
		core->capabilities[platform_data[i].type].value = platform_data[i].value;
	}

exit:
	return rc;
}

static void update_inst_capability(struct msm_platform_inst_capability *in,
		struct msm_vidc_inst_capability *capability)
{
	if (!in || !capability) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, in, capability);
		return;
	}
	if (in->cap_id >= INST_CAP_MAX) {
		d_vpr_e("%s: invalid cap id %d\n", __func__, in->cap_id);
		return;
	}

	capability->cap[in->cap_id].cap_id = in->cap_id;
	capability->cap[in->cap_id].min = in->min;
	capability->cap[in->cap_id].max = in->max;
	capability->cap[in->cap_id].step_or_mask = in->step_or_mask;
	capability->cap[in->cap_id].value = in->value;
	capability->cap[in->cap_id].flags = in->flags;
	capability->cap[in->cap_id].v4l2_id = in->v4l2_id;
	capability->cap[in->cap_id].hfi_id = in->hfi_id;
}

static void update_inst_cap_dependency(
	struct msm_platform_inst_cap_dependency *in,
	struct msm_vidc_inst_capability *capability)
{
	if (!in || !capability) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, in, capability);
		return;
	}
	if (in->cap_id >= INST_CAP_MAX) {
		d_vpr_e("%s: invalid cap id %d\n", __func__, in->cap_id);
		return;
	}

	capability->cap[in->cap_id].cap_id = in->cap_id;
	memcpy(capability->cap[in->cap_id].parents, in->parents,
		sizeof(capability->cap[in->cap_id].parents));
	memcpy(capability->cap[in->cap_id].children, in->children,
		sizeof(capability->cap[in->cap_id].children));
	capability->cap[in->cap_id].adjust = in->adjust;
	capability->cap[in->cap_id].set = in->set;
}

int msm_vidc_deinit_instance_caps(struct msm_vidc_core *core)
{
	int rc = 0;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	msm_vidc_vmem_free((void **)&core->inst_caps);
	core->inst_caps = NULL;
	d_vpr_h("%s: core->inst_caps freed\n", __func__);

	return rc;
}

int msm_vidc_init_instance_caps(struct msm_vidc_core *core)
{
	int rc = 0;
	u8 enc_valid_codecs, dec_valid_codecs;
	u8 count_bits, enc_codec_count;
	u8 codecs_count = 0;
	int i, j, check_bit;
	int num_platform_cap_data, num_platform_cap_dependency_data;
	struct msm_platform_inst_capability *platform_cap_data = NULL;
	struct msm_platform_inst_cap_dependency *platform_cap_dependency_data = NULL;

	if (!core || !core->platform || !core->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		rc = -EINVAL;
		goto error;
	}

	platform_cap_data = core->platform->data.inst_cap_data;
	if (!platform_cap_data) {
		d_vpr_e("%s: platform instance cap data is NULL\n",
				__func__);
			rc = -EINVAL;
		goto error;
	}

	platform_cap_dependency_data = core->platform->data.inst_cap_dependency_data;
	if (!platform_cap_dependency_data) {
		d_vpr_e("%s: platform instance cap dependency data is NULL\n",
				__func__);
			rc = -EINVAL;
		goto error;
	}

	enc_valid_codecs = core->capabilities[ENC_CODECS].value;
	count_bits = enc_valid_codecs;
	COUNT_BITS(count_bits, codecs_count);
	enc_codec_count = codecs_count;

	dec_valid_codecs = core->capabilities[DEC_CODECS].value;
	count_bits = dec_valid_codecs;
	COUNT_BITS(count_bits, codecs_count);

	core->codecs_count = codecs_count;
	rc = msm_vidc_vmem_alloc(codecs_count * sizeof(struct msm_vidc_inst_capability),
		(void **)&core->inst_caps, __func__);
	if (rc)
		goto error;

	check_bit = 0;
	/* determine codecs for enc domain */
	for (i = 0; i < enc_codec_count; i++) {
		while (check_bit < (sizeof(enc_valid_codecs) * 8)) {
			if (enc_valid_codecs & BIT(check_bit)) {
				core->inst_caps[i].domain = MSM_VIDC_ENCODER;
				core->inst_caps[i].codec = enc_valid_codecs &
						BIT(check_bit);
				check_bit++;
				break;
			}
			check_bit++;
		}
	}

	/* reset checkbit to check from 0th bit of decoder codecs set bits*/
	check_bit = 0;
	/* determine codecs for dec domain */
	for (; i < codecs_count; i++) {
		while (check_bit < (sizeof(dec_valid_codecs) * 8)) {
			if (dec_valid_codecs & BIT(check_bit)) {
				core->inst_caps[i].domain = MSM_VIDC_DECODER;
				core->inst_caps[i].codec = dec_valid_codecs &
						BIT(check_bit);
				check_bit++;
				break;
			}
			check_bit++;
		}
	}

	num_platform_cap_data = core->platform->data.inst_cap_data_size;
	num_platform_cap_dependency_data = core->platform->data.inst_cap_dependency_data_size;
	d_vpr_h("%s: num caps %d, dependency %d\n", __func__,
		num_platform_cap_data, num_platform_cap_dependency_data);

	/* loop over each platform capability */
	for (i = 0; i < num_platform_cap_data; i++) {
		/* select matching core codec and update it */
		for (j = 0; j < codecs_count; j++) {
			if ((platform_cap_data[i].domain &
				core->inst_caps[j].domain) &&
				(platform_cap_data[i].codec &
				core->inst_caps[j].codec)) {
				/* update core capability */
				update_inst_capability(&platform_cap_data[i],
					&core->inst_caps[j]);
			}
		}
	}

	/* loop over each platform dependency capability */
	for (i = 0; i < num_platform_cap_dependency_data; i++) {
		/* select matching core codec and update it */
		for (j = 0; j < codecs_count; j++) {
			if ((platform_cap_dependency_data[i].domain &
				core->inst_caps[j].domain) &&
				(platform_cap_dependency_data[i].codec &
				core->inst_caps[j].codec)) {
				/* update core dependency capability */
				update_inst_cap_dependency(
					&platform_cap_dependency_data[i],
					&core->inst_caps[j]);
			}
		}
	}

error:
	return rc;
}

int msm_vidc_core_deinit_locked(struct msm_vidc_core *core, bool force)
{
	int rc = 0;
	struct msm_vidc_inst *inst, *dummy;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	rc = __strict_check(core, __func__);
	if (rc) {
		d_vpr_e("%s(): core was not locked\n", __func__);
		return rc;
	}

	if (is_core_state(core, MSM_VIDC_CORE_DEINIT))
		return 0;

	if (force) {
		d_vpr_e("%s(): force deinit core\n", __func__);
	} else {
		/* in normal case, deinit core only if no session present */
		if (!list_empty(&core->instances)) {
			d_vpr_h("%s(): skip deinit\n", __func__);
			return 0;
		} else {
			d_vpr_h("%s(): deinit core\n", __func__);
		}
	}

	venus_hfi_core_deinit(core, force);

	/* unlink all sessions from core, if any */
	list_for_each_entry_safe(inst, dummy, &core->instances, list) {
		msm_vidc_change_state(inst, MSM_VIDC_ERROR, __func__);
		list_move_tail(&inst->list, &core->dangling_instances);
	}
	msm_vidc_change_core_state(core, MSM_VIDC_CORE_DEINIT, __func__);

	return rc;
}

int msm_vidc_core_deinit(struct msm_vidc_core *core, bool force)
{
	int rc = 0;
	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core_lock(core, __func__);
	rc = msm_vidc_core_deinit_locked(core, force);
	core_unlock(core, __func__);

	return rc;
}

int msm_vidc_core_init_wait(struct msm_vidc_core *core)
{
	const int interval = 10;
	int max_tries, count = 0, rc = 0;

	if (!core || !core->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core_lock(core, __func__);
	if (is_core_state(core, MSM_VIDC_CORE_INIT)) {
		rc = 0;
		goto unlock;
	} else if (is_core_state(core, MSM_VIDC_CORE_DEINIT)) {
		rc = -EINVAL;
		goto unlock;
	}

	d_vpr_h("%s(): waiting for state change\n", __func__);
	max_tries = core->capabilities[HW_RESPONSE_TIMEOUT].value / interval;
	while (count < max_tries) {
		if (core->state != MSM_VIDC_CORE_INIT_WAIT)
			break;

		core_unlock(core, __func__);
		msleep_interruptible(interval);
		core_lock(core, __func__);
		count++;
	}
	d_vpr_h("%s: state %s, interval %u, count %u, max_tries %u\n", __func__,
		core_state_name(core->state), interval, count, max_tries);

	if (is_core_state(core, MSM_VIDC_CORE_INIT)) {
		d_vpr_h("%s: sys init successful\n", __func__);
		rc = 0;
		goto unlock;
	} else {
		d_vpr_h("%s: sys init wait timedout. state %s\n",
			__func__, core_state_name(core->state));
		/* mark video hw unresponsive */
		msm_vidc_change_core_sub_state(core,
			0, CORE_SUBSTATE_VIDEO_UNRESPONSIVE, __func__);
		rc = -EINVAL;
		goto unlock;
	}
unlock:
	if (rc)
		msm_vidc_core_deinit_locked(core, true);
	core_unlock(core, __func__);
	return rc;
}

int msm_vidc_core_init(struct msm_vidc_core *core)
{
	int rc = 0;

	if (!core || !core->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core_lock(core, __func__);
	if (is_core_state(core, MSM_VIDC_CORE_INIT) ||
			is_core_state(core, MSM_VIDC_CORE_INIT_WAIT))
		goto unlock;

	msm_vidc_change_core_state(core, MSM_VIDC_CORE_INIT_WAIT, __func__);
	/* clear PM suspend from core sub_state */
	msm_vidc_change_core_sub_state(core, CORE_SUBSTATE_PM_SUSPEND, 0, __func__);
	msm_vidc_change_core_sub_state(core, CORE_SUBSTATE_PAGE_FAULT, 0, __func__);

	rc = venus_hfi_core_init(core);
	if (rc) {
		d_vpr_e("%s: core init failed\n", __func__);
		goto unlock;
	}

unlock:
	if (rc)
		msm_vidc_core_deinit_locked(core, true);
	core_unlock(core, __func__);
	return rc;
}

int msm_vidc_inst_timeout(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_core *core;
	struct msm_vidc_inst *instance;
	bool found;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	core_lock(core, __func__);
	/*
	 * All sessions will be removed from core list in core deinit,
	 * do not deinit core from a session which is not present in
	 * core list.
	 */
	found = false;
	list_for_each_entry(instance, &core->instances, list) {
		if (instance == inst) {
			found = true;
			break;
		}
	}
	if (!found) {
		i_vpr_e(inst,
			"%s: session not available in core list\n", __func__);
		rc = -EINVAL;
		goto unlock;
	}
	/* mark video hw unresponsive */
	msm_vidc_change_core_sub_state(core,
		0, CORE_SUBSTATE_VIDEO_UNRESPONSIVE, __func__);

	/* call core deinit for a valid instance timeout case */
	msm_vidc_core_deinit_locked(core, true);

unlock:
	core_unlock(core, __func__);

	return rc;
}

int msm_vidc_print_buffer_info(struct msm_vidc_inst *inst)
{
	struct msm_vidc_buffers *buffers;
	int i;

	if (!inst) {
		i_vpr_e(inst, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/* Print buffer details */
	for (i = 1; i < ARRAY_SIZE(buf_type_name_arr); i++) {
		buffers = msm_vidc_get_buffers(inst, i, __func__);
		if (!buffers)
			continue;

		i_vpr_h(inst, "buf: type: %11s, count %2d, extra %2d, actual %2d, size %9u\n",
			buf_name(i), buffers->min_count,
			buffers->extra_count, buffers->actual_count,
			buffers->size);
	}

	return 0;
}

int msm_vidc_print_inst_info(struct msm_vidc_inst *inst)
{
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	enum msm_vidc_port_type port;
	bool is_secure, is_decode;
	u32 bit_depth, bit_rate, frame_rate, width, height;
	struct dma_buf *dbuf;
	struct inode *f_inode;
	unsigned long inode_num = 0;
	long ref_count = -1;
	int i = 0;

	if (!inst || !inst->capabilities) {
		i_vpr_e(inst, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	is_secure = is_secure_session(inst);
	is_decode = inst->domain == MSM_VIDC_DECODER;
	port = is_decode ? INPUT_PORT : OUTPUT_PORT;
	width = inst->fmts[port].fmt.pix_mp.width;
	height = inst->fmts[port].fmt.pix_mp.height;
	bit_depth = inst->capabilities->cap[BIT_DEPTH].value & 0xFFFF;
	bit_rate = inst->capabilities->cap[BIT_RATE].value;
	frame_rate = inst->capabilities->cap[FRAME_RATE].value >> 16;

	i_vpr_e(inst, "%s %s session, HxW: %d x %d, fps: %d, bitrate: %d, bit-depth: %d\n",
		is_secure ? "Secure" : "Non-Secure",
		is_decode ? "Decode" : "Encode",
		height, width,
		frame_rate, bit_rate, bit_depth);

	/* Print buffer details */
	for (i = 1; i < ARRAY_SIZE(buf_type_name_arr); i++) {
		buffers = msm_vidc_get_buffers(inst, i, __func__);
		if (!buffers)
			continue;

		i_vpr_e(inst, "count: type: %11s, min: %2d, extra: %2d, actual: %2d\n",
			buf_name(i), buffers->min_count,
			buffers->extra_count, buffers->actual_count);

		list_for_each_entry(buf, &buffers->list, list) {
			if (!buf->dmabuf)
				continue;
			dbuf = (struct dma_buf *)buf->dmabuf;
			if (dbuf && dbuf->file) {
				f_inode = file_inode(dbuf->file);
				if (f_inode) {
					inode_num = f_inode->i_ino;
					ref_count = file_count(dbuf->file);
				}
			}
			i_vpr_e(inst,
				"buf: type: %11s, index: %2d, fd: %4d, size: %9u, off: %8u, filled: %9u, daddr: %#llx, inode: %8lu, ref: %2ld, flags: %8x, ts: %16lld, attr: %8x\n",
				buf_name(i), buf->index, buf->fd, buf->buffer_size,
				buf->data_offset, buf->data_size, buf->device_addr,
				inode_num, ref_count, buf->flags, buf->timestamp, buf->attr);
		}
	}

	return 0;
}

void msm_vidc_print_core_info(struct msm_vidc_core *core)
{
	struct msm_vidc_inst *inst = NULL;
	struct msm_vidc_inst *instances[MAX_SUPPORTED_INSTANCES];
	s32 num_instances = 0;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	core_lock(core, __func__);
	list_for_each_entry(inst, &core->instances, list)
		instances[num_instances++] = inst;
	core_unlock(core, __func__);

	while (num_instances--) {
		inst = instances[num_instances];
		inst = get_inst_ref(core, inst);
		if (!inst)
			continue;
		inst_lock(inst, __func__);
		msm_vidc_print_inst_info(inst);
		inst_unlock(inst, __func__);
		put_inst(inst);
	}
}

int msm_vidc_smmu_fault_handler(struct iommu_domain *domain,
		struct device *dev, unsigned long iova, int flags, void *data)
{
	struct msm_vidc_core *core = data;

	if (!domain || !core || !core->capabilities) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, domain, core);
		return -EINVAL;
	}

	if (is_core_sub_state(core, CORE_SUBSTATE_PAGE_FAULT)) {
		if (core->capabilities[NON_FATAL_FAULTS].value) {
			dprintk_ratelimit(VIDC_ERR, "err ",
					"%s: non-fatal pagefault address: %lx\n",
					__func__, iova);
			return 0;
		}
	}

	d_vpr_e(FMT_STRING_FAULT_HANDLER, __func__, iova);

	/* mark smmu fault as handled */
	msm_vidc_change_core_sub_state(core, 0, CORE_SUBSTATE_PAGE_FAULT, __func__);

	/* print noc error log registers */
	venus_hfi_noc_error_info(core);

	msm_vidc_print_core_info(core);
	/*
	 * Return -ENOSYS to elicit the default behaviour of smmu driver.
	 * If we return -ENOSYS, then smmu driver assumes page fault handler
	 * is not installed and prints a list of useful debug information like
	 * FAR, SID etc. This information is not printed if we return 0.
	 */
	return -ENOSYS;
}

int msm_vidc_trigger_ssr(struct msm_vidc_core *core,
		u64 trigger_ssr_val)
{
	struct msm_vidc_ssr *ssr;

	if (!core) {
		d_vpr_e("%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}
	ssr = &core->ssr;
	/*
	 * <test_addr><sub_client_id><ssr_type>
	 * ssr_type: 0-3 bits
	 * sub_client_id: 4-7 bits
	 * reserved: 8-31 bits
	 * test_addr: 32-63 bits
	 */
	ssr->ssr_type = (trigger_ssr_val &
			(unsigned long)SSR_TYPE) >> SSR_TYPE_SHIFT;
	ssr->sub_client_id = (trigger_ssr_val &
			(unsigned long)SSR_SUB_CLIENT_ID) >> SSR_SUB_CLIENT_ID_SHIFT;
	ssr->test_addr = (trigger_ssr_val &
			(unsigned long)SSR_ADDR_ID) >> SSR_ADDR_SHIFT;
	schedule_work(&core->ssr_work);
	return 0;
}

void msm_vidc_ssr_handler(struct work_struct *work)
{
	int rc;
	struct msm_vidc_core *core;
	struct msm_vidc_ssr *ssr;

	core = container_of(work, struct msm_vidc_core, ssr_work);
	if (!core) {
		d_vpr_e("%s: invalid params %pK\n", __func__, core);
		return;
	}
	ssr = &core->ssr;

	core_lock(core, __func__);
	if (is_core_state(core, MSM_VIDC_CORE_INIT)) {
		/*
		 * In current implementation, user-initiated SSR triggers
		 * a fatal error from hardware. However, there is no way
		 * to know if fatal error is due to SSR or not. Handle
		 * user SSR as non-fatal.
		 */
		rc = venus_hfi_trigger_ssr(core, ssr->ssr_type,
			ssr->sub_client_id, ssr->test_addr);
		if (rc)
			d_vpr_e("%s: trigger_ssr failed\n", __func__);
	} else {
		d_vpr_e("%s: video core not initialized\n", __func__);
	}
	core_unlock(core, __func__);
}

int msm_vidc_trigger_stability(struct msm_vidc_core *core,
		u64 trigger_stability_val)
{
	struct msm_vidc_inst *inst = NULL;
	struct msm_vidc_stability stability;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/*
	 * <payload><sub_client_id><stability_type>
	 * stability_type: 0-3 bits
	 * sub_client_id: 4-7 bits
	 * reserved: 8-31 bits
	 * payload: 32-63 bits
	 */
	memset(&stability, 0, sizeof(struct msm_vidc_stability));
	stability.stability_type = (trigger_stability_val &
			(unsigned long)STABILITY_TYPE) >> STABILITY_TYPE_SHIFT;
	stability.sub_client_id = (trigger_stability_val &
			(unsigned long)STABILITY_SUB_CLIENT_ID) >> STABILITY_SUB_CLIENT_ID_SHIFT;
	stability.value = (trigger_stability_val &
			(unsigned long)STABILITY_PAYLOAD_ID) >> STABILITY_PAYLOAD_SHIFT;

	core_lock(core, __func__);
	list_for_each_entry(inst, &core->instances, list) {
		memcpy(&inst->stability, &stability, sizeof(struct msm_vidc_stability));
		schedule_work(&inst->stability_work);
	}
	core_unlock(core, __func__);

	return 0;
}

void msm_vidc_stability_handler(struct work_struct *work)
{
	int rc;
	struct msm_vidc_inst *inst;
	struct msm_vidc_stability *stability;

	inst = container_of(work, struct msm_vidc_inst, stability_work);
	inst = get_inst_ref(g_core, inst);
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	inst_lock(inst, __func__);
	stability = &inst->stability;
	rc = venus_hfi_trigger_stability(inst, stability->stability_type,
		stability->sub_client_id, stability->value);
	if (rc)
		i_vpr_e(inst, "%s: trigger_stability failed\n", __func__);
	inst_unlock(inst, __func__);

	put_inst(inst);
}

int cancel_stability_work_sync(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: Invalid arguments\n", __func__);
		return -EINVAL;
	}
	cancel_work_sync(&inst->stability_work);

	return 0;
}

void msm_vidc_fw_unload_handler(struct work_struct *work)
{
	struct msm_vidc_core *core = NULL;
	int rc = 0;

	core = container_of(work, struct msm_vidc_core, fw_unload_work.work);
	if (!core) {
		d_vpr_e("%s: invalid work or core handle\n", __func__);
		return;
	}

	d_vpr_h("%s: deinitializing video core\n",__func__);
	rc = msm_vidc_core_deinit(core, false);
	if (rc)
		d_vpr_e("%s: Failed to deinit core\n", __func__);

}

void msm_vidc_batch_handler(struct work_struct *work)
{
	struct msm_vidc_inst *inst;
	enum msm_vidc_allow allow;
	struct msm_vidc_core *core;
	int rc = 0;

	inst = container_of(work, struct msm_vidc_inst, decode_batch.work.work);
	inst = get_inst_ref(g_core, inst);
	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	core = inst->core;
	inst_lock(inst, __func__);
	if (is_session_error(inst)) {
		i_vpr_e(inst, "%s: failled. Session error\n", __func__);
		goto exit;
	}

	if (is_core_sub_state(core, CORE_SUBSTATE_PM_SUSPEND)) {
		i_vpr_h(inst, "%s: device in pm suspend state\n", __func__);
		goto exit;
	}

	allow = msm_vidc_allow_qbuf(inst, OUTPUT_MPLANE);
	if (allow != MSM_VIDC_ALLOW) {
		i_vpr_e(inst, "%s: not allowed in state: %s\n", __func__,
			state_name(inst->state));
		goto exit;
	}

	i_vpr_h(inst, "%s: queue pending batch buffers\n", __func__);
	rc = msm_vidc_queue_deferred_buffers(inst, MSM_VIDC_BUF_OUTPUT);
	if (rc) {
		i_vpr_e(inst, "%s: batch qbufs failed\n", __func__);
		msm_vidc_change_state(inst, MSM_VIDC_ERROR, __func__);
	}

exit:
	inst_unlock(inst, __func__);
	put_inst(inst);
}

int msm_vidc_flush_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type type)
{
	int rc = 0;
	struct msm_vidc_core *core;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf, *dummy;
	enum msm_vidc_buffer_type buffer_type[2];
	int i;

	if (!inst || !inst->core) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (type == MSM_VIDC_BUF_INPUT) {
		buffer_type[0] = MSM_VIDC_BUF_INPUT_META;
		buffer_type[1] = MSM_VIDC_BUF_INPUT;
	} else if (type == MSM_VIDC_BUF_OUTPUT) {
		buffer_type[0] = MSM_VIDC_BUF_OUTPUT_META;
		buffer_type[1] = MSM_VIDC_BUF_OUTPUT;
	} else {
		i_vpr_h(inst, "%s: invalid buffer type %d\n",
			__func__, type);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(buffer_type); i++) {
		buffers = msm_vidc_get_buffers(inst, buffer_type[i], __func__);
		if (!buffers)
			return -EINVAL;

		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			if (buf->attr & MSM_VIDC_ATTR_QUEUED ||
				buf->attr & MSM_VIDC_ATTR_DEFERRED) {
				print_vidc_buffer(VIDC_HIGH, "high", "flushing buffer", inst, buf);
				if (!(buf->attr & MSM_VIDC_ATTR_BUFFER_DONE)) {
					if (is_decode_session(inst) && is_output_buffer(buf->type)) {
						if (buf->dbuf_get) {
							call_mem_op(core, dma_buf_put, inst, buf->dmabuf);
							buf->dbuf_get = 0;
						}
					}
					buf->data_size = 0;
					msm_vidc_vb2_buffer_done(inst, buf);
				}
			}
		}
	}

	return rc;
}

int msm_vidc_flush_read_only_buffers(struct msm_vidc_inst *inst,
		enum msm_vidc_buffer_type type)
{
	int rc = 0;
	struct msm_vidc_buffer *ro_buf, *dummy;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (!is_decode_session(inst) || !is_output_buffer(type))
		return 0;

	list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
		if (ro_buf->attr & MSM_VIDC_ATTR_READ_ONLY)
			continue;
		print_vidc_buffer(VIDC_ERR, "high", "flush ro buf", inst, ro_buf);
		if (ro_buf->attach && ro_buf->sg_table)
			call_mem_op(core, dma_buf_unmap_attachment, core,
				ro_buf->attach, ro_buf->sg_table);
		if (ro_buf->attach && ro_buf->dmabuf)
			call_mem_op(core, dma_buf_detach, core,
				ro_buf->dmabuf, ro_buf->attach);
		if (ro_buf->dbuf_get)
			call_mem_op(core, dma_buf_put, inst, ro_buf->dmabuf);
		ro_buf->attach = NULL;
		ro_buf->sg_table = NULL;
		ro_buf->dmabuf = NULL;
		ro_buf->dbuf_get = 0;
		ro_buf->device_addr = 0x0;
		list_del_init(&ro_buf->list);
		msm_vidc_pool_free(inst, ro_buf);
	}

	return rc;
}

void msm_vidc_destroy_buffers(struct msm_vidc_inst *inst)
{
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf, *dummy;
	struct msm_vidc_timestamp *ts, *dummy_ts;
	struct msm_memory_dmabuf *dbuf, *dummy_dbuf;
	struct msm_vidc_input_timer *timer, *dummy_timer;
	struct msm_vidc_buffer_stats *stats, *dummy_stats;
	struct msm_vidc_inst_cap_entry *entry, *dummy_entry;
	struct msm_vidc_fence *fence, *dummy_fence;
	struct msm_vidc_core *core;

	static const enum msm_vidc_buffer_type ext_buf_types[] = {
		MSM_VIDC_BUF_INPUT,
		MSM_VIDC_BUF_OUTPUT,
		MSM_VIDC_BUF_INPUT_META,
		MSM_VIDC_BUF_OUTPUT_META,
	};
	static const enum msm_vidc_buffer_type internal_buf_types[] = {
		MSM_VIDC_BUF_BIN,
		MSM_VIDC_BUF_ARP,
		MSM_VIDC_BUF_COMV,
		MSM_VIDC_BUF_NON_COMV,
		MSM_VIDC_BUF_LINE,
		MSM_VIDC_BUF_DPB,
		MSM_VIDC_BUF_PERSIST,
		MSM_VIDC_BUF_VPSS,
		MSM_VIDC_BUF_PARTIAL_DATA,
	};
	int i;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}
	core = inst->core;

	for (i = 0; i < ARRAY_SIZE(internal_buf_types); i++) {
		buffers = msm_vidc_get_buffers(inst, internal_buf_types[i], __func__);
		if (!buffers)
			continue;
		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			i_vpr_h(inst,
				"destroying internal buffer: type %d idx %d fd %d addr %#llx size %d\n",
				buf->type, buf->index, buf->fd, buf->device_addr, buf->buffer_size);
			msm_vidc_destroy_internal_buffer(inst, buf);
		}
	}

	/*
	 * read_only list does not take dma ref_count using dma_buf_get().
	 * dma_buf ptr will be obselete when its ref_count reaches zero.
	 * Hence printthe dma_buf info before releasing the ref count.
	 */
	list_for_each_entry_safe(buf, dummy, &inst->buffers.read_only.list, list) {
		print_vidc_buffer(VIDC_ERR, "err ", "destroying ro buf", inst, buf);
		if (buf->attach && buf->sg_table)
			call_mem_op(core, dma_buf_unmap_attachment, core,
				buf->attach, buf->sg_table);
		if (buf->attach && buf->dmabuf)
			call_mem_op(core, dma_buf_detach, core, buf->dmabuf, buf->attach);
		if (buf->dbuf_get)
			call_mem_op(core, dma_buf_put, inst, buf->dmabuf);
		list_del_init(&buf->list);
		msm_vidc_pool_free(inst, buf);
	}

	for (i = 0; i < ARRAY_SIZE(ext_buf_types); i++) {
		buffers = msm_vidc_get_buffers(inst, ext_buf_types[i], __func__);
		if (!buffers)
			continue;

		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			if (buf->attach && buf->sg_table)
				call_mem_op(core, dma_buf_unmap_attachment, core,
					buf->attach, buf->sg_table);
			if (buf->attach && buf->dmabuf)
				call_mem_op(core, dma_buf_detach, core, buf->dmabuf, buf->attach);
			if (buf->dbuf_get) {
				print_vidc_buffer(VIDC_ERR, "err ", "destroying: put dmabuf", inst, buf);
				call_mem_op(core, dma_buf_put, inst, buf->dmabuf);
			}
			list_del_init(&buf->list);
			msm_vidc_pool_free(inst, buf);
		}
	}

	list_for_each_entry_safe(ts, dummy_ts, &inst->timestamps.list, sort.list) {
		i_vpr_e(inst, "%s: removing ts: val %lld, rank %lld\n",
			__func__, ts->sort.val, ts->rank);
		list_del(&ts->sort.list);
		msm_vidc_pool_free(inst, ts);
	}

	list_for_each_entry_safe(ts, dummy_ts, &inst->ts_reorder.list, sort.list) {
		i_vpr_e(inst, "%s: removing reorder ts: val %lld\n",
			__func__, ts->sort.val);
		list_del(&ts->sort.list);
		msm_vidc_pool_free(inst, ts);
	}

	list_for_each_entry_safe(timer, dummy_timer, &inst->input_timer_list, list) {
		i_vpr_e(inst, "%s: removing input_timer %lld\n",
			__func__, timer->time_us);
		list_del(&timer->list);
		msm_vidc_pool_free(inst, timer);
	}

	list_for_each_entry_safe(stats, dummy_stats, &inst->buffer_stats_list, list) {
		print_buffer_stats(VIDC_ERR, "err ", inst, stats);
		list_del(&stats->list);
		msm_vidc_pool_free(inst, stats);
	}

	list_for_each_entry_safe(dbuf, dummy_dbuf, &inst->dmabuf_tracker, list) {
		struct dma_buf *dmabuf;
		struct inode *f_inode;
		unsigned long inode_num = 0;

		dmabuf = dbuf->dmabuf;
		if (dmabuf && dmabuf->file) {
			f_inode = file_inode(dmabuf->file);
			if (f_inode) {
				inode_num = f_inode->i_ino;
			}
		}
		i_vpr_e(inst, "%s: removing dma_buf %p, inode %lu, refcount %u\n",
			__func__, dbuf->dmabuf, inode_num, dbuf->refcount);
		call_mem_op(core, dma_buf_put_completely, inst, dbuf);
	}

	list_for_each_entry_safe(entry, dummy_entry, &inst->firmware_list, list) {
		i_vpr_e(inst, "%s: fw list: %s\n", __func__, cap_name(entry->cap_id));
		list_del(&entry->list);
		msm_vidc_vmem_free((void **)&entry);
	}

	list_for_each_entry_safe(entry, dummy_entry, &inst->children_list, list) {
		i_vpr_e(inst, "%s: child list: %s\n", __func__, cap_name(entry->cap_id));
		list_del(&entry->list);
		msm_vidc_vmem_free((void **)&entry);
	}

	list_for_each_entry_safe(entry, dummy_entry, &inst->caps_list, list) {
		list_del(&entry->list);
		msm_vidc_vmem_free((void **)&entry);
	}

	list_for_each_entry_safe(fence, dummy_fence, &inst->fence_list, list) {
		i_vpr_e(inst, "%s: destroying fence %s\n", __func__, fence->name);
		msm_vidc_fence_destroy(inst, (u32)fence->dma_fence.seqno);
	}

	/* destroy buffers from pool */
	msm_vidc_pools_deinit(inst);
}

static void msm_vidc_close_helper(struct kref *kref)
{
	struct msm_vidc_inst *inst = container_of(kref,
		struct msm_vidc_inst, kref);

	i_vpr_h(inst, "%s()\n", __func__);
	msm_vidc_fence_deinit(inst);
	msm_vidc_debugfs_deinit_inst(inst);
	if (is_decode_session(inst))
		msm_vdec_inst_deinit(inst);
	else if (is_encode_session(inst))
		msm_venc_inst_deinit(inst);
	msm_vidc_free_input_cr_list(inst);
	if (inst->workq)
		destroy_workqueue(inst->workq);
	msm_vidc_destroy_buffers(inst);
	msm_vidc_remove_dangling_session(inst);
	msm_vidc_try_suspend(inst);
	mutex_destroy(&inst->client_lock);
	mutex_destroy(&inst->request_lock);
	mutex_destroy(&inst->lock);
	msm_vidc_vmem_free((void **)&inst->capabilities);
	msm_vidc_vmem_free((void **)&inst);
}

struct msm_vidc_inst *get_inst_ref(struct msm_vidc_core *core,
		struct msm_vidc_inst *instance)
{
	struct msm_vidc_inst *inst = NULL;
	bool matches = false;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst == instance) {
			matches = true;
			break;
		}
	}
	inst = (matches && kref_get_unless_zero(&inst->kref)) ? inst : NULL;
	mutex_unlock(&core->lock);
	return inst;
}

struct msm_vidc_inst *get_inst(struct msm_vidc_core *core,
		u32 session_id)
{
	struct msm_vidc_inst *inst = NULL;
	bool matches = false;

	if (!core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			matches = true;
			break;
		}
	}
	inst = (matches && kref_get_unless_zero(&inst->kref)) ? inst : NULL;
	mutex_unlock(&core->lock);
	return inst;
}

void put_inst(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	kref_put(&inst->kref, msm_vidc_close_helper);
}

bool core_lock_check(struct msm_vidc_core *core, const char *func)
{
	return mutex_is_locked(&core->lock);
}

void core_lock(struct msm_vidc_core *core, const char *function)
{
	mutex_lock(&core->lock);
}

void core_unlock(struct msm_vidc_core *core, const char *function)
{
	mutex_unlock(&core->lock);
}

bool inst_lock_check(struct msm_vidc_inst *inst, const char *func)
{
	return mutex_is_locked(&inst->lock);
}

void inst_lock(struct msm_vidc_inst *inst, const char *function)
{
	mutex_lock(&inst->lock);
}

void inst_unlock(struct msm_vidc_inst *inst, const char *function)
{
	mutex_unlock(&inst->lock);
}

bool client_lock_check(struct msm_vidc_inst *inst, const char *func)
{
	return mutex_is_locked(&inst->client_lock);
}

void client_lock(struct msm_vidc_inst *inst, const char *function)
{
	mutex_lock(&inst->client_lock);
}

void client_unlock(struct msm_vidc_inst *inst, const char *function)
{
	mutex_unlock(&inst->client_lock);
}

int msm_vidc_update_bitstream_buffer_size(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;
	struct v4l2_format *fmt;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (is_decode_session(inst)) {
		fmt = &inst->fmts[INPUT_PORT];
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = call_session_op(core,
			buffer_size, inst, MSM_VIDC_BUF_INPUT);
	}

	return 0;
}

int msm_vidc_update_meta_port_settings(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;
	struct v4l2_format *fmt;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	fmt = &inst->fmts[INPUT_META_PORT];
	fmt->fmt.meta.buffersize = call_session_op(core,
		buffer_size, inst, MSM_VIDC_BUF_INPUT_META);
	inst->buffers.input_meta.min_count =
			inst->buffers.input.min_count;
	inst->buffers.input_meta.extra_count =
			inst->buffers.input.extra_count;
	inst->buffers.input_meta.actual_count =
			inst->buffers.input.actual_count;
	inst->buffers.input_meta.size = fmt->fmt.meta.buffersize;

	fmt = &inst->fmts[OUTPUT_META_PORT];
	fmt->fmt.meta.buffersize = call_session_op(core,
		buffer_size, inst, MSM_VIDC_BUF_OUTPUT_META);
	inst->buffers.output_meta.min_count =
			inst->buffers.output.min_count;
	inst->buffers.output_meta.extra_count =
			inst->buffers.output.extra_count;
	inst->buffers.output_meta.actual_count =
			inst->buffers.output.actual_count;
	inst->buffers.output_meta.size = fmt->fmt.meta.buffersize;
	return 0;
}

int msm_vidc_update_buffer_count(struct msm_vidc_inst *inst, u32 port)
{
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	switch (port) {
	case INPUT_PORT:
		inst->buffers.input.min_count = call_session_op(core,
			min_count, inst, MSM_VIDC_BUF_INPUT);
		inst->buffers.input.extra_count = call_session_op(core,
			extra_count, inst, MSM_VIDC_BUF_INPUT);
		if (inst->buffers.input.actual_count <
			inst->buffers.input.min_count +
			inst->buffers.input.extra_count) {
			inst->buffers.input.actual_count =
				inst->buffers.input.min_count +
				inst->buffers.input.extra_count;
		}
		if (is_input_meta_enabled(inst)) {
			inst->buffers.input_meta.min_count =
					inst->buffers.input.min_count;
			inst->buffers.input_meta.extra_count =
					inst->buffers.input.extra_count;
			inst->buffers.input_meta.actual_count =
					inst->buffers.input.actual_count;
		} else {
			inst->buffers.input_meta.min_count = 0;
			inst->buffers.input_meta.extra_count = 0;
			inst->buffers.input_meta.actual_count = 0;
		}
		i_vpr_h(inst, "%s: type:  INPUT, count: min %u, extra %u, actual %u\n", __func__,
			inst->buffers.input.min_count,
			inst->buffers.input.extra_count,
			inst->buffers.input.actual_count);
		break;
	case OUTPUT_PORT:
		if (!inst->bufq[INPUT_PORT].vb2q->streaming)
			inst->buffers.output.min_count = call_session_op(core,
				min_count, inst, MSM_VIDC_BUF_OUTPUT);
		inst->buffers.output.extra_count = call_session_op(core,
			extra_count, inst, MSM_VIDC_BUF_OUTPUT);
		if (inst->buffers.output.actual_count <
			inst->buffers.output.min_count +
			inst->buffers.output.extra_count) {
			inst->buffers.output.actual_count =
				inst->buffers.output.min_count +
				inst->buffers.output.extra_count;
		}
		if (is_output_meta_enabled(inst)) {
			inst->buffers.output_meta.min_count =
					inst->buffers.output.min_count;
			inst->buffers.output_meta.extra_count =
					inst->buffers.output.extra_count;
			inst->buffers.output_meta.actual_count =
					inst->buffers.output.actual_count;
		} else {
			inst->buffers.output_meta.min_count = 0;
			inst->buffers.output_meta.extra_count = 0;
			inst->buffers.output_meta.actual_count = 0;
		}
		i_vpr_h(inst, "%s: type: OUTPUT, count: min %u, extra %u, actual %u\n", __func__,
			inst->buffers.output.min_count,
			inst->buffers.output.extra_count,
			inst->buffers.output.actual_count);
		break;
	default:
		d_vpr_e("%s unknown port %d\n", __func__, port);
		return -EINVAL;
	}

	return 0;
}

void msm_vidc_schedule_core_deinit(struct msm_vidc_core *core)
{
	if (!core)
		return;

	if (!core->capabilities[FW_UNLOAD].value)
		return;

	cancel_delayed_work(&core->fw_unload_work);

	schedule_delayed_work(&core->fw_unload_work,
		msecs_to_jiffies(core->capabilities[FW_UNLOAD_DELAY].value));

	d_vpr_h("firmware unload delayed by %u ms\n",
		core->capabilities[FW_UNLOAD_DELAY].value);

	return;
}

static const char *get_codec_str(enum msm_vidc_codec_type type)
{
	switch (type) {
	case MSM_VIDC_H264: return " avc";
	case MSM_VIDC_HEVC: return "hevc";
	case MSM_VIDC_VP9:  return " vp9";
	case MSM_VIDC_AV1:  return " av1";
	case MSM_VIDC_HEIC: return "heic";
	case MSM_VIDC_MPEG2: return "mpeg2";
	}

	return "....";
}

static const char *get_domain_str(enum msm_vidc_domain_type type)
{
	switch (type) {
	case MSM_VIDC_ENCODER: return "E";
	case MSM_VIDC_DECODER: return "D";
	}

	return ".";
}

int msm_vidc_update_debug_str(struct msm_vidc_inst *inst)
{
	u32 sid;
	int client_id = INVALID_CLIENT_ID;
	const char *codec;
	const char *domain;

	if (!inst) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	if (inst->capabilities)
		client_id = inst->capabilities->cap[CLIENT_ID].value;

	sid = inst->session_id;
	codec = get_codec_str(inst->codec);
	domain = get_domain_str(inst->domain);
	if (client_id != INVALID_CLIENT_ID) {
		snprintf(inst->debug_str, sizeof(inst->debug_str), "%08x: %s%s_%d",
			sid, codec, domain, client_id);
	} else {
		snprintf(inst->debug_str, sizeof(inst->debug_str), "%08x: %s%s",
			sid, codec, domain);
	}
	d_vpr_h("%s: sid: %08x, codec: %s, domain: %s, final: %s\n",
		__func__, sid, codec, domain, inst->debug_str);

	return 0;
}

static int msm_vidc_print_insts_info(struct msm_vidc_core *core)
{
	struct msm_vidc_inst *inst;
	u32 height, width, fps, orate;
	struct msm_vidc_inst_capability *capability;
	struct v4l2_format *out_f;
	struct v4l2_format *inp_f;
	char prop[64];

	d_vpr_e("Print all running instances\n");
	d_vpr_e("%6s | %6s | %5s | %5s | %5s\n", "width", "height", "fps", "orate", "prop");

	core_lock(core, __func__);
	list_for_each_entry(inst, &core->instances, list) {
		out_f = &inst->fmts[OUTPUT_PORT];
		inp_f = &inst->fmts[INPUT_PORT];
		capability = inst->capabilities;
		memset(&prop, 0, sizeof(prop));

		width = max(out_f->fmt.pix_mp.width, inp_f->fmt.pix_mp.width);
		height = max(out_f->fmt.pix_mp.height, inp_f->fmt.pix_mp.height);
		fps = capability->cap[FRAME_RATE].value >> 16;
		orate = capability->cap[OPERATING_RATE].value >> 16;

		if (is_realtime_session(inst))
			strlcat(prop, "RT ", sizeof(prop));
		else
			strlcat(prop, "NRT", sizeof(prop));

		if (is_thumbnail_session(inst))
			strlcat(prop, "+THUMB", sizeof(prop));

		if (is_image_session(inst))
			strlcat(prop, "+IMAGE", sizeof(prop));

		i_vpr_e(inst, "%6u | %6u | %5u | %5u | %5s\n", width, height, fps, orate, prop);
	}
	core_unlock(core, __func__);

	return 0;
}

bool msm_vidc_ignore_session_load(struct msm_vidc_inst *inst) {

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (!is_realtime_session(inst) || is_thumbnail_session(inst) ||
			is_image_session(inst))
		return true;

	return false;
}

int msm_vidc_check_core_mbps(struct msm_vidc_inst *inst)
{
	u32 mbps = 0, total_mbps = 0, enc_mbps = 0;
	u32 critical_mbps = 0;
	struct msm_vidc_core *core;
	struct msm_vidc_inst *instance;

	if (!inst || !inst->core || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	/* skip mbps check for non-realtime, thumnail, image sessions */
	if (msm_vidc_ignore_session_load(inst)) {
		i_vpr_h(inst,
			"%s: skip mbps check due to NRT %d, TH %d, IMG %d\n", __func__,
			!is_realtime_session(inst), is_thumbnail_session(inst),
			is_image_session(inst));
		return 0;
	}

	core_lock(core, __func__);
	list_for_each_entry(instance, &core->instances, list) {
		if (is_critical_priority_session(instance))
			critical_mbps += msm_vidc_get_inst_load(instance);
	}
	core_unlock(core, __func__);

	if (critical_mbps > core->capabilities[MAX_MBPS].value) {
		i_vpr_e(inst, "%s: Hardware overloaded with critical sessions. needed %u, max %u",
			__func__, critical_mbps, core->capabilities[MAX_MBPS].value);
		return -ENOMEM;
	}

	core_lock(core, __func__);
	list_for_each_entry(instance, &core->instances, list) {
		/* ignore invalid/error session */
		if (is_session_error(instance))
			continue;

		/* ignore thumbnail, image, and non realtime sessions */
		if (msm_vidc_ignore_session_load(instance))
			continue;

		mbps = msm_vidc_get_inst_load(instance);
		total_mbps += mbps;
		if (is_encode_session(instance))
			enc_mbps += mbps;
	}
	core_unlock(core, __func__);

	if (is_encode_session(inst)) {
		/* reject encoder if all encoders mbps is greater than MAX_MBPS */
		if (enc_mbps > core->capabilities[MAX_MBPS].value) {
			i_vpr_e(inst, "%s: Hardware overloaded. needed %u, max %u", __func__,
				mbps, core->capabilities[MAX_MBPS].value);
			return -ENOMEM;
		}
		/*
		 * if total_mbps is greater than max_mbps then reduce all decoders
		 * priority by 1 to allow this encoder
		 */
		if (total_mbps > core->capabilities[MAX_MBPS].value) {
			core_lock(core, __func__);
			list_for_each_entry(instance, &core->instances, list) {
				/* reduce realtime decode sessions priority */
				if (is_decode_session(instance) && is_realtime_session(instance)) {
					instance->adjust_priority = RT_DEC_DOWN_PRORITY_OFFSET;
					i_vpr_h(inst, "%s: pending adjust priority by %d\n",
						__func__, instance->adjust_priority);
				}
			}
			core_unlock(core, __func__);
		}
	} else if (is_decode_session(inst)){
		if (total_mbps > core->capabilities[MAX_MBPS].value) {
			inst->adjust_priority = RT_DEC_DOWN_PRORITY_OFFSET;
			i_vpr_h(inst, "%s: pending adjust priority by %d\n",
				__func__, inst->adjust_priority);
		}
	}

	i_vpr_h(inst, "%s: HW load needed %u is within max %u", __func__,
			total_mbps, core->capabilities[MAX_MBPS].value);

	return 0;
}

int msm_vidc_check_core_mbpf(struct msm_vidc_inst *inst)
{
	u32 video_mbpf = 0, image_mbpf = 0, video_rt_mbpf = 0;
	u32 critical_mbpf = 0;
	struct msm_vidc_core *core;
	struct msm_vidc_inst *instance;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	core_lock(core, __func__);
	list_for_each_entry(instance, &core->instances, list) {
		if (is_critical_priority_session(instance))
			critical_mbpf += msm_vidc_get_mbs_per_frame(instance);
	}
	core_unlock(core, __func__);

	if (critical_mbpf > core->capabilities[MAX_MBPF].value) {
		i_vpr_e(inst, "%s: Hardware overloaded with critical sessions. needed %u, max %u",
			__func__, critical_mbpf, core->capabilities[MAX_MBPF].value);
		return -ENOMEM;
	}

	core_lock(core, __func__);
	list_for_each_entry(instance, &core->instances, list) {
		/* ignore thumbnail session */
		if (is_thumbnail_session(instance))
			continue;

		if (is_image_session(instance))
			image_mbpf += msm_vidc_get_mbs_per_frame(instance);
		else
			video_mbpf += msm_vidc_get_mbs_per_frame(instance);
	}
	core_unlock(core, __func__);

	if (video_mbpf > core->capabilities[MAX_MBPF].value) {
		i_vpr_e(inst, "%s: video overloaded. needed %u, max %u", __func__,
			video_mbpf, core->capabilities[MAX_MBPF].value);
		return -ENOMEM;
	}

	if (image_mbpf > core->capabilities[MAX_IMAGE_MBPF].value) {
		i_vpr_e(inst, "%s: image overloaded. needed %u, max %u", __func__,
			image_mbpf, core->capabilities[MAX_IMAGE_MBPF].value);
		return -ENOMEM;
	}

	core_lock(core, __func__);
	/* check real-time video sessions max limit */
	list_for_each_entry(instance, &core->instances, list) {
		if (msm_vidc_ignore_session_load(instance))
			continue;

		video_rt_mbpf += msm_vidc_get_mbs_per_frame(instance);
	}
	core_unlock(core, __func__);

	if (video_rt_mbpf > core->capabilities[MAX_RT_MBPF].value) {
		i_vpr_e(inst, "%s: real-time video overloaded. needed %u, max %u",
			__func__, video_rt_mbpf, core->capabilities[MAX_RT_MBPF].value);
		return -ENOMEM;
	}

	return 0;
}

static int msm_vidc_check_inst_mbpf(struct msm_vidc_inst *inst)
{
	u32 mbpf = 0, max_mbpf = 0;
	struct msm_vidc_inst_capability *capability;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	capability = inst->capabilities;

	if (is_secure_session(inst))
		max_mbpf = capability->cap[SECURE_MBPF].max;
	else if (is_encode_session(inst) && capability->cap[LOSSLESS].value)
		max_mbpf = capability->cap[LOSSLESS_MBPF].max;
	else
		max_mbpf = capability->cap[MBPF].max;

	/* check current session mbpf */
	mbpf = msm_vidc_get_mbs_per_frame(inst);
	if (mbpf > max_mbpf) {
		i_vpr_e(inst, "%s: session overloaded. needed %u, max %u", __func__,
			mbpf, max_mbpf);
		return -ENOMEM;
	}

	return 0;
}

u32 msm_vidc_get_max_bitrate(struct msm_vidc_inst* inst)
{
	u32 max_bitrate = 0x7fffffff;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (inst->capabilities->cap[LOWLATENCY_MODE].value)
		max_bitrate = min(max_bitrate,
			(u32)inst->capabilities->cap[LOWLATENCY_MAX_BITRATE].max);

	if (inst->capabilities->cap[ALL_INTRA].value)
		max_bitrate = min(max_bitrate,
			(u32)inst->capabilities->cap[ALLINTRA_MAX_BITRATE].max);

	if (inst->codec == MSM_VIDC_HEVC) {
		max_bitrate = min(max_bitrate,
			(u32)inst->capabilities->cap[CABAC_MAX_BITRATE].max);
	} else if (inst->codec == MSM_VIDC_H264) {
		if (inst->capabilities->cap[ENTROPY_MODE].value ==
			V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC)
			max_bitrate = min(max_bitrate,
				(u32)inst->capabilities->cap[CAVLC_MAX_BITRATE].max);
		else
			max_bitrate = min(max_bitrate,
				(u32)inst->capabilities->cap[CABAC_MAX_BITRATE].max);
	}
	if (max_bitrate == 0x7fffffff || !max_bitrate)
		max_bitrate = min(max_bitrate, (u32)inst->capabilities->cap[BIT_RATE].max);

	return max_bitrate;
}

static bool msm_vidc_allow_image_encode_session(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst_capability *capability;
	struct v4l2_format *fmt;
	u32 min_width, min_height, max_width, max_height, pix_fmt, profile;
	bool allow = false;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return false;
	}
	capability = inst->capabilities;

	if (!is_image_encode_session(inst)) {
		i_vpr_e(inst, "%s: not an image encode session\n", __func__);
		return false;
	}

	pix_fmt = capability->cap[PIX_FMTS].value;
	profile = capability->cap[PROFILE].value;

	/* is input with & height is in allowed range */
	min_width = capability->cap[FRAME_WIDTH].min;
	max_width = capability->cap[FRAME_WIDTH].max;
	min_height = capability->cap[FRAME_HEIGHT].min;
	max_height = capability->cap[FRAME_HEIGHT].max;
	fmt = &inst->fmts[INPUT_PORT];
	if (!in_range(fmt->fmt.pix_mp.width, min_width, max_width) ||
		!in_range(fmt->fmt.pix_mp.height, min_height, max_height)) {
		i_vpr_e(inst, "unsupported wxh [%u x %u], allowed [%u x %u] to [%u x %u]\n",
			fmt->fmt.pix_mp.width, fmt->fmt.pix_mp.height,
			min_width, min_height, max_width, max_height);
		allow = false;
		goto exit;
	}

	/* is linear yuv color fmt */
	allow = is_linear_yuv_colorformat(pix_fmt);
	if (!allow) {
		i_vpr_e(inst, "%s: compressed fmt: %#x\n", __func__, pix_fmt);
		goto exit;
	}

	/* is output grid dimension */
	fmt = &inst->fmts[OUTPUT_PORT];
	allow = fmt->fmt.pix_mp.width == HEIC_GRID_DIMENSION;
	allow &= fmt->fmt.pix_mp.height == HEIC_GRID_DIMENSION;
	if (!allow) {
		i_vpr_e(inst, "%s: output is not a grid dimension: %u x %u\n", __func__,
			fmt->fmt.pix_mp.width, fmt->fmt.pix_mp.height);
		goto exit;
	}

	/* is bitrate mode CQ */
	allow = capability->cap[BITRATE_MODE].value == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ;
	if (!allow) {
		i_vpr_e(inst, "%s: bitrate mode is not CQ: %#x\n", __func__,
			capability->cap[BITRATE_MODE].value);
		goto exit;
	}

	/* is all intra */
	allow = !capability->cap[GOP_SIZE].value;
	allow &= !capability->cap[B_FRAME].value;
	if (!allow) {
		i_vpr_e(inst, "%s: not all intra: gop: %u, bframe: %u\n", __func__,
			capability->cap[GOP_SIZE].value, capability->cap[B_FRAME].value);
		goto exit;
	}

	/* is time delta based rc disabled */
	allow = !capability->cap[TIME_DELTA_BASED_RC].value;
	if (!allow) {
		i_vpr_e(inst, "%s: time delta based rc not disabled: %#x\n", __func__,
			capability->cap[TIME_DELTA_BASED_RC].value);
		goto exit;
	}

	/* is frame skip mode disabled */
	allow = !capability->cap[FRAME_SKIP_MODE].value;
	if (!allow) {
		i_vpr_e(inst, "%s: frame skip mode not disabled: %#x\n", __func__,
			capability->cap[FRAME_SKIP_MODE].value);
		goto exit;
	}

exit:
	if (!allow)
		i_vpr_e(inst, "%s: current session not allowed\n", __func__);

	return allow;
}

static int msm_vidc_check_resolution_supported(struct msm_vidc_inst *inst)
{
	struct msm_vidc_inst_capability *capability;
	u32 width = 0, height = 0, min_width, min_height,
		max_width, max_height;
	bool is_interlaced = false;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	capability = inst->capabilities;

	if (is_decode_session(inst)) {
		width = inst->fmts[INPUT_PORT].fmt.pix_mp.width;
		height = inst->fmts[INPUT_PORT].fmt.pix_mp.height;
	} else if (is_encode_session(inst)) {
		width = inst->crop.width;
		height = inst->crop.height;
	}

	if (is_secure_session(inst)) {
		min_width = capability->cap[SECURE_FRAME_WIDTH].min;
		max_width = capability->cap[SECURE_FRAME_WIDTH].max;
		min_height = capability->cap[SECURE_FRAME_HEIGHT].min;
		max_height = capability->cap[SECURE_FRAME_HEIGHT].max;
	} else if (is_encode_session(inst) && capability->cap[LOSSLESS].value) {
		min_width = capability->cap[LOSSLESS_FRAME_WIDTH].min;
		max_width = capability->cap[LOSSLESS_FRAME_WIDTH].max;
		min_height = capability->cap[LOSSLESS_FRAME_HEIGHT].min;
		max_height = capability->cap[LOSSLESS_FRAME_HEIGHT].max;
	} else {
		min_width = capability->cap[FRAME_WIDTH].min;
		max_width = capability->cap[FRAME_WIDTH].max;
		min_height = capability->cap[FRAME_HEIGHT].min;
		max_height = capability->cap[FRAME_HEIGHT].max;
	}

	/* reject odd resolution session */
	if (is_encode_session(inst) &&
		(is_odd(width) || is_odd(height) ||
		is_odd(inst->compose.width) ||
		is_odd(inst->compose.height))) {
		i_vpr_e(inst, "%s: resolution is not even. wxh [%u x %u], compose [%u x %u]\n",
			__func__, width, height, inst->compose.width,
			inst->compose.height);
		return -EINVAL;
	}

	/* check if input width and height is in supported range */
	if (is_decode_session(inst) || is_encode_session(inst)) {
		if (!in_range(width, min_width, max_width) ||
			!in_range(height, min_height, max_height)) {
			i_vpr_e(inst,
				"%s: unsupported input wxh [%u x %u], allowed range: [%u x %u] to [%u x %u]\n",
				__func__, width, height, min_width,
				min_height, max_width, max_height);
			return -EINVAL;
		}
	}

	/* check interlace supported resolution */
	is_interlaced = capability->cap[CODED_FRAMES].value == CODED_FRAMES_INTERLACE;
	if (is_interlaced && (width > INTERLACE_WIDTH_MAX || height > INTERLACE_HEIGHT_MAX ||
		NUM_MBS_PER_FRAME(width, height) > INTERLACE_MB_PER_FRAME_MAX)) {
		i_vpr_e(inst, "%s: unsupported interlace wxh [%u x %u], max [%u x %u]\n",
			__func__, width, height, INTERLACE_WIDTH_MAX, INTERLACE_HEIGHT_MAX);
		return -EINVAL;
	}

	return 0;
}

static int msm_vidc_check_max_sessions(struct msm_vidc_inst *inst)
{
	u32 width = 0, height = 0;
	u32 num_1080p_sessions = 0, num_4k_sessions = 0, num_8k_sessions = 0;
	struct msm_vidc_inst *i;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	if (!core->capabilities) {
		i_vpr_e(inst, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	core_lock(core, __func__);
	list_for_each_entry(i, &core->instances, list) {
		/* skip image sessions count */
		if (is_image_session(i))
			continue;

		if (is_decode_session(i)) {
			width = i->fmts[INPUT_PORT].fmt.pix_mp.width;
			height = i->fmts[INPUT_PORT].fmt.pix_mp.height;
		} else if (is_encode_session(i)) {
			width = i->crop.width;
			height = i->crop.height;
		}

		/*
		 * one 8k session equals to 64 720p sessions in reality.
		 * So for one 8k session the number of 720p sessions will
		 * exceed max supported session count(16), hence one 8k session
		 * will be rejected as well.
		 * Therefore, treat one 8k session equal to two 4k sessions and
		 * one 4k session equal to two 1080p sessions and
		 * one 1080p session equal to two 720p sessions. This equation
		 * will make one 8k session equal to eight 720p sessions
		 * which looks good.
		 *
		 * Do not treat resolutions above 4k as 8k session instead
		 * treat (4K + half 4k) above as 8k session
		 */
		if (res_is_greater_than(width, height, 4096 + (4096 >> 1), 2176 + (2176 >> 1))) {
			num_8k_sessions += 1;
			num_4k_sessions += 2;
			num_1080p_sessions += 4;
		} else if (res_is_greater_than(width, height, 1920 + (1920 >> 1), 1088 + (1088 >> 1))) {
			num_4k_sessions += 1;
			num_1080p_sessions += 2;
		} else if (res_is_greater_than(width, height, 1280 + (1280 >> 1), 736 + (736 >> 1))) {
			num_1080p_sessions += 1;
		}
	}
	core_unlock(core, __func__);

	if (num_8k_sessions > core->capabilities[MAX_NUM_8K_SESSIONS].value) {
		i_vpr_e(inst, "%s: total 8k sessions %d, exceeded max limit %d\n",
			__func__, num_8k_sessions,
			core->capabilities[MAX_NUM_8K_SESSIONS].value);
		return -ENOMEM;
	}

	if (num_4k_sessions > core->capabilities[MAX_NUM_4K_SESSIONS].value) {
		i_vpr_e(inst, "%s: total 4K sessions %d, exceeded max limit %d\n",
			__func__, num_4k_sessions,
			core->capabilities[MAX_NUM_4K_SESSIONS].value);
		return -ENOMEM;
	}

	if (num_1080p_sessions > core->capabilities[MAX_NUM_1080P_SESSIONS].value) {
		i_vpr_e(inst, "%s: total 1080p sessions %d, exceeded max limit %d\n",
			__func__, num_1080p_sessions,
			core->capabilities[MAX_NUM_1080P_SESSIONS].value);
		return -ENOMEM;
	}

	return 0;
}

int msm_vidc_check_session_supported(struct msm_vidc_inst *inst)
{
	bool allow = false;
	int rc = 0;

	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (is_image_session(inst) && is_secure_session(inst)) {
		i_vpr_e(inst, "%s: secure image session not supported\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	rc = msm_vidc_check_core_mbps(inst);
	if (rc)
		goto exit;

	rc = msm_vidc_check_core_mbpf(inst);
	if (rc)
		goto exit;

	rc = msm_vidc_check_inst_mbpf(inst);
	if (rc)
		goto exit;

	rc = msm_vidc_check_resolution_supported(inst);
	if (rc)
		goto exit;

	/* check image capabilities */
	if (is_image_encode_session(inst)) {
		allow = msm_vidc_allow_image_encode_session(inst);
		if (!allow) {
			rc = -EINVAL;
			goto exit;
		}
	}

	rc = msm_vidc_check_max_sessions(inst);
	if (rc)
		goto exit;

exit:
	if (rc) {
		i_vpr_e(inst, "%s: current session not supported\n", __func__);
		msm_vidc_print_insts_info(inst->core);
	}

	return rc;
}

int msm_vidc_check_scaling_supported(struct msm_vidc_inst *inst)
{
	u32 iwidth, owidth, iheight, oheight, ds_factor;

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (is_image_session(inst) || is_decode_session(inst)) {
		i_vpr_h(inst, "%s: Scaling is supported for encode session only\n", __func__);
		return 0;
	}

	if (!is_scaling_enabled(inst)) {
		i_vpr_h(inst, "%s: Scaling not enabled. skip scaling check\n", __func__);
		return 0;
	}

	iwidth = inst->crop.width;
	iheight = inst->crop.height;
	owidth = inst->compose.width;
	oheight = inst->compose.height;
	ds_factor = inst->capabilities->cap[SCALE_FACTOR].value;

	/* upscaling: encoder doesnot support upscaling */
	if (owidth > iwidth || oheight > iheight) {
		i_vpr_e(inst, "%s: upscale not supported: input [%u x %u], output [%u x %u]\n",
			__func__, iwidth, iheight, owidth, oheight);
		return -EINVAL;
	}

	/* downscaling: only supported upto 1/8 of width & 1/8 of height */
	if (iwidth > owidth * ds_factor || iheight > oheight * ds_factor) {
		i_vpr_e(inst,
			"%s: unsupported ratio: input [%u x %u], output [%u x %u], ratio %u\n",
			__func__, iwidth, iheight, owidth, oheight, ds_factor);
		return -EINVAL;
	}

	return 0;
}

struct msm_vidc_fw_query_params {
	u32 hfi_prop_name;
	u32 port;
};

int msm_vidc_get_properties(struct msm_vidc_inst *inst)
{
	int rc = 0;
	int i;

	static const struct msm_vidc_fw_query_params fw_query_params[] = {
		{HFI_PROP_STAGE, HFI_PORT_NONE},
		{HFI_PROP_PIPE, HFI_PORT_NONE},
		{HFI_PROP_QUALITY_MODE, HFI_PORT_BITSTREAM}
	};

	if (!inst || !inst->capabilities) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(fw_query_params); i++) {

		if (is_decode_session(inst)) {
			if (fw_query_params[i].hfi_prop_name == HFI_PROP_QUALITY_MODE)
				continue;
		}

		i_vpr_l(inst, "%s: querying fw for property %#x\n", __func__,
				fw_query_params[i].hfi_prop_name);

		rc = venus_hfi_session_property(inst,
				fw_query_params[i].hfi_prop_name,
				(HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				HFI_HOST_FLAGS_INTR_REQUIRED |
				HFI_HOST_FLAGS_GET_PROPERTY),
				fw_query_params[i].port,
				HFI_PAYLOAD_NONE,
				NULL,
				0);
		if (rc)
			return rc;
	}

	return 0;
}

struct context_bank_info *msm_vidc_get_context_bank_for_region(
	struct msm_vidc_core *core, enum msm_vidc_buffer_region region)
{
	struct context_bank_info *cb = NULL, *match = NULL;

	if (!region || region >= MSM_VIDC_REGION_MAX) {
		d_vpr_e("Invalid region %#x\n", region);
		return NULL;
	}

	venus_hfi_for_each_context_bank(core, cb) {
		if (cb->region == region) {
			match = cb;
			break;
		}
	}
	if (!match)
		d_vpr_e("cb not found for region %#x\n", region);

	return match;
}

struct context_bank_info *msm_vidc_get_context_bank_for_device(
	struct msm_vidc_core *core, struct device *dev)
{
	struct context_bank_info *cb = NULL, *match = NULL;

	if (!core || !dev) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	venus_hfi_for_each_context_bank(core, cb) {
		if (of_device_is_compatible(dev->of_node, cb->name)) {
			match = cb;
			break;
		}
	}
	if (!match)
		d_vpr_e("cb not found for dev %s\n", dev_name(dev));

	return match;
}
