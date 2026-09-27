/*
 * Automated checks for adj:selective-color, adj:black-and-white,
 * adj:blend-if and adj:luminosity-mask
 *
 * check.c
 * Copyright 2026 by David
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * Runs the operations on synthetic pixels and checks them against
 * reference implementations written here in double precision from the
 * published formulas (tests/reference.py has the same in Python, for
 * the comparison with FFmpeg), against values published for Photoshop,
 * and against what the formulas must give: families and hue ranges that
 * must not change, grays that stay gray, exact alpha at the split
 * points, monotonic fades, the mask series, identities. Also alpha, NaN,
 * infinities and values outside 0 to 1, 8, 16 and 32 bit integer, half
 * and float buffers, grayscale buffers, linear light and another color
 * space.
 *
 *   check <build folder>
 *
 * Prints PASS or FAIL per case and exits with 1 if any case failed.
 */

#include <gegl.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules.h"

#define WORK "R'G'B'A float"

static gint     n_failed   = 0;
static gint     n_passed   = 0;
static gint     n_messages = 0;  /* warnings and criticals from GLib and GEGL */

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

static void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...) G_GNUC_PRINTF (3, 4);

static void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...)
{
  printf ("%s  %s", ok ? "PASS" : "FAIL", name);
  if (format)
    {
      va_list args;
      gchar  *detail;

      va_start (args, format);
      detail = g_strdup_vprintf (format, args);
      va_end (args);
      printf (": %s", detail);
      g_free (detail);
    }
  printf ("\n");
  fflush (stdout);

  if (ok)
    n_passed++;
  else
    n_failed++;
}

/* running an operation ------------------------------------------------ */

/* Runs op with the properties (NULL terminated "name=value" strings) on
 * n pixels in in_format (a row of n pixels) and returns the result in
 * out_format; g_free it. */
static gpointer
run_fmt (const gchar        *op,
         const gchar *const *props,
         const Babl         *in_format,
         gconstpointer       in,
         gint                n,
         const Babl         *out_format)
{
  GeglBuffer *buf = gegl_buffer_new (GEGL_RECTANGLE (0, 0, n, 1), in_format);
  GeglNode   *graph, *src, *node;
  gpointer    out;
  gint        i;

  gegl_buffer_set (buf, NULL, 0, in_format, in, GEGL_AUTO_ROWSTRIDE);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                               "buffer", buf, NULL);
  node  = gegl_node_new_child (graph, "operation", op, NULL);
  for (i = 0; props && props[i]; i++)
    if (! node_set_from_string (node, props[i]))
      {
        report ("set property", FALSE, "%s %s", op, props[i]);
      }
  gegl_node_link (src, node);
  out = g_malloc0 ((gsize) n * babl_format_get_bytes_per_pixel (out_format));
  gegl_node_blit (node, 1.0, GEGL_RECTANGLE (0, 0, n, 1), out_format, out,
                  GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (graph);
  g_object_unref (buf);
  return out;
}

static gfloat *
run (const gchar        *op,
     const gchar *const *props,
     const gfloat       *in,
     gint                n)
{
  return run_fmt (op, props, babl_format (WORK), in, n, babl_format (WORK));
}

/* props from a printf format: a space separated list */
static gchar **
props_new (const gchar *format,
           ...) G_GNUC_PRINTF (1, 2);

static gchar **
props_new (const gchar *format,
           ...)
{
  va_list  args;
  gchar   *s, **v;

  va_start (args, format);
  s = g_strdup_vprintf (format, args);
  va_end (args);
  v = g_strsplit (s, " ", -1);
  g_free (s);
  return v;
}

static gfloat *
run_p (const gchar  *op,
       gchar       **props,
       const gfloat *in,
       gint          n)
{
  gfloat *out = run (op, (const gchar *const *) props, in, n);

  g_strfreev (props);
  return out;
}

static GRand *rnd;

/* n random opaque pixels in 0 to 1, with some grays, primaries and ties */
static gfloat *
random_pixels (gint n)
{
  gfloat *p = g_new (gfloat, 4 * n);
  gint    i, k;

  for (i = 0; i < n; i++)
    {
      for (k = 0; k < 3; k++)
        p[4 * i + k] = g_rand_double (rnd);
      switch (i % 17)
        {
        case 3: p[4 * i + 1] = p[4 * i]; break;                       /* tie */
        case 5: p[4 * i + 1] = p[4 * i + 2] = p[4 * i]; break;        /* gray */
        case 7: p[4 * i] = 1.0f; p[4 * i + 2] = 0.0f; break;          /* red to yellow */
        case 11: p[4 * i + 2] = g_rand_int_range (rnd, 0, 256) / 255.0f; break;
        default: break;
        }
      p[4 * i + 3] = (i % 5 == 0) ? g_rand_double (rnd) : 1.0f;
    }
  return p;
}

static gdouble
max_diff_rgb (const gfloat  *a,
              const gdouble *b,
              gint           n)
{
  gdouble d = 0.0;
  gint    i, k;

  for (i = 0; i < n; i++)
    for (k = 0; k < 3; k++)
      d = MAX (d, fabs ((gdouble) a[4 * i + k] - b[3 * i + k]));
  return d;
}

static gboolean
alpha_kept (const gfloat *in,
            const gfloat *out,
            gint          n)
{
  gint i;

  for (i = 0; i < n; i++)
    if (memcmp (&in[4 * i + 3], &out[4 * i + 3], sizeof (gfloat)) != 0)
      return FALSE;
  return TRUE;
}

static gboolean
all_finite (const gfloat *p,
            gint          n_floats)
{
  gint i;

  for (i = 0; i < n_floats; i++)
    if (! isfinite (p[i]))
      return FALSE;
  return TRUE;
}

/* ================================================================== */
/* Selective Color                                                     */
/* ================================================================== */

#define SC_OP "adj:selective-color"

static const gchar *const families[9] =
  { "reds", "yellows", "greens", "cyans", "blues", "magentas",
    "whites", "neutrals", "blacks" };
static const gchar *const inks[4] = { "cyan", "magenta", "yellow", "black" };

/* the reference: Bœsch's formulas as the blog states them, in double
 * precision, on 0 to 1 */
static void
sc_ref (const gdouble *rgb,
        gdouble        adj[9][4],   /* percent */
        gboolean       relative,
        gdouble       *out)
{
  gdouble c[3], max, min, mid, d[3] = { 0, 0, 0 };
  gint    f, k;

  for (k = 0; k < 3; k++)
    c[k] = CLAMP (isnan (rgb[k]) ? 0.0 : rgb[k], 0.0, 1.0);
  max = MAX (c[0], MAX (c[1], c[2]));
  min = MIN (c[0], MIN (c[1], c[2]));
  mid = c[0] + c[1] + c[2] - max - min;

  for (f = 0; f < 9; f++)
    {
      gboolean in_family;
      gdouble  scale;

      switch (f)
        {
        case 0: in_family = c[0] == max; scale = max - mid; break;
        case 1: in_family = c[2] == min; scale = mid - min; break;
        case 2: in_family = c[1] == max; scale = max - mid; break;
        case 3: in_family = c[0] == min; scale = mid - min; break;
        case 4: in_family = c[2] == max; scale = max - mid; break;
        case 5: in_family = c[1] == min; scale = mid - min; break;
        case 6: in_family = c[0] > 0.5 && c[1] > 0.5 && c[2] > 0.5;
                scale = (min - 0.5) * 2.0; break;
        case 7: in_family = TRUE;
                scale = 1.0 - (fabs (max - 0.5) + fabs (min - 0.5)); break;
        default: in_family = c[0] < 0.5 && c[1] < 0.5 && c[2] < 0.5;
                 scale = (0.5 - max) * 2.0; break;
        }
      if (! in_family || scale <= 0.0)
        continue;
      for (k = 0; k < 3; k++)
        {
          gdouble a  = adj[f][k] / 100.0;
          gdouble kk = adj[f][3] / 100.0;
          gdouble m  = relative ? 1.0 - c[k] : 1.0;
          gdouble x  = ((-1.0 - a) * kk - a) * m;

          d[k] += CLAMP (x, -c[k], 1.0 - c[k]) * scale;
        }
    }
  for (k = 0; k < 3; k++)
    out[k] = (isnan (rgb[k]) ? 0.0 : rgb[k]) + d[k];
}

