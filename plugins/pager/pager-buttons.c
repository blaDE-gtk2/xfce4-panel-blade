/*
 * Copyright (C) 2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <blxo/blxo.h>
#include <libbladebar/libbladebar.h>
#include <common/bar-private.h>

#include "pager-buttons.h"



static void pager_buttons_get_property               (GObject       *object,
                                                      guint          prop_id,
                                                      GValue        *value,
                                                      GParamSpec    *pspec);
static void pager_buttons_set_property               (GObject       *object,
                                                      guint          prop_id,
                                                      const GValue  *value,
                                                      GParamSpec    *pspec);
static void pager_buttons_finalize                   (GObject       *object);
static void pager_buttons_queue_rebuild              (PagerButtons  *pager);
static void pager_buttons_screen_workspace_changed   (WnckScreen    *screen,
                                                      WnckWorkspace *previous_workspace,
                                                      PagerButtons  *pager);
static void pager_buttons_screen_workspace_created   (WnckScreen    *screen,
                                                      WnckWorkspace *created_workspace,
                                                      PagerButtons  *pager);
static void pager_buttons_screen_workspace_destroyed (WnckScreen    *screen,
                                                      WnckWorkspace *destroyed_workspace,
                                                      PagerButtons  *pager);
static void pager_buttons_screen_viewports_changed   (WnckScreen    *screen,
                                                      PagerButtons  *pager);
static void pager_buttons_workspace_button_toggled   (GtkWidget     *button,
                                                      WnckWorkspace *workspace);
static void pager_buttons_workspace_button_label     (WnckWorkspace *workspace,
                                                      GtkWidget     *label);
static void pager_buttons_viewport_button_toggled    (GtkWidget     *button,
                                                      PagerButtons  *pager);



struct _PagerButtonsClass
{
  GtkTableClass __parent__;
};

struct _PagerButtons
{
  GtkTable __parent__;

  GSList         *buttons;

  guint           rebuild_id;

  WnckScreen     *wnck_screen;

  gint            rows;
  GtkOrientation  orientation;
};

enum
{
  PROP_0,
  PROP_SCREEN,
  PROP_ROWS,
  PROP_ORIENTATION
};

enum
{
  VIEWPORT_X,
  VIEWPORT_Y,
  N_INFOS
};



BLADE_BAR_DEFINE_TYPE (PagerButtons, pager_buttons, GTK_TYPE_TABLE)



static void
pager_buttons_class_init (PagerButtonsClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = pager_buttons_get_property;
  gobject_class->set_property = pager_buttons_set_property;
  gobject_class->finalize = pager_buttons_finalize;

  g_object_class_install_property (gobject_class,
                                   PROP_SCREEN,
                                   g_param_spec_object ("screen",
                                                         NULL, NULL,
                                                         WNCK_TYPE_SCREEN,
                                                         BLXO_PARAM_WRITABLE
                                                         | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class,
                                   PROP_ROWS,
                                   g_param_spec_int ("rows",
                                                     NULL, NULL,
                                                     1, 100, 1,
                                                     BLXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_ORIENTATION,
                                   g_param_spec_enum ("orientation",
                                                     NULL, NULL,
                                                     GTK_TYPE_ORIENTATION,
                                                     GTK_ORIENTATION_HORIZONTAL,
                                                     BLXO_PARAM_READWRITE));
}



static void
pager_buttons_init (PagerButtons *pager)
{
  pager->rows = 1;
  pager->wnck_screen = NULL;
  pager->orientation = GTK_ORIENTATION_HORIZONTAL;
  pager->buttons = NULL;
  pager->rebuild_id = 0;

  /* although I'd prefer normal allocation, the homogeneous setting
   * takes care of small bars, while non-homogeneous tables allocate
   * outside the bar size --nick */
  gtk_table_set_homogeneous (GTK_TABLE (pager), TRUE);
}



