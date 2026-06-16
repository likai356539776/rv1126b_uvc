#include "mpp_jpeg.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>

extern "C" {
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/rk_mpi_cmd.h>
}

/*
 * RGA im2d headers have C/C++ linkage conflicts when compiled as C++.
 * Include only the type/format headers and forward-declare the functions
 * we actually use.
 */
#include <rga/im2d_type.h>
#include <rga/rga.h>

extern "C" {
rga_buffer_t wrapbuffer_virtualaddr_t(void *vir_addr, int width, int height,
                                      int wstride, int hstride, int format);
rga_buffer_t wrapbuffer_fd_t(int fd, int width, int height,
                             int wstride, int hstride, int format);
IM_STATUS imresize_t(const rga_buffer_t src, rga_buffer_t dst,
                     double fx, double fy, int interpolation, int sync);
IM_STATUS imcvtcolor_t(rga_buffer_t src, rga_buffer_t dst,
                       int sfmt, int dfmt, int mode, int sync);
const char *imStrError_t(IM_STATUS status);
}

#define ALIGN16(x) (((x) + 15) & ~15)
#define ALIGN2(x) (((x) + 1) & ~1)

#define HW_LOG(fmt, ...) fprintf(stderr, "[pip_hw] " fmt "\n", ##__VA_ARGS__)

struct PipHwContext {
	int canvas_w;
	int canvas_h;
	int hor_stride;
	int ver_stride;
	int quality;

	MppCtx dec_ctx;
	MppApi *dec_mpi;

	MppCtx enc_ctx;
	MppApi *enc_mpi;
	MppEncCfg enc_cfg;

	MppBufferGroup buf_grp;
	MppBuffer canvas_buf;
	MppBuffer pkt_buf;
	MppBuffer dec_input_buf;
	size_t dec_input_buf_size;

	bool dec_info_change_done;
	MppBufferGroup dec_frm_grp;
};

static bool init_decoder(PipHwContext *c) {
	MPP_RET ret;

	ret = mpp_create(&c->dec_ctx, &c->dec_mpi);
	if (ret != MPP_OK) {
		HW_LOG("mpp_create(dec) failed: %d", ret);
		return false;
	}

	ret = c->dec_mpi->control(c->dec_ctx, MPP_SET_DISABLE_THREAD, NULL);
	if (ret != MPP_OK)
		HW_LOG("MPP_SET_DISABLE_THREAD warning: %d", ret);

	ret = mpp_init(c->dec_ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
	if (ret != MPP_OK) {
		HW_LOG("mpp_init(dec MJPEG) failed: %d", ret);
		mpp_destroy(c->dec_ctx);
		c->dec_ctx = NULL;
		return false;
	}

	MppFrameFormat fmt = MPP_FMT_YUV420SP;
	ret = c->dec_mpi->control(c->dec_ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt);
	if (ret != MPP_OK)
		HW_LOG("MPP_DEC_SET_OUTPUT_FORMAT warning: %d", ret);

	return true;
}

static bool init_encoder(PipHwContext *c) {
	MPP_RET ret;

	ret = mpp_create(&c->enc_ctx, &c->enc_mpi);
	if (ret != MPP_OK) {
		HW_LOG("mpp_create(enc) failed: %d", ret);
		return false;
	}

	RK_S64 timeout = 500;
	c->enc_mpi->control(c->enc_ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);

	ret = mpp_init(c->enc_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
	if (ret != MPP_OK) {
		HW_LOG("mpp_init(enc MJPEG) failed: %d", ret);
		mpp_destroy(c->enc_ctx);
		c->enc_ctx = NULL;
		return false;
	}

	ret = mpp_enc_cfg_init(&c->enc_cfg);
	if (ret != MPP_OK) {
		HW_LOG("mpp_enc_cfg_init failed: %d", ret);
		return false;
	}

	ret = c->enc_mpi->control(c->enc_ctx, MPP_ENC_GET_CFG, c->enc_cfg);
	if (ret != MPP_OK) {
		HW_LOG("MPP_ENC_GET_CFG failed: %d", ret);
		return false;
	}

	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:width", c->canvas_w);
	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:height", c->canvas_h);
	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:hor_stride", c->hor_stride);
	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:ver_stride", c->ver_stride);
	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:format", MPP_FMT_YUV420SP);
	mpp_enc_cfg_set_s32(c->enc_cfg, "prep:range", MPP_FRAME_RANGE_JPEG);

	/*
	 * MJPEG should run in FIXQP mode; otherwise some MPP versions may still
	 * enter rc_model_v2 path and hit alloc_bits assertion under burst load.
	 */
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:mode", MPP_ENC_RC_MODE_FIXQP);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_in_flex", 0);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_in_num", 30);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_in_denom", 1);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_out_flex", 0);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_out_num", 30);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:fps_out_denom", 1);
	mpp_enc_cfg_set_s32(c->enc_cfg, "rc:gop", 60);

	mpp_enc_cfg_set_s32(c->enc_cfg, "codec:type", MPP_VIDEO_CodingMJPEG);
	mpp_enc_cfg_set_s32(c->enc_cfg, "jpeg:q_factor", c->quality);
	mpp_enc_cfg_set_s32(c->enc_cfg, "jpeg:qf_max", 99);
	mpp_enc_cfg_set_s32(c->enc_cfg, "jpeg:qf_min", 1);

	ret = c->enc_mpi->control(c->enc_ctx, MPP_ENC_SET_CFG, c->enc_cfg);
	if (ret != MPP_OK) {
		HW_LOG("MPP_ENC_SET_CFG failed: %d", ret);
		return false;
	}

	return true;
}

