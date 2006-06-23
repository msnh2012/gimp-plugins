/*
 * This is a plug-in for the GIMP.
 *
 * Generates clickable image maps.
 *
 * Copyright (C) 1998-2005 Maurits Rijk  m.rijk@chello.nl
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
 */

#include "config.h"

#include <gtk/gtk.h>

#include "imap_about.h"
#include "imap_circle.h"
#include "imap_file.h"
#include "imap_grid.h"
#include "imap_main.h"
#include "imap_menu.h"
#include "imap_menu_funcs.h"
#include "imap_polygon.h"
#include "imap_preferences.h"
#include "imap_rectangle.h"
#include "imap_settings.h"
#include "imap_stock.h"
#include "imap_source.h"

#include "libgimp/stdplugins-intl.h"
#include "libgimpwidgets/gimpstock.h"

/* Fix me: move all of these prototypes to imap_menu.h */

void save();
void do_close();
void do_quit();
void do_cut();
void do_copy();
void do_paste();
void do_clear();
void do_select_all();
void do_deselect_all();
void do_grid_settings_dialog();
void do_zoom_in();
void do_zoom_out();
void do_move_to_front();
void do_send_to_back();
void do_edit_selected_shape();
void do_create_guides_dialog();
void do_use_gimp_guides_dialog();

void imap_help();
void set_func(int func);

static Menu_t _menu;
static GtkUIManager *ui_manager;

GtkWidget*
menu_get_widget(const gchar *path)
{
  return gtk_ui_manager_get_widget (ui_manager, path);
}

static void
set_sensitive (const gchar *path, gboolean sensitive)
{
  GtkAction *action = gtk_ui_manager_get_action (ui_manager, path);
  g_object_set (action, "sensitive", sensitive, NULL);
}

static void
menu_mru(GtkWidget *widget, gpointer data)
{
   MRU_t *mru = get_mru();
   char *filename = (char*) data;

   if (g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
      load(filename);
   } else {
      do_file_error_dialog(_("Error opening file"), filename);
      mru_remove(mru, filename);
      menu_build_mru_items(mru);
   }
}

void
menu_set_zoom_sensitivity(gint factor)
{
  set_sensitive ("/MainMenu/ViewMenu/ZoomIn", factor < 8);
  set_sensitive ("/MainMenu/ViewMenu/ZoomOut", factor > 1);
}

void
menu_shapes_selected(gint count)
{
  gboolean sensitive = (count > 0);
  set_sensitive ("/MainMenu/EditMenu/Cut", sensitive);
  set_sensitive ("/MainMenu/EditMenu/Copy", sensitive);
  set_sensitive ("/MainMenu/EditMenu/Clear", sensitive);
  set_sensitive ("/MainMenu/EditMenu/EditAreaInfo", sensitive);
  set_sensitive ("/MainMenu/EditMenu/DeselectAll", sensitive);
}

#ifdef _NOT_READY_YET_
static void
command_list_changed(Command_t *command, gpointer data)
{
   gchar *scratch;
   GtkWidget *icon;

   /* Set undo entry */
   if (_menu.undo)
      gtk_widget_destroy(_menu.undo);
   scratch = g_strdup_printf (_("_Undo %s"),
                              command && command->name ? command->name : "");
   _menu.undo = insert_item_with_label(_menu.edit_menu, 1, scratch,
				       menu_command, &_menu.cmd_undo);
   g_free (scratch);
   add_accelerator(_menu.undo, 'Z', GDK_CONTROL_MASK);
   gtk_widget_set_sensitive(_menu.undo, (command != NULL));

   icon = gtk_image_new_from_stock(GTK_STOCK_UNDO, GTK_ICON_SIZE_MENU);
   gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(_menu.undo), icon);
   gtk_widget_show(icon);

   /* Set redo entry */
   command = command_list_get_redo_command();
   if (_menu.redo)
      gtk_widget_destroy(_menu.redo);
   scratch = g_strdup_printf (_("_Redo %s"),
                              command && command->name ? command->name : "");
   _menu.redo = insert_item_with_label(_menu.edit_menu, 2, scratch,
				       menu_command, &_menu.cmd_redo);
   g_free (scratch);
   add_accelerator(_menu.redo, 'Z', GDK_SHIFT_MASK|GDK_CONTROL_MASK);
   gtk_widget_set_sensitive(_menu.redo, (command != NULL));

   icon = gtk_image_new_from_stock(GTK_STOCK_REDO, GTK_ICON_SIZE_MENU);
   gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(_menu.redo), icon);
   gtk_widget_show(icon);
}
#endif

