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

namespace my_app {

CameraV4l2RgbReader::CameraV4l2RgbReader() = default;

CameraV4l2RgbReader::~CameraV4l2RgbReader() {
	Close();
}

int CameraV4l2RgbReader::Open(int width, int height, const std::string& node, int fps) {
	std::lock_guard<std::mutex> lock(mutex_);
	Close();

	node_ = node;
	width_ = width;
	height_ = height;
	fps_ = fps;
	frame_index_ = 0;

	APP_LOGI("v4l2_camera: Opening device %s with resolution %dx%d @ %d fps...\n",
	         node_.c_str(), width_, height_, fps_);

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

	v4l2_format fmt{};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width_;
	fmt.fmt.pix.height = height_;
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
		         "Please ensure the camera supports MJPEG at the configured resolution.\n",
		         fourcc_str);
		Close();
		return -1;
	}

	if (fmt.fmt.pix.width != (uint32_t)width_ || fmt.fmt.pix.height != (uint32_t)height_) {
		APP_LOGW("v4l2_camera: Resolution adjusted by driver from %dx%d to %ux%u\n",
		         width_, height_, fmt.fmt.pix.width, fmt.fmt.pix.height);
		width_ = fmt.fmt.pix.width;
		height_ = fmt.fmt.pix.height;
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

	decompressor_ = tjInitDecompress();
	if (!decompressor_) {
		APP_LOGE("v4l2_camera: Failed to initialize turbojpeg decompressor\n");
		Close();
		return -1;
	}

	nv12_tight_.resize(width_ * height_ * 3 / 2);
	APP_LOGI("v4l2_camera: Camera initialized successfully on %s\n", node_.c_str());
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

	if (decompressor_) {
		tjDestroy(decompressor_);
		decompressor_ = nullptr;
	}
}

int CameraV4l2RgbReader::ReadNextRgbInto(image_buffer_t* out, int timeout_ms) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (fd_ < 0 || !decompressor_) {
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

	int w = 0, h = 0, jpeg_subsamp = 0, jpeg_colorspace = 0;
	if (tjDecompressHeader3(decompressor_, jpeg_buf, jpeg_size, &w, &h, &jpeg_subsamp, &jpeg_colorspace) != 0) {
		if (tjGetErrorCode(decompressor_) == TJERR_FATAL) {
			APP_LOGE("v4l2_camera: tjDecompressHeader3 failed: %s\n", tjGetErrorStr2(decompressor_));
			ioctl(fd_, VIDIOC_QBUF, &buf);
			return -1;
		} else {
			APP_LOGW("v4l2_camera: tjDecompressHeader3 warning: %s\n", tjGetErrorStr2(decompressor_));
		}
	}

	if (w != width_ || h != height_) {
		if (w > 0 && h > 0) {
			APP_LOGE("v4l2_camera: Frame size mismatch, expected %dx%d, got %dx%d\n", width_, height_, w, h);
			ioctl(fd_, VIDIOC_QBUF, &buf);
			return -1;
		}
	}

	if (tjDecompress2(decompressor_, jpeg_buf, jpeg_size, out->virt_addr, width_, 0, height_, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
		if (tjGetErrorCode(decompressor_) == TJERR_FATAL) {
			APP_LOGE("v4l2_camera: tjDecompress2 failed: %s\n", tjGetErrorStr2(decompressor_));
			ioctl(fd_, VIDIOC_QBUF, &buf);
			return -1;
		} else {
			// Log warning but do not abort frame
			APP_LOGW("v4l2_camera: tjDecompress2 warning: %s\n", tjGetErrorStr2(decompressor_));
		}
	}

	image_buffer_t src_rgb{};
	src_rgb.width = width_;
	src_rgb.height = height_;
	src_rgb.format = IMAGE_FORMAT_RGB888;
	src_rgb.virt_addr = out->virt_addr;
	src_rgb.size = width_ * height_ * 3;

	image_buffer_t dst_nv12{};
	dst_nv12.width = width_;
	dst_nv12.height = height_;
	dst_nv12.format = IMAGE_FORMAT_YUV420SP_NV12;
	dst_nv12.virt_addr = nv12_tight_.data();
	dst_nv12.size = width_ * height_ * 3 / 2;

	if (convert_image(&src_rgb, &dst_nv12, nullptr, nullptr, 0) != 0) {
		APP_LOGE("v4l2_camera: convert_image RGB->NV12 fail\n");
		ioctl(fd_, VIDIOC_QBUF, &buf);
		return -1;
	}

	if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
		APP_LOGE("v4l2_camera: VIDIOC_QBUF failed: %s\n", strerror(errno));
		return -1;
	}

	frame_index_++;
	return 0;
}

} // namespace my_app
