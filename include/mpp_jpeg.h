#ifndef MY_UVC_MPP_JPEG_H_
#define MY_UVC_MPP_JPEG_H_

#include <cstddef>
#include <cstdint>
#include <vector>

struct PipHwContext;

bool pip_hw_init(PipHwContext **ctx, int canvas_w, int canvas_h, int quality);
void pip_hw_deinit(PipHwContext *ctx);

bool pip_hw_composite(PipHwContext *ctx,
                      const uint8_t *jpeg_data, size_t jpeg_len,
                      const uint8_t *overlay_nv12, int ow, int oh,
                      int ox, int oy,
                      std::vector<uint8_t> *out_jpeg);

bool pip_hw_rgb_to_nv12(const uint8_t *rgb, int w, int h,
                        std::vector<uint8_t> *nv12);

#endif // MY_UVC_MPP_JPEG_H_
