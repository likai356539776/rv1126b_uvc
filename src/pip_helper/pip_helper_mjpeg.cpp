#include "my_uvc_pip/pip_helper.h"

#include "mpp_jpeg.h"
#include "pip_mjpeg.h"

#include <algorithm>
#include <atomic>
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

namespace fs = std::filesystem;

thread_local char g_err[512];

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
};

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
	if (!cfg->pip_overlay_path || !cfg->pip_overlay_path[0]) {
		set_err("pip_overlay_path empty");
		return nullptr;
	}
	if (cfg->pip_w <= 0 || cfg->pip_h <= 0) {
		set_err("pip_w/pip_h invalid");
		return nullptr;
	}

	auto *p = new (std::nothrow) PipHelperImpl{};
	if (!p) {
		set_err("oom");
		return nullptr;
	}
	p->channel_id = channel_id;
	p->pip_ow = (cfg->pip_w + 1) & ~1;
	p->pip_oh = (cfg->pip_h + 1) & ~1;
	p->pip_x = cfg->pip_x;
	p->pip_y = cfg->pip_y;

	if (!pip_hw_init(&p->hw, cfg->canvas_width, cfg->canvas_height, cfg->pip_jpeg_quality)) {
		set_err("pip_hw_init failed");
		delete p;
		return nullptr;
	}

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
		if (!read_file_all(overlay_path, &ov_jpg)) {
			set_err("cannot read overlay file");
			pip_hw_deinit(p->hw);
			delete p;
			return nullptr;
		}
		std::vector<uint8_t> ov_dec;
		int ojw = 0, ojh = 0;
		if (!pip_mjpeg_decode_jpeg_rgb(ov_jpg.data(), ov_jpg.size(), &ov_dec, &ojw, &ojh)) {
			set_err("overlay JPEG decode failed: %s", pip_mjpeg_last_error());
			pip_hw_deinit(p->hw);
			delete p;
			return nullptr;
		}
		std::vector<uint8_t> scaled_rgb(static_cast<size_t>(p->pip_ow * p->pip_oh * 3));
		pip_mjpeg_scale_rgb_bilinear(ov_dec.data(), ojw, ojh, scaled_rgb.data(), p->pip_ow, p->pip_oh);
		if (!pip_hw_rgb_to_nv12(scaled_rgb.data(), p->pip_ow, p->pip_oh, &p->overlay_nv12_single)) {
			set_err("overlay RGB→NV12 failed");
			pip_hw_deinit(p->hw);
			delete p;
			return nullptr;
		}
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

	const uint8_t *ov_ptr = p->overlay_nv12_single.data();
	size_t dir_slot = 0;
	if (p->overlay_dir_lazy) {
		const size_t n = p->overlay_src_paths.size();
		if (n == 0) {
			set_err("overlay dir empty");
			return -1;
		}
		dir_slot = p->overlay_idx % n;
		if (!ensure_overlay_dir_slot(p, dir_slot)) {
			set_err("overlay slot decode failed");
			return -1;
		}
		ov_ptr = p->overlay_nv12_cache[dir_slot].data();
	}

	p->jpeg_out.clear();
	if (!pip_hw_composite(p->hw, bg_jpeg, bg_jpeg_len, ov_ptr, p->pip_ow, p->pip_oh, p->pip_x, p->pip_y,
	                      &p->jpeg_out)) {
		set_err("pip_hw_composite failed");
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
