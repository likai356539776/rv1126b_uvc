#include "camera_rockit_vi_vo.h"

#include <algorithm>
#include <cstring>

#include "app_log.h"

extern "C" {
// 顺序与 SDK sample 一致：勿再包含 rk_type.h，否则 usr/include/rk_type.h 与 rockchip/rk_type.h 会重复定义
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"
#include "rk_comm_vi.h"
#include "rk_comm_video.h"
}

namespace my_app {

namespace {

// VI→VPSS 的 SYS_Bind 目标通道号：多路 VPSS 示例中固定为 0（与 sample_vi_vpss_osd_venc 一致），勿与 vpss_chn_vo 混用。
constexpr int kVpssViBindChn = 0;

void nv12_tight_copy(const uint8_t* src_y, int y_stride, const uint8_t* src_uv, int uv_stride, int w, int h,
                     uint8_t* dst) {
  for (int y = 0; y < h; ++y) {
    memcpy(dst + (size_t)y * (size_t)w, src_y + (size_t)y * (size_t)y_stride, (size_t)w);
  }
  uint8_t* dst_uv = dst + (size_t)w * (size_t)h;
  const int uv_h = h / 2;
  for (int y = 0; y < uv_h; ++y) {
    memcpy(dst_uv + (size_t)y * (size_t)w, src_uv + (size_t)y * (size_t)uv_stride, (size_t)w);
  }
}

int vo_intf_from_arg(int type) {
  if (type == 1) return VO_INTF_MIPI;
  if (type == 2) return VO_INTF_HDMI;
  if (type == 3) return VO_INTF_LCD;
  return VO_INTF_DEFAULT;
}

void fill_vpss_chn_attr(VPSS_CHN_ATTR_S* a, RK_U32 w, RK_U32 h, RK_U32 buf_cnt, RK_U32 depth, bool horiz_mirror) {
  memset(a, 0, sizeof(*a));
  a->enChnMode = VPSS_CHN_MODE_USER;
  a->enDynamicRange = DYNAMIC_RANGE_SDR8;
  a->enPixelFormat = RK_FMT_YUV420SP;
  a->stFrameRate.s32SrcFrameRate = -1;
  a->stFrameRate.s32DstFrameRate = -1;
  a->u32Width = w;
  a->u32Height = h;
  a->enCompressMode = COMPRESS_MODE_NONE;
  a->u32Depth = depth;
  a->u32FrameBufCnt = buf_cnt;
  // rk_comm_vpss.h：chn bMirror 在 hardware VPSS 上无效；本工程 RV1126B 用 RGA，与 simple_vi_vpss_venc_rawstream 一致可设通道镜像。
  a->bMirror = horiz_mirror ? RK_TRUE : RK_FALSE;
  a->bFlip = RK_FALSE;
}

}  // namespace

static ROTATION_E VoRotationFromDeg(int deg) {
  switch (deg) {
    case 90:
      return ROTATION_90;
    case 180:
      return ROTATION_180;
    case 270:
      return ROTATION_270;
    default:
      return ROTATION_0;
  }
}

static bool VoVpssSwapWhForRotation(int deg) {
  return deg == 90 || deg == 270;
}

CameraRockitRgbReader::CameraRockitRgbReader() = default;

CameraRockitRgbReader::~CameraRockitRgbReader() {
  ShutdownSubsystem();
}

int CameraRockitRgbReader::ViDevInit() {
  const int devId = cfg_.vi_dev_id;
  const int pipeId = cfg_.vi_pipe_id;

  VI_DEV_ATTR_S stDevAttr;
  VI_DEV_BIND_PIPE_S stBindPipe;
  memset(&stDevAttr, 0, sizeof(stDevAttr));
  memset(&stBindPipe, 0, sizeof(stBindPipe));

  int ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
  if (ret == RK_ERR_VI_NOT_CONFIG) {
    ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_VI_SetDevAttr fail 0x%x\n", ret);
      return -1;
    }
  }

