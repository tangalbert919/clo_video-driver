// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */
 /* Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved. */

#include "venus_hfi_queue.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_core.h"
#include "msm_vidc_memory.h"
#include "msm_vidc_platform.h"

static int __strict_check(struct msm_vidc_core *core, const char *function)
{
	bool fatal = !mutex_is_locked(&core->lock);

	WARN_ON(fatal);

	if (fatal)
		d_vpr_e("%s: strict check failed\n", function);

	return fatal ? -EINVAL : 0;
}

static void __set_queue_hdr_defaults(struct hfi_queue_header *q_hdr)
{
	q_hdr->qhdr_status = 0x1;
	q_hdr->qhdr_type = VIDC_IFACEQ_DFLT_QHDR;
	q_hdr->qhdr_q_size = VIDC_IFACEQ_QUEUE_SIZE / 4;
	q_hdr->qhdr_pkt_size = 0;
	q_hdr->qhdr_rx_wm = 0x1;
	q_hdr->qhdr_tx_wm = 0x1;
	q_hdr->qhdr_rx_req = 0x1;
	q_hdr->qhdr_tx_req = 0x0;
	q_hdr->qhdr_rx_irq_status = 0x0;
	q_hdr->qhdr_tx_irq_status = 0x0;
	q_hdr->qhdr_read_idx = 0x0;
	q_hdr->qhdr_write_idx = 0x0;
}

static void __dump_packet(u8 *packet, const char *function, void *qinfo)
{
	u32 c = 0, session_id, packet_size = *(u32 *)packet;
	const int row_size = 32;
	/*
	 * row must contain enough for 0xdeadbaad * 8 to be converted into
	 * "de ad ba ab " * 8 + '\0'
	 */
	char row[3 * 32];
	session_id = *((u32 *)packet + 1);

	d_vpr_t("%08x: %s: %pK\n", session_id, function, qinfo);

	for (c = 0; c * row_size < packet_size; ++c) {
		int bytes_to_read = ((c + 1) * row_size > packet_size) ?
			packet_size % row_size : row_size;
		hex_dump_to_buffer(packet + c * row_size, bytes_to_read,
				row_size, 4, row, sizeof(row), false);
		d_vpr_t("%08x: %s\n", session_id, row);
	}
}

static int __write_queue(struct msm_vidc_iface_q_info *qinfo, u8 *packet,
			 bool *rx_req_is_set)
{
	struct hfi_queue_header *queue;
	u32 packet_size_in_words, new_write_idx;
	u32 empty_space, read_idx, write_idx;
	u32 *write_ptr;

	if (!qinfo || !packet) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, qinfo, packet);
		return -EINVAL;
	} else if (!qinfo->q_array.align_virtual_addr) {
		d_vpr_e("Queues have already been freed\n");
		return -EINVAL;
	}

	queue = (struct hfi_queue_header *) qinfo->q_hdr;
	if (!queue) {
		d_vpr_e("queue not present\n");
		return -ENOENT;
	}

	if (msm_vidc_debug & VIDC_PKT)
		__dump_packet(packet, __func__, qinfo);

	// TODO: handle writing packet
	//d_vpr_e("skip writing packet\n");
	//return 0;

	packet_size_in_words = (*(u32 *)packet) >> 2;
	if (!packet_size_in_words || packet_size_in_words >
		qinfo->q_array.mem_size>>2) {
		d_vpr_e("Invalid packet size\n");
		return -ENODATA;
	}

	read_idx = queue->qhdr_read_idx;
	write_idx = queue->qhdr_write_idx;

	empty_space = (write_idx >=  read_idx) ?
		((qinfo->q_array.mem_size>>2) - (write_idx -  read_idx)) :
		(read_idx - write_idx);
	if (empty_space <= packet_size_in_words) {
		queue->qhdr_tx_req =  1;
		d_vpr_e("Insufficient size (%d) to write (%d)\n",
					  empty_space, packet_size_in_words);
		return -ENOTEMPTY;
	}

	queue->qhdr_tx_req =  0;

	new_write_idx = write_idx + packet_size_in_words;
	write_ptr = (u32 *)((qinfo->q_array.align_virtual_addr) +
			(write_idx << 2));
	if (write_ptr < (u32 *)qinfo->q_array.align_virtual_addr ||
	    write_ptr > (u32 *)(qinfo->q_array.align_virtual_addr +
	    qinfo->q_array.mem_size)) {
		d_vpr_e("Invalid write index\n");
		return -ENODATA;
	}

	if (new_write_idx < (qinfo->q_array.mem_size >> 2)) {
		memcpy(write_ptr, packet, packet_size_in_words << 2);
	} else {
		new_write_idx -= qinfo->q_array.mem_size >> 2;
		memcpy(write_ptr, packet, (packet_size_in_words -
			new_write_idx) << 2);
		memcpy((void *)qinfo->q_array.align_virtual_addr,
			packet + ((packet_size_in_words - new_write_idx) << 2),
			new_write_idx  << 2);
	}

	/*
	 * Memory barrier to make sure packet is written before updating the
	 * write index
	 */
	mb();
	queue->qhdr_write_idx = new_write_idx;
	if (rx_req_is_set)
		*rx_req_is_set = true;
	/*
	 * Memory barrier to make sure write index is updated before an
	 * interrupt is raised on venus.
	 */
	mb();
	return 0;
}

