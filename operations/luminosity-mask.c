/*
 * Luminosity Mask: lights, darks, midtones, tonal range, saturation and
 * hue masks, a GEGL operation
 *
 * luminosity-mask.c
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
 * Makes a gray mask from the image's tones, as the luminosity masks of
 * Tony Kuyper's tutorials and panels (TK) do, or sets the image's alpha
 * from it. With L the luminosity of a pixel (0 to 1):
 *
 *   Lights 1   L                     ("Lights": the Gray channel)
 *   Darks 1    1 - L                 (the Lights mask inverted)
 *   Lights n   Lights (n-1) x Lights (n-1)
 *   Darks n    Darks (n-1) x Darks (n-1)
 *   Midtones n (1 - Lights n) x (1 - Darks n)
 *
 * That is Kuyper's recipe: each mask of a series intersects the one
 * before with itself (his original "Luminosity Masks" tutorial, 2006:
 * "the 'Bright Lights' mask comes from intersecting the 'Light Lights'
 * mask with itself"), which in 16 bits is Photoshop's Calculations with
 * Multiply, and the midtones are a Lights and a Darks mask, both
 * inverted, multiplied ("How to Make 16-bit Luminosity Masks", Good
 * Light Journal, 2015). So Lights n is L to the power 2^(n-1): L, L^2,
 * L^4, L^8, L^16. "Powers" gives the other series in use, L^n (each mask
 * intersected with Lights 1 once more), as for example Todd Marsh
 * describes it; the midtones are then (1 - L^n) x (1 - (1 - L)^n).
 *
 * A tonal range selects luminosities from a low to a high value (on
 * Photoshop's scale of 0 to 255, as zone masks are given) with a feather
 * on both sides; saturation and hue ranges the same with the HSV
 * saturation (0 to 100 %) and hue (0 to 360 degrees). A hue mask is
 * weighted by the saturation, so that grays, whose hue means nothing,
 * are not selected.
 *
 * The luminosity is Photoshop's Gray (0.3 R + 0.59 G + 0.11 B of the
 * display values), the luminance of the image's space with its curve, or
 * CIE L*, all from the image's values with its own curve (R'G'B' in the
 * image's space). On a grayscale drawable without alpha, such as a
 * channel in GIMP, the values themselves are the luminosity (a channel
 * holds the mask values as they are) and the result is gray as well.
 * NaN counts as 0; the mask is always within 0 to 1.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (adj_luminosity_mask_kind)
  enum_value (ADJ_LM_LIGHTS,     "lights",     N_("Lights"))
  enum_value (ADJ_LM_DARKS,      "darks",      N_("Darks"))
  enum_value (ADJ_LM_MIDTONES,   "midtones",   N_("Midtones"))
  enum_value (ADJ_LM_TONES,      "tonal-range", N_("Tonal range (zone)"))
  enum_value (ADJ_LM_SATURATION, "saturation", N_("Saturation range"))
  enum_value (ADJ_LM_HUE,        "hue",        N_("Hue range"))
enum_end (AdjLuminosityMaskKind)

enum_start (adj_luminosity_mask_series)
  enum_value (ADJ_LM_SQUARES, "tk",     N_("TK (each squares the one before)"))
  enum_value (ADJ_LM_POWERS,  "powers", N_("Powers (L to the power n)"))
enum_end (AdjLuminosityMaskSeries)

enum_start (adj_luminosity_mask_luminosity)
  enum_value (ADJ_LM_GRAY,      "gray",      N_("Photoshop Gray (0.30 R + 0.59 G + 0.11 B)"))
  enum_value (ADJ_LM_LUMINANCE, "luminance", N_("Luminance of the image's color space"))
  enum_value (ADJ_LM_LSTAR,     "lightness", N_("Lightness (CIE L*)"))
enum_end (AdjLuminosityMaskLuminosity)

enum_start (adj_luminosity_mask_fade)
  enum_value (ADJ_LM_SMOOTH, "smooth", N_("Smooth"))
  enum_value (ADJ_LM_LINEAR, "linear", N_("Linear"))
enum_end (AdjLuminosityMaskFade)

enum_start (adj_luminosity_mask_output)
  enum_value (ADJ_LM_OUT_MASK,  "mask",  N_("Gray mask"))
  enum_value (ADJ_LM_OUT_ALPHA, "alpha", N_("Transparency of the image"))
enum_end (AdjLuminosityMaskOutput)

property_enum (mask, _("Mask"),
               AdjLuminosityMaskKind, adj_luminosity_mask_kind, ADJ_LM_LIGHTS)
  description (_("Lights select the light tones, Darks the dark ones, "
                 "Midtones those between; a range selects tones, "
                 "saturations or hues between two values"))

property_int (level, _("Level"), 1)
  description (_("1 is the widest mask; each level narrows it (Lights 2 "
                 "is Lights 1 intersected with itself)"))
  value_range (1, 5)
  ui_meta ("visible", "mask {lights, darks, midtones}")

property_enum (series, _("Series"),
               AdjLuminosityMaskSeries, adj_luminosity_mask_series,
               ADJ_LM_SQUARES)
  description (_("TK: each level intersects the one before with itself, "
                 "as in Tony Kuyper's recipe (L, L^2, L^4, L^8, L^16). "
                 "Powers: each level intersects once more with Lights 1 "
                 "(L, L^2, L^3, L^4, L^5)."))
  ui_meta ("visible", "mask {lights, darks, midtones}")

property_double (tone_low, _("From"), 96.0)
  description (_("The darkest tone fully selected, 0 (black) to 255 "
                 "(white)"))
  value_range (0.0, 255.0)
  ui_digits (0)
  ui_meta ("visible", "mask {tonal-range}")

property_double (tone_high, _("To"), 160.0)
  description (_("The lightest tone fully selected, 0 (black) to 255 "
                 "(white)"))
  value_range (0.0, 255.0)
  ui_digits (0)
  ui_meta ("visible", "mask {tonal-range}")

property_double (tone_feather, _("Feather"), 32.0)
  description (_("How far beyond the range the selection fades out, on "
                 "the scale of 0 to 255"))
  value_range (0.0, 255.0)
  ui_digits (0)
  ui_meta ("visible", "mask {tonal-range}")

property_double (saturation_low, _("From"), 50.0)
  description (_("The lowest saturation fully selected (HSV saturation)"))
  value_range (0.0, 100.0)
  ui_digits (0)
  ui_meta ("unit", "percent")
  ui_meta ("visible", "mask {saturation}")

property_double (saturation_high, _("To"), 100.0)
  description (_("The highest saturation fully selected"))
  value_range (0.0, 100.0)
  ui_digits (0)
  ui_meta ("unit", "percent")
  ui_meta ("visible", "mask {saturation}")

property_double (saturation_feather, _("Feather"), 20.0)
  description (_("How far beyond the range the selection fades out"))
  value_range (0.0, 100.0)
  ui_digits (0)
  ui_meta ("unit", "percent")
  ui_meta ("visible", "mask {saturation}")

property_double (hue_center, _("Hue"), 0.0)
  description (_("The middle of the hues selected: 0 red, 60 yellow, 120 "
                 "green, 180 cyan, 240 blue, 300 magenta"))
  value_range (0.0, 360.0)
  ui_digits (0)
  ui_meta ("unit", "degree")
  ui_meta ("visible", "mask {hue}")

property_double (hue_width, _("Width"), 30.0)
  description (_("How many degrees of hue around it are fully selected"))
  value_range (0.0, 360.0)
  ui_digits (0)
  ui_meta ("unit", "degree")
  ui_meta ("visible", "mask {hue}")

property_double (hue_feather, _("Feather"), 30.0)
  description (_("How many degrees beyond that the selection fades out"))
  value_range (0.0, 180.0)
  ui_digits (0)
  ui_meta ("unit", "degree")
  ui_meta ("visible", "mask {hue}")

property_enum (fade, _("Fade"),
               AdjLuminosityMaskFade, adj_luminosity_mask_fade, ADJ_LM_SMOOTH)
  description (_("The shape of the feather of a range: a smooth curve or a "
                 "straight line"))
  ui_meta ("visible", "mask {tonal-range, saturation, hue}")

property_enum (luminosity, _("Luminosity"),
               AdjLuminosityMaskLuminosity, adj_luminosity_mask_luminosity,
               ADJ_LM_GRAY)
  description (_("The gray the tones are measured with. Photoshop Gray is "
                 "what luminosity mask panels in Photoshop start from. On "
                 "a channel the channel's own values are used."))
  ui_meta ("visible", "mask {lights, darks, midtones, tonal-range}")

property_boolean (invert, _("Invert"), FALSE)
  description (_("Selects the opposite: the mask becomes 1 minus the mask"))

property_enum (output, _("Output"),
               AdjLuminosityMaskOutput, adj_luminosity_mask_output,
               ADJ_LM_OUT_MASK)
  description (_("A gray image of the mask (white is selected), or the "
                 "image with the mask as its transparency. On a channel the "
                 "mask always replaces the channel's values."))

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     luminosity_mask
#define GEGL_OP_C_SOURCE luminosity-mask.c

#include "gegl-op.h"

#include <math.h>
#include <string.h>

/* the conversion to the luminosity, and whether the input is a gray
 * drawable without alpha whose values are taken as they are */
