#!/bin/sh
# Prints a listing of the user's own folders of GIMP (every file and
# folder with its type, size and modification time, or "absent"), so
# that the test scripts can check that a test run changed none of them:
# they take one listing before and one after and compare them.
#
#   tests/snapshot.sh > before.txt
#
# Only names, sizes and times are read, never the contents of the files.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
for dir in \
    "$HOME/.config/GIMP/3.2" \
    "$HOME/.var/app/org.gimp.GIMP" \
    "$HOME/.local/share/gegl-0.4"
do
    if [ -e "$dir" ]; then
        find "$dir" -printf '%y %s %T@ %p\n' 2>/dev/null | LC_ALL=C sort -k4
    else
        echo "absent $dir"
    fi
done
