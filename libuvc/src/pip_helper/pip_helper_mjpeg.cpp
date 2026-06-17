#include "my_uvc_pip/pip_helper.h"
#include "my_uvc_pip/pip_overlay_stale_policy.hpp"
#include "my_uvc_pip/pip_tile_layout.hpp"

#include "mpp_jpeg.h"
#include "pip_mjpeg.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef MY_UVC_PIP_HELPER_TRANSITIONAL_IO
#define MY_UVC_PIP_HELPER_TRANSITIONAL_IO 1
#endif

namespace fs = std::filesystem;

thread_local char g_err[512];

static int64_t mono_ms_now()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void set_err(const char *fmt, ...)
{
	g_err[0] = '\0';
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(g_err, sizeof(g_err), fmt, ap);
	va_end(ap);
}

static std::string to_lower_ext(std::string s)
{
	for (auto &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

static bool read_file_all(const std::string &path, std::vector<uint8_t> *out)
{
	if (!out)
		return false;
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in.is_open())
		return false;
	const std::streamsize size = in.tellg();
	if (size <= 0)
		return false;
	in.seekg(0, std::ios::beg);
	out->resize(static_cast<size_t>(size));
	return static_cast<bool>(in.read(reinterpret_cast<char *>(out->data()), size));
}

static bool list_jpeg_paths_from_dir(const std::string &dir, std::vector<fs::path> *paths)
{
	if (!paths)
		return false;
	paths->clear();
	std::error_code ec;
	for (const auto &ent : fs::directory_iterator(fs::path(dir), ec)) {
		if (ec)
			break;
		if (!ent.is_regular_file())
			continue;
		auto ext = to_lower_ext(ent.path().extension().string());
		if (ext == ".jpg" || ext == ".jpeg")
			paths->push_back(ent.path());
	}
	if (ec || paths->empty())
		return false;
	std::sort(paths->begin(), paths->end(),
	          [](const fs::path &a, const fs::path &b) { return a.filename().string() < b.filename().string(); });
	return true;
}

struct PipHelperImpl {
	int channel_id = 0;
	PipHwContext *hw = nullptr;
	int pip_ow = 0;
	int pip_oh = 0;
	int pip_x = 0;
	int pip_y = 0;
	std::vector<uint8_t> overlay_nv12_single;
	/*
	 * Directory overlay: paths + lazy NV12 cache.
	 * Overlay JPEGs use libjpeg (CPU); background uses MPP (HW) in pip_hw.
	 * A background thread pre-fills slots so composite stays near target fps.
	 */
	bool overlay_dir_lazy = false;
	std::vector<std::string> overlay_src_paths;
	std::vector<std::vector<uint8_t>> overlay_nv12_cache;
	std::vector<std::unique_ptr<std::mutex>> overlay_slot_mtx;
	std::thread overlay_preload_th;
	std::atomic<bool> overlay_preload_stop{false};
	size_t overlay_idx = 0;
	std::vector<uint8_t> jpeg_out;
	bool ready = false;
	/** 与 pip_helper_config_t.pip_overlay_stale_timeout_ms 一致（含 -1），供 §2.4 策略纯函数。 */
	int32_t stale_timeout_cfg_ms = -1;
	/** 单文件主讲人 NV12：最后有效帧的单调时间（毫秒）。 */
	int64_t presenter_last_update_ms = 0;
	bool presenter_has_valid = false;
	/** create 时自 pip_overlay_path 单文件解码预载成功（非 NV12 API 流）。 */
	bool presenter_preloaded_jpeg = false;
	/** 下三分之一网格（0 = 未启用）。 */
	int tile_n_tiles = 0;
	std::array<my_uvc_pip::PipTileRect, my_uvc_pip::kPipTileLayoutMax> tile_rects{};
	std::vector<std::vector<uint8_t>> tile_nv12_cache;
	std::vector<int64_t> tile_last_update_ms;
	std::vector<uint8_t> tile_has_valid;
	float pip_width_stretch_factor = 1.0f;
	bool border_enable = true;
	int border_radius = 4;
	int border_thickness = 2;
	uint8_t border_y = 235;
	uint8_t border_u = 128;
	uint8_t border_v = 128;
};

static bool parse_hex_color_to_yuv(const char *hex, uint8_t *y, uint8_t *u, uint8_t *v) {
	if (!hex) return false;
	std::string s = hex;
	if (!s.empty() && s[0] == '#') {
		s = s.substr(1);
	}
	if (s.size() != 6) {
		return false;
	}
	unsigned int r = 0, g = 0, b = 0;
	if (std::sscanf(s.c_str(), "%2x%2x%2x", &r, &g, &b) != 3) {
		return false;
	}
	// BT.601 limited range conversion
	double yd = 16.0 + 0.257 * r + 0.504 * g + 0.098 * b;
	double ud = 128.0 - 0.148 * r - 0.291 * g + 0.439 * b;
	double vd = 128.0 + 0.439 * r - 0.368 * g - 0.071 * b;

	*y = static_cast<uint8_t>(yd < 0.0 ? 0 : (yd > 255.0 ? 255 : yd));
	*u = static_cast<uint8_t>(ud < 0.0 ? 0 : (ud > 255.0 ? 255 : ud));
	*v = static_cast<uint8_t>(vd < 0.0 ? 0 : (vd > 255.0 ? 255 : vd));
	return true;
}

static bool decode_jpeg_file_to_pip_nv12(PipHelperImpl *p, const char *jpeg_path,
                                         std::vector<uint8_t> *nv12_out)
{
	std::vector<uint8_t> ov_dec;
	int ojw = 0, ojh = 0;
	if (!pip_mjpeg_decode_jpeg_file_rgb(jpeg_path, &ov_dec, &ojw, &ojh)) {
		set_err("overlay decode failed: %s (%s)", jpeg_path, pip_mjpeg_last_error());
		return false;
	}
	std::vector<uint8_t> scaled_rgb(static_cast<size_t>(p->pip_ow * p->pip_oh * 3));
	pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, scaled_rgb.data(), p->pip_ow, p->pip_oh);
	if (!pip_hw_rgb_to_nv12(scaled_rgb.data(), p->pip_ow, p->pip_oh, nv12_out)) {
		set_err("overlay RGB→NV12 failed");
		return false;
	}
	return true;
}

