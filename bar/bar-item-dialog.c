/*
 * Copyright (C) 2008-2010 Nick Schermer <nick@xfce.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <blxo/blxo.h>
#include <libbladeui/libbladeui.h>
#include <libbladeutil/libbladeutil.h>

#include <common/bar-private.h>
#include <common/bar-utils.h>
#include <libbladebar/libbladebar.h>

#include <bar/bar-application.h>
#include <bar/bar-item-dialog.h>
#include <bar/bar-dialogs.h>
#include <bar/bar-module.h>
#include <bar/bar-module-factory.h>
#include <bar/bar-preferences-dialog.h>



#define BORDER         (6)
#define ITEMS_HELP_URL "http://www.xfce.org"



static void         bar_item_dialog_finalize               (GObject            *object);
static void         bar_item_dialog_response               (GtkDialog          *dialog,
                                                              gint                response_id);
static void         bar_item_dialog_unique_changed         (BarModuleFactory *factory,
                                                              BarModule        *module,
                                                              BarItemDialog    *dialog);
static gboolean     bar_item_dialog_unique_changed_foreach (GtkTreeModel       *model,
                                                              GtkTreePath        *path,
                                                              GtkTreeIter        *iter,
                                                              gpointer            user_data);
static gboolean     bar_item_dialog_separator_func         (GtkTreeModel       *model,
                                                              GtkTreeIter        *iter,
                                                              gpointer            user_data);
static void         bar_item_dialog_selection_changed      (GtkTreeSelection   *selection,
                                                              BarItemDialog    *dialog);
static BarModule *bar_item_dialog_get_selected_module    (GtkTreeView        *treeview);
static void         bar_item_dialog_drag_begin             (GtkWidget          *treeview,
                                                              GdkDragContext     *context,
                                                              BarItemDialog    *dialog);
static void         bar_item_dialog_drag_data_get          (GtkWidget          *treeview,
                                                              GdkDragContext     *context,
                                                              GtkSelectionData   *selection_data,
                                                              guint               info,
                                                              guint               drag_time,
                                                              BarItemDialog    *dialog);
static void         bar_item_dialog_drag_data_received     (GtkWidget          *treeview,
                                                              GdkDragContext     *context,
                                                              gint                x,
                                                              gint                y,
                                                              GtkSelectionData   *selection_data,
                                                              guint               info,
                                                              guint               drag_time,
                                                              BarItemDialog    *dialog);
static void         bar_item_dialog_populate_store         (BarItemDialog    *dialog);
static gint         bar_item_dialog_compare_func           (GtkTreeModel       *model,
                                                              GtkTreeIter        *a,
                                                              GtkTreeIter        *b,
                                                              gpointer            user_data);
static gboolean     bar_item_dialog_visible_func           (GtkTreeModel       *model,
                                                              GtkTreeIter        *iter,
                                                              gpointer            user_data);
static void         bar_item_dialog_text_renderer          (GtkTreeViewColumn  *column,
                                                              GtkCellRenderer    *renderer,
                                                              GtkTreeModel       *model,
                                                              GtkTreeIter        *iter,
                                                              gpointer            user_data);



struct _BarItemDialogClass
{
  XfceTitledDialogClass __parent__;
};

struct _BarItemDialog
{
  XfceTitledDialog  __parent__;

  BarApplication   *application;

  BarModuleFactory *factory;

  BarWindow        *active;

  /* pointers to list */
  GtkListStore       *store;
  GtkTreeView        *treeview;
  GtkWidget          *add_button;
};

enum
{
  COLUMN_ICON_NAME,
  COLUMN_MODULE,
  COLUMN_SENSITIVE,
  N_COLUMNS
};

static const GtkTargetEntry drag_targets[] =
{
  { "blade-bar/plugin-name", 0, 0 }
};

static const GtkTargetEntry drop_targets[] =
{
  { "blade-bar/plugin-widget", GTK_TARGET_SAME_APP, 0 }
};



G_DEFINE_TYPE (BarItemDialog, bar_item_dialog, XFCE_TYPE_TITLED_DIALOG)



static BarItemDialog *dialog_singleton = NULL;



