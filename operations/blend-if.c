/*
 * Blend If: the "This Layer" sliders of Photoshop's Blending Options,
 * a GEGL operation
 *
 * blend-if.c
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
 * Makes pixels transparent by their own gray, red, green or blue value,
 * as the "This Layer" sliders of Photoshop's Blend If do. Each of the
 * four has a black and a white slider on a scale of 0 to 255, and each
 * slider can be split in two halves for a soft transition, so there are
 * four values per channel, in the order of the PSD file's layer blending
 * ranges: black low, black high, white low, white high.
 *
 *   value below black low          hidden
 *   black low to black high        fading in
 *   black high to white low        shown
 *   white low to white high        fading out
 *   value above white high         hidden
 *
 * An unsplit slider hides the values beyond it and keeps its own value,
 * as Adobe's help describes it ("if you drag the white slider to 235,
 * pixels with brightness values higher than 235 ... will be excluded").
 * The fade is linear in the value by default; "Smooth" uses a smoothstep
 * curve instead. The four channels' factors multiply the pixel's alpha.
 * The colors are left as they are.
 *
 * Gray is 0.3 R + 0.59 G + 0.11 B, the luminosity Photoshop documents
 * for its histogram, on the image's values with its own curve (R'G'B' in
 * the image's space), as Photoshop works on display values. This is not
 * verified against Photoshop, nor is its exact rounding at 8 bits.
 *
 * The "Underlying Layer" sliders need the layers below, which a filter
 * does not see (GIMP makes filters with a second input destructive);
 * they are not part of this operation.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (adj_blend_if_channel)
  enum_value (ADJ_BLEND_IF_GRAY,  "gray",  N_("Gray"))
  enum_value (ADJ_BLEND_IF_RED,   "red",   N_("Red"))
  enum_value (ADJ_BLEND_IF_GREEN, "green", N_("Green"))
  enum_value (ADJ_BLEND_IF_BLUE,  "blue",  N_("Blue"))
enum_end (AdjBlendIfChannel)

enum_start (adj_blend_if_fade)
  enum_value (ADJ_BLEND_IF_LINEAR, "linear", N_("Linear (as Photoshop)"))
  enum_value (ADJ_BLEND_IF_SMOOTH, "smooth", N_("Smooth"))
enum_end (AdjBlendIfFade)

property_enum (blend_if, _("Blend If"),
               AdjBlendIfChannel, adj_blend_if_channel, ADJ_BLEND_IF_GRAY)
  description (_("The channel whose sliders are shown. It only chooses "
                 "what the dialog shows; every channel's sliders apply."))

#define ADJ_BLEND_IF_SLIDER(ch, name, label, def, blurb)                \
  property_double (ch##_##name, label, def)                             \
    description (blurb)                                                 \
    value_range (0.0, 255.0)                                            \
    ui_digits (0)                                                       \
    ui_meta ("visible", "blend-if {" #ch "}")

#define ADJ_BLEND_IF_CHANNEL(ch)                                        \
  ADJ_BLEND_IF_SLIDER (ch, black_low, _("Black: hidden below"), 0.0,    \
    _("Pixels whose value is below this are hidden (the left half of "  \
      "the black slider)"))                                             \
  ADJ_BLEND_IF_SLIDER (ch, black_high, _("Black: fully shown from"), 0.0, \
    _("Pixels whose value is at least this are fully shown; between "   \
      "the two black values they fade in (the right half of the black " \
      "slider)"))                                                       \
  ADJ_BLEND_IF_SLIDER (ch, white_low, _("White: fully shown up to"), 255.0, \
    _("Pixels whose value is at most this are fully shown; between "    \
      "the two white values they fade out (the left half of the white " \
      "slider)"))                                                       \
  ADJ_BLEND_IF_SLIDER (ch, white_high, _("White: hidden above"), 255.0, \
    _("Pixels whose value is above this are hidden (the right half of " \
      "the white slider)"))

ADJ_BLEND_IF_CHANNEL (gray)
ADJ_BLEND_IF_CHANNEL (red)
ADJ_BLEND_IF_CHANNEL (green)
ADJ_BLEND_IF_CHANNEL (blue)

#undef ADJ_BLEND_IF_CHANNEL
#undef ADJ_BLEND_IF_SLIDER

property_enum (fade, _("Fade"),
               AdjBlendIfFade, adj_blend_if_fade, ADJ_BLEND_IF_LINEAR)
  description (_("How pixels fade between the two halves of a split "
                 "slider: linearly in their value, or along a smooth "
                 "curve"))

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     blend_if
#define GEGL_OP_C_SOURCE blend-if.c

#include "gegl-op.h"

#include <math.h>

/* one channel's ramps, in values of 0 to 1 */
typedef struct
{
  gboolean used;          /* the sliders hide anything */
  gboolean black_cut;     /* the black slider is above 0 */
  gboolean white_cut;     /* the white slider is below 255 */
  gfloat   b0, b1, w0, w1;
} Range;

