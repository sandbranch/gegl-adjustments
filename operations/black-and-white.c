/*
 * Black & White: Photoshop's Black & White adjustment, a GEGL operation
 *
 * black-and-white.c
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
 * Turns an image gray with six hue weights (Reds, Yellows, Greens, Cyans,
 * Blues, Magentas) and optionally tints it, as Photoshop's Black & White
 * adjustment does.
 *
 * The gray is the formula Mark Ransom published on Stack Overflow
 * (answer 55233732, 2019, to "What Is the Algorithm Behind Photoshop's
 * Black and White Adjustment Layer?"), which he reports gives Photoshop's
 * values for the question's sample; Graphite's black_and_white node
 * (Apache-2.0) uses it too. The code here is written for this operation:
 *
 *   The smallest channel is gray and stays. What lies above it is split
 *   into a secondary color (the part two channels share) and a primary
 *   (the rest of the largest channel). For a pixel whose smallest
 *   channel is blue, with r' = r - min and g' = g - min:
 *     gray = min + Yellows * min (r', g') + Reds * (r' - min (r', g'))
 *                + Greens * (g' - min (r', g'))
 *   and likewise with Cyans (smallest red) and Magentas (smallest green).
 *
 * So each slider acts only on colors of its hue, grays stay as they are,
 * and with every slider at 100 % the gray is the largest channel, at 0 %
 * the smallest. Photoshop's defaults are Reds 40, Yellows 60, Greens 40,
 * Cyans 60, Blues 20, Magentas 80 (the same in psd-tools, Graphite and
 * Photoshop's own dialog as tutorials show it). The result is limited to
 * 0 to 1, as Photoshop's is, or to the pixel's own channels if they lie
 * beyond (float images).
 *
 * The tint gives the gray the tint color's hue and saturation the way
 * the Luminosity blend mode does (SetLum and ClipColor of the W3C
 * Compositing and Blending Level 1 specification, with its luminosity
 * 0.3 R + 0.59 G + 0.11 B). The default tint #e1d3b3 (hue 42 and
 * saturation 20 % in HSB) is Graphite's default for Photoshop's; not
 * verified against Photoshop.
 *
 * Photoshop works on the document's display values, so the operation
 * works on the image's values with its own curve (R'G'B' in the image's
 * space). NaN is taken as 0 and values beyond 10^6 as 10^6, so the
 * result is always finite.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (adj_black_and_white_preset)
  enum_value (ADJ_BW_CUSTOM,         "custom",          N_("Custom (the sliders below)"))
  enum_value (ADJ_BW_DEFAULT,        "default",         N_("Default"))
  enum_value (ADJ_BW_RED,            "red-filter",      N_("Red filter"))
  enum_value (ADJ_BW_ORANGE,         "orange-filter",   N_("Orange filter"))
  enum_value (ADJ_BW_YELLOW,         "yellow-filter",   N_("Yellow filter"))
  enum_value (ADJ_BW_GREEN,          "green-filter",    N_("Green filter"))
  enum_value (ADJ_BW_BLUE,           "blue-filter",     N_("Blue filter"))
  enum_value (ADJ_BW_HC_RED,         "high-contrast-red-filter",
                                                        N_("High contrast red filter"))
  enum_value (ADJ_BW_HC_BLUE,        "high-contrast-blue-filter",
                                                        N_("High contrast blue filter"))
  enum_value (ADJ_BW_INFRARED,       "infrared",        N_("Infrared look"))
  enum_value (ADJ_BW_LIGHTER,        "lighter",         N_("Lighter"))
  enum_value (ADJ_BW_DARKER,         "darker",          N_("Darker"))
  enum_value (ADJ_BW_DARKEST,        "darkest-channel", N_("Darkest channel (all 0 %)"))
  enum_value (ADJ_BW_BRIGHTEST,      "brightest-channel",
                                                        N_("Brightest channel (all 100 %)"))
enum_end (AdjBlackAndWhitePreset)

property_enum (preset, _("Preset"),
               AdjBlackAndWhitePreset, adj_black_and_white_preset,
               ADJ_BW_CUSTOM)
  description (_("Custom uses the sliders. Default is Photoshop's default "
                 "mix; the others are this filter's own mixes, named after "
                 "the color filters used with black and white film. A "
                 "preset other than Custom is used instead of the "
                 "sliders."))

#define ADJ_BW_SLIDER(name, label, def, blurb)                          \
  property_double (name, label, def)                                    \
    description (blurb)                                                 \
    value_range (-200.0, 300.0)                                         \
    ui_digits (0)                                                       \
    ui_meta ("unit", "percent")                                         \
    ui_meta ("sensitive", "preset {custom}")

ADJ_BW_SLIDER (reds,     _("Reds"),     40.0,
               _("How light reds become: 100 % keeps a red's own "
                 "brightest channel, 0 % makes it as dark as its darkest"))
ADJ_BW_SLIDER (yellows,  _("Yellows"),  60.0, _("How light yellows become"))
ADJ_BW_SLIDER (greens,   _("Greens"),   40.0, _("How light greens become"))
ADJ_BW_SLIDER (cyans,    _("Cyans"),    60.0, _("How light cyans become"))
ADJ_BW_SLIDER (blues,    _("Blues"),    20.0, _("How light blues become"))
ADJ_BW_SLIDER (magentas, _("Magentas"), 80.0, _("How light magentas become"))

#undef ADJ_BW_SLIDER

property_boolean (tint, _("Tint"), FALSE)
  description (_("Gives the gray image the hue and saturation of the tint "
                 "color, as the Luminosity blend mode does"))

property_color (tint_color, _("Tint color"), "#e1d3b3")
  description (_("The tint's hue and saturation; its lightness does not "
                 "matter. The default is hue 42, saturation 20 % (HSB)."))
  ui_meta ("sensitive", "tint")

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     black_and_white
#define GEGL_OP_C_SOURCE black-and-white.c

#include "gegl-op.h"

#include <math.h>

/* reds, yellows, greens, cyans, blues, magentas, in percent; the presets
 * other than Default are this operation's own */
