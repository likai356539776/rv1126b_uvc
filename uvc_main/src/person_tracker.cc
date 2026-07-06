#include "person_tracker.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "app_log.h"

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

namespace {
// 1080p base crop strategies
const CropStrategy kBaseCropStrategies[5] = {
	{470, 1070, 1.00, 180, 156.0, 94.0}, // 1~4 tiles (large tiles, 1/3 aspect deadzone tracking)
	{630, 530,  1.15, 180, 189.0, 94.0}, // 5~6 tiles
	{470, 530,  1.10, 150, 117.0, 70.0}, // 7~8 tiles (medium tiles)
	{630, 350,  1.08, 140, 126.0, 75.0}, // 9 tiles
	{470, 260,  1.05, 100, 70.0,  47.0}  // 10~16 tiles (small tiles, 15% deadzone responsive tracking)
};

int GetStrategyIndex(int n_active) {
	if (n_active <= 4) return 0;
	if (n_active <= 6) return 1;
	if (n_active <= 8) return 2;
	if (n_active <= 9) return 3;
	return 4; // 10~16
}
} // namespace

PersonTracker::PersonTracker() {
	slots_.resize(my_uvc_pip::kPipTileLayoutMax);
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
                           const uint8_t* nv12_data, int vw, int vh, int64_t now_ms) {
	// 1. Calculate the active slots count to determine strategy
	int active_count = 0;
	for (const auto& slot : slots_) {
		if (slot.active) active_count++;
	}
	// Fallback to detected count to prime strategies on startup
	if (active_count == 0) {
		active_count = (int)persons.size();
	}

	int strategy_idx = GetStrategyIndex(active_count);
	const auto& strategy = kBaseCropStrategies[strategy_idx];
	double target_ratio = (double)strategy.target_w / strategy.target_h;

	// 2. Scale min width and height thresholds based on input vw relative to 1080p (1920)
	double scale_factor = (double)vw / 1920.0;
	double min_w = strategy.min_crop_w * scale_factor;
	double min_h = min_w / target_ratio;

	int P = (int)persons.size();

	// Track which detected person is matched to which slot
	std::vector<int> person_matched_to_slot(P, -1);
	std::vector<bool> slot_occupied(slots_.size(), false);

	if (P > 0) {
		// Precompute center coordinates for all detected persons
		struct PersonCenter {
			double cx;
			double cy;
		};
		std::vector<PersonCenter> det_persons(P);
		for (int i = 0; i < P; i++) {
			det_persons[i].cx = persons[i].box.left + (persons[i].box.right - persons[i].box.left + 1) / 2.0;
			det_persons[i].cy = persons[i].box.top + (persons[i].box.bottom - persons[i].box.top + 1) / 2.0;
		}

		// Compute all possible pairs of (person_idx, slot_idx, distance)
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

		// Sort pairs by distance ascending
		std::sort(pairs.begin(), pairs.end(), [](const MatchPair &a, const MatchPair &b) {
			return a.dist < b.dist;
		});

		// Greedy match pairs based on global distance optimization
		for (const auto &pair : pairs) {
			if (person_matched_to_slot[pair.person_idx] == -1 && !slot_occupied[pair.slot_idx]) {
				person_matched_to_slot[pair.person_idx] = pair.slot_idx;
				slot_occupied[pair.slot_idx] = true;
			}
		}

		// Second pass: Assign unmatched persons to inactive slots
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



	// Third pass: Update slots and crop active ones
	for (size_t s = 0; s < slots_.size(); s++) {
		// Find if any person is matched to this slot
		int matched_idx = -1;
		for (size_t i = 0; i < persons.size(); i++) {
			if (person_matched_to_slot[i] == static_cast<int>(s)) {
				matched_idx = static_cast<int>(i);
				break;
			}
		}

		if (matched_idx != -1) {
			// Slot is matched with a newly detected person
			const auto &person = persons[matched_idx];
			int pw = person.box.right - person.box.left + 1;
			int ph = person.box.bottom - person.box.top + 1;
			double cx_new = person.box.left + pw / 2.0;
			double cy_new = person.box.top + ph / 2.0;

			// Lock aspect ratio to match display target and apply layout strategy parameters
			double w_new = pw;
			double h_new = ph;
			double current_ratio = w_new / h_new;

			if (current_ratio > target_ratio) {
				// Target is wider: scale width and determine height from ratio
				w_new = w_new * strategy.padding;
				h_new = w_new / target_ratio;
			} else {
				// Target is taller: scale height and determine width from ratio
				h_new = h_new * strategy.padding;
				w_new = h_new * target_ratio;
			}

			// Clear-distance protection: clamp to minimum scaled size
			if (w_new < min_w) {
				w_new = min_w;
				h_new = min_h;
			}

			if (slots_[s].active) {
				double dx = std::abs(cx_new - slots_[s].cx);
				double dy = std::abs(cy_new - slots_[s].cy);
				double dw = std::abs(w_new - slots_[s].w);
				double dh = std::abs(h_new - slots_[s].h);

				// Calculate relative size adaptive deadzone thresholds independently for X and Y axes
				double ref_w = slots_[s].w;
				double ref_h = slots_[s].h;
				double dist_limit_x = std::max(ref_w * (strategy.base_dist_limit / (double)strategy.target_w), 4.0);
				double dist_limit_y = std::max(ref_h * (strategy.base_dist_limit / (double)strategy.target_w), 4.0);
				double scale_limit = std::max(ref_w * (strategy.base_scale_limit / (double)strategy.target_w), 6.0);

				if (dx > dist_limit_x || dy > dist_limit_y || dw > scale_limit || dh > scale_limit) {
					slots_[s].moving = true;
				}

				if (slots_[s].moving) {
					slots_[s].cx = 0.2 * cx_new + 0.8 * slots_[s].cx;
					slots_[s].cy = 0.2 * cy_new + 0.8 * slots_[s].cy;
					slots_[s].w  = 0.2 * w_new  + 0.8 * slots_[s].w;
					slots_[s].h  = 0.2 * h_new  + 0.8 * slots_[s].h;

					double current_dx = std::abs(cx_new - slots_[s].cx);
					double current_dy = std::abs(cy_new - slots_[s].cy);
					double current_dw = std::abs(w_new - slots_[s].w);
					double current_dh = std::abs(h_new - slots_[s].h);

					// High-precision adaptive convergence locking thresholds
					double lock_dist_x = std::max(ref_w * 0.015, 2.0);
					double lock_dist_y = std::max(ref_h * 0.015, 2.0);
					double lock_scale = std::max(ref_w * 0.02, 3.0);

					if (current_dx < lock_dist_x && current_dy < lock_dist_y && 
					    current_dw < lock_scale && current_dh < lock_scale) {
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

			// Crop the person and update the saved buffer
			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			GetAlignedCropBoxCentered(vw, vh, (int)slots_[s].cx, (int)slots_[s].cy, (int)slots_[s].w, (int)slots_[s].h, &crop_box, &crop_w, &crop_h);

			slots_[s].crop_w = crop_w;
			slots_[s].crop_h = crop_h;
			slots_[s].last_nv12.resize(crop_w * crop_h * 3 / 2);

			nv12_crop_cpu(nv12_data, vw, vh, crop_box.left, crop_box.top, slots_[s].last_nv12.data(), crop_w, crop_h);
		} else {
			// No person matched in this frame. Check 1-second persistence timeout.
			if (slots_[s].active) {
				if (now_ms - slots_[s].last_seen_ms > 1000) {
					slots_[s].active = false;
					slots_[s].moving = false;
				}
			}
		}
	}
}

std::shared_ptr<FrameData> PersonTracker::GenerateFrameData(long long frame_idx, const uint8_t* nv12_data,
                                                           int vw, int vh, int max_tiles) const {
	auto new_frame = std::make_shared<FrameData>();
	new_frame->frame_index = frame_idx;
	new_frame->bg_w = vw;
	new_frame->bg_h = vh;
	new_frame->bg_nv12.assign(nv12_data, nv12_data + vw * vh * 3 / 2);

	// Presenter is no longer populated from YOLO slots
	new_frame->presenter_w = 0;
	new_frame->presenter_h = 0;
	new_frame->presenter_updated = false;

	// Tiles (Slots 0..M-1)
	std::vector<size_t> active_tile_indices;
	for (size_t s = 0; s < slots_.size(); s++) {
		if (slots_[s].active && !slots_[s].last_nv12.empty()) {
			active_tile_indices.push_back(s);
		}
	}

	// Sort active slots left-to-right (by cx)
	std::sort(active_tile_indices.begin(), active_tile_indices.end(), [this](size_t a, size_t b) {
		return slots_[a].cx < slots_[b].cx;
	});

	int n_active = std::min((int)active_tile_indices.size(), max_tiles);
	new_frame->tiles.resize(n_active);
	for (int i = 0; i < n_active; i++) {
		size_t s = active_tile_indices[i];
		new_frame->tiles[i].w = slots_[s].crop_w;
		new_frame->tiles[i].h = slots_[s].crop_h;
		new_frame->tiles[i].nv12 = slots_[s].last_nv12;
	}

	return new_frame;
}

} // namespace my_app