static void
bar_item_dialog_class_init (BarItemDialogClass *klass)
{
  GObjectClass   *gobject_class;
  GtkDialogClass *gtkdialog_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = bar_item_dialog_finalize;

  gtkdialog_class = GTK_DIALOG_CLASS (klass);
  gtkdialog_class->response = bar_item_dialog_response;
}



static void
bar_item_dialog_init (BarItemDialog *dialog)
{
  GtkWidget         *main_vbox;
  GtkWidget         *hbox;
  GtkWidget         *label;
  GtkWidget         *entry;
  GtkWidget         *scroll;
  GtkWidget         *treeview;
  GtkTreeModel      *filter;
  GtkTreeViewColumn *column;
  GtkCellRenderer   *renderer;
  GtkTreeSelection  *selection;

  dialog->application = bar_application_get ();

  /* register the window in the application */
  bar_application_take_dialog (dialog->application, GTK_WINDOW (dialog));

  /* make the application windows insensitive */
  bar_application_windows_blocked (dialog->application, TRUE);

  dialog->factory = bar_module_factory_get ();

  /* monitor unique changes */
  g_signal_connect (G_OBJECT (dialog->factory), "unique-changed",
      G_CALLBACK (bar_item_dialog_unique_changed), dialog);

  gtk_window_set_title (GTK_WINDOW (dialog), _("Add New Items"));
  xfce_titled_dialog_set_subtitle (XFCE_TITLED_DIALOG (dialog),
      _("Add new plugins to the bar"));
  gtk_window_set_icon_name (GTK_WINDOW (dialog), GTK_STOCK_ADD);
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 350, 450);
  gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_NORMAL);

  dialog->add_button = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_widget_show (dialog->add_button);

  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), dialog->add_button, GTK_RESPONSE_OK);
  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  main_vbox = gtk_vbox_new (FALSE, BORDER * 2);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), BORDER);
  gtk_widget_show (main_vbox);

  /* search widget */
  hbox = gtk_hbox_new (FALSE, BORDER);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("_Search:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), entry, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  gtk_widget_set_tooltip_text (entry, _("Enter search phrase here"));
#if GTK_CHECK_VERSION (2, 16, 0)
  gtk_entry_set_icon_from_stock (GTK_ENTRY (entry), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FIND);
#endif
  gtk_widget_show (entry);

  /* scroller */
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (main_vbox), scroll, TRUE, TRUE, 0);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
  gtk_widget_show (scroll);

  /* create the store and automatically sort it */
  dialog->store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (dialog->store), COLUMN_MODULE, bar_item_dialog_compare_func, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (dialog->store), COLUMN_MODULE, GTK_SORT_ASCENDING);

  /* create treemodel with filter */
  filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (dialog->store), NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (filter), bar_item_dialog_visible_func, entry, NULL);
  g_signal_connect_swapped (G_OBJECT (entry), "changed", G_CALLBACK (gtk_tree_model_filter_refilter), filter);

  /* treeview */
  treeview = gtk_tree_view_new_with_model (filter);
  dialog->treeview = GTK_TREE_VIEW (treeview);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
  gtk_tree_view_set_enable_search (GTK_TREE_VIEW (treeview), FALSE);
  gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (treeview), bar_item_dialog_separator_func, NULL, NULL);
  g_signal_connect_swapped (G_OBJECT (treeview), "start-interactive-search", G_CALLBACK (gtk_widget_grab_focus), entry);
  gtk_container_add (GTK_CONTAINER (scroll), treeview);
  gtk_widget_show (treeview);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (bar_item_dialog_selection_changed), dialog);

  g_object_unref (G_OBJECT (filter));

  /* signals for treeview dnd */
  gtk_drag_source_set (treeview, GDK_BUTTON1_MASK, drag_targets, G_N_ELEMENTS (drag_targets), GDK_ACTION_COPY);
  g_signal_connect (G_OBJECT (treeview), "drag-begin", G_CALLBACK (bar_item_dialog_drag_begin), dialog);
  g_signal_connect (G_OBJECT (treeview), "drag-data-get", G_CALLBACK (bar_item_dialog_drag_data_get), dialog);

  /* remove plugin when dropping it back in the treeview */
  gtk_drag_dest_set (GTK_WIDGET (treeview), GTK_DEST_DEFAULT_ALL,
      drop_targets, G_N_ELEMENTS (drop_targets), GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (treeview), "drag-data-received", G_CALLBACK (bar_item_dialog_drag_data_received), dialog);

  /* icon renderer */
  renderer = gtk_cell_renderer_pixbuf_new ();
  column = gtk_tree_view_column_new_with_attributes ("", renderer, "icon-name", COLUMN_ICON_NAME, "sensitive", COLUMN_SENSITIVE, NULL);
  g_object_set (G_OBJECT (renderer), "stock-size", GTK_ICON_SIZE_DND, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

  /* text renderer */
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer, bar_item_dialog_text_renderer, NULL, NULL);
  gtk_tree_view_column_set_attributes (column, renderer, "sensitive", COLUMN_SENSITIVE, NULL);
  g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

  bar_item_dialog_populate_store (dialog);
}