static gchar **
sc_props (gdouble  adj[9][4],
          gboolean relative)
{
  GPtrArray *a = g_ptr_array_new ();
  gint       f, k;

  for (f = 0; f < 9; f++)
    for (k = 0; k < 4; k++)
      if (adj[f][k] != 0.0)
        g_ptr_array_add (a, g_strdup_printf ("%s-%s=%.17g", families[f],
                                             inks[k], adj[f][k]));
  g_ptr_array_add (a, g_strdup (relative ? "method=relative" : "method=absolute"));
  g_ptr_array_add (a, NULL);
  return (gchar **) g_ptr_array_free (a, FALSE);
}

static gfloat *
sc_run (gdouble       adj[9][4],
        gboolean      relative,
        const gfloat *in,
        gint          n)
{
  return run_p (SC_OP, sc_props (adj, relative), in, n);
}

static gdouble
sc_compare (gdouble       adj[9][4],
            gboolean      relative,
            const gfloat *in,
            gint          n)
{
  gfloat  *out = sc_run (adj, relative, in, n);
  gdouble *ref = g_new (gdouble, 3 * n);
  gdouble  d;
  gint     i;

  for (i = 0; i < n; i++)
    {
      gdouble rgb[3] = { in[4 * i], in[4 * i + 1], in[4 * i + 2] };

      sc_ref (rgb, adj, relative, &ref[3 * i]);
    }
  d = max_diff_rgb (out, ref, n);
  g_free (out);
  g_free (ref);
  return d;
}

static void
check_selective_color (void)
{
  const gint  n  = 20000;
  gfloat     *in = random_pixels (n);
  gdouble     adj[9][4];
  gint        f, k, m, t;

  printf ("-- Selective Color\n");

  /* each family alone, each ink alone and all four, both methods */
  for (m = 0; m < 2; m++)
    for (f = 0; f < 9; f++)
      {
        gdouble worst = 0.0;

        for (t = 0; t < 6; t++)
          {
            memset (adj, 0, sizeof (adj));
            if (t < 4)
              adj[f][t] = t % 2 ? -73.0 : 58.0;
            else
              for (k = 0; k < 4; k++)
                adj[f][k] = g_rand_double_range (rnd, -100.0, 100.0);
            if (t == 5)
              adj[f][0] = 100.0, adj[f][3] = -100.0;
            worst = MAX (worst, sc_compare (adj, m == 0, in, n));
          }
        {
          gchar *name = g_strdup_printf ("sc_%s_%s_matches_reference",
                                         families[f], m == 0 ? "relative" : "absolute");
          report (name, worst < 2e-6, "max difference %.2g", worst);
          g_free (name);
        }
      }

  /* all families at once, random settings */
  for (m = 0; m < 2; m++)
    {
      gdouble worst = 0.0;

      for (t = 0; t < 8; t++)
        {
          for (f = 0; f < 9; f++)
            for (k = 0; k < 4; k++)
              adj[f][k] = g_rand_double_range (rnd, -100.0, 100.0);
          worst = MAX (worst, sc_compare (adj, m == 0, in, n));
        }
      report (m == 0 ? "sc_all_families_relative_matches_reference"
                     : "sc_all_families_absolute_matches_reference",
              worst < 4e-6, "max difference %.2g", worst);
    }

  /* the results stay within 0 to 1 whatever the settings: the weights of
   * a pixel add up to at most 1 */
  {
    gboolean inside = TRUE;
    gdouble  worst_sum = 0.0;

    for (t = 0; t < 16; t++)
      {
        gfloat *out;
        gint    i;

        for (f = 0; f < 9; f++)
          for (k = 0; k < 4; k++)
            adj[f][k] = g_rand_boolean (rnd) ? 100.0 : -100.0;
        out = sc_run (adj, t % 2 == 0, in, n);
        for (i = 0; i < 4 * n; i++)
          if (i % 4 != 3 && (out[i] < -1e-6f || out[i] > 1.0f + 1e-6f))
            inside = FALSE;
        g_free (out);
      }
    /* the sum of the weights, directly */
    for (t = 0; t < n; t++)
      {
        gdouble c[3] = { in[4 * t], in[4 * t + 1], in[4 * t + 2] };
        gdouble max = MAX (c[0], MAX (c[1], c[2]));
        gdouble min = MIN (c[0], MIN (c[1], c[2]));
        gdouble mid = c[0] + c[1] + c[2] - max - min;
        gdouble sum = (max - mid) + (mid - min) +
                      MAX (0.0, 1.0 - fabs (max - 0.5) - fabs (min - 0.5)) +
                      MAX (0.0, 2.0 * min - 1.0) + MAX (0.0, 1.0 - 2.0 * max);

        worst_sum = MAX (worst_sum, sum);
      }
    report ("sc_results_stay_within_0_to_1", inside,
            "the weights of a pixel add up to at most %.9f", worst_sum);
  }

  /* Bœsch's measurements in Photoshop: the pixel (180, 100, 50), Reds,
   * Absolute. Cyan +60 % takes 48 from red, magenta -60 % adds 48 to
   * green, yellow -70 % adds 56 to blue; at +-100 % the changes stop at
   * red -56 and +24, green -31 and +49, blue -16 and +64; black with
   * cyan 40 % gives red +13 at black -40 % and -54 at black +20 %. */
  {
    const gfloat px[4] = { 180 / 255.0f, 100 / 255.0f, 50 / 255.0f, 1.0f };
    struct { gint ink; gdouble v; gint ch; gint want; gdouble cyan; } cases[] =
      {
        { 0,  60, 0, -48, 0 }, { 1, -60, 1,  48, 0 }, { 2, -70, 2,  56, 0 },
        { 0, 100, 0, -56, 0 }, { 0, -100, 0, 24, 0 },
        { 1, 100, 1, -31, 0 }, { 1, -100, 1, 49, 0 },
        { 2, 100, 2, -16, 0 }, { 2, -100, 2, 64, 0 },
        { 3, -40, 0,  13, 40 }, { 3, 20, 0, -54, 40 },
      };
    gboolean ok = TRUE;
    GString *s = g_string_new (NULL);
    guint    i;

    for (i = 0; i < G_N_ELEMENTS (cases); i++)
      {
        gfloat *out;
        gint    got;

        memset (adj, 0, sizeof (adj));
        adj[0][cases[i].ink] = cases[i].v;
        if (cases[i].cyan != 0)
          adj[0][0] = cases[i].cyan;
        out = sc_run (adj, FALSE, px, 1);
        got = (gint) lrint (out[cases[i].ch] * 255.0) -
              (gint) lrint (px[cases[i].ch] * 255.0);
        if (got != cases[i].want)
          ok = FALSE;
        g_string_append_printf (s, "%s%+d", i ? " " : "", got);
        g_free (out);
      }
    report ("sc_photoshop_values_of_the_blog", ok, "%s", s->str);
    g_string_free (s, TRUE);
  }

  /* Relative scales by the ink a color has: pure red has no cyan, so
   * adding cyan leaves it; Absolute takes half of the red at +50 % */
  {
    const gfloat red[4] = { 1, 0, 0, 1 };
    gfloat      *rel, *abs_;

    memset (adj, 0, sizeof (adj));
    adj[0][0] = 50.0;
    rel  = sc_run (adj, TRUE, red, 1);
    abs_ = sc_run (adj, FALSE, red, 1);
    report ("sc_relative_leaves_pure_red_with_cyan", rel[0] == 1.0f,
            "red %.6f", rel[0]);
    report ("sc_absolute_takes_half_the_red", fabsf (abs_[0] - 0.5f) < 1e-6f,
            "red %.6f", abs_[0]);
    g_free (rel);
    g_free (abs_);
  }

  /* a family changes only its own colors */
  {
    static const gfloat probes[][3] =
      {
        { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 0, 0, 1 },
        { 1, 0, 1 }, { 1, 1, 1 }, { 0, 0, 0 }, { 0.5f, 0.5f, 0.5f },
        { 0.8f, 0.7f, 0.6f }, { 0.2f, 0.3f, 0.1f }, { 0.9f, 0.2f, 0.4f },
      };
    /* which probes each family may change: bit i for probe i */
    static const guint allowed[9] =
      {
        /* reds: red the largest, above the middle channel */
        1 << 0 | 1 << 9 | 1 << 11,
        /* yellows: blue the smallest, below the middle channel */
        1 << 1 | 1 << 9 | 1 << 10,
        /* greens */
        1 << 2 | 1 << 10,
        /* cyans: red the smallest */
        1 << 3,
        /* blues */
        1 << 4,
        /* magentas: green the smallest */
        1 << 5 | 1 << 11,
        /* whites: min > 1/2, white itself too */
        1 << 6 | 1 << 9,
        /* neutrals: not black, white or fully saturated */
        1 << 8 | 1 << 9 | 1 << 10 | 1 << 11,
        /* blacks: max < 1/2 */
        1 << 10,
      };
    gfloat   in2[G_N_ELEMENTS (probes) * 4];
    gboolean ok = TRUE;
    GString *s  = g_string_new (NULL);
    guint    i;

    for (i = 0; i < G_N_ELEMENTS (probes); i++)
      {
        in2[4 * i] = probes[i][0];
        in2[4 * i + 1] = probes[i][1];
        in2[4 * i + 2] = probes[i][2];
        in2[4 * i + 3] = 1.0f;
      }
    for (f = 0; f < 9; f++)
      {
        gfloat *out;
        guint   changed = 0;
        guint   want    = allowed[f];

        /* inks added: each channel of a color in the family goes down
         * (black itself cannot: it stays) */
        memset (adj, 0, sizeof (adj));
        adj[f][0] = 40.0; adj[f][1] = 30.0; adj[f][2] = 20.0; adj[f][3] = 10.0;
        out = sc_run (adj, FALSE, in2, G_N_ELEMENTS (probes));
        for (i = 0; i < G_N_ELEMENTS (probes); i++)
          if (memcmp (&out[4 * i], &in2[4 * i], 3 * sizeof (gfloat)) != 0)
            changed |= 1u << i;
        if (changed != want)
          {
            ok = FALSE;
            g_string_append_printf (s, "%s changed 0x%x, expected 0x%x; ",
                                    families[f], changed, want);
          }
        g_free (out);
      }
    report ("sc_each_family_changes_only_its_colors", ok, "%s",
            ok ? "12 probe colors, 9 families" : s->str);
    g_string_free (s, TRUE);
  }

  /* all sliders at 0: the input as it is, bit for bit, NaN too; also in
   * 8 bits */
  {
    gfloat  *odd = g_memdup2 (in, 4 * sizeof (gfloat) * 64);
    gfloat  *out;
    guint8   u8[4 * 256], *o8;
    gint     i;

    odd[0] = NAN; odd[5] = INFINITY; odd[10] = -3.0f; odd[13] = 7.5f;
    memset (adj, 0, sizeof (adj));
    out = sc_run (adj, TRUE, odd, 64);
    report ("sc_zero_sliders_leave_the_image", memcmp (out, odd, 64 * 16) == 0, NULL);
    g_free (out);
    for (i = 0; i < 4 * 256; i++)
      u8[i] = g_rand_int_range (rnd, 0, 256);
    o8 = run_fmt (SC_OP, NULL, babl_format ("R'G'B'A u8"), u8, 256,
                  babl_format ("R'G'B'A u8"));
    report ("sc_zero_sliders_leave_8_bit_images", memcmp (o8, u8, sizeof (u8)) == 0, NULL);
    g_free (o8);
    g_free (odd);
  }

  /* alpha untouched; NaN counts as 0; infinities and values beyond 0 to
   * 1 keep what lies beyond */
  {
    gfloat  px[4 * 6] =
      {
        NAN, 0.2f, 0.1f, 0.5f,
        INFINITY, 0.3f, 0.2f, 1.0f,
        -INFINITY, 0.3f, 0.2f, 1.0f,
        1.5f, 0.3f, 0.2f, 0.25f,
        -0.5f, 0.3f, 0.9f, 1.0f,
        0.7f, 0.3f, 0.2f, NAN,
      };
    gfloat *out;
    gdouble ref[3];
    gboolean ok = TRUE;
    gint     i;

    for (f = 0; f < 9; f++)
      for (k = 0; k < 4; k++)
        adj[f][k] = 30.0 + f - 7.0 * k;
    out = sc_run (adj, TRUE, px, 6);
    report ("sc_alpha_untouched", alpha_kept (px, out, 6), NULL);
    report ("sc_nan_gives_finite_results", isfinite (out[0]) && isfinite (out[1]), NULL);
    report ("sc_infinities_stay", out[4] == INFINITY && out[8] == -INFINITY &&
            isfinite (out[5]) && isfinite (out[9]), NULL);
    for (i = 3; i < 5; i++)
      {
        gdouble rgb[3] = { px[4 * i], px[4 * i + 1], px[4 * i + 2] };

        sc_ref (rgb, adj, TRUE, ref);
        for (k = 0; k < 3; k++)
          if (fabs (out[4 * i + k] - ref[k]) > 2e-6)
            ok = FALSE;
      }
    report ("sc_values_beyond_0_to_1_keep_the_excess", ok,
            "1.5 becomes %.6f, -0.5 becomes %.6f", out[12], out[16]);
    g_free (out);
  }

  g_free (in);
}

