#include "person_tracker.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "app_log.h"

namespace my_app {

PersonTracker::PersonTracker() {
	slots_.resize(1 + my_uvc_pip::kPipTileLayoutMax);
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
	int P = (int)persons.size();

	// Track which detected person is matched to which slot
	std::vector<int> person_matched_to_slot(P, -1);
	std::vector<bool> slot_occupied(slots_.size(), false);

	if (P == 1) {
		// Special case: If only 1 person is detected, they MUST go to Slot 0 (Presenter)
		person_matched_to_slot[0] = 0;
		slot_occupied[0] = true;
	} else if (P > 1) {
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

	// Prepare image_buffer_t for source image
	image_buffer_t src_nv12_img{};
	src_nv12_img.width = vw;
	src_nv12_img.height = vh;
	src_nv12_img.format = IMAGE_FORMAT_YUV420SP_NV12;
	src_nv12_img.size = vw * vh * 3 / 2;
	src_nv12_img.virt_addr = const_cast<uint8_t*>(nv12_data);

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

			// Crop the person and update the saved buffer
			image_rect_t crop_box{};
			int crop_w = 0, crop_h = 0;
			GetAlignedCropBoxCentered(vw, vh, (int)slots_[s].cx, (int)slots_[s].cy, (int)slots_[s].w, (int)slots_[s].h, &crop_box, &crop_w, &crop_h);

			slots_[s].crop_w = crop_w;
			slots_[s].crop_h = crop_h;
			slots_[s].last_nv12.resize(crop_w * crop_h * 3 / 2);

			image_buffer_t dst_nv12_img{};
			dst_nv12_img.width = crop_w;
			dst_nv12_img.height = crop_h;
			dst_nv12_img.format = IMAGE_FORMAT_YUV420SP_NV12;
			dst_nv12_img.virt_addr = slots_[s].last_nv12.data();
			dst_nv12_img.size = crop_w * crop_h * 3 / 2;

			image_rect_t dst_box{0, 0, crop_w - 1, crop_h - 1};
			if (convert_image(&src_nv12_img, &dst_nv12_img, &crop_box, &dst_box, 0) != 0) {
				APP_LOGE("PersonTracker: crop slot %zu failed\n", s);
			}
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

	// Presenter (Slot 0)
	if (slots_[0].active && !slots_[0].last_nv12.empty()) {
		new_frame->presenter_w = slots_[0].crop_w;
		new_frame->presenter_h = slots_[0].crop_h;
		new_frame->presenter_nv12 = slots_[0].last_nv12;
		new_frame->presenter_updated = true;
	}

	// Tiles (Slots 1..M)
	std::vector<size_t> active_tile_indices;
	for (size_t s = 1; s < slots_.size(); s++) {
		if (slots_[s].active && !slots_[s].last_nv12.empty()) {
			active_tile_indices.push_back(s);
		}
	}

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