static void
pager_buttons_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PagerButtons *pager = XFCE_PAGER_BUTTONS (object);

  switch (prop_id)
    {
    case PROP_ROWS:
      g_value_set_int (value, pager->rows);
      break;

    case PROP_ORIENTATION:
      g_value_set_enum (value, pager->orientation);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
pager_buttons_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PagerButtons *pager = XFCE_PAGER_BUTTONS (object);

  switch (prop_id)
    {
    case PROP_SCREEN:
      pager->wnck_screen = g_value_dup_object (value);
      bar_return_if_fail (WNCK_IS_SCREEN (pager->wnck_screen));

      g_signal_connect (G_OBJECT (pager->wnck_screen), "active-workspace-changed",
          G_CALLBACK (pager_buttons_screen_workspace_changed), pager);
      g_signal_connect (G_OBJECT (pager->wnck_screen), "workspace-created",
          G_CALLBACK (pager_buttons_screen_workspace_created), pager);
      g_signal_connect (G_OBJECT (pager->wnck_screen), "workspace-destroyed",
          G_CALLBACK (pager_buttons_screen_workspace_destroyed), pager);
      g_signal_connect (G_OBJECT (pager->wnck_screen), "viewports-changed",
          G_CALLBACK (pager_buttons_screen_viewports_changed), pager);

      pager_buttons_queue_rebuild (pager);
      break;

    case PROP_ROWS:
      pager_buttons_set_n_rows (pager, g_value_get_int (value));
      break;

    case PROP_ORIENTATION:
      pager_buttons_set_orientation (pager, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
pager_buttons_finalize (GObject *object)
{
  PagerButtons *pager = XFCE_PAGER_BUTTONS (object);

  if (pager->rebuild_id != 0)
    g_source_remove (pager->rebuild_id);

  if (G_LIKELY (pager->wnck_screen != NULL))
    {
      g_signal_handlers_disconnect_by_func (G_OBJECT (pager->wnck_screen),
          G_CALLBACK (pager_buttons_screen_workspace_changed), pager);
      g_signal_handlers_disconnect_by_func (G_OBJECT (pager->wnck_screen),
          G_CALLBACK (pager_buttons_screen_workspace_created), pager);
      g_signal_handlers_disconnect_by_func (G_OBJECT (pager->wnck_screen),
          G_CALLBACK (pager_buttons_screen_workspace_destroyed), pager);
      g_signal_handlers_disconnect_by_func (G_OBJECT (pager->wnck_screen),
          G_CALLBACK (pager_buttons_screen_viewports_changed), pager);

      g_object_unref (G_OBJECT (pager->wnck_screen));
    }

  g_slist_free (pager->buttons);

  (*G_OBJECT_CLASS (pager_buttons_parent_class)->finalize) (object);
}



static gboolean
pager_buttons_button_press_event (GtkWidget      *button,
                                  GdkEventButton *event)
{
  guint modifiers;

  bar_return_val_if_fail (GTK_IS_TOGGLE_BUTTON (button), FALSE);

  modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

  /* block toggle events on an active button */
  if (event->button == 1
      && modifiers != GDK_CONTROL_MASK
      && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    return TRUE;

  return FALSE;
}



static gboolean
pager_buttons_rebuild_idle (gpointer user_data)
{
  PagerButtons  *pager = XFCE_PAGER_BUTTONS (user_data);
  GList         *li, *workspaces;
  WnckWorkspace *active_ws;
  gint           n, n_workspaces;
  gint           rows, cols;
  gint           row, col;
  GtkWidget     *button;
  WnckWorkspace *workspace = NULL;
  GtkWidget     *bar_plugin;
  GtkWidget     *label;
  gint           workspace_width, workspace_height = 0;
  gint           screen_width = 0, screen_height = 0;
  gint           viewport_x, viewport_y;
  gboolean       viewport_mode = FALSE;
  gint           n_viewports = 0;
  gint          *vp_info;
  gchar          text[8];

  bar_return_val_if_fail (XFCE_IS_PAGER_BUTTONS (pager), FALSE);
  bar_return_val_if_fail (WNCK_IS_SCREEN (pager->wnck_screen), FALSE);

  GDK_THREADS_ENTER ();

  gtk_container_foreach (GTK_CONTAINER (pager),
      (GtkCallback) gtk_widget_destroy, NULL);

  g_slist_free (pager->buttons);
  pager->buttons = NULL;

  active_ws = wnck_screen_get_active_workspace (pager->wnck_screen);
  workspaces = wnck_screen_get_workspaces (pager->wnck_screen);
  if (workspaces == NULL)
    goto leave;

  n_workspaces = g_list_length (workspaces);

  /* check if the user uses 1 workspace with viewports */
  if (G_UNLIKELY (n_workspaces == 1
      && wnck_workspace_is_virtual (WNCK_WORKSPACE (workspaces->data))))
    {
      workspace = WNCK_WORKSPACE (workspaces->data);

      workspace_width = wnck_workspace_get_width (workspace);
      workspace_height = wnck_workspace_get_height (workspace);
      screen_width = wnck_screen_get_width (pager->wnck_screen);
      screen_height = wnck_screen_get_height (pager->wnck_screen);

      /* we only support viewports that are equally spread */
      if ((workspace_width % screen_width) == 0
          && (workspace_height % screen_height) == 0)
        {
          n_viewports = (workspace_width / screen_width) * (workspace_height / screen_height);

          rows = CLAMP (1, pager->rows, n_viewports);
          cols = n_workspaces / rows;
          if (cols * rows < n_workspaces)
            cols++;

          viewport_mode = TRUE;
        }
      else
        {
          g_warning ("only viewports with equally distributed screens are supported: %dx%d & %dx%d",
                     workspace_width, workspace_height, screen_width, screen_height);

          goto workspace_layout;
        }
    }
  else
    {
      workspace_layout:

      rows = CLAMP (1, pager->rows, n_workspaces);
      cols = n_workspaces / rows;
      if (cols * rows < n_workspaces)
        cols++;
    }

  if (pager->orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_table_resize (GTK_TABLE (pager), rows, cols);
  else
    gtk_table_resize (GTK_TABLE (pager), cols, rows);


  bar_plugin = gtk_widget_get_ancestor (GTK_WIDGET (pager), XFCE_TYPE_BAR_PLUGIN);

  if (G_UNLIKELY (viewport_mode))
    {
      bar_return_val_if_fail (WNCK_IS_WORKSPACE (workspace), FALSE);

      viewport_x = wnck_workspace_get_viewport_x (workspace);
      viewport_y = wnck_workspace_get_viewport_y (workspace);

      for (n = 0; n < n_viewports; n++)
        {
          vp_info = g_new0 (gint, N_INFOS);
          vp_info[VIEWPORT_X] = (n % (workspace_height / screen_height)) * screen_width;
          vp_info[VIEWPORT_Y] = (n / (workspace_height / screen_height)) * screen_height;

          button = blade_bar_create_toggle_button ();
          if (viewport_x >= vp_info[VIEWPORT_X] && viewport_x < vp_info[VIEWPORT_X] + screen_width
              && viewport_y >= vp_info[VIEWPORT_Y] && viewport_y < vp_info[VIEWPORT_Y] + screen_height)
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
          g_signal_connect (G_OBJECT (button), "toggled",
              G_CALLBACK (pager_buttons_viewport_button_toggled), pager);
          g_signal_connect (G_OBJECT (button), "button-press-event",
              G_CALLBACK (pager_buttons_button_press_event), NULL);
          blade_bar_plugin_add_action_widget (BLADE_BAR_PLUGIN (bar_plugin), button);
          gtk_widget_show (button);

          g_object_set_data_full (G_OBJECT (button), "viewport-info", vp_info,
                                  (GDestroyNotify) g_free);

          g_snprintf (text, sizeof (text), "%d", n + 1);
          label = gtk_label_new (text);
          gtk_label_set_angle (GTK_LABEL (label),
              pager->orientation == GTK_ORIENTATION_HORIZONTAL ? 0 : 270);
          gtk_container_add (GTK_CONTAINER (button), label);
          gtk_widget_show (label);

          if (pager->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
              row = n % cols;
              col = n / cols;
            }
          else
            {
              row = n / cols;
              col = n % cols;
            }


          gtk_table_attach (GTK_TABLE (pager), button,
                            row, row + 1, col, col + 1,
                            GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
                            0, 0);
        }
    }
  else
    {
      for (li = workspaces, n = 0; li != NULL; li = li->next, n++)
        {
          workspace = WNCK_WORKSPACE (li->data);

          button = blade_bar_create_toggle_button ();
          if (workspace == active_ws)
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
          g_signal_connect (G_OBJECT (button), "toggled",
              G_CALLBACK (pager_buttons_workspace_button_toggled), workspace);
          g_signal_connect (G_OBJECT (button), "button-press-event",
              G_CALLBACK (pager_buttons_button_press_event), NULL);
          blade_bar_plugin_add_action_widget (BLADE_BAR_PLUGIN (bar_plugin), button);
          gtk_widget_show (button);

          label = gtk_label_new (NULL);
          g_signal_connect_object (G_OBJECT (workspace), "name-changed",
              G_CALLBACK (pager_buttons_workspace_button_label), label, 0);
          pager_buttons_workspace_button_label (workspace, label);
          gtk_label_set_angle (GTK_LABEL (label),
              pager->orientation == GTK_ORIENTATION_HORIZONTAL ? 0 : 270);
          gtk_container_add (GTK_CONTAINER (button), label);
          gtk_widget_show (label);

          pager->buttons = g_slist_prepend (pager->buttons, button);

          if (pager->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
              row = n % cols;
              col = n / cols;
            }
          else
            {
              row = n / cols;
              col = n % cols;
            }

          gtk_table_attach (GTK_TABLE (pager), button,
                            row, row + 1, col, col + 1,
                            GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
                            0, 0);
        }
    }

  pager->buttons = g_slist_reverse (pager->buttons);

  leave:

  GDK_THREADS_LEAVE ();

  return FALSE;
}



static void
pager_buttons_rebuild_idle_destroyed (gpointer user_data)
{
  XFCE_PAGER_BUTTONS (user_data)->rebuild_id = 0;
}



static void
pager_buttons_queue_rebuild (PagerButtons *pager)
{
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));

  if (pager->rebuild_id == 0)
    {
      pager->rebuild_id = g_idle_add_full (G_PRIORITY_LOW, pager_buttons_rebuild_idle,
                                           pager, pager_buttons_rebuild_idle_destroyed);
    }
}



static void
pager_buttons_screen_workspace_changed (WnckScreen    *screen,
                                        WnckWorkspace *previous_workspace,
                                        PagerButtons  *pager)
{
  gint           active = -1, n;
  WnckWorkspace *active_ws;
  GSList        *li;

  bar_return_if_fail (WNCK_IS_SCREEN (screen));
  bar_return_if_fail (previous_workspace == NULL || WNCK_IS_WORKSPACE (previous_workspace));
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));
  bar_return_if_fail (pager->wnck_screen == screen);

  active_ws = wnck_screen_get_active_workspace (screen);
  if (G_LIKELY (active_ws != NULL))
    active = wnck_workspace_get_number (active_ws);

  for (li = pager->buttons, n = 0; li != NULL; li = li->next, n++)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (li->data), n == active);
}



