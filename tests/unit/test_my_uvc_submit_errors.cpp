/**
 * P1-U2: submit argument validation (shared with lib via my_uvc_validate_submit_buffer).
 */
#include "my_uvc_internal.hpp"
#include "my_uvc/my_uvc.h"

#include <vector>

static int check(my_uvc_err_t got, my_uvc_err_t want)
{
	return got == want ? 0 : 1;
}

int main()
{
	std::vector<uint8_t> buf(16, 0);
	my_uvc_opaque impl{};
	impl.cfg.channels = 2;
	impl.cfg.width = 1920;
	impl.cfg.height = 1080;

	if (check(my_uvc_validate_submit_buffer(nullptr, 0, buf.data(), 1), MY_UVC_ERR_INVALID_ARG))
		return 1;
	if (check(my_uvc_validate_submit_buffer(&impl, 0, nullptr, 1), MY_UVC_ERR_INVALID_ARG))
		return 1;
	if (check(my_uvc_validate_submit_buffer(&impl, 0, buf.data(), 0), MY_UVC_ERR_INVALID_ARG))
		return 1;
	if (check(my_uvc_validate_submit_buffer(&impl, -1, buf.data(), 1), MY_UVC_ERR_INVALID_ARG))
		return 1;
	if (check(my_uvc_validate_submit_buffer(&impl, 2, buf.data(), 1), MY_UVC_ERR_INVALID_ARG))
		return 1;
	if (check(my_uvc_validate_submit_buffer(&impl, 0, buf.data(), 1), MY_UVC_OK))
		return 1;

	return 0;
}
