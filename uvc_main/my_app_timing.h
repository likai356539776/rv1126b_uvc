#pragma once

// Build-time switch for timing/FPS statistics.
// Default: OFF (0). Enable via CMake option MY_APP_ENABLE_TIMING=ON, or add -DMY_APP_ENABLE_TIMING=1.
#ifndef MY_APP_ENABLE_TIMING
#define MY_APP_ENABLE_TIMING 0
#endif

#define MY_APP_TIMING_ENABLED (MY_APP_ENABLE_TIMING)

