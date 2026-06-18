#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <turbojpeg.h>
#include "camera_reader.h"

extern "C" {
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>
}

namespace my_app {

class CameraV4l2RgbReader : public CameraReader {
public:
	CameraV4l2RgbReader();
	~CameraV4l2RgbReader() override;

	CameraV4l2RgbReader(const CameraV4l2RgbReader&) = delete;
	CameraV4l2RgbReader& operator=(const CameraV4l2RgbReader&) = delete;

	int Open(int width, int height, const std::string& node, int fps, int camera_width = 0, int camera_height = 0) override;
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

	bool InitMppDecoder(size_t max_jpeg_size);
	bool DecodeJpegToMppFrame(const uint8_t *jpeg_data, size_t jpeg_len, MppFrame *out_frame);

	int fd_ = -1;
	int width_ = 0;
	int height_ = 0;
	int fps_ = 0;
	std::string node_;
	long long frame_index_ = 0;

	std::vector<Buffer> buffers_;
	std::vector<uint8_t> nv12_tight_;

	// MPP decoder members
	MppCtx dec_ctx_ = nullptr;
	MppApi *dec_mpi_ = nullptr;
	MppBufferGroup buf_grp_ = nullptr;
	MppBufferGroup dec_frm_grp_ = nullptr;
	MppBuffer dec_input_buf_ = nullptr;
	size_t dec_input_buf_size_ = 0;
	bool dec_info_change_done_ = false;

	int capture_width_ = 0;
	int capture_height_ = 0;

	mutable std::mutex mutex_;
};

} // namespace my_app