static bool ensure_overlay_dir_slot(PipHelperImpl *p, size_t slot)
{
	if (slot >= p->overlay_nv12_cache.size())
		return false;
	std::unique_lock<std::mutex> lk;
	if (slot < p->overlay_slot_mtx.size() && p->overlay_slot_mtx[slot])
		lk = std::unique_lock<std::mutex>(*p->overlay_slot_mtx[slot]);
	if (!p->overlay_nv12_cache[slot].empty())
		return true;
	return decode_jpeg_file_to_pip_nv12(p, p->overlay_src_paths[slot].c_str(),
	                                    &p->overlay_nv12_cache[slot]);
}

extern "C" const char *pip_helper_version(void)
{
	return "pip_helper_mjpeg";
}

extern "C" const char *pip_helper_stub_version(void)
{
	return pip_helper_version();
}

extern "C" const char *pip_helper_last_error(void)
{
	return g_err;
}

extern "C" pip_helper_t *pip_helper_create(int channel_id, const pip_helper_config_t *cfg)
{
	g_err[0] = '\0';
	if (!cfg || !cfg->pip_enable) {
		set_err("pip_enable is 0");
		return nullptr;
	}
	if (cfg->canvas_width <= 0 || cfg->canvas_height <= 0) {
		set_err("invalid canvas size");
		return nullptr;
	}
#if MY_UVC_PIP_HELPER_TRANSITIONAL_IO
	if (!cfg->pip_overlay_path || !cfg->pip_overlay_path[0]) {
		set_err("pip_overlay_path empty");
		return nullptr;
	}
#endif
	// Simplify: Strictly use config values. 0 means "auto/adaptive".
	float sw = cfg->pip_adaptive_scale_w > 0.0f ? cfg->pip_adaptive_scale_w : 0.666667f;
	float sh = cfg->pip_adaptive_scale_h > 0.0f ? cfg->pip_adaptive_scale_h : 0.333333f;

	int pw = (cfg->pip_w > 0) ? cfg->pip_w : static_cast<int>(cfg->canvas_width * sw);
	int ph = (cfg->pip_h > 0) ? cfg->pip_h : static_cast<int>(cfg->canvas_height * sh);
	int px = cfg->pip_x;
	int py = cfg->pip_y;

	// If position is not explicitly provided (>0), default to bottom-center
	if (px <= 0 && py <= 0) {
		px = (cfg->canvas_width - pw) / 2;
		py = cfg->canvas_height - ph;
	}

	// Final safety bounds clamping (no forced resets)
	if (pw > cfg->canvas_width) pw = cfg->canvas_width;
	if (ph > cfg->canvas_height) ph = cfg->canvas_height;
	if (px < 0) px = 0;
	if (py < 0) py = 0;
	if (px + pw > cfg->canvas_width) px = cfg->canvas_width - pw;
	if (py + ph > cfg->canvas_height) py = cfg->canvas_height - ph;

	auto *p = new (std::nothrow) PipHelperImpl{};
	if (!p) {
		set_err("oom");
		return nullptr;
	}
	p->channel_id = channel_id;
	{
		int ow = (pw + 1) & ~1;
		int oh = (ph + 1) & ~1;
		p->pip_ow = (ow / 4) * 4;
		p->pip_oh = (oh / 4) * 4;
		p->pip_x = px;
		p->pip_y = py;
	}
	p->pip_width_stretch_factor = cfg->pip_width_stretch_factor > 0.0f ? cfg->pip_width_stretch_factor : 1.0f;
	p->border_enable = cfg->pip_border_enable != 0;
	p->border_radius = cfg->pip_border_radius >= 0 ? cfg->pip_border_radius : 4;
	p->border_thickness = cfg->pip_border_thickness >= 0 ? cfg->pip_border_thickness : 2;
	p->border_y = 235;
	p->border_u = 128;
	p->border_v = 128;
	if (cfg->pip_border_color && cfg->pip_border_color[0] != '\0') {
		parse_hex_color_to_yuv(cfg->pip_border_color, &p->border_y, &p->border_u, &p->border_v);
	}
	p->stale_timeout_cfg_ms = static_cast<int32_t>(cfg->pip_overlay_stale_timeout_ms);

	const int nt = cfg->pip_tile_n_tiles;
	if (nt < 0 || nt > my_uvc_pip::kPipTileLayoutMax) {
		set_err("pip_tile_n_tiles out of range");
		delete p;
		return nullptr;
	}
	if (cfg->pip_tile_gap_px < 0 || cfg->pip_tile_margin_px < 0) {
		set_err("pip_tile_gap_px/margin_px invalid");
		delete p;
		return nullptr;
	}
	if (nt > 0) {
		my_uvc_pip::PipTileLayoutSpec tls{};
		tls.canvas_w = cfg->canvas_width;
		tls.canvas_h = cfg->canvas_height;
		tls.n_tiles = nt;
		tls.gap_px = cfg->pip_tile_gap_px;
		tls.margin_px = cfg->pip_tile_margin_px;
		std::array<my_uvc_pip::PipTileRect, my_uvc_pip::kPipTileLayoutMax> tr{};
		const int got = my_uvc_pip::pip_tile_layout_full_screen(tls, &tr);
		if (got != nt) {
			set_err("pip tile layout invalid for canvas");
			delete p;
			return nullptr;
		}
		p->tile_n_tiles = nt;
		p->tile_rects = tr;
		p->tile_nv12_cache.resize(static_cast<size_t>(nt));
		p->tile_last_update_ms.assign(static_cast<size_t>(nt), 0);
		p->tile_has_valid.assign(static_cast<size_t>(nt), 0);
	}

	if (!pip_hw_init(&p->hw, cfg->canvas_width, cfg->canvas_height, cfg->pip_jpeg_quality)) {
		set_err("pip_hw_init failed");
		delete p;
		return nullptr;
	}

#if MY_UVC_PIP_HELPER_TRANSITIONAL_IO
	const std::string overlay_path = cfg->pip_overlay_path;
	std::error_code ec;
	const bool overlay_is_dir = fs::is_directory(fs::path(overlay_path), ec) && !ec;

	if (overlay_is_dir) {
		std::vector<fs::path> ov_paths;
		if (!list_jpeg_paths_from_dir(overlay_path, &ov_paths)) {
			set_err("overlay dir has no jpeg");
			pip_hw_deinit(p->hw);
			delete p;
			return nullptr;
		}
		p->overlay_dir_lazy = true;
		p->overlay_src_paths.reserve(ov_paths.size());
		for (const auto &path_ent : ov_paths)
			p->overlay_src_paths.push_back(path_ent.string());
		const size_t n = ov_paths.size();
		p->overlay_nv12_cache.resize(n);
		p->overlay_slot_mtx.clear();
		p->overlay_slot_mtx.reserve(n);
		for (size_t i = 0; i < n; i++)
			p->overlay_slot_mtx.emplace_back(std::make_unique<std::mutex>());
		/* Pre-decode slot 0 so first composite is fast. */
		if (!ensure_overlay_dir_slot(p, 0)) {
			pip_hw_deinit(p->hw);
			delete p;
			return nullptr;
		}
		/* CPU libjpeg decode for remaining slots — preload off-thread so fps stays up. */
		if (n > 1) {
			p->overlay_preload_stop.store(false);
			p->overlay_preload_th = std::thread([p, n]() {
				for (size_t i = 1; i < n; i++) {
					if (p->overlay_preload_stop.load(std::memory_order_relaxed))
						return;
					(void)ensure_overlay_dir_slot(p, i);
				}
			});
		}
	} else {
		std::vector<uint8_t> ov_jpg;
		if (!overlay_path.empty() && read_file_all(overlay_path, &ov_jpg)) {
			std::vector<uint8_t> ov_dec;
			int ojw = 0, ojh = 0;
			if (pip_mjpeg_decode_jpeg_rgb(ov_jpg.data(), ov_jpg.size(), &ov_dec, &ojw, &ojh)) {
				std::vector<uint8_t> scaled_rgb(static_cast<size_t>(p->pip_ow * p->pip_oh * 3));
				pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, scaled_rgb.data(), p->pip_ow, p->pip_oh);
				if (pip_hw_rgb_to_nv12(scaled_rgb.data(), p->pip_ow, p->pip_oh, &p->overlay_nv12_single)) {
					p->presenter_preloaded_jpeg = true;
				}
			}
		}
	}