/* ================================================================== */
/* Black & White                                                       */
/* ================================================================== */

#define BW_OP "adj:black-and-white"

/* Mark Ransom's formula, in double precision; w in percent */
static gdouble
bw_ref (const gdouble *w,
        gdouble        r,
        gdouble        g,
        gdouble        b)
{
  gdouble gray = MIN (r, MIN (g, b)), max = MAX (r, MAX (g, b)), lo = gray;

  r -= gray; g -= gray; b -= gray;
  if (r == 0)
    {
      gdouble cyan = MIN (g, b);

      g -= cyan; b -= cyan;
      gray += cyan * w[3] / 100 + g * w[2] / 100 + b * w[4] / 100;
    }
  else if (g == 0)
    {
      gdouble magenta = MIN (r, b);

      r -= magenta; b -= magenta;
      gray += magenta * w[5] / 100 + r * w[0] / 100 + b * w[4] / 100;
    }
  else
    {
      gdouble yellow = MIN (r, g);

      r -= yellow; g -= yellow;
      gray += yellow * w[1] / 100 + r * w[0] / 100 + g * w[2] / 100;
    }
  return CLAMP (gray, MIN (0.0, lo), MAX (1.0, max));
}

static gchar **
bw_props (const gdouble *w,
          const gchar   *extra)
{
  return props_new ("reds=%.17g yellows=%.17g greens=%.17g cyans=%.17g "
                    "blues=%.17g magentas=%.17g%s%s",
                    w[0], w[1], w[2], w[3], w[4], w[5],
                    extra ? " " : "", extra ? extra : "");
}

