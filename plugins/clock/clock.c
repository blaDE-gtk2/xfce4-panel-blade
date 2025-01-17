/*
 * Copyright (C) 2007-2010 Nick Schermer <nick@xfce.org>
 * Copyright (C) 2012      Guido Berhoerster <gber@opensuse.org>
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

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <blxo/blxo.h>
#include <libbladeui/libbladeui.h>
#include <libbladebar/libbladebar.h>
#include <common/bar-private.h>
#include <common/bar-blconf.h>
#include <common/bar-utils.h>

#include "clock.h"
#include "clock-time.h"
#include "clock-analog.h"
#include "clock-binary.h"
#include "clock-digital.h"
#include "clock-fuzzy.h"
#include "clock-lcd.h"
#include "clock-dialog_ui.h"

#define DEFAULT_TOOLTIP_FORMAT "%A %d %B %Y"

/* Please adjust the following command to match your distribution */
/* e.g. "time-admin" */
#define DEFAULT_TIME_CONFIG_TOOL "time-admin"

/* Use the posix directory for the names. If people want a time based on posix or
 * right time, they can prepend that manually in the entry */
#define ZONEINFO_DIR "/usr/share/zoneinfo/posix/"



static void     clock_plugin_get_property              (GObject               *object,
                                                        guint                  prop_id,
                                                        GValue                *value,
                                                        GParamSpec            *pspec);
static void     clock_plugin_set_property              (GObject               *object,
                                                        guint                  prop_id,
                                                        const GValue          *value,
                                                        GParamSpec            *pspec);
static gboolean clock_plugin_leave_notify_event        (GtkWidget             *widget,
                                                        GdkEventCrossing      *event,
                                                        ClockPlugin           *plugin);
static gboolean clock_plugin_enter_notify_event        (GtkWidget             *widget,
                                                        GdkEventCrossing      *event,
                                                        ClockPlugin           *plugin);
static gboolean clock_plugin_button_press_event        (GtkWidget             *widget,
                                                        GdkEventButton        *event,
                                                        ClockPlugin           *plugin);
static void     clock_plugin_construct                 (BladeBarPlugin       *bar_plugin);
static void     clock_plugin_free_data                 (BladeBarPlugin       *bar_plugin);
static gboolean clock_plugin_size_changed              (BladeBarPlugin       *bar_plugin,
                                                        gint                   size);
static void     clock_plugin_size_ratio_changed        (BladeBarPlugin       *bar_plugin);
static void     clock_plugin_mode_changed              (BladeBarPlugin       *bar_plugin,
                                                        BladeBarPluginMode    mode);
static void     clock_plugin_screen_position_changed   (BladeBarPlugin       *bar_plugin,
                                                        XfceScreenPosition     position);
static void     clock_plugin_configure_plugin          (BladeBarPlugin       *bar_plugin);
static void     clock_plugin_set_mode                  (ClockPlugin           *plugin);
static void     clock_plugin_reposition_calendar       (ClockPlugin           *plugin);
static gboolean clock_plugin_pointer_grab              (ClockPlugin           *plugin,
                                                        GtkWidget             *widget,
                                                        gboolean               keep);
static void     clock_plugin_pointer_ungrab            (ClockPlugin           *plugin,
                                                        GtkWidget             *widget);
static gboolean clock_plugin_calendar_pointed          (GtkWidget             *calendar_window,
                                                        gdouble                x_root,
                                                        gdouble                y_root);
static gboolean clock_plugin_calendar_button_press_event (GtkWidget           *calendar_window,
                                                          GdkEventButton      *event,
                                                          ClockPlugin         *plugin);
static gboolean clock_plugin_calendar_key_press_event  (GtkWidget             *calendar_window,
                                                        GdkEventKey           *event,
                                                        ClockPlugin           *plugin);
static void     clock_plugin_popup_calendar            (ClockPlugin           *plugin,
                                                        gboolean               modal);
static void     clock_plugin_hide_calendar             (ClockPlugin           *plugin);
static gboolean clock_plugin_tooltip                   (gpointer               user_data);



enum
{
  PROP_0,
  PROP_MODE,
  PROP_TOOLTIP_FORMAT,
  PROP_COMMAND,
  PROP_ROTATE_VERTICALLY,
  PROP_TIME_CONFIG_TOOL
};

typedef enum
{
  CLOCK_PLUGIN_MODE_ANALOG = 0,
  CLOCK_PLUGIN_MODE_BINARY,
  CLOCK_PLUGIN_MODE_DIGITAL,
  CLOCK_PLUGIN_MODE_FUZZY,
  CLOCK_PLUGIN_MODE_LCD,

  /* defines */
  CLOCK_PLUGIN_MODE_MIN = CLOCK_PLUGIN_MODE_ANALOG,
  CLOCK_PLUGIN_MODE_MAX = CLOCK_PLUGIN_MODE_LCD,
  CLOCK_PLUGIN_MODE_DEFAULT = CLOCK_PLUGIN_MODE_DIGITAL
}
ClockPluginMode;

struct _ClockPluginClass
{
  BladeBarPluginClass __parent__;
};

struct _ClockPlugin
{
  BladeBarPlugin __parent__;

  GtkWidget          *clock;
  GtkWidget          *button;