typedef struct
{
  gboolean    gray_raw;
  const Babl *fish;       /* NULL, or from the working format to the gray */
  gfloat      fish_scale; /* 1, or 1/100 for CIE L */
} State;

static void
prepare (GeglOperation *operation)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *space  = gegl_operation_get_source_space (operation, "input");
  const Babl     *source = gegl_operation_get_source_format (operation, "input");
  const Babl     *format;
  State          *state;

  if (! o->user_data)
    o->user_data = g_new0 (State, 1);
  state = o->user_data;

  state->gray_raw = FALSE;
  if (source && babl_format_get_n_components (source) == 1)
    {
      const Babl *model = babl_format_get_model (source);
      const char *name  = model ? babl_get_name (model) : "";

      /* the values of a channel as they are: keep its curve */
      if (strcmp (name, "Y") == 0 || strcmp (name, "Y'") == 0 ||
          strcmp (name, "Y~") == 0)
        {
          gchar *f = g_strdup_printf ("%s float", name);

          state->gray_raw = TRUE;
          format = babl_format_with_space (f, space);
          g_free (f);
          gegl_operation_set_format (operation, "input", format);
          gegl_operation_set_format (operation, "output", format);
          state->fish = NULL;
          return;
        }
    }

  format = babl_format_with_space ("R'G'B'A float", space);
  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);

  state->fish       = NULL;
  state->fish_scale = 1.0f;
  if (o->luminosity == ADJ_LM_LUMINANCE)
    {
      state->fish = babl_fish (format, babl_format_with_space ("Y' float", space));
    }
  else if (o->luminosity == ADJ_LM_LSTAR)
    {
      state->fish = babl_fish (format, babl_format_with_space ("CIE L float", space));
      state->fish_scale = 0.01f;
    }
}

