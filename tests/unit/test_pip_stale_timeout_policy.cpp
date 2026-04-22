/** P3-U4: pip_overlay_stale_policy 纯函数（设计 v1.12）。 */
#include "my_uvc_pip/pip_overlay_stale_policy.hpp"

#include <cstdlib>
#include <vector>

using my_uvc_pip::pip_effective_stale_timeout_ms;
using my_uvc_pip::pip_presenter_overlay_ptr;
using my_uvc_pip::pip_stale_should_clear_to_bg;

int main()
{
	if (pip_effective_stale_timeout_ms(-1) != 5000)
		return 1;
	if (pip_effective_stale_timeout_ms(0) != 0)
		return 1;
	if (pip_effective_stale_timeout_ms(123) != 123)
		return 1;

	if (pip_stale_should_clear_to_bg(0, 4999, -1))
		return 1;
	if (!pip_stale_should_clear_to_bg(0, 5000, -1))
		return 1;
	if (pip_stale_should_clear_to_bg(100, 5000, 0))
		return 1;
	if (!pip_stale_should_clear_to_bg(0, 8000, 8000))
		return 1;

	std::vector<uint8_t> buf(16, 1);
	if (pip_presenter_overlay_ptr(buf, false, 0, 100, -1) != nullptr)
		return 1;
	if (pip_presenter_overlay_ptr(std::vector<uint8_t>{}, true, 0, 100, -1) != nullptr)
		return 1;
	if (pip_presenter_overlay_ptr(buf, true, 0, 4999, -1) == nullptr)
		return 1;
	if (pip_presenter_overlay_ptr(buf, true, 0, 5000, -1) != nullptr)
		return 1;
	if (pip_presenter_overlay_ptr(buf, true, 0, 5000, 0) == nullptr)
		return 1;

	return 0;
}
