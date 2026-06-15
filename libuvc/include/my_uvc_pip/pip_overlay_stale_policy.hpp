#pragma once

/**
 * PiP 断流超时语义（设计 v1.12）：与 LIBMY_UVC_REFACTOR_DESIGN.md §2.4 / §3.8 一致。
 * 纯函数，无 I/O；供 pip_helper 与单测共用。
 */
#include <cstdint>
#include <vector>

namespace my_uvc_pip {

/** `-1` → 库缺省 5000 ms；`0` → 不因超时露背图（仅冻结）；`>0` → 自定义毫秒。 */
inline int32_t pip_effective_stale_timeout_ms(int32_t pip_overlay_stale_timeout_ms)
{
	if (pip_overlay_stale_timeout_ms < 0)
		return 5000;
	return pip_overlay_stale_timeout_ms;
}

/**
 * @param last_update_ms 该路最后一次有效帧的单调时间（毫秒，与 now_ms 同域）
 * @param now_ms 当前单调时间（毫秒）
 * @param pip_overlay_stale_timeout_ms_cfg create 入参（-1/0/>0）
 */
inline bool pip_stale_should_clear_to_bg(int64_t last_update_ms, int64_t now_ms,
                                         int32_t pip_overlay_stale_timeout_ms_cfg)
{
	const int32_t eff = pip_effective_stale_timeout_ms(pip_overlay_stale_timeout_ms_cfg);
	if (eff <= 0)
		return false;
	return (now_ms - last_update_ms) >= eff;
}

/**
 * 主讲人（单路）NV12：无有效缓存、或已超时露背图时返回 nullptr；否则返回待合成指针。
 * @param stale_cfg_ms create 入参 pip_overlay_stale_timeout_ms（-1 / 0 / >0）
 */
inline const uint8_t *pip_presenter_overlay_ptr(const std::vector<uint8_t> &nv12_cache, bool has_valid,
                                                int64_t last_update_ms, int64_t now_ms,
                                                int32_t stale_cfg_ms)
{
	if (!has_valid || nv12_cache.empty())
		return nullptr;
	if (pip_stale_should_clear_to_bg(last_update_ms, now_ms, stale_cfg_ms))
		return nullptr;
	return nv12_cache.data();
}

} // namespace my_uvc_pip