static void
bar_item_dialog_finalize (GObject *object)
{
  BarItemDialog *dialog = BAR_ITEM_DIALOG (object);

  /* disconnect unique-changed signal */
  g_signal_handlers_disconnect_by_func (G_OBJECT (dialog->factory),
      bar_item_dialog_unique_changed, dialog);

  /* make the windows sensitive again */
  bar_application_windows_blocked (dialog->application, FALSE);

  g_object_unref (G_OBJECT (dialog->store));
  g_object_unref (G_OBJECT (dialog->factory));
  g_object_unref (G_OBJECT (dialog->application));

  (*G_OBJECT_CLASS (bar_item_dialog_parent_class)->finalize) (object);
}



static void
bar_item_dialog_response (GtkDialog *gtk_dialog,
                            gint       response_id)
{
  BarItemDialog *dialog = BAR_ITEM_DIALOG (gtk_dialog);
  BarModule     *module;

  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));
  bar_return_if_fail (GTK_IS_TREE_VIEW (dialog->treeview));
  bar_return_if_fail (BAR_IS_APPLICATION (dialog->application));

  if (response_id == GTK_RESPONSE_HELP)
    {
      bar_utils_show_help (GTK_WINDOW (gtk_dialog), "add-new-items", NULL);
    }
  else if (response_id == GTK_RESPONSE_OK)
    {
      module = bar_item_dialog_get_selected_module (dialog->treeview);
      if (G_LIKELY (module != NULL))
        {
          bar_application_add_new_item (dialog->application,
              dialog->active,
              bar_module_get_name (module), NULL);
          g_object_unref (G_OBJECT (module));
        }
    }
  else
    {
      if (!bar_preferences_dialog_visible ())
        bar_application_window_select (dialog->application, NULL);

      gtk_widget_destroy (GTK_WIDGET (gtk_dialog));
    }
}



static void
bar_item_dialog_unique_changed (BarModuleFactory *factory,
                                  BarModule        *module,
                                  BarItemDialog    *dialog)
{
  bar_return_if_fail (BAR_IS_MODULE_FACTORY (factory));
  bar_return_if_fail (BAR_IS_MODULE (module));
  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));
  bar_return_if_fail (GTK_IS_LIST_STORE (dialog->store));

  /* search the module and update its sensitivity */
  g_object_set_data (G_OBJECT (dialog->store), "dialog", dialog);
  gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->store),
      bar_item_dialog_unique_changed_foreach, module);
  g_object_set_data (G_OBJECT (dialog->store), "dialog", NULL);

  /* update button sensitivity */
  bar_item_dialog_selection_changed (gtk_tree_view_get_selection (dialog->treeview), dialog);
}



static gboolean
bar_item_dialog_unique_changed_foreach (GtkTreeModel *model,
                                          GtkTreePath  *path,
                                          GtkTreeIter  *iter,
                                          gpointer      user_data)
{
  BarModule *module;
  gboolean     result;
  GtkWidget   *dialog;

  bar_return_val_if_fail (BAR_IS_MODULE (user_data), FALSE);

  /* get the module of this iter */
  gtk_tree_model_get (model, iter, COLUMN_MODULE, &module, -1);

  /* skip the separator */
  if (G_UNLIKELY (module == NULL))
    return FALSE;

  /* check if this is the module we're looking for */
  result = !!(module == BAR_MODULE (user_data));

  if (result)
    {
      dialog = g_object_get_data (G_OBJECT (model), "dialog");
      bar_return_val_if_fail (BAR_IS_ITEM_DIALOG (dialog), FALSE);

      /* update the module unique status */
      gtk_list_store_set (GTK_LIST_STORE (model), iter,
                          COLUMN_SENSITIVE, bar_module_is_usable (module,
                              gtk_widget_get_screen (dialog)), -1);
    }

  g_object_unref (G_OBJECT (module));

  /* continue searching or break if the module was found */
  return result;
}



