#!/bin/sh
# The filters' dialogs in GIMP on a Broadway display, driven with a
# headless Chrome, for the screenshots in docs/: the Colors menu,
# Selective Color (Blues, split view), Black & White (the preset list,
# then Red filter with Tint), Blend If (split black slider), Luminosity
# Mask (Midtones), Create Luminosity Masks and the channels it makes;
# and the Luminosity Mask applied from the menu to a channel with OK,
# the image saved, and the XCF checked in headless GIMP: the channel
# keeps the filter, live. Run tests/gimp-check.sh first (it puts the
# modules and the plug-in in place). Lists the user's folders before
# and after and fails if anything changed.
#
#   tests/gui/shots.sh               all
#   ADJ_SHOTS="sc bw" tests/gui/shots.sh   some (menu sc bw bi lm plugin channel)
#
# Prints PASS or FAIL per step and exits non-zero if one failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
docs=$top/docs
mkdir -p "$docs"
status=0
pass () { echo "PASS  gui_$1"; }
fail () { echo "FAIL  gui_$1"; status=1; }

# the dialog: the topmost canvas that is neither the whole page nor the
# Colors menu (816 pixels high, and kept after it closes); Broadway
# draws each window as a canvas with its shadow (26 pixels here), the
# title bar's middle 36 pixels below the canvas's top
dialog_rect () {
    $cdp "$view" "eval:(() => { const c = [...document.querySelectorAll('canvas')]
      .map(e => [e.getBoundingClientRect(), Number(e.style.zIndex) || 0])
      .filter(([r, z], i, all) => r.width < 1200 && r.height > 150 && r.height < 780 &&
              getComputedStyle(all[i] ? document.querySelectorAll('canvas')[i] : null).display !== 'none')
      .sort((a, b) => b[1] - a[1])[0];
      return c ? [c[0].left, c[0].top, c[0].width, c[0].height].map(Math.round).join(',') : '' })()" \
      2>/dev/null | tail -1
}

