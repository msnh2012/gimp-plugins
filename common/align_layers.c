/* align_layers.c -- This is a plug-in for the GIMP (1.0's API)
 * Author: Shuji Narazaki <narazaki@InetQ.or.jp>
 * Time-stamp: <1999-12-18 05:48:38 yasuhiro>
 * Version:  0.26
 *
 * Copyright (C) 1997-1998 Shuji Narazaki <narazaki@InetQ.or.jp>
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#define	PLUG_IN_NAME "plug_in_align_layers"
#define SHORT_NAME   "align_layers"
#define SCALE_WIDTH  150

enum
{
  H_NONE,
  H_COLLECT,
  LEFT2RIGHT,
  RIGHT2LEFT,
  SNAP2HGRID
};

enum
{
  H_BASE_LEFT,
  H_BASE_CENTER,
  H_BASE_RIGHT
};

enum
{
  V_NONE,
  V_COLLECT,
  TOP2BOTTOM,
  BOTTOM2TOP,
  SNAP2VGRID
};

enum
{
  V_BASE_TOP,
  V_BASE_CENTER,
  V_BASE_BOTTOM
};

static void	query	(void);
static void	run	(const gchar      *name,
			 gint              nparams,
			 const GimpParam  *param,
			 gint             *nreturn_vals,
			 GimpParam       **return_vals);

static GimpPDBStatusType align_layers                   (gint32  image_id);
static void              align_layers_get_align_offsets (gint32  drawable_id,
                                                         gint	*x,
                                                         gint	*y);

static gint align_layers_dialog      (void);
static void align_layers_ok_callback (GtkWidget *widget,
				      gpointer   data);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


/* dialog variables */
typedef struct
{
  gint	h_style;
  gint	h_base;
  gint	v_style;
  gint	v_base;
  gint	ignore_bottom;
  gint	base_is_bottom_layer;
  gint	grid_size;
} ValueType;

static ValueType VALS =
{
  H_NONE,
  H_BASE_LEFT,
  V_NONE,
  V_BASE_TOP,
  TRUE,
  FALSE,
  10
};

typedef struct
{
  gboolean run;
} Interface;

static Interface INTERFACE =
{
  FALSE
};


MAIN ()

static void
query (void)
{
  static GimpParamDef args [] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive"},
    { GIMP_PDB_IMAGE, "image", "Input image"},
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable (not used)"},
    { GIMP_PDB_INT32, "link-afteer-alignment", "Link the visible layers after alignment"},
    { GIMP_PDB_INT32, "use-bottom", "use the bottom layer as the base of alignment"}
  };

  gimp_install_procedure (PLUG_IN_NAME,
			  "Align visible layers",
			  "Align visible layers",
			  "Shuji Narazaki <narazaki@InetQ.or.jp>",
			  "Shuji Narazaki",
			  "1997",
			  N_("<Image>/Layer/Align _Visible Layers..."),
			  "RGB*,GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_EXECUTION_ERROR;
  GimpRunMode       run_mode;
  gint              image_id, layer_num;

  run_mode = param[0].data.d_int32;
  image_id = param[1].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  switch ( run_mode )
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_image_get_layers (image_id, &layer_num);
      if (layer_num < 2)
	{
	  g_message (_("There are not enough layers to align."));
	  return;
	}
      gimp_get_data (PLUG_IN_NAME, &VALS);
      if (! align_layers_dialog ())
	return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      gimp_get_data (PLUG_IN_NAME, &VALS);
      break;
    }

  status = align_layers (image_id);

  if (run_mode != GIMP_RUN_NONINTERACTIVE)
    gimp_displays_flush ();
  if (run_mode == GIMP_RUN_INTERACTIVE && status == GIMP_PDB_SUCCESS)
    gimp_set_data (PLUG_IN_NAME, &VALS, sizeof (ValueType));

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
}

