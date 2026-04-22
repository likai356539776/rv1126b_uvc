#!/usr/bin/env bash
# §5.0: 阶段 3 相关宿主机闸口 — PiP 单测 + pip_helper fopen 检查（不跑板端脚本）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"${ROOT}/scripts/run_unit_tests_host.sh" "$@"
"${ROOT}/tests/integration/check_pip_helper_no_product_fopen.sh" --strict

echo ""
echo "run_p3_host_smoke: ok"
