#!/usr/bin/env bash
# P3-I4: 检查 pip_helper 源码是否仍含 fopen（v1.12 目标为产品路径无库内读盘；过渡期为告警/可 strict）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${ROOT}"

strict=0
if [[ "${1:-}" == "--strict" ]]; then
	strict=1
fi

hits=""
if command -v rg >/dev/null 2>&1; then
	hits=$(rg -n '\bfopen\s*\(' src/pip_helper src/pip_mjpeg.cpp 2>/dev/null || true)
else
	hits=$(grep -R -n 'fopen' src/pip_helper src/pip_mjpeg.cpp 2>/dev/null || true)
fi

if [[ -n "${hits}" ]]; then
	echo "check_pip_helper_no_product_fopen: transitional: fopen still present under src/pip_helper (remove when v1.12 NV12 API is done):"
	echo "${hits}" | head -n 20
	if [[ "${strict}" -eq 1 ]]; then
		echo "check_pip_helper_no_product_fopen: FAIL (--strict)"
		exit 1
	fi
	echo "check_pip_helper_no_product_fopen: WARN (pass without --strict)"
	exit 0
fi

echo "check_pip_helper_no_product_fopen: ok (no fopen in src/pip_helper or src/pip_mjpeg.cpp)"
exit 0