static gboolean
bar_item_dialog_separator_func (GtkTreeModel *model,
                                  GtkTreeIter  *iter,
                                  gpointer      user_data)
{
  BarModule *module;

  /* it's a separator if the module is null */
  gtk_tree_model_get (model, iter, COLUMN_MODULE, &module, -1);
  if (G_UNLIKELY (module == NULL))
    return TRUE;
  g_object_unref (G_OBJECT (module));

  return FALSE;
}



static void
bar_item_dialog_selection_changed (GtkTreeSelection *selection,
                                     BarItemDialog  *dialog)
{
  BarModule *module;
  gboolean     sensitive = FALSE;

  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));
  bar_return_if_fail (GTK_IS_TREE_SELECTION (selection));

  module = bar_item_dialog_get_selected_module (dialog->treeview);
  if (module != NULL)
    {
      sensitive = bar_module_is_usable (module, gtk_widget_get_screen (GTK_WIDGET (dialog)));
      g_object_unref (G_OBJECT (module));
    }

  gtk_widget_set_sensitive (dialog->add_button, sensitive);
}



static BarModule *
bar_item_dialog_get_selected_module (GtkTreeView *treeview)
{
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  GtkTreeIter       iter;
  BarModule      *module = NULL;

  bar_return_val_if_fail (GTK_IS_TREE_VIEW (treeview), NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (G_LIKELY (selection != NULL))
    {
      if (gtk_tree_selection_get_selected (selection, &model, &iter))
        {
          gtk_tree_model_get (model, &iter, COLUMN_MODULE, &module, -1);
          if (G_LIKELY (module != NULL))
            {
              /* check if the module is still valid */
              if (!bar_module_is_valid (module))
                {
                  g_object_unref (G_OBJECT (module));

                  /* no, cannot add it, return null */
                  module = NULL;
                }
            }
        }
    }

  return module;
}



static void
bar_item_dialog_drag_begin (GtkWidget       *treeview,
                              GdkDragContext  *context,
                              BarItemDialog *dialog)
{
  BarModule  *module;
  const gchar  *icon_name;
  GtkIconTheme *theme;

  bar_return_if_fail (GTK_IS_TREE_VIEW (treeview));
  bar_return_if_fail (GDK_IS_DRAG_CONTEXT (context));
  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));

  module = bar_item_dialog_get_selected_module (GTK_TREE_VIEW (treeview));
  if (G_LIKELY (module != NULL))
    {
      if (bar_module_is_usable (module, gtk_widget_get_screen (GTK_WIDGET (dialog))))
        {
          /* set the drag icon */
          icon_name = bar_module_get_icon_name (module);
          theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (treeview));
          if (!blxo_str_is_empty (icon_name)
              && gtk_icon_theme_has_icon (theme, icon_name))
            gtk_drag_set_icon_name (context, icon_name, 0, 0);
          else
            gtk_drag_set_icon_default (context);
        }
      else
        {
          /* plugin is not usable */
          gtk_drag_set_icon_name (context, GTK_STOCK_CANCEL, 0, 0);
        }

      g_object_unref (G_OBJECT (module));
    }
}



static void
bar_item_dialog_drag_data_get (GtkWidget        *treeview,
                                 GdkDragContext   *context,
                                 GtkSelectionData *selection_data,
                                 guint             drag_info,
                                 guint             drag_time,
                                 BarItemDialog  *dialog)
{
  BarModule *module;
  const gchar *internal_name;

  bar_return_if_fail (GTK_IS_TREE_VIEW (treeview));
  bar_return_if_fail (GDK_IS_DRAG_CONTEXT (context));
  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));

  module = bar_item_dialog_get_selected_module (GTK_TREE_VIEW (treeview));
  if (G_LIKELY (module != NULL))
    {
      /* set the internal module name as selection data */
      internal_name = bar_module_get_name (module);
      gtk_selection_data_set (selection_data, selection_data->target, 8,
          (guchar *) internal_name, strlen (internal_name));
      g_object_unref (G_OBJECT (module));
    }
}



