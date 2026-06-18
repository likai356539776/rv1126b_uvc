#include "camera_v4l2_reader.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <cstring>
#include <errno.h>
#include "app_log.h"

extern "C" {
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/rk_mpi_cmd.h>
}

namespace my_app {

CameraV4l2RgbReader::CameraV4l2RgbReader() = default;

CameraV4l2RgbReader::~CameraV4l2RgbReader() {
	Close();
}

static void LogCameraCapabilities(int fd, const std::string& node) {
	v4l2_fmtdesc fmtdesc{};
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmtdesc.index = 0;

	APP_LOGI("v4l2_camera: Probing capabilities for %s:\n", node.c_str());
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
		char fourcc_str[5] = {0};
		std::memcpy(fourcc_str, &fmtdesc.pixelformat, 4);
		APP_LOGI("  Format [%d]: %s (%s)\n", fmtdesc.index, fourcc_str, fmtdesc.description);

		v4l2_frmsizeenum frmsize{};
		frmsize.pixel_format = fmtdesc.pixelformat;
		frmsize.index = 0;
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
			if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
				APP_LOGI("    Resolution [%d]: %dx%d\n", frmsize.index, frmsize.discrete.width, frmsize.discrete.height);
			} else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
				APP_LOGI("    Resolution [%d]: %dx%d to %dx%d (step %dx%d)\n",
				         frmsize.index,
				         frmsize.stepwise.min_width, frmsize.stepwise.min_height,
				         frmsize.stepwise.max_width, frmsize.stepwise.max_height,
				         frmsize.stepwise.step_width, frmsize.stepwise.step_height);
			}
			frmsize.index++;
		}
		fmtdesc.index++;
	}
}

static bool QueryMaxMjpegResolution(int fd, int &max_w, int &max_h, int limit_w, int limit_h) {
	v4l2_fmtdesc fmtdesc{};
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmtdesc.index = 0;

	bool found_mjpeg = false;
	max_w = 0;
	max_h = 0;
	int max_pixels = 0;

	int max_allowed_w = (limit_w > 0) ? limit_w : 3840;
	int max_allowed_h = (limit_h > 0) ? limit_h : 2160;

	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
		if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
			found_mjpeg = true;
			v4l2_frmsizeenum frmsize{};
			frmsize.pixel_format = fmtdesc.pixelformat;
			frmsize.index = 0;
			while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
				if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
					int w = frmsize.discrete.width;
					int h = frmsize.discrete.height;
					if (w <= max_allowed_w && h <= max_allowed_h) {
						int pixels = w * h;
						if (pixels > max_pixels || (pixels == max_pixels && w > max_w)) {
							max_pixels = pixels;
							max_w = w;
							max_h = h;
						}
					}
				} else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
					int w = frmsize.stepwise.max_width;
					int h = frmsize.stepwise.max_height;
					if (w <= max_allowed_w && h <= max_allowed_h) {
						int pixels = w * h;
						if (pixels > max_pixels || (pixels == max_pixels && w > max_w)) {
							max_pixels = pixels;
							max_w = w;
							max_h = h;
						}
					}
				}
				frmsize.index++;
			}
		}
		fmtdesc.index++;
	}

	if (found_mjpeg && max_pixels == 0) {
		int min_pixels = 2000000000;
		fmtdesc.index = 0;
		while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
			if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
				v4l2_frmsizeenum frmsize{};
				frmsize.pixel_format = fmtdesc.pixelformat;
				frmsize.index = 0;
				while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
					if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
						int w = frmsize.discrete.width;
						int h = frmsize.discrete.height;
						int pixels = w * h;
						if (pixels < min_pixels) {
							min_pixels = pixels;
							max_w = w;
							max_h = h;
						}
					}
					frmsize.index++;
				}
			}
			fmtdesc.index++;
		}
		if (min_pixels < 2000000000) {
			max_pixels = min_pixels;
		}
	}
	return found_mjpeg && max_pixels > 0;
}

