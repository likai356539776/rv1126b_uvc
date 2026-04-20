/** P3-U1: bottom-third grid layout pure functions. */
#include "my_uvc_pip/pip_tile_layout.hpp"

#include <array>
#include <cstdlib>

using my_uvc_pip::PipTileLayoutSpec;
using my_uvc_pip::PipTileRect;
using my_uvc_pip::kPipTileLayoutMax;
using my_uvc_pip::pip_tile_layout_bottom_third;

int main()
{
	std::array<PipTileRect, kPipTileLayoutMax> r{};

	if (pip_tile_layout_bottom_third(PipTileLayoutSpec{}, nullptr) != -1)
		return 1;
	if (pip_tile_layout_bottom_third(PipTileLayoutSpec{960, 540, 0, 4, 2}, &r) != -1)
		return 1;
	if (pip_tile_layout_bottom_third(PipTileLayoutSpec{960, 540, 17, 4, 2}, &r) != -1)
		return 1;

	/* 4 tiles single row */
	const PipTileLayoutSpec s4{960, 540, 4, 4, 2};
	if (pip_tile_layout_bottom_third(s4, &r) != 4)
		return 1;
	const int m = 2;
	const int g = 4;
	const int band_top = 360;
	const int inner_w = 960 - 2 * m;
	const int tw = (inner_w - 3 * g) / 4;
	if (r[0].x != m || r[0].y != band_top + m || r[0].w != tw || r[0].h != 180 - 2 * m)
		return 1;
	if (r[3].x != m + 3 * (tw + g))
		return 1;

	/* 10 tiles: 8 + 2 */
	const PipTileLayoutSpec s10{1920, 1080, 10, 2, 4};
	if (pip_tile_layout_bottom_third(s10, &r) != 10)
		return 1;
	const int m10 = 4;
	const int g10 = 2;
	const int band_top10 = (2 * 1080) / 3;
	const int band_h10 = 1080 - band_top10;
	const int inner_h10 = band_h10 - 2 * m10 - g10;
	const int row_h = inner_h10 / 2;
	const int inner_w10 = 1920 - 2 * m10;
	const int exp_w1 = (inner_w10 - g10) / 2;
	if (r[0].y != band_top10 + m10 || r[0].h != row_h)
		return 1;
	if (r[8].y != band_top10 + m10 + row_h + g10 || r[8].w != exp_w1)
		return 1;
	if (r[9].x != m10 + exp_w1 + g10)
		return 1;

	/* 16 tiles */
	const PipTileLayoutSpec s16{1280, 720, 16, 0, 0};
	if (pip_tile_layout_bottom_third(s16, &r) != 16)
		return 1;
	for (int i = 0; i < 16; i++) {
		if (r[static_cast<size_t>(i)].x < 0 || r[static_cast<size_t>(i)].y < 0)
			return 1;
		if (r[static_cast<size_t>(i)].x + r[static_cast<size_t>(i)].w > 1280)
			return 1;
		if (r[static_cast<size_t>(i)].y + r[static_cast<size_t>(i)].h > 720)
			return 1;
	}

	return 0;
}
