# Compares adj:selective-color with FFmpeg's selectivecolor filter and
# with the Python reference (tests/reference.py), and adj:black-and-white
# with the reference, on generated pixels (tests/crosscheck.sh runs it).
#
#   crosscheck.py <output folder>
#
# $ADJ_APPLY is the command that runs tests/adj-apply.c (with the build
# folder as its first argument). For every family alone and for all of
# them at once, Relative and Absolute:
#
#   - FFmpeg against the reference's copy of FFmpeg's integer arithmetic
#     (tests/reference.py selective_color_ffmpeg): must be the same, at 8
#     and at 16 bits, which shows the reference reads FFmpeg right;
#   - the operation on the same pixels in float against the reference in
#     double precision (the published formulas);
#   - the operation rounded to 8 and 16 bits against FFmpeg: FFmpeg
#     rounds each family's change before it adds them, so they may differ
#     by one code per family that changes the pixel.
#
# Prints PASS or FAIL per case and exits with 1 if any failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
import os
import random
import shlex
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference as ref  # noqa: E402

OUT = sys.argv[1]
APPLY = shlex.split(os.environ['ADJ_APPLY'])
W, H = 128, 64
failed = []
worst = {'float': 0.0, 'codes8': 0, 'codes16': 0, 'bw': 0.0}


def result(name, ok, msg=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    sys.stdout.flush()
    if not ok:
        failed.append(name)


def pixels8():
    """every combination of 0, 1, 51, 102, 127, 128, 129, 153, 204, 254,
    255 (1331 colors), then random colors, W x H in all"""
    levels = [0, 1, 51, 102, 127, 128, 129, 153, 204, 254, 255]
    px = [(r, g, b) for r in levels for g in levels for b in levels]
    rnd = random.Random(7)
    while len(px) < W * H:
        px.append((rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)))
    return np.array(px[:W * H], np.int64)


def ffmpeg(raw_in, pix_fmt, settings, relative):
    opts = ['correction_method=' + ('relative' if relative else 'absolute')]
    for fam, ink in settings.items():
        opts.append("%s='%s'" % (fam, ' '.join('%.6g' % (x / 100.0) for x in ink)))
    src = os.path.join(OUT, 'in.raw')
    dst = os.path.join(OUT, 'ffmpeg.raw')
    raw_in.tofile(src)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', pix_fmt,
                    '-s', '%dx%d' % (W, H), '-i', src, '-vf', 'selectivecolor=' + ':'.join(opts),
                    '-f', 'rawvideo', '-pix_fmt', pix_fmt, dst], check=True)
    dtype = np.uint8 if pix_fmt == 'rgb24' else np.dtype('<u2')
    return np.fromfile(dst, dtype).astype(np.int64).reshape(-1, 3)


def apply(rgb, op, props):
    """the operation on n x 3 floats (R'G'B', sRGB), alpha 1"""
    rgba = np.ones((rgb.shape[0], 4), np.float32)
    rgba[:, :3] = rgb
    src = os.path.join(OUT, 'op-in.raw')
    dst = os.path.join(OUT, 'op-out.raw')
    rgba.tofile(src)
    subprocess.run(APPLY + [src, dst, str(W), str(H), op] + props, check=True)
    return np.fromfile(dst, np.float32).reshape(-1, 4)[:, :3].astype(float)


def sc_props(settings, relative):
    p = ['method=' + ('relative' if relative else 'absolute')]
    for fam, ink in settings.items():
        for name, v in zip(ref.INKS, ink):
            if v:
                p.append('%s-%s=%.17g' % (fam, name, v))
    return p


def cases():
    rnd = random.Random(20260927)
    out = []
    for relative in (True, False):
        for fam in ref.FAMILIES:
            out.append((relative, {fam: [rnd.randint(-100, 100) for _ in range(4)]}))
            out.append((relative, {fam: [100, -100, 60, -40]}))
        for _ in range(3):
            out.append((relative, {f: [rnd.randint(-100, 100) for _ in range(4)]
                                   for f in ref.FAMILIES}))
    return out


