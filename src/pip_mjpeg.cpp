#include "pip_mjpeg.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

namespace {

struct PipJpegErrorMgr {
	struct jpeg_error_mgr pub;
	jmp_buf jb;
	char msg[JMSG_LENGTH_MAX];
};

static void pip_jpeg_error_exit(j_common_ptr cinfo) {
	auto *m = reinterpret_cast<PipJpegErrorMgr *>(cinfo->err);
	m->msg[0] = '\0';
	if (cinfo->err && cinfo->err->format_message)
		(*cinfo->err->format_message)(cinfo, m->msg);
	longjmp(m->jb, 1);
}

thread_local PipJpegErrorMgr *g_last_err = nullptr;
thread_local char g_last_msg[JMSG_LENGTH_MAX] = {0};

} // namespace

const char *pip_mjpeg_last_error() {
	if (g_last_err && g_last_err->msg[0])
		return g_last_err->msg;
	if (g_last_msg[0])
		return g_last_msg;
	return "";
}

bool pip_mjpeg_decode_jpeg_file_rgb(const char *path, std::vector<uint8_t> *rgb, int *out_w, int *out_h) {
	if (!path || !path[0] || !rgb || !out_w || !out_h)
		return false;
	std::vector<uint8_t> jpeg_data;
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in.is_open()) {
		std::snprintf(g_last_msg, sizeof(g_last_msg), "open failed: %s", std::strerror(errno));
		return false;
	}
	const std::streamsize sz = in.tellg();
	if (sz <= 0) {
		std::snprintf(g_last_msg, sizeof(g_last_msg), "empty or invalid jpeg file");
		return false;
	}
	in.seekg(0, std::ios::beg);
	jpeg_data.resize(static_cast<size_t>(sz));
	if (!in.read(reinterpret_cast<char *>(jpeg_data.data()), sz)) {
		std::snprintf(g_last_msg, sizeof(g_last_msg), "read failed");
		return false;
	}
	return pip_mjpeg_decode_jpeg_rgb(jpeg_data.data(), jpeg_data.size(), rgb, out_w, out_h);
}

bool pip_mjpeg_decode_jpeg_rgb(const uint8_t *jpeg_data, size_t jpeg_len, std::vector<uint8_t> *rgb, int *out_w,
                               int *out_h) {
	if (!jpeg_data || jpeg_len < 2 || !rgb || !out_w || !out_h)
		return false;

	jpeg_decompress_struct cinfo{};
	PipJpegErrorMgr jerr{};
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = pip_jpeg_error_exit;
	jerr.msg[0] = '\0';
	g_last_err = &jerr;
	g_last_msg[0] = '\0';

	if (setjmp(jerr.jb)) {
		if (jerr.msg[0])
			std::snprintf(g_last_msg, sizeof(g_last_msg), "%s", jerr.msg);
		jpeg_destroy_decompress(&cinfo);
		g_last_err = nullptr;
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, const_cast<unsigned char *>(jpeg_data), static_cast<unsigned long>(jpeg_len));
	const int hdr = jpeg_read_header(&cinfo, TRUE);
	if (hdr != JPEG_HEADER_OK) {
		std::snprintf(jerr.msg, sizeof(jerr.msg), "jpeg_read_header=%d", hdr);
		std::snprintf(g_last_msg, sizeof(g_last_msg), "%s", jerr.msg);
		jpeg_destroy_decompress(&cinfo);
		g_last_err = nullptr;
		return false;
	}

	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	const int w = static_cast<int>(cinfo.output_width);
	const int h = static_cast<int>(cinfo.output_height);
	const int row_stride = w * 3;
	rgb->resize(static_cast<size_t>(row_stride * h));

	JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE,
	                                                 static_cast<unsigned int>(row_stride), 1);

	for (int y = 0; cinfo.output_scanline < cinfo.output_height; y++) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		std::memcpy(rgb->data() + static_cast<size_t>(y) * static_cast<size_t>(row_stride), buffer[0],
		            static_cast<size_t>(row_stride));
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	*out_w = w;
	*out_h = h;
	g_last_err = nullptr;
	g_last_msg[0] = '\0';
	return true;
}