static void
paste_buffer_added(Object_t *obj, gpointer data)
{
  set_sensitive("/MainMenu/EditMenu/Paste", TRUE);
}

static void
paste_buffer_removed(Object_t *obj, gpointer data)
{
  set_sensitive("/MainMenu/EditMenu/Paste", FALSE);
}

/* Normal items */
static GtkActionEntry entries[] = {
  { "FileMenu", NULL, "_File" },
  { "Open", GTK_STOCK_OPEN, "_Open...", NULL, "Open", do_file_open_dialog},
  { "OpenRecentMenu", NULL, "Open Recent" },
  { "Save", GTK_STOCK_SAVE, "_Save...", NULL, "Save", save},
  { "SaveAs", GTK_STOCK_SAVE_AS, "Save _As...", "<shift><control>S", NULL,
    do_file_save_as_dialog},
  { "Close", GTK_STOCK_CLOSE, NULL, NULL, NULL, do_close},
  { "Quit", GTK_STOCK_QUIT, NULL, NULL, NULL, do_quit},

  { "EditMenu", NULL, "_Edit" },
  { "Undo", GTK_STOCK_UNDO, NULL, NULL, "Undo", NULL},
  { "Redo", GTK_STOCK_REDO, NULL, NULL, "Redo", NULL},
  { "Cut", GTK_STOCK_CUT, NULL, NULL, "Cut", do_cut},
  { "Copy", GTK_STOCK_COPY, NULL, NULL, "Copy", do_copy},
  { "Paste", GTK_STOCK_PASTE, NULL, NULL, "Paste", do_paste},
  { "Clear", GTK_STOCK_DELETE, NULL, "Delete", NULL, do_clear},
  { "SelectAll", NULL, "Select _All", "<control>A", NULL, do_select_all},
  { "DeselectAll", NULL, "Deselect _All", "<shift><control>A", NULL,
    do_deselect_all},
  { "EditAreaInfo", GTK_STOCK_EDIT, "Edit Area Info...", NULL,
    "Edit selected area info", do_edit_selected_shape},
  { "Preferences", GTK_STOCK_PREFERENCES, NULL, NULL, "Preferences",
    do_preferences_dialog},
  { "MoveToFront", IMAP_STOCK_TO_FRONT, "", NULL, "Move to Front",
    do_move_to_front},
  { "SendToBack", IMAP_STOCK_TO_BACK, "", NULL, "Send to Back",
    do_send_to_back},
  { "DeleteArea", NULL, "Delete Area", NULL, NULL, NULL},
  { "MoveUp", GTK_STOCK_GO_UP, "Move Up", NULL, NULL, NULL},
  { "MoveDown", GTK_STOCK_GO_DOWN, "Move Down", NULL, NULL, NULL},

  { "InsertPoint", NULL, "Insert Point", NULL, NULL, polygon_insert_point},
  { "DeletePoint", NULL, "Delete Point", NULL, NULL, polygon_delete_point},

  { "ViewMenu", NULL, "_View" },
  { "Source", NULL, "Source...", NULL, NULL, do_source_dialog},
  { "ZoomIn", GTK_STOCK_ZOOM_IN, NULL, "plus", "Zoom in", do_zoom_in},
  { "ZoomOut", GTK_STOCK_ZOOM_OUT, NULL, "minus", "Zoom out", do_zoom_out},
  { "ZoomToMenu", NULL, "_Zoom To" },

  { "MappingMenu", NULL, "_Mapping" },
  { "EditMapInfo", IMAP_STOCK_MAP_INFO, "Edit Map Info...", NULL, NULL,
    do_settings_dialog},

  { "ToolsMenu", NULL, "_Tools" },
  { "GridSettings", NULL, "Grid Settings...", NULL, NULL,
    do_grid_settings_dialog},
  { "UseGimpGuides", NULL, "Use GIMP Guides...", NULL, NULL,
    do_use_gimp_guides_dialog},
  { "CreateGuides", NULL, "Create Guides...", NULL, NULL,
    do_create_guides_dialog},

  { "HelpMenu", NULL, "_Help" },
  { "Contents", GTK_STOCK_HELP, "_Contents", NULL, NULL, imap_help},
  { "About", GTK_STOCK_ABOUT, NULL, NULL, NULL,
    do_about_dialog},

  { "ZoomMenu", NULL, "_Zoom" },
};

