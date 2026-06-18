#pragma once

#include <cstdint>
#include <string>
#include "image_utils.h"

namespace my_app {

class CameraReader {
public:
	virtual ~CameraReader() = default;
	virtual int Open(int width, int height, const std::string& node, int fps, int camera_width = 0, int camera_height = 0) = 0;
	virtual void Close() = 0;
	virtual int ReadNextRgbInto(image_buffer_t* out, int timeout_ms) = 0;
	virtual const uint8_t* GetLastNv12Data() const = 0;
	virtual int width() const = 0;
	virtual int height() const = 0;
	virtual int fps() const = 0;
	virtual long long frame_index() const = 0;
};

} // namespace my_app