#else
	(void)cfg->pip_overlay_path;
	p->overlay_nv12_single.resize(static_cast<size_t>(p->pip_ow) * static_cast<size_t>(p->pip_oh) * 3 / 2);
#endif

	if (p->overlay_nv12_single.empty()) {
		p->overlay_nv12_single.resize(static_cast<size_t>(p->pip_ow) * static_cast<size_t>(p->pip_oh) * 3 / 2);
	}

	if (p->overlay_dir_lazy) {
		p->presenter_has_valid = false;
	} else {
#if MY_UVC_PIP_HELPER_TRANSITIONAL_IO
		p->presenter_has_valid = p->presenter_preloaded_jpeg;
		p->presenter_last_update_ms = mono_ms_now();
#else
		p->presenter_has_valid = false;
#endif
	}

	p->ready = true;
	return reinterpret_cast<pip_helper_t *>(p);
}

extern "C" void pip_helper_destroy(pip_helper_t *h)
{
	if (!h)
		return;
	auto *p = reinterpret_cast<PipHelperImpl *>(h);
	p->overlay_preload_stop.store(true);
	if (p->overlay_preload_th.joinable())
		p->overlay_preload_th.join();
	if (p->hw)
		pip_hw_deinit(p->hw);
	delete p;
}

extern "C" int pip_helper_composite_mjpeg(pip_helper_t *h, const uint8_t *bg_jpeg, size_t bg_jpeg_len,
                                          const uint8_t **out_jpeg, size_t *out_jpeg_len)
{
	return pip_helper_composite_mjpeg_ex(h, bg_jpeg, bg_jpeg_len, nullptr, out_jpeg, out_jpeg_len);
}