static void
check_black_and_white (void)
{
  const gint     n = 20000;
  gfloat        *in = random_pixels (n);
  const gdouble  defaults[6] = { 40, 60, 40, 60, 20, 80 };
  gdouble        w[6];
  gint           t, i, k;

  printf ("-- Black & White\n");

  /* against the reference, with the defaults and with random sliders */
  for (t = 0; t < 10; t++)
    {
      gfloat  *out;
      gdouble *ref = g_new (gdouble, 3 * n);
      gdouble  d;
      gchar   *name;

      for (k = 0; k < 6; k++)
        w[k] = t == 0 ? defaults[k] : g_rand_double_range (rnd, -200.0, 300.0);
      out = t == 0 ? run (BW_OP, NULL, in, n) : run_p (BW_OP, bw_props (w, NULL), in, n);
      for (i = 0; i < n; i++)
        {
          gdouble v = bw_ref (w, in[4 * i], in[4 * i + 1], in[4 * i + 2]);

          ref[3 * i] = ref[3 * i + 1] = ref[3 * i + 2] = v;
        }
      d = max_diff_rgb (out, ref, n);
      name = g_strdup_printf (t == 0 ? "bw_defaults_match_reference"
                                     : "bw_random_sliders_%d_match_reference", t);
      report (name, d < 2e-6, "max difference %.2g", d);
      g_free (name);
      if (t == 0)
        report ("bw_alpha_untouched", alpha_kept (in, out, n), NULL);
      g_free (out);
      g_free (ref);
    }

  /* Photoshop's defaults on primaries and secondaries: the slider value */
  {
    const gfloat px[4 * 6] =
      {
        1, 0, 0, 1,  1, 1, 0, 1,  0, 1, 0, 1,
        0, 1, 1, 1,  0, 0, 1, 1,  1, 0, 1, 1,
      };
    const gdouble want[6] = { 0.40, 0.60, 0.40, 0.60, 0.20, 0.80 };
    gfloat       *out = run (BW_OP, NULL, px, 6);
    gboolean      ok  = TRUE;
    GString      *s   = g_string_new (NULL);

    for (i = 0; i < 6; i++)
      {
        if (fabs (out[4 * i] - want[i]) > 1e-6 || out[4 * i] != out[4 * i + 1] ||
            out[4 * i] != out[4 * i + 2])
          ok = FALSE;
        g_string_append_printf (s, "%s%.4f", i ? " " : "", out[4 * i]);
      }
    report ("bw_defaults_on_primaries_and_secondaries", ok,
            "red yellow green cyan blue magenta: %s", s->str);
    g_string_free (s, TRUE);
    g_free (out);
  }

  /* grays stay as they are, with any sliders and every preset */
  {
    gfloat grays[4 * 33];
    const gchar *presets[] =
      { "custom", "default", "red-filter", "orange-filter", "yellow-filter",
        "green-filter", "blue-filter", "high-contrast-red-filter",
        "high-contrast-blue-filter", "infrared", "lighter", "darker",
        "darkest-channel", "brightest-channel" };
    gboolean ok = TRUE;
    guint    p;

    for (i = 0; i < 33; i++)
      grays[4 * i] = grays[4 * i + 1] = grays[4 * i + 2] = i / 32.0f,
      grays[4 * i + 3] = 1.0f;
    for (p = 0; p < G_N_ELEMENTS (presets); p++)
      {
        gchar  *pr = g_strdup_printf ("preset=%s", presets[p]);
        gdouble ws[6] = { -200, 300, 17, -45, 250, 0 };
        gfloat *out = run_p (BW_OP, bw_props (ws, pr), grays, 33);

        for (i = 0; i < 4 * 33; i++)
          if (out[i] != grays[i])
            ok = FALSE;
        g_free (out);
        g_free (pr);
      }
    report ("bw_neutral_stays_neutral", ok, "33 grays, 14 presets");
  }

  /* each slider acts only on its hues: a color of the red to yellow
   * sextant (blue smallest) depends on reds, yellows and greens only */
  {
    gfloat   px[4 * 400];
    gboolean ok = TRUE;
    gint     s;

    for (i = 0; i < 400; i++)
      {
        gfloat a = g_rand_double (rnd), b = g_rand_double (rnd);
        gfloat lo = MIN (a, b) * g_rand_double (rnd);

        /* sextant s: which channel is smallest */
        s = i % 3;
        px[4 * i + s] = lo;
        px[4 * i + (s + 1) % 3] = a;
        px[4 * i + (s + 2) % 3] = b;
        px[4 * i + 3] = 1.0f;
      }
    for (s = 0; s < 3; s++)
      {
        /* the three sliders that must not matter for smallest channel s:
         * blue smallest: cyans, blues, magentas; red smallest: reds,
         * yellows, magentas; green smallest: yellows, greens, cyans */
        static const gint unused[3][3] = { { 0, 1, 5 }, { 1, 2, 3 }, { 3, 4, 5 } };
        gdouble a[6] = { 40, 60, 40, 60, 20, 80 }, b[6];
        gfloat *oa, *ob;

        memcpy (b, a, sizeof (b));
        for (k = 0; k < 3; k++)
          b[unused[s][k]] = k == 1 ? 300 : -200;
        oa = run_p (BW_OP, bw_props (a, NULL), px, 400);
        ob = run_p (BW_OP, bw_props (b, NULL), px, 400);
        for (i = s; i < 400; i += 3)
          if (oa[4 * i] != ob[4 * i])
            ok = FALSE;
        g_free (oa);
        g_free (ob);
      }
    report ("bw_each_slider_acts_only_on_its_hues", ok, "400 colors, 3 sextants");
  }

  /* one slider on a pure primary: the gray is the slider's value, other
   * sliders do nothing */
  {
    const gfloat px[4 * 6] =
      {
        1, 0, 0, 1,  1, 1, 0, 1,  0, 1, 0, 1,
        0, 1, 1, 1,  0, 0, 1, 1,  1, 0, 1, 1,
      };
    gboolean ok = TRUE;

    for (k = 0; k < 6; k++)
      {
        gdouble ws[6] = { 0, 0, 0, 0, 0, 0 };
        gfloat *out;

        ws[k] = 70;
        out = run_p (BW_OP, bw_props (ws, NULL), px, 6);
        for (i = 0; i < 6; i++)
          if (fabs (out[4 * i] - (i == k ? 0.70 : 0.0)) > 1e-6)
            ok = FALSE;
        g_free (out);
      }
    report ("bw_slider_sets_the_gray_of_its_color", ok, NULL);
  }

  /* all sliders at 100 %: the largest channel; at 0 %: the smallest;
   * limits at 0 and 1 */
  {
    gfloat  *hi = run_p (BW_OP, g_strsplit ("preset=brightest-channel", " ", -1), in, n);
    gfloat  *lo = run_p (BW_OP, g_strsplit ("preset=darkest-channel", " ", -1), in, n);
    gboolean ok = TRUE;
    gdouble  big[6] = { 300, 300, 300, 300, 300, 300 };
    gdouble  small[6] = { -200, -200, -200, -200, -200, -200 };
    gfloat  *b, *s;

    for (i = 0; i < n; i++)
      {
        const gfloat *p = in + 4 * i;

        if (fabsf (hi[4 * i] - MAX (p[0], MAX (p[1], p[2]))) > 1e-6f ||
            fabsf (lo[4 * i] - MIN (p[0], MIN (p[1], p[2]))) > 1e-6f)
          ok = FALSE;
      }
    report ("bw_all_100_is_the_largest_channel_all_0_the_smallest", ok, NULL);
    b = run_p (BW_OP, bw_props (big, NULL), in, n);
    s = run_p (BW_OP, bw_props (small, NULL), in, n);
    ok = TRUE;
    for (i = 0; i < 4 * n; i++)
      if (i % 4 != 3 && (b[i] < 0 || b[i] > 1 || s[i] < 0 || s[i] > 1))
        ok = FALSE;
    report ("bw_results_within_0_to_1", ok, "sliders at 300 %% and -200 %%");
    g_free (hi); g_free (lo); g_free (b); g_free (s);
  }

  /* a preset replaces the sliders; Default is the sliders' defaults */
  {
    gdouble ws[6] = { 250, -100, 30, 7, 90, -40 };
    gfloat *a = run_p (BW_OP, bw_props (ws, "preset=default"), in, n);
    gfloat *b = run (BW_OP, NULL, in, n);

    report ("bw_preset_replaces_the_sliders", memcmp (a, b, 16 * n) == 0, NULL);
    g_free (a);
    g_free (b);
  }

  /* the tint: Graphite's test values for Photoshop's default tint and a
   * blue one (0 to 255, within 1), and the clipping toward the
   * luminosity with a red tint (within 0.51) */
  {
    struct { gfloat in[3]; const gchar *tint; gfloat want[3]; gfloat tol; } cases[] =
      {
        { { 200, 200, 200 }, "#e1d3b3", { 213, 199, 167 }, 1 },
        { {  50,  50,  50 }, "#e1d3b3", {  63,  49,  17 }, 1 },
        { { 200, 100,  50 }, "#e1d3b3", { 133, 119,  87 }, 1 },
        { { 200, 200, 200 }, "#1e3c78",   { 176, 202, 255 }, 1 },
        { {  50,  50,  50 }, "#1e3c78",   {  22,  52, 112 }, 1 },
        { { 200, 100,  50 }, "#1e3c78",   {  92, 122, 182 }, 1 },
        { {   1,   1,   1 }, "#ff0000",     { 3.33f,  0,  0 }, 0.51f },
        { {  38,  38,  38 }, "#ff0000",     { 126.67f, 0, 0 }, 0.51f },
        { {  75,  75,  75 }, "#ff0000",     { 250.01f, 0, 0 }, 0.51f },
        { {  78,  78,  78 }, "#ff0000",     { 255, 2.15f, 2.15f }, 0.51f },
        { { 129, 129, 129 }, "#ff0000",     { 255, 75, 75 }, 0.51f },
        { { 200, 200, 200 }, "#ff0000",     { 255, 176.43f, 176.43f }, 0.51f },
      };
    gboolean ok = TRUE;
    gdouble  worst = 0;
    guint    c;

    for (c = 0; c < G_N_ELEMENTS (cases); c++)
      {
        gfloat  px[4] = { cases[c].in[0] / 255, cases[c].in[1] / 255,
                          cases[c].in[2] / 255, 1 };
        gchar  *t = g_strdup_printf ("tint=true tint-color=%s", cases[c].tint);
        gfloat *out = run_p (BW_OP, g_strsplit (t, " ", -1), px, 1);

        for (k = 0; k < 3; k++)
          {
            gdouble d = fabs (out[k] * 255.0 - cases[c].want[k]);

            worst = MAX (worst, d);
            if (d > cases[c].tol)
              ok = FALSE;
          }
        g_free (out);
        g_free (t);
      }
    report ("bw_tint_matches_graphite_values", ok,
            "12 cases, largest difference %.3f of 255", worst);
  }

  /* the tinted color keeps the gray as its luminosity where nothing
   * clips, and without tint the result is gray */
  {
    gfloat  *g   = run (BW_OP, NULL, in, n);
    gfloat  *t   = run_p (BW_OP, g_strsplit ("tint=true tint-color=#c8b496", " ", -1),
                          in, n);
    gdouble  worst = 0;
    gboolean gray = TRUE;

    for (i = 0; i < n; i++)
      {
        const gfloat *p = t + 4 * i;
        gdouble       l = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];

        if (g[4 * i] != g[4 * i + 1] || g[4 * i] != g[4 * i + 2])
          gray = FALSE;
        if (MIN (p[0], MIN (p[1], p[2])) > 1e-4 && MAX (p[0], MAX (p[1], p[2])) < 1 - 1e-4)
          worst = MAX (worst, fabs (l - g[4 * i]));
      }
    report ("bw_without_tint_the_result_is_gray", gray, NULL);
    report ("bw_tint_keeps_the_luminosity", worst < 1e-6, "max difference %.2g", worst);
    g_free (g);
    g_free (t);
  }

  /* NaN and infinities give finite results; values beyond 0 to 1 */
  {
    gfloat  px[4 * 5] =
      {
        NAN, 0.5f, 0.2f, 1,  INFINITY, 0.1f, 0.2f, 1,  0.3f, -INFINITY, 0.5f, 1,
        2.0f, 1.5f, 1.2f, 1,  -0.5f, 0.2f, 0.1f, 1,
      };
    gfloat *out = run (BW_OP, NULL, px, 5);
    gfloat *tt  = run_p (BW_OP, g_strsplit ("tint=true", " ", -1), px, 5);
    gdouble r1  = bw_ref (defaults, 2.0, 1.5, 1.2);

    report ("bw_nan_and_infinities_give_finite_results",
            all_finite (out, 20) && all_finite (tt, 20), NULL);
    report ("bw_values_beyond_0_to_1", fabs (out[12] - r1) < 1e-6 && out[16] <= 0.2f,
            "(2, 1.5, 1.2) gives %.4f", out[12]);
    g_free (out);
    g_free (tt);
  }

  g_free (in);
}