/* Toggle items */
static GtkToggleActionEntry toggle_entries[] = {
  { "AreaList", NULL, "Area List", NULL, NULL, NULL, TRUE },
  { "Grid", GIMP_STOCK_GRID, "_Grid", NULL, "Grid", toggle_grid, FALSE }
};

static GtkRadioActionEntry color_entries[] = {
  { "Color", NULL, "Color", NULL, NULL, 0},
  { "Gray", NULL, "Gray", NULL, NULL, 1},
};

static GtkRadioActionEntry mapping_entries[] = {
  { "Arrow", GIMP_STOCK_CURSOR, "Arrow", NULL, "Select existing area", 0},
  { "Rectangle", IMAP_STOCK_RECTANGLE, "Rectangle", NULL,
    "Define Rectangle area", 1},
  { "Circle", IMAP_STOCK_CIRCLE, "Circle", NULL, "Define Circle/Oval area", 2},
  { "Polygon", IMAP_STOCK_POLYGON, "Polygon", NULL, "Define Polygon area", 3},
};

static GtkRadioActionEntry zoom_entries[] = {
  { "Zoom1:1", NULL, "1:1", NULL, NULL, 0},
  { "Zoom1:2", NULL, "1:2", NULL, NULL, 1},
  { "Zoom1:3", NULL, "1:3", NULL, NULL, 2},
  { "Zoom1:4", NULL, "1:4", NULL, NULL, 3},
  { "Zoom1:5", NULL, "1:5", NULL, NULL, 4},
  { "Zoom1:6", NULL, "1:6", NULL, NULL, 5},
  { "Zoom1:7", NULL, "1:7", NULL, NULL, 6},
  { "Zoom1:8", NULL, "1:8", NULL, NULL, 7},
};

