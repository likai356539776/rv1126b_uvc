#include "my_uvc_internal.hpp"

my_uvc_err_t my_uvc_validate_submit_buffer(const my_uvc_opaque *impl, int channel_id, const void *data,
                                           size_t len)
{
	if (!impl || !data || len == 0)
		return MY_UVC_ERR_INVALID_ARG;
	if (channel_id < 0 || channel_id >= impl->cfg.channels || channel_id >= MY_UVC_MAX_CHANNELS)
		return MY_UVC_ERR_INVALID_ARG;
	return MY_UVC_OK;
}
