#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include "image_utils.h"
#include "yolov8.h"
#include "postprocess.h"
#include "my_uvc_pip/pip_tile_layout.hpp"

namespace my_app {

struct TrackedSlot {
	double cx = -1.0;
	double cy = -1.0;
	double w = 0.0;
	double h = 0.0;
	bool active = false;
	bool moving = false;
	int64_t last_seen_ms = 0;
	std::vector<uint8_t> last_nv12;
	int crop_w = 0;
	int crop_h = 0;
};

struct FrameData {
	long long frame_index = 0;
	std::vector<uint8_t> bg_nv12;
	int bg_w = 0;
	int bg_h = 0;

	std::vector<uint8_t> presenter_nv12;
	int presenter_w = 0;
	int presenter_h = 0;
	bool presenter_updated = false;

	struct Tile {
		std::vector<uint8_t> nv12;
		int w = 0;
		int h = 0;
	};
	std::vector<Tile> tiles;
};

class PersonTracker {
public:
	PersonTracker();
	~PersonTracker() = default;

	void Update(const std::vector<object_detect_result>& persons,
	            const uint8_t* nv12_data, int vw, int vh, int64_t now_ms);

	std::shared_ptr<FrameData> GenerateFrameData(long long frame_idx, const uint8_t* nv12_data,
	                                             int vw, int vh, int max_tiles) const;

private:
	void GetAlignedCropBoxCentered(int src_w, int src_h, int cx, int cy, int w, int h,
	                               image_rect_t *src_box, int *crop_w, int *crop_h) const;

	std::vector<TrackedSlot> slots_;
};

} // namespace my_app