static void
pager_buttons_screen_workspace_created (WnckScreen    *screen,
                                        WnckWorkspace *created_workspace,
                                        PagerButtons  *pager)
{
  bar_return_if_fail (WNCK_IS_SCREEN (screen));
  bar_return_if_fail (WNCK_IS_WORKSPACE (created_workspace));
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));
  bar_return_if_fail (pager->wnck_screen == screen);

  pager_buttons_queue_rebuild (pager);
}



static void
pager_buttons_screen_workspace_destroyed (WnckScreen    *screen,
                                          WnckWorkspace *destroyed_workspace,
                                          PagerButtons  *pager)
{
  bar_return_if_fail (WNCK_IS_SCREEN (screen));
  bar_return_if_fail (WNCK_IS_WORKSPACE (destroyed_workspace));
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));
  bar_return_if_fail (pager->wnck_screen == screen);

  pager_buttons_queue_rebuild (pager);
}



static void
pager_buttons_screen_viewports_changed (WnckScreen    *screen,
                                        PagerButtons  *pager)
{
  bar_return_if_fail (WNCK_IS_SCREEN (screen));
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));
  bar_return_if_fail (pager->wnck_screen == screen);

  /* yes we are extremely lazy here, but this event is
   * also emitted when the viewport setup changes... */
  if (pager->buttons == NULL)
    pager_buttons_queue_rebuild (pager);
}



