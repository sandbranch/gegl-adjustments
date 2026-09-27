/*
 * How long the operations take on a 24 megapixel image
 *
 * bench.c
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
 *   bench <build folder> [width height]
 *
 * Times each operation on a 6000 x 4000 image, in float and in 8 bits
 * (as GIMP's 8 bit images), with GEGL's threads and with one thread; the
 * best of three runs. Two point filters of GEGL, gegl:invert-gamma and
 * gegl:levels, are timed the same way, for scale.
 */

#include <gegl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "modules.h"

static gint W = 6000, H = 4000;

/* a photo-like image: smooth ramps with fine detail */
static GeglBuffer *
make_image (const gchar *format)
{
  GeglBuffer *b   = gegl_buffer_new (GEGL_RECTANGLE (0, 0, W, H),
                                     babl_format (format));
  gfloat     *row = g_new (gfloat, W * 4);
  gint        x, y;

  for (y = 0; y < H; y++)
    {
      for (x = 0; x < W; x++)
        {
          row[x * 4 + 0] = (gfloat) x / W;
          row[x * 4 + 1] = (gfloat) y / H;
          row[x * 4 + 2] = 0.5f + 0.5f * sinf (x * 0.013f + y * 0.007f);
          row[x * 4 + 3] = 1.0f;
        }
      gegl_buffer_set (b, GEGL_RECTANGLE (0, y, W, 1), 0,
                       babl_format ("R'G'B'A float"), row, GEGL_AUTO_ROWSTRIDE);
    }
  g_free (row);
  return b;
}

/* seconds for the operation over the whole image, the best of three;
 * props is a space separated list of name=value */
static gdouble
time_op (GeglBuffer  *in,
         const gchar *op,
         const gchar *props)
{
  GeglBuffer *out  = gegl_buffer_new (GEGL_RECTANGLE (0, 0, W, H),
                                      gegl_buffer_get_format (in));
  gchar     **p    = g_strsplit (props ? props : "", " ", -1);
  gdouble     best = G_MAXDOUBLE;
  gint        run, i;

  for (run = 0; run < 4; run++)
    {
      GeglNode *g    = gegl_node_new ();
      GeglNode *src  = gegl_node_new_child (g, "operation", "gegl:buffer-source",
                                            "buffer", in, NULL);
      GeglNode *node = gegl_node_new_child (g, "operation", op, NULL);
      GeglNode *sink = gegl_node_new_child (g, "operation", "gegl:write-buffer",
                                            "buffer", out, NULL);
      gint64    t0;

      for (i = 0; p[i]; i++)
        if (p[i][0] && ! node_set_from_string (node, p[i]))
          fprintf (stderr, "cannot set %s\n", p[i]);
      gegl_node_link_many (src, node, sink, NULL);
      t0 = g_get_monotonic_time ();
      gegl_node_process (sink);
      /* the first run allocates the output's tiles */
      if (run > 0)
        best = MIN (best, (g_get_monotonic_time () - t0) / 1e6);
      g_object_unref (g);
    }
  g_strfreev (p);
  g_object_unref (out);
  return best;
}

int
main (int    argc,
      char **argv)
{
  static const gchar *const rows[][3] =
    {
      { "gegl:invert-gamma", "", "gegl:invert-gamma" },
      { "gegl:levels", "out-high=0.9", "gegl:levels (linear light)" },
      { "adj:selective-color", "reds-cyan=-30 reds-black=10",
        "Selective Color, 1 family" },
      { "adj:selective-color",
        "reds-cyan=-30 yellows-magenta=20 greens-yellow=15 cyans-cyan=10 "
        "blues-black=-20 magentas-yellow=5 whites-black=-10 neutrals-cyan=5 "
        "blacks-black=10", "Selective Color, 9 families" },
      { "adj:black-and-white", "", "Black & White" },
      { "adj:black-and-white", "tint=true", "Black & White, tint" },
      { "adj:blend-if", "gray-black-low=20 gray-black-high=80 red-white-low=200 "
        "red-white-high=250", "Blend If, 2 channels" },
      { "adj:luminosity-mask", "mask=lights level=3", "Luminosity Mask, Lights 3" },
      { "adj:luminosity-mask", "mask=midtones level=2 luminosity=lightness",
        "Luminosity Mask, Midtones, L*" },
      { "adj:luminosity-mask", "mask=hue", "Luminosity Mask, hue" },
      { "adj:selective-color", "", "Selective Color at 0 (passes through)" },
    };
  gchar      *dir;
  GeglBuffer *img_f, *img_8;
  gint        threads, t;
  guint       r;

  if (argc != 2 && argc != 4)
    {
      fprintf (stderr, "usage: %s <build folder> [width height]\n", argv[0]);
      return 2;
    }
  if (argc == 4)
    {
      W = atoi (argv[2]);
      H = atoi (argv[3]);
    }

  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (NULL, NULL);
  /* room for the images in memory, as GIMP gives GEGL; the default is
   * smaller, and swapping to disk would be what is timed */
  g_object_set (gegl_config (), "tile-cache-size", (guint64) 8 << 30, NULL);
  g_object_get (gegl_config (), "threads", &threads, NULL);

  img_f = make_image ("R'G'B'A float");
  img_8 = make_image ("R'G'B'A u8");

  printf ("%d x %d pixels (%.1f megapixels), GEGL threads: %d\n\n",
          W, H, W * H / 1e6, threads);
  for (t = 0; t < 2; t++)
    {
      gint n = t == 0 ? threads : 1;

      g_object_set (gegl_config (), "threads", n, NULL);
      printf ("%d thread%s                              float     8 bit\n",
              n, n > 1 ? "s" : " ");
      for (r = 0; r < G_N_ELEMENTS (rows); r++)
        {
          gdouble tf = time_op (img_f, rows[r][0], rows[r][1]);
          gdouble t8 = time_op (img_8, rows[r][0], rows[r][1]);

          printf ("  %-37s %6.3f s  %6.3f s\n", rows[r][2], tf, t8);
        }
      printf ("\n");
    }

  g_object_unref (img_f);
  g_object_unref (img_8);
  gegl_exit ();
  modules_cleanup (dir);
  return 0;
}