p8 = pixels8()
rnd16 = np.random.default_rng(5)
p16 = np.clip(p8 * 257 + rnd16.integers(-200, 201, p8.shape), 0, 65535)
for i, (relative, settings) in enumerate(cases()):
    label = '%s_%s' % ('relative' if relative else 'absolute',
                       'all' if len(settings) > 1 else list(settings)[0])
    label += '_%d' % i
    n_active = len(settings)

    # FFmpeg against the reference's copy of its arithmetic
    f8 = ffmpeg(p8.astype(np.uint8), 'rgb24', settings, relative)
    e8 = ref.selective_color_ffmpeg(p8, settings, relative, 8)
    f16 = ffmpeg(p16.astype('<u2'), 'rgb48le', settings, relative)
    e16 = ref.selective_color_ffmpeg(p16, settings, relative, 16)
    same8 = int((f8 != e8).any(axis=1).sum())
    same16 = int((f16 != e16).any(axis=1).sum())
    result('ffmpeg_arithmetic_as_read_' + label, same8 == 0 and same16 == 0,
           'pixels that differ: %d of %d at 8 bits, %d at 16 bits' % (same8, W * H, same16))

    # the operation against the published formulas, in float
    x8 = p8 / 255.0
    got = apply(x8, 'adj:selective-color', sc_props(settings, relative))
    want = ref.selective_color(x8, settings, relative)
    d = float(np.abs(got - want).max())
    worst['float'] = max(worst['float'], d)
    result('op_matches_reference_' + label, d < 3e-6, 'max difference %.2g' % d)

    # the operation rounded, against FFmpeg; FFmpeg's whites and blacks
    # start at 129 and below 128 (the blog's > N/2 and < N/2 in integers),
    # the formulas at 127.5, so at 8 bits the whites of min = 128 get
    # 1/255 of their change in the operation, none in FFmpeg
    c8 = np.rint(got * 255).astype(np.int64)
    diff8 = int(np.abs(c8 - f8).max())
    worst['codes8'] = max(worst['codes8'], diff8)
    got16 = apply(p16 / 65535.0, 'adj:selective-color', sc_props(settings, relative))
    c16 = np.rint(got16 * 65535).astype(np.int64)
    diff16 = int(np.abs(c16 - f16).max())
    worst['codes16'] = max(worst['codes16'], diff16)
    limit = n_active + 1
    result('op_against_ffmpeg_' + label, diff8 <= limit and diff16 <= limit,
           'largest difference %d codes at 8 bits (%.2f %% of pixels differ), %d at 16 bits'
           ' (limit: %d families changing a pixel, +1)'
           % (diff8, 100.0 * (c8 != f8).any(axis=1).mean(), diff16, limit - 1))

# Black & White against the reference
rng = np.random.default_rng(11)
x = rng.random((W * H, 3))
for i, weights in enumerate([(40, 60, 40, 60, 20, 80), (-200, 300, 17, -45, 250, 0),
                             (120, 110, -10, -50, -50, 80)]):
    names = ('reds', 'yellows', 'greens', 'cyans', 'blues', 'magentas')
    got = apply(x, 'adj:black-and-white', ['%s=%g' % (n, v) for n, v in zip(names, weights)])
    want = ref.black_and_white(x, weights)
    d = float(np.abs(got - want[:, None]).max())
    worst['bw'] = max(worst['bw'], d)
    result('bw_matches_reference_%d' % i, d < 2e-6, 'max difference %.2g' % d)

print('largest differences: operation against the formulas %.2g; against FFmpeg '
      '%d codes at 8 bits, %d at 16 bits; Black & White %.2g'
      % (worst['float'], worst['codes8'], worst['codes16'], worst['bw']))
print('%d failed' % len(failed))
sys.exit(1 if failed else 0)