static GimpPDBStatusType
align_layers (gint32 image_id)
{
  gint	layer_num = 0;
  gint	visible_layer_num = 0;
  gint *layers = NULL;
  gint	index;
  gint  vindex;
  gint	step_x   = 0;
  gint	step_y   = 0;
  gint	x        = 0;
  gint	y        = 0;
  gint	orig_x   = 0;
  gint	orig_y   = 0;
  gint	offset_x = 0;
  gint	offset_y = 0;
  gint	base_x   = 0;
  gint	base_y   = 0;
  gint  bg_index = 0;

  layers = gimp_image_get_layers (image_id, &layer_num);
  bg_index = layer_num - 1;

  for (index = 0; index < layer_num; index++)
    {
      if (gimp_layer_get_visible (layers[index]))
	visible_layer_num++;
    }

  if (VALS.ignore_bottom)
    {
      layer_num--;
      if (gimp_layer_get_visible (layers[bg_index]))
	visible_layer_num--;
    }

  if (0 < visible_layer_num)
    {
      gint	min_x = G_MAXINT;
      gint	min_y = G_MAXINT;
      gint	max_x = G_MININT;
      gint	max_y = G_MININT;

      /* 0 is the top layer */
      for (index = 0; index < layer_num; index++)
	{
	  if (gimp_layer_get_visible (layers[index]))
	    {
	      gimp_drawable_offsets (layers[index], &orig_x, &orig_y);
	      align_layers_get_align_offsets (layers[index], &offset_x,
					      &offset_y);
	      orig_x += offset_x;
	      orig_y += offset_y;

	      if ( orig_x < min_x ) min_x = orig_x;
	      if ( max_x < orig_x ) max_x = orig_x;
	      if ( orig_y < min_y ) min_y = orig_y;
	      if ( max_y < orig_y ) max_y = orig_y;
	    }
	}

      if (VALS.base_is_bottom_layer)
	{
	  gimp_drawable_offsets (layers[bg_index], &orig_x, &orig_y);
	  align_layers_get_align_offsets (layers[bg_index], &offset_x,
					  &offset_y);
	  orig_x += offset_x;
	  orig_y += offset_y;
	  base_x = min_x = orig_x;
	  base_y = min_y = orig_y;
	}

      if (visible_layer_num > 1)
	{
	  step_x = (max_x - min_x) / (visible_layer_num - 1);
	  step_y = (max_y - min_y) / (visible_layer_num - 1);
	}

      if ( (VALS.h_style == LEFT2RIGHT) || (VALS.h_style == RIGHT2LEFT))
	base_x = min_x;

      if ( (VALS.v_style == TOP2BOTTOM) || (VALS.v_style == BOTTOM2TOP))
	base_y = min_y;
    }

  gimp_undo_push_group_start (image_id);

  for (vindex = -1, index = 0; index < layer_num; index++)
    {
      if (gimp_layer_get_visible (layers[index]))
	vindex++;
      else
	continue;

      gimp_drawable_offsets (layers[index], &orig_x, &orig_y);
      align_layers_get_align_offsets (layers[index], &offset_x, &offset_y);

      switch (VALS.h_style)
	{
	case H_NONE:
	  x = orig_x;
	  break;
	case H_COLLECT:
	  x = base_x - offset_x;
	  break;
	case LEFT2RIGHT:
	  x = (base_x + vindex * step_x) - offset_x;
	  break;
	case RIGHT2LEFT:
	  x = (base_x + (visible_layer_num - vindex - 1) * step_x) - offset_x;
	  break;
	case SNAP2HGRID:
	  x = VALS.grid_size
	    * (int) ((orig_x + offset_x + VALS.grid_size /2) / VALS.grid_size)
	    - offset_x;
	  break;
	}
      switch (VALS.v_style)
	{
	case V_NONE:
	  y = orig_y;
	  break;
	case V_COLLECT:
	  y = base_y - offset_y;
	  break;
	case TOP2BOTTOM:
	  y = (base_y + vindex * step_y) - offset_y;
	  break;
	case BOTTOM2TOP:
	  y = (base_y + (visible_layer_num - vindex - 1) * step_y) - offset_y;
	  break;
	case SNAP2VGRID:
	  y = VALS.grid_size
	    * (int) ((orig_y + offset_y + VALS.grid_size / 2) / VALS.grid_size)
	    - offset_y;
	  break;
	}
      gimp_layer_set_offsets (layers[index], x, y);
    }

  gimp_undo_push_group_end (image_id);

  return GIMP_PDB_SUCCESS;
}

