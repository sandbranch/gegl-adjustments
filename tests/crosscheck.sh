#!/bin/sh
# Compares adj:selective-color with FFmpeg's selectivecolor filter and
# with a Python reference of the published formulas (tests/crosscheck.py,
# tests/reference.py), and adj:black-and-white with its reference.
# FFmpeg and numpy run on this machine; the operations run through
# tests/adj-apply.c (tests/adj-apply.sh), built by tests/check.sh in
# tests/output/build-check (or $BUILD), inside the Flatpak GIMP or
# natively with GIMP_FLATPAK=0, isolated from the user's folders. Writes to tests/output/crosscheck. Lists the
# user's GIMP folders before and after and fails if anything changed.
# Exits non-zero if a case fails, 2 if ffmpeg or numpy are missing.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
build=${BUILD:-tests/output/build-check}
case $build in /*) ;; *) build=$top/$build ;; esac

command -v ffmpeg >/dev/null 2>&1 && ffmpeg -hide_banner -filters 2>/dev/null |
  grep -q ' selectivecolor ' || { echo "SKIP  no ffmpeg with the selectivecolor filter"; exit 2; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP  no numpy"; exit 2; }
[ -x "$build/adj-apply" ] || { echo "no $build/adj-apply: run tests/check.sh first" >&2; exit 2; }
rm -rf "$out/crosscheck"
mkdir -p "$out/crosscheck"
echo "FFmpeg: $(ffmpeg -version | head -1)"

snapshot_begin crosscheck
ADJ_APPLY="$here/adj-apply.sh"
export ADJ_APPLY
python3 "$here/crosscheck.py" "$out/crosscheck"
status=$?
snapshot_end crosscheck || status=1
exit $status
