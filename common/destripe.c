/*
 *   Destripe filter for The GIMP -- an image manipulation
 *   program
 *
 *   Copyright 1997 Marc Lehmann, heavily modified from a filter by
 *   Michael Sweet.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/*
 * Constants...
 */

#define PLUG_IN_NAME     "plug_in_destripe"
#define PLUG_IN_VERSION  "0.2"
#define HELP_ID          "plug-in-destripe"
#define PREVIEW_SIZE     200
#define SCALE_WIDTH      140
#define MAX_AVG          100


/*
 * Local functions...
 */

static void      query (void);
static void      run   (const gchar      *name,
                        gint              nparams,
                        const GimpParam  *param,
                        gint             *nreturn_vals,
                        GimpParam       **return_vals);

static void      destripe                (void);

static gboolean  destripe_dialog         (void);

static void      preview_init            (void);
static void      preview_update          (void);
static void      preview_scroll_callback (void);


/*
 * Globals...
 */

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run    /* run_proc   */
};

GtkWidget      *preview;                /* Preview widget */
gint            preview_width,          /* Width of preview widget */
                preview_height,         /* Height of preview widget */
                preview_x1,             /* Upper-left X of preview */
                preview_y1,             /* Upper-left Y of preview */
                preview_x2,             /* Lower-right X of preview */
                preview_y2;             /* Lower-right Y of preview */
GtkObject      *hscroll_data,           /* Horizontal scrollbar data */
               *vscroll_data;           /* Vertical scrollbar data */

GimpDrawable   *drawable = NULL;        /* Current image */
gint            sel_x1,                 /* Selection bounds */
                sel_y1,
                sel_x2,
                sel_y2;
gint            img_bpp;                /* Bytes-per-pixel in image */


typedef struct
{
  gboolean      histogram;
  gint          avg_width;
} DestripeValues;

static DestripeValues vals =
{
  FALSE,
  36
};


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",  "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",     "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",  "Input drawable" },
    { GIMP_PDB_INT32,    "avg_width", "Averaging filter width (default = 36)" }
  };

  gimp_install_procedure (PLUG_IN_NAME,
                          "Destripe filter, used to remove vertical stripes "
                          "caused by cheap scanners.",
                          "This plug-in tries to remove vertical stripes from "
                          "an image.",
                          "Marc Lehmann <pcg@goof.com>",
                          "Marc Lehmann <pcg@goof.com>",
                          PLUG_IN_VERSION,
                          N_("Des_tripe..."),
                          "RGB*, GRAY*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);

  gimp_plugin_menu_register (PLUG_IN_NAME,
                             N_("<Image>/Filters/Enhance"));
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  GimpRunMode       run_mode;   /* Current run mode */
  GimpPDBStatusType status;     /* Return status */
  static GimpParam  values[1];  /* Return values */

  INIT_I18N ();

  /*
   * Initialize parameter data...
   */

  status   = GIMP_PDB_SUCCESS;
  run_mode = param[0].data.d_int32;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;

  /*
   * Get drawable information...
   */

  drawable = gimp_drawable_get (param[2].data.d_drawable);

  gimp_drawable_mask_bounds (drawable->drawable_id,
                             &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  img_bpp = gimp_drawable_bpp (drawable->drawable_id);

  /*
   * See how we will run
   */

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*
       * Possibly retrieve data...
       */
      gimp_get_data (PLUG_IN_NAME, &vals);

      /*
       * Get information from the dialog...
       */
      if (!destripe_dialog ())
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      /*
       * Make sure all the arguments are present...
       */
      if (nparams != 4)
        status = GIMP_PDB_CALLING_ERROR;
      else
        vals.avg_width = param[3].data.d_int32;
      break;

    case GIMP_RUN_WITH_LAST_VALS :
      /*
       * Possibly retrieve data...
       */
      gimp_get_data (PLUG_IN_NAME, &vals);
      break;

    default :
      status = GIMP_PDB_CALLING_ERROR;
      break;
    };

  /*
   * Destripe the image...
   */

  if (status == GIMP_PDB_SUCCESS)
    {
      if ((gimp_drawable_is_rgb (drawable->drawable_id) ||
           gimp_drawable_is_gray (drawable->drawable_id)))
        {
          /*
           * Set the tile cache size...
           */
          gimp_tile_cache_ntiles ((drawable->width + gimp_tile_width () - 1) /
                                  gimp_tile_width ());

          /*
           * Run!
           */
          destripe ();

          /*
           * If run mode is interactive, flush displays...
           */
          if (run_mode != GIMP_RUN_NONINTERACTIVE)
            gimp_displays_flush ();

          /*
           * Store data...
           */
          if (run_mode == GIMP_RUN_INTERACTIVE)
            gimp_set_data (PLUG_IN_NAME, &vals, sizeof (vals));
        }
      else
        status = GIMP_PDB_EXECUTION_ERROR;
    };

  /*
   * Reset the current run status...
   */
  values[0].data.d_status = status;

  /*
   * Detach from the drawable...
   */
  gimp_drawable_detach (drawable);
}