static const char *ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='Open'/>"
"      <menuitem action='Save'/>"
"      <menuitem action='SaveAs'/>"
"      <separator/>"
"      <menuitem action='Close'/>"
"      <menuitem action='Quit'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Cut'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Paste'/>"
"      <menuitem action='Clear'/>"
"      <separator/>"
"      <menuitem action='SelectAll'/>"
"      <menuitem action='DeselectAll'/>"
"      <separator/>"
"      <menuitem action='EditAreaInfo'/>"
"      <separator/>"
"      <menuitem action='Preferences'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='AreaList'/>"
"      <menuitem action='Source'/>"
"      <separator/>"
"      <menuitem action='Color'/>"
"      <menuitem action='Gray'/>"
"      <separator/>"
"      <menuitem action='ZoomIn'/>"
"      <menuitem action='ZoomOut'/>"
"      <menu action='ZoomToMenu'>"
"        <menuitem action='Zoom1:1'/>"
"        <menuitem action='Zoom1:2'/>"
"        <menuitem action='Zoom1:3'/>"
"        <menuitem action='Zoom1:4'/>"
"        <menuitem action='Zoom1:5'/>"
"        <menuitem action='Zoom1:6'/>"
"        <menuitem action='Zoom1:7'/>"
"        <menuitem action='Zoom1:8'/>"
"      </menu>"
"    </menu>"
"    <menu action='MappingMenu'>"
"      <menuitem action='Arrow'/>"
"      <menuitem action='Rectangle'/>"
"      <menuitem action='Circle'/>"
"      <menuitem action='Polygon'/>"
"      <separator/>"
"      <menuitem action='EditMapInfo'/>"
"    </menu>"
"    <menu action='ToolsMenu'>"
"      <menuitem action='Grid'/>"
"      <menuitem action='GridSettings'/>"
"      <separator/>"
"      <menuitem action='UseGimpGuides'/>"
"      <menuitem action='CreateGuides'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='Contents'/>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
""
"  <popup name='PopupMenu'>"
"    <menuitem action='EditMapInfo'/>"
"    <menu action='ToolsMenu'>"
"      <menuitem action='Arrow'/>"
"      <menuitem action='Rectangle'/>"
"      <menuitem action='Circle'/>"
"      <menuitem action='Polygon'/>"
"    </menu>"
"    <menu action='ZoomMenu'>"
"      <menuitem action='ZoomIn'/>"
"      <menuitem action='ZoomOut'/>"
"    </menu>"
"    <menuitem action='Grid'/>"
"    <menuitem action='GridSettings'/>"
"    <menuitem action='CreateGuides'/>"
"    <menuitem action='Paste'/>"
"  </popup>"
""
"  <popup name='ObjectPopupMenu'>"
"    <menuitem action='EditAreaInfo'/>"
"    <menuitem action='DeleteArea'/>"
"    <menuitem action='MoveUp'/>"
"    <menuitem action='MoveDown'/>"
"    <menuitem action='Cut'/>"
"    <menuitem action='Copy'/>"
"  </popup>"
""
"  <popup name='PolygonPopupMenu'>"
"    <menuitem action='InsertPoint'/>"
"    <menuitem action='DeletePoint'/>"
"    <menuitem action='EditAreaInfo'/>"
"    <menuitem action='DeleteArea'/>"
"    <menuitem action='MoveUp'/>"
"    <menuitem action='MoveDown'/>"
"    <menuitem action='Cut'/>"
"    <menuitem action='Copy'/>"
"  </popup>"
""
"  <toolbar name='Toolbar'>"
"    <toolitem action='Open'/>"
"    <toolitem action='Save'/>"
"    <separator/>"
"    <toolitem action='Preferences'/>"
"    <separator/>"
"    <toolitem action='Undo'/>"
"    <toolitem action='Redo'/>"
"    <separator/>"
"    <toolitem action='Cut'/>"
"    <toolitem action='Copy'/>"
"    <toolitem action='Paste'/>"
"    <separator/>"
"    <toolitem action='ZoomIn'/>"
"    <toolitem action='ZoomOut'/>"
"    <separator/>"
"    <toolitem action='EditMapInfo'/>"
"    <separator/>"
"    <toolitem action='MoveToFront'/>"
"    <toolitem action='SendToBack'/>"
"    <separator/>"
"    <toolitem action='Grid'/>"
"  </toolbar>"
""
"  <toolbar name='Tools'>"
"    <toolitem action='Arrow'/>"
"    <toolitem action='Rectangle'/>"
"    <toolitem action='Circle'/>"
"    <toolitem action='Polygon'/>"
"    <separator/>"
"    <toolitem action='EditAreaInfo'/>"
"  </toolbar>"
""
"  <toolbar name='Selection'>"
"    <toolitem action='MoveUp'/>"
"    <toolitem action='MoveDown'/>"
"    <toolitem action='EditAreaInfo'/>"
"    <toolitem action='Clear'/>"
"  </toolbar>"
"</ui>";

Menu_t*
make_menu(GtkWidget *main_vbox, GtkWidget *window)
{
  GtkWidget *menubar;
  GtkActionGroup *action_group;
  GtkAccelGroup *accel_group;
  GError *error;

  action_group = gtk_action_group_new ("MenuActions");
  gtk_action_group_add_actions (action_group, entries, G_N_ELEMENTS (entries),
				window);
  gtk_action_group_add_toggle_actions (action_group, toggle_entries,
				       G_N_ELEMENTS (toggle_entries), window);
  gtk_action_group_add_radio_actions (action_group, color_entries,
				      G_N_ELEMENTS (color_entries), 0,
				      NULL, window);
  gtk_action_group_add_radio_actions (action_group, zoom_entries,
				      G_N_ELEMENTS (zoom_entries), 0,
				      NULL, window);
  gtk_action_group_add_radio_actions (action_group, mapping_entries,
				      G_N_ELEMENTS (mapping_entries), 0,
				      G_CALLBACK (set_func), window);

  ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

  accel_group = gtk_ui_manager_get_accel_group (ui_manager);
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  error = NULL;
  if (!gtk_ui_manager_add_ui_from_string (ui_manager, ui_description, -1,
					  &error))
    {
      g_message ("building menus failed: %s", error->message);
      g_error_free (error);
      /* exit (EXIT_FAILURE); */
    }

  menubar = gtk_ui_manager_get_widget (ui_manager, "/MainMenu");
  gtk_widget_show (menubar);
  gtk_box_pack_start (GTK_BOX (main_vbox), menubar, FALSE, FALSE, 0);

  paste_buffer_add_add_cb(paste_buffer_added, NULL);
  paste_buffer_add_remove_cb(paste_buffer_removed, NULL);

  set_sensitive ("/MainMenu/EditMenu/Paste", FALSE);
  menu_shapes_selected (0);

  return &_menu;
}

