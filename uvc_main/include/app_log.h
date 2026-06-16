// Application log levels for rknn_my_app_demo (see MY_APP_LOG_LEVEL).
#pragma once

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_app_log_level;

void app_log_init_from_env(void);

#ifdef __cplusplus
}
#endif

// Always printed (init / I/O failures, wrong usage).
#define APP_LOGE(...) printf(__VA_ARGS__)

// MY_APP_LOG_LEVEL (default 3 if unset): 1 = errors only, 2 = +warnings, 3 = +info, 4 = +debug
#define APP_LOGW(...) do { if (g_app_log_level >= 2) printf(__VA_ARGS__); } while (0)
#define APP_LOGI(...) do { if (g_app_log_level >= 3) printf(__VA_ARGS__); } while (0)
#define APP_LOGD(...) do { if (g_app_log_level >= 4) printf(__VA_ARGS__); } while (0)
