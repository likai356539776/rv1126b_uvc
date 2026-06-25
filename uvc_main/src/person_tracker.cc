#include "person_tracker.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "app_log.h"

extern "C" {
#include "rk_mpi_mmz.h"
#include "rk_mpi_mb.h"
}

#include <rga/im2d_type.h>
#include <rga/rga.h>

extern "C" {
rga_buffer_t wrapbuffer_virtualaddr_t(void *vir_addr, int width, int height,
                                      int wstride, int hstride, int format);
rga_buffer_t wrapbuffer_fd_t(int fd, int width, int height,
                             int wstride, int hstride, int format);
IM_STATUS imresize_t(const rga_buffer_t src, rga_buffer_t dst,
                     double fx, double fy, int interpolation, int sync);
IM_STATUS imcrop_t(const rga_buffer_t src, rga_buffer_t dst,
                   im_rect rect, int sync);
}

namespace {
void nv12_crop_cpu(const uint8_t* src, int src_w, int src_h, int crop_x, int crop_y, uint8_t* dst, int crop_w, int crop_h) {
	// Crop Y plane
	const uint8_t* src_y = src;
	uint8_t* dst_y = dst;
	for (int y = 0; y < crop_h; ++y) {
		std::memcpy(dst_y + y * crop_w, src_y + (crop_y + y) * src_w + crop_x, crop_w);
	}

	// Crop UV plane (interleaved U and V)
	const uint8_t* src_uv = src + src_w * src_h;
	uint8_t* dst_uv = dst + crop_w * crop_h;
	int uv_h = crop_h / 2;
	int src_uv_stride = src_w;
	int dst_uv_stride = crop_w;
	int crop_uv_y = crop_y / 2;
	int crop_uv_x = crop_x;

	for (int y = 0; y < uv_h; ++y) {
		std::memcpy(dst_uv + y * dst_uv_stride, src_uv + (crop_uv_y + y) * src_uv_stride + crop_uv_x, crop_w);
	}
}
} // namespace