  ret = RK_MPI_VI_GetDevIsEnable(devId);
  if (ret != RK_SUCCESS) {
    ret = RK_MPI_VI_EnableDev(devId);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_VI_EnableDev fail 0x%x\n", ret);
      return -1;
    }
    stBindPipe.u32Num = 1;
    stBindPipe.PipeId[0] = pipeId;
    ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_VI_SetDevBindPipe fail 0x%x\n", ret);
      return -1;
    }
  }
  return 0;
}

int CameraRockitRgbReader::ViChnInit(bool bind_pipeline) {
  const int pipeId = cfg_.vi_pipe_id;
  const int channelId = cfg_.vi_chn_id;
  const int w = cfg_.width;
  const int h = cfg_.height;

  VI_CHN_ATTR_S vi_chn_attr;
  memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
  vi_chn_attr.stIspOpt.u32BufCount = bind_pipeline ? 5u : 4u;
  vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
  vi_chn_attr.stSize.u32Width = (RK_U32)w;
  vi_chn_attr.stSize.u32Height = (RK_U32)h;
  vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
  vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
  vi_chn_attr.u32Depth = bind_pipeline ? 0u : 2u;

  int ret = RK_MPI_VI_SetChnAttr(pipeId, channelId, &vi_chn_attr);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VI_SetChnAttr fail 0x%x\n", ret);
    return -1;
  }
  ret = RK_MPI_VI_EnableChn(pipeId, channelId);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VI_EnableChn fail 0x%x\n", ret);
    return -1;
  }
  return 0;
}

int CameraRockitRgbReader::VpssInitBindPath() {
  const int g = cfg_.vpss_grp;
  const RK_U32 disp_w = (RK_U32)vo_disp_w_;
  const RK_U32 disp_h = (RK_U32)vo_disp_h_;
  // 90°/270°：VPSS→VO 为 disp_h×disp_w，配合 VO enRotation 在竖屏上等比显示横屏源；0°/180°：disp_w×disp_h。
  const bool swap_wh = VoVpssSwapWhForRotation(cfg_.vo_rotation_deg);
  const RK_U32 vo_chn_w = swap_wh ? disp_h : disp_w;
  const RK_U32 vo_chn_h = swap_wh ? disp_w : disp_h;
  const RK_U32 vi_w = (RK_U32)width_;
  const RK_U32 vi_h = (RK_U32)height_;

  VPSS_GRP_ATTR_S grp{};
  grp.u32MaxW = std::max(std::max(vi_w, disp_w), 64u);
  grp.u32MaxH = std::max(std::max(vi_h, disp_h), 64u);
  grp.enPixelFormat = RK_FMT_YUV420SP;
  grp.stFrameRate.s32SrcFrameRate = -1;
  grp.stFrameRate.s32DstFrameRate = -1;
  grp.enCompressMode = COMPRESS_MODE_NONE;
#ifdef RV1126B
  // 与 SDK simple_vi_bind_vpss_bind_vo test_vpss_init 一致，避免大分辨率 VI 时 max 过小
  grp.u32MaxW = 4096;
  grp.u32MaxH = 4096;
  grp.u32MaxQueue = 10;
  grp.enVProcDev = VIDEO_PROC_DEV_RGA;
#endif

  RK_S32 ret = RK_MPI_VPSS_CreateGrp(g, &grp);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VPSS_CreateGrp fail 0x%x\n", ret);
    return -1;
  }

#ifdef RV1126B
  // 与 SDK sample_comm_vpss.c 一致（sample_vi_vo / 多路 VPSS 示例路径）
  ret = RK_MPI_VPSS_SetVProcDev(g, VIDEO_PROC_DEV_RGA);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VPSS_SetVProcDev fail 0x%x\n", ret);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }
  ret = RK_MPI_VPSS_ResetGrp(g);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VPSS_ResetGrp fail 0x%x\n", ret);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }
#endif

  // 先配置/使能算法通道（通常 ch0），再 VO 通道（通常 ch1），与 sample_vi_vpss_osd_venc 一致
  VPSS_CHN_ATTR_S chn_algo{};
  fill_vpss_chn_attr(&chn_algo, vi_w, vi_h, 8u, 2u, cfg_.camera_mirror);
  ret = RK_MPI_VPSS_SetChnAttr(g, cfg_.vpss_chn_algo, &chn_algo);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: VPSS SetChnAttr algo chn fail 0x%x\n", ret);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }
  ret = RK_MPI_VPSS_EnableChn(g, cfg_.vpss_chn_algo);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: VPSS EnableChn algo fail 0x%x\n", ret);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }

  VPSS_CHN_ATTR_S chn_vo{};
  fill_vpss_chn_attr(&chn_vo, vo_chn_w, vo_chn_h, 8u, 0u, cfg_.camera_mirror);
  ret = RK_MPI_VPSS_SetChnAttr(g, cfg_.vpss_chn_vo, &chn_vo);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: VPSS SetChnAttr vo chn fail 0x%x\n", ret);
    RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_algo);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }
  ret = RK_MPI_VPSS_EnableChn(g, cfg_.vpss_chn_vo);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: VPSS EnableChn vo fail 0x%x\n", ret);
    RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_algo);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }

  ret = RK_MPI_VPSS_StartGrp(g);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VPSS_StartGrp fail 0x%x\n", ret);
    RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_algo);
    RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_vo);
    RK_MPI_VPSS_DestroyGrp(g);
    return -1;
  }

#ifdef RV1126B
  // simple_vi_vpss_venc_rawstream.c：StartGrp → ResetGrp → SetVProcDev → SetGrpMirror；仅 SetGrpMirror 时部分 BSP 不生效。
  if (cfg_.camera_mirror) {
    ret = RK_MPI_VPSS_ResetGrp(g);
    if (ret != RK_SUCCESS) {
      APP_LOGW("rockit: RK_MPI_VPSS_ResetGrp after StartGrp fail 0x%x (mirror setup)\n", ret);
    }
    ret = RK_MPI_VPSS_SetVProcDev(g, VIDEO_PROC_DEV_RGA);
    if (ret != RK_SUCCESS) {
      APP_LOGW("rockit: RK_MPI_VPSS_SetVProcDev after StartGrp fail 0x%x (mirror setup)\n", ret);
    }
    ret = RK_MPI_VPSS_SetGrpMirror(g, RK_TRUE, RK_FALSE);
    if (ret != RK_SUCCESS) {
      APP_LOGW("rockit: RK_MPI_VPSS_SetGrpMirror(H) fail 0x%x — camera_mirror 可能未生效\n", ret);
    } else {
      APP_LOGI("rockit: VPSS mirror: chn bMirror + SetGrpMirror(H) after StartGrp/ResetGrp/SetVProcDev\n");
    }
  }
#else
  if (cfg_.camera_mirror) {
    APP_LOGW("rockit: camera_mirror 仅 RV1126B 上实现（需 RK_MPI_VPSS_SetGrpMirror），已忽略\n");
  }
#endif

  vpss_inited_ = true;
  APP_LOGI("rockit: VPSS grp=%d chn_vo=%d %ux%u vo_rot=%d mirror=%d chn_algo=%d %ux%u (bind path)\n", g,
           cfg_.vpss_chn_vo, vo_chn_w, vo_chn_h, cfg_.vo_rotation_deg, cfg_.camera_mirror ? 1 : 0, cfg_.vpss_chn_algo,
           vi_w, vi_h);
  return 0;
}

void CameraRockitRgbReader::VpssDeinit() {
  if (!vpss_inited_) {
    return;
  }
  const int g = cfg_.vpss_grp;
  RK_MPI_VPSS_StopGrp(g);
  RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_vo);
  RK_MPI_VPSS_DisableChn(g, cfg_.vpss_chn_algo);
  RK_MPI_VPSS_DestroyGrp(g);
  vpss_inited_ = false;
}

