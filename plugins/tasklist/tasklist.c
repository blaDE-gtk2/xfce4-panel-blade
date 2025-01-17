/*
 * Copyright (C) 2008-2010 Nick Schermer <nick@xfce.org>
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

#include <blxo/blxo.h>
#include <libbladeui/libbladeui.h>
#include <common/bar-blconf.h>
#include <common/bar-utils.h>
#include <common/bar-private.h>
#include <libbladebar/libbladebar.h>

#include "tasklist-widget.h"
#include "tasklist-dialog_ui.h"

/* TODO move to header */
GType tasklist_plugin_get_type (void) G_GNUC_CONST;
void tasklist_plugin_register_type (BladeBarTypeModule *type_module);
#define XFCE_TYPE_TASKLIST_PLUGIN            (tasklist_plugin_get_type ())
#define XFCE_TASKLIST_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XFCE_TYPE_TASKLIST_PLUGIN, TasklistPlugin))
#define XFCE_TASKLIST_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), XFCE_TYPE_TASKLIST_PLUGIN, TasklistPluginClass))
#define XFCE_IS_TASKLIST_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XFCE_TYPE_TASKLIST_PLUGIN))
#define XFCE_IS_TASKLIST_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), XFCE_TYPE_TASKLIST_PLUGIN))
#define XFCE_TASKLIST_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), XFCE_TYPE_TASKLIST_PLUGIN, TasklistPluginClass))


typedef struct _TasklistPluginClass TasklistPluginClass;
struct _TasklistPluginClass
{
  BladeBarPluginClass __parent__;
};

typedef struct _TasklistPlugin TasklistPlugin;
struct _TasklistPlugin
{
  BladeBarPlugin __parent__;

  /* the tasklist widget */
  GtkWidget     *tasklist;
  GtkWidget     *handle;
};



static void     tasklist_plugin_construct               (BladeBarPlugin    *bar_plugin);
static void     tasklist_plugin_mode_changed            (BladeBarPlugin    *bar_plugin,
                                                         BladeBarPluginMode mode);
static gboolean tasklist_plugin_size_changed            (BladeBarPlugin    *bar_plugin,
                                                         gint                size);
static void     tasklist_plugin_nrows_changed           (BladeBarPlugin    *bar_plugin,
                                                         guint               nrows);
static void     tasklist_plugin_screen_position_changed (BladeBarPlugin    *bar_plugin,
                                                         XfceScreenPosition  position);
static void     tasklist_plugin_configure_plugin        (BladeBarPlugin    *bar_plugin);
static gboolean tasklist_plugin_handle_expose_event     (GtkWidget          *widget,
                                                         GdkEventExpose     *event,
                                                         TasklistPlugin     *plugin);



/* define and register the plugin */
BLADE_BAR_DEFINE_PLUGIN_RESIDENT (TasklistPlugin, tasklist_plugin)



static void
tasklist_plugin_class_init (TasklistPluginClass *klass)
{
  BladeBarPluginClass *plugin_class;

  plugin_class = BLADE_BAR_PLUGIN_CLASS (klass);
  plugin_class->construct = tasklist_plugin_construct;
  plugin_class->mode_changed = tasklist_plugin_mode_changed;
  plugin_class->size_changed = tasklist_plugin_size_changed;
  plugin_class->nrows_changed = tasklist_plugin_nrows_changed;
  plugin_class->screen_position_changed = tasklist_plugin_screen_position_changed;
  plugin_class->configure_plugin = tasklist_plugin_configure_plugin;
}



static void
tasklist_plugin_init (TasklistPlugin *plugin)
{
  GtkWidget *box;

  /* create widgets */
  box = xfce_hvbox_new (GTK_ORIENTATION_HORIZONTAL, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (plugin), box);
  blxo_binding_new (G_OBJECT (plugin), "orientation", G_OBJECT (box), "orientation");
  gtk_widget_show (box);

  plugin->handle = gtk_alignment_new (0.00, 0.00, 0.00, 0.00);
  gtk_box_pack_start (GTK_BOX (box), plugin->handle, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (plugin->handle), "expose-event",
      G_CALLBACK (tasklist_plugin_handle_expose_event), plugin);
  gtk_widget_set_size_request (plugin->handle, 8, 8);
  gtk_widget_show (plugin->handle);

  plugin->tasklist = g_object_new (XFCE_TYPE_TASKLIST, NULL);
  gtk_box_pack_start (GTK_BOX (box), plugin->tasklist, TRUE, TRUE, 0);

  blxo_binding_new (G_OBJECT (plugin->tasklist), "show-handle",
                   G_OBJECT (plugin->handle), "visible");
}



static void
tasklist_plugin_construct (BladeBarPlugin *bar_plugin)
{
  TasklistPlugin      *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);
  const BarProperty  properties[] =
  {
    { "show-labels", G_TYPE_BOOLEAN },
    { "grouping", G_TYPE_UINT },
    { "include-all-workspaces", G_TYPE_BOOLEAN },
    { "include-all-monitors", G_TYPE_BOOLEAN },
    { "flat-buttons", G_TYPE_BOOLEAN },
    { "switch-workspace-on-unminimize", G_TYPE_BOOLEAN },
    { "show-only-minimized", G_TYPE_BOOLEAN },
    { "show-wireframes", G_TYPE_BOOLEAN },
    { "show-handle", G_TYPE_BOOLEAN },
    { "sort-order", G_TYPE_UINT },
    { "window-scrolling", G_TYPE_BOOLEAN },
    { "wrap-windows", G_TYPE_BOOLEAN },
    { "include-all-blinking", G_TYPE_BOOLEAN },
    { "middle-click", G_TYPE_UINT },
    { NULL }
  };

  /* show configure */
  blade_bar_plugin_menu_show_configure (BLADE_BAR_PLUGIN (plugin));

  /* expand the plugin */
  /* blade_bar_plugin_set_expand (bar_plugin, FALSE); */
  blade_bar_plugin_set_shrink (bar_plugin, TRUE);

  /* bind all properties */
  bar_properties_bind (NULL, G_OBJECT (plugin->tasklist),
                         blade_bar_plugin_get_property_base (bar_plugin),
                         properties, FALSE);

  /* show the tasklist */
  gtk_widget_show (plugin->tasklist);
}