bool pip_hw_init(PipHwContext **out, int canvas_w, int canvas_h, int quality) {
	if (!out || canvas_w <= 0 || canvas_h <= 0)
		return false;

	canvas_w = ALIGN2(canvas_w);
	canvas_h = ALIGN2(canvas_h);
	if (quality < 1) quality = 1;
	if (quality > 99) quality = 99;

	PipHwContext *c = new (std::nothrow) PipHwContext();
	if (!c)
		return false;
	memset(c, 0, sizeof(*c));

	c->canvas_w = canvas_w;
	c->canvas_h = canvas_h;
	c->hor_stride = ALIGN16(canvas_w);
	c->ver_stride = ALIGN16(canvas_h);
	c->quality = quality;

	if (!init_decoder(c)) {
		delete c;
		return false;
	}

	if (!init_encoder(c)) {
		if (c->dec_ctx) mpp_destroy(c->dec_ctx);
		delete c;
		return false;
	}

	MPP_RET ret = mpp_buffer_group_get_internal(
		&c->buf_grp,
		MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
	if (ret != MPP_OK) {
		HW_LOG("mpp_buffer_group_get_internal failed: %d", ret);
		pip_hw_deinit(c);
		return false;
	}

	size_t canvas_size = static_cast<size_t>(c->hor_stride) *
	                     static_cast<size_t>(c->ver_stride) * 3 / 2;
	ret = mpp_buffer_get(c->buf_grp, &c->canvas_buf, canvas_size);
	if (ret != MPP_OK) {
		HW_LOG("mpp_buffer_get(canvas) failed: %d", ret);
		pip_hw_deinit(c);
		return false;
	}

	size_t pkt_size = static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h);
	ret = mpp_buffer_get(c->buf_grp, &c->pkt_buf, pkt_size);
	if (ret != MPP_OK) {
		HW_LOG("mpp_buffer_get(pkt) failed: %d", ret);
		pip_hw_deinit(c);
		return false;
	}

	c->dec_input_buf_size = pkt_size;
	ret = mpp_buffer_get(c->buf_grp, &c->dec_input_buf, c->dec_input_buf_size);
	if (ret != MPP_OK) {
		HW_LOG("mpp_buffer_get(dec_input) failed: %d", ret);
		pip_hw_deinit(c);
		return false;
	}

	HW_LOG("init ok: canvas=%dx%d stride=%dx%d q=%d",
	       c->canvas_w, c->canvas_h, c->hor_stride, c->ver_stride, c->quality);

	*out = c;
	return true;
}