static int __read_queue(struct msm_vidc_iface_q_info *qinfo, u8 *packet,
			u32 *pb_tx_req_is_set)
{
	struct hfi_queue_header *queue;
	u32 packet_size_in_words, new_read_idx;
	u32 *read_ptr;
	u32 receive_request = 0;
	u32 read_idx, write_idx;
	int rc = 0;

	if (!qinfo || !packet || !pb_tx_req_is_set) {
		d_vpr_e("%s: invalid params %pK %pK %pK\n",
			__func__, qinfo, packet, pb_tx_req_is_set);
		return -EINVAL;
	} else if (!qinfo->q_array.align_virtual_addr) {
		d_vpr_e("Queues have already been freed\n");
		return -EINVAL;
	}

	/*
	 * Memory barrier to make sure data is valid before
	 *reading it
	 */
	mb();
	queue = (struct hfi_queue_header *) qinfo->q_hdr;

	if (!queue) {
		d_vpr_e("Queue memory is not allocated\n");
		return -ENOMEM;
	}

	/*
	 * Do not set receive request for debug queue, if set,
	 * Venus generates interrupt for debug messages even
	 * when there is no response message available.
	 * In general debug queue will not become full as it
	 * is being emptied out for every interrupt from Venus.
	 * Venus will anyway generates interrupt if it is full.
	 */
	if (queue->qhdr_type & HFI_Q_ID_CTRL_TO_HOST_MSG_Q)
		receive_request = 1;

	read_idx = queue->qhdr_read_idx;
	write_idx = queue->qhdr_write_idx;

	if (read_idx == write_idx) {
		queue->qhdr_rx_req = receive_request;
		/*
		 * mb() to ensure qhdr is updated in main memory
		 * so that venus reads the updated header values
		 */
		mb();
		*pb_tx_req_is_set = 0;
		d_vpr_l(
			"%s queue is empty, rx_req = %u, tx_req = %u, read_idx = %u\n",
			receive_request ? "message" : "debug",
			queue->qhdr_rx_req, queue->qhdr_tx_req,
			queue->qhdr_read_idx);
		return -ENODATA;
	}

	read_ptr = (u32 *)((qinfo->q_array.align_virtual_addr) +
				(read_idx << 2));
	if (read_ptr < (u32 *)qinfo->q_array.align_virtual_addr ||
	    read_ptr > (u32 *)(qinfo->q_array.align_virtual_addr +
	    qinfo->q_array.mem_size - sizeof(*read_ptr))) {
		d_vpr_e("Invalid read index\n");
		return -ENODATA;
	}

	packet_size_in_words = (*read_ptr) >> 2;
	if (!packet_size_in_words) {
		d_vpr_e("Zero packet size\n");
		return -ENODATA;
	}

	new_read_idx = read_idx + packet_size_in_words;
	if (((packet_size_in_words << 2) <= VIDC_IFACEQ_VAR_HUGE_PKT_SIZE) &&
		read_idx <= (qinfo->q_array.mem_size >> 2)) {
		if (new_read_idx < (qinfo->q_array.mem_size >> 2)) {
			memcpy(packet, read_ptr,
					packet_size_in_words << 2);
		} else {
			new_read_idx -= (qinfo->q_array.mem_size >> 2);
			memcpy(packet, read_ptr,
			(packet_size_in_words - new_read_idx) << 2);
			memcpy(packet + ((packet_size_in_words -
					new_read_idx) << 2),
					(u8 *)qinfo->q_array.align_virtual_addr,
					new_read_idx << 2);
		}
	} else {
		d_vpr_e("BAD packet received, read_idx: %#x, pkt_size: %d\n",
			read_idx, packet_size_in_words << 2);
		d_vpr_e("Dropping this packet\n");
		new_read_idx = write_idx;
		rc = -ENODATA;
	}

	queue->qhdr_rx_req = receive_request;

	queue->qhdr_read_idx = new_read_idx;
	/*
	 * mb() to ensure qhdr is updated in main memory
	 * so that venus reads the updated header values
	 */
	mb();

	*pb_tx_req_is_set = (queue->qhdr_tx_req == 1) ? 1 : 0;

	if ((msm_vidc_debug & VIDC_PKT) &&
		!(queue->qhdr_type & HFI_Q_ID_CTRL_TO_HOST_DEBUG_Q)) {
		__dump_packet(packet, __func__, qinfo);
	}

	return rc;
}

