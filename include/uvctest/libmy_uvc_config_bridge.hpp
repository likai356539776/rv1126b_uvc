#pragma once

#include "app_config.h"
#include "my_uvc/my_uvc.h"

/** Map merged AppConfig::libmy_uvc into libmy_uvc C API config (see LibmyUvcIniFields / config/libmy_uvc.ini). */
void uvctest_fill_my_uvc_config(const AppConfig &app, my_uvc_on_open_fn on_open, my_uvc_on_close_fn on_close,
                                 void *user_data, my_uvc_config_t *out);
