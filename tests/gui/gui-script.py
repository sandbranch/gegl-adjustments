# Runs inside GIMP on a Broadway display (tests/gui/start.sh). Makes a
# generated picture (sky, hills, a red roof, a gray wall, highlights and
# shadows: no photo), shows it in a window and, with ADJ_GUI_MODE:
#
#   layer    the layer is selected (for the filters on a layer)
#   channel  a channel "Lights" is added with the picture's gray and
#            selected, for a filter on a channel
#
# Then waits: when ADJ_GUI_OUT/save exists it saves the image as
# ADJ_GUI_OUT/gui-<mode>.xcf and writes ADJ_GUI_OUT/saved; when
# ADJ_GUI_OUT/quit exists (or after 10 minutes) it quits GIMP.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
import array
import math
import os
import time

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio  # noqa: E402

out = os.environ['ADJ_GUI_OUT']
mode = os.environ.get('ADJ_GUI_MODE', 'layer')
W, H = 640, 420


def picture():
    a = array.array('f')
    for y in range(H):
        for x in range(W):
            u, v = x / (W - 1), y / (H - 1)
            hill = 0.55 + 0.08 * math.sin(u * 7.0) + 0.05 * math.sin(u * 17.0 + 1.0)
            if v < hill:
                # sky: deep blue at the top, pale cyan near the hills, a sun
                t = v / hill
                r, g, b = 0.15 + 0.55 * t, 0.35 + 0.5 * t, 0.85 + 0.1 * t
                d = math.hypot(x - 0.8 * W, y - 0.18 * H)
                if d < 32:
                    r = g = b = 1.0
                    g = 0.97
            else:
                # hills: greens and yellows, darker below
                t = (v - hill) / (1 - hill)
                r = 0.25 + 0.45 * (0.5 + 0.5 * math.sin(u * 11.0)) * (1 - t)
                g = 0.55 * (1 - 0.7 * t) + 0.1
                b = 0.12 * (1 - t)
            # a house: gray wall, red roof, a magenta door
            if 0.18 < u < 0.38 and 0.62 < v < 0.86:
                r = g = b = 0.62
                if 0.26 < u < 0.30 and v > 0.74:
                    r, g, b = 0.7, 0.15, 0.55
            if 0.16 < u < 0.40 and 0.50 < v <= 0.62 and abs(u - 0.28) < (v - 0.50) * 1.0:
                r, g, b = 0.8, 0.18, 0.12
            a.extend((min(max(r, 0), 1), min(max(g, 0), 1), min(max(b, 0), 1), 1.0))
    return a


image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
layer = Gimp.Layer.new(image, 'picture', W, H, Gimp.ImageType.RGBA_IMAGE, 100,
                       Gimp.LayerMode.NORMAL)
image.insert_layer(layer, None, 0)
rect = Gegl.Rectangle.new(0, 0, W, H)
data = picture()
buf = layer.get_buffer()
buf.set(rect, "R'G'B'A float", data.tobytes())
buf.flush()
layer.update(0, 0, W, H)
if mode == 'channel':
    ch = Gimp.Channel.new(image, 'Lights', W, H, 50.0, Gegl.Color.new('black'))
    image.insert_channel(ch, None, 0)
    gray = array.array('f', (0.3 * data[i] + 0.59 * data[i + 1] + 0.11 * data[i + 2]
                             for i in range(0, len(data), 4)))
    cb = ch.get_buffer()
    cb.set(rect, "Y' float", gray.tobytes())
    cb.flush()
    ch.update(0, 0, W, H)
    ch.set_visible(True)
    image.set_selected_channels([ch])
else:
    image.set_selected_layers([layer])
image.clean_all()
display = Gimp.Display.new(image)
Gimp.displays_flush()
with open(os.path.join(out, 'ready'), 'w') as f:
    f.write('ready\n')

t0 = time.time()
while not os.path.exists(os.path.join(out, 'quit')) and time.time() - t0 < 600:
    if os.path.exists(os.path.join(out, 'save')):
        os.remove(os.path.join(out, 'save'))
        xcf = os.path.join(out, 'gui-%s.xcf' % mode)
        Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(xcf), None)
        with open(os.path.join(out, 'saved'), 'w') as f:
            f.write(xcf + '\n')
    time.sleep(0.3)

pdb = Gimp.get_pdb()
quit_proc = pdb.lookup_procedure('gimp-quit')
quit_config = quit_proc.create_config()
quit_config.set_property('force', True)
quit_proc.run(quit_config)
