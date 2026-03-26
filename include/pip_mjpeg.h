#ifndef MY_UVC_PIP_MJPEG_H_
#define MY_UVC_PIP_MJPEG_H_

#include <cstddef>
#include <cstdint>
#include <vector>

/** Returns last libjpeg error message for current thread (best-effort). */
const char *pip_mjpeg_last_error();

/** Decode JPEG bytes to packed RGB888 (row-major, width * height * 3). */
bool pip_mjpeg_decode_jpeg_rgb(const uint8_t *jpeg_data, size_t jpeg_len, std::vector<uint8_t> *rgb, int *out_w,
                               int *out_h);

/** Decode JPEG file from filesystem path to packed RGB888. */
bool pip_mjpeg_decode_jpeg_file_rgb(const char *path, std::vector<uint8_t> *rgb, int *out_w, int *out_h);

/** Bilinear resize RGB888. */
void pip_mjpeg_scale_rgb_bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/** Overwrite dst with src at (dx,dy), clipped to dst [0,dw) x [0,dh). */
void pip_mjpeg_blit_rgb(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, int dx, int dy);

/** Encode RGB888 to JPEG (baseline). quality 1–100. */
bool pip_mjpeg_encode_rgb_jpeg(const uint8_t *rgb, int w, int h, int quality, std::vector<uint8_t> *jpeg_out);

/**
 * Background: JPEG bytes (one frame). Overlay: already scaled RGB, size ow×oh.
 * Writes full canvas (canvas_w × canvas_h) as JPEG.
 * tmp_canvas must hold at least canvas_w * canvas_h * 3 bytes (reused).
 */
bool pip_mjpeg_composite_jpeg(const uint8_t *bg_jpeg, size_t bg_jpeg_len, int canvas_w, int canvas_h,
                              const uint8_t *overlay_rgb, int ow, int oh, int ox, int oy, int jpeg_quality,
                              std::vector<uint8_t> *out_jpeg, std::vector<uint8_t> *tmp_canvas_rgb);

#endif // MY_UVC_PIP_MJPEG_H_
