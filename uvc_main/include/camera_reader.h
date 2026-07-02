#pragma once

#include <cstdint>
#include <string>
#include "image_utils.h"

namespace my_app {

struct ZeroCopyFrame {
  long long frame_index = 0;
  int ch0_fd = -1;
  int ch1_fd = -1;
  int ch2_fd = -1;
  int ch0_w = 0;
  int ch0_h = 0;
  int ch1_w = 0;
  int ch1_h = 0;
  int ch2_w = 0;
  int ch2_h = 0;
  void* opaque_frame0 = nullptr;
  void* opaque_frame1 = nullptr;
  void* opaque_frame2 = nullptr;
  void* ch1_vir = nullptr;
};

class CameraReader {
public:
	virtual ~CameraReader() = default;
	virtual int Open(int width, int height, const std::string& node, int fps, int camera_width = 0, int camera_height = 0, bool vo_enable = true) = 0;
	virtual void Close() = 0;
	virtual int ReadNextRgbInto(image_buffer_t* out, int timeout_ms) = 0;
	virtual const uint8_t* GetLastNv12Data() const = 0;
	virtual int width() const = 0;
	virtual int height() const = 0;
	virtual int fps() const = 0;
	virtual long long frame_index() const = 0;

	virtual int GetZeroCopyFrame(ZeroCopyFrame* out_frame, int timeout_ms) {
		(void)out_frame; (void)timeout_ms; return -1;
	}
	virtual void ReleaseZeroCopyFrame(ZeroCopyFrame* frame) {
		(void)frame;
	}
};

} // namespace my_app
