/*
 * Applies one of the operations to raw float pixels, for
 * tests/crosscheck.sh
 *
 * adj-apply.c
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
 *   adj-apply <build folder> <in.raw> <out.raw> <width> <height>
 *             <operation> [property=value ...]
 *
 * The raw files hold R'G'B'A float pixels (sRGB, 16 bytes each, row by
 * row, in the machine's byte order). Properties are given as on the gegl
 * command line, e.g. reds-cyan=-40 method=absolute.
 */

#include <gegl.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "modules.h"

#define FMT "R'G'B'A float"

int
main (int    argc,
      char **argv)
{
  gchar      *pixels = NULL, *dir;
  gsize       length;
  gint        w, h, i;
  GeglBuffer *in;
  GeglNode   *graph, *src, *op;
  gfloat     *out;

  if (argc < 7)
    {
      fprintf (stderr, "usage: %s <build folder> <in.raw> <out.raw> "
                       "<width> <height> <operation> [property=value ...]\n",
               argv[0]);
      return 2;
    }
  w = atoi (argv[4]);
  h = atoi (argv[5]);
  if (w <= 0 || h <= 0 ||
      ! g_file_get_contents (argv[2], &pixels, &length, NULL) ||
      length != (gsize) w * h * 4 * sizeof (gfloat))
    {
      fprintf (stderr, "%s: cannot read %d x %d pixels\n", argv[2], w, h);
      return 2;
    }

  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (NULL, NULL);

  in = gegl_buffer_new (GEGL_RECTANGLE (0, 0, w, h), babl_format (FMT));
  gegl_buffer_set (in, NULL, 0, babl_format (FMT), pixels, GEGL_AUTO_ROWSTRIDE);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                               "buffer", in, NULL);
  op    = gegl_node_new_child (graph, "operation", argv[6], NULL);
  for (i = 7; i < argc; i++)
    if (! node_set_from_string (op, argv[i]))
      {
        fprintf (stderr, "cannot set %s\n", argv[i]);
        return 2;
      }
  gegl_node_link (src, op);
  out = g_new (gfloat, (gsize) w * h * 4);
  gegl_node_blit (op, 1.0, GEGL_RECTANGLE (0, 0, w, h), babl_format (FMT), out,
                  GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_file_set_contents (argv[3], (const gchar *) out,
                       (gssize) w * h * 4 * sizeof (gfloat), NULL);

  g_object_unref (graph);
  g_object_unref (in);
  g_free (out);
  g_free (pixels);
  gegl_exit ();
  modules_cleanup (dir);

  return 0;
}