static void
destripe_rect (gint      sel_x1,
               gint      sel_y1,
               gint      sel_x2,
               gint      sel_y2,
               gboolean  do_preview)
{
  GimpPixelRgn  src_rgn;        /* source image region */
  GimpPixelRgn  dst_rgn;        /* destination image region */
  guchar       *src_rows;       /* image data */
  guchar       *dest_rgba = NULL;
  double        progress, progress_inc;
  int           sel_width = sel_x2 - sel_x1;
  int           sel_height = sel_y2 - sel_y1;
  long         *hist, *corr;        /* "histogram" data */
  int           tile_width = gimp_tile_width ();
  int           i, x, y, ox, cols;

  /* initialize */

  progress = 0.0;
  progress_inc = 0.0;

  /*
   * Let the user know what we're doing...
   */

  if (!do_preview)
    {
      gimp_progress_init (_("Destriping..."));

      progress = 0;
      progress_inc = 0.5 * tile_width / sel_width;
    } else
    {
      dest_rgba = g_new(guchar, img_bpp * preview_width * preview_height);
    }

  /*
   * Setup for filter...
   */

  gimp_pixel_rgn_init (&src_rgn, drawable,
                       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
  gimp_pixel_rgn_init (&dst_rgn, drawable,
                       sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);

  hist = g_new (long, sel_width * img_bpp);
  corr = g_new (long, sel_width * img_bpp);
  src_rows = g_new (guchar, tile_width * sel_height * img_bpp);

  memset (hist, 0, sel_width * img_bpp * sizeof (long));

  /*
   * collect "histogram" data.
   */

  for (ox = sel_x1; ox < sel_x2; ox += tile_width)
    {
      guchar *rows = src_rows;

      cols = sel_x2 - ox;
      if (cols > tile_width)
        cols = tile_width;

      gimp_pixel_rgn_get_rect (&src_rgn, rows, ox, sel_y1, cols, sel_height);

      for (y = 0; y < sel_height; y++)
        {
          long   *h       = hist + (ox - sel_x1) * img_bpp;
          guchar *row_end = rows + cols * img_bpp;

          while (rows < row_end)
            *h++ += *rows++;
        }

      if (!do_preview)
        gimp_progress_update (progress += progress_inc);
    }

  /*
   * average out histogram
   */

  if (TRUE)
    {
      gint extend = (vals.avg_width / 2) * img_bpp;

      for (i = 0; i < MIN (3, img_bpp); i++)
        {
          long *h   = hist - extend + i;
          long *c   = corr - extend + i;
          long  sum = 0;
          gint  cnt = 0;

          for (x = -extend; x < sel_width * img_bpp; x += img_bpp)
            {
              if (x + extend < sel_width * img_bpp)
                {
                  sum += h[ extend]; cnt++;
                }
              if (x - extend >= 0)
                {
                  sum -= h[-extend]; cnt--;
                }
              if (x >= 0)
                {
                  if (*h)
                    *c = ((sum / cnt - *h) << 10) / *h;
                  else
                    *c = G_MAXINT;
                }

              h += img_bpp;
              c += img_bpp;
            }
        }
    }
  else
    {
      for (i = 0; i < MIN (3, img_bpp); i++)
        {
          long *h = hist + i + sel_width * img_bpp - img_bpp;
          long *c = corr + i + sel_width * img_bpp - img_bpp;
          long  i = *h;

          do
            {
              h -= img_bpp;
              c -= img_bpp;

              if (*h - i > vals.avg_width && i - *h > vals.avg_width)
                i = *h;

              if (*h)
                *c = (i-128) << 10 / *h;
              else
                *c = G_MAXINT;
            }
          while (h > hist);
        }
    }

  /*
   * remove stripes.
   */

  for (ox = sel_x1; ox < sel_x2; ox += tile_width)
    {
      guchar *rows = src_rows;

      cols = sel_x2 - ox;
      if (cols > tile_width)
        cols = tile_width;

      gimp_pixel_rgn_get_rect (&src_rgn, rows, ox, sel_y1, cols, sel_height);

      if (!do_preview)
        gimp_progress_update (progress += progress_inc);

      for (y = 0; y < sel_height; y++)
        {
          long   *c = corr + (ox - sel_x1) * img_bpp;
          guchar *row_end = rows + cols * img_bpp;

          if (vals.histogram)
            while (rows < row_end)
              {
                *rows = MIN (255, MAX (0, 128 + (*rows * *c >> 10)));
                c++; rows++;
              }
          else
            while (rows < row_end)
              {
                *rows = MIN (255, MAX (0, *rows + (*rows * *c >> 10) ));
                c++; rows++;
              }

          if (do_preview)
            memcpy (dest_rgba + img_bpp * (y * preview_width+ox-sel_x1),
                    rows - cols * img_bpp, cols * img_bpp);
        }

      if (!do_preview)
        {
          gimp_pixel_rgn_set_rect (&dst_rgn, src_rows,
                                   ox, sel_y1, cols, sel_height);
          gimp_progress_update (progress += progress_inc);
        }
    }

  g_free (src_rows);

  /*
   * Update the screen...
   */

  if (do_preview)
    {
      gimp_preview_area_draw (GIMP_PREVIEW_AREA (preview),
                              0, 0, preview_width, preview_height,
                              gimp_drawable_type (drawable->drawable_id),
                              dest_rgba,
                              img_bpp * preview_width);
      g_free (dest_rgba);
    }
  else
    {
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id,
                            sel_x1, sel_y1, sel_width, sel_height);
    }
  g_free (hist);
  g_free (corr);
}

