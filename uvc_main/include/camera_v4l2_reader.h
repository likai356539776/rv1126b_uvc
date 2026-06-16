#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <turbojpeg.h>
#include "camera_reader.h"

namespace my_app {

class CameraV4l2RgbReader : public CameraReader {
public:
	CameraV4l2RgbReader();
	~CameraV4l2RgbReader() override;

	CameraV4l2RgbReader(const CameraV4l2RgbReader&) = delete;
	CameraV4l2RgbReader& operator=(const CameraV4l2RgbReader&) = delete;

	int Open(int width, int height, const std::string& node, int fps) override;
	void Close() override;
	int ReadNextRgbInto(image_buffer_t* out, int timeout_ms) override;
	const uint8_t* GetLastNv12Data() const override { return nv12_tight_.data(); }

	int width() const override { return width_; }
	int height() const override { return height_; }
	int fps() const override { return fps_; }
	long long frame_index() const override { return frame_index_; }

private:
	struct Buffer {
		void* start;
		size_t length;
	};

	int fd_ = -1;
	int width_ = 0;
	int height_ = 0;
	int fps_ = 0;
	std::string node_;
	long long frame_index_ = 0;

	std::vector<Buffer> buffers_;
	std::vector<uint8_t> nv12_tight_;

	tjhandle decompressor_ = nullptr;
	mutable std::mutex mutex_;
};

} // namespace my_app
