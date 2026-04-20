#!/usr/bin/env bash
# P1-I3 / P5-T3: SONAME, NEEDED, exported symbols for libmy_uvc.so, uvctest, and static pip_helper.
# Run in SDK env after cross-build. Optional: READELF/NM prefixed for aarch64, e.g.
#   READELF=aarch64-buildroot-linux-gnu-readelf NM=aarch64-buildroot-linux-gnu-nm ./check_libmy_uvc_soname_exports.sh build-rv1126b
set -euo pipefail
BUILD_DIR="${1:?usage: $0 <build-dir e.g. build-rv1126b>}"
READELF="${READELF:-readelf}"
NM="${NM:-nm}"

SO="${BUILD_DIR}/libmy_uvc.so.1.0.0"
BIN="${BUILD_DIR}/uvctest"
ARCHIVE="${BUILD_DIR}/libmy_uvc_pip_helper.a"

for f in "${SO}" "${BIN}"; do
	if [[ ! -f "${f}" ]]; then
		echo "missing ${f}"
		exit 1
	fi
done

echo "=== SONAME / NEEDED (libmy_uvc.so) ==="
${READELF} -d "${SO}" | grep -E 'SONAME|NEEDED' || true

echo "=== Exported dynamic symbols (my_uvc_) ==="
${NM} -D --defined-only "${SO}" 2>/dev/null | grep ' my_uvc_' || true

if ! ${NM} -D --defined-only "${SO}" 2>/dev/null | grep -q ' my_uvc_load_ini_section_only'; then
	echo "FAIL: my_uvc_load_ini_section_only not in dynamic symbol table (expected MY_UVC_API export)"
	exit 1
fi

echo "=== NEEDED / RPATH (uvctest) ==="
${READELF} -d "${BIN}" | grep -E 'NEEDED|RPATH|RUNPATH' || true

if [[ -f "${ARCHIVE}" ]]; then
	echo "=== pip_helper symbols in libmy_uvc_pip_helper.a (defined T/t) ==="
	${NM} "${ARCHIVE}" 2>/dev/null | grep -E ' pip_helper_' | head -20 || true
else
	echo "=== (optional) missing ${ARCHIVE} — skip static lib symbol check ==="
fi

echo "check_libmy_uvc_soname_exports: ok"