void pip_mjpeg_scale_rgb_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
	if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
		return;

	for (int y = 0; y < dh; y++) {
		const float v = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
		const int y0 = static_cast<int>(std::floor(v));
		const int y1 = (y0 + 1 < sh) ? (y0 + 1) : (sh - 1);
		const float fy = v - static_cast<float>(y0);
		for (int x = 0; x < dw; x++) {
			const float u = (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
			const int x0 = static_cast<int>(std::floor(u));
			const int x1 = (x0 + 1 < sw) ? (x0 + 1) : (sw - 1);
			const float fx = u - static_cast<float>(x0);
			for (int c = 0; c < 3; c++) {
				const float p00 = static_cast<float>(src[(static_cast<size_t>(y0) * static_cast<size_t>(sw) +
				                                        static_cast<size_t>(x0)) *
				                                           3U +
				                                       static_cast<size_t>(c)]);
				const float p01 = static_cast<float>(src[(static_cast<size_t>(y0) * static_cast<size_t>(sw) +
				                                        static_cast<size_t>(x1)) *
				                                           3U +
				                                       static_cast<size_t>(c)]);
				const float p10 = static_cast<float>(src[(static_cast<size_t>(y1) * static_cast<size_t>(sw) +
				                                        static_cast<size_t>(x0)) *
				                                           3U +
				                                       static_cast<size_t>(c)]);
				const float p11 = static_cast<float>(src[(static_cast<size_t>(y1) * static_cast<size_t>(sw) +
				                                        static_cast<size_t>(x1)) *
				                                           3U +
				                                       static_cast<size_t>(c)]);
				const float p0 = p00 * (1.f - fx) + p01 * fx;
				const float p1 = p10 * (1.f - fx) + p11 * fx;
				const float p = p0 * (1.f - fy) + p1 * fy;
				int o = static_cast<int>(p + 0.5f);
				if (o < 0)
					o = 0;
				if (o > 255)
					o = 255;
				dst[(static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 3U +
				    static_cast<size_t>(c)] = static_cast<uint8_t>(o);
			}
		}
	}
}

void pip_mjpeg_blit_rgb(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, int dx, int dy) {
	if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
		return;

	const int x0 = (dx < 0) ? -dx : 0;
	const int y0 = (dy < 0) ? -dy : 0;
	const int dst_x0 = (dx < 0) ? 0 : dx;
	const int dst_y0 = (dy < 0) ? 0 : dy;
	const int copy_w = std::min(sw - x0, dw - dst_x0);
	const int copy_h = std::min(sh - y0, dh - dst_y0);
	if (copy_w <= 0 || copy_h <= 0)
		return;

	for (int j = 0; j < copy_h; j++) {
		const uint8_t *srow = src + static_cast<size_t>(y0 + j) * static_cast<size_t>(sw) * 3U;
		uint8_t *drow = dst + static_cast<size_t>(dst_y0 + j) * static_cast<size_t>(dw) * 3U;
		std::memcpy(drow + static_cast<size_t>(dst_x0) * 3U,
		            srow + static_cast<size_t>(x0) * 3U, static_cast<size_t>(copy_w) * 3U);
	}
}

bool pip_mjpeg_encode_rgb_jpeg(const uint8_t *rgb, int w, int h, int quality, std::vector<uint8_t> *jpeg_out) {
	if (!rgb || w <= 0 || h <= 0 || !jpeg_out)
		return false;
	int q = quality;
	if (q < 1)
		q = 1;
	if (q > 100)
		q = 100;

	jpeg_compress_struct cinfo{};
	PipJpegErrorMgr jerr{};
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = pip_jpeg_error_exit;
	jerr.msg[0] = '\0';
	g_last_err = &jerr;

	unsigned char *mem = nullptr;
	unsigned long mem_size = 0;

	if (setjmp(jerr.jb)) {
		jpeg_destroy_compress(&cinfo);
		if (mem)
			std::free(mem);
		g_last_err = nullptr;
		return false;
	}

	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, &mem, &mem_size);
	cinfo.image_width = static_cast<unsigned int>(w);
	cinfo.image_height = static_cast<unsigned int>(h);
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, q, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	const int row_stride = w * 3;
	while (cinfo.next_scanline < cinfo.image_height) {
		JSAMPROW row_pointer[1];
		row_pointer[0] = const_cast<JSAMPROW>(rgb + static_cast<size_t>(cinfo.next_scanline) *
		                                                 static_cast<size_t>(row_stride));
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	g_last_err = nullptr;

	if (!mem || mem_size == 0) {
		if (mem)
			std::free(mem);
		g_last_err = nullptr;
		return false;
	}
	jpeg_out->assign(mem, mem + mem_size);
	std::free(mem);
	g_last_err = nullptr;
	return true;
}

bool pip_mjpeg_composite_jpeg(const uint8_t *bg_jpeg, size_t bg_jpeg_len, int canvas_w, int canvas_h,
                              const uint8_t *overlay_rgb, int ow, int oh, int ox, int oy, int jpeg_quality,
                              std::vector<uint8_t> *out_jpeg, std::vector<uint8_t> *tmp_canvas_rgb) {
	if (!bg_jpeg || bg_jpeg_len == 0 || canvas_w <= 0 || canvas_h <= 0 || !overlay_rgb || ow <= 0 || oh <= 0 ||
	    !out_jpeg || !tmp_canvas_rgb)
		return false;

	const size_t need = static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h) * 3U;
	if (tmp_canvas_rgb->size() < need)
		tmp_canvas_rgb->resize(need);

	std::vector<uint8_t> bg_rgb;
	int bw = 0;
	int bh = 0;
	if (!pip_mjpeg_decode_jpeg_rgb(bg_jpeg, bg_jpeg_len, &bg_rgb, &bw, &bh))
		return false;

	pip_mjpeg_scale_rgb_bilinear(bg_rgb.data(), bw, bh, tmp_canvas_rgb->data(), canvas_w, canvas_h);
	pip_mjpeg_blit_rgb(overlay_rgb, ow, oh, tmp_canvas_rgb->data(), canvas_w, canvas_h, ox, oy);
	return pip_mjpeg_encode_rgb_jpeg(tmp_canvas_rgb->data(), canvas_w, canvas_h, jpeg_quality, out_jpeg);
}