int CameraRockitRgbReader::VoInitBindPath() {
  const int VoLayer = cfg_.vo_layer;
  const int VoDev = cfg_.vo_dev;
  const int VoChn = cfg_.vo_chn;
  const int W = vo_disp_w_;
  const int H = vo_disp_h_;
  const int intf_e = vo_intf_from_arg(cfg_.vo_intf_type);

  RK_S32 ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_GRAPHIC);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VO_BindLayer fail 0x%x\n", ret);
    return -1;
  }

  VO_PUB_ATTR_S stVoPubAttr{};
  VO_VIDEO_LAYER_ATTR_S stLayerAttr{};
  VO_CHN_ATTR_S stChnAttr{};

  if (intf_e != VO_INTF_DEFAULT) {
    stVoPubAttr.enIntfType = (VO_INTF_TYPE_E)intf_e;
  } else {
    stVoPubAttr.enIntfType = VO_INTF_DEFAULT;
  }
  stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

  ret = RK_MPI_VO_SetPubAttr(VoDev, &stVoPubAttr);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VO_SetPubAttr fail 0x%x\n", ret);
    return -1;
  }
  ret = RK_MPI_VO_Enable(VoDev);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VO_Enable fail 0x%x\n", ret);
    return -1;
  }

  // 与 SAMPLE_COMM_VO_CreateChn 一致；缺省时 RGN AttachToChn(VO) 常返回失败（0xffffffff）
  {
    RK_U32 disp_buf_len = 0;
    RK_S32 dret = RK_MPI_VO_GetLayerDispBufLen(VoLayer, &disp_buf_len);
    if (dret == RK_SUCCESS) {
      dret = RK_MPI_VO_SetLayerDispBufLen(VoLayer, 3);
      if (dret != RK_SUCCESS) {
        APP_LOGW("rockit: RK_MPI_VO_SetLayerDispBufLen(3) fail 0x%x (可能影响 RGN/VO OSD)\n", dret);
      }
    } else {
      APP_LOGW("rockit: RK_MPI_VO_GetLayerDispBufLen fail 0x%x\n", dret);
    }
  }

  stLayerAttr.enPixFormat = RK_FMT_RGB888;
  stLayerAttr.stDispRect.s32X = 0;
  stLayerAttr.stDispRect.s32Y = 0;
  stLayerAttr.stDispRect.u32Width = (RK_U32)W;
  stLayerAttr.stDispRect.u32Height = (RK_U32)H;
  stLayerAttr.stImageSize.u32Width = (RK_U32)W;
  stLayerAttr.stImageSize.u32Height = (RK_U32)H;
  stLayerAttr.u32DispFrmRt = 25;

  if (cfg_.vo_layer_no_compress) {
    stLayerAttr.enCompressMode = COMPRESS_MODE_NONE;
    ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_VO_SetLayerAttr(none, vo OSD) fail 0x%x\n", ret);
      return -1;
    }
    RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);
    ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (ret != RK_SUCCESS) {
      APP_LOGE(
          "rockit: RK_MPI_VO_EnableLayer(none, vo OSD) fail 0x%x — 检查 vo_intf_type / vo 分辨率\n",
          ret);
      return -1;
    }
    APP_LOGI("rockit: VO layer COMPRESS_MODE_NONE (MPI RGN / vo_osd)\n");
  } else {
    stLayerAttr.enCompressMode = COMPRESS_AFBC_16x16;
    ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_VO_SetLayerAttr fail 0x%x\n", ret);
      return -1;
    }

    RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);

    ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (ret != RK_SUCCESS) {
      APP_LOGW("rockit: RK_MPI_VO_EnableLayer(AFBC) fail 0x%x, retry COMPRESS_MODE_NONE\n", ret);
      stLayerAttr.enCompressMode = COMPRESS_MODE_NONE;
      ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
      if (ret != RK_SUCCESS) {
        APP_LOGE("rockit: RK_MPI_VO_SetLayerAttr(none) fail 0x%x\n", ret);
        return -1;
      }
      ret = RK_MPI_VO_EnableLayer(VoLayer);
      if (ret != RK_SUCCESS) {
        APP_LOGE(
            "rockit: RK_MPI_VO_EnableLayer fail 0x%x — 检查 vo_intf_type(MIPI=1)、vo_width/vo_height 是否与物理屏一致（SDK "
            "RV1126B 默认 1080x1920）\n",
            ret);
        return -1;
      }
    }
  }

  stChnAttr.stRect.s32X = 0;
  stChnAttr.stRect.s32Y = 0;
  stChnAttr.stRect.u32Width = (RK_U32)W;
  stChnAttr.stRect.u32Height = (RK_U32)H;
  stChnAttr.u32FgAlpha = 255;
  stChnAttr.u32BgAlpha = 0;
  stChnAttr.enMirror = MIRROR_NONE;
  // 双 VPSS 通道时不宜对 VPSS 做 SetChnRotation（易 VO 黑屏）；横屏源→竖屏屏在 VO 通道旋转（参考 sample_demo_eis / simple_vi_avs_send_vo）
  stChnAttr.enRotation = VoRotationFromDeg(cfg_.vo_rotation_deg);
  stChnAttr.u32Priority = 1;

  ret = RK_MPI_VO_SetChnAttr(VoLayer, VoChn, &stChnAttr);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VO_SetChnAttr fail 0x%x\n", ret);
    return -1;
  }
  ret = RK_MPI_VO_EnableChn(VoLayer, VoChn);
  if (ret != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_VO_EnableChn fail 0x%x\n", ret);
    return -1;
  }

  vo_inited_ = true;
  {
    const char* rs = "";
    if (cfg_.vo_rotation_deg == 90) {
      rs = " VO_ROT90";
    } else if (cfg_.vo_rotation_deg == 180) {
      rs = " VO_ROT180";
    } else if (cfg_.vo_rotation_deg == 270) {
      rs = " VO_ROT270";
    }
    APP_LOGI("rockit: VO (simple_vi_bind_vpss_bind_vo style) layer=%d dev=%d chn=%d %dx%d RGB888%s%s\n", VoLayer, VoDev,
             VoChn, W, H, cfg_.vo_layer_no_compress ? " COMPRESS_NONE" : "", rs);
  }
  return 0;
}