/* Writes into cmdq without raising an interrupt */
static int __iface_cmdq_write_relaxed(struct msm_vidc_core *core,
				      void *pkt, bool *requires_interrupt)
{
	struct msm_vidc_iface_q_info *q_info;
	//struct vidc_hal_cmd_pkt_hdr *cmd_packet;
	int rc = -E2BIG;

	if (!core || !pkt) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, core, pkt);
		return -EINVAL;
	}

	rc = __strict_check(core, __func__);
	if (rc)
		return rc;

	if (!core_in_valid_state(core)) {
		d_vpr_e("%s: fw not in init state\n", __func__);
		rc = -EINVAL;
		goto err_q_null;
	}

	//cmd_packet = (struct vidc_hal_cmd_pkt_hdr *)pkt;
	//core->last_packet_type = cmd_packet->packet_type;

	q_info = &core->iface_queues[VIDC_IFACEQ_CMDQ_IDX];
	if (!q_info) {
		d_vpr_e("cannot write to shared Q's\n");
		goto err_q_null;
	}

	if (!q_info->q_array.align_virtual_addr) {
		d_vpr_e("cannot write to shared CMD Q's\n");
		rc = -ENODATA;
		goto err_q_null;
	}

	if (!__write_queue(q_info, (u8 *)pkt, requires_interrupt)) {
		rc = 0;
	} else {
		d_vpr_e("queue full\n");
	}

err_q_null:
	return rc;
}

int venus_hfi_queue_cmd_write(struct msm_vidc_core *core, void *pkt)
{
	bool needs_interrupt = false;
	int rc = __iface_cmdq_write_relaxed(core, pkt, &needs_interrupt);

	if (!rc && needs_interrupt)
		call_venus_op(core, raise_interrupt, core);

	return rc;
}

int venus_hfi_queue_cmd_write_intr(struct msm_vidc_core *core, void *pkt,
				   bool allow_intr)
{
	bool needs_interrupt = false;
	int rc = __iface_cmdq_write_relaxed(core, pkt, &needs_interrupt);

	if (!rc && allow_intr && needs_interrupt)
		call_venus_op(core, raise_interrupt, core);

	return rc;
}

int venus_hfi_queue_msg_read(struct msm_vidc_core *core, void *pkt)
{
	u32 tx_req_is_set = 0;
	int rc = 0;
	struct msm_vidc_iface_q_info *q_info;

	if (!pkt) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (!core_in_valid_state(core)) {
		d_vpr_e("%s: fw not in init state\n", __func__);
		rc = -EINVAL;
		goto read_error_null;
	}

	q_info = &core->iface_queues[VIDC_IFACEQ_MSGQ_IDX];
	if (!q_info->q_array.align_virtual_addr) {
		d_vpr_e("cannot read from shared MSG Q's\n");
		rc = -ENODATA;
		goto read_error_null;
	}

	if (!__read_queue(q_info, (u8 *)pkt, &tx_req_is_set)) {
		if (tx_req_is_set) {
			//call_venus_op(core, raise_interrupt, core);
			d_vpr_e("%s: queue is full\n", __func__);
			rc = -EINVAL;
			goto read_error_null;
		}
		rc = 0;
	} else {
		rc = -ENODATA;
	}

read_error_null:
	return rc;
}