  GtkWidget          *calendar_window;
  GtkWidget          *calendar;

  gchar              *command;
  ClockPluginMode     mode;
  guint               rotate_vertically : 1;

  gchar              *tooltip_format;
  ClockTimeTimeout   *tooltip_timeout;

  GdkGrabStatus       grab_pointer;
  GdkGrabStatus       grab_keyboard;

  gchar              *time_config_tool;
  ClockTime          *time;
};

typedef struct
{
  ClockPlugin *plugin;
  GtkBuilder  *builder;
  guint        zonecompletion_idle;
}
ClockPluginDialog;

static const gchar *tooltip_formats[] =
{
  DEFAULT_TOOLTIP_FORMAT,
  "%x",
  N_("Week %V"),
  NULL
};

static const gchar *digital_formats[] =
{
  DEFAULT_DIGITAL_FORMAT,
  "%T",
  "%r",
  "%I:%M %p",
  NULL
};

enum
{
  COLUMN_FORMAT,
  COLUMN_SEPARATOR,
  COLUMN_TEXT,
  N_COLUMNS
};



/* define the plugin */
BLADE_BAR_DEFINE_PLUGIN (ClockPlugin, clock_plugin,
  clock_time_register_type,
  xfce_clock_analog_register_type,
  xfce_clock_binary_register_type,
  xfce_clock_digital_register_type,
  xfce_clock_fuzzy_register_type,
  xfce_clock_lcd_register_type)



