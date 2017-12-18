/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2000-2017
 *					All rights reserved
 *
 *  This file is part of GPAC / AAC ADTS write filter
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <gpac/bitstream.h>

#ifndef GPAC_DISABLE_AV_PARSERS
#include <gpac/avparse.h>
#endif


typedef struct
{
	//opts
	u32 exporter, mpeg2;

	//only one input pid declared
	GF_FilterPid *ipid;
	//only one output pid declared
	GF_FilterPid *opid;

	u32 codecid, channels, sr_idx, aac_type;
	Bool first;

	GF_BitStream *bs_w;
} GF_ADTSMxCtx;




GF_Err adtsmx_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	u32 i, sr, bps;
	const GF_PropertyValue *p;
#ifndef GPAC_DISABLE_AV_PARSERS
	GF_M4ADecSpecInfo acfg;
#endif
	GF_ADTSMxCtx *ctx = gf_filter_get_udta(filter);

	if (is_remove) {
		ctx->ipid = NULL;
		gf_filter_pid_remove(ctx->opid);
		return GF_OK;
	}
	if (! gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!p) return GF_NOT_SUPPORTED;
	ctx->codecid = p->value.uint;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_SAMPLE_RATE);
	if (!p) return GF_NOT_SUPPORTED;
	sr = p->value.uint;
	for (i=0; i<16; i++) {
		if (GF_M4ASampleRates[i] == (u32) sr) {
			ctx->sr_idx = i;
			break;
		}
	}

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_NUM_CHANNELS);
	if (!p) return GF_NOT_SUPPORTED;
	ctx->channels = p->value.uint;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_BPS);
	bps = p ? p->value.uint : 16;

	ctx->aac_type = 0;
	if (ctx->codecid==GF_CODECID_AAC_MPEG4) {
		if (!ctx->mpeg2) {
#ifndef GPAC_DISABLE_AV_PARSERS
			memset(&acfg, 0, sizeof(GF_M4ADecSpecInfo));
			p = gf_filter_pid_get_property(pid, GF_PROP_PID_DECODER_CONFIG);
			if (!p) return GF_NOT_SUPPORTED;
			gf_m4a_get_config(p->value.data.ptr, p->value.data.size, &acfg);
			ctx->aac_type = acfg.base_object_type - 1;
		}
#endif
	} else {
		ctx->aac_type = ctx->codecid - GF_CODECID_AAC_MPEG2_MP;
	}

	if (!ctx->opid) {
		ctx->opid = gf_filter_pid_new(filter);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_FILE) );
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_FILE_EXT, &PROP_STRING("aac") );
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_MIME, &PROP_STRING("audio/aac") );
		ctx->first = GF_TRUE;

		if (ctx->exporter) {
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("Exporting MPEG-%d AAC - SampleRate %d %d channels %d bits per sample\n", sr, ctx->channels, bps));
		}

	}
	ctx->ipid = pid;

	return GF_OK;
}



GF_Err adtsmx_process(GF_Filter *filter)
{
	GF_ADTSMxCtx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	char *data, *output;
	u32 pck_size, size;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck) {
		if (gf_filter_pid_is_eos(ctx->ipid)) {
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}

	data = (char *) gf_filter_pck_get_data(pck, &pck_size);

	size = pck_size+7;
	dst_pck = gf_filter_pck_new_alloc(ctx->opid, size, &output);

	if (!ctx->bs_w) ctx->bs_w = gf_bs_new(output, size, GF_BITSTREAM_WRITE);
	else gf_bs_reassign_buffer(ctx->bs_w, output, size);

	gf_bs_write_int(ctx->bs_w, 0xFFF, 12);/*sync*/
	gf_bs_write_int(ctx->bs_w, (ctx->mpeg2==1) ? 1 : 0, 1);/*mpeg2 aac*/
	gf_bs_write_int(ctx->bs_w, 0, 2); /*layer*/
	gf_bs_write_int(ctx->bs_w, 1, 1); /* protection_absent*/
	gf_bs_write_int(ctx->bs_w, ctx->aac_type, 2);
	gf_bs_write_int(ctx->bs_w, ctx->sr_idx, 4);
	gf_bs_write_int(ctx->bs_w, 0, 1);
	gf_bs_write_int(ctx->bs_w, ctx->channels, 3);
	gf_bs_write_int(ctx->bs_w, 0, 4);
	gf_bs_write_int(ctx->bs_w, 7+pck_size, 13);
	gf_bs_write_int(ctx->bs_w, 0x7FF, 11);
	gf_bs_write_int(ctx->bs_w, 0, 2);
	memcpy(output+7, data, pck_size);
	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_byte_offset(dst_pck, GF_FILTER_NO_BO);


	gf_filter_pck_set_framing(dst_pck, ctx->first, GF_FALSE);
	ctx->first = GF_FALSE;

	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);

	return GF_OK;
}

static void adtsmx_finalize(GF_Filter *filter)
{
	GF_ADTSMxCtx *ctx = gf_filter_get_udta(filter);
	if (ctx->bs_w) gf_bs_del(ctx->bs_w);
}

static const GF_FilterCapability ADTSMxInputs[] =
{
	CAP_INC_UINT(GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
	CAP_INC_UINT(GF_PROP_PID_CODECID, GF_CODECID_AAC_MPEG4),
	CAP_INC_UINT(GF_PROP_PID_CODECID, GF_CODECID_AAC_MPEG2_MP),
	CAP_INC_UINT(GF_PROP_PID_CODECID, GF_CODECID_AAC_MPEG2_LCP),
	CAP_INC_UINT(GF_PROP_PID_CODECID, GF_CODECID_AAC_MPEG2_SSRP),

	CAP_EXC_BOOL(GF_PROP_PID_UNFRAMED, GF_TRUE),
};


static const GF_FilterCapability ADTSMxOutputs[] =
{
	CAP_INC_UINT(GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
	{}
};

#define OFFS(_n)	#_n, offsetof(GF_ADTSMxCtx, _n)
static const GF_FilterArgs ADTSMxArgs[] =
{
	{ OFFS(exporter), "compatibility with old exporter, displays export results", GF_PROP_BOOL, "false", NULL, GF_FALSE},
	{ OFFS(mpeg2), "Signals as MPEG2 AAC", GF_PROP_UINT, "auto", "auto|no|yes", GF_FALSE},
	{}
};


GF_FilterRegister ADTSMxRegister = {
	.name = "write_adts",
	.description = "ADTS Mux",
	.private_size = sizeof(GF_ADTSMxCtx),
	.args = ADTSMxArgs,
	.finalize = adtsmx_finalize,
	INCAPS(ADTSMxInputs),
	OUTCAPS(ADTSMxOutputs),
	.configure_pid = adtsmx_configure_pid,
	.process = adtsmx_process
};


const GF_FilterRegister *adtsmx_register(GF_FilterSession *session)
{
	return &ADTSMxRegister;
}