bool CameraV4l2RgbReader::InitMppDecoder(size_t max_jpeg_size) {
	MPP_RET ret;
	ret = mpp_create(&dec_ctx_, &dec_mpi_);
	if (ret != MPP_OK) {
		APP_LOGE("v4l2_camera: mpp_create(dec) failed: %d\n", ret);
		return false;
	}
	ret = dec_mpi_->control(dec_ctx_, MPP_SET_DISABLE_THREAD, NULL);
	if (ret != MPP_OK) {
		APP_LOGW("v4l2_camera: MPP_SET_DISABLE_THREAD warning: %d\n", ret);
	}
	ret = mpp_init(dec_ctx_, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
	if (ret != MPP_OK) {
		APP_LOGE("v4l2_camera: mpp_init(dec MJPEG) failed: %d\n", ret);
		return false;
	}
	MppFrameFormat fmt = MPP_FMT_YUV420SP;
	ret = dec_mpi_->control(dec_ctx_, MPP_DEC_SET_OUTPUT_FORMAT, &fmt);
	if (ret != MPP_OK) {
		APP_LOGW("v4l2_camera: MPP_DEC_SET_OUTPUT_FORMAT warning: %d\n", ret);
	}

	ret = mpp_buffer_group_get_internal(&buf_grp_, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
	if (ret != MPP_OK) {
		APP_LOGE("v4l2_camera: mpp_buffer_group_get_internal failed: %d\n", ret);
		return false;
	}

	dec_input_buf_size_ = max_jpeg_size;
	ret = mpp_buffer_get(buf_grp_, &dec_input_buf_, dec_input_buf_size_);
	if (ret != MPP_OK) {
		APP_LOGE("v4l2_camera: mpp_buffer_get for input failed: %d\n", ret);
		return false;
	}
	dec_info_change_done_ = false;
	return true;
}

bool CameraV4l2RgbReader::DecodeJpegToMppFrame(const uint8_t *jpeg_data, size_t jpeg_len, MppFrame *out_frame) {
	if (jpeg_len > dec_input_buf_size_) {
		APP_LOGE("v4l2_camera: jpeg_len %zu exceeds dec_input_buf_size_ %zu\n", jpeg_len, dec_input_buf_size_);
		return false;
	}

	void *input_ptr = mpp_buffer_get_ptr(dec_input_buf_);
	std::memcpy(input_ptr, jpeg_data, jpeg_len);

	MppPacket pkt = nullptr;
	MPP_RET ret = mpp_packet_init(&pkt, input_ptr, jpeg_len);
	if (ret != MPP_OK) {
		APP_LOGE("v4l2_camera: mpp_packet_init failed: %d\n", ret);
		return false;
	}
	mpp_packet_set_buffer(pkt, dec_input_buf_);

	int max_tries = 12;
	for (int attempt = 0; attempt < max_tries; attempt++) {
		MppFrame frame = nullptr;
		ret = dec_mpi_->decode(dec_ctx_, pkt, &frame);
		if (ret != MPP_OK) {
			APP_LOGE("v4l2_camera: decode() failed: %d (attempt %d)\n", ret, attempt);
			if (frame) mpp_frame_deinit(&frame);
			break;
		}

		if (!frame)
			continue;

		if (mpp_frame_get_info_change(frame)) {
			RK_U32 width = mpp_frame_get_width(frame);
			RK_U32 height = mpp_frame_get_height(frame);
			RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
			RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
			RK_U32 buf_size = mpp_frame_get_buf_size(frame);

			APP_LOGI("v4l2_camera: dec info_change: %ux%u stride %ux%u buf_size %u\n",
			         width, height, hor_stride, ver_stride, buf_size);

			const uint64_t page = 4096u;
			const uint64_t extra_pages = 102u;
			uint64_t need = static_cast<uint64_t>(buf_size);
			const uint64_t min_nv12 = static_cast<uint64_t>(hor_stride) * static_cast<uint64_t>(ver_stride) * 3u / 2u;
			if (min_nv12 > need)
				need = min_nv12;
			uint64_t pages = (need + page - 1u) / page + extra_pages;
			const size_t lim = static_cast<size_t>(pages * page);

			APP_LOGI("v4l2_camera: dec buffer pool limit_config size %zu (buf_size %u)\n", lim, buf_size);

			if (dec_frm_grp_) {
				mpp_buffer_group_put(dec_frm_grp_);
				dec_frm_grp_ = nullptr;
			}

			ret = mpp_buffer_group_get_internal(&dec_frm_grp_, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
			if (ret != MPP_OK) {
				APP_LOGE("v4l2_camera: dec buffer group alloc failed: %d\n", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = mpp_buffer_group_limit_config(dec_frm_grp_, lim, 24);
			if (ret != MPP_OK) {
				APP_LOGE("v4l2_camera: dec buffer group limit_config failed: %d\n", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = dec_mpi_->control(dec_ctx_, MPP_DEC_SET_EXT_BUF_GROUP, dec_frm_grp_);
			if (ret != MPP_OK) {
				APP_LOGE("v4l2_camera: MPP_DEC_SET_EXT_BUF_GROUP failed: %d\n", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			ret = dec_mpi_->control(dec_ctx_, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
			if (ret != MPP_OK) {
				APP_LOGE("v4l2_camera: MPP_DEC_SET_INFO_CHANGE_READY failed: %d\n", ret);
				mpp_frame_deinit(&frame);
				mpp_packet_deinit(&pkt);
				return false;
			}

			dec_info_change_done_ = true;
			mpp_frame_deinit(&frame);

			mpp_packet_set_pos(pkt, input_ptr);
			mpp_packet_set_length(pkt, jpeg_len);
			continue;
		}

		RK_U32 err = mpp_frame_get_errinfo(frame);
		if (err) {
			APP_LOGE("v4l2_camera: decode frame error: 0x%x\n", err);
			mpp_frame_deinit(&frame);
			mpp_packet_deinit(&pkt);
			return false;
		}

		mpp_packet_deinit(&pkt);
		*out_frame = frame;
		return true;
	}

	if (pkt) mpp_packet_deinit(&pkt);
	return false;
}

int CameraV4l2RgbReader::Open(int width, int height, const std::string& node, int fps, int camera_width, int camera_height) {
	std::lock_guard<std::mutex> lock(mutex_);
	Close();

	node_ = node;
	width_ = width;
	height_ = height;
	fps_ = fps;
	frame_index_ = 0;

	APP_LOGI("v4l2_camera: Opening device %s with target resolution %dx%d @ %d fps, camera limit %dx%d...\n",
	         node_.c_str(), width_, height_, fps_, camera_width, camera_height);

	fd_ = open(node_.c_str(), O_RDWR | O_NONBLOCK, 0);
	if (fd_ < 0) {
		APP_LOGE("v4l2_camera: Failed to open %s: %s\n", node_.c_str(), strerror(errno));
		return -1;
	}

	v4l2_capability cap{};
	if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_QUERYCAP failed: %s\n", strerror(errno));
		Close();
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		APP_LOGE("v4l2_camera: %s is not a video capture device\n", node_.c_str());
		Close();
		return -1;
	}

	LogCameraCapabilities(fd_, node_);

	// Query maximum supported MJPEG resolution with configured limits
	capture_width_ = width_;
	capture_height_ = height_;
	int max_w = 0, max_h = 0;
	if (QueryMaxMjpegResolution(fd_, max_w, max_h, camera_width, camera_height)) {
		capture_width_ = max_w;
		capture_height_ = max_h;
		APP_LOGI("v4l2_camera: Probed maximum MJPEG resolution: %dx%d. Target resolution: %dx%d\n",
		         capture_width_, capture_height_, width_, height_);
	} else {
		APP_LOGW("v4l2_camera: QueryMaxMjpegResolution failed or MJPEG format not supported. Falling back to target resolution %dx%d\n",
		         width_, height_);
	}

	v4l2_format fmt{};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = capture_width_;
	fmt.fmt.pix.height = capture_height_;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_S_FMT failed: %s\n", strerror(errno));
		Close();
		return -1;
	}

	if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
		char fourcc_str[5] = {0};
		std::memcpy(fourcc_str, &fmt.fmt.pix.pixelformat, 4);
		APP_LOGE("v4l2_camera: Device did not accept MJPEG format. Negotiated format: %s. "
		         "Please ensure the camera supports MJPEG.\n",
		         fourcc_str);
		Close();
		return -1;
	}

	if (fmt.fmt.pix.width != (uint32_t)capture_width_ || fmt.fmt.pix.height != (uint32_t)capture_height_) {
		APP_LOGW("v4l2_camera: Resolution adjusted by driver from %dx%d to %ux%u\n",
		         capture_width_, capture_height_, fmt.fmt.pix.width, fmt.fmt.pix.height);
		capture_width_ = fmt.fmt.pix.width;
		capture_height_ = fmt.fmt.pix.height;
	}

	v4l2_streamparm parm{};
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = fps_;
	if (ioctl(fd_, VIDIOC_S_PARM, &parm) < 0) {
		APP_LOGW("v4l2_camera: VIDIOC_S_PARM failed: %s\n", strerror(errno));
	}

	v4l2_requestbuffers req{};
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_REQBUFS failed: %s\n", strerror(errno));
		Close();
		return -1;
	}

	buffers_.resize(req.count);
	for (uint32_t i = 0; i < req.count; ++i) {
		v4l2_buffer buf{};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
			APP_LOGE("v4l2_camera: VIDIOC_QUERYBUF failed for buffer %u: %s\n", i, strerror(errno));
			Close();
			return -1;
		}

		buffers_[i].length = buf.length;
		buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

		if (buffers_[i].start == MAP_FAILED) {
			APP_LOGE("v4l2_camera: mmap failed for buffer %u: %s\n", i, strerror(errno));
			Close();
			return -1;
		}
	}

	for (uint32_t i = 0; i < req.count; ++i) {
		v4l2_buffer buf{};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
			APP_LOGE("v4l2_camera: VIDIOC_QBUF failed for buffer %u: %s\n", i, strerror(errno));
			Close();
			return -1;
		}
	}

	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_STREAMON failed: %s\n", strerror(errno));
		Close();
		return -1;
	}

	size_t max_jpeg_size = std::max<size_t>(8 * 1024 * 1024, (size_t)capture_width_ * capture_height_ * 3 / 2);
	if (!InitMppDecoder(max_jpeg_size)) {
		APP_LOGE("v4l2_camera: Failed to initialize MPP decoder\n");
		Close();
		return -1;
	}

	nv12_tight_.resize(width_ * height_ * 3 / 2);
	APP_LOGI("v4l2_camera: Camera initialized successfully on %s at %dx%d (max)\n", node_.c_str(), capture_width_, capture_height_);
	return 0;
}

void CameraV4l2RgbReader::Close() {
	if (fd_ >= 0) {
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ioctl(fd_, VIDIOC_STREAMOFF, &type);

		for (auto& buf : buffers_) {
			if (buf.start && buf.start != MAP_FAILED) {
				munmap(buf.start, buf.length);
			}
		}
		buffers_.clear();

		close(fd_);
		fd_ = -1;
	}

	if (dec_input_buf_) {
		mpp_buffer_put(dec_input_buf_);
		dec_input_buf_ = nullptr;
	}
	if (dec_frm_grp_) {
		mpp_buffer_group_put(dec_frm_grp_);
		dec_frm_grp_ = nullptr;
	}
	if (buf_grp_) {
		mpp_buffer_group_put(buf_grp_);
		buf_grp_ = nullptr;
	}
	if (dec_ctx_) {
		mpp_destroy(dec_ctx_);
		dec_ctx_ = nullptr;
		dec_mpi_ = nullptr;
	}
}

int CameraV4l2RgbReader::ReadNextRgbInto(image_buffer_t* out, int timeout_ms) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (fd_ < 0 || !dec_ctx_ || !dec_mpi_) {
		return -1;
	}

	pollfd pfd{};
	pfd.fd = fd_;
	pfd.events = POLLIN;

	int r = poll(&pfd, 1, timeout_ms);
	if (r < 0) {
		APP_LOGE("v4l2_camera: poll failed: %s\n", strerror(errno));
		return -1;
	} else if (r == 0) {
		return -2; // timeout
	}

	v4l2_buffer buf{};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_DQBUF failed: %s\n", strerror(errno));
		return -1;
	}

	uint8_t* jpeg_buf = static_cast<uint8_t*>(buffers_[buf.index].start);
	unsigned long jpeg_size = buf.bytesused;

	// Check for valid JPEG SOI marker (0xFFD8) to skip corrupt camera init frames
	if (jpeg_size < 4 || jpeg_buf[0] != 0xFF || jpeg_buf[1] != 0xD8) {
		APP_LOGW("v4l2_camera: Ignored corrupt camera frame (starts with 0x%02x 0x%02x, size=%lu)\n",
		         jpeg_size > 0 ? jpeg_buf[0] : 0, jpeg_size > 1 ? jpeg_buf[1] : 0, jpeg_size);
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -3;
	}

	MppFrame decoded_frame = nullptr;
	if (!DecodeJpegToMppFrame(jpeg_buf, jpeg_size, &decoded_frame)) {
		APP_LOGE("v4l2_camera: DecodeJpegToMppFrame failed, resetting decoder\n");
		dec_mpi_->reset(dec_ctx_);
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -3;
	}

	int dec_w = mpp_frame_get_width(decoded_frame);
	int dec_h = mpp_frame_get_height(decoded_frame);
	int hor_stride = mpp_frame_get_hor_stride(decoded_frame);
	int ver_stride = mpp_frame_get_ver_stride(decoded_frame);
	void* dec_ptr = mpp_buffer_get_ptr(mpp_frame_get_buffer(decoded_frame));

	if (!dec_ptr) {
		APP_LOGE("v4l2_camera: Decoded frame buffer is null\n");
		mpp_frame_deinit(&decoded_frame);
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -3;
	}

	image_buffer_t src_nv12{};
	src_nv12.width = dec_w;
	src_nv12.height = dec_h;
	src_nv12.width_stride = hor_stride;
	src_nv12.height_stride = ver_stride;
	src_nv12.format = IMAGE_FORMAT_YUV420SP_NV12;
	src_nv12.virt_addr = static_cast<unsigned char*>(dec_ptr);
	src_nv12.size = hor_stride * ver_stride * 3 / 2;

	image_buffer_t dst_rgb{};
	dst_rgb.width = width_;
	dst_rgb.height = height_;
	dst_rgb.width_stride = width_;
	dst_rgb.height_stride = height_;
	dst_rgb.format = IMAGE_FORMAT_RGB888;
	dst_rgb.virt_addr = out->virt_addr;
	dst_rgb.size = width_ * height_ * 3;

	if (convert_image(&src_nv12, &dst_rgb, nullptr, nullptr, 0) != 0) {
		APP_LOGE("v4l2_camera: convert_image NV12->RGB888 failed\n");
		mpp_frame_deinit(&decoded_frame);
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -3;
	}

	image_buffer_t dst_nv12{};
	dst_nv12.width = width_;
	dst_nv12.height = height_;
	dst_nv12.width_stride = width_;
	dst_nv12.height_stride = height_;
	dst_nv12.format = IMAGE_FORMAT_YUV420SP_NV12;
	dst_nv12.virt_addr = nv12_tight_.data();
	dst_nv12.size = width_ * height_ * 3 / 2;

	if (convert_image(&src_nv12, &dst_nv12, nullptr, nullptr, 0) != 0) {
		APP_LOGE("v4l2_camera: convert_image NV12->NV12 failed\n");
		mpp_frame_deinit(&decoded_frame);
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -3;
	}

	mpp_frame_deinit(&decoded_frame);

	if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_QBUF failed: %s\n", strerror(errno));
		return -1;
	}

	frame_index_++;
	return 0;
}

} // namespace my_app
