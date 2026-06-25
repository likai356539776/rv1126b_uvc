#ifndef MY_UVC_MPP_JPEG_H_
#define MY_UVC_MPP_JPEG_H_

#include <cstddef>
#include <cstdint>
#include <vector>

struct PipHwContext;

bool pip_hw_init(PipHwContext **ctx, int canvas_w, int canvas_h, int quality);
void pip_hw_deinit(PipHwContext *ctx);

struct PipBorderConfig {
	bool enable;
	int radius;
	int thickness;
	uint8_t y;
	uint8_t u;
	uint8_t v;
};

/**
 * One NV12 layer: 先可选 RGA 缩放到 dst_w×dst_h，再 blit 到画布 (ox,oy)。
 * src_w/src_h 均 >0 且与 dst 不同时做缩放；否则认为源已为 dst_w×dst_h。
 */
struct PipHwNv12Blit {
	const uint8_t *nv12;
	int fd;
	int dst_w, dst_h;
	int ox, oy;
	int src_w, src_h;
};

/** NV12 虚拟地址 → 虚拟地址缩放（RGA），宽高将 ALIGN2。失败返回 false。 */
bool pip_hw_nv12_resize_virtual(const uint8_t *src_nv12, int src_w, int src_h, uint8_t *dst_nv12, int dst_w,
                                int dst_h);

bool pip_hw_composite_layers(PipHwContext *ctx,
                             const uint8_t *jpeg_data, size_t jpeg_len,
                             const PipHwNv12Blit *blits, int n_blits,
                             const PipBorderConfig *bc,
                             std::vector<uint8_t> *out_jpeg);

bool pip_hw_composite_layers_nv12(PipHwContext *ctx,
                                  int bg_fd, int bg_w, int bg_h,
                                  const PipHwNv12Blit *blits, int n_blits,
                                  const PipBorderConfig *bc,
                                  std::vector<uint8_t> *out_jpeg);

bool pip_hw_composite(PipHwContext *ctx,
                      const uint8_t *jpeg_data, size_t jpeg_len,
                      const uint8_t *overlay_nv12, int ow, int oh,
                      int ox, int oy,
                      std::vector<uint8_t> *out_jpeg);

bool pip_hw_rgb_to_nv12(const uint8_t *rgb, int w, int h,
                        std::vector<uint8_t> *nv12);

#endif // MY_UVC_MPP_JPEG_H_