namespace my_app {

PersonTracker::PersonTracker() {
	slots_.resize(my_uvc_pip::kPipTileLayoutMax);
}

PersonTracker::~PersonTracker() {
	for (auto &slot : slots_) {
		if (slot.mb_blk) {
			RK_MPI_MMZ_Free(slot.mb_blk);
			slot.mb_blk = nullptr;
		} else if (slot.virt_addr) {
			std::free(slot.virt_addr);
			slot.virt_addr = nullptr;
		}
	}
}

void PersonTracker::GetAlignedCropBoxCentered(int src_w, int src_h, int cx, int cy, int w, int h,
                                             image_rect_t *src_box, int *crop_w, int *crop_h) const {
	// RGA NV12 requires width aligned to 16, height/x/y aligned to 2
	int aw = (w + 15) & ~15;
	int ah = (h + 1) & ~1;

	int left = cx - aw / 2;
	int top = cy - ah / 2;

	// Clamp to source bounds
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (left + aw > src_w) left = src_w - aw;
	if (top + ah > src_h) top = src_h - ah;

	// Second clamp after potential negative adjustment
	if (left < 0) { left = 0; aw = (src_w / 16) * 16; }
	if (top < 0)  { top = 0;  ah = (src_h / 2) * 2; }

	// RGA requires x and y coordinates also 2-pixel aligned for YUV formats
	left = left & ~1;
	top  = top  & ~1;

	// After aligning left/top down, ensure we don't exceed src bounds
	if (left + aw > src_w) aw = ((src_w - left) / 16) * 16;
	if (top  + ah > src_h) ah = ((src_h - top)  / 2)  * 2;

	src_box->left   = left;
	src_box->top    = top;
	src_box->right  = left + aw - 1;
	src_box->bottom = top  + ah - 1;

	*crop_w = aw;
	*crop_h = ah;
}

void PersonTracker::Update(const std::vector<object_detect_result>& persons,
                           const ZeroCopyFrame& frame, int64_t now_ms, int max_tiles) {
	// Lazy allocation of slot buffers
	int crop_size = 640 * 640 * 3 / 2;
	if (frame.ch0_fd != -1) {
		for (auto &slot : slots_) {
			if (!slot.mb_blk) {
				RK_S32 ret = RK_MPI_MMZ_Alloc((MB_BLK*)&slot.mb_blk, crop_size, 0);
				if (ret == RK_SUCCESS) {
					slot.fd = RK_MPI_MMZ_Handle2Fd(slot.mb_blk);
					slot.virt_addr = RK_MPI_MB_Handle2VirAddr(slot.mb_blk);
				} else {
					APP_LOGE("[PersonTracker] MMZ Alloc failed for slot! ret=0x%x\n", ret);
					slot.mb_blk = nullptr;
					slot.fd = -1;
					slot.virt_addr = nullptr;
				}
			}
		}
	} else {
		for (auto &slot : slots_) {
			if (!slot.virt_addr) {
				slot.virt_addr = std::malloc(crop_size);
			}
		}
	}

	int P = (int)persons.size();

	double scale_x = (frame.ch1_w > 0) ? (double)frame.ch0_w / frame.ch1_w : 1.0;
	double scale_y = (frame.ch1_h > 0) ? (double)frame.ch0_h / frame.ch1_h : 1.0;

	std::vector<int> person_matched_to_slot(P, -1);
	std::vector<bool> slot_occupied(slots_.size(), false);

	if (P > 0) {
		struct PersonCenter {
			double cx;
			double cy;
		};
		std::vector<PersonCenter> det_persons(P);
		for (int i = 0; i < P; i++) {
			double raw_cx = persons[i].box.left + (persons[i].box.right - persons[i].box.left + 1) / 2.0;
			double raw_cy = persons[i].box.top + (persons[i].box.bottom - persons[i].box.top + 1) / 2.0;
			det_persons[i].cx = raw_cx * scale_x;
			det_persons[i].cy = raw_cy * scale_y;
		}

		struct MatchPair {
			int person_idx;
			int slot_idx;
			double dist;
		};
		std::vector<MatchPair> pairs;
		for (int i = 0; i < P; i++) {
			for (size_t s = 0; s < slots_.size(); s++) {
				if (slots_[s].active) {
					double dist = std::hypot(det_persons[i].cx - slots_[s].cx, det_persons[i].cy - slots_[s].cy);
					pairs.push_back({i, static_cast<int>(s), dist});
				}
			}
		}

		std::sort(pairs.begin(), pairs.end(), [](const MatchPair &a, const MatchPair &b) {
			return a.dist < b.dist;
		});

		for (const auto &pair : pairs) {
			if (person_matched_to_slot[pair.person_idx] == -1 && !slot_occupied[pair.slot_idx]) {
				person_matched_to_slot[pair.person_idx] = pair.slot_idx;
				slot_occupied[pair.slot_idx] = true;
			}
		}

		for (size_t i = 0; i < persons.size(); i++) {
			if (person_matched_to_slot[i] == -1) {
				int target_slot = -1;
				for (size_t s = 0; s < slots_.size(); s++) {
					if (!slots_[s].active && !slot_occupied[s]) {
						target_slot = static_cast<int>(s);
						break;
					}
				}
				if (target_slot != -1) {
					person_matched_to_slot[i] = target_slot;
					slot_occupied[target_slot] = true;
				}
			}
		}
	}

	int vw = frame.ch0_w;
	int vh = frame.ch0_h;

	for (size_t s = 0; s < slots_.size(); s++) {
		int matched_idx = -1;
		for (size_t i = 0; i < persons.size(); i++) {
			if (person_matched_to_slot[i] == static_cast<int>(s)) {
				matched_idx = static_cast<int>(i);
				break;
			}
		}

		if (matched_idx != -1) {
			const auto &person = persons[matched_idx];
			int pw = person.box.right - person.box.left + 1;
			int ph = person.box.bottom - person.box.top + 1;
			double cx_new = (person.box.left + pw / 2.0) * scale_x;
			double cy_new = (person.box.top + ph / 2.0) * scale_y;
			double w_new = pw * scale_x;
			double h_new = ph * scale_y;

			if (slots_[s].active) {
				double dist = std::hypot(cx_new - slots_[s].cx, cy_new - slots_[s].cy);
				double dw = std::abs(w_new - slots_[s].w);
				double dh = std::abs(h_new - slots_[s].h);

				if (dist > 12.0 || dw > 16.0 || dh > 16.0) {
					slots_[s].moving = true;
				}

				if (slots_[s].moving) {
					slots_[s].cx = 0.2 * cx_new + 0.8 * slots_[s].cx;
					slots_[s].cy = 0.2 * cy_new + 0.8 * slots_[s].cy;
					slots_[s].w  = 0.2 * w_new  + 0.8 * slots_[s].w;
					slots_[s].h  = 0.2 * h_new  + 0.8 * slots_[s].h;

					double current_dist = std::hypot(cx_new - slots_[s].cx, cy_new - slots_[s].cy);
					double current_dw = std::abs(w_new - slots_[s].w);
					double current_dh = std::abs(h_new - slots_[s].h);
					if (current_dist < 2.0 && current_dw < 3.0 && current_dh < 3.0) {
						slots_[s].cx = cx_new;
						slots_[s].cy = cy_new;
						slots_[s].w  = w_new;
						slots_[s].h  = h_new;
						slots_[s].moving = false;
					}
				}
			} else {
				slots_[s].cx = cx_new;
				slots_[s].cy = cy_new;
				slots_[s].w = w_new;
				slots_[s].h = h_new;
				slots_[s].active = true;
				slots_[s].moving = false;
			}
			slots_[s].last_seen_ms = now_ms;

			double target_ar = 1.0;
			int n = max_tiles;
			if (n < 4) n = 4;
			if (n > 16) n = 16;

			int R = 1, C = 4;
			if (n <= 4) { R = 1; C = 4; }
			else if (n <= 6) { R = 2; C = 3; }
			else if (n <= 8) { R = 2; C = 4; }
			else if (n <= 9) { R = 3; C = 3; }
			else { R = 4; C = 4; }

			int margin_px = 2;
			int gap_px = 2;
			int tile_h = (frame.ch2_h - 2 * margin_px - (R - 1) * gap_px) / R;
			int tile_w = (frame.ch2_w - 2 * margin_px - (C - 1) * gap_px) / C;
			if (tile_h > 0 && tile_w > 0) {
				target_ar = (double)tile_w / tile_h;
			}

			double shift_y = 0.12 * slots_[s].h;
			double cx_crop = slots_[s].cx;
			double cy_crop = slots_[s].cy - shift_y;

			double pad_factor = 1.15;
			double base_w = slots_[s].w * pad_factor;
			double base_h = slots_[s].h * pad_factor;
			double cw_crop = base_w;
			double ch_crop = base_h;

			if (base_w / base_h < target_ar) {
				cw_crop = base_h * target_ar;
			} else {
				ch_crop = base_w / target_ar;
			}

			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			GetAlignedCropBoxCentered(vw, vh, (int)cx_crop, (int)cy_crop, (int)cw_crop, (int)ch_crop, &crop_box, &crop_w, &crop_h);

			slots_[s].crop_w = crop_w;
			slots_[s].crop_h = crop_h;

			if (frame.ch0_fd != -1 && slots_[s].fd != -1) {
				rga_buffer_t src = wrapbuffer_fd_t(frame.ch0_fd, frame.ch0_w, frame.ch0_h, frame.ch0_w, frame.ch0_h, RK_FORMAT_YCbCr_420_SP);
				rga_buffer_t dst = wrapbuffer_fd_t(slots_[s].fd, crop_w, crop_h, crop_w, crop_h, RK_FORMAT_YCbCr_420_SP);
				
				im_rect rect = { crop_box.left, crop_box.top, crop_w, crop_h };
				imcrop_t(src, dst, rect, 1);
			} else if (frame.opaque_frame0 && slots_[s].virt_addr) {
				rga_buffer_t src = wrapbuffer_virtualaddr_t(frame.opaque_frame0, frame.ch0_w, frame.ch0_h, frame.ch0_w, frame.ch0_h, RK_FORMAT_YCbCr_420_SP);
				rga_buffer_t dst = wrapbuffer_virtualaddr_t(slots_[s].virt_addr, crop_w, crop_h, crop_w, crop_h, RK_FORMAT_YCbCr_420_SP);
				
				im_rect rect = { crop_box.left, crop_box.top, crop_w, crop_h };
				imcrop_t(src, dst, rect, 1);
			}
		} else {
			if (slots_[s].active) {
				if (now_ms - slots_[s].last_seen_ms > 1000) {
					slots_[s].active = false;
					slots_[s].moving = false;
				}
			}
		}
	}
}

std::shared_ptr<FrameData> PersonTracker::GenerateFrameData(const ZeroCopyFrame& frame, CameraReader* reader, int max_tiles) const {
	auto new_frame = std::make_shared<FrameData>();
	new_frame->frame_index = frame.frame_index;
	new_frame->bg_w = frame.ch2_w;
	new_frame->bg_h = frame.ch2_h;
	new_frame->bg_fd = frame.ch2_fd;
	new_frame->bg_virt_addr = static_cast<const uint8_t*>(frame.opaque_frame2);

	new_frame->camera_frame = frame;
	new_frame->camera_reader = reader;

	new_frame->presenter_fd = -1;
	new_frame->presenter_virt_addr = nullptr;
	new_frame->presenter_w = 0;
	new_frame->presenter_h = 0;
	new_frame->presenter_updated = false;

	std::vector<size_t> active_tile_indices;
	for (size_t s = 0; s < slots_.size(); s++) {
		if (slots_[s].active && (slots_[s].fd != -1 || slots_[s].virt_addr != nullptr)) {
			active_tile_indices.push_back(s);
		}
	}

	std::sort(active_tile_indices.begin(), active_tile_indices.end(), [this](size_t a, size_t b) {
		return slots_[a].cx < slots_[b].cx;
	});

	int n_active = std::min((int)active_tile_indices.size(), max_tiles);
	new_frame->tiles.resize(n_active);
	for (int i = 0; i < n_active; i++) {
		size_t s = active_tile_indices[i];
		new_frame->tiles[i].w = slots_[s].crop_w;
		new_frame->tiles[i].h = slots_[s].crop_h;
		new_frame->tiles[i].fd = slots_[s].fd;
		new_frame->tiles[i].virt_addr = static_cast<const uint8_t*>(slots_[s].virt_addr);
	}

	return new_frame;
}

} // namespace my_app
