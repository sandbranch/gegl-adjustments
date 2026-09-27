# Runs inside GIMP (tests/gimp-check.sh): runs Create Luminosity Masks
# (plug-ins/luminosity-masks, installed in the test profile) without a
# dialog on 8 bit and float linear images, from the visible image and
# from a layer, and checks the channels: their names, and their values
# against Kuyper's formulas on Photoshop's Gray (Lights n = L^(2^(n-1)),
# Darks n the same of 1 - L, Midtones n = (1 - Lights n)(1 - Darks n))
# and the powers series. Prints PASS or FAIL per check.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
import array
import os

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl  # noqa: E402

OUT = os.environ['ADJ_CHECK_OUT']
W, H = 96, 64
failed = []


def result(name, ok, msg=''):
    print('%s  plugin_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failed.append(name)


def scene():
    a = array.array('f')
    for y in range(H):
        for x in range(W):
            a.extend((x / (W - 1), y / (H - 1), ((x * 5 + y * 3) % 17) / 16.0, 1.0))
    return a


def run(image, drawables, **settings):
    proc = Gimp.get_pdb().lookup_procedure('plug-in-adj-luminosity-masks')
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    cfg.set_core_object_array('drawables', drawables)
    for k, v in settings.items():
        cfg.set_property(k, v)
    res = proc.run(cfg)
    return res.index(0), res.index(1) if res.index(0) == Gimp.PDBStatusType.SUCCESS else None


def level_value(l, kind, n, squares):
    def s(x):
        y = x
        for _ in range(1, n):
            y = y * y if squares else y * x
        return y
    if kind == 'Lights':
        return s(l)
    if kind == 'Darks':
        return s(1 - l)
    return (1 - s(l)) * (1 - s(1 - l))


data = scene()
for prec, own, tol in ((Gimp.Precision.U8_NON_LINEAR, "Y' float", 0.51 / 255),
                       (Gimp.Precision.FLOAT_LINEAR, 'Y float', 1e-5)):
    nick = prec.value_nick.replace('-', '_')
    for series in ('tk', 'powers'):
        image = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.RGB, prec)
        layer = Gimp.Layer.new(image, 'scene', W, H, Gimp.ImageType.RGBA_IMAGE, 100,
                               Gimp.LayerMode.NORMAL)
        image.insert_layer(layer, None, 0)
        rect = Gegl.Rectangle.new(0, 0, W, H)
        layer.get_buffer().set(rect, "R'G'B'A float", data.tobytes())
        layer.get_buffer().flush()
        # the gray as the layer holds its pixels
        px = array.array('f', layer.get_buffer().get(rect, 1.0, "R'G'B'A float",
                                                      Gegl.AbyssPolicy.NONE))
        gray = [min(max(0.3 * px[i] + 0.59 * px[i + 1] + 0.11 * px[i + 2], 0), 1)
                for i in range(0, len(px), 4)]
        status, names = run(image, [layer], series=series)
        want = ['%s %d' % (k, n) for k in ('Lights', 'Darks', 'Midtones') for n in range(1, 6)]
        got = [c.get_name() for c in image.get_channels()]
        result('%s_%s_channels' % (nick, series),
               status == Gimp.PDBStatusType.SUCCESS and names.split('\n') == want and
               got == want and len(image.get_layers()) == 1,
               '%s: %s' % (status.value_nick, ', '.join(got)))
        worst = 0.0
        for ch in image.get_channels():
            kind, n = ch.get_name().split()
            vals = array.array('f', ch.get_buffer().get(rect, 1.0, own, Gegl.AbyssPolicy.NONE))
            for v, l in zip(vals, gray):
                worst = max(worst, abs(v - level_value(l, kind, int(n), series == 'tk')))
        result('%s_%s_values' % (nick, series), worst <= tol + 1e-6,
               'max difference %.2g from the formulas' % worst)
        image.delete()

# from a layer, and fewer of them
image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
bottom = Gimp.Layer.new(image, 'bottom', W, H, Gimp.ImageType.RGB_IMAGE, 100, Gimp.LayerMode.NORMAL)
image.insert_layer(bottom, None, 0)
Gimp.context_set_foreground(Gegl.Color.new('#808080'))
bottom.edit_fill(Gimp.FillType.FOREGROUND)
top = Gimp.Layer.new(image, 'white', W, H, Gimp.ImageType.RGB_IMAGE, 100, Gimp.LayerMode.NORMAL)
image.insert_layer(top, None, 0)
top.edit_fill(Gimp.FillType.WHITE)
top.set_visible(False)
status, names = run(image, [top], source='layer', darks=False, midtones=False, levels=2)
rect = Gegl.Rectangle.new(0, 0, W, H)
vals = [set(array.array('f', c.get_buffer().get(rect, 1.0, "Y' float", Gegl.AbyssPolicy.NONE)))
        for c in image.get_channels()]
result('from_a_hidden_layer', names == 'Lights 1\nLights 2' and vals == [{1.0}, {1.0}],
       '%s, values %s' % (names and names.replace('\n', ', '), vals))
status, names = run(image, [top], source='visible', lights=False, midtones=False, levels=1)
darks = [c for c in image.get_channels() if c.get_name() == 'Darks 1']
vals = set(array.array('f', darks[0].get_buffer().get(
    rect, 1.0, "Y' float", Gegl.AbyssPolicy.NONE))) if darks else set()
want = round(1 - 0x80 / 255.0, 3)
result('from_the_visible_image', names == 'Darks 1' and {round(v, 3) for v in vals} == {want},
       'Darks 1 of 50 %% gray: %s' % vals)
image.delete()

print('%d failed' % len(failed))
with open(os.path.join(OUT, 'plugin-check.status'), 'w') as fh:
    fh.write('%d\n' % len(failed))