static const gdouble presets[][6] =
  {
    [ADJ_BW_DEFAULT]   = {   40,  60,  40,  60,   20,   80 },
    [ADJ_BW_RED]       = {  120, 110, -10, -30,  -20,   80 },
    [ADJ_BW_ORANGE]    = {  100, 120,  10, -30,  -40,   40 },
    [ADJ_BW_YELLOW]    = {   70, 110,  40,   0,  -20,   40 },
    [ADJ_BW_GREEN]     = {  -20,  80, 120,  60,  -20,  -20 },
    [ADJ_BW_BLUE]      = {  -20, -10,  10, 110,  150,   80 },
    [ADJ_BW_HC_RED]    = {  160, 140, -40, -60,  -60,  100 },
    [ADJ_BW_HC_BLUE]   = {  -60, -40, -20, 140,  220,  100 },
    [ADJ_BW_INFRARED]  = {  -40, 230, 150, -70,  -80, -100 },
    [ADJ_BW_LIGHTER]   = {   60,  80,  60,  80,   40,  100 },
    [ADJ_BW_DARKER]    = {   20,  40,  20,  40,    0,   60 },
    [ADJ_BW_DARKEST]   = {    0,   0,   0,   0,    0,    0 },
    [ADJ_BW_BRIGHTEST] = {  100, 100, 100, 100,  100,  100 },
  };

#define ADJ_BW_LIMIT 1e6f

/* weights as fractions (reds, yellows, greens, cyans, blues, magentas),
 * and the tint in the working format with its luminosity */
typedef struct
{
  gfloat   w[6];
  gboolean tint;
  gfloat   tint_rgb[3];
  gfloat   tint_lum;
} Settings;

static void
settings_get (GeglOperation *operation,
              Settings      *s)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const gdouble   sliders[6] = { o->reds, o->yellows, o->greens,
                                 o->cyans, o->blues, o->magentas };
  const gdouble  *v = sliders;
  gint            i;

  if (o->preset != ADJ_BW_CUSTOM && o->preset < (gint) G_N_ELEMENTS (presets))
    v = presets[o->preset];
  for (i = 0; i < 6; i++)
    s->w[i] = CLAMP (v[i], -200.0, 300.0) / 100.0f;

  s->tint = o->tint && o->tint_color;
  if (s->tint)
    {
      const Babl *format = gegl_operation_get_format (operation, "output");
      gfloat      rgba[4];

      if (! format)
        format = babl_format ("R'G'B'A float");
      gegl_color_get_pixel (o->tint_color, format, rgba);
      for (i = 0; i < 3; i++)
        s->tint_rgb[i] = isfinite (rgba[i]) ? rgba[i] : 0.0f;
      s->tint_lum = 0.3f * s->tint_rgb[0] + 0.59f * s->tint_rgb[1] +
                    0.11f * s->tint_rgb[2];
    }
}