static void
bar_item_dialog_drag_data_received (GtkWidget        *treeview,
                                      GdkDragContext   *context,
                                      gint              x,
                                      gint              y,
                                      GtkSelectionData *selection_data,
                                      guint             info,
                                      guint             drag_time,
                                      BarItemDialog  *dialog)
{
  GtkWidget *widget;

  bar_return_if_fail (GTK_IS_TREE_VIEW (treeview));
  bar_return_if_fail (GDK_IS_DRAG_CONTEXT (context));
  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));

  /* ask the plugin to cleanup when we destroy a bar window */
  widget = gtk_drag_get_source_widget (context);
  bar_return_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (widget));
  blade_bar_plugin_provider_ask_remove (BLADE_BAR_PLUGIN_PROVIDER (widget));

  gtk_drag_finish (context, TRUE, FALSE, drag_time);

  g_signal_stop_emission_by_name (G_OBJECT (treeview), "drag-data-received");
}



static void
bar_item_dialog_populate_store (BarItemDialog *dialog)
{
  GList       *modules, *li;
  gint         n;
  GtkTreeIter  iter;
  BarModule *module;

  bar_return_if_fail (BAR_IS_ITEM_DIALOG (dialog));
  bar_return_if_fail (BAR_IS_MODULE_FACTORY (dialog->factory));
  bar_return_if_fail (GTK_IS_LIST_STORE (dialog->store));

  /* add all known modules in the factory */
  modules = bar_module_factory_get_modules (dialog->factory);
  for (li = modules, n = 0; li != NULL; li = li->next, n++)
    {
      module = BAR_MODULE (li->data);

      gtk_list_store_insert_with_values (dialog->store, &iter, n,
          COLUMN_MODULE, module,
          COLUMN_ICON_NAME, bar_module_get_icon_name (module),
          COLUMN_SENSITIVE, bar_module_is_usable (module,
              gtk_widget_get_screen (GTK_WIDGET (dialog))), -1);
    }

  g_list_free (modules);

  /* add an empty item for separator in 2nd position */
  if (bar_module_factory_has_launcher (dialog->factory))
    gtk_list_store_insert_with_values (dialog->store, &iter, 1,
                                       COLUMN_MODULE, NULL, -1);
}



static gint
bar_item_dialog_compare_func (GtkTreeModel *model,
                                GtkTreeIter  *a,
                                GtkTreeIter  *b,
                                gpointer      user_data)
{
  BarModule *module_a;
  BarModule *module_b;
  const gchar *name_a;
  const gchar *name_b;
  gint         result;

  /* get modules a name */
  gtk_tree_model_get (model, a, COLUMN_MODULE, &module_a, -1);
  gtk_tree_model_get (model, b, COLUMN_MODULE, &module_b, -1);

  if (G_UNLIKELY (module_a == NULL || module_b == NULL))
    {
      /* don't move the separator */
      result = 0;
    }
  else if (blxo_str_is_equal (LAUNCHER_PLUGIN_NAME,
                             bar_module_get_name (module_a)))
    {
      /* move the launcher to the first position */
      result = -1;
    }
  else if (blxo_str_is_equal (LAUNCHER_PLUGIN_NAME,
                             bar_module_get_name (module_b)))
    {
      /* move the launcher to the first position */
      result = 1;
    }
  else
    {
      /* get the visible module names */
      name_a = bar_module_get_display_name (module_a);
      name_b = bar_module_get_display_name (module_b);

      /* get sort order */
      if (G_LIKELY (name_a && name_b))
        result = g_utf8_collate (name_a, name_b);
      else if (name_a == name_b)
        result = 0;
      else
        result = name_a == NULL ? 1 : -1;
    }

  if (G_LIKELY (module_a))
    g_object_unref (G_OBJECT (module_a));
  if (G_LIKELY (module_b))
    g_object_unref (G_OBJECT (module_b));

  return result;
}



