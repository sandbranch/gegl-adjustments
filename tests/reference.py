# Reference implementations in Python (numpy), for tests/crosscheck.py:
#
#   selective_color       Clement Boesch's formulas for Photoshop's
#                         Selective Color, as his blog post states them
#                         ("Understanding selective coloring in Adobe
#                         Photoshop", blog.pkh.me/p/22, 2017), in double
#                         precision on 0 to 1
#   selective_color_ffmpeg  the same as FFmpeg's vf_selectivecolor.c does
#                         it in integers (8 or 16 bits): each family's
#                         change rounded with lrintf, whites and blacks
#                         against 1 << (bits - 1), the sum clipped
#   black_and_white       Mark Ransom's formula for Photoshop's Black &
#                         White (Stack Overflow answer 55233732)
#
# Written for these tests from the published formulas; FFmpeg's code
# (LGPL-2.1+) was read for how it rounds.
#
# Copyright 2026 David
# SPDX-License-Identifier: LGPL-3.0-or-later
import numpy as np

FAMILIES = ('reds', 'yellows', 'greens', 'cyans', 'blues', 'magentas',
            'whites', 'neutrals', 'blacks')
INKS = ('cyan', 'magenta', 'yellow', 'black')


def _weights(c):
    """The scale of each family for pixels c (n x 3, 0 to 1), and whether
    the pixel is in the family, as the blog defines them."""
    mx = c.max(axis=1)
    mn = c.min(axis=1)
    md = c.sum(axis=1) - mx - mn
    r, g, b = c[:, 0], c[:, 1], c[:, 2]
    return {
        'reds': (r == mx, mx - md),
        'yellows': (b == mn, md - mn),
        'greens': (g == mx, mx - md),
        'cyans': (r == mn, md - mn),
        'blues': (b == mx, mx - md),
        'magentas': (g == mn, md - mn),
        'whites': ((r > 0.5) & (g > 0.5) & (b > 0.5), (mn - 0.5) * 2),
        'neutrals': (np.ones_like(r, bool), 1 - (np.abs(mx - 0.5) + np.abs(mn - 0.5))),
        'blacks': ((r < 0.5) & (g < 0.5) & (b < 0.5), (0.5 - mx) * 2),
    }


def selective_color(rgb, settings, relative=True):
    """rgb: n x 3 floats in 0 to 1; settings: {family: (c, m, y, k)} in
    percent. Returns n x 3."""
    c = np.clip(np.nan_to_num(np.asarray(rgb, float), nan=0.0), 0, 1)
    w = _weights(c)
    d = np.zeros_like(c)
    for fam, ink in settings.items():
        inside, scale = w[fam]
        use = inside & (scale > 0)
        k = ink[3] / 100.0
        for ch in range(3):
            a = ink[ch] / 100.0
            m = (1 - c[:, ch]) if relative else 1.0
            x = ((-1 - a) * k - a) * m
            d[:, ch] += np.where(use, np.clip(x, -c[:, ch], 1 - c[:, ch]) * scale, 0)
    return np.nan_to_num(np.asarray(rgb, float), nan=0.0) + d


def _lrintf(x):
    # lrintf rounds halves to even in the default rounding mode, as
    # numpy's rint does
    return np.rint(x).astype(np.int64)


def selective_color_ffmpeg(rgb_int, settings, relative=True, bits=8):
    """FFmpeg's integer version on n x 3 integers of the given depth."""
    full = (1 << bits) - 1
    mid = 1 << (bits - 1)
    v = np.asarray(rgb_int, np.int64)
    r, g, b = v[:, 0], v[:, 1], v[:, 2]
    mx = v.max(axis=1)
    mn = v.min(axis=1)
    md = v.sum(axis=1) - mx - mn
    flags = {
        'reds': r == mx, 'cyans': r == mn, 'greens': g == mx, 'magentas': g == mn,
        'blues': b == mx, 'yellows': b == mn,
        'whites': (r > mid) & (g > mid) & (b > mid),
        'neutrals': ((r | g | b) != 0) & ((r != full) | (g != full) | (b != full)),
        'blacks': (r < mid) & (g < mid) & (b < mid),
    }
    scales = {
        'reds': mx - md, 'greens': mx - md, 'blues': mx - md,
        'cyans': md - mn, 'magentas': md - mn, 'yellows': md - mn,
        'whites': (mn << 1) - full,
        'neutrals': (full * 2 - (np.abs((mx << 1) - full) + np.abs((mn << 1) - full)) + 1) >> 1,
        'blacks': full - (mx << 1),
    }
    norm = v.astype(np.float32) * np.float32(1.0 / full)
    adj = np.zeros_like(v)
    for fam in FAMILIES:
        if fam not in settings or not any(settings[fam]):
            continue
        ink = [np.float32(x / 100.0) for x in settings[fam]]
        use = flags[fam] & (scales[fam] > 0)
        s = scales[fam].astype(np.float32)
        for ch in range(3):
            value = norm[:, ch]
            lo = -value
            hi = np.float32(1.0) - value
            res = (np.float32(-1.0) - ink[ch]) * ink[3] - ink[ch]
            if relative:
                res = res * hi
            res = np.clip(np.float32(res), lo, hi).astype(np.float32)
            adj[:, ch] += np.where(use, _lrintf(res * s), 0)
    return np.clip(v + adj, 0, full)


def black_and_white(rgb, weights):
    """rgb: n x 3 floats; weights: reds, yellows, greens, cyans, blues,
    magentas in percent. Returns the gray, n."""
    w = np.asarray(weights, float) / 100.0
    v = np.asarray(rgb, float)
    mn = v.min(axis=1)
    mx = v.max(axis=1)
    p = v - mn[:, None]
    r, g, b = p[:, 0], p[:, 1], p[:, 2]
    cyan = np.minimum(g, b)
    mag = np.minimum(r, b)
    yel = np.minimum(r, g)
    gray = np.where(
        r == 0, mn + cyan * w[3] + (g - cyan) * w[2] + (b - cyan) * w[4],
        np.where(g == 0, mn + mag * w[5] + (r - mag) * w[0] + (b - mag) * w[4],
                 mn + yel * w[1] + (r - yel) * w[0] + (g - yel) * w[2]))
    return np.clip(gray, np.minimum(0, mn), np.maximum(1, mx))
