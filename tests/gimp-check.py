# Runs inside GIMP (tests/gimp-check.sh): applies each operation to a
# synthetic image as a non-destructive filter (Gimp.DrawableFilter),
# checks that it is one, saves the image as XCF with the filter and loads
# it again (the settings must come back), merges the filters and compares
# the pixels with the same operation run in plain GEGL in this process,
# on RGB images of every precision and on a grayscale image. Also: Blend
# If on a layer without alpha, and the Luminosity Mask on a channel (the
# PDB refuses non-destructive filters on channels in GIMP 3.2; merging
# one works). Writes the scene and GIMP's float results as TIFF for the
# comparison with the gegl command line in gimp-check.sh. Prints PASS or
# FAIL per check.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
import array
import json
import math
import os
import random

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio, GLib  # noqa: E402

OUT = os.environ['ADJ_CHECK_OUT']
W, H = 256, 160
FMT = "R'G'B'A float"
failed = []


def result(name, ok, msg=''):
    print('%s  gimp_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failed.append(name)


# the enums of the operations, in the order of their values; GIMP's
# config takes them by nick, GEGL by number
ENUMS = {
    'adj:selective-color': {
        'colors': ['reds', 'yellows', 'greens', 'cyans', 'blues', 'magentas', 'whites',
                   'neutrals', 'blacks'],
        'method': ['relative', 'absolute']},
    'adj:black-and-white': {
        'preset': ['custom', 'default', 'red-filter', 'orange-filter', 'yellow-filter',
                   'green-filter', 'blue-filter', 'high-contrast-red-filter',
                   'high-contrast-blue-filter', 'infrared', 'lighter', 'darker',
                   'darkest-channel', 'brightest-channel']},
    'adj:blend-if': {
        'blend-if': ['gray', 'red', 'green', 'blue'],
        'fade': ['linear', 'smooth']},
    'adj:luminosity-mask': {
        'mask': ['lights', 'darks', 'midtones', 'tonal-range', 'saturation', 'hue'],
        'series': ['tk', 'powers'],
        'luminosity': ['gray', 'luminance', 'lightness'],
        'fade': ['smooth', 'linear'],
        'output': ['mask', 'alpha']},
}

TINT = '#3c78b4'
CASES = [
    ('selective_color_absolute', 'adj:selective-color', 'Selective Color',
     {'reds-cyan': -45.0, 'reds-black': 20.0, 'yellows-magenta': 30.0, 'blues-yellow': -35.0,
      'whites-black': -25.0, 'neutrals-yellow': 15.0, 'blacks-cyan': 20.0,
      'method': 'absolute', 'colors': 'whites'}),
    ('selective_color_relative', 'adj:selective-color', 'Selective Color',
     {'greens-magenta': 60.0, 'cyans-black': -40.0, 'magentas-cyan': 25.0,
      'neutrals-black': 10.0}),
    ('black_and_white_tint', 'adj:black-and-white', 'Black & White',
     {'reds': 120.0, 'yellows': 90.0, 'blues': -30.0, 'tint': True, 'tint-color': TINT}),
    ('black_and_white_preset', 'adj:black-and-white', 'Black & White',
     {'preset': 'infrared'}),
    ('blend_if', 'adj:blend-if', 'Blend If',
     {'gray-black-low': 30.0, 'gray-black-high': 90.0, 'blue-white-low': 180.0,
      'blue-white-high': 240.0, 'fade': 'smooth', 'blend-if': 'blue'}),
    ('luminosity_mask_midtones', 'adj:luminosity-mask', 'Luminosity Mask',
     {'mask': 'midtones', 'level': 2}),
    ('luminosity_mask_hue_alpha', 'adj:luminosity-mask', 'Luminosity Mask',
     {'mask': 'hue', 'hue-center': 200.0, 'hue-width': 40.0, 'output': 'alpha',
      'fade': 'linear'}),
]


def scene():
    """hues across, lightness down, some noise; the top rows half
    transparent"""
    rnd = random.Random(1)
    a = array.array('f')
    for y in range(H):
        for x in range(W):
            h = x / W * 6.0
            s = 0.2 + 0.8 * ((x * 7 + y * 3) % 11) / 10.0
            v = y / (H - 1)
            i = int(h) % 6
            f = h - int(h)
            p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
            r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
            n = (rnd.random() - 0.5) * 0.02
            alpha = 0.5 if y < 8 else 1.0
            a.extend((min(max(r + n, 0), 1), min(max(g + n, 0), 1), min(max(b + n, 0), 1), alpha))
    return a


def gegl_props(op, props):
    out = {}
    for k, v in props.items():
        if k in ENUMS[op]:
            out[k] = ENUMS[op][k].index(v)
        elif k == 'tint-color':
            out[k] = Gegl.Color.new(v)
        else:
            out[k] = v
    return out


def gegl_result(source, op, props, width, height):
    """the operation on the buffer source in plain GEGL, as R'G'B'A float"""
    rect = Gegl.Rectangle.new(0, 0, width, height)
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', source)
    node = g.create_child(op)
    for k, v in gegl_props(op, props).items():
        node.set_property(k, v)
    out = Gegl.Buffer.new(FMT, 0, 0, width, height)
    sink = g.create_child('gegl:write-buffer')
    sink.set_property('buffer', out)
    src.link(node)
    node.link(sink)
    sink.process()
    return out


def quantized(buf, image, like):
    """buf's pixels stored in a layer of the same type as like, and read
    back as R'G'B'A float"""
    rect = Gegl.Rectangle.new(0, 0, W, H)
    q = Gimp.Layer.new(image, 'q', W, H, like.type(), 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(q, None, 1)
    qb = q.get_buffer()
    qb.set(rect, FMT, buf.get(rect, 1.0, FMT, Gegl.AbyssPolicy.NONE))
    qb.flush()
    out = pixels(q)
    image.remove_layer(q)
    return out


def save_tiff(path, data):
    rect = Gegl.Rectangle.new(0, 0, W, H)
    buf = Gegl.Buffer.new(FMT, 0, 0, W, H)
    buf.set(rect, FMT, data.tobytes())
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', buf)
    save = g.create_child('gegl:tiff-save')
    save.set_property('path', path)
    src.link(save)
    save.process()


def pixels(drawable, fmt=FMT):
    rect = Gegl.Rectangle.new(0, 0, drawable.get_width(), drawable.get_height())
    return array.array('f', drawable.get_buffer().get(rect, 1.0, fmt, Gegl.AbyssPolicy.NONE))


def max_diff(a, b):
    if len(a) != len(b):
        return float('inf')
    return max(abs(p - q) for p, q in zip(a, b))


def new_image(precision, data, base=Gimp.ImageBaseType.RGB, alpha=True):
    image = Gimp.Image.new_with_precision(W, H, base, precision)
    if base == Gimp.ImageBaseType.RGB:
        kind = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    else:
        kind = Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE
    layer = Gimp.Layer.new(image, 'scene', W, H, kind, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    rect = Gegl.Rectangle.new(0, 0, W, H)
    buf = layer.get_buffer()
    buf.set(rect, FMT, data.tobytes())
    buf.flush()
    layer.update(0, 0, W, H)
    return image, layer


def add_filter(drawable, op, title, props):
    f = Gimp.DrawableFilter.new(drawable, op, title)
    cfg = f.get_config()
    for k, v in props.items():
        cfg.set_property(k, Gegl.Color.new(v) if k == 'tint-color' else v)
    f.update()
    drawable.append_filter(f)
    return f


def same_setting(got, want):
    if isinstance(want, float):
        # XCF keeps the filter's numbers as 32 bit floats
        return abs(got - want) < 1e-5
    if isinstance(want, str) and want.startswith('#'):
        a = got.get_rgba()
        b = Gegl.Color.new(want).get_rgba()
        return all(abs(x - y) < 1e-6 for x, y in zip(a, b))
    return got == want


def tol_for(precision):
    name = precision.value_nick
    if name.startswith('u8'):
        return 1.0 / 255 + 1e-6
    if name.startswith('u16'):
        return 2.0 / 65535
    if name.startswith('half'):
        return 2e-3
    return 2e-5


Gegl.init(None)
data = scene()
save_tiff(os.path.join(OUT, 'scene.tif'), data)
settings_out = {}

PRECISIONS = [Gimp.Precision.U8_NON_LINEAR, Gimp.Precision.U16_NON_LINEAR,
              Gimp.Precision.U32_LINEAR, Gimp.Precision.HALF_LINEAR,
              Gimp.Precision.FLOAT_NON_LINEAR, Gimp.Precision.FLOAT_LINEAR,
              Gimp.Precision.DOUBLE_NON_LINEAR]

for label, op, title, props in CASES:
    settings_out[label] = {'op': op, 'props': props}
    for precision in PRECISIONS + ['gray']:
        if precision == 'gray':
            name = label + '_gray_8_bit'
            image, layer = new_image(Gimp.Precision.U8_NON_LINEAR, data,
                                     Gimp.ImageBaseType.GRAY)
            prec = Gimp.Precision.U8_NON_LINEAR
        else:
            name = label + '_' + precision.value_nick.replace('-', '_')
            image, layer = new_image(precision, data)
            prec = precision
        # plain GEGL on the layer's pixels, stored in the layer's format
        want_buf = gegl_result(layer.get_buffer(), op, props, W, H)
        want = quantized(want_buf, image, layer)
        f = add_filter(layer, op, title, props)
        names = [x.get_operation_name() for x in layer.get_filters()]
        ok_filter = names == [op]

        # the filter survives an XCF save and load, with its settings
        xcf = os.path.join(OUT, name + '.xcf')
        Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(xcf), None)
        loaded = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(xcf))
        ll = loaded.get_layers()[0]
        lf = ll.get_filters()
        same = len(lf) == 1 and lf[0].get_operation_name() == op
        bad = []
        if same:
            cfg = lf[0].get_config()
            for k, v in props.items():
                got = cfg.get_property(k)
                if not same_setting(got, v):
                    same = False
                    bad.append('%s=%r' % (k, got))

        layer.merge_filters()
        got = pixels(layer)
        d = max_diff(got, want)
        ll.merge_filters()
        d2 = max_diff(pixels(ll), want)
        tol = tol_for(prec)
        ok = ok_filter and same and d <= tol and d2 <= tol
        result(name, ok, 'non-destructive %s, settings after XCF %s, against plain GEGL '
               '%.2g, after XCF %.2g' % (ok_filter, 'kept' if same else 'LOST ' + ', '.join(bad),
                                         d, d2))
        if precision == Gimp.Precision.FLOAT_NON_LINEAR:
            save_tiff(os.path.join(OUT, 'gimp-' + label + '.tif'), got)
        image.delete()
        loaded.delete()

# Blend If on a layer without alpha, over a red layer: the hidden pixels
# show the red while the filter is live, and GIMP adds an alpha channel
# when it merges the filter (the operation's "needs-alpha" key)
image, layer = new_image(Gimp.Precision.FLOAT_NON_LINEAR, data, alpha=False)
red = Gimp.Layer.new(image, 'red', W, H, Gimp.ImageType.RGB_IMAGE, 100, Gimp.LayerMode.NORMAL)
image.insert_layer(red, None, 1)
Gimp.context_set_foreground(Gegl.Color.new('#ff0000'))
red.edit_fill(Gimp.FillType.FOREGROUND)
add_filter(layer, 'adj:blend-if', 'Blend If', {'gray-black-low': 100.0, 'gray-black-high': 100.0})
# a dark pixel (bottom rows are light, top rows dark)
ok_pick, dark = image.pick_color([layer], 5, 20, True, False, 0)
ok_pick2, light = image.pick_color([layer], 5, H - 5, True, False, 0)
dr = dark.get_rgba()
lr = light.get_rgba()
result('blend_if_live_on_a_layer_without_alpha',
       ok_pick and abs(dr[0] - 1) < 1e-3 and dr[1] < 1e-3 and not
       (abs(lr[0] - 1) < 1e-3 and lr[1] < 1e-3),
       'the composite at a dark pixel %s, at a light one %s'
       % (tuple(round(v, 3) for v in dr[:3]), tuple(round(v, 3) for v in lr[:3])))
# merging it through the PDB does not add the alpha channel: GIMP's
# filter tool reads the "needs-alpha" key, the PDB's filters do not
# (GIMP 3.2.6), so the merged layer is opaque; noted, not checked
layer.merge_filters()
print('NOTE  gimp_blend_if_merged_through_the_pdb_on_a_layer_without_alpha: alpha channel '
      'afterwards: %s' % layer.has_alpha())
image.delete()

# the Luminosity Mask on a channel: the PDB refuses to make it
# non-destructive (GIMP 3.2: "only drawables of type GimpLayer can have
# non-destructive effects"); merged at once it works, with the channel's
# values taken as they are
for prec in (Gimp.Precision.U8_NON_LINEAR, Gimp.Precision.FLOAT_LINEAR):
    image, layer = new_image(prec, data)
    ch = Gimp.Channel.new(image, 'Lights 2', W, H, 50.0, Gegl.Color.new('black'))
    image.insert_channel(ch, None, 0)
    rect = Gegl.Rectangle.new(0, 0, W, H)
    ramp = array.array('f', [x / (W - 1) for y in range(H) for x in range(W)])
    buf = ch.get_buffer()
    own = 'Y\' float' if prec == Gimp.Precision.U8_NON_LINEAR else 'Y float'
    buf.set(rect, own, ramp.tobytes())
    buf.flush()
    before = array.array('f', ch.get_buffer().get(rect, 1.0, own, Gegl.AbyssPolicy.NONE))
    refused = ''
    f = Gimp.DrawableFilter.new(ch, 'adj:luminosity-mask', 'Luminosity Mask')
    f.get_config().set_property('mask', 'lights')
    f.get_config().set_property('level', 2)
    f.update()
    # libgimp reports the PDB's refusal as a message, not as an exception
    try:
        ch.append_filter(f)
    except GLib.Error as e:
        refused = e.message
    kept = [x.get_operation_name() for x in ch.get_filters()]
    ch.merge_filter(f)
    after = array.array('f', ch.get_buffer().get(rect, 1.0, own, Gegl.AbyssPolicy.NONE))
    tol = 1.0 / 255 if prec == Gimp.Precision.U8_NON_LINEAR else 1e-6
    d = max(abs(a - b * b) for a, b in zip(after, before))
    nick = prec.value_nick.replace('-', '_')
    result('channel_filter_refused_by_the_pdb_' + nick, kept == [],
           'filters on the channel after append_filter: %s %s' % (kept, refused))
    result('channel_merged_mask_is_the_square_' + nick, d <= tol + 1e-7,
           'Lights 2 of the channel\'s own values, max difference %.2g' % d)
    image.delete()

with open(os.path.join(OUT, 'cases.json'), 'w') as fh:
    json.dump(settings_out, fh)
print('%d failed' % len(failed))
with open(os.path.join(OUT, 'gimp-check.status'), 'w') as fh:
    fh.write('%d\n' % len(failed))