/*
 * 'destripe()' - Destripe an image.
 *
 */

static void
destripe (void)
{
  destripe_rect (sel_x1, sel_y1, sel_x2, sel_y2, FALSE);
}

static gboolean
destripe_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *frame;
  GtkWidget *scrollbar;
  GtkWidget *button;
  GtkObject *adj;
  gboolean   run;

  gimp_ui_init ("destripe", TRUE);

  dialog = gimp_dialog_new (_("Destripe"), "destripe",
                            NULL, 0,
                            gimp_standard_help_func, HELP_ID,

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox,
                      TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  table = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_table_attach(GTK_TABLE(table), frame, 0, 1, 0, 1,
                   0, 0, 0, 0);
  gtk_widget_show (frame);

  preview_width  = MIN (sel_x2 - sel_x1, PREVIEW_SIZE);
  preview_height = MIN (sel_y2 - sel_y1, PREVIEW_SIZE);

  preview = gimp_preview_area_new ();
  gtk_widget_set_size_request (preview, preview_width, preview_height);
  gtk_container_add (GTK_CONTAINER (frame), preview);
  gtk_widget_show (preview);

  hscroll_data = gtk_adjustment_new (0, 0, sel_x2 - sel_x1 - 1, 1.0,
                                     MIN (preview_width, sel_x2 - sel_x1),
                                     MIN (preview_width, sel_x2 - sel_x1));

  g_signal_connect (hscroll_data, "value_changed",
                    G_CALLBACK (preview_scroll_callback),
                    NULL);

  scrollbar = gtk_hscrollbar_new (GTK_ADJUSTMENT (hscroll_data));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_CONTINUOUS);
  gtk_table_attach (GTK_TABLE (table), scrollbar, 0, 1, 1, 2,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (scrollbar);

  vscroll_data = gtk_adjustment_new (0, 0, sel_y2 - sel_y1 - 1, 1.0,
                                     MIN (preview_height, sel_y2 - sel_y1),
                                     MIN (preview_height, sel_y2 - sel_y1));

  g_signal_connect (vscroll_data, "value_changed",
                    G_CALLBACK (preview_scroll_callback),
                    NULL);

  scrollbar = gtk_vscrollbar_new (GTK_ADJUSTMENT (vscroll_data));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_CONTINUOUS);
  gtk_table_attach (GTK_TABLE (table), scrollbar, 1, 2, 0, 1, 0,
                    GTK_FILL, 0, 0);
  gtk_widget_show (scrollbar);

  preview_init ();

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
                              _("_Width:"), SCALE_WIDTH, 0,
                              vals.avg_width, 2, MAX_AVG, 1, 10, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &vals.avg_width);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (preview_update),
                    NULL);

  button = gtk_check_button_new_with_mnemonic (_("Create _histogram"));
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), vals.histogram);
  gtk_widget_show (button);

  g_signal_connect (button, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &vals.histogram);
  g_signal_connect (button, "toggled",
                    G_CALLBACK (preview_update),
                    NULL);

  gtk_widget_show (dialog);

  preview_update ();

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}

/*  preview functions  */

static void
preview_init (void)
{
  preview_x1 = sel_x1;
  preview_y1 = sel_y1;
  preview_x2 = preview_x1 + MIN (preview_width, sel_x2 - sel_x1);
  preview_y2 = preview_y1 + MIN (preview_height, sel_y2 -sel_y1);
}

static void
preview_scroll_callback (void)
{
  preview_x1 = sel_x1 + GTK_ADJUSTMENT (hscroll_data)->value;
  preview_y1 = sel_y1 + GTK_ADJUSTMENT (vscroll_data)->value;
  preview_x2 = preview_x1 + MIN (preview_width, sel_x2 - sel_x1);
  preview_y2 = preview_y1 + MIN (preview_height, sel_y2 - sel_y1);

  preview_update ();
}

static void
preview_update (void)
{
  destripe_rect (preview_x1, preview_y1, preview_x2, preview_y2, TRUE);
}