int venus_hfi_queue_dbg_read(struct msm_vidc_core *core, void *pkt)
{
	u32 tx_req_is_set = 0;
	int rc = 0;
	struct msm_vidc_iface_q_info *q_info;

	if (!pkt) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	q_info = &core->iface_queues[VIDC_IFACEQ_DBGQ_IDX];
	if (!q_info->q_array.align_virtual_addr) {
		d_vpr_e("cannot read from shared DBG Q's\n");
		rc = -ENODATA;
		goto dbg_error_null;
	}

	if (!__read_queue(q_info, (u8 *)pkt, &tx_req_is_set)) {
		if (tx_req_is_set) {
			d_vpr_e("%s: queue is full\n", __func__);
			//call_venus_op(core, raise_interrupt, core);
			rc = -EINVAL;
			goto dbg_error_null;
		}
		rc = 0;
	} else {
		rc = -ENODATA;
	}

dbg_error_null:
	return rc;
}

void venus_hfi_queue_deinit(struct msm_vidc_core *core)
{
	int i;

	d_vpr_h("%s()\n", __func__);

	if (!core->iface_q_table.align_virtual_addr) {
		d_vpr_h("%s: queues already deallocated\n", __func__);
		return;
	}

	call_mem_op(core, memory_unmap, core, &core->iface_q_table.map);
	call_mem_op(core, memory_free, core, &core->iface_q_table.alloc);
	call_mem_op(core, memory_unmap, core, &core->sfr.map);
	call_mem_op(core, memory_free, core, &core->sfr.alloc);

	for (i = 0; i < VIDC_IFACEQ_NUMQ; i++) {
		core->iface_queues[i].q_hdr = NULL;
		core->iface_queues[i].q_array.align_virtual_addr = NULL;
		core->iface_queues[i].q_array.align_device_addr = 0;
	}

	core->iface_q_table.align_virtual_addr = NULL;
	core->iface_q_table.align_device_addr = 0;

	core->sfr.align_virtual_addr = NULL;
	core->sfr.align_device_addr = 0;
}