static void
range_set (Range   *r,
           gdouble  black_low,
           gdouble  black_high,
           gdouble  white_low,
           gdouble  white_high)
{
  gdouble b0 = CLAMP (MIN (black_low, black_high), 0.0, 255.0);
  gdouble b1 = CLAMP (MAX (black_low, black_high), 0.0, 255.0);
  gdouble w0 = CLAMP (MIN (white_low, white_high), 0.0, 255.0);
  gdouble w1 = CLAMP (MAX (white_low, white_high), 0.0, 255.0);

  r->b0 = b0 / 255.0;
  r->b1 = b1 / 255.0;
  r->w0 = w0 / 255.0;
  r->w1 = w1 / 255.0;
  /* the sliders at their ends hide nothing, not even values beyond 0
   * and 1 in float images */
  r->black_cut = b1 > 0.0;
  r->white_cut = w0 < 255.0;
  r->used      = r->black_cut || r->white_cut;
}

typedef struct
{
  Range    ch[4];         /* gray, red, green, blue */
  gboolean smooth;
} Settings;

static void
settings_get (GeglProperties *o,
              Settings       *s)
{
  range_set (&s->ch[0], o->gray_black_low, o->gray_black_high,
             o->gray_white_low, o->gray_white_high);
  range_set (&s->ch[1], o->red_black_low, o->red_black_high,
             o->red_white_low, o->red_white_high);
  range_set (&s->ch[2], o->green_black_low, o->green_black_high,
             o->green_white_low, o->green_white_high);
  range_set (&s->ch[3], o->blue_black_low, o->blue_black_high,
             o->blue_white_low, o->blue_white_high);
  s->smooth = o->fade == ADJ_BLEND_IF_SMOOTH;
}

static gboolean
settings_used (const Settings *s)
{
  return s->ch[0].used || s->ch[1].used || s->ch[2].used || s->ch[3].used;
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
fade (gfloat   t,
      gboolean smooth)
{
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return smooth ? t * t * (3.0f - 2.0f * t) : t;
}

/* how much of a pixel with value v one channel's sliders keep */
static inline gfloat
range_factor (const Range *r,
              gfloat       v,
              gboolean     smooth)
{
  gfloat f = 1.0f;

  if (r->black_cut)
    {
      if (r->b1 > r->b0)
        f *= fade ((v - r->b0) / (r->b1 - r->b0), smooth);
      else if (v < r->b0)
        f = 0.0f;
    }
  if (r->white_cut)
    {
      if (r->w1 > r->w0)
        f *= fade ((r->w1 - v) / (r->w1 - r->w0), smooth);
      else if (v > r->w1)
        f = 0.0f;
    }
  return f;
}

static gboolean
process (GeglOperation       *operation,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o   = GEGL_PROPERTIES (operation);
  const gfloat   *in  = in_buf;
  gfloat         *out = out_buf;
  Settings        s;
  glong           i;

  (void) roi;
  (void) level;

  settings_get (o, &s);

  for (i = 0; i < n_pixels; i++)
    {
      /* NaN counts as 0 */
      const gfloat r = isnan (in[0]) ? 0.0f : in[0];
      const gfloat g = isnan (in[1]) ? 0.0f : in[1];
      const gfloat b = isnan (in[2]) ? 0.0f : in[2];
      const gfloat a = isnan (in[3]) ? 0.0f : in[3];
      gfloat       f = 1.0f;

      if (s.ch[0].used)
        f *= range_factor (&s.ch[0], 0.3f * r + 0.59f * g + 0.11f * b, s.smooth);
      if (s.ch[1].used)
        f *= range_factor (&s.ch[1], r, s.smooth);
      if (s.ch[2].used)
        f *= range_factor (&s.ch[2], g, s.smooth);
      if (s.ch[3].used)
        f *= range_factor (&s.ch[3], b, s.smooth);

      out[0] = in[0];
      out[1] = in[1];
      out[2] = in[2];
      out[3] = a * f;

      in  += 4;
      out += 4;
    }

  return TRUE;
}

/* with the sliders at their ends the input goes through as it is */
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
  if (! settings_used (&s))
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
    "name",            "adj:blend-if",
    "title",           _("Blend If"),
    "categories",      "color",
    "description",     _("Makes pixels transparent by their own gray, red, "
                         "green or blue value, with split sliders for soft "
                         "transitions, as the \"This Layer\" sliders of "
                         "Photoshop's Blend If do"),
    /* GIMP adds an alpha channel to a layer without one */
    "needs-alpha",     "true",
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Blend If..."),
    NULL);
}

#endif