static gboolean
bar_item_dialog_visible_func (GtkTreeModel *model,
                                GtkTreeIter  *iter,
                                gpointer      user_data)
{

  GtkEntry    *entry = GTK_ENTRY (user_data);
  const gchar *text, *name, *comment;
  BarModule *module;
  gchar       *normalized;
  gchar       *text_casefolded;
  gchar       *name_casefolded;
  gchar       *comment_casefolded;
  gboolean     visible = FALSE;

  /* search string from dialog */
  text = gtk_entry_get_text (entry);
  if (G_UNLIKELY (blxo_str_is_empty (text)))
    return TRUE;

  gtk_tree_model_get (model, iter, COLUMN_MODULE, &module, -1);

  /* hide separator when searching */
  if (G_UNLIKELY (module == NULL))
    return FALSE;

  /* casefold the search text */
  normalized = g_utf8_normalize (text, -1, G_NORMALIZE_ALL);
  text_casefolded = g_utf8_casefold (normalized, -1);
  g_free (normalized);

  name = bar_module_get_display_name (module);
  if (G_LIKELY (name != NULL))
    {
      /* casefold the name */
      normalized = g_utf8_normalize (name, -1, G_NORMALIZE_ALL);
      name_casefolded = g_utf8_casefold (normalized, -1);
      g_free (normalized);

      /* search */
      visible = (strstr (name_casefolded, text_casefolded) != NULL);

      g_free (name_casefolded);
    }

  if (!visible)
    {
      comment = bar_module_get_comment (module);
      if (comment != NULL)
        {
          /* casefold the comment */
          normalized = g_utf8_normalize (comment, -1, G_NORMALIZE_ALL);
          comment_casefolded = g_utf8_casefold (normalized, -1);
          g_free (normalized);

          /* search */
          visible = (strstr (comment_casefolded, text_casefolded) != NULL);

          g_free (comment_casefolded);
        }
    }

  g_free (text_casefolded);
  g_object_unref (G_OBJECT (module));

  return visible;
}



static void
bar_item_dialog_text_renderer (GtkTreeViewColumn *column,
                                 GtkCellRenderer   *renderer,
                                 GtkTreeModel      *model,
                                 GtkTreeIter       *iter,
                                 gpointer           user_data)
{
  BarModule *module;
  gchar       *markup;
  const gchar *name, *comment;

  gtk_tree_model_get (model, iter, COLUMN_MODULE, &module, -1);
  if (G_UNLIKELY (module == NULL))
    return;

  /* avoid (null) in markup string */
  comment = bar_module_get_comment (module);
  if (blxo_str_is_empty (comment))
    comment = "";

  name = bar_module_get_display_name (module);
  markup = g_markup_printf_escaped ("<b>%s</b>\n%s", name, comment);
  g_object_set (G_OBJECT (renderer), "markup", markup, NULL);
  g_free (markup);

  g_object_unref (G_OBJECT (module));
}



void
bar_item_dialog_show (BarWindow *window)
{
  GdkScreen        *screen;
  BarApplication *application;

  bar_return_if_fail (window == NULL || BAR_IS_WINDOW (window));

  /* check if not the entire application is locked */
  if (bar_dialogs_kiosk_warning ())
    return;

  if (G_LIKELY (dialog_singleton == NULL))
    {
      /* create new dialog singleton */
      dialog_singleton = g_object_new (BAR_TYPE_ITEM_DIALOG, NULL);
      g_object_add_weak_pointer (G_OBJECT (dialog_singleton), (gpointer) &dialog_singleton);
    }

  /* show the dialog on the same screen as the bar */
  if (G_UNLIKELY (window != NULL))
    {
      /* set the active bar */
      application = bar_application_get ();
      bar_application_window_select (application, window);
      dialog_singleton->active = window;
      g_object_unref (G_OBJECT (application));

      screen = gtk_window_get_screen (GTK_WINDOW (window));
    }
  else
    {
      screen = gdk_screen_get_default ();
    }

  gtk_window_set_screen (GTK_WINDOW (dialog_singleton), screen);

  /* focus the window */
  gtk_window_present (GTK_WINDOW (dialog_singleton));
}



void
bar_item_dialog_show_from_id (gint bar_id)
{
  BarApplication *application;
  BarWindow      *window;

  application = bar_application_get ();
  window = bar_application_get_window (application, bar_id);
  bar_item_dialog_show (window);
  g_object_unref (G_OBJECT (application));
}



gboolean
bar_item_dialog_visible (void)
{
  return !!(dialog_singleton != NULL);
}