static void
pager_buttons_workspace_button_label (WnckWorkspace *workspace,
                                      GtkWidget     *label)
{
  const gchar *name;
  gchar       *utf8 = NULL, *name_num = NULL;

  bar_return_if_fail (WNCK_IS_WORKSPACE (workspace));
  bar_return_if_fail (GTK_IS_LABEL (label));

  /* try to get an utf-8 valid name */
  name = wnck_workspace_get_name (workspace);
  if (!blxo_str_is_empty (name)
      && !g_utf8_validate (name, -1, NULL))
    name = utf8 = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);

  if (blxo_str_is_empty (name))
    name = name_num = g_strdup_printf (_("Workspace %d"),
        wnck_workspace_get_number (workspace) + 1);

  gtk_label_set_text (GTK_LABEL (label), name);

  g_free (utf8);
  g_free (name_num);
}



static void
pager_buttons_workspace_button_toggled (GtkWidget     *button,
                                        WnckWorkspace *workspace)
{
  WnckWorkspace *active_ws;

  bar_return_if_fail (GTK_IS_TOGGLE_BUTTON (button));
  bar_return_if_fail (WNCK_IS_WORKSPACE (workspace));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
      active_ws = wnck_screen_get_active_workspace (wnck_workspace_get_screen (workspace));
      if (active_ws != workspace)
        wnck_workspace_activate (workspace, gtk_get_current_event_time ());
    }
}