static void
align_layers_get_align_offsets (gint32	drawable_id,
				gint   *x,
				gint   *y)
{
  GimpDrawable	*layer = gimp_drawable_get (drawable_id);

  switch (VALS.h_base)
    {
    case H_BASE_LEFT:
      *x = 0;
      break;
    case H_BASE_CENTER:
      *x = (gint) (layer->width / 2);
      break;
    case H_BASE_RIGHT:
      *x = layer->width;
      break;
    default:
      *x = 0;
      break;
    }
  switch (VALS.v_base)
    {
    case V_BASE_TOP:
      *y = 0;
      break;
    case V_BASE_CENTER:
      *y = (gint) (layer->height / 2);
      break;
    case V_BASE_BOTTOM:
      *y = layer->height;
      break;
    default:
      *y = 0;
      break;
    }
}

/* dialog stuff */
static int
align_layers_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *table;
  GtkWidget *optionmenu;
  GtkWidget *toggle;
  GtkObject *adj;

  gimp_ui_init (SHORT_NAME, FALSE);

  dlg = gimp_dialog_new (_("Align Visible Layers"), SHORT_NAME,
			 gimp_standard_help_func, "filters/align_layers.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 GTK_STOCK_OK, align_layers_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (dlg, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  table = gimp_parameter_settings_new (GTK_DIALOG (dlg)->vbox, 7, 3);

  optionmenu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gimp_menu_item_update),
			   &VALS.h_style, (gpointer) VALS.h_style,

			   _("None"),
			   (gpointer) H_NONE, NULL,
			   _("Collect"),
			   (gpointer) H_COLLECT, NULL,
			   _("Fill (left to right)"),
			   (gpointer) LEFT2RIGHT, NULL,
			   _("Fill (right to left)"),
			   (gpointer) RIGHT2LEFT, NULL,
			   _("Snap to Grid"),
			   (gpointer) SNAP2HGRID, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("_Horizontal Style:"), 1.0, 0.5,
			     optionmenu, 1, FALSE);

  optionmenu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gimp_menu_item_update),
			   &VALS.h_base, (gpointer) VALS.h_base,

			   _("Left Edge"),  (gpointer) H_BASE_LEFT, NULL,
			   _("Center"),     (gpointer) H_BASE_CENTER, NULL,
			   _("Right Edge"), (gpointer) H_BASE_RIGHT, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Ho_rizontal Base:"), 1.0, 0.5,
			     optionmenu, 1, FALSE);

  optionmenu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gimp_menu_item_update),
			   &VALS.v_style, (gpointer) VALS.v_style,

			   _("None"),
			   (gpointer) V_NONE, NULL,
			   _("Collect"),
			   (gpointer) V_COLLECT, NULL,
			   _("Fill (top to bottom)"),
			   (gpointer) TOP2BOTTOM, NULL,
			   _("Fill (bottom to top)"),
			   (gpointer) BOTTOM2TOP, NULL,
			   _("Snap to Grid"),
			   (gpointer) SNAP2VGRID, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
			     _("_Vertical Style:"), 1.0, 0.5,
			     optionmenu, 1, FALSE);

  optionmenu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gimp_menu_item_update),
			   &VALS.v_base, (gpointer) VALS.v_base,

			   _("Top Edge"),    (gpointer) V_BASE_TOP, NULL,
			   _("Center"),      (gpointer) V_BASE_CENTER, NULL,
			   _("Bottom Edge"), (gpointer) V_BASE_BOTTOM, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 3,
			     _("Ver_tical Base:"), 1.0, 0.5,
			     optionmenu, 1, FALSE);

  toggle =
    gtk_check_button_new_with_mnemonic
    (_("_Ignore the Bottom Layer even if Visible"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), VALS.ignore_bottom);
  gtk_table_attach_defaults (GTK_TABLE (table), toggle, 0, 2, 4, 5);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &VALS.ignore_bottom);

  toggle =
    gtk_check_button_new_with_mnemonic
    (_("_Use the (Invisible) Bottom Layer as the Base"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
				VALS.base_is_bottom_layer);
  gtk_table_attach_defaults (GTK_TABLE (table), toggle, 0, 2, 5, 6);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &VALS.base_is_bottom_layer);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 6,
			      _("_Grid Size:"), SCALE_WIDTH, 0,
			      VALS.grid_size, 0, 200, 1, 10, 0,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &VALS.grid_size);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return INTERFACE.run;
}

static void
align_layers_ok_callback (GtkWidget *widget,
			  gpointer   data)
{
  INTERFACE.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}
