#pragma once

#include <cstdint>
#include <vector>

#include "image_utils.h"
#include "camera_reader.h"

namespace my_app {

// Rockit 摄像头：对齐 SDK simple_vi_bind_vpss_bind_vo.c（仅此一路，无其它 VO 送帧方式）
// - VO 开启：VI→VPSS(ch 算法)→取帧；VPSS(ch VO)→VO；VI→VPSS 绑定固定 ch0
// - VO 关闭：仅 VI + depth，RK_MPI_VI_GetChnFrame（无 VPSS/VO）
struct RockitCameraConfig {
  int vi_pipe_id = 0;
  int vi_dev_id = 0;
  int vi_chn_id = 0;
  int width = 1920;
  int height = 1080;
  bool vo_enable = true;
  int vo_layer = 0;
  int vo_dev = 0;
  int vo_chn = 0;
  // 0 = VO_INTF_DEFAULT; 1 = MIPI; 2 = HDMI; 3 = LCD（RV1126B SDK 示例默认 1）
#if defined(RV1126B)
  int vo_intf_type = 1;
#else
  int vo_intf_type = 0;
#endif
  // VPSS/VO 显示分辨率（对应 simple_vi_bind_vpss_bind_vo 的 -w/-h）；0 表示与 VI width/height 相同
  int vo_disp_width = 0;
  int vo_disp_height = 0;
  int vpss_grp = 0;
  // 与 SDK sample_vi_vpss_osd_venc 一致：VO 走 VPSS 较高通道，算法取帧走 ch0；VI→VPSS 绑定目标固定为 ch0（见 camera_rockit_vi_vo.cc）
  int vpss_chn_vo = 1;
  int vpss_chn_algo = 0;
  // When true, VO layer uses COMPRESS_MODE_NONE. AFBC + RGA splice often does not composite
  // VPSS-attached RGN; SetBitMap succeeds but nothing appears on panel.
  bool vo_layer_no_compress = false;
  // VO 通道旋转（度）：仅允许 0/90/180/270，对应 RK_MPI_VO_SetChnAttr enRotation；不用 VPSS SetChnRotation。
  // 90/270：VPSS→VO 输出 vo_disp_height×vo_disp_width（横屏源在竖屏上等比）；0/180：输出 vo_disp_width×vo_disp_height。
  int vo_rotation_deg = 0;
  // 水平镜像（前置/朝人安装）。RV1126B+RGA：两路 VPSS chn bMirror + StartGrp 后 ResetGrp/SetVProcDev/SetGrpMirror + 绑定后再 SetGrpMirror（SDK simple_vi_vpss_venc_rawstream.c）；预览/算法/OSD 一致。
  bool camera_mirror = false;
};

class CameraRockitRgbReader : public CameraReader {
 public:
  CameraRockitRgbReader();
  ~CameraRockitRgbReader() override;

  CameraRockitRgbReader(const CameraRockitRgbReader&) = delete;
  CameraRockitRgbReader& operator=(const CameraRockitRgbReader&) = delete;

  int Open(int width, int height, const std::string& node, int fps) override;
  int Open(const RockitCameraConfig& cfg);
  void Close() override;
  void ShutdownSubsystem();

  int ReadNextRgbInto(image_buffer_t* out, int timeout_ms) override;

  int width() const override { return width_; }
  int height() const override { return height_; }
  int fps() const override { return fps_; }
  long long frame_index() const override { return frame_index_; }
  int vo_disp_width() const { return vo_disp_w_; }
  int vo_disp_height() const { return vo_disp_h_; }
  const RockitCameraConfig& config() const { return cfg_; }
  const uint8_t* GetLastNv12Data() const override { return nv12_tight_.data(); }

 private:
  int ViDevInit();
  int ViChnInit(bool bind_pipeline);
  int VpssInitBindPath();
  void VpssDeinit();
  int VoInitBindPath();
  void VoDeinitBindPath();
  void UnbindAll();

  RockitCameraConfig cfg_{};
  int width_ = 0;
  int height_ = 0;
  int fps_ = 0;
  long long frame_index_ = 0;

  bool mpi_inited_ = false;
  bool vpss_inited_ = false;
  bool vo_inited_ = false;
  bool bind_vo_pipeline_ = false;
  bool sys_bound_ = false;

  int vo_disp_w_ = 0;
  int vo_disp_h_ = 0;

  std::vector<unsigned char> nv12_tight_;
};

}  // namespace my_app