/* ================================================================== */
/* Blend If                                                            */
/* ================================================================== */

#define BI_OP "adj:blend-if"

static gfloat
bi_alpha (const gchar *props,
          gfloat r, gfloat g, gfloat b)
{
  const gfloat px[4] = { r, g, b, 1.0f };
  gfloat      *out   = run_p (BI_OP, g_strsplit (props, " ", -1), px, 1);
  gfloat       a     = out[3];

  g_free (out);
  return a;
}

static void
check_blend_if (void)
{
  static const gchar *const chans[4] = { "gray", "red", "green", "blue" };
  gint c, i;

  printf ("-- Blend If\n");

  /* split sliders: exact alpha at and between the split points, for each
   * channel. The value v of the channel (for gray: a gray pixel) */
  for (c = 0; c < 4; c++)
    {
      gchar   *p = g_strdup_printf ("%s-black-low=50 %s-black-high=100 "
                                    "%s-white-low=150 %s-white-high=200",
                                    chans[c], chans[c], chans[c], chans[c]);
      const gfloat v[]    = { 0, 49, 50, 60, 75, 100, 101, 149, 150, 175, 190, 200, 201, 255 };
      const gfloat want[] = { 0, 0, 0, 0.2f, 0.5f, 1, 1, 1, 1, 0.5f, 0.2f, 0, 0, 0 };
      gboolean ok = TRUE;
      GString *s  = g_string_new (NULL);
      guint    j;

      for (j = 0; j < G_N_ELEMENTS (v); j++)
        {
          gfloat x = v[j] / 255.0f;
          gfloat a;

          /* the other channels at 0.5, which the defaults of the other
           * channels' sliders keep */
          a = c == 0 ? bi_alpha (p, x, x, x)
                     : bi_alpha (p, c == 1 ? x : 0.5f, c == 2 ? x : 0.5f, c == 3 ? x : 0.5f);
          if (fabsf (a - want[j]) > 2e-6f)
            ok = FALSE;
          g_string_append_printf (s, "%s%g", j ? " " : "", a);
        }
      {
        gchar *name = g_strdup_printf ("bi_%s_split_points", chans[c]);

        report (name, ok, "alpha at 0 49 50 60 75 100 101 149 150 175 190 200 201 255: %s",
                s->str);
        g_free (name);
      }
      g_string_free (s, TRUE);
      g_free (p);
    }

  /* unsplit sliders: the slider's own value is shown, as Adobe's help
   * describes (black 80: 79 hidden, 80 shown; white 235: 235 shown, 236
   * hidden) */
  {
    const gchar *p = "red-black-low=80 red-black-high=80 red-white-low=235 red-white-high=235";
    gfloat a79  = bi_alpha (p, 79 / 255.0f, 0.5f, 0.5f);
    gfloat a80  = bi_alpha (p, 80 / 255.0f, 0.5f, 0.5f);
    gfloat a235 = bi_alpha (p, 235 / 255.0f, 0.5f, 0.5f);
    gfloat a236 = bi_alpha (p, 236 / 255.0f, 0.5f, 0.5f);

    report ("bi_unsplit_sliders_keep_their_own_value",
            a79 == 0 && a80 == 1 && a235 == 1 && a236 == 0,
            "79: %g 80: %g 235: %g 236: %g", a79, a80, a235, a236);
  }

  /* the gray is 0.3 R + 0.59 G + 0.11 B */
  {
    const gchar *p = "gray-black-low=0 gray-black-high=255 gray-white-low=255 gray-white-high=255";
    gfloat a = bi_alpha (p, 0.8f, 0.4f, 0.1f);

    report ("bi_gray_is_photoshop_gray", fabsf (a - (0.3f * 0.8f + 0.59f * 0.4f + 0.11f * 0.1f)) < 1e-6f,
            "alpha %.6f", a);
  }

  /* the fade is monotonic, linear and smooth, and smooth is a smoothstep */
  {
    const gint n = 2001;
    gfloat    *px = g_new (gfloat, 4 * n);
    gboolean   ok = TRUE;
    gint       f;

    for (i = 0; i < n; i++)
      px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = -0.1f + 1.2f * i / (n - 1),
      px[4 * i + 3] = 1.0f;
    for (f = 0; f < 2; f++)
      {
        gchar  *p = g_strdup_printf ("gray-black-low=20 gray-black-high=120 "
                                     "gray-white-low=130 gray-white-high=240 fade=%s",
                                     f ? "smooth" : "linear");
        gfloat *out = run_p (BI_OP, g_strsplit (p, " ", -1), px, n);

        for (i = 1; i < n; i++)
          {
            gfloat v = px[4 * i];

            if (v <= 125 / 255.0f && out[4 * i + 3] < out[4 * (i - 1) + 3])
              ok = FALSE;
            if (v > 125 / 255.0f && px[4 * (i - 1)] >= 125 / 255.0f &&
                out[4 * i + 3] > out[4 * (i - 1) + 3])
              ok = FALSE;
          }
        g_free (out);
        g_free (p);
      }
    report ("bi_fade_is_monotonic", ok, "linear and smooth, 2001 values");
    g_free (px);
    {
      const gchar *p = "gray-black-low=0 gray-black-high=100 fade=smooth";
      gfloat q = bi_alpha (p, 25 / 255.0f, 25 / 255.0f, 25 / 255.0f);
      gfloat h = bi_alpha (p, 50 / 255.0f, 50 / 255.0f, 50 / 255.0f);

      report ("bi_smooth_is_smoothstep", fabsf (q - 0.15625f) < 1e-5f && fabsf (h - 0.5f) < 1e-5f,
              "at a quarter %.6f, at half %.6f", q, h);
    }
  }

  /* channels multiply; colors untouched, alpha multiplied */
  {
    const gchar *p = "red-black-low=0 red-black-high=255 blue-white-low=0 blue-white-high=255";
    gfloat px[4] = { 0.6f, 0.3f, 0.2f, 0.5f };
    gfloat *out = run_p (BI_OP, g_strsplit (p, " ", -1), px, 1);

    report ("bi_channels_multiply", fabsf (out[3] - 0.5f * 0.6f * 0.8f) < 1e-6f &&
            memcmp (out, px, 12) == 0, "alpha %.6f", out[3]);
    g_free (out);
  }

  /* the defaults leave the image as it is, bit for bit, also values
   * beyond 0 to 1 and NaN; NaN counts as 0 otherwise */
  {
    gfloat *in = random_pixels (4096);
    gfloat *out;
    gfloat  odd[8] = { NAN, -0.5f, 3.0f, 0.7f,  0.5f, 0.5f, 0.5f, NAN };

    in[0] = NAN; in[5] = -2.0f; in[10] = 5.0f; in[3] = INFINITY;
    out = run (BI_OP, NULL, in, 4096);
    report ("bi_defaults_leave_the_image", memcmp (out, in, 4096 * 16) == 0, NULL);
    g_free (out);
    g_free (in);
    out = run_p (BI_OP, g_strsplit ("red-black-low=10 red-black-high=10", " ", -1), odd, 2);
    report ("bi_nan_counts_as_0", out[3] == 0.0f && isnan (out[0]) && out[7] == 0.0f,
            "alpha %g and %g, the NaN color kept", out[3], out[7]);
    g_free (out);
  }
}