void CameraRockitRgbReader::VoDeinitBindPath() {
  if (!vo_inited_) {
    return;
  }
  const int VoLayer = cfg_.vo_layer;
  const int VoDev = cfg_.vo_dev;
  const int VoChn = cfg_.vo_chn;
  RK_MPI_VO_DisableChn(VoLayer, VoChn);
  RK_MPI_VO_DisableLayer(VoLayer);
  RK_MPI_VO_Disable(VoDev);
  RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
  RK_MPI_VO_CloseFd();
  vo_inited_ = false;
}

void CameraRockitRgbReader::UnbindAll() {
  if (!sys_bound_ || !bind_vo_pipeline_) {
    return;
  }
  MPP_CHN_S stVpssVo{};
  stVpssVo.enModId = RK_ID_VPSS;
  stVpssVo.s32DevId = cfg_.vpss_grp;
  stVpssVo.s32ChnId = cfg_.vpss_chn_vo;

  MPP_CHN_S stVo{};
  stVo.enModId = RK_ID_VO;
  stVo.s32DevId = cfg_.vo_layer;
  stVo.s32ChnId = cfg_.vo_chn;

  MPP_CHN_S stVi{};
  stVi.enModId = RK_ID_VI;
  stVi.s32DevId = cfg_.vi_pipe_id;
  stVi.s32ChnId = cfg_.vi_chn_id;

  MPP_CHN_S stVpssIn{};
  stVpssIn.enModId = RK_ID_VPSS;
  stVpssIn.s32DevId = cfg_.vpss_grp;
  stVpssIn.s32ChnId = kVpssViBindChn;

  RK_S32 r = RK_MPI_SYS_UnBind(&stVpssVo, &stVo);
  if (r != RK_SUCCESS) {
    APP_LOGW("rockit: UnBind VPSS->VO 0x%x\n", r);
  }
  r = RK_MPI_SYS_UnBind(&stVi, &stVpssIn);
  if (r != RK_SUCCESS) {
    APP_LOGW("rockit: UnBind VI->VPSS 0x%x\n", r);
  }
  sys_bound_ = false;
}