static void
prepare (GeglOperation *operation)
{
  const Babl *space  = gegl_operation_get_source_space (operation, "input");
  const Babl *format = babl_format_with_space ("R'G'B'A float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static inline gfloat
sane (gfloat v)
{
  if (isnan (v))
    return 0.0f;
  return v < -ADJ_BW_LIMIT ? -ADJ_BW_LIMIT : (v > ADJ_BW_LIMIT ? ADJ_BW_LIMIT : v);
}

/* the gray of Photoshop's Black & White for one pixel */
static inline gfloat
bw_gray (const gfloat *w,
         gfloat        r,
         gfloat        g,
         gfloat        b)
{
  const gfloat min = MIN (r, MIN (g, b));
  const gfloat max = MAX (r, MAX (g, b));
  gfloat       rr = r - min, gg = g - min, bb = b - min;
  gfloat       gray, s;

  if (rr == 0.0f)
    {
      /* the smallest is red: cyan, green, blue */
      s = MIN (gg, bb);
      gray = min + s * w[3] + (gg - s) * w[2] + (bb - s) * w[4];
    }
  else if (gg == 0.0f)
    {
      /* the smallest is green: magenta, red, blue */
      s = MIN (rr, bb);
      gray = min + s * w[5] + (rr - s) * w[0] + (bb - s) * w[4];
    }
  else
    {
      /* the smallest is blue: yellow, red, green */
      s = MIN (rr, gg);
      gray = min + s * w[1] + (rr - s) * w[0] + (gg - s) * w[2];
    }

  /* 0 to 1, as in Photoshop, or the pixel's own range beyond */
  return CLAMP (gray, MIN (0.0f, min), MAX (1.0f, max));
}

static gboolean
process (GeglOperation       *operation,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
  const gfloat *in  = in_buf;
  gfloat       *out = out_buf;
  Settings      s;
  glong         i;

  (void) roi;
  (void) level;

  settings_get (operation, &s);

  for (i = 0; i < n_pixels; i++)
    {
      const gfloat gray = bw_gray (s.w, sane (in[0]), sane (in[1]), sane (in[2]));

      if (! s.tint)
        {
          out[0] = out[1] = out[2] = gray;
        }
      else
        {
          /* SetLum (tint, gray), then ClipColor, in double precision so
           * that large values cannot overflow */
          gdouble c[3], l = gray, n, x;
          gint    k;

          for (k = 0; k < 3; k++)
            c[k] = (gdouble) s.tint_rgb[k] + (l - s.tint_lum);
          l = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
          n = MIN (c[0], MIN (c[1], c[2]));
          x = MAX (c[0], MAX (c[1], c[2]));
          for (k = 0; k < 3; k++)
            {
              if (n < 0.0 && l - n > 0.0)
                c[k] = l + (c[k] - l) * l / (l - n);
              if (x > 1.0 && x - l > 0.0)
                c[k] = l + (c[k] - l) * (1.0 - l) / (x - l);
            }
          for (k = 0; k < 3; k++)
            out[k] = (gfloat) c[k];
        }
      out[3] = in[3];

      in  += 4;
      out += 4;
    }

  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationPointFilterClass *point_class     =
    GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  operation_class->prepare        = prepare;
  operation_class->opencl_support = FALSE;
  point_class->process            = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "adj:black-and-white",
    "title",           _("Black & White"),
    "categories",      "color",
    "description",     _("Turns the image gray with a weight for each of "
                         "reds, yellows, greens, cyans, blues and magentas, "
                         "and can tint it, as Photoshop's Black & White "
                         "adjustment does"),
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Black & White..."),
    NULL);
}

#endif