/* ================================================================== */
/* Luminosity Mask                                                     */
/* ================================================================== */

#define LM_OP "adj:luminosity-mask"

static gdouble
ps_gray (const gfloat *p)
{
  return 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
}

static gdouble
series_ref (gdouble x, gint n, gboolean squares)
{
  gdouble y = x;
  gint    i;

  for (i = 1; i < n; i++)
    y = squares ? y * y : y * x;
  return y;
}

static void
check_luminosity_mask (void)
{
  const gint n  = 20000;
  gfloat    *in = random_pixels (n);
  gint       i, level, s, kind;

  printf ("-- Luminosity Mask\n");

  /* Lights, Darks and Midtones 1 to 5, both series, against the formulas */
  for (kind = 0; kind < 3; kind++)
    for (s = 0; s < 2; s++)
      {
        static const gchar *const kinds[3] = { "lights", "darks", "midtones" };
        gdouble worst = 0;
        gchar  *name;

        for (level = 1; level <= 5; level++)
          {
            gfloat *out = run_p (LM_OP, props_new ("mask=%s level=%d series=%s",
                                                   kinds[kind], level,
                                                   s ? "powers" : "tk"),
                                 in, n);

            for (i = 0; i < n; i++)
              {
                gdouble l = CLAMP (ps_gray (in + 4 * i), 0, 1);
                gdouble want =
                  kind == 0 ? series_ref (l, level, !s) :
                  kind == 1 ? series_ref (1 - l, level, !s) :
                  (1 - series_ref (l, level, !s)) * (1 - series_ref (1 - l, level, !s));

                worst = MAX (worst, fabs (out[4 * i] - want));
                if (out[4 * i] != out[4 * i + 1] || out[4 * i] != out[4 * i + 2])
                  worst = 1;
              }
            if (kind == 0 && s == 0 && level == 1)
              report ("lm_mask_keeps_alpha", alpha_kept (in, out, n), NULL);
            g_free (out);
          }
        name = g_strdup_printf ("lm_%s_1_to_5_%s", kinds[kind], s ? "powers" : "tk");
        report (name, worst < 2e-6, "max difference %.2g", worst);
        g_free (name);
      }

  /* the TK definitions at one value: L = 0.6 */
  {
    const gfloat px[4] = { 0.6f, 0.6f, 0.6f, 1 };
    gfloat      *l2 = run_p (LM_OP, props_new ("mask=lights level=2"), px, 1);
    gfloat      *l3 = run_p (LM_OP, props_new ("mask=lights level=3"), px, 1);
    gfloat      *d2 = run_p (LM_OP, props_new ("mask=darks level=2"), px, 1);
    gfloat      *m1 = run_p (LM_OP, props_new ("mask=midtones level=1"), px, 1);

    report ("lm_tk_values_at_0.6",
            fabsf (l2[0] - 0.36f) < 1e-6f && fabsf (l3[0] - 0.1296f) < 1e-6f &&
            fabsf (d2[0] - 0.16f) < 1e-6f && fabsf (m1[0] - 0.24f) < 1e-6f,
            "Lights 2 %.4f (L*L), Lights 3 %.4f (L^4), Darks 2 %.4f, Midtones 1 %.4f "
            "((1 - L) L)", l2[0], l3[0], d2[0], m1[0]);
    g_free (l2); g_free (l3); g_free (d2); g_free (m1);
  }

  /* the luminosity choices: babl's Y' of the space, and CIE L* */
  {
    gfloat *y  = g_new (gfloat, n);
    gfloat *lab = g_new (gfloat, n);
    gfloat *o1 = run_p (LM_OP, props_new ("luminosity=luminance"), in, n);
    gfloat *o2 = run_p (LM_OP, props_new ("luminosity=lightness"), in, n);
    gdouble w1 = 0, w2 = 0;

    babl_process (babl_fish (babl_format (WORK), babl_format ("Y' float")), in, y, n);
    babl_process (babl_fish (babl_format (WORK), babl_format ("CIE L float")), in, lab, n);
    for (i = 0; i < n; i++)
      {
        w1 = MAX (w1, fabs (o1[4 * i] - CLAMP (y[i], 0, 1)));
        w2 = MAX (w2, fabs (o2[4 * i] - CLAMP (lab[i] / 100.0, 0, 1)));
      }
    report ("lm_luminance_is_babl_Y'", w1 < 1e-6, "max difference %.2g", w1);
    report ("lm_lightness_is_cie_L", w2 < 1e-5, "max difference %.2g", w2);
    g_free (y); g_free (lab); g_free (o1); g_free (o2);
  }

  /* tonal range: 1 inside, the feather's middle at 0.5, 0 beyond; a
   * feather of 0 is a hard edge that keeps the range's ends */
  {
    const gfloat v[]     = { 0, 63, 64, 80, 96, 128, 160, 176, 192, 193, 255 };
    const gfloat want_s[] = { 0, 0, 0, 0.5f, 1, 1, 1, 0.5f, 0, 0, 0 };
    gfloat       px[4 * G_N_ELEMENTS (v)];
    gfloat      *sm, *li, *hard;
    gboolean     ok = TRUE;
    guint        j;

    for (j = 0; j < G_N_ELEMENTS (v); j++)
      px[4 * j] = px[4 * j + 1] = px[4 * j + 2] = v[j] / 255.0f, px[4 * j + 3] = 1;
    sm   = run_p (LM_OP, props_new ("mask=tonal-range tone-low=96 tone-high=160 tone-feather=32"),
                  px, G_N_ELEMENTS (v));
    li   = run_p (LM_OP, props_new ("mask=tonal-range tone-low=96 tone-high=160 tone-feather=32 "
                                    "fade=linear"), px, G_N_ELEMENTS (v));
    hard = run_p (LM_OP, props_new ("mask=tonal-range tone-low=96 tone-high=160 tone-feather=0"),
                  px, G_N_ELEMENTS (v));
    for (j = 0; j < G_N_ELEMENTS (v); j++)
      {
        if (fabsf (sm[4 * j] - want_s[j]) > 2e-5f || fabsf (li[4 * j] - want_s[j]) > 2e-5f)
          ok = FALSE;
        if (hard[4 * j] != (v[j] >= 96 && v[j] <= 160 ? 1.0f : 0.0f))
          ok = FALSE;
      }
    report ("lm_tonal_range_and_feather", ok, "smooth, linear and hard, 11 tones");
    /* linear at a quarter of the feather: 0.75; smooth: 0.84375 */
    {
      gfloat q[4] = { 88 / 255.0f, 88 / 255.0f, 88 / 255.0f, 1 };
      gfloat *a = run_p (LM_OP, props_new ("mask=tonal-range tone-low=96 tone-high=160 "
                                           "tone-feather=32 fade=linear"), q, 1);
      gfloat *b = run_p (LM_OP, props_new ("mask=tonal-range tone-low=96 tone-high=160 "
                                           "tone-feather=32"), q, 1);

      report ("lm_feather_shapes", fabsf (a[0] - 0.75f) < 1e-5f && fabsf (b[0] - 0.84375f) < 1e-5f,
              "linear %.5f, smooth %.5f", a[0], b[0]);
      g_free (a);
      g_free (b);
    }
    g_free (sm); g_free (li); g_free (hard);
  }

  /* saturation range (HSV saturation) and hue range, with wrap around
   * red, weighted by saturation, grays not selected */
  {
    const gfloat px[4 * 7] =
      {
        1, 0, 0, 1,           /* red, sat 1, hue 0 */
        1, 0, 0.1f, 1,        /* hue 354 */
        1, 0.5f, 0.5f, 1,     /* sat 0.5, hue 0 */
        1, 1, 0, 1,           /* yellow, hue 60 */
        0.5f, 0.5f, 0.5f, 1,  /* gray */
        0, 0.2f, 1, 1,        /* hue about 228 */
        1, 0.25f, 0, 1,       /* hue 15 */
      };
    gfloat *sat = run_p (LM_OP, props_new ("mask=saturation saturation-low=80 saturation-high=100 "
                                           "saturation-feather=20 fade=linear"), px, 7);
    gfloat *hue = run_p (LM_OP, props_new ("mask=hue hue-center=0 hue-width=20 hue-feather=10 "
                                           "fade=linear"), px, 7);
    /* sat 0.5 is 30 below the range, beyond the feather of 20 */
    gboolean ok_s = fabsf (sat[0] - 1) < 1e-6f && sat[8] == 0 &&
                    sat[16] == 0 && fabsf (sat[4] - 1) < 1e-6f;
    /* hue 354: 6 degrees from 0, within the 10 of half the width: 1;
     * hue 15: 5 into the feather of 10: 0.5 */
    gboolean ok_h = fabsf (hue[0] - 1) < 1e-6f && fabsf (hue[4] - 1) < 1e-6f &&
                    fabsf (hue[8] - 0.5f) < 1e-6f && hue[12] == 0 && hue[16] == 0 &&
                    hue[20] == 0 && fabsf (hue[24] - 0.5f) < 1e-5f;

    report ("lm_saturation_range", ok_s, "sat 1: %g, sat 0.5: %g, gray: %g",
            sat[0], sat[8], sat[16]);
    report ("lm_hue_range_wraps_and_weights_by_saturation", ok_h,
            "red %g, 354: %g, pink %g, yellow %g, gray %g, blue %g, 15: %g",
            hue[0], hue[4], hue[8], hue[12], hue[16], hue[20], hue[24]);
    g_free (sat);
    g_free (hue);
  }

  /* invert, and the mask as transparency */
  {
    gfloat *a = run_p (LM_OP, props_new ("mask=lights level=2"), in, n);
    gfloat *b = run_p (LM_OP, props_new ("mask=lights level=2 invert=true"), in, n);
    gfloat *c = run_p (LM_OP, props_new ("mask=lights level=2 output=alpha"), in, n);
    gboolean inv = TRUE, alpha = TRUE;

    for (i = 0; i < n; i++)
      {
        if (fabsf (a[4 * i] + b[4 * i] - 1.0f) > 1e-6f)
          inv = FALSE;
        if (memcmp (&c[4 * i], &in[4 * i], 12) != 0 ||
            fabsf (c[4 * i + 3] - in[4 * i + 3] * a[4 * i]) > 1e-6f)
          alpha = FALSE;
      }
    report ("lm_invert", inv, NULL);
    report ("lm_output_as_transparency", alpha, "colors kept, alpha times the mask");
    g_free (a); g_free (b); g_free (c);
  }

  /* on a gray buffer without alpha (a channel in GIMP) the numbers are
   * taken as they are, in both curves and at 8 and 16 bits: Lights 1
   * leaves them, Lights 2 squares them */
  {
    static const struct { const gchar *name; const gchar *own; gdouble tol; } fmts[] =
      {
        { "Y float", "Y float", 1e-6 }, { "Y' float", "Y' float", 1e-6 },
        { "Y u8", "Y float", 0.51 / 255 }, { "Y' u8", "Y' float", 0.51 / 255 },
        { "Y u16", "Y float", 0.51 / 65535 },
      };
    const gchar *const l2[] = { "mask=lights", "level=2", NULL };
    gboolean     ok = TRUE;
    gdouble      worst = 0;
    guint        f;

    for (f = 0; f < G_N_ELEMENTS (fmts); f++)
      {
        const Babl *fmt = babl_format (fmts[f].name);
        const Babl *own = babl_format (fmts[f].own);
        gint        bpp = babl_format_get_bytes_per_pixel (fmt);
        guint8      raw[256 * 4];
        gfloat      x[256], y[256];
        gpointer    o1, o2;

        for (i = 0; i < 256; i++)
          x[i] = i / 255.0f;
        babl_process (babl_fish (own, fmt), x, raw, 256);
        babl_process (babl_fish (fmt, own), raw, x, 256);
        o1 = run_fmt (LM_OP, NULL, fmt, raw, 256, fmt);
        if (memcmp (o1, raw, 256 * bpp) != 0)
          ok = FALSE;
        o2 = run_fmt (LM_OP, l2, fmt, raw, 256, fmt);
        babl_process (babl_fish (fmt, own), o2, y, 256);
        for (i = 0; i < 256; i++)
          worst = MAX (worst, fabs (y[i] - (gdouble) x[i] * x[i]) - fmts[f].tol);
        g_free (o1);
        g_free (o2);
      }
    report ("lm_gray_buffers_are_taken_as_they_are", ok && worst <= 0,
            "Y and Y' in float, u8 and u16: Lights 1 keeps the numbers, Lights 2 "
            "squares them (worst beyond rounding %.2g)", MAX (worst, 0));
  }

  /* NaN counts as 0, the mask is within 0 to 1 */
  {
    gfloat px[4 * 4] = { NAN, NAN, NAN, 1,  INFINITY, 0, 0, 1,  -3, 2, 0.5f, 1,  0.5f, 0.5f, 0.5f, NAN };
    gboolean ok = TRUE;
    gint     k;

    for (k = 0; k < 6; k++)
      {
        static const gchar *const m[] =
          { "lights", "darks", "midtones", "tonal-range", "saturation", "hue" };
        gfloat *out = run_p (LM_OP, props_new ("mask=%s", m[k]), px, 4);

        for (i = 0; i < 4; i++)
          if (! (out[4 * i] >= 0 && out[4 * i] <= 1))
            ok = FALSE;
        g_free (out);
        out = run_p (LM_OP, props_new ("mask=%s output=alpha", m[k]), px, 4);
        for (i = 0; i < 3; i++)
          if (! (out[4 * i + 3] >= 0 && out[4 * i + 3] <= 1))
            ok = FALSE;
        if (out[15] != 0)
          ok = FALSE;
        g_free (out);
      }
    report ("lm_nan_and_infinities_give_masks_within_0_to_1", ok, NULL);
  }

  g_free (in);
}

