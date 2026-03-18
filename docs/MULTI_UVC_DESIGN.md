# my_uvc Multi-UVC Design (v2 Plan)

## Goal

Extend current single-channel `my_uvc` to support multiple UVC functions (for example `uvc.gs1`, `uvc.gs2`, `uvc.gs3`) while keeping each channel independently configurable.

## Current v1 Baseline

- Single UVC function: `uvc.gs1`
- Single source: one H.264 file
- Single stream context in `main.cpp`

## Target v2 Architecture

- `UsbGadgetManager`
  - Configure multiple UVC functions in configfs
  - Bind/unbind and report channel to `/dev/videoX` mapping

- `ChannelConfig` (per channel)
  - `enable`
  - `format` (initially H.264 only)
  - `width` / `height`
  - `fps`
  - `source_path`
  - `loop_file`

- `ChannelRuntime` (per channel)
  - NAL/frame index
  - SPS/PPS cache
  - host negotiated fps
  - stream on/off state
  - statistics

- `StreamScheduler`
  - run one worker thread per channel, or a single loop with per-channel timing
  - enforce independent frame pacing

## Key Implementation Points

1. **USB config script**
   - Add options for channel count and per-channel resolution.
   - Create `uvc.gsN` functions and symlink all into one config.

2. **UVC control path**
   - Keep using existing local `third_party/uvc` stack.
   - Channel ID comes from detected `/dev/videoX`.

3. **Frame source abstraction**
   - Replace v1 single source with `IFrameSource` interface:
     - `FileFrameSource` (existing behavior)
     - later: `VencFrameSource` (real-time encoder output)

4. **Fault isolation**
   - One channel failure should not stop other channels.

## Suggested Milestones

- M1: dual-channel file source (`uvc.gs1`, `uvc.gs2`) with same file
- M2: dual-channel independent files and independent fps
- M3: migrate one channel to real-time VENC source

## Risks

- Host-side compatibility differs by OS and camera app for multi-UVC enumeration.
- Bandwidth constraints on USB link when multiple high-bitrate streams run concurrently.