int CameraRockitRgbReader::Open(int width, int height, const std::string& node, int fps) {
  (void)node;
  RockitCameraConfig cfg;
  cfg.vi_pipe_id = 0;
  cfg.vi_dev_id = 0;
  cfg.vi_chn_id = 0;
  cfg.width = width;
  cfg.height = height;
  cfg.vo_disp_width = height;  // Transpose display size for vertical panel
  cfg.vo_disp_height = width;
  cfg.vo_layer_no_compress = false;
  cfg.vo_rotation_deg = 0;
  cfg.camera_mirror = false;

  int ret = Open(cfg);
  if (ret == 0) {
    fps_ = fps;
  }
  return ret;
}

int CameraRockitRgbReader::Open(const RockitCameraConfig& cfg) {
  Close();
  ShutdownSubsystem();

  cfg_ = cfg;
  if (cfg_.vo_rotation_deg != 0 && cfg_.vo_rotation_deg != 90 && cfg_.vo_rotation_deg != 180 &&
      cfg_.vo_rotation_deg != 270) {
    APP_LOGE("rockit: vo_rotation_deg must be 0, 90, 180, or 270 (got %d)\n", cfg_.vo_rotation_deg);
    return -1;
  }
  if ((cfg_.width & 1) || (cfg_.height & 1)) {
    APP_LOGE("rockit: width/height must be even\n");
    return -1;
  }

  width_ = cfg_.width;
  height_ = cfg_.height;
  fps_ = 30;
  frame_index_ = 0;
  bind_vo_pipeline_ = cfg_.vo_enable;

  vo_disp_w_ = cfg_.vo_disp_width > 0 ? cfg_.vo_disp_width : cfg_.width;
  vo_disp_h_ = cfg_.vo_disp_height > 0 ? cfg_.vo_disp_height : cfg_.height;

  nv12_tight_.resize((size_t)width_ * (size_t)height_ * 3u / 2u);

  if (RK_MPI_SYS_Init() != RK_SUCCESS) {
    APP_LOGE("rockit: RK_MPI_SYS_Init fail\n");
    return -1;
  }
  mpi_inited_ = true;

  if (ViDevInit() != 0) {
    ShutdownSubsystem();
    return -1;
  }
  if (ViChnInit(bind_vo_pipeline_) != 0) {
    ShutdownSubsystem();
    return -1;
  }

  if (bind_vo_pipeline_) {
    if (VpssInitBindPath() != 0) {
      ShutdownSubsystem();
      return -1;
    }
    if (VoInitBindPath() != 0) {
      ShutdownSubsystem();
      return -1;
    }

    MPP_CHN_S stVi{};
    stVi.enModId = RK_ID_VI;
    stVi.s32DevId = cfg_.vi_pipe_id;
    stVi.s32ChnId = cfg_.vi_chn_id;

    MPP_CHN_S stVpssIn{};
    stVpssIn.enModId = RK_ID_VPSS;
    stVpssIn.s32DevId = cfg_.vpss_grp;
    stVpssIn.s32ChnId = kVpssViBindChn;

    if (cfg_.vpss_chn_vo == cfg_.vpss_chn_algo) {
      APP_LOGE("rockit: vpss_chn_vo and vpss_chn_algo must differ\n");
      ShutdownSubsystem();
      return -1;
    }

    RK_S32 ret = RK_MPI_SYS_Bind(&stVi, &stVpssIn);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_SYS_Bind VI->VPSS fail 0x%x\n", ret);
      ShutdownSubsystem();
      return -1;
    }

    MPP_CHN_S stVpssVo{};
    stVpssVo.enModId = RK_ID_VPSS;
    stVpssVo.s32DevId = cfg_.vpss_grp;
    stVpssVo.s32ChnId = cfg_.vpss_chn_vo;

    MPP_CHN_S stVo{};
    stVo.enModId = RK_ID_VO;
    stVo.s32DevId = cfg_.vo_layer;
    stVo.s32ChnId = cfg_.vo_chn;

    ret = RK_MPI_SYS_Bind(&stVpssVo, &stVo);
    if (ret != RK_SUCCESS) {
      APP_LOGE("rockit: RK_MPI_SYS_Bind VPSS->VO fail 0x%x\n", ret);
      RK_MPI_SYS_UnBind(&stVi, &stVpssIn);
      ShutdownSubsystem();
      return -1;
    }
    sys_bound_ = true;

#ifdef RV1126B
    // VI→VPSS 绑定后再次 SetGrpMirror，避免部分驱动在 bind 前忽略组镜像。
    if (cfg_.camera_mirror) {
      RK_S32 mret = RK_MPI_VPSS_SetGrpMirror(cfg_.vpss_grp, RK_TRUE, RK_FALSE);
      if (mret != RK_SUCCESS) {
        APP_LOGW("rockit: RK_MPI_VPSS_SetGrpMirror after VI/VO bind fail 0x%x\n", mret);
      }
    }
#endif

    APP_LOGI(
        "rockit: VI(pipe=%d,chn=%d)->VPSS(grp=%d,bind_in=%d) VPSS(chn=%d)->VO(layer=%d,chn=%d) algo=VPSS(chn=%d)\n",
        cfg_.vi_pipe_id, cfg_.vi_chn_id, cfg_.vpss_grp, kVpssViBindChn, cfg_.vpss_chn_vo, cfg_.vo_layer, cfg_.vo_chn,
        cfg_.vpss_chn_algo);
  }

  APP_LOGI("rockit: capture %dx%d vo=%d bind_pipeline=%d\n", width_, height_, cfg_.vo_enable ? 1 : 0,
           bind_vo_pipeline_ ? 1 : 0);
  return 0;
}

