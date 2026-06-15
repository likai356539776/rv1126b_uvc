#pragma once

#include "my_uvc/my_uvc.h"

struct my_uvc_opaque {
	my_uvc_config_t cfg;
};

my_uvc_err_t my_uvc_validate_submit_buffer(const my_uvc_opaque *impl, int channel_id, const void *data,
                                           size_t len);