/* ================================================================== */
/* all operations: formats, color spaces, messages                    */
/* ================================================================== */

/* runs every operation with some settings on buffers of other formats
 * and compares with the float result of the same numbers */
static void
check_formats (void)
{
  static const gchar *const ops[][2] =
    {
      { SC_OP, "reds-cyan=-40 reds-black=20 whites-yellow=35 neutrals-magenta=-25 "
               "blacks-black=30 method=absolute" },
      { BW_OP, "reds=120 blues=-50 tint=true" },
      { BI_OP, "gray-black-low=30 gray-black-high=90 green-white-low=180 green-white-high=230" },
      { LM_OP, "mask=midtones level=2 output=alpha" },
    };
  static const struct { const gchar *name; gdouble tol; } fmts[] =
    {
      { "R'G'B'A u8", 1.0 / 255 }, { "R'G'B'A u16", 2.0 / 65535 },
      { "R'G'B'A u32", 1e-6 }, { "R'G'B'A half", 2e-3 }, { "R'G'B'A float", 1e-7 },
      { "R'G'B' u8", 1.0 / 255 }, { "Y'A u8", 1.0 / 255 }, { "Y'A float", 1e-6 },
      { "RGBA float", 1e-6 }, { "RaGaBaA float", 1e-5 },
    };
  const gint n = 4096;
  guint      o, f;

  printf ("-- formats\n");
  for (o = 0; o < G_N_ELEMENTS (ops); o++)
    for (f = 0; f < G_N_ELEMENTS (fmts); f++)
      {
        const Babl *fmt  = babl_format (fmts[f].name);
        gint        bpp  = babl_format_get_bytes_per_pixel (fmt);
        gfloat     *base = random_pixels (n);
        gpointer    raw  = g_malloc (n * bpp);
        gfloat     *same = g_new (gfloat, 4 * n);
        gchar     **p    = g_strsplit (ops[o][1], " ", -1);
        gfloat     *want, *got;
        gpointer    got_raw;
        gdouble     worst = 0;
        gint        i;
        gchar      *name;
        gboolean    gray = strchr (fmts[f].name, 'Y') != NULL;
        gboolean    no_alpha = ! strchr (fmts[f].name, 'A');

        /* the pixels as that format holds them, in the working format */
        babl_process (babl_fish (babl_format (WORK), fmt), base, raw, n);
        babl_process (babl_fish (fmt, babl_format (WORK)), raw, same, n);
        want = run (ops[o][0], (const gchar *const *) p, same, n);
        got_raw = run_fmt (ops[o][0], (const gchar *const *) p, fmt, raw, n, fmt);
        got = g_new (gfloat, 4 * n);
        babl_process (babl_fish (fmt, babl_format (WORK)), got_raw, got, n);
        /* the expected result stored in the format, and read back */
        {
          gpointer tmp = g_malloc (n * bpp);

          babl_process (babl_fish (babl_format (WORK), fmt), want, tmp, n);
          babl_process (babl_fish (fmt, babl_format (WORK)), tmp, want, n);
          g_free (tmp);
        }
        for (i = 0; i < 4 * n; i++)
          {
            /* without alpha in the format, alpha changes cannot show */
            if (no_alpha && i % 4 == 3)
              continue;
            /* premultiplied: colors of (almost) transparent pixels mean
             * little */
            if (strstr (fmts[f].name, "Ra") && want[4 * (i / 4) + 3] < 1e-3f)
              continue;
            worst = MAX (worst, fabs (got[i] - want[i]));
          }
        name = g_strdup_printf ("%s_on_%s", ops[o][0] + 4, fmts[f].name);
        report (name, worst <= fmts[f].tol + 1e-7,
                "max difference %.2g%s", worst, gray ? " (gray)" : "");
        g_free (name);
        g_strfreev (p);
        g_free (base); g_free (raw); g_free (same); g_free (want);
        g_free (got); g_free (got_raw);
      }
}