int venus_hfi_reset_queue_header(struct msm_vidc_core *core)
{
	struct msm_vidc_iface_q_info *iface_q;
	struct hfi_queue_header *q_hdr;
	int i, rc = 0;

	if (!core) {
		d_vpr_e("%s: invalid param\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < VIDC_IFACEQ_NUMQ; i++) {
		iface_q = &core->iface_queues[i];
		__set_queue_hdr_defaults(iface_q->q_hdr);
	}

	iface_q = &core->iface_queues[VIDC_IFACEQ_CMDQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_HOST_TO_CTRL_CMD_Q;

	iface_q = &core->iface_queues[VIDC_IFACEQ_MSGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_MSG_Q;

	iface_q = &core->iface_queues[VIDC_IFACEQ_DBGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_DEBUG_Q;
	/*
	 * Set receive request to zero on debug queue as there is no
	 * need of interrupt from video hardware for debug messages
	 */
	q_hdr->qhdr_rx_req = 0;

	return rc;
}

int venus_hfi_queue_init(struct msm_vidc_core *core)
{
	int rc = 0;
	struct hfi_queue_table_header *q_tbl_hdr;
	struct hfi_queue_header *q_hdr;
	struct msm_vidc_iface_q_info *iface_q;
	struct msm_vidc_alloc alloc;
	struct msm_vidc_map map;
	int offset = 0;
	u32 i;

	d_vpr_h("%s()\n", __func__);

	if (core->iface_q_table.align_virtual_addr) {
		d_vpr_h("%s: queues already allocated\n", __func__);
		venus_hfi_reset_queue_header(core);
		return 0;
	}

	memset(&alloc, 0, sizeof(alloc));
	alloc.type = MSM_VIDC_BUF_QUEUE;
	alloc.region = MSM_VIDC_NON_SECURE;
	alloc.size = TOTAL_QSIZE;
	alloc.secure = false;
	alloc.map_kernel = true;
	rc = call_mem_op(core, memory_alloc, core, &alloc);
	if (rc) {
		d_vpr_e("%s: alloc failed\n", __func__);
		goto fail_alloc_queue;
	}
	core->iface_q_table.align_virtual_addr = alloc.kvaddr;
	core->iface_q_table.alloc = alloc;

	memset(&map, 0, sizeof(map));
	map.type = alloc.type;
	map.region = alloc.region;
	map.dmabuf = alloc.dmabuf;
	rc = call_mem_op(core, memory_map, core, &map);
	if (rc) {
		d_vpr_e("%s: alloc failed\n", __func__);
		goto fail_alloc_queue;
	}
	core->iface_q_table.align_device_addr = map.device_addr;
	core->iface_q_table.map = map;

	core->iface_q_table.mem_size = VIDC_IFACEQ_TABLE_SIZE;
	offset += core->iface_q_table.mem_size;

	for (i = 0; i < VIDC_IFACEQ_NUMQ; i++) {
		iface_q = &core->iface_queues[i];
		iface_q->q_array.align_device_addr = map.device_addr + offset;
		iface_q->q_array.align_virtual_addr = (void*)((char*)alloc.kvaddr + offset);
		iface_q->q_array.mem_size = VIDC_IFACEQ_QUEUE_SIZE;
		offset += iface_q->q_array.mem_size;
		iface_q->q_hdr = VIDC_IFACEQ_GET_QHDR_START_ADDR(
				core->iface_q_table.align_virtual_addr, i);
		__set_queue_hdr_defaults(iface_q->q_hdr);
	}

	q_tbl_hdr = (struct hfi_queue_table_header *)
			core->iface_q_table.align_virtual_addr;
	q_tbl_hdr->qtbl_version = 0;
	q_tbl_hdr->device_addr = (void *)core;
	strlcpy(q_tbl_hdr->name, "msm_v4l2_vidc", sizeof(q_tbl_hdr->name));
	q_tbl_hdr->qtbl_size = VIDC_IFACEQ_TABLE_SIZE;
	q_tbl_hdr->qtbl_qhdr0_offset = sizeof(struct hfi_queue_table_header);
	q_tbl_hdr->qtbl_qhdr_size = sizeof(struct hfi_queue_header);
	q_tbl_hdr->qtbl_num_q = VIDC_IFACEQ_NUMQ;
	q_tbl_hdr->qtbl_num_active_q = VIDC_IFACEQ_NUMQ;

	iface_q = &core->iface_queues[VIDC_IFACEQ_CMDQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_HOST_TO_CTRL_CMD_Q;

	iface_q = &core->iface_queues[VIDC_IFACEQ_MSGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_MSG_Q;

	iface_q = &core->iface_queues[VIDC_IFACEQ_DBGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_DEBUG_Q;
	/*
	 * Set receive request to zero on debug queue as there is no
	 * need of interrupt from video hardware for debug messages
	 */
	q_hdr->qhdr_rx_req = 0;

	/* sfr buffer */
	memset(&alloc, 0, sizeof(alloc));
	alloc.type = MSM_VIDC_BUF_QUEUE;
	alloc.region = MSM_VIDC_NON_SECURE;
	alloc.size = ALIGNED_SFR_SIZE;
	alloc.secure = false;
	alloc.map_kernel = true;
	rc = call_mem_op(core, memory_alloc, core, &alloc);
	if (rc) {
		d_vpr_e("%s: sfr alloc failed\n", __func__);
		goto fail_alloc_queue;
	}
	core->sfr.align_virtual_addr = alloc.kvaddr;
	core->sfr.alloc = alloc;

	memset(&map, 0, sizeof(map));
	map.type = alloc.type;
	map.region = alloc.region;
	map.dmabuf = alloc.dmabuf;
	rc = call_mem_op(core, memory_map, core, &map);
	if (rc) {
		d_vpr_e("%s: sfr map failed\n", __func__);
		goto fail_alloc_queue;
	}
	core->sfr.align_device_addr = map.device_addr;
	core->sfr.map = map;

	core->sfr.mem_size = ALIGNED_SFR_SIZE;
	/* write sfr buffer size in first word */
	*((u32 *)core->sfr.align_virtual_addr) = core->sfr.mem_size;

	return 0;
fail_alloc_queue:
	return -ENOMEM;
}