void pip_hw_deinit(PipHwContext *c) {
	if (!c) return;

	if (c->enc_cfg) {
		mpp_enc_cfg_deinit(c->enc_cfg);
		c->enc_cfg = NULL;
	}
	if (c->enc_ctx) {
		c->enc_mpi->reset(c->enc_ctx);
		mpp_destroy(c->enc_ctx);
		c->enc_ctx = NULL;
	}
	if (c->dec_ctx) {
		c->dec_mpi->reset(c->dec_ctx);
		mpp_destroy(c->dec_ctx);
		c->dec_ctx = NULL;
	}
	if (c->canvas_buf) {
		mpp_buffer_put(c->canvas_buf);
		c->canvas_buf = NULL;
	}
	if (c->pkt_buf) {
		mpp_buffer_put(c->pkt_buf);
		c->pkt_buf = NULL;
	}
	if (c->dec_input_buf) {
		mpp_buffer_put(c->dec_input_buf);
		c->dec_input_buf = NULL;
	}
	if (c->dec_frm_grp) {
		mpp_buffer_group_put(c->dec_frm_grp);
		c->dec_frm_grp = NULL;
	}
	if (c->buf_grp) {
		mpp_buffer_group_put(c->buf_grp);
		c->buf_grp = NULL;
	}

	HW_LOG("deinit done");
	delete c;
}