void CameraRockitRgbReader::Close() {
  if (!mpi_inited_) {
    return;
  }
  if (!bind_vo_pipeline_) {
    RK_MPI_VI_DisableChn(cfg_.vi_pipe_id, cfg_.vi_chn_id);
  }
}

void CameraRockitRgbReader::ShutdownSubsystem() {
  UnbindAll();
  VoDeinitBindPath();
  VpssDeinit();

  if (mpi_inited_) {
    RK_MPI_VI_DisableChn(cfg_.vi_pipe_id, cfg_.vi_chn_id);
    RK_MPI_VI_DisableDev(cfg_.vi_dev_id);
    RK_MPI_SYS_Exit();
    mpi_inited_ = false;
  }
  bind_vo_pipeline_ = false;
  sys_bound_ = false;
  nv12_tight_.clear();
  width_ = height_ = fps_ = 0;
  frame_index_ = 0;
}

int CameraRockitRgbReader::ReadNextRgbInto(image_buffer_t* out, int timeout_ms) {
  if (!mpi_inited_ || !out || !out->virt_addr) {
    return -1;
  }
  if (out->width != width_ || out->height != height_ || out->format != IMAGE_FORMAT_RGB888 ||
      out->size < width_ * height_ * 3) {
    APP_LOGE("rockit: RGB output buffer mismatch (expect %dx%d RGB888)\n", width_, height_);
    return -1;
  }

  VIDEO_FRAME_INFO_S stFrame{};
  RK_S32 s32Ret = RK_FAILURE;

  if (bind_vo_pipeline_) {
    s32Ret =
        RK_MPI_VPSS_GetChnFrame(cfg_.vpss_grp, cfg_.vpss_chn_algo, &stFrame, timeout_ms);
  } else {
    s32Ret = RK_MPI_VI_GetChnFrame(cfg_.vi_pipe_id, cfg_.vi_chn_id, &stFrame, timeout_ms);
  }

  if (s32Ret != RK_SUCCESS) {
    return -1;
  }

  const RK_U32 vw = stFrame.stVFrame.u32Width;
  const RK_U32 vh = stFrame.stVFrame.u32Height;
  const RK_U32 vs_w = stFrame.stVFrame.u32VirWidth;
  const RK_U32 vs_h = stFrame.stVFrame.u32VirHeight;

  uint8_t* p = (uint8_t*)RK_MPI_MB_Handle2VirAddr(stFrame.stVFrame.pMbBlk);
  if (!p) {
    if (bind_vo_pipeline_) {
      RK_MPI_VPSS_ReleaseChnFrame(cfg_.vpss_grp, cfg_.vpss_chn_algo, &stFrame);
    } else {
      RK_MPI_VI_ReleaseChnFrame(cfg_.vi_pipe_id, cfg_.vi_chn_id, &stFrame);
    }
    return -1;
  }

  if (vw != (RK_U32)width_ || vh != (RK_U32)height_) {
    APP_LOGW("rockit: frame size %ux%u differs from expected %dx%d\n", vw, vh, width_, height_);
  }

  const int cw = (int)std::min(vw, (RK_U32)width_);
  const int ch = (int)std::min(vh, (RK_U32)height_);
  if (cw < 2 || ch < 2 || (ch & 1)) {
    if (bind_vo_pipeline_) {
      RK_MPI_VPSS_ReleaseChnFrame(cfg_.vpss_grp, cfg_.vpss_chn_algo, &stFrame);
    } else {
      RK_MPI_VI_ReleaseChnFrame(cfg_.vi_pipe_id, cfg_.vi_chn_id, &stFrame);
    }
    return -1;
  }

  nv12_tight_copy(p, (int)vs_w, p + (size_t)vs_w * (size_t)vs_h, (int)vs_w, cw, ch, nv12_tight_.data());

  image_buffer_t src_nv12{};
  src_nv12.width = cw;
  src_nv12.height = ch;
  src_nv12.format = IMAGE_FORMAT_YUV420SP_NV12;
  src_nv12.virt_addr = nv12_tight_.data();
  src_nv12.size = cw * ch * 3 / 2;

  image_buffer_t dst_rgb = *out;
  if (cw != width_ || ch != height_) {
    memset(dst_rgb.virt_addr, 0, (size_t)dst_rgb.size);
  }
  if (convert_image(&src_nv12, &dst_rgb, nullptr, nullptr, 0) != 0) {
    APP_LOGE("rockit: convert_image NV12->RGB fail\n");
    if (bind_vo_pipeline_) {
      RK_MPI_VPSS_ReleaseChnFrame(cfg_.vpss_grp, cfg_.vpss_chn_algo, &stFrame);
    } else {
      RK_MPI_VI_ReleaseChnFrame(cfg_.vi_pipe_id, cfg_.vi_chn_id, &stFrame);
    }
    return -1;
  }

  if (bind_vo_pipeline_) {
    s32Ret = RK_MPI_VPSS_ReleaseChnFrame(cfg_.vpss_grp, cfg_.vpss_chn_algo, &stFrame);
  } else {
    s32Ret = RK_MPI_VI_ReleaseChnFrame(cfg_.vi_pipe_id, cfg_.vi_chn_id, &stFrame);
  }
  if (s32Ret != RK_SUCCESS) {
    APP_LOGE("rockit: ReleaseChnFrame fail 0x%x\n", s32Ret);
  }
  frame_index_++;
  return 0;
}

}  // namespace my_app