static void
clock_plugin_class_init (ClockPluginClass *klass)
{
  GObjectClass         *gobject_class;
  BladeBarPluginClass *plugin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = clock_plugin_set_property;
  gobject_class->get_property = clock_plugin_get_property;

  plugin_class = BLADE_BAR_PLUGIN_CLASS (klass);
  plugin_class->construct = clock_plugin_construct;
  plugin_class->free_data = clock_plugin_free_data;
  plugin_class->size_changed = clock_plugin_size_changed;
  plugin_class->mode_changed = clock_plugin_mode_changed;
  plugin_class->screen_position_changed = clock_plugin_screen_position_changed;
  plugin_class->configure_plugin = clock_plugin_configure_plugin;

  g_object_class_install_property (gobject_class,
                                   PROP_MODE,
                                   g_param_spec_uint ("mode",
                                                      NULL, NULL,
                                                      CLOCK_PLUGIN_MODE_MIN,
                                                      CLOCK_PLUGIN_MODE_MAX,
                                                      CLOCK_PLUGIN_MODE_DEFAULT,
                                                      BLXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_TOOLTIP_FORMAT,
                                   g_param_spec_string ("tooltip-format",
                                                        NULL, NULL,
                                                        DEFAULT_TOOLTIP_FORMAT,
                                                        BLXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_ROTATE_VERTICALLY,
                                   g_param_spec_boolean ("rotate-vertically",
                                                         NULL, NULL,
                                                         TRUE,
                                                         BLXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_COMMAND,
                                   g_param_spec_string ("command",
                                                        NULL, NULL, NULL,
                                                        BLXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_TIME_CONFIG_TOOL,
                                   g_param_spec_string ("time-config-tool",
                                                        NULL, NULL,
                                                        DEFAULT_TIME_CONFIG_TOOL,
                                                        BLXO_PARAM_READWRITE));
}



static void
clock_plugin_init (ClockPlugin *plugin)
{
  plugin->mode = CLOCK_PLUGIN_MODE_DEFAULT;
  plugin->clock = NULL;
  plugin->tooltip_format = g_strdup (DEFAULT_TOOLTIP_FORMAT);
  plugin->tooltip_timeout = NULL;
  plugin->command = NULL;
  plugin->time_config_tool = g_strdup (DEFAULT_TIME_CONFIG_TOOL);
  plugin->rotate_vertically = TRUE;
  plugin->time = clock_time_new ();

  plugin->button = blade_bar_create_toggle_button ();
  /* blade_bar_plugin_add_action_widget (BLADE_BAR_PLUGIN (plugin), plugin->button); */
  gtk_container_add (GTK_CONTAINER (plugin), plugin->button);
  gtk_widget_set_name (plugin->button, "clock-button");
  gtk_button_set_relief (GTK_BUTTON (plugin->button), GTK_RELIEF_NONE);
  /* Have to handle all events in the button object rather than the plugin.
   * Otherwise, default handlers will block the events. */
  g_signal_connect (G_OBJECT (plugin->button), "button-press-event",
                    G_CALLBACK (clock_plugin_button_press_event), plugin);
  g_signal_connect (G_OBJECT (plugin->button), "enter-notify-event",
                    G_CALLBACK (clock_plugin_enter_notify_event), plugin);
  g_signal_connect (G_OBJECT (plugin->button), "leave-notify-event",
                    G_CALLBACK (clock_plugin_leave_notify_event), plugin);
  gtk_widget_show (plugin->button);
}



static void
clock_plugin_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_uint (value, plugin->mode);
      break;

    case PROP_TOOLTIP_FORMAT:
      g_value_set_string (value, plugin->tooltip_format);
      break;

    case PROP_COMMAND:
      g_value_set_string (value, plugin->command);
      break;

    case PROP_TIME_CONFIG_TOOL:
      g_value_set_string (value, plugin->time_config_tool);
      break;

    case PROP_ROTATE_VERTICALLY:
      g_value_set_boolean (value, plugin->rotate_vertically);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
clock_plugin_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (object);
  gboolean     rotate_vertically;

  switch (prop_id)
    {
    case PROP_MODE:
      if (plugin->mode != g_value_get_uint (value))
        {
          plugin->mode = g_value_get_uint (value);
          clock_plugin_set_mode (plugin);
        }
      break;

    case PROP_TOOLTIP_FORMAT:
      g_free (plugin->tooltip_format);
      plugin->tooltip_format = g_value_dup_string (value);
      break;

    case PROP_COMMAND:
      g_free (plugin->command);
      plugin->command = g_value_dup_string (value);
      /*
       * ensure the calendar window is hidden since a non-empty command disables
       * toggling
       */
      clock_plugin_hide_calendar (plugin);
      break;

    case PROP_TIME_CONFIG_TOOL:
      g_free (plugin->time_config_tool);
      plugin->time_config_tool = g_value_dup_string (value);
      break;

    case PROP_ROTATE_VERTICALLY:
      rotate_vertically = g_value_get_boolean (value);
      if (plugin->rotate_vertically != rotate_vertically)
        {
          plugin->rotate_vertically = rotate_vertically;
          clock_plugin_set_mode (plugin);
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gboolean
clock_plugin_leave_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event,
                                 ClockPlugin      *plugin)
{
  /* stop a running tooltip timeout when we leave the widget */
  if (plugin->tooltip_timeout != NULL)
    {
      clock_time_timeout_free (plugin->tooltip_timeout);
      plugin->tooltip_timeout = NULL;
    }

  return FALSE;
}



static gboolean
clock_plugin_enter_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event,
                                 ClockPlugin      *plugin)
{
  guint        interval;

  /* start the tooltip timeout if needed */
  if (plugin->tooltip_timeout == NULL)
    {
      interval = clock_time_interval_from_format (plugin->tooltip_format);
      plugin->tooltip_timeout = clock_time_timeout_new (interval, plugin->time,
                                                        G_CALLBACK (clock_plugin_tooltip), plugin);
    }

  return FALSE;
}



static gboolean
clock_plugin_button_press_event (GtkWidget      *widget,
                                 GdkEventButton *event,
                                 ClockPlugin    *plugin)
{
  GError      *error = NULL;

  if (event->button == 1 || event->button == 2)
    {
      if (event->type == GDK_BUTTON_PRESS &&
          blxo_str_is_empty (plugin->command))
        {
          /* toggle calendar window visibility */
          if (plugin->calendar_window == NULL
              || !gtk_widget_get_visible (GTK_WIDGET (plugin->calendar_window)))
            clock_plugin_popup_calendar
              (plugin, event->button == 1 && !(event->state & GDK_CONTROL_MASK));
          else
            clock_plugin_hide_calendar (plugin);

          return TRUE;
        }
      else if (event->type == GDK_2BUTTON_PRESS
               && !blxo_str_is_empty (plugin->command))
        {
          /* launch command */
          if (!xfce_spawn_command_line_on_screen (gtk_widget_get_screen (widget),
                                                  plugin->command, FALSE,
                                                  FALSE, &error))
            {
              xfce_dialog_show_error (NULL, error,
                                      _("Failed to execute clock command"));
              g_error_free (error);
            }

          return TRUE;
        }
      return TRUE;
    }

  /* bypass GTK_TOGGLE_BUTTON's handler and go directly to the plugin's one */
  return (*GTK_WIDGET_CLASS (clock_plugin_parent_class)->button_press_event) (GTK_WIDGET (plugin), event);
}



static void
clock_plugin_construct (BladeBarPlugin *bar_plugin)
{
  ClockPlugin         *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);
  const BarProperty  properties[] =
  {
    { "mode", G_TYPE_UINT },
    { "tooltip-format", G_TYPE_STRING },
    { "command", G_TYPE_STRING },
    { "rotate-vertically", G_TYPE_BOOLEAN },
    { "time-config-tool", G_TYPE_STRING },
    { NULL }
  };

  const BarProperty  time_properties[] =
    {
      { "timezone", G_TYPE_STRING },
      { NULL }
    };

  /* show configure */
  blade_bar_plugin_menu_show_configure (bar_plugin);

  /* connect all properties */
  bar_properties_bind (NULL, G_OBJECT (bar_plugin),
                         blade_bar_plugin_get_property_base (bar_plugin),
                         properties, FALSE);

  bar_properties_bind (NULL, G_OBJECT (plugin->time),
                         blade_bar_plugin_get_property_base (bar_plugin),
                         time_properties, FALSE);

  /* make sure a mode is set */
  if (plugin->mode == CLOCK_PLUGIN_MODE_DEFAULT)
    clock_plugin_set_mode (plugin);
}



static void
clock_plugin_free_data (BladeBarPlugin *bar_plugin)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);

  if (plugin->tooltip_timeout != NULL)
    clock_time_timeout_free (plugin->tooltip_timeout);

  if (plugin->calendar_window != NULL)
    gtk_widget_destroy (plugin->calendar_window);

  g_object_unref (G_OBJECT (plugin->time));

  g_free (plugin->tooltip_format);
  g_free (plugin->time_config_tool);
  g_free (plugin->command);
}



static gboolean
clock_plugin_size_changed (BladeBarPlugin *bar_plugin,
                           gint             size)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);
  gdouble      ratio;
  gint         ratio_size;
  gint         offset;

  if (plugin->clock == NULL)
    return TRUE;

  /* get the width:height ratio */
  g_object_get (G_OBJECT (plugin->clock), "size-ratio", &ratio, NULL);
  if (ratio > 0)
    {
      ratio_size = size;
      offset = 0;
    }
  else
    {
      ratio_size = -1;
      offset = 0;
    }

  /* set the clock size */
  if (blade_bar_plugin_get_mode (bar_plugin) == BLADE_BAR_PLUGIN_MODE_HORIZONTAL)
    {
      if (ratio > 0)
        {
          ratio_size = ceil (ratio_size * ratio);
          ratio_size += offset;
        }

      gtk_widget_set_size_request (GTK_WIDGET (bar_plugin), ratio_size, size);
    }
  else
    {
      if (ratio > 0)
        {
          ratio_size = ceil (ratio_size / ratio);
          ratio_size += offset;
        }

      gtk_widget_set_size_request (GTK_WIDGET (bar_plugin), size, ratio_size);
    }

  if (plugin->calendar_window != NULL
      && gtk_widget_get_visible (GTK_WIDGET (plugin->calendar_window)))
    clock_plugin_reposition_calendar (plugin);

  return TRUE;
}