extern "C" int pip_helper_composite_mjpeg_ex(pip_helper_t *h, const uint8_t *bg_jpeg, size_t bg_jpeg_len,
                                             const pip_helper_composite_opts_t *opts, const uint8_t **out_jpeg,
                                             size_t *out_jpeg_len)
{
	g_err[0] = '\0';
	if (!h || !bg_jpeg || bg_jpeg_len == 0 || !out_jpeg || !out_jpeg_len) {
		set_err("invalid args");
		return -1;
	}
	auto *p = reinterpret_cast<PipHelperImpl *>(h);
	if (!p->ready || !p->hw) {
		set_err("helper not ready");
		return -1;
	}

	int na = 0;
	const uint8_t **tnv = nullptr;
	if (opts) {
		na = opts->n_active;
		tnv = opts->tile_nv12;
	}
	if (na < 0) {
		set_err("n_active negative");
		return -1;
	}
	if (na > p->tile_n_tiles) {
		set_err("n_active > pip_tile_n_tiles");
		return -1;
	}
	if (p->tile_n_tiles == 0 && na > 0) {
		set_err("n_active set but pip_tile_n_tiles is 0");
		return -1;
	}
	if (na > 0 && !tnv) {
		set_err("tile_nv12 required when n_active > 0");
		return -1;
	}
	const int *tile_upd = (opts && opts->tile_nv12_updated) ? opts->tile_nv12_updated : nullptr;
	if (!tile_upd) {
		for (int i = 0; i < na; i++) {
			if (!tnv[i]) {
				set_err("tile_nv12[%d] is null", i);
				return -1;
			}
		}
	} else {
		for (int i = 0; i < na; i++) {
			if (tile_upd[i] && !tnv[i]) {
				set_err("tile_nv12_updated[%d] without tile_nv12", i);
				return -1;
			}
		}
	}
	const int *tile_sw = (opts && opts->tile_src_w) ? opts->tile_src_w : nullptr;
	const int *tile_sh = (opts && opts->tile_src_h) ? opts->tile_src_h : nullptr;
	if (tile_sw || tile_sh) {
		if (!tile_sw || !tile_sh) {
			set_err("tile_src_w and tile_src_h must both be null or both set");
			return -1;
		}
		for (int i = 0; i < na; i++) {
			const int tws = tile_sw[i];
			const int ths = tile_sh[i];
			if ((tws > 0) != (ths > 0)) {
				set_err("tile_src_w/h invalid at slot %d", i);
				return -1;
			}
		}
	}

	const int64_t frame_now_ms = (opts && opts->now_ms != 0) ? opts->now_ms : mono_ms_now();

	const uint8_t *ov_ptr = nullptr;
	const size_t need = static_cast<size_t>(p->pip_ow) * static_cast<size_t>(p->pip_oh) * 3 / 2;

	bool used_bg_for_presenter = false;
	int cur_ow = p->pip_ow;
	int cur_oh = p->pip_oh;
	int cur_x = p->pip_x;
	int cur_y = p->pip_y;

	if (bg_jpeg && bg_jpeg_len > 0) {
		std::vector<uint8_t> ov_dec;
		int ojw = 0, ojh = 0;
		if (pip_mjpeg_decode_jpeg_rgb(bg_jpeg, bg_jpeg_len, &ov_dec, &ojw, &ojh)) {
			double scale = std::min(static_cast<double>(p->pip_ow) / ojw, static_cast<double>(p->pip_oh) / ojh);
			int cur_h = static_cast<int>(ojh * scale);
			int cur_w = static_cast<int>(ojw * scale * p->pip_width_stretch_factor);
			if (cur_w > p->pip_ow) {
				cur_w = p->pip_ow;
			}
			cur_ow = (cur_w / 16) * 16;
			cur_oh = (cur_h / 2) * 2;
			if (cur_ow <= 0) cur_ow = 16;
			if (cur_oh <= 0) cur_oh = 2;
			cur_x = (p->pip_x + (p->pip_ow - cur_ow) / 2) & ~1;
			cur_y = p->pip_y + (p->pip_oh - cur_oh);

			std::vector<uint8_t> scaled_rgb(static_cast<size_t>(cur_ow * cur_oh * 3));
			pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, scaled_rgb.data(), cur_ow, cur_oh);
			const size_t cur_need = static_cast<size_t>(cur_ow) * static_cast<size_t>(cur_oh) * 3 / 2;
			if (p->overlay_nv12_single.size() != cur_need) {
				p->overlay_nv12_single.resize(cur_need);
			}
			if (pip_hw_rgb_to_nv12(scaled_rgb.data(), cur_ow, cur_oh, &p->overlay_nv12_single)) {
				ov_ptr = p->overlay_nv12_single.data();
				used_bg_for_presenter = true;
			}
		}
	}

	if (!used_bg_for_presenter) {
		if (opts && opts->presenter_nv12_updated) {
			if (!opts->presenter_nv12) {
				set_err("presenter_nv12_updated without presenter_nv12");
				return -1;
			}
			const int psw = opts->presenter_nv12_src_w;
			const int psh = opts->presenter_nv12_src_h;
			if ((psw > 0) != (psh > 0)) {
				set_err("presenter_nv12_src_w/h must both be 0 or both >0");
				return -1;
			}
			if (psw > 0 && psh > 0) {
				double scale = std::min(static_cast<double>(p->pip_ow) / psw, static_cast<double>(p->pip_oh) / psh);
				int cur_h = static_cast<int>(psh * scale);
				int cur_w = static_cast<int>(psw * scale * p->pip_width_stretch_factor);
				if (cur_w > p->pip_ow) {
					cur_w = p->pip_ow;
				}
				cur_ow = (cur_w / 16) * 16;
				cur_oh = (cur_h / 2) * 2;
				if (cur_ow <= 0) cur_ow = 16;
				if (cur_oh <= 0) cur_oh = 2;
				cur_x = (p->pip_x + (p->pip_ow - cur_ow) / 2) & ~1;
				cur_y = p->pip_y + (p->pip_oh - cur_oh);

				const size_t cur_need = static_cast<size_t>(cur_ow) * static_cast<size_t>(cur_oh) * 3 / 2;
				if (p->overlay_nv12_single.size() != cur_need) {
					p->overlay_nv12_single.resize(cur_need);
				}
				if (!pip_hw_nv12_resize_virtual(opts->presenter_nv12, psw, psh, p->overlay_nv12_single.data(),
				                                cur_ow, cur_oh)) {
					set_err("presenter_nv12 resize failed");
					return -1;
				}
				used_bg_for_presenter = true;
			} else {
				if (p->overlay_nv12_single.size() != need) {
					p->overlay_nv12_single.resize(need);
				}
				std::memcpy(p->overlay_nv12_single.data(), opts->presenter_nv12, need);
			}
			p->presenter_last_update_ms = frame_now_ms;
			p->presenter_has_valid = true;
			ov_ptr = p->overlay_nv12_single.data();
		} else {
			if (p->presenter_preloaded_jpeg) {
				p->presenter_last_update_ms = frame_now_ms;
				p->presenter_has_valid = true;
				ov_ptr = p->overlay_nv12_single.data();
			} else {
				ov_ptr = my_uvc_pip::pip_presenter_overlay_ptr(p->overlay_nv12_single, p->presenter_has_valid,
				                                               p->presenter_last_update_ms, frame_now_ms,
				                                               p->stale_timeout_cfg_ms);
			}

			if (!ov_ptr && p->overlay_dir_lazy) {
				const size_t n = p->overlay_src_paths.size();
				if (n == 0) {
					set_err("overlay dir empty");
					return -1;
				}
				const size_t dir_slot = p->overlay_idx % n;
				if (!ensure_overlay_dir_slot(p, dir_slot)) {
					set_err("overlay slot decode failed");
					return -1;
				}
				ov_ptr = p->overlay_nv12_cache[dir_slot].data();
			}
		}
	}

	std::vector<PipHwNv12Blit> blits;
	blits.reserve((ov_ptr ? 1u : 0u) + static_cast<size_t>(std::max(0, na)));
	for (int i = 0; i < na; i++) {
		const my_uvc_pip::PipTileRect &tr = p->tile_rects[static_cast<size_t>(i)];
		int tow = 0;
		int toh = 0;
		if (!my_uvc_pip::pip_tile_rect_nv12_plane_wh(tr, &tow, &toh)) {
			set_err("tile %d has degenerate NV12 size", i);
			return -1;
		}
		const int tox = (tr.x + 1) & ~1;
		const int toy = (tr.y + 1) & ~1;
		const size_t tneed = static_cast<size_t>(tow) * static_cast<size_t>(toh) * 3 / 2;
		const uint8_t *blit_nv12 = nullptr;
		int isw = 0;
		int ish = 0;
		if (!tile_upd) {
			blit_nv12 = tnv[i];
			if (tile_sw && tile_sh && tile_sw[i] > 0 && tile_sh[i] > 0) {
				isw = tile_sw[i];
				ish = tile_sh[i];
			}
		} else {
			if (tile_upd[i]) {
				if (static_cast<size_t>(i) >= p->tile_nv12_cache.size()) {
					set_err("tile cache not initialized");
					return -1;
				}
				p->tile_nv12_cache[static_cast<size_t>(i)].resize(tneed);
				if (tile_sw && tile_sh && tile_sw[i] > 0 && tile_sh[i] > 0) {
					if (!pip_hw_nv12_resize_virtual(tnv[i], tile_sw[i], tile_sh[i],
					                                p->tile_nv12_cache[static_cast<size_t>(i)].data(), tow, toh)) {
						set_err("tile %d nv12 resize failed", i);
						return -1;
					}
				} else {
					std::memcpy(p->tile_nv12_cache[static_cast<size_t>(i)].data(), tnv[i], tneed);
				}
				p->tile_last_update_ms[static_cast<size_t>(i)] = frame_now_ms;
				p->tile_has_valid[static_cast<size_t>(i)] = 1;
			}
			blit_nv12 = my_uvc_pip::pip_presenter_overlay_ptr(
			    p->tile_nv12_cache[static_cast<size_t>(i)], !!p->tile_has_valid[static_cast<size_t>(i)],
			    p->tile_last_update_ms[static_cast<size_t>(i)], frame_now_ms, p->stale_timeout_cfg_ms);
		}
		if (blit_nv12)
			blits.push_back(PipHwNv12Blit{blit_nv12, tow, toh, tox, toy, isw, ish});
	}
	if (ov_ptr) {
		if (used_bg_for_presenter) {
			blits.push_back(PipHwNv12Blit{ov_ptr, cur_ow, cur_oh, cur_x, cur_y, 0, 0});
		} else {
			blits.push_back(PipHwNv12Blit{ov_ptr, p->pip_ow, p->pip_oh, p->pip_x, p->pip_y, 0, 0});
		}
	}

	PipBorderConfig bc{};
	bc.enable = p->border_enable;
	bc.radius = p->border_radius;
	bc.thickness = p->border_thickness;
	bc.y = p->border_y;
	bc.u = p->border_u;
	bc.v = p->border_v;

	p->jpeg_out.clear();
	if (!pip_hw_composite_layers(p->hw, bg_jpeg, bg_jpeg_len, blits.data(), static_cast<int>(blits.size()),
	                             &bc, &p->jpeg_out)) {
		set_err("pip_hw_composite_layers failed");
		return -1;
	}

	if (p->overlay_dir_lazy) {
		p->overlay_idx++;
		if (p->overlay_idx >= p->overlay_src_paths.size())
			p->overlay_idx = 0;
	}

	*out_jpeg = p->jpeg_out.data();
	*out_jpeg_len = p->jpeg_out.size();
	return 0;
}

