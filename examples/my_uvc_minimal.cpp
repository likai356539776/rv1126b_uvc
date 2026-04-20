/**
 * P1-T9: Minimal program linking libmy_uvc — API smoke (no UVC hardware required for create/destroy).
 */
#include "my_uvc/my_uvc.h"

#include <cstdio>
#include <cstdlib>

namespace {
int noop_open(void *, int, int, int, int) {
	return 0;
}
void noop_close(void *) {}
} // namespace

int main()
{
	std::printf("MY_UVC_API_VERSION=%d\n", MY_UVC_API_VERSION);
	my_uvc_config_t cfg{};
	cfg.channels = 1;
	cfg.width = 1920;
	cfg.height = 1080;
	cfg.is_mjpeg = 1;
	cfg.on_open = noop_open;
	cfg.on_close = noop_close;
	cfg.user_data = nullptr;

	my_uvc_t *ctx = my_uvc_create(&cfg);
	if (!ctx)
		return 1;
	my_uvc_destroy(ctx);
	return 0;
}