static void
clock_plugin_size_ratio_changed (BladeBarPlugin *bar_plugin)
{
  clock_plugin_size_changed (bar_plugin, blade_bar_plugin_get_size (bar_plugin));
}



static void
clock_plugin_mode_changed (BladeBarPlugin     *bar_plugin,
                           BladeBarPluginMode  mode)
{
  ClockPlugin    *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);
  GtkOrientation  orientation;

  if (plugin->rotate_vertically)
    {
      orientation = (mode == BLADE_BAR_PLUGIN_MODE_VERTICAL) ?
        GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
      g_object_set (G_OBJECT (plugin->clock), "orientation", orientation, NULL);
    }

  /* do a size update */
  clock_plugin_size_changed (bar_plugin, blade_bar_plugin_get_size (bar_plugin));
}



static void
clock_plugin_screen_position_changed (BladeBarPlugin    *bar_plugin,
                                      XfceScreenPosition  position)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);

  if (plugin->calendar_window != NULL
      && gtk_widget_get_visible (GTK_WIDGET (plugin->calendar_window)))
    clock_plugin_reposition_calendar (plugin);
}



static void
clock_plugin_configure_plugin_mode_changed (GtkComboBox       *combo,
                                            ClockPluginDialog *dialog)
{
  guint    i, active, mode;
  GObject *object;
  struct {
    const gchar *widget;
    const gchar *binding;
    const gchar *property;
  } names[] = {
    { "show-seconds", "show-seconds", "active" },
    { "true-binary", "true-binary", "active" },
    { "show-military", "show-military", "active" },
    { "flash-separators", "flash-separators", "active" },
    { "show-meridiem", "show-meridiem", "active" },
    { "digital-box", "digital-format", "text" },
    { "fuzziness-box", "fuzziness", "value" },
    { "show-inactive", "show-inactive", "active" },
    { "show-grid", "show-grid", "active" },
  };

  bar_return_if_fail (GTK_IS_COMBO_BOX (combo));
  bar_return_if_fail (GTK_IS_BUILDER (dialog->builder));
  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (dialog->plugin));

  /* the active items for each mode */
  mode = gtk_combo_box_get_active (combo);
  switch (mode)
    {
    case CLOCK_PLUGIN_MODE_ANALOG:
      active = 1 << 1;
      break;

    case CLOCK_PLUGIN_MODE_BINARY:
      active = 1 << 1 | 1 << 2 | 1 << 8 | 1 << 9;
      break;

    case CLOCK_PLUGIN_MODE_DIGITAL:
      active = 1 << 6;
      break;

    case CLOCK_PLUGIN_MODE_FUZZY:
      active = 1 << 7;
      break;

    case CLOCK_PLUGIN_MODE_LCD:
      active = 1 << 1 | 1 << 3 | 1 << 4 | 1 << 5;
      break;

    default:
      bar_assert_not_reached ();
      active = 0;
      break;
    }

  /* show or hide the dialog widgets */
  for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
      object = gtk_builder_get_object (dialog->builder, names[i].widget);
      bar_return_if_fail (GTK_IS_WIDGET (object));
      if (BAR_HAS_FLAG (active, 1 << (i + 1)))
        gtk_widget_show (GTK_WIDGET (object));
      else
        gtk_widget_hide (GTK_WIDGET (object));
    }

  /* make sure the new mode is set */
  if (dialog->plugin->mode != mode)
    g_object_set (G_OBJECT (dialog->plugin), "mode", mode, NULL);
  bar_return_if_fail (G_IS_OBJECT (dialog->plugin->clock));

  /* connect the blxo bindings */
  for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
      if (BAR_HAS_FLAG (active, 1 << (i + 1)))
        {
          object = gtk_builder_get_object (dialog->builder, names[i].binding);
          bar_return_if_fail (G_IS_OBJECT (object));
          blxo_mutual_binding_new (G_OBJECT (dialog->plugin->clock), names[i].binding,
                                  G_OBJECT (object), names[i].property);
        }
    }
}



