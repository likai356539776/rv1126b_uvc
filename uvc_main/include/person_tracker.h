#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include "image_utils.h"
#include "yolov8.h"
#include "postprocess.h"
#include "my_uvc_pip/pip_tile_layout.hpp"

#include "camera_reader.h"

namespace my_app {

struct TrackedSlot {
	double cx = -1.0;
	double cy = -1.0;
	double w = 0.0;
	double h = 0.0;
	bool active = false;
	bool moving = false;
	int64_t last_seen_ms = 0;
	
	void* mb_blk = nullptr;
	int fd = -1;
	void* virt_addr = nullptr;

	int crop_w = 0;
	int crop_h = 0;
};

struct FrameData {
	long long frame_index = 0;
	int bg_fd = -1;
	const uint8_t* bg_virt_addr = nullptr;
	int bg_w = 0;
	int bg_h = 0;

	int presenter_fd = -1;
	const uint8_t* presenter_virt_addr = nullptr;
	int presenter_w = 0;
	int presenter_h = 0;
	bool presenter_updated = false;

	struct Tile {
		int fd = -1;
		const uint8_t* virt_addr = nullptr;
		int w = 0;
		int h = 0;
	};
	std::vector<Tile> tiles;

	ZeroCopyFrame camera_frame;
	CameraReader* camera_reader = nullptr;

	~FrameData() {
		if (camera_reader) {
			camera_reader->ReleaseZeroCopyFrame(&camera_frame);
		}
	}
};

class PersonTracker {
public:
	PersonTracker();
	~PersonTracker();

	void Update(const std::vector<object_detect_result>& persons,
	            const ZeroCopyFrame& frame, int64_t now_ms, int max_tiles = 4);

	std::shared_ptr<FrameData> GenerateFrameData(const ZeroCopyFrame& frame, CameraReader* reader, int max_tiles) const;

private:
	void GetAlignedCropBoxCentered(int src_w, int src_h, int cx, int cy, int w, int h,
	                               image_rect_t *src_box, int *crop_w, int *crop_h) const;

	std::vector<TrackedSlot> slots_;
};

} // namespace my_app
