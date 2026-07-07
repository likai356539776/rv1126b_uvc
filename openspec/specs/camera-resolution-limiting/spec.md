# camera-resolution-limiting Specification

## Purpose
TBD - created by archiving change limit-camera-resolution-2k. Update Purpose after archive.
## Requirements
### Requirement: Maximum Camera Input Resolution Capping
The start script `start_uvc_pip.sh` SHALL pass `--camera-size 2560x1440` to the `uvctest` program when starting the pipeline in `v4l2` mode.

#### Scenario: Capping a 4K camera to 2K resolution
- **WHEN** starting the pipeline in `v4l2` mode with a 4K USB camera connected (supporting `3840x2160`)
- **THEN** the pipeline opens the camera at a resolution of at most `2560x1440` (2K).