static void
pager_buttons_viewport_button_toggled (GtkWidget    *button,
                                       PagerButtons *pager)
{
  gint *vp_info;

  bar_return_if_fail (GTK_IS_TOGGLE_BUTTON (button));
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));
  bar_return_if_fail (WNCK_IS_SCREEN (pager->wnck_screen));

  vp_info = g_object_get_data (G_OBJECT (button), "viewport-info");
  if (G_UNLIKELY (vp_info == NULL))
    return;

  wnck_screen_move_viewport (pager->wnck_screen,
                             vp_info[VIEWPORT_X],
                             vp_info[VIEWPORT_Y]);
}



GtkWidget *
pager_buttons_new (WnckScreen *screen)
{
  bar_return_val_if_fail (WNCK_IS_SCREEN (screen), NULL);

  return g_object_new (XFCE_TYPE_PAGER_BUTTONS,
                       "screen", screen, NULL);
}



void
pager_buttons_set_orientation (PagerButtons   *pager,
                               GtkOrientation  orientation)
{
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));

  if (pager->orientation == orientation)
   return;

  pager->orientation = orientation;
  pager_buttons_queue_rebuild (pager);
}



void
pager_buttons_set_n_rows (PagerButtons *pager,
                          gint          rows)
{
  bar_return_if_fail (XFCE_IS_PAGER_BUTTONS (pager));

  if (pager->rows == rows)
   return;

  pager->rows = rows;
  pager_buttons_queue_rebuild (pager);
}