/* Photoshop works on the document's values with their own curve; so on
 * an image in linear light the operations act on the sRGB curve's
 * values, and in another space on that space's values */
static void
check_spaces (void)
{
  const gint  n  = 4096;
  gfloat     *in = random_pixels (n);
  const gchar *const p[] = { "reds-cyan=-60", "neutrals-yellow=40", "method=relative", NULL };
  const Babl *lin = babl_format ("RGBA float");
  gfloat     *lin_in = g_new (gfloat, 4 * n);
  gfloat     *lin_out, *want, *got_perc;
  gdouble     worst = 0;
  gint        i;

  printf ("-- color spaces\n");
  babl_process (babl_fish (babl_format (WORK), lin), in, lin_in, n);
  lin_out = run_fmt (SC_OP, p, lin, lin_in, n, lin);
  want = run (SC_OP, p, in, n);
  got_perc = g_new (gfloat, 4 * n);
  babl_process (babl_fish (lin, babl_format (WORK)), lin_out, got_perc, n);
  for (i = 0; i < 4 * n; i++)
    worst = MAX (worst, fabs (got_perc[i] - want[i]));
  report ("linear_light_images_use_the_srgb_curve_values", worst < 1e-5,
          "max difference %.2g", worst);

  /* Adobe RGB: the same numbers give the same result in Adobe RGB's
   * own terms */
  {
    const Babl *adobe = babl_space ("Adobish");
    const Babl *af    = babl_format_with_space (WORK, adobe);
    gfloat     *out   = run_fmt (SC_OP, p, af, in, n, af);

    worst = 0;
    for (i = 0; i < 4 * n; i++)
      worst = MAX (worst, fabs (out[i] - want[i]));
    report ("other_spaces_use_their_own_values", worst < 2e-6,
            "Adobe RGB, max difference %.2g", worst);
    g_free (out);
  }
  g_free (in); g_free (lin_in); g_free (lin_out); g_free (want); g_free (got_perc);
}

/* the operations are there with their keys */
static void
check_registration (void)
{
  static const gchar *const ops[] = { SC_OP, BW_OP, BI_OP, LM_OP };
  guint i;

  printf ("-- registration\n");
  for (i = 0; i < G_N_ELEMENTS (ops); i++)
    {
      const gchar *menu = gegl_operation_get_key (ops[i], "gimp:menu-path");
      gchar       *name = g_strdup_printf ("%s_registered", ops[i] + 4);

      report (name, gegl_has_operation (ops[i]) && menu &&
              strcmp (menu, "<Image>/Colors") == 0,
              "%s, %s", gegl_operation_get_key (ops[i], "title"), menu);
      g_free (name);
    }
}

int
main (int    argc,
      char **argv)
{
  gchar *dir;

  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <build folder>\n", argv[0]);
      return 2;
    }
  setlocale (LC_ALL, "C");
  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (&argc, &argv);
  g_log_set_default_handler (log_handler, NULL);
  rnd = g_rand_new_with_seed (20260927);

  check_registration ();
  check_selective_color ();
  check_black_and_white ();
  check_blend_if ();
  check_luminosity_mask ();
  check_formats ();
  check_spaces ();

  report ("no_warnings_or_criticals", n_messages == 0, "%d", n_messages);

  g_rand_free (rnd);
  gegl_exit ();
  if (! g_getenv ("ADJ_CHECK_KEEP_MODULE"))
    modules_cleanup (dir);
  else
    g_free (dir);

  printf ("%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