static void
tasklist_plugin_mode_changed (BladeBarPlugin     *bar_plugin,
                              BladeBarPluginMode  mode)
{
  TasklistPlugin *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);

  /* set the new tasklist mode */
  xfce_tasklist_set_mode (XFCE_TASKLIST (plugin->tasklist), mode);
}



static gboolean
tasklist_plugin_size_changed (BladeBarPlugin *bar_plugin,
                              gint             size)
{
  TasklistPlugin *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);

  /* set the tasklist size */
  xfce_tasklist_set_size (XFCE_TASKLIST (plugin->tasklist), size);

  return TRUE;
}



static void
tasklist_plugin_nrows_changed (BladeBarPlugin *bar_plugin,
                               guint            nrows)
{
  TasklistPlugin *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);

  /* set the tasklist nrows */
  xfce_tasklist_set_nrows (XFCE_TASKLIST (plugin->tasklist), nrows);
}



static void
tasklist_plugin_screen_position_changed (BladeBarPlugin    *bar_plugin,
                                         XfceScreenPosition  position)
{
  TasklistPlugin *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);

  /* update monitor geometry; this function is also triggered when
   * the bar is moved to another monitor during runtime */
  xfce_tasklist_update_monitor_geometry (XFCE_TASKLIST (plugin->tasklist));
}



static void
tasklist_plugin_configure_plugin (BladeBarPlugin *bar_plugin)
{
  TasklistPlugin *plugin = XFCE_TASKLIST_PLUGIN (bar_plugin);
  GtkBuilder     *builder;
  GObject        *dialog;
  GObject        *object;
  GtkTreeIter     iter;

  /* setup the dialog */
  BAR_UTILS_LINK_4UI
  builder = bar_utils_builder_new (bar_plugin, tasklist_dialog_ui,
                                     tasklist_dialog_ui_length, &dialog);
  if (G_UNLIKELY (builder == NULL))
    return;

#define TASKLIST_DIALOG_BIND(name, property) \
  object = gtk_builder_get_object (builder, (name)); \
  bar_return_if_fail (G_IS_OBJECT (object)); \
  blxo_mutual_binding_new (G_OBJECT (plugin->tasklist), (name), \
                          G_OBJECT (object), (property));

#define TASKLIST_DIALOG_BIND_INV(name, property) \
  object = gtk_builder_get_object (builder, (name)); \
  bar_return_if_fail (G_IS_OBJECT (object)); \
  blxo_mutual_binding_new_with_negation (G_OBJECT (plugin->tasklist), \
                                        name,  G_OBJECT (object), \
                                        property);

  TASKLIST_DIALOG_BIND ("show-labels", "active")
  TASKLIST_DIALOG_BIND ("grouping", "active")
  TASKLIST_DIALOG_BIND ("include-all-workspaces", "active")
  TASKLIST_DIALOG_BIND ("include-all-monitors", "active")
  TASKLIST_DIALOG_BIND ("flat-buttons", "active")
  TASKLIST_DIALOG_BIND_INV ("switch-workspace-on-unminimize", "active")
  TASKLIST_DIALOG_BIND ("show-only-minimized", "active")
  TASKLIST_DIALOG_BIND ("show-wireframes", "active")
  TASKLIST_DIALOG_BIND ("show-handle", "active")
  TASKLIST_DIALOG_BIND ("sort-order", "active")
  TASKLIST_DIALOG_BIND ("window-scrolling", "active")
  TASKLIST_DIALOG_BIND ("middle-click", "active")

#ifndef GDK_WINDOWING_X11
  /* not functional in x11, so avoid confusion */
  object = gtk_builder_get_object (builder, "show-wireframes");
  gtk_widget_hide (GTK_WIDGET (object));
#endif

  /* TODO: remove this if always group is supported */
  object = gtk_builder_get_object (builder, "grouping-model");
  if (gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (object), &iter, NULL, 2))
    gtk_list_store_remove (GTK_LIST_STORE (object), &iter);

  gtk_widget_show (GTK_WIDGET (dialog));
}



static gboolean
tasklist_plugin_handle_expose_event (GtkWidget *widget,
                                     GdkEventExpose *event,
                                     TasklistPlugin *plugin)
{
  GtkOrientation orientation;

  bar_return_val_if_fail (XFCE_IS_TASKLIST_PLUGIN (plugin), FALSE);
  bar_return_val_if_fail (plugin->handle == widget, FALSE);

  if (!GTK_WIDGET_DRAWABLE (widget))
    return FALSE;

  /* get the orientation */
  if (blade_bar_plugin_get_orientation (BLADE_BAR_PLUGIN (plugin)) ==
      GTK_ORIENTATION_HORIZONTAL)
    orientation = GTK_ORIENTATION_VERTICAL;
  else
    orientation = GTK_ORIENTATION_HORIZONTAL;

  /* paint the handle */
  gtk_paint_handle (widget->style, widget->window,
                    GTK_WIDGET_STATE (widget), GTK_SHADOW_NONE,
                    &(event->area), widget, "handlebox",
                    widget->allocation.x,
                    widget->allocation.y,
                    widget->allocation.width,
                    widget->allocation.height,
                    orientation);

  return TRUE;
}
