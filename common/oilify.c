/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1996 Torsten Martinsen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#define SCALE_WIDTH  125
#define HISTSIZE     256

#define MODE_RGB       0
#define MODE_INTEN     1

#define INTENSITY(p)   ((guint) (p[0]*77+p[1]*150+p[2]*29) >> 8)

typedef struct
{
  gdouble  mask_size;
  gint     mode;
  gboolean preview;
} OilifyVals;


/* Declare local functions.
 */
static void      query  (void);
static void      run    (const gchar      *name,
                         gint              nparams,
                         const GimpParam  *param,
                         gint             *nreturn_vals,
                         GimpParam       **return_vals);

static void      oilify             (GimpDrawable *drawable,
                                     GimpPreview  *preview);
static void      oilify_rgb         (GimpDrawable *drawable,
                                     GimpPreview  *preview);
static void      oilify_intensity   (GimpDrawable *drawable,
                                     GimpPreview  *preview);

static gboolean  oilify_dialog      (GimpDrawable *drawable);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static OilifyVals ovals =
{
  7.0,     /* mask size */
  0,       /* mode      */
  TRUE     /* preview   */
};


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "mask_size", "Oil paint mask size" },
    { GIMP_PDB_INT32, "mode", "Algorithm {RGB (0), INTENSITY (1)}" }
  };

  gimp_install_procedure ("plug_in_oilify",
                          "Modify the specified drawable to resemble an oil "
                          "painting",
                          "This function performs the well-known oil-paint "
                          "effect on the specified drawable.  The size of the "
                          "input mask is specified by 'mask_size'.",
                          "Torsten Martinsen",
                          "Torsten Martinsen",
                          "1996",
                          N_("Oili_fy..."),
                          "RGB*, GRAY*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);

  gimp_plugin_menu_register ("plug_in_oilify",
                             N_("<Image>/Filters/Artistic"));
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;

  INIT_I18N ();

  run_mode = param[0].data.d_int32;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  *nreturn_vals = 2;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_oilify", &ovals);

      /*  First acquire information with a dialog  */
      if (! oilify_dialog (drawable))
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
        {
          status = GIMP_PDB_CALLING_ERROR;
        }
      else
        {
          ovals.mask_size = (gdouble) param[3].data.d_int32;
          ovals.mode = (gint) param[4].data.d_int32;

          if ((ovals.mask_size < 1.0) ||
              ((ovals.mode != MODE_INTEN) &&
               (ovals.mode != MODE_RGB)))
            status = GIMP_PDB_CALLING_ERROR;
        }
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_oilify", &ovals);
      break;

    default:
      break;
    }

  /*  Make sure that the drawable is gray or RGB color  */
  if ((status == GIMP_PDB_SUCCESS) &&
      (gimp_drawable_is_rgb (drawable->drawable_id) ||
       gimp_drawable_is_gray (drawable->drawable_id)))
    {
      gimp_progress_init (_("Oil Painting..."));
      gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));

      oilify (drawable, NULL);

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
        gimp_displays_flush ();

      /*  Store data  */
      if (run_mode == GIMP_RUN_INTERACTIVE)
        gimp_set_data ("plug_in_oilify", &ovals, sizeof (OilifyVals));
    }
  else
    {
      /* gimp_message ("oilify: cannot operate on indexed color images"); */
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*
 * For each RGB channel, replace the pixel at (x,y) with the
 * value that occurs most often in the n x n chunk centered
 * at (x,y).
 */
static void
oilify_rgb (GimpDrawable *drawable,
            GimpPreview  *preview)
{
  GimpPixelRgn  src_rgn, dest_rgn;
  gint          bytes;
  gint          width, height;
  guchar       *src_row, *src;
  guchar       *dest_row, *dest;
  gint          x, y, c, b, xx, yy, n;
  gint          x1, y1, x2, y2;
  gint          x3, y3, x4, y4;
  gint          Cnt[4];
  gint          Hist[4][HISTSIZE];
  gpointer      pr1;
  gint          progress, max_progress;
  guchar       *src_buf;

  /*  get the selection bounds  */
  if (preview)
    {
      gimp_preview_get_position (preview, &x1, &y1);
      gimp_preview_get_size (preview, &width, &height);

      x2 = x1 + width;
      y2 = y1 + height;
    }
  else
    {
      gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
      width  = x2 - x1;
      height = y2 - y1;
    }

  progress = 0;
  max_progress = width * height;

  bytes = drawable->bpp;

  n = (int) ovals.mask_size / 2;

  gimp_pixel_rgn_init (&dest_rgn, drawable,
                       x1, y1, width, height, (preview == NULL), TRUE);
  gimp_pixel_rgn_init (&src_rgn, drawable,
                       x1, y1, width, height, FALSE, FALSE);
  src_buf = g_new (guchar, width * height * bytes);
  gimp_pixel_rgn_get_rect (&src_rgn, src_buf, x1, y1, width, height);

  for (pr1 = gimp_pixel_rgns_register (1, &dest_rgn);
       pr1 != NULL;
       pr1 = gimp_pixel_rgns_process (pr1))
    {
      dest_row = dest_rgn.data;

      for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
        {
          dest = dest_row;

          for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
            {
              x3 = CLAMP ((x - n), x1, x2);
              y3 = CLAMP ((y - n), y1, y2);
              x4 = CLAMP ((x + n + 1), x1, x2);
              y4 = CLAMP ((y + n + 1), y1, y2);

              memset(Cnt, 0, sizeof(Cnt));
              memset(Hist, 0, sizeof(Hist));

              src_row = src_buf + ((y3 - y1) * width + (x3 - x1)) * bytes;
              for (yy = y3 ; yy < y4 ; yy++)
                {
                  src = src_row;
                  for (xx = x3 ; xx < x4 ; xx++)
                    {
                      for (b = 0; b < bytes; b++)
                        {
                          if ((c = ++Hist[b][src[b]]) > Cnt[b])
                            {
                              dest[b] = src[b];
                              Cnt[b] = c;
                            }
                        }
                      src += bytes;
                    }
                  src_row += width * bytes;
                }

                dest += bytes;
            }

          dest_row += dest_rgn.rowstride;
        }

      if (preview)
        {
          gimp_drawable_preview_draw_region (preview, &dest_rgn);
        }
      else
        {
          progress += dest_rgn.w * dest_rgn.h;
          if ((progress % 5) == 0)
            gimp_progress_update ((double) progress / (double) max_progress);
        }
    }

  g_free (src_buf);

  if (!preview)
    {
      /*  update the oil-painted region  */
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id, x1, y1, width, height);
    }
}

