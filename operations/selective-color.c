/*
 * Selective Color: Photoshop's Selective Color adjustment, a GEGL operation
 *
 * selective-color.c
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
 * Adds or removes cyan, magenta, yellow and black in nine families of
 * colors (Reds, Yellows, Greens, Cyans, Blues, Magentas, Whites, Neutrals
 * and Blacks), relative to the amount a pixel has or absolutely, as
 * Photoshop's Selective Color adjustment does.
 *
 * The formulas are those Clément Bœsch reverse engineered from Photoshop
 * and published in "Understanding selective coloring in Adobe Photoshop"
 * (blog.pkh.me/p/22, 2017), which he implemented in FFmpeg's
 * libavfilter/vf_selectivecolor.c (LGPL-2.1+). Graphite's selective_color
 * node (Apache-2.0) was read too. The code here is written for this
 * operation, in floating point:
 *
 *   For a pixel (r, g, b) in 0 to 1, each family has a weight w:
 *     Reds, Greens, Blues        max - mid, when r, g or b is the largest
 *     Cyans, Magentas, Yellows   mid - min, when r, g or b is the smallest
 *     Whites                     2 min - 1, when min > 1/2
 *     Blacks                     1 - 2 max, when max < 1/2
 *     Neutrals                   1 - |max - 1/2| - |min - 1/2|
 *   (max, mid and min of r, g and b). Cyan acts on red, magenta on green
 *   and yellow on blue; with the family's slider a for that ink and k for
 *   black (both -1 to 1), a channel v changes by
 *     w * clamp (((-1 - a) k - a) * m, -v, 1 - v)
 *   with m = 1 for Absolute and m = 1 - v for Relative. The changes of
 *   all families are added.
 *
 * The weights of one pixel add up to at most 1 (the two hue families it
 * is in, Neutrals, and Whites or Blacks), so a result stays within 0 to 1.
 * Photoshop works on the document's display values, so the operation
 * works on the image's values with its own curve (R'G'B' in the image's
 * space). Values outside 0 to 1 are taken as 0 or 1 for the weights and
 * the limits, and keep what lies beyond; NaN is taken as 0.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (adj_selective_color_colors)
  enum_value (ADJ_SC_REDS,     "reds",     N_("Reds"))
  enum_value (ADJ_SC_YELLOWS,  "yellows",  N_("Yellows"))
  enum_value (ADJ_SC_GREENS,   "greens",   N_("Greens"))
  enum_value (ADJ_SC_CYANS,    "cyans",    N_("Cyans"))
  enum_value (ADJ_SC_BLUES,    "blues",    N_("Blues"))
  enum_value (ADJ_SC_MAGENTAS, "magentas", N_("Magentas"))
  enum_value (ADJ_SC_WHITES,   "whites",   N_("Whites"))
  enum_value (ADJ_SC_NEUTRALS, "neutrals", N_("Neutrals"))
  enum_value (ADJ_SC_BLACKS,   "blacks",   N_("Blacks"))
enum_end (AdjSelectiveColorColors)

enum_start (adj_selective_color_method)
  enum_value (ADJ_SC_RELATIVE, "relative", N_("Relative"))
  enum_value (ADJ_SC_ABSOLUTE, "absolute", N_("Absolute"))
enum_end (AdjSelectiveColorMethod)

property_enum (colors, _("Colors"),
               AdjSelectiveColorColors, adj_selective_color_colors,
               ADJ_SC_REDS)
  description (_("The family of colors whose sliders are shown. It only "
                 "chooses what the dialog shows; every family's settings "
                 "apply."))

/* four sliders per family, shown when the family is chosen above */
#define ADJ_SC_INK(family, ink, label, blurb)                           \
  property_double (family##_##ink, label, 0.0)                          \
    description (blurb)                                                 \
    value_range (-100.0, 100.0)                                         \
    ui_digits (0)                                                       \
    ui_meta ("unit", "percent")                                         \
    ui_meta ("visible", "colors {" #family "}")

#define ADJ_SC_FAMILY(family)                                           \
  ADJ_SC_INK (family, cyan, _("Cyan"),                                  \
              _("Cyan to add (positive) or remove (negative) in these " \
                "colors; acts on red"))                                 \
  ADJ_SC_INK (family, magenta, _("Magenta"),                            \
              _("Magenta to add (positive) or remove (negative) in "    \
                "these colors; acts on green"))                         \
  ADJ_SC_INK (family, yellow, _("Yellow"),                              \
              _("Yellow to add (positive) or remove (negative) in "     \
                "these colors; acts on blue"))                          \
  ADJ_SC_INK (family, black, _("Black"),                                \
              _("Black to add (positive, darker) or remove (negative, " \
                "lighter) in these colors"))

ADJ_SC_FAMILY (reds)
ADJ_SC_FAMILY (yellows)
ADJ_SC_FAMILY (greens)
ADJ_SC_FAMILY (cyans)
ADJ_SC_FAMILY (blues)
ADJ_SC_FAMILY (magentas)
ADJ_SC_FAMILY (whites)
ADJ_SC_FAMILY (neutrals)
ADJ_SC_FAMILY (blacks)

#undef ADJ_SC_FAMILY
#undef ADJ_SC_INK

property_enum (method, _("Method"),
               AdjSelectiveColorMethod, adj_selective_color_method,
               ADJ_SC_RELATIVE)
  description (_("Relative changes an ink in proportion to how much of it "
                 "a color has (the ink is 1 minus its channel: pure red has "
                 "no cyan, so adding cyan leaves it as it is). Absolute "
                 "adds the same amount to every color of the family. "
                 "Photoshop's default is Relative."))

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     selective_color
#define GEGL_OP_C_SOURCE selective-color.c

#include "gegl-op.h"

#include <math.h>

#define N_FAMILIES 9

/* the settings as fractions, in the order of AdjSelectiveColorColors, and
 * which families have any */
typedef struct
{
  gint   n;                        /* families with a setting */
  gint   family[N_FAMILIES];
  gfloat ink[N_FAMILIES][4];       /* cyan, magenta, yellow, black */
} Settings;

static void
settings_get (GeglProperties *o,
              Settings       *s)
{
  const gdouble v[N_FAMILIES][4] =
    {
      { o->reds_cyan,     o->reds_magenta,     o->reds_yellow,     o->reds_black     },
      { o->yellows_cyan,  o->yellows_magenta,  o->yellows_yellow,  o->yellows_black  },
      { o->greens_cyan,   o->greens_magenta,   o->greens_yellow,   o->greens_black   },
      { o->cyans_cyan,    o->cyans_magenta,    o->cyans_yellow,    o->cyans_black    },
      { o->blues_cyan,    o->blues_magenta,    o->blues_yellow,    o->blues_black    },
      { o->magentas_cyan, o->magentas_magenta, o->magentas_yellow, o->magentas_black },
      { o->whites_cyan,   o->whites_magenta,   o->whites_yellow,   o->whites_black   },
      { o->neutrals_cyan, o->neutrals_magenta, o->neutrals_yellow, o->neutrals_black },
      { o->blacks_cyan,   o->blacks_magenta,   o->blacks_yellow,   o->blacks_black   },
    };
  gint f, c;

  s->n = 0;
  for (f = 0; f < N_FAMILIES; f++)
    {
      if (v[f][0] == 0.0 && v[f][1] == 0.0 && v[f][2] == 0.0 && v[f][3] == 0.0)
        continue;
      s->family[s->n] = f;
      for (c = 0; c < 4; c++)
        s->ink[s->n][c] = CLAMP (v[f][c], -100.0, 100.0) / 100.0f;
      s->n++;
    }
}

static void
prepare (GeglOperation *operation)
{
  const Babl *space = gegl_operation_get_source_space (operation, "input");
  const Babl *format = babl_format_with_space ("R'G'B'A float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

/* NaN as 0, the rest as it is */
static inline gfloat
finite_or_zero (gfloat v)
{
  return isnan (v) ? 0.0f : v;
}

static inline gfloat
clamp01 (gfloat v)
{
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static gboolean
process (GeglOperation       *operation,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o        = GEGL_PROPERTIES (operation);
  const gfloat   *in       = in_buf;
  gfloat         *out      = out_buf;
  const gboolean  relative = o->method == ADJ_SC_RELATIVE;
  Settings        s;
  glong           i;

  (void) roi;
  (void) level;

  settings_get (o, &s);

  for (i = 0; i < n_pixels; i++)
    {
      gfloat v[3], c[3], d[3] = { 0.0f, 0.0f, 0.0f };
      gfloat max, min, mid, w[N_FAMILIES];
      gint   j, k;

      for (k = 0; k < 3; k++)
        {
          v[k] = finite_or_zero (in[k]);
          c[k] = clamp01 (v[k]);
        }

      max = MAX (c[0], MAX (c[1], c[2]));
      min = MIN (c[0], MIN (c[1], c[2]));
      mid = c[0] + c[1] + c[2] - max - min;

      /* a hue family has the pixel when its channel is the largest (red,
       * green, blue) or the smallest (cyan: red, magenta: green, yellow:
       * blue); with ties the weight is 0 anyway */
      w[ADJ_SC_REDS]     = c[0] == max ? max - mid : 0.0f;
      w[ADJ_SC_GREENS]   = c[1] == max ? max - mid : 0.0f;
      w[ADJ_SC_BLUES]    = c[2] == max ? max - mid : 0.0f;
      w[ADJ_SC_CYANS]    = c[0] == min ? mid - min : 0.0f;
      w[ADJ_SC_MAGENTAS] = c[1] == min ? mid - min : 0.0f;
      w[ADJ_SC_YELLOWS]  = c[2] == min ? mid - min : 0.0f;
      w[ADJ_SC_WHITES]   = min > 0.5f ? 2.0f * min - 1.0f : 0.0f;
      w[ADJ_SC_BLACKS]   = max < 0.5f ? 1.0f - 2.0f * max : 0.0f;
      w[ADJ_SC_NEUTRALS] = MAX (0.0f, 1.0f - fabsf (max - 0.5f) - fabsf (min - 0.5f));

      for (j = 0; j < s.n; j++)
        {
          const gfloat  wj  = w[s.family[j]];
          const gfloat *ink = s.ink[j];

          if (wj <= 0.0f)
            continue;

          for (k = 0; k < 3; k++)
            {
              gfloat a   = ink[k];
              gfloat amt = (-1.0f - a) * ink[3] - a;

              if (relative)
                amt *= 1.0f - c[k];
              amt = amt < -c[k] ? -c[k] : (amt > 1.0f - c[k] ? 1.0f - c[k] : amt);
              d[k] += wj * amt;
            }
        }

      out[0] = v[0] + d[0];
      out[1] = v[1] + d[1];
      out[2] = v[2] + d[2];
      out[3] = in[3];

      in  += 4;
      out += 4;
    }

  return TRUE;
}

/* with all sliders at 0 the input goes through as it is */
static gboolean
operation_process (GeglOperation        *operation,
                   GeglOperationContext *context,
                   const gchar          *output_prop,
                   const GeglRectangle  *result,
                   gint                  level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  Settings        s;

  (void) level;
  settings_get (o, &s);
  if (s.n == 0)
    {
      gpointer in = gegl_operation_context_get_object (context, "input");

      if (in)
        gegl_operation_context_take_object (context, "output",
                                            g_object_ref (G_OBJECT (in)));
      return TRUE;
    }

  return GEGL_OPERATION_CLASS (gegl_op_parent_class)->process (
           operation, context, output_prop, result,
           gegl_operation_context_get_level (context));
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationPointFilterClass *point_class     =
    GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  operation_class->prepare        = prepare;
  operation_class->process        = operation_process;
  operation_class->opencl_support = FALSE;
  point_class->process            = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "adj:selective-color",
    "title",           _("Selective Color"),
    "categories",      "color",
    "description",     _("Adds or removes cyan, magenta, yellow and black in "
                         "reds, yellows, greens, cyans, blues, magentas, "
                         "whites, neutrals and blacks, as Photoshop's "
                         "Selective Color adjustment does"),
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Selective Color..."),
    NULL);
}

#endif