static bool decode_jpeg(PipHwContext *c,
                        const uint8_t *jpeg_data, size_t jpeg_len,
                        MppFrame *out_frame) {
	MPP_RET ret;
	MppPacket pkt = NULL;

	if (jpeg_len > c->dec_input_buf_size) {
		HW_LOG("jpeg_len %zu exceeds dec_input_buf_size %zu",
		       jpeg_len, c->dec_input_buf_size);
		return false;
	}

	void *input_ptr = mpp_buffer_get_ptr(c->dec_input_buf);
	memcpy(input_ptr, jpeg_data, jpeg_len);

	ret = mpp_packet_init(&pkt, input_ptr, jpeg_len);
	if (ret != MPP_OK) {
		HW_LOG("mpp_packet_init failed: %d", ret);
		return false;
	}
	mpp_packet_set_buffer(pkt, c->dec_input_buf);

	int max_tries = 12;
	for (int attempt = 0; attempt < max_tries; attempt++) {
		MppFrame frame = NULL;

		ret = c->dec_mpi->decode(c->dec_ctx, pkt, &frame);
		if (ret != MPP_OK) {
			HW_LOG("decode() failed: %d (attempt %d)", ret, attempt);
			if (frame) mpp_frame_deinit(&frame);
			break;
		}

		if (!frame)
			continue;

		if (mpp_frame_get_info_change(frame)) {
			RK_U32 width = mpp_frame_get_width(frame);
			RK_U32 height = mpp_frame_get_height(frame);
			RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
			RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
			RK_U32 buf_size = mpp_frame_get_buf_size(frame);

			HW_LOG("dec info_change: %ux%u stride %ux%u buf_size %u",
			       width, height, hor_stride, ver_stride, buf_size);

			/*
			 * MJPEG decoder info_change: mpp_frame_get_buf_size can be smaller
			 * than the internal buf_slot pool (observed RV1126: 3760128 vs
			 * mpp_buf_slot size_total 4177920 = +102 * 4KiB pages). Under-size
			 * limit_config triggers mismatch / unstable decode when PiP loads
			 * the pipeline.
			 */
			const uint64_t page = 4096u;
			const uint64_t extra_pages = 102u;
			uint64_t need = static_cast<uint64_t>(buf_size);
			const uint64_t min_nv12 = static_cast<uint64_t>(hor_stride)
				* static_cast<uint64_t>(ver_stride) * 3u / 2u;
			if (min_nv12 > need)
				need = min_nv12;
			uint64_t pages = (need + page - 1u) / page + extra_pages;
			const size_t lim = static_cast<size_t>(pages * page);

			HW_LOG("dec buffer pool limit_config size %zu (buf_size %u)",
			       lim, buf_size);

			if (c->dec_frm_grp) {
				mpp_buffer_group_put(c->dec_frm_grp);
				c->dec_frm_grp = NULL;
			}

			ret = mpp_buffer_group_get_internal(
				&c->dec_frm_grp,
				MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
			if (ret != MPP_OK) {
				HW_LOG("dec buffer group alloc failed: %d", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = mpp_buffer_group_limit_config(c->dec_frm_grp, lim, 24);
			if (ret != MPP_OK) {
				HW_LOG("dec buffer group limit_config failed: %d", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = c->dec_mpi->control(c->dec_ctx,
			                          MPP_DEC_SET_EXT_BUF_GROUP,
			                          c->dec_frm_grp);
			if (ret != MPP_OK) {
				HW_LOG("MPP_DEC_SET_EXT_BUF_GROUP failed: %d", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = c->dec_mpi->control(c->dec_ctx,
			                          MPP_DEC_SET_INFO_CHANGE_READY,
			                          NULL);
			if (ret != MPP_OK) {
				HW_LOG("MPP_DEC_SET_INFO_CHANGE_READY failed: %d", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			c->dec_info_change_done = true;
			mpp_frame_deinit(&frame);

			mpp_packet_set_pos(pkt, input_ptr);
			mpp_packet_set_length(pkt, jpeg_len);
			continue;
		}

		RK_U32 err = mpp_frame_get_errinfo(frame);
		if (err) {
			HW_LOG("decode frame error: 0x%x", err);
			mpp_frame_deinit(&frame);
			mpp_packet_deinit(&pkt);
			return false;
		}

		mpp_packet_deinit(&pkt);
		*out_frame = frame;
		return true;
	}

	if (pkt) mpp_packet_deinit(&pkt);
	return false;
}

bool pip_hw_nv12_resize_virtual(const uint8_t *src_nv12, int src_w, int src_h, uint8_t *dst_nv12, int dst_w,
                                int dst_h)
{
	if (!src_nv12 || !dst_nv12)
		return false;
	src_w = ALIGN2(src_w);
	src_h = ALIGN2(src_h);
	dst_w = ALIGN2(dst_w);
	dst_h = ALIGN2(dst_h);
	if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
		return false;

	rga_buffer_t src = wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(src_nv12), src_w, src_h, src_w, src_h,
	                                              RK_FORMAT_YCbCr_420_SP);
	rga_buffer_t dst =
	    wrapbuffer_virtualaddr_t(dst_nv12, dst_w, dst_h, dst_w, dst_h, RK_FORMAT_YCbCr_420_SP);

	IM_STATUS st = imresize_t(src, dst, 0, 0, 0, 1);
	if (st != IM_STATUS_SUCCESS) {
		HW_LOG("pip_hw_nv12_resize_virtual failed: %s", imStrError_t(st));
		return false;
	}
	return true;
}

[[maybe_unused]] static bool rga_resize_nv12(int src_fd, int src_w, int src_h, int src_hstride, int src_vstride,
                            int dst_fd, int dst_w, int dst_h, int dst_hstride, int dst_vstride) {
	rga_buffer_t src = wrapbuffer_fd_t(src_fd, src_w, src_h,
	                                   src_hstride, src_vstride,
	                                   RK_FORMAT_YCbCr_420_SP);
	rga_buffer_t dst = wrapbuffer_fd_t(dst_fd, dst_w, dst_h,
	                                   dst_hstride, dst_vstride,
	                                   RK_FORMAT_YCbCr_420_SP);

	IM_STATUS st = imresize_t(src, dst, 0, 0, 0, 1);
	if (st != IM_STATUS_SUCCESS) {
		HW_LOG("imresize failed: %s", imStrError_t(st));
		return false;
	}
	return true;
}

static void blit_nv12(uint8_t *canvas, int cw_stride, int ch,
                      const uint8_t *overlay, int ow, int oh,
                      int ox, int oy) {
	ox = ALIGN2(ox);
	oy = ALIGN2(oy);

	uint8_t *canvas_y = canvas;
	const uint8_t *ov_y = overlay;
	for (int j = 0; j < oh && (oy + j) < ch; j++) {
		memcpy(canvas_y + (oy + j) * cw_stride + ox,
		       ov_y + j * ow,
		       static_cast<size_t>(ow));
	}

	int uv_h = oh / 2;
	int canvas_y_plane = cw_stride * ALIGN16(ch);
	int ov_y_plane = ow * oh;
	uint8_t *canvas_uv = canvas + canvas_y_plane;
	const uint8_t *ov_uv = overlay + ov_y_plane;
	for (int j = 0; j < uv_h && (oy / 2 + j) < (ch / 2); j++) {
		memcpy(canvas_uv + (oy / 2 + j) * cw_stride + ox,
		       ov_uv + j * ow,
		       static_cast<size_t>(ow));
	}
}

static bool encode_nv12_to_jpeg(PipHwContext *c, std::vector<uint8_t> *out_jpeg) {
	MPP_RET ret;
	MppFrame frame = NULL;
	MppPacket packet = NULL;

	ret = mpp_frame_init(&frame);
	if (ret != MPP_OK) {
		HW_LOG("mpp_frame_init(enc) failed: %d", ret);
		return false;
	}

	mpp_frame_set_width(frame, c->canvas_w);
	mpp_frame_set_height(frame, c->canvas_h);
	mpp_frame_set_hor_stride(frame, c->hor_stride);
	mpp_frame_set_ver_stride(frame, c->ver_stride);
	mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
	mpp_frame_set_buffer(frame, c->canvas_buf);
	mpp_frame_set_eos(frame, 0);

	MppMeta meta = mpp_frame_get_meta(frame);
	ret = mpp_packet_init_with_buffer(&packet, c->pkt_buf);
	if (ret != MPP_OK) {
		HW_LOG("mpp_packet_init_with_buffer(enc) failed: %d", ret);
		mpp_frame_deinit(&frame);
		return false;
	}
	mpp_packet_set_length(packet, 0);
	mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);

	ret = c->enc_mpi->encode_put_frame(c->enc_ctx, frame);
	if (ret != MPP_OK) {
		HW_LOG("encode_put_frame failed: %d", ret);
		mpp_frame_deinit(&frame);
		mpp_packet_deinit(&packet);
		return false;
	}
	mpp_frame_deinit(&frame);

	ret = c->enc_mpi->encode_get_packet(c->enc_ctx, &packet);
	if (ret != MPP_OK || !packet) {
		HW_LOG("encode_get_packet failed: %d pkt=%p", ret, packet);
		return false;
	}

	void *ptr = mpp_packet_get_pos(packet);
	size_t len = mpp_packet_get_length(packet);
	if (!ptr || len == 0) {
		HW_LOG("encode output empty");
		mpp_packet_deinit(&packet);
		return false;
	}

	out_jpeg->assign(static_cast<uint8_t *>(ptr),
	                 static_cast<uint8_t *>(ptr) + len);
	mpp_packet_deinit(&packet);
	return true;
}

static void draw_rounded_border(uint8_t *canvas, int cw_stride, int ch, int ox, int oy, int dw, int dh, const PipBorderConfig &bc) {
	if (!bc.enable || bc.radius <= 0) return;
	ox = ALIGN2(ox);
	oy = ALIGN2(oy);
	int w = ALIGN2(dw);
	int h = ALIGN2(dh);
	int r = bc.radius;
	int t = bc.thickness;
	uint8_t by = bc.y;
	uint8_t bu = bc.u;
	uint8_t bv = bc.v;

	// Canvas dimensions
	int canvas_y_plane = cw_stride * ALIGN16(ch);
	uint8_t *canvas_y = canvas;
	uint8_t *canvas_uv = canvas + canvas_y_plane;

	auto draw_pixel = [&](int px, int py, uint8_t y_val, uint8_t u_val, uint8_t v_val) {
		if (px < 0 || px >= cw_stride || py < 0 || py >= ch) return;
		canvas_y[py * cw_stride + px] = y_val;
		int uv_x = (px / 2) * 2;
		int uv_y = py / 2;
		canvas_uv[uv_y * cw_stride + uv_x] = u_val;
		canvas_uv[uv_y * cw_stride + uv_x + 1] = v_val;
	};

	// 1. Mask out the 4 outer corners to background black (Y=16, U=128, V=128)
	// Top-Left corner: center at (ox + r, oy + r)
	for (int y = 0; y < r; y++) {
		for (int x = 0; x < r; x++) {
			int dx = r - x;
			int dy = r - y;
			if (dx * dx + dy * dy > r * r) {
				draw_pixel(ox + x, oy + y, 16, 128, 128);
			}
		}
	}
	// Top-Right corner: center at (ox + w - r, oy + r)
	for (int y = 0; y < r; y++) {
		for (int x = 0; x < r; x++) {
			int dx = x + 1;
			int dy = r - y;
			if (dx * dx + dy * dy > r * r) {
				draw_pixel(ox + w - r + x, oy + y, 16, 128, 128);
			}
		}
	}
	// Bottom-Left corner: center at (ox + r, oy + h - r)
	for (int y = 0; y < r; y++) {
		for (int x = 0; x < r; x++) {
			int dx = r - x;
			int dy = y + 1;
			if (dx * dx + dy * dy > r * r) {
				draw_pixel(ox + x, oy + h - r + y, 16, 128, 128);
			}
		}
	}
	// Bottom-Right corner: center at (ox + w - r, oy + h - r)
	for (int y = 0; y < r; y++) {
		for (int x = 0; x < r; x++) {
			int dx = x + 1;
			int dy = y + 1;
			if (dx * dx + dy * dy > r * r) {
				draw_pixel(ox + w - r + x, oy + h - r + y, 16, 128, 128);
			}
		}
	}

	// 2. Draw border outline if thickness > 0
	if (t > 0) {
		// Top-Left corner border: (r-t)^2 < dx^2 + dy^2 <= r^2
		for (int y = 0; y < r; y++) {
			for (int x = 0; x < r; x++) {
				int dx = r - x;
				int dy = r - y;
				int d2 = dx * dx + dy * dy;
				if (d2 <= r * r && d2 > (r - t) * (r - t)) {
					draw_pixel(ox + x, oy + y, by, bu, bv);
				}
			}
		}
		// Top-Right corner border:
		for (int y = 0; y < r; y++) {
			for (int x = 0; x < r; x++) {
				int dx = x + 1;
				int dy = r - y;
				int d2 = dx * dx + dy * dy;
				if (d2 <= r * r && d2 > (r - t) * (r - t)) {
					draw_pixel(ox + w - r + x, oy + y, by, bu, bv);
				}
			}
		}
		// Bottom-Left corner border:
		for (int y = 0; y < r; y++) {
			for (int x = 0; x < r; x++) {
				int dx = r - x;
				int dy = y + 1;
				int d2 = dx * dx + dy * dy;
				if (d2 <= r * r && d2 > (r - t) * (r - t)) {
					draw_pixel(ox + x, oy + h - r + y, by, bu, bv);
				}
			}
		}
		// Bottom-Right corner border:
		for (int y = 0; y < r; y++) {
			for (int x = 0; x < r; x++) {
				int dx = x + 1;
				int dy = y + 1;
				int d2 = dx * dx + dy * dy;
				if (d2 <= r * r && d2 > (r - t) * (r - t)) {
					draw_pixel(ox + w - r + x, oy + h - r + y, by, bu, bv);
				}
			}
		}

		// Top straight border
		for (int x = r; x < w - r; x++) {
			for (int y = 0; y < t; y++) {
				draw_pixel(ox + x, oy + y, by, bu, bv);
			}
		}
		// Bottom straight border
		for (int x = r; x < w - r; x++) {
			for (int y = h - t; y < h; y++) {
				draw_pixel(ox + x, oy + y, by, bu, bv);
			}
		}
		// Left straight border
		for (int y = r; y < h - r; y++) {
			for (int x = 0; x < t; x++) {
				draw_pixel(ox + x, oy + y, by, bu, bv);
			}
		}
		// Right straight border
		for (int y = r; y < h - r; y++) {
			for (int x = w - t; x < w; x++) {
				draw_pixel(ox + x, oy + y, by, bu, bv);
			}
		}
	}
}

bool pip_hw_composite_layers(PipHwContext *c,
                             const uint8_t *jpeg_data, size_t jpeg_len,
                             const PipHwNv12Blit *blits, int n_blits,
                             const PipBorderConfig *bc,
                             std::vector<uint8_t> *out_jpeg)
{
	if (!c || !jpeg_data || jpeg_len == 0 || !out_jpeg)
		return false;
	if (n_blits < 0)
		return false;
	if (n_blits > 0 && !blits)
		return false;

	/* Step 1: MPP decode JPEG → NV12 (for validation) */
	MppFrame dec_frame = NULL;
	if (!decode_jpeg(c, jpeg_data, jpeg_len, &dec_frame))
		return false;
	mpp_frame_deinit(&dec_frame);

	/* Step 2: Initialize canvas with black and perform overlay blits */
	mpp_buffer_sync_begin(c->canvas_buf);
	uint8_t *canvas_ptr = static_cast<uint8_t *>(mpp_buffer_get_ptr(c->canvas_buf));
	std::memset(canvas_ptr, 16, static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride));
	std::memset(canvas_ptr + static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride), 128, static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride) / 2);

	std::vector<uint8_t> scale_scratch;
	for (int i = 0; i < n_blits; i++) {
		const PipHwNv12Blit &b = blits[i];
		if (!b.nv12 || b.dst_w <= 0 || b.dst_h <= 0)
			continue;
		const int dw = ALIGN2(b.dst_w);
		const int dh = ALIGN2(b.dst_h);
		const bool custom_src = b.src_w > 0 && b.src_h > 0;
		int sw = custom_src ? b.src_w : b.dst_w;
		int sh = custom_src ? b.src_h : b.dst_h;
		sw = ALIGN2(sw);
		sh = ALIGN2(sh);
		const uint8_t *blit_src = b.nv12;
		if (custom_src && (sw != dw || sh != dh)) {
			const size_t need = static_cast<size_t>(dw) * static_cast<size_t>(dh) * 3 / 2;
			if (scale_scratch.size() < need)
				scale_scratch.resize(need);
			if (!pip_hw_nv12_resize_virtual(b.nv12, sw, sh, scale_scratch.data(), b.dst_w, b.dst_h)) {
				mpp_buffer_sync_end(c->canvas_buf);
				return false;
			}
			blit_src = scale_scratch.data();
		}
		blit_nv12(canvas_ptr, c->hor_stride, c->ver_stride, blit_src, dw, dh, b.ox, b.oy);
	}
	if (bc && bc->enable) {
		for (int i = 0; i < n_blits; i++) {
			const PipHwNv12Blit &b = blits[i];
			if (!b.nv12 || b.dst_w <= 0 || b.dst_h <= 0)
				continue;
			draw_rounded_border(canvas_ptr, c->hor_stride, c->ver_stride, b.ox, b.oy, b.dst_w, b.dst_h, *bc);
		}
	}
	mpp_buffer_sync_end(c->canvas_buf);

	/* Step 3: MPP encode canvas NV12 → JPEG */
	return encode_nv12_to_jpeg(c, out_jpeg);
}

bool pip_hw_composite_layers_nv12(PipHwContext *c,
                                  const uint8_t *bg_nv12, int bg_w, int bg_h,
                                  const PipHwNv12Blit *blits, int n_blits,
                                  const PipBorderConfig *bc,
                                  std::vector<uint8_t> *out_jpeg)
{
	if (!c || !bg_nv12 || bg_w <= 0 || bg_h <= 0 || !out_jpeg)
		return false;
	if (n_blits < 0)
		return false;
	if (n_blits > 0 && !blits)
		return false;

	/* Step 1: Initialize canvas with black and perform overlay blits */
	mpp_buffer_sync_begin(c->canvas_buf);
	uint8_t *canvas_ptr = static_cast<uint8_t *>(mpp_buffer_get_ptr(c->canvas_buf));
	std::memset(canvas_ptr, 16, static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride));
	std::memset(canvas_ptr + static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride), 128, static_cast<size_t>(c->hor_stride) * static_cast<size_t>(c->ver_stride) / 2);

	std::vector<uint8_t> scale_scratch;
	for (int i = 0; i < n_blits; i++) {
		const PipHwNv12Blit &b = blits[i];
		if (!b.nv12 || b.dst_w <= 0 || b.dst_h <= 0)
			continue;
		const int dw = ALIGN2(b.dst_w);
		const int dh = ALIGN2(b.dst_h);
		const bool custom_src = b.src_w > 0 && b.src_h > 0;
		int sw = custom_src ? b.src_w : b.dst_w;
		int sh = custom_src ? b.src_h : b.dst_h;
		sw = ALIGN2(sw);
		sh = ALIGN2(sh);
		const uint8_t *blit_src = b.nv12;
		if (custom_src && (sw != dw || sh != dh)) {
			const size_t need = static_cast<size_t>(dw) * static_cast<size_t>(dh) * 3 / 2;
			if (scale_scratch.size() < need)
				scale_scratch.resize(need);
			if (!pip_hw_nv12_resize_virtual(b.nv12, sw, sh, scale_scratch.data(), b.dst_w, b.dst_h)) {
				mpp_buffer_sync_end(c->canvas_buf);
				return false;
			}
			blit_src = scale_scratch.data();
		}
		blit_nv12(canvas_ptr, c->hor_stride, c->ver_stride, blit_src, dw, dh, b.ox, b.oy);
	}
	if (bc && bc->enable) {
		for (int i = 0; i < n_blits; i++) {
			const PipHwNv12Blit &b = blits[i];
			if (!b.nv12 || b.dst_w <= 0 || b.dst_h <= 0)
				continue;
			draw_rounded_border(canvas_ptr, c->hor_stride, c->ver_stride, b.ox, b.oy, b.dst_w, b.dst_h, *bc);
		}
	}
	mpp_buffer_sync_end(c->canvas_buf);

	/* Step 2: MPP encode canvas NV12 → JPEG */
	return encode_nv12_to_jpeg(c, out_jpeg);
}

bool pip_hw_composite(PipHwContext *c,
                      const uint8_t *jpeg_data, size_t jpeg_len,
                      const uint8_t *overlay_nv12, int ow, int oh,
                      int ox, int oy,
                      std::vector<uint8_t> *out_jpeg)
{
	PipHwNv12Blit b[1];
	int n = 0;
	if (overlay_nv12 && ow > 0 && oh > 0) {
		b[0] = {overlay_nv12, ow, oh, ox, oy, 0, 0};
		n = 1;
	}
	return pip_hw_composite_layers(c, jpeg_data, jpeg_len, b, n, nullptr, out_jpeg);
}

bool pip_hw_rgb_to_nv12(const uint8_t *rgb, int w, int h,
                        std::vector<uint8_t> *nv12) {
	if (!rgb || w <= 0 || h <= 0 || !nv12)
		return false;

	w = ALIGN2(w);
	h = ALIGN2(h);

	size_t nv12_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 3 / 2;
	nv12->resize(nv12_size);

	rga_buffer_t src = wrapbuffer_virtualaddr_t(
		const_cast<uint8_t *>(rgb), w, h, w, h, RK_FORMAT_RGB_888);
	rga_buffer_t dst = wrapbuffer_virtualaddr_t(
		nv12->data(), w, h, w, h, RK_FORMAT_YCbCr_420_SP);

	IM_STATUS st = imcvtcolor_t(src, dst,
	                            RK_FORMAT_RGB_888,
	                            RK_FORMAT_YCbCr_420_SP,
	                            IM_RGB_TO_YUV_BT601_LIMIT, 1);
	if (st != IM_STATUS_SUCCESS) {
		HW_LOG("imcvtcolor RGB->NV12 failed: %s", imStrError_t(st));
		return false;
	}

	return true;
}