static inline gfloat
clamp01 (gfloat v)
{
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline gfloat
not_nan (gfloat v)
{
  return isnan (v) ? 0.0f : v;
}

/* 1 at distance 0, 0 at distance feather or more */
static inline gfloat
feather (gfloat   distance,
         gfloat   width,
         gboolean smooth)
{
  gfloat t;

  if (distance <= 0.0f)
    return 1.0f;
  if (width <= 0.0f || distance >= width)
    return 0.0f;
  t = 1.0f - distance / width;
  return smooth ? t * t * (3.0f - 2.0f * t) : t;
}

/* 1 from low to high, fading out over the feather on both sides */
static inline gfloat
range (gfloat   v,
       gfloat   low,
       gfloat   high,
       gfloat   width,
       gboolean smooth)
{
  if (v < low)
    return feather (low - v, width, smooth);
  if (v > high)
    return feather (v - high, width, smooth);
  return 1.0f;
}

/* x^(2^(n-1)) or x^n, n of 1 to 5 */
static inline gfloat
series (gfloat   x,
        gint     n,
        gboolean squares)
{
  gfloat y = x;
  gint   i;

  for (i = 1; i < n; i++)
    y = squares ? y * y : y * x;
  return y;
}

typedef struct
{
  gint     kind;
  gint     level;
  gboolean squares;
  gboolean smooth;
  gboolean invert;
  gfloat   low, high, width;   /* of the range, in 0 to 1 */
  gfloat   hue, hue_half, hue_feather;  /* in turns of 0 to 1 */
} Settings;

static void
settings_get (GeglProperties *o,
              Settings       *s)
{
  s->kind    = o->mask;
  s->level   = CLAMP (o->level, 1, 5);
  s->squares = o->series == ADJ_LM_SQUARES;
  s->smooth  = o->fade == ADJ_LM_SMOOTH;
  s->invert  = o->invert;
  if (o->mask == ADJ_LM_SATURATION)
    {
      s->low   = MIN (o->saturation_low, o->saturation_high) / 100.0;
      s->high  = MAX (o->saturation_low, o->saturation_high) / 100.0;
      s->width = o->saturation_feather / 100.0;
    }
  else
    {
      s->low   = MIN (o->tone_low, o->tone_high) / 255.0;
      s->high  = MAX (o->tone_low, o->tone_high) / 255.0;
      s->width = o->tone_feather / 255.0;
    }
  s->hue         = fmod (o->hue_center, 360.0) / 360.0;
  s->hue_half    = CLAMP (o->hue_width, 0.0, 360.0) / 720.0;
  s->hue_feather = CLAMP (o->hue_feather, 0.0, 180.0) / 360.0;
}

/* the mask of one pixel from its luminosity l and, for saturation and
 * hue masks, its channels */
static inline gfloat
mask_value (const Settings *s,
            gfloat          l,
            gfloat          r,
            gfloat          g,
            gfloat          b)
{
  gfloat m;

  l = clamp01 (l);
  switch (s->kind)
    {
    case ADJ_LM_LIGHTS:
      m = series (l, s->level, s->squares);
      break;
    case ADJ_LM_DARKS:
      m = series (1.0f - l, s->level, s->squares);
      break;
    case ADJ_LM_MIDTONES:
      m = (1.0f - series (l, s->level, s->squares)) *
          (1.0f - series (1.0f - l, s->level, s->squares));
      break;
    case ADJ_LM_TONES:
      m = range (l, s->low, s->high, s->width, s->smooth);
      break;
    default:
      {
        gfloat max, min, sat, hue, d;

        r = clamp01 (r);
        g = clamp01 (g);
        b = clamp01 (b);
        max = MAX (r, MAX (g, b));
        min = MIN (r, MIN (g, b));
        sat = max > 0.0f ? (max - min) / max : 0.0f;
        if (s->kind == ADJ_LM_SATURATION)
          {
            m = range (sat, s->low, s->high, s->width, s->smooth);
            break;
          }
        if (max - min <= 0.0f)
          {
            m = 0.0f;
            break;
          }
        /* the hexagonal hue, in turns */
        if (max == r)
          hue = (g - b) / (max - min);
        else if (max == g)
          hue = 2.0f + (b - r) / (max - min);
        else
          hue = 4.0f + (r - g) / (max - min);
        hue /= 6.0f;
        if (hue < 0.0f)
          hue += 1.0f;
        /* the distance around the circle from the middle of the range */
        d = fabsf (hue - s->hue);
        if (d > 0.5f)
          d = 1.0f - d;
        m = feather (d - s->hue_half, s->hue_feather, s->smooth) * sat;
      }
      break;
    }

  m = clamp01 (m);
  return s->invert ? 1.0f - m : m;
}

#define CHUNK 1024

static gboolean
process (GeglOperation       *operation,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o     = GEGL_PROPERTIES (operation);
  const State    *state = o->user_data;
  const gfloat   *in    = in_buf;
  gfloat         *out   = out_buf;
  Settings        s;
  gfloat          lum[CHUNK];
  glong           i, j;

  (void) roi;
  (void) level;

  settings_get (o, &s);

  if (state->gray_raw)
    {
      for (i = 0; i < n_pixels; i++)
        {
          const gfloat v = not_nan (in[i]);

          out[i] = mask_value (&s, v, v, v, v);
        }
      return TRUE;
    }

  for (j = 0; j < n_pixels; j += CHUNK)
    {
      const glong n = MIN (CHUNK, n_pixels - j);

      if (state->fish)
        {
          babl_process (state->fish, in, lum, n);
          for (i = 0; i < n; i++)
            lum[i] = not_nan (lum[i]) * state->fish_scale;
        }
      else
        {
          for (i = 0; i < n; i++)
            lum[i] = 0.3f  * not_nan (in[4 * i]) +
                     0.59f * not_nan (in[4 * i + 1]) +
                     0.11f * not_nan (in[4 * i + 2]);
        }

      for (i = 0; i < n; i++)
        {
          const gfloat *p = in + 4 * i;
          gfloat       *q = out + 4 * i;
          const gfloat  m = mask_value (&s, lum[i], not_nan (p[0]),
                                        not_nan (p[1]), not_nan (p[2]));

          if (o->output == ADJ_LM_OUT_ALPHA)
            {
              q[0] = p[0];
              q[1] = p[1];
              q[2] = p[2];
              q[3] = not_nan (p[3]) * m;
            }
          else
            {
              q[0] = q[1] = q[2] = m;
              q[3] = p[3];
            }
        }

      in  += 4 * n;
      out += 4 * n;
    }

  return TRUE;
}

static void
finalize (GObject *object)
{
  GeglProperties *o = GEGL_PROPERTIES (object);

  g_clear_pointer (&o->user_data, g_free);

  G_OBJECT_CLASS (gegl_op_parent_class)->finalize (object);
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GObjectClass                  *object_class    = G_OBJECT_CLASS (klass);
  GeglOperationClass            *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationPointFilterClass *point_class     =
    GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  object_class->finalize          = finalize;
  operation_class->prepare        = prepare;
  operation_class->opencl_support = FALSE;
  point_class->process            = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "adj:luminosity-mask",
    "title",           _("Luminosity Mask"),
    "categories",      "color",
    "description",     _("Makes a mask of the light, dark or middle tones "
                         "(Lights 1 to 5, Darks 1 to 5, Midtones 1 to 5 as "
                         "in Tony Kuyper's luminosity masks), of a tonal "
                         "range, a saturation range or a hue range; as a "
                         "gray image or as the image's transparency"),
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Luminosity Mask..."),
    NULL);
}

#endif