static void
clock_plugin_configure_plugin_chooser_changed (GtkComboBox *combo,
                                               GtkEntry    *entry)
{
  GtkTreeIter   iter;
  GtkTreeModel *model;
  gchar        *format;

  bar_return_if_fail (GTK_IS_COMBO_BOX (combo));
  bar_return_if_fail (GTK_IS_ENTRY (entry));

  if (gtk_combo_box_get_active_iter (combo, &iter))
    {
      model = gtk_combo_box_get_model (combo);
      gtk_tree_model_get (model, &iter, COLUMN_FORMAT, &format, -1);

      if (format != NULL)
        {
          gtk_entry_set_text (entry, format);
          gtk_widget_hide (GTK_WIDGET (entry));
          g_free (format);
        }
      else
        {
          gtk_widget_show (GTK_WIDGET (entry));
        }
    }
}



static gboolean
clock_plugin_configure_plugin_chooser_separator (GtkTreeModel *model,
                                                 GtkTreeIter  *iter,
                                                 gpointer      user_data)
{
  gboolean separator;

  gtk_tree_model_get (model, iter, COLUMN_SEPARATOR, &separator, -1);

  return separator;
}



static void
clock_plugin_configure_plugin_chooser_fill (ClockPlugin *plugin,
                                            GtkComboBox *combo,
                                            GtkEntry    *entry,
                                            const gchar *formats[])
{
  guint         i;
  GtkListStore *store;
  gchar        *preview;
  GtkTreeIter   iter;
  const gchar  *active_format;
  gboolean      has_active = FALSE;

  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (plugin));
  bar_return_if_fail (GTK_IS_COMBO_BOX (combo));
  bar_return_if_fail (GTK_IS_ENTRY (entry));

  gtk_combo_box_set_row_separator_func (combo,
      clock_plugin_configure_plugin_chooser_separator, NULL, NULL);

  store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
  gtk_combo_box_set_model (combo, GTK_TREE_MODEL (store));

  active_format = gtk_entry_get_text (entry);

  for (i = 0; formats[i] != NULL; i++)
    {
      preview = clock_time_strdup_strftime (plugin->time, _(formats[i]));
      gtk_list_store_insert_with_values (store, &iter, i,
                                         COLUMN_FORMAT, _(formats[i]),
                                         COLUMN_TEXT, preview, -1);
      g_free (preview);

      if (has_active == FALSE
          && !blxo_str_is_empty (active_format)
          && strcmp (active_format, formats[i]) == 0)
        {
          gtk_combo_box_set_active_iter (combo, &iter);
          gtk_widget_hide (GTK_WIDGET (entry));
          has_active = TRUE;
        }
    }

  gtk_list_store_insert_with_values (store, NULL, i++,
                                     COLUMN_SEPARATOR, TRUE, -1);

  gtk_list_store_insert_with_values (store, &iter, i++,
                                     COLUMN_TEXT, _("Custom Format"), -1);
  if (!has_active)
    {
      gtk_combo_box_set_active_iter (combo, &iter);
      gtk_widget_show (GTK_WIDGET (entry));
    }

  g_signal_connect (G_OBJECT (combo), "changed",
      G_CALLBACK (clock_plugin_configure_plugin_chooser_changed), entry);

  g_object_unref (G_OBJECT (store));
}



static void
clock_plugin_configure_plugin_free (gpointer user_data)
{
  ClockPluginDialog *dialog = user_data;

  if (dialog->zonecompletion_idle != 0)
    g_source_remove (dialog->zonecompletion_idle);

  g_slice_free (ClockPluginDialog, dialog);
}



static void
clock_plugin_configure_config_tool_changed (ClockPluginDialog *dialog)
{
  GObject *object;
  gchar   *path;

  bar_return_if_fail (GTK_IS_BUILDER (dialog->builder));
  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (dialog->plugin));

  object = gtk_builder_get_object (dialog->builder, "run-time-config-tool");
  bar_return_if_fail (GTK_IS_BUTTON (object));
  path = g_find_program_in_path (dialog->plugin->time_config_tool);
  gtk_widget_set_visible (GTK_WIDGET (object), path != NULL);
  g_free (path);
}



static void
clock_plugin_configure_run_config_tool (GtkWidget   *button,
                                        ClockPlugin *plugin)
{
  GError *error = NULL;

  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (plugin));

  if (!xfce_spawn_command_line_on_screen (gtk_widget_get_screen (button),
                                          plugin->time_config_tool,
                                          FALSE, FALSE, &error))
    {
      xfce_dialog_show_error (NULL, error, _("Failed to execute command \"%s\"."), plugin->time_config_tool);
      g_error_free (error);
    }
}



