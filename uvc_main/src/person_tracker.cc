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
                           const uint8_t* nv12_data, int vw, int vh, int64_t now_ms, int max_tiles) {
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
			double w_new = pw;
			double h_new = ph;

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

			// Calculate target grid cell aspect ratio based on layout configs
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
			int tile_h = (vh - 2 * margin_px - (R - 1) * gap_px) / R;
			int tile_w = (vw - 2 * margin_px - (C - 1) * gap_px) / C;
			if (tile_h > 0 && tile_w > 0) {
				target_ar = (double)tile_w / tile_h;
			}

			// Apply upward shift (12% of height shift upward to center the head)
			double shift_y = 0.12 * slots_[s].h;
			double cx_crop = slots_[s].cx;
			double cy_crop = slots_[s].cy - shift_y;

			// Add 15% safety padding, then expand to match target aspect ratio
			double pad_factor = 1.15;
			double base_w = slots_[s].w * pad_factor;
			double base_h = slots_[s].h * pad_factor;
			double cw_crop = base_w;
			double ch_crop = base_h;

			if (base_w / base_h < target_ar) {
				// Slot is wider than person, expand width
				cw_crop = base_h * target_ar;
			} else {
				// Slot is narrower than person, expand height
				ch_crop = base_w / target_ar;
			}

			// Crop the person and update the saved buffer using aligned crop box
			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			GetAlignedCropBoxCentered(vw, vh, (int)cx_crop, (int)cy_crop, (int)cw_crop, (int)ch_crop, &crop_box, &crop_w, &crop_h);

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