void
menu_build_mru_items(MRU_t *mru)
{
   GList *p;
   gint position = 0;
   int i;

  return;

   if (_menu.nr_off_mru_items) {
      GList *children;

      children = gtk_container_get_children(GTK_CONTAINER(_menu.open_recent));
      p = g_list_nth(children, position);
      for (i = 0; i < _menu.nr_off_mru_items; i++, p = p->next) {
	 gtk_widget_destroy((GtkWidget*) p->data);
      }
      g_list_free(children);
   }

   i = 0;
   for (p = mru->list; p; p = p->next, i++) {
      GtkWidget *item = insert_item_with_label(_menu.open_recent, position++,
					       (gchar*) p->data,
					       menu_mru, p->data);
      if (i < 9) {
	 guchar accelerator_key = '1' + i;
	 add_accelerator(item, accelerator_key, GDK_CONTROL_MASK);
      }
   }
   _menu.nr_off_mru_items = i;
}

void
do_main_popup_menu(GdkEventButton *event)
{
  GtkWidget *popup = gtk_ui_manager_get_widget (ui_manager, "/PopupMenu");
  gtk_menu_popup (GTK_MENU (popup), NULL, NULL, NULL, NULL,
		  event->button, event->time);
}

void
menu_check_grid(gboolean check)
{
  GtkAction *action = gtk_ui_manager_get_action (ui_manager,
						 "/MainMenu/ToolsMenu/Grid");
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), check);
}

void
menu_set_zoom(gint factor)
{
  menu_set_zoom_sensitivity (factor);
}

GtkWidget*
make_toolbar(GtkWidget *main_vbox, GtkWidget *window)
{
  GtkWidget *handlebox, *toolbar;

  handlebox = gtk_handle_box_new ();
  gtk_box_pack_start (GTK_BOX (main_vbox), handlebox, FALSE, FALSE, 0);
  toolbar = gtk_ui_manager_get_widget (ui_manager, "/Toolbar");

  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);
  gtk_container_set_border_width (GTK_CONTAINER (toolbar), 0);

  gtk_container_add (GTK_CONTAINER (handlebox), toolbar);
  gtk_widget_show (toolbar);
  gtk_widget_show (handlebox);

  return handlebox;
}

GtkWidget*
make_tools(GtkWidget *window)
{
  GtkWidget *handlebox, *toolbar;

  handlebox = gtk_handle_box_new ();
  toolbar = gtk_ui_manager_get_widget (ui_manager, "/Tools");
  gtk_toolbar_set_orientation (GTK_TOOLBAR (toolbar),
			       GTK_ORIENTATION_VERTICAL);

  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);
  gtk_container_set_border_width (GTK_CONTAINER (toolbar), 0);

  gtk_container_add (GTK_CONTAINER (handlebox), toolbar);
  gtk_widget_show (toolbar);
  gtk_widget_show (handlebox);

  return handlebox;
}

GtkWidget*
make_selection_toolbar(void)
{
  GtkWidget *toolbar;

  toolbar = gtk_ui_manager_get_widget (ui_manager, "/Selection");

  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
  gtk_toolbar_set_orientation(GTK_TOOLBAR(toolbar), GTK_ORIENTATION_VERTICAL);
  gtk_container_set_border_width(GTK_CONTAINER(toolbar), 0);

  gtk_widget_show (toolbar);
  return toolbar;
}