static void
clock_plugin_configure_zoneinfo_model_insert (GtkListStore *store,
                                              const gchar  *parent)
{
  gchar       *filename;
  GtkTreeIter  iter;
  GDir        *dir;
  const gchar *name;
  gsize        dirlen = strlen (ZONEINFO_DIR);

  bar_return_if_fail (GTK_IS_LIST_STORE (store));

  dir = g_dir_open (parent, 0, NULL);
  if (dir == NULL)
    return;

  for (;;)
    {
      name = g_dir_read_name (dir);
      if (name == NULL)
        break;

      filename = g_build_filename (parent, name, NULL);

      if (g_file_test (filename, G_FILE_TEST_IS_DIR))
        {
          if (!g_file_test (filename, G_FILE_TEST_IS_SYMLINK))
            clock_plugin_configure_zoneinfo_model_insert (store, filename);
        }
      else
        {
          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter, 0, filename + dirlen, -1);
        }

      g_free (filename);
    }

  g_dir_close (dir);
}



static gboolean
clock_plugin_configure_zoneinfo_model (gpointer data)
{
  ClockPluginDialog  *dialog = data;
  GtkEntryCompletion *completion;
  GtkListStore       *store;
  GObject            *object;

  GDK_THREADS_ENTER ();

  dialog->zonecompletion_idle = 0;

  object = gtk_builder_get_object (dialog->builder, "timezone-name");
  bar_return_val_if_fail (GTK_IS_ENTRY (object), FALSE);

  /* build timezone model */
  store = gtk_list_store_new (1, G_TYPE_STRING);
  clock_plugin_configure_zoneinfo_model_insert (store, ZONEINFO_DIR);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), 0, GTK_SORT_ASCENDING);

  completion = gtk_entry_completion_new ();
  gtk_entry_completion_set_model (completion, GTK_TREE_MODEL (store));
  g_object_unref (G_OBJECT (store));

  gtk_entry_set_completion (GTK_ENTRY (object), completion);
  gtk_entry_completion_set_popup_single_match (completion, TRUE);
  gtk_entry_completion_set_text_column (completion, 0);

  g_object_unref (G_OBJECT (completion));

  GDK_THREADS_LEAVE ();

  return FALSE;
}



static void
clock_plugin_configure_plugin (BladeBarPlugin *bar_plugin)
{
  ClockPlugin       *plugin = XFCE_CLOCK_PLUGIN (bar_plugin);
  ClockPluginDialog *dialog;
  GtkBuilder        *builder;
  GObject           *window;
  GObject           *object;
  GObject           *combo;

  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (plugin));

  /* setup the dialog */
  BAR_UTILS_LINK_4UI
  builder = bar_utils_builder_new (bar_plugin, clock_dialog_ui,
                                     clock_dialog_ui_length, &window);
  if (G_UNLIKELY (builder == NULL))
    return;

  dialog = g_slice_new0 (ClockPluginDialog);
  dialog->plugin = plugin;
  dialog->builder = builder;

  object = gtk_builder_get_object (builder, "run-time-config-tool");
  bar_return_if_fail (GTK_IS_BUTTON (object));
  g_signal_connect_swapped (G_OBJECT (plugin), "notify::time-config-tool",
                            G_CALLBACK (clock_plugin_configure_config_tool_changed),
                            dialog);
  clock_plugin_configure_config_tool_changed (dialog);
  g_signal_connect (G_OBJECT (object), "clicked",
      G_CALLBACK (clock_plugin_configure_run_config_tool), plugin);

  object = gtk_builder_get_object (builder, "timezone-name");
  bar_return_if_fail (GTK_IS_ENTRY (object));
  blxo_mutual_binding_new (G_OBJECT (plugin->time), "timezone",
                          G_OBJECT (object), "text");

  /* idle add the zone completion */
  dialog->zonecompletion_idle = g_idle_add (clock_plugin_configure_zoneinfo_model, dialog);

  object = gtk_builder_get_object (builder, "mode");
  g_signal_connect_data (G_OBJECT (object), "changed",
      G_CALLBACK (clock_plugin_configure_plugin_mode_changed), dialog,
      (GClosureNotify) clock_plugin_configure_plugin_free, 0);
  blxo_mutual_binding_new (G_OBJECT (plugin), "mode",
                          G_OBJECT (object), "active");

  object = gtk_builder_get_object (builder, "tooltip-format");
  blxo_mutual_binding_new (G_OBJECT (plugin), "tooltip-format",
                          G_OBJECT (object), "text");
  combo = gtk_builder_get_object (builder, "tooltip-chooser");
  clock_plugin_configure_plugin_chooser_fill (plugin,
                                              GTK_COMBO_BOX (combo),
                                              GTK_ENTRY (object),
                                              tooltip_formats);

  object = gtk_builder_get_object (builder, "digital-format");
  combo = gtk_builder_get_object (builder, "digital-chooser");
  clock_plugin_configure_plugin_chooser_fill (plugin,
                                              GTK_COMBO_BOX (combo),
                                              GTK_ENTRY (object),
                                              digital_formats);

  gtk_widget_show (GTK_WIDGET (window));
}



