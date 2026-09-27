#!/bin/sh
# Runs tests/adj-apply.c (built in tests/output/build-check, or $BUILD)
# isolated as the other tests (tests/common.sh), for tests/crosscheck.py:
#
#   tests/adj-apply.sh <in.raw> <out.raw> <width> <height> <operation> [property=value ...]
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
build=${BUILD:-tests/output/build-check}
case $build in /*) ;; *) build=$top/$build ;; esac
in_gimp "$build/adj-apply" "$build" "$@"