extern "C" int pip_helper_composite_nv12_background(pip_helper_t *h, const uint8_t *bg_nv12, int bg_w, int bg_h,
                                                    const pip_helper_composite_opts_t *opts, const uint8_t **out_jpeg,
                                                    size_t *out_jpeg_len)
{
	g_err[0] = '\0';
	if (!h || !bg_nv12 || bg_w <= 0 || bg_h <= 0 || !out_jpeg || !out_jpeg_len) {
		set_err("invalid args");
		return -1;
	}
	auto *p = reinterpret_cast<PipHelperImpl *>(h);
	if (!p->ready || !p->hw) {
		set_err("helper not ready");
		return -1;
	}

	int na = 0;
	const uint8_t **tnv = nullptr;
	if (opts) {
		na = opts->n_active;
		tnv = opts->tile_nv12;
	}
	if (na < 0) {
		set_err("n_active negative");
		return -1;
	}
	if (na > p->tile_n_tiles) {
		set_err("n_active > pip_tile_n_tiles");
		return -1;
	}
	if (p->tile_n_tiles == 0 && na > 0) {
		set_err("n_active set but pip_tile_n_tiles is 0");
		return -1;
	}
	if (na > 0 && !tnv) {
		set_err("tile_nv12 required when n_active > 0");
		return -1;
	}
	const int *tile_upd = (opts && opts->tile_nv12_updated) ? opts->tile_nv12_updated : nullptr;
	if (!tile_upd) {
		for (int i = 0; i < na; i++) {
			if (!tnv[i]) {
				set_err("tile_nv12[%d] is null", i);
				return -1;
			}
		}
	} else {
		for (int i = 0; i < na; i++) {
			if (tile_upd[i] && !tnv[i]) {
				set_err("tile_nv12_updated[%d] without tile_nv12", i);
				return -1;
			}
		}
	}
	const int *tile_sw = (opts && opts->tile_src_w) ? opts->tile_src_w : nullptr;
	const int *tile_sh = (opts && opts->tile_src_h) ? opts->tile_src_h : nullptr;
	if (tile_sw || tile_sh) {
		if (!tile_sw || !tile_sh) {
			set_err("tile_src_w and tile_src_h must both be null or both set");
			return -1;
		}
		for (int i = 0; i < na; i++) {
			const int tws = tile_sw[i];
			const int ths = tile_sh[i];
			if ((tws > 0) != (ths > 0)) {
				set_err("tile_src_w/h invalid at slot %d", i);
				return -1;
			}
		}
	}

	const int64_t frame_now_ms = (opts && opts->now_ms != 0) ? opts->now_ms : mono_ms_now();

	const uint8_t *ov_ptr = nullptr;
	const size_t need = static_cast<size_t>(p->pip_ow) * static_cast<size_t>(p->pip_oh) * 3 / 2;

	bool used_bg_for_presenter = false;
	int cur_ow = p->pip_ow;
	int cur_oh = p->pip_oh;
	int cur_x = p->pip_x;
	int cur_y = p->pip_y;

	if (bg_nv12 && bg_w > 0 && bg_h > 0) {
		double scale = std::min(static_cast<double>(p->pip_ow) / bg_w, static_cast<double>(p->pip_oh) / bg_h);
		int cur_h = static_cast<int>(bg_h * scale);
		int cur_w = static_cast<int>(bg_w * scale * p->pip_width_stretch_factor);
		if (cur_w > p->pip_ow) {
			cur_w = p->pip_ow;
		}
		cur_ow = (cur_w / 16) * 16;
		cur_oh = (cur_h / 2) * 2;
		if (cur_ow <= 0) cur_ow = 16;
		if (cur_oh <= 0) cur_oh = 2;
		cur_x = (p->pip_x + (p->pip_ow - cur_ow) / 2) & ~1;
		cur_y = p->pip_y + (p->pip_oh - cur_oh);

		const size_t cur_need = static_cast<size_t>(cur_ow) * static_cast<size_t>(cur_oh) * 3 / 2;
		if (p->overlay_nv12_single.size() != cur_need) {
			p->overlay_nv12_single.resize(cur_need);
		}
		if (pip_hw_nv12_resize_virtual(bg_nv12, bg_w, bg_h, p->overlay_nv12_single.data(), cur_ow, cur_oh)) {
			ov_ptr = p->overlay_nv12_single.data();
			used_bg_for_presenter = true;
		}
	}

	if (!used_bg_for_presenter) {
		if (opts && opts->presenter_nv12_updated) {
			if (!opts->presenter_nv12) {
				set_err("presenter_nv12_updated without presenter_nv12");
				return -1;
			}
			const int psw = opts->presenter_nv12_src_w;
			const int psh = opts->presenter_nv12_src_h;
			if ((psw > 0) != (psh > 0)) {
				set_err("presenter_nv12_src_w/h must both be 0 or both >0");
				return -1;
			}
			if (psw > 0 && psh > 0) {
				double scale = std::min(static_cast<double>(p->pip_ow) / psw, static_cast<double>(p->pip_oh) / psh);
				int cur_h = static_cast<int>(psh * scale);
				int cur_w = static_cast<int>(psw * scale * p->pip_width_stretch_factor);
				if (cur_w > p->pip_ow) {
					cur_w = p->pip_ow;
				}
				cur_ow = (cur_w / 16) * 16;
				cur_oh = (cur_h / 2) * 2;
				if (cur_ow <= 0) cur_ow = 16;
				if (cur_oh <= 0) cur_oh = 2;
				cur_x = (p->pip_x + (p->pip_ow - cur_ow) / 2) & ~1;
				cur_y = p->pip_y + (p->pip_oh - cur_oh);

				const size_t cur_need = static_cast<size_t>(cur_ow) * static_cast<size_t>(cur_oh) * 3 / 2;
				if (p->overlay_nv12_single.size() != cur_need) {
					p->overlay_nv12_single.resize(cur_need);
				}
				if (!pip_hw_nv12_resize_virtual(opts->presenter_nv12, psw, psh, p->overlay_nv12_single.data(),
				                                cur_ow, cur_oh)) {
					set_err("presenter_nv12 resize failed");
					return -1;
				}
				used_bg_for_presenter = true;
			} else {
				if (p->overlay_nv12_single.size() != need) {
					p->overlay_nv12_single.resize(need);
				}
				std::memcpy(p->overlay_nv12_single.data(), opts->presenter_nv12, need);
			}
			p->presenter_last_update_ms = frame_now_ms;
			p->presenter_has_valid = true;
			ov_ptr = p->overlay_nv12_single.data();
		} else {
			if (p->presenter_preloaded_jpeg) {
				p->presenter_last_update_ms = frame_now_ms;
				p->presenter_has_valid = true;
				ov_ptr = p->overlay_nv12_single.data();
			} else {
				ov_ptr = my_uvc_pip::pip_presenter_overlay_ptr(p->overlay_nv12_single, p->presenter_has_valid,
				                                               p->presenter_last_update_ms, frame_now_ms,
				                                               p->stale_timeout_cfg_ms);
			}

			if (!ov_ptr && p->overlay_dir_lazy) {
				const size_t n = p->overlay_src_paths.size();
				if (n == 0) {
					set_err("overlay dir empty");
					return -1;
				}
				const size_t dir_slot = p->overlay_idx % n;
				if (!ensure_overlay_dir_slot(p, dir_slot)) {
					set_err("overlay slot decode failed");
					return -1;
				}
				ov_ptr = p->overlay_nv12_cache[dir_slot].data();
			}
		}
	}

	std::vector<PipHwNv12Blit> blits;
	blits.reserve((ov_ptr ? 1u : 0u) + static_cast<size_t>(std::max(0, na)));
	for (int i = 0; i < na; i++) {
		const my_uvc_pip::PipTileRect &tr = p->tile_rects[static_cast<size_t>(i)];
		int tow = 0;
		int toh = 0;
		if (!my_uvc_pip::pip_tile_rect_nv12_plane_wh(tr, &tow, &toh)) {
			set_err("tile %d has degenerate NV12 size", i);
			return -1;
		}
		const int tox = (tr.x + 1) & ~1;
		const int toy = (tr.y + 1) & ~1;
		const size_t tneed = static_cast<size_t>(tow) * static_cast<size_t>(toh) * 3 / 2;
		const uint8_t *blit_nv12 = nullptr;
		int isw = 0;
		int ish = 0;
		if (!tile_upd) {
			blit_nv12 = tnv[i];
			if (tile_sw && tile_sh && tile_sw[i] > 0 && tile_sh[i] > 0) {
				isw = tile_sw[i];
				ish = tile_sh[i];
			}
		} else {
			if (tile_upd[i]) {
				if (static_cast<size_t>(i) >= p->tile_nv12_cache.size()) {
					set_err("tile cache not initialized");
					return -1;
				}
				p->tile_nv12_cache[static_cast<size_t>(i)].resize(tneed);
				if (tile_sw && tile_sh && tile_sw[i] > 0 && tile_sh[i] > 0) {
					if (!pip_hw_nv12_resize_virtual(tnv[i], tile_sw[i], tile_sh[i],
					                                p->tile_nv12_cache[static_cast<size_t>(i)].data(), tow, toh)) {
						set_err("tile %d nv12 resize failed", i);
						return -1;
					}
				} else {
					std::memcpy(p->tile_nv12_cache[static_cast<size_t>(i)].data(), tnv[i], tneed);
				}
				p->tile_last_update_ms[static_cast<size_t>(i)] = frame_now_ms;
				p->tile_has_valid[static_cast<size_t>(i)] = 1;
			}
			blit_nv12 = my_uvc_pip::pip_presenter_overlay_ptr(
			    p->tile_nv12_cache[static_cast<size_t>(i)], !!p->tile_has_valid[static_cast<size_t>(i)],
			    p->tile_last_update_ms[static_cast<size_t>(i)], frame_now_ms, p->stale_timeout_cfg_ms);
		}
		if (blit_nv12)
			blits.push_back(PipHwNv12Blit{blit_nv12, tow, toh, tox, toy, isw, ish});
	}
	if (ov_ptr) {
		if (used_bg_for_presenter) {
			blits.push_back(PipHwNv12Blit{ov_ptr, cur_ow, cur_oh, cur_x, cur_y, 0, 0});
		} else {
			blits.push_back(PipHwNv12Blit{ov_ptr, p->pip_ow, p->pip_oh, p->pip_x, p->pip_y, 0, 0});
		}
	}

	PipBorderConfig bc{};
	bc.enable = p->border_enable;
	bc.radius = p->border_radius;
	bc.thickness = p->border_thickness;
	bc.y = p->border_y;
	bc.u = p->border_u;
	bc.v = p->border_v;

	p->jpeg_out.clear();
	if (!pip_hw_composite_layers_nv12(p->hw, bg_nv12, bg_w, bg_h, blits.data(), static_cast<int>(blits.size()),
	                                  &bc, &p->jpeg_out)) {
		set_err("pip_hw_composite_layers_nv12 failed");
		return -1;
	}

	if (p->overlay_dir_lazy) {
		p->overlay_idx++;
		if (p->overlay_idx >= p->overlay_src_paths.size())
			p->overlay_idx = 0;
	}

	*out_jpeg = p->jpeg_out.data();
	*out_jpeg_len = p->jpeg_out.size();
	return 0;
}