static void
clock_plugin_set_mode (ClockPlugin *plugin)
{
  const BarProperty properties[][5] =
  {
    { /* analog */
      { "show-seconds", G_TYPE_BOOLEAN },
      { NULL },
    },
    { /* binary */
      { "show-seconds", G_TYPE_BOOLEAN },
      { "true-binary", G_TYPE_BOOLEAN },
      { "show-inactive", G_TYPE_BOOLEAN },
      { "show-grid", G_TYPE_BOOLEAN },
      { NULL },
    },
    { /* digital */
      { "digital-format", G_TYPE_STRING },
      { NULL },
    },
    { /* fuzzy */
      { "fuzziness", G_TYPE_UINT },
      { NULL },
    },
    { /* lcd */
      { "show-seconds", G_TYPE_BOOLEAN },
      { "show-military", G_TYPE_BOOLEAN },
      { "show-meridiem", G_TYPE_BOOLEAN },
      { "flash-separators", G_TYPE_BOOLEAN },
      { NULL },
    }
  };
  GtkOrientation      orientation;

  bar_return_if_fail (XFCE_IS_CLOCK_PLUGIN (plugin));

  if (plugin->clock != NULL)
    gtk_widget_destroy (plugin->clock);

  /* create a new clock */
  if (plugin->mode == CLOCK_PLUGIN_MODE_ANALOG)
    plugin->clock = xfce_clock_analog_new (plugin->time);
  else if (plugin->mode == CLOCK_PLUGIN_MODE_BINARY)
    plugin->clock = xfce_clock_binary_new (plugin->time);
  else if (plugin->mode == CLOCK_PLUGIN_MODE_DIGITAL)
    plugin->clock = xfce_clock_digital_new (plugin->time);
  else if (plugin->mode == CLOCK_PLUGIN_MODE_FUZZY)
    plugin->clock = xfce_clock_fuzzy_new (plugin->time);
  else
    plugin->clock = xfce_clock_lcd_new (plugin->time);


  if (plugin->rotate_vertically)
    {
      orientation =
        (blade_bar_plugin_get_mode (BLADE_BAR_PLUGIN (plugin))
         == BLADE_BAR_PLUGIN_MODE_VERTICAL) ?
        GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
      g_object_set (G_OBJECT (plugin->clock), "orientation", orientation, NULL);
    }

  /* watch width/height changes */
  g_signal_connect_swapped (G_OBJECT (plugin->clock), "notify::size-ratio",
      G_CALLBACK (clock_plugin_size_ratio_changed), plugin);

  clock_plugin_size_changed (BLADE_BAR_PLUGIN (plugin),
      blade_bar_plugin_get_size (BLADE_BAR_PLUGIN (plugin)));

  bar_properties_bind (NULL, G_OBJECT (plugin->clock),
                         blade_bar_plugin_get_property_base (BLADE_BAR_PLUGIN (plugin)),
                         properties[plugin->mode], FALSE);

  gtk_container_add (GTK_CONTAINER (plugin->button), plugin->clock);

  gtk_widget_show (plugin->clock);
}



static void
clock_plugin_reposition_calendar (ClockPlugin *plugin)
{
  gint x, y;

  blade_bar_plugin_position_widget (BLADE_BAR_PLUGIN (plugin),
                                     GTK_WIDGET (plugin->calendar_window),
                                     NULL, &x, &y);
  gtk_window_move (GTK_WINDOW (plugin->calendar_window), x, y);
}



static void
clock_plugin_calendar_show_event (GtkWidget   *calendar_window,
                                  ClockPlugin *plugin)
{
  GDateTime *date_time;

  bar_return_if_fail (BLADE_IS_BAR_PLUGIN (plugin));

  clock_plugin_reposition_calendar (plugin);

  date_time = clock_time_get_time (plugin->time);
  gtk_calendar_select_month (GTK_CALENDAR (plugin->calendar), g_date_time_get_month (date_time) - 1,
                             g_date_time_get_year (date_time));
  gtk_calendar_select_day (GTK_CALENDAR (plugin->calendar), g_date_time_get_day_of_month (date_time));
  g_date_time_unref (date_time);
}



static void
clock_plugin_pointer_ungrab (ClockPlugin *plugin,
                             GtkWidget   *widget)
{
  if (plugin->grab_pointer == GDK_GRAB_SUCCESS)
    gdk_pointer_ungrab (GDK_CURRENT_TIME);
  if (plugin->grab_keyboard == GDK_GRAB_SUCCESS)
    gdk_keyboard_ungrab (GDK_CURRENT_TIME);
}



static gboolean
clock_plugin_pointer_grab (ClockPlugin *plugin,
                           GtkWidget   *widget,
                           gboolean     keep)
{
  GdkWindow     *window;
  gboolean       grab_succeed = FALSE;
  guint          i;
  GdkEventMask   pointer_mask = GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                                | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK
                                | GDK_POINTER_MOTION_MASK;

  window = widget->window;

  /* don't try to get the grab for longer then 1/4 second */
  for (i = 0; i < (G_USEC_PER_SEC / 100 / 4); i++)
    {
      plugin->grab_keyboard = gdk_keyboard_grab (window, TRUE, GDK_CURRENT_TIME);
      if (plugin->grab_keyboard == GDK_GRAB_SUCCESS)
        {
          plugin->grab_pointer = gdk_pointer_grab (window, TRUE, pointer_mask,
                                                   NULL, NULL, GDK_CURRENT_TIME);
          if (plugin->grab_pointer == GDK_GRAB_SUCCESS)
            {
              grab_succeed = TRUE;
              break;
            }
        }

      g_usleep (100);
    }

  /* release the grab */
  if (!keep)
    clock_plugin_pointer_ungrab (plugin, widget);

  if (!grab_succeed)
    {
      clock_plugin_pointer_ungrab (plugin, widget);
      g_printerr (PACKAGE_NAME ": Unable to get keyboard and mouse "
                  "grab. Popup failed.\n");
    }

  return grab_succeed;
}