/*
 * For each RGB channel, replace the pixel at (x,y) with the
 * value that occurs most often in the n x n chunk centered
 * at (x,y). Histogram is based on intensity.
 */
static void
oilify_intensity (GimpDrawable *drawable,
                  GimpPreview  *preview)
{
  GimpPixelRgn  src_rgn, dest_rgn;
  gint          bytes;
  gint          width, height;
  guchar       *src_row, *src, *selected_src = NULL;
  guchar       *dest_row, *dest;
  gint          x, y, c, xx, yy, n;
  gint          x1, y1, x2, y2;
  gint          x3, y3, x4, y4;
  gint          Cnt;
  gint          Hist[HISTSIZE];
  gpointer      pr1;
  gint          progress, max_progress;
  guchar       *src_buf;

  /*  get the selection bounds  */
  if (preview)
    {
      gimp_preview_get_position (preview, &x1, &y1);
      gimp_preview_get_size (preview, &width, &height);

      x2 = x1 + width;
      y2 = y1 + height;
    }
  else
    {
      gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
      width  = x2 - x1;
      height = y2 - y1;
    }
  bytes = drawable->bpp;

  progress = 0;
  max_progress = width * height;

  n = (int) ovals.mask_size / 2;

  gimp_pixel_rgn_init (&dest_rgn, drawable,
                       x1, y1, width, height, (preview == NULL), TRUE);
  gimp_pixel_rgn_init (&src_rgn, drawable,
                       x1, y1, width, height, FALSE, FALSE);
  src_buf = g_new (guchar, width * height * bytes);
  gimp_pixel_rgn_get_rect (&src_rgn, src_buf, x1, y1, width, height);

  for (pr1 = gimp_pixel_rgns_register (1, &dest_rgn);
       pr1 != NULL;
       pr1 = gimp_pixel_rgns_process (pr1))
    {
      dest_row = dest_rgn.data;

      for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
        {
          dest = dest_row;

          for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
            {
              Cnt = 0;
              memset(Hist, 0, sizeof(Hist));

              x3 = CLAMP ((x - n), x1, x2);
              y3 = CLAMP ((y - n), y1, y2);
              x4 = CLAMP ((x + n + 1), x1, x2);
              y4 = CLAMP ((y + n + 1), y1, y2);

              src_row = src_buf + ((y3 - y1) * width + (x3 - x1)) * bytes;
              for (yy = y3 ; yy < y4 ; yy++)
                {
                  src = src_row;
                  for (xx = x3 ; xx < x4 ; xx++)
                    {
                      if ((c = ++Hist[INTENSITY(src)]) > Cnt)
                        {
                          Cnt = c;
                          selected_src = src;
                        }

                      src += bytes;
                    }

                  src_row += width * bytes;
                }
              memcpy (dest, selected_src, bytes);
              dest += bytes;
            }

          dest_row += dest_rgn.rowstride;
        }

      if (preview)
        {
          gimp_drawable_preview_draw_region (preview, &dest_rgn);
        }
      else
        {
          progress += dest_rgn.w * dest_rgn.h;
          if ((progress % 5) == 0)
            gimp_progress_update ((double) progress / (double) max_progress);
        }
    }

  g_free (src_buf);

  if (!preview)
    {
      /*  update the oil-painted region  */
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id, x1, y1, width, height);
    }
}

static void
oilify (GimpDrawable *drawable,
        GimpPreview  *preview)
{
  if (gimp_drawable_is_rgb (drawable->drawable_id) &&
      (ovals.mode == MODE_INTEN))
    oilify_intensity (drawable, preview);
  else
    oilify_rgb (drawable, preview);
}

static gint
oilify_dialog (GimpDrawable *drawable)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *preview;
  GtkWidget *table;
  GtkWidget *toggle;
  GtkObject *adj;
  gboolean   run;

  gimp_ui_init ("oilify", FALSE);

  dialog = gimp_dialog_new (_("Oilify"), "oilify",
                            NULL, 0,
                            gimp_standard_help_func, "plug-in-oilify",

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
  gtk_widget_show (main_vbox);

  preview = gimp_drawable_preview_new (drawable, &ovals.preview);
  gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
  gtk_widget_show (preview);
  g_signal_connect_swapped (preview, "invalidated",
                            G_CALLBACK (oilify), drawable);

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                              _("_Mask size:"), SCALE_WIDTH, 0,
                              ovals.mask_size, 3.0, 50.0, 1.0, 5.0, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &ovals.mask_size);
  g_signal_connect_swapped (adj, "value_changed",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);

  toggle = gtk_check_button_new_with_mnemonic (_("_Use intensity algorithm"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 3, 1, 2, GTK_FILL, 0, 0, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), ovals.mode);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &ovals.mode);
  g_signal_connect_swapped (toggle, "toggled",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);

  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}
