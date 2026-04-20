#pragma once

/**
 * PiP 下三分之一网格布局（§2.4）：≤8 路单行，>8 路双行（首行 8、次行剩余），间隙与边距可配。
 * 纯函数，无 I/O，供 pip_helper 与单元测试复用。
 */
#include <array>
#include <cstddef>
#include <cstdint>

namespace my_uvc_pip {

struct PipTileRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct PipTileLayoutSpec {
	int canvas_w = 0;
	int canvas_h = 0;
	/** 网格路数 1~16 */
	int n_tiles = 0;
	int gap_px = 0;
	/** 下三分之一带内边距（相对整幅画布左右与带上下） */
	int margin_px = 0;
};

inline constexpr int kPipTileLayoutMax = 16;

/**
 * 计算各 tile 在画布上的像素矩形（左上角坐标 + 宽高）。
 * @return 写入的 tile 个数（等于 spec.n_tiles），参数非法时返回 -1。
 */
inline int pip_tile_layout_bottom_third(const PipTileLayoutSpec &spec,
                                        std::array<PipTileRect, kPipTileLayoutMax> *out)
{
	if (!out || spec.canvas_w <= 0 || spec.canvas_h <= 0)
		return -1;
	const int n = spec.n_tiles;
	if (n < 1 || n > kPipTileLayoutMax)
		return -1;
	if (spec.gap_px < 0 || spec.margin_px < 0)
		return -1;

	const int band_top = (2 * spec.canvas_h) / 3;
	const int band_h = spec.canvas_h - band_top;
	const int m = spec.margin_px;
	const int g = spec.gap_px;
	if (m * 2 > band_h || m * 2 > spec.canvas_w)
		return -1;

	if (n <= 8) {
		const int inner_w = spec.canvas_w - 2 * m;
		if (inner_w <= 0)
			return -1;
		const int tile_w = (inner_w - (n - 1) * g);
		if (tile_w <= 0 || n == 0)
			return -1;
		const int tw = tile_w / n;
		if (tw <= 0)
			return -1;
		const int tile_h = band_h - 2 * m;
		if (tile_h <= 0)
			return -1;
		for (int i = 0; i < n; i++) {
			(*out)[static_cast<size_t>(i)] = PipTileRect{m + i * (tw + g), band_top + m, tw, tile_h};
		}
		return n;
	}

	const int n0 = 8;
	const int n1 = n - n0;
	const int inner_h = band_h - 2 * m - g;
	const int row_h = inner_h / 2;
	if (row_h <= 0)
		return -1;

	const int inner_w = spec.canvas_w - 2 * m;
	const int tw0 = (inner_w - (n0 - 1) * g);
	if (tw0 <= 0)
		return -1;
	const int tile_w0 = tw0 / n0;
	if (tile_w0 <= 0)
		return -1;

	for (int i = 0; i < n0; i++) {
		(*out)[static_cast<size_t>(i)] = PipTileRect{m + i * (tile_w0 + g), band_top + m, tile_w0, row_h};
	}

	const int tw1_full = (inner_w - (n1 - 1) * g);
	if (tw1_full <= 0 || n1 <= 0)
		return -1;
	const int tile_w1 = tw1_full / n1;
	if (tile_w1 <= 0)
		return -1;
	const int y1 = band_top + m + row_h + g;
	for (int i = 0; i < n1; i++) {
		(*out)[static_cast<size_t>(8 + i)] = PipTileRect{m + i * (tile_w1 + g), y1, tile_w1, row_h};
	}
	return n;
}

} // namespace my_uvc_pip