# moves the dialog to the top right corner of the page; sets vx, vy (its
# visible top left corner) and vw (its visible width)
move_dialog () {
    r=$(dialog_rect)
    [ -n "$r" ] || return 1
    l=${r%%,*}; r=${r#*,}; t=${r%%,*}; r=${r#*,}; w=${r%%,*}
    vw=$((w - 52))
    vx=$((1278 - vw))
    vy=120
    gx=$((l + w / 2)); gy=$((t + 36))
    $cdp "$view" down:$gx,$gy move:$(( (gx + vx + vw / 2) / 2 )),$(( (gy + vy + 10) / 2 )) \
      move:$((vx + vw / 2)),$((vy + 10)) up:$((vx + vw / 2 + 1)),$((vy + 10)) wait:1200 >/dev/null
}
# a point in the dialog, from its visible top left corner
p () { echo "$((vx + $1)),$((vy + $2))"; }
# the x of value v on a slider from lo to hi (the bar starts 7 pixels in
# and ends 48 before the dialog's right edge)
slider () { echo $(( 7 + ($1 - $2) * (vw - 55) / ($3 - $2) )); }

MENU=321,49
open_filter () {
    $cdp "$view" click:$MENU wait:900 click:"$1" wait:4000 >/dev/null
    move_dialog || { fail "$2: no dialog"; return 1; }
    pass "$2: the dialog opened"
}

session () {
    name=$1
    ADJ_GUI_MODE=layer
    [ "$name" = channel ] && ADJ_GUI_MODE=channel
    export ADJ_GUI_MODE
    tries=0
    until start_gimp "$name"; do
        stop_all
        tries=$((tries + 1))
        [ $tries -lt 3 ] || { fail "$name: GIMP did not start (log: $gui_out/gimp-$name.log)"; return; }
    done
    case $name in
    menu)
        $cdp "$view" click:$MENU wait:1200 shot:"$docs/colors-menu.png" >/dev/null &&
          pass "menu: the Colors menu with the four filters";;
    sc)
        open_filter 377,847 sc || return
        $cdp "$view" click:"$(p 220 113)" wait:1200 click:"$(p 37 232)" wait:1000 \
          click:"$(p "$(slider 50 -100 100)" 140)" wait:1000 \
          click:"$(p "$(slider 40 -100 100)" 226)" wait:1000 \
          click:"$(p 296 306)" wait:5000 shot:"$docs/selective-color.png" >/dev/null &&
          pass "sc: Blues, cyan +50 and black +40, split view";;
    bw)
        open_filter 375,747 bw || return
        $cdp "$view" click:"$(p 285 113)" wait:1500 shot:"$docs/black-and-white-presets.png" \
          click:"$(p 47 174)" wait:1500 click:"$(p 18 307)" wait:5000 \
          shot:"$docs/black-and-white.png" >/dev/null &&
          pass "bw: the presets, then Red filter with Tint";;
    bi)
        open_filter 353,772 bi || return
        $cdp "$view" click:"$(p "$(slider 60 0 255)" 142)" wait:1200 \
          click:"$(p "$(slider 150 0 255)" 169)" wait:5000 shot:"$docs/blend-if.png" >/dev/null &&
          pass "bi: gray black slider split at about 60 and 150";;
    lm)
        open_filter 384,822 lm || return
        $cdp "$view" click:"$(p 220 113)" wait:1200 click:"$(p 30 174)" wait:1500 \
          click:"$(p "$(slider 2 1 5)" 142)" wait:5000 shot:"$docs/luminosity-mask.png" >/dev/null &&
          pass "lm: Midtones 2";;
    plugin)
        $cdp "$view" click:$MENU wait:900 click:411,722 wait:5000 >/dev/null
        move_dialog || { fail "plugin: no dialog"; return; }
        $cdp "$view" wait:800 shot:"$docs/create-luminosity-masks.png" >/dev/null
        # OK: the last button, bottom right (the plug-in's dialog has a
        # margin below its buttons); then the Channels tab
        r=$(dialog_rect); h=${r##*,}
        $cdp "$view" click:"$(p $((vw - 50)) $((h - 52 - 42)))" wait:10000 \
          click:1087,78 wait:1500 shot:"$docs/luminosity-mask-channels.png" >/dev/null
        pass "plugin: the dialog opened";;
    channel)
        open_filter 384,822 channel || return
        # OK, bottom right
        r=$(dialog_rect); h=${r##*,}
        $cdp "$view" click:"$(p $((vw - 50)) $((h - 52 - 22)))" wait:3000 \
          shot:"$docs/luminosity-mask-on-a-channel.png" >/dev/null
        touch "$gui_out/save"
        if wait_for "$gui_out/saved" 60; then
            pass "channel: the image saved with the filter on the channel"
        else
            fail "channel: not saved (log: $gui_out/gimp-channel.log)"
        fi;;
    esac
    stop_all
}

snapshot_begin gui
for s in ${ADJ_SHOTS:-menu sc bw bi lm plugin channel}; do
    session "$s"
done

# the channel's filter in the saved XCF, in headless GIMP
xcf=$gui_out/gui-channel.xcf
if [ -f "$xcf" ]; then
    GIMP3_DIRECTORY="$out/gimp-profile"
    GEGL_PATH="$out/gimp-check-modules:/app/lib/gegl-0.4"
    export GIMP3_DIRECTORY GEGL_PATH
    pass_env="GIMP3_DIRECTORY GEGL_PATH"
    run_prefix="--timeout=600" in_gimp gimp-console-3.2 --no-interface --no-data --no-fonts \
      --batch-interpreter python-fu-eval -b "
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio
img = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path('$xcf'))
for c in img.get_channels():
    print('CHANNEL', c.get_name(), [f.get_operation_name() for f in c.get_filters()])
" --quit >"$gui_out/xcf-check.log" 2>&1
    if grep -q "^CHANNEL Lights \['adj:luminosity-mask'\]" "$gui_out/xcf-check.log"; then
        pass "channel: after loading the XCF the channel Lights has the live filter adj:luminosity-mask"
    else
        fail "channel: the XCF's channel: $(grep CHANNEL "$gui_out/xcf-check.log")"
    fi
fi

snapshot_end gui || status=1
[ $status = 0 ] && echo "gui: all passed" || echo "gui: FAILED (screenshots in docs, logs in $gui_out)"
exit $status