static gboolean
clock_plugin_calendar_pointed (GtkWidget *calendar_window,
                               gdouble    x_root,
                               gdouble    y_root)
{
  gint          window_x, window_y;

  if (gtk_widget_get_mapped (calendar_window))
    {
      gdk_window_get_position (calendar_window->window, &window_x, &window_y);

      if (x_root >= window_x && x_root < window_x + calendar_window->allocation.width &&
          y_root >= window_y && y_root < window_y + calendar_window->allocation.height)
        return TRUE;
    }

  return FALSE;
}



static gboolean
clock_plugin_calendar_button_press_event (GtkWidget      *calendar_window,
                                          GdkEventButton *event,
                                          ClockPlugin    *plugin)
{
  if (event->type == GDK_BUTTON_PRESS &&
      !clock_plugin_calendar_pointed (calendar_window, event->x_root, event->y_root))
    {
      clock_plugin_hide_calendar (plugin);
      return TRUE;
    }

  return FALSE;
}



static gboolean
clock_plugin_calendar_key_press_event (GtkWidget      *calendar_window,
                                       GdkEventKey    *event,
                                       ClockPlugin    *plugin)
{
  if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_Escape)
    {
      clock_plugin_hide_calendar (plugin);
      return TRUE;
    }

  return FALSE;
}



static void
clock_plugin_popup_calendar (ClockPlugin *plugin,
                             gboolean     modal)
{
  if (plugin->calendar_window == NULL)
    {
      plugin->calendar_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
      gtk_window_set_type_hint (GTK_WINDOW (plugin->calendar_window),
                                GDK_WINDOW_TYPE_HINT_UTILITY);
      gtk_window_set_decorated (GTK_WINDOW (plugin->calendar_window), FALSE);
      gtk_window_set_resizable (GTK_WINDOW (plugin->calendar_window), FALSE);
      gtk_window_set_skip_taskbar_hint(GTK_WINDOW (plugin->calendar_window), TRUE);
      gtk_window_set_skip_pager_hint(GTK_WINDOW (plugin->calendar_window), TRUE);
      gtk_window_set_keep_above (GTK_WINDOW (plugin->calendar_window), TRUE);
      gtk_window_stick (GTK_WINDOW (plugin->calendar_window));

      plugin->calendar = gtk_calendar_new ();
      gtk_calendar_set_display_options (GTK_CALENDAR (plugin->calendar),
                                        GTK_CALENDAR_SHOW_HEADING
                                        | GTK_CALENDAR_SHOW_DAY_NAMES
                                        | GTK_CALENDAR_SHOW_WEEK_NUMBERS);
      g_signal_connect (G_OBJECT (plugin->calendar_window), "show",
                        G_CALLBACK (clock_plugin_calendar_show_event), plugin);
      g_signal_connect (G_OBJECT (plugin->calendar_window), "button-press-event",
                        G_CALLBACK (clock_plugin_calendar_button_press_event), plugin);
      g_signal_connect (G_OBJECT (plugin->calendar_window), "key-press-event",
                        G_CALLBACK (clock_plugin_calendar_key_press_event), plugin);
      gtk_container_add (GTK_CONTAINER (plugin->calendar_window), plugin->calendar);
      gtk_widget_show (plugin->calendar);
    }

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);
  gtk_widget_show (GTK_WIDGET (plugin->calendar_window));
  blade_bar_plugin_block_autohide (BLADE_BAR_PLUGIN (plugin), TRUE);
  if (modal)
    clock_plugin_pointer_grab (plugin, GTK_WIDGET (plugin->calendar_window), TRUE);
}



static void
clock_plugin_hide_calendar (ClockPlugin *plugin)
{
  /* calendar_window is initialized on the first call to clock_plugin_popup_calendar () */
  if (plugin->calendar_window == NULL)
    return;

  clock_plugin_pointer_ungrab (plugin, GTK_WIDGET (plugin->calendar_window));
  gtk_widget_hide (GTK_WIDGET (plugin->calendar_window));
  blade_bar_plugin_block_autohide (BLADE_BAR_PLUGIN (plugin), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);
}



static gboolean
clock_plugin_tooltip (gpointer user_data)
{
  ClockPlugin *plugin = XFCE_CLOCK_PLUGIN (user_data);
  gchar       *string;

  /* set the tooltip */
  string = clock_time_strdup_strftime (plugin->time, plugin->tooltip_format);
  gtk_widget_set_tooltip_markup (GTK_WIDGET (plugin), string);
  g_free (string);

  /* make sure the tooltip is up2date */
  gtk_widget_trigger_tooltip_query (GTK_WIDGET (plugin));

  /* keep the timeout running */
  return TRUE;
}
