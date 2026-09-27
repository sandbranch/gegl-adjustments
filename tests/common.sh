# shellcheck shell=sh
# Sourced by the test scripts. Sets $here (tests), $top (the repository)
# and $out (tests/output), and defines:
#
#   in_sdk <command line>   runs it in the repository, inside the Flatpak
#                           GIMP's SDK (as gimp-plugin-devtools/gimp-build.sh
#                           does, see below) or natively with GIMP_FLATPAK=0
#   in_gimp <command...>    runs a command of the GIMP Flatpak (gegl,
#                           python3, gimp-console-3.2) with a throwaway
#                           home: HOME and the XDG folders are set inside
#                           the sandbox to tests/output/home, GIO uses no
#                           GVFS (GIO_USE_VFS=local), and the variables
#                           named in $pass_env are passed on
#   snapshot_begin          lists the user's GIMP folders (tests/snapshot.sh)
#   snapshot_end            lists them again and fails if anything changed
#
# Without the flatpak (GIMP_FLATPAK=0) in_gimp runs the command natively
# with the same throwaway home.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later

# ($here is set by the script that sources this one)
# shellcheck disable=SC2154
top=$(dirname "$here")
out=$here/output
mkdir -p "$out"

if [ -z "$GIMP_FLATPAK" ]; then
    if command -v flatpak >/dev/null 2>&1 && flatpak info org.gimp.GIMP >/dev/null 2>&1; then
        GIMP_FLATPAK=1
    else
        GIMP_FLATPAK=0
    fi
fi

test_home=$out/home
home_env="export GIO_USE_VFS=local HOME='$test_home' \
XDG_CONFIG_HOME='$test_home/.config' XDG_DATA_HOME='$test_home/.local/share' \
XDG_CACHE_HOME='$test_home/.cache' XDG_STATE_HOME='$test_home/.local/state' \
CCACHE_DIR='$test_home/.cache/ccache'"

# The same as gimp-build.sh, with two differences: "flatpak run
# --sandbox" does not mount ~/.var/app/org.gimp.GIMP at all, and the
# home is the throwaway one. (A plain "flatpak run --devel", as
# gimp-build.sh does it, rewrites Flatpak's loader cache in
# ~/.var/app/org.gimp.GIMP/.ld.so each time it switches between the SDK
# and the runtime, and the SDK's ccache and babl caches go to
# ~/.var/app/org.gimp.GIMP/cache.)
in_sdk () {
    mkdir -p "$test_home"
    if [ "$GIMP_FLATPAK" = 1 ]; then
        flatpak run --sandbox --devel --filesystem="$top" \
          --env=PKG_CONFIG_PATH=/app/lib/pkgconfig:/app/share/pkgconfig \
          --command=sh org.gimp.GIMP -c "$home_env; cd '$top' && $*"
    else
        (cd "$top" && sh -c "$home_env; $*")
    fi
}


# the command's words, quoted for sh -c
quote_args () {
    q=
    for a; do
        a=$(printf '%s\n' "$a" | sed "s/'/'\\\\''/g")
        q="$q '$a'"
    done
    printf '%s' "$q"
}

in_gimp () {
    mkdir -p "$test_home"
    if [ "$GIMP_FLATPAK" = 1 ]; then
        envs=
        for v in $pass_env; do
            eval "val=\${$v-}"
            envs="$envs --env=$v=$val"
        done
        # shellcheck disable=SC2086
        flatpak run --sandbox --no-documents-portal --filesystem="$top" $envs \
          --command=sh org.gimp.GIMP -c "$home_env; exec $(quote_args "$@")"
    else
        sh -c "$home_env; exec $(quote_args "$@")"
    fi
}

snapshot_begin () {
    "$here/snapshot.sh" > "$out/snapshot-before.txt"
}

# prints PASS or FAIL; returns 1 if anything changed
snapshot_end () {
    "$here/snapshot.sh" > "$out/snapshot-after.txt"
    if cmp -s "$out/snapshot-before.txt" "$out/snapshot-after.txt"; then
        echo "PASS  your GIMP folders are unchanged ($(wc -l < "$out/snapshot-after.txt") entries)"
        return 0
    fi
    echo "FAIL  your GIMP folders changed:"
    diff "$out/snapshot-before.txt" "$out/snapshot-after.txt" | head -20
    others=$(flatpak ps --columns=application 2>/dev/null | grep -c org.gimp.GIMP)
    [ "$others" -gt 0 ] &&
      echo "      ($others other GIMP Flatpak instances are running; a GIMP that is" \
           "not this test's can change these folders)"
    return 1
}
