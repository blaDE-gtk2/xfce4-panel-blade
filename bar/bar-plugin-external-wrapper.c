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

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <libbladeutil/libbladeutil.h>

#include <common/bar-private.h>
#include <common/bar-dbus.h>
#include <common/bar-debug.h>

#include <libbladebar/libbladebar.h>
#include <libbladebar/blade-bar-plugin-provider.h>

#include <bar/bar-module.h>
#include <bar/bar-plugin-external.h>
#include <bar/bar-plugin-external-wrapper.h>
#include <bar/bar-window.h>
#include <bar/bar-dialogs.h>
#include <bar/bar-marshal.h>



#define WRAPPER_BIN HELPERDIR G_DIR_SEPARATOR_S "wrapper"



static GObject   *bar_plugin_external_wrapper_constructor              (GType                           type,
                                                                          guint                           n_construct_params,
                                                                          GObjectConstructParam          *construct_params);
static void       bar_plugin_external_wrapper_set_properties           (BarPluginExternal            *external,
                                                                          GSList                         *properties);
static gchar    **bar_plugin_external_wrapper_get_argv                 (BarPluginExternal            *external,
                                                                          gchar                         **arguments);
static gboolean   bar_plugin_external_wrapper_remote_event             (BarPluginExternal            *external,
                                                                          const gchar                    *name,
                                                                          const GValue                   *value,
                                                                          guint                          *handle);
static gboolean   bar_plugin_external_wrapper_dbus_provider_signal     (BarPluginExternalWrapper     *external,
                                                                          BladeBarPluginProviderSignal   provider_signal,
                                                                          GError                        **error);
static gboolean   bar_plugin_external_wrapper_dbus_remote_event_result (BarPluginExternalWrapper     *external,
                                                                          guint                           handle,
                                                                          gboolean                        result,
                                                                          GError                        **error);



/* include the dbus glue generated by dbus-binding-tool */
#include <bar/bar-plugin-external-wrapper-infos.h>



struct _BarPluginExternalWrapperClass
{
  BarPluginExternalClass __parent__;
};

struct _BarPluginExternalWrapper
{
  BarPluginExternal __parent__;
};

enum
{
  SET,
  REMOTE_EVENT,
  REMOTE_EVENT_RESULT,
  LAST_SIGNAL
};



static guint external_signals[LAST_SIGNAL];



G_DEFINE_TYPE (BarPluginExternalWrapper, bar_plugin_external_wrapper, BAR_TYPE_PLUGIN_EXTERNAL)



static void
bar_plugin_external_wrapper_class_init (BarPluginExternalWrapperClass *klass)
{
  GObjectClass             *gobject_class;
  BarPluginExternalClass *plugin_external_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor = bar_plugin_external_wrapper_constructor;

  plugin_external_class = BAR_PLUGIN_EXTERNAL_CLASS (klass);
  plugin_external_class->get_argv = bar_plugin_external_wrapper_get_argv;
  plugin_external_class->set_properties = bar_plugin_external_wrapper_set_properties;
  plugin_external_class->remote_event = bar_plugin_external_wrapper_remote_event;

  external_signals[SET] =
    g_signal_new (g_intern_static_string ("set"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  BAR_TYPE_DBUS_SET_SIGNAL);

  external_signals[REMOTE_EVENT] =
    g_signal_new (g_intern_static_string ("remote-event"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _bar_marshal_VOID__STRING_BOXED_UINT,
                  G_TYPE_NONE, 3,
                  G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_UINT);

  external_signals[REMOTE_EVENT_RESULT] =
    g_signal_new (g_intern_static_string ("remote-event-result"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _bar_marshal_VOID__UINT_BOOLEAN,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT, G_TYPE_BOOLEAN);

  /* add dbus type info for plugins */
  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass),
      &dbus_glib_bar_plugin_external_wrapper_object_info);
}



static void
bar_plugin_external_wrapper_init (BarPluginExternalWrapper *external)
{
}



static GObject *
bar_plugin_external_wrapper_constructor (GType                  type,
                                           guint                  n_construct_params,
                                           GObjectConstructParam *construct_params)
{
  GObject         *object;
  gchar           *path;
  DBusGConnection *connection;
  GError          *error = NULL;

  object = G_OBJECT_CLASS (bar_plugin_external_wrapper_parent_class)->constructor (type,
                                                                                     n_construct_params,
                                                                                     construct_params);

  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (G_LIKELY (connection != NULL))
    {
      /* register the object in dbus, the wrapper will monitor this object */
      bar_return_val_if_fail (BAR_PLUGIN_EXTERNAL (object)->unique_id != -1, NULL);
      path = g_strdup_printf (BAR_DBUS_WRAPPER_PATH, BAR_PLUGIN_EXTERNAL (object)->unique_id);
      dbus_g_connection_register_g_object (connection, path, object);
      bar_debug (BAR_DEBUG_EXTERNAL, "register dbus path %s", path);
      g_free (path);

      dbus_g_connection_unref (connection);
    }
  else
    {
      g_critical ("Failed to get D-Bus session bus: %s", error->message);
      g_error_free (error);
    }

  return object;
}



static gchar **
bar_plugin_external_wrapper_get_argv (BarPluginExternal   *external,
                                        gchar               **arguments)
{
  guint   i, argc = PLUGIN_ARGV_ARGUMENTS;
  gchar **argv;

  bar_return_val_if_fail (BAR_IS_PLUGIN_EXTERNAL_WRAPPER (external), NULL);
  bar_return_val_if_fail (BAR_IS_MODULE (external->module), NULL);
  bar_return_val_if_fail (GTK_IS_SOCKET (external), NULL);

  /* add the number of arguments to the argc count */
  if (G_UNLIKELY (arguments != NULL))
    argc += g_strv_length (arguments);

  /* setup the basic argv */
  argv = g_new0 (gchar *, argc + 1);
  argv[PLUGIN_ARGV_0] = g_strjoin ("-", WRAPPER_BIN, bar_module_get_api (external->module), NULL);
  argv[PLUGIN_ARGV_FILENAME] = g_strdup (bar_module_get_filename (external->module));
  argv[PLUGIN_ARGV_UNIQUE_ID] = g_strdup_printf ("%d", external->unique_id);;
  argv[PLUGIN_ARGV_SOCKET_ID] = g_strdup_printf ("%u", gtk_socket_get_id (GTK_SOCKET (external)));;
  argv[PLUGIN_ARGV_NAME] = g_strdup (bar_module_get_name (external->module));
  argv[PLUGIN_ARGV_DISPLAY_NAME] = g_strdup (bar_module_get_display_name (external->module));
  argv[PLUGIN_ARGV_COMMENT] = g_strdup (bar_module_get_comment (external->module));
  argv[PLUGIN_ARGV_BACKGROUND_IMAGE] = g_strdup (""); /* unused, for 4.6 plugins only */

  /* append the arguments */
  if (G_UNLIKELY (arguments != NULL))
    {
      for (i = 0; arguments[i] != NULL; i++)
        argv[i + PLUGIN_ARGV_ARGUMENTS] = g_strdup (arguments[i]);
    }

  return argv;
}



static void
bar_plugin_external_wrapper_set_properties (BarPluginExternal *external,
                                              GSList              *properties)
{
  GPtrArray      *array;
  GValue          message = { 0, };
  PluginProperty *property;
  GSList         *li;
  guint           i;

  array = g_ptr_array_sized_new (1);

  g_value_init (&message, BAR_TYPE_DBUS_SET_PROPERTY);
  g_value_take_boxed (&message, dbus_g_type_specialized_construct (G_VALUE_TYPE (&message)));

  /* put properties in a dbus-suitable array for the wrapper */
  for (li = properties; li != NULL; li = li->next)
    {
      property = li->data;

      dbus_g_type_struct_set (&message,
                              DBUS_SET_TYPE, property->type,
                              DBUS_SET_VALUE, &property->value,
                              G_MAXUINT);

      g_ptr_array_add (array, g_value_dup_boxed (&message));
    }

  /* send array to the wrapper */
  g_signal_emit (G_OBJECT (external), external_signals[SET], 0, array);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  for (i = 0; i < array->len; i++)
    g_value_array_free (g_ptr_array_index (array, i));
  G_GNUC_END_IGNORE_DEPRECATIONS
  g_ptr_array_free (array, TRUE);
  g_value_unset (&message);
}



static gboolean
bar_plugin_external_wrapper_remote_event (BarPluginExternal *external,
                                            const gchar         *name,
                                            const GValue        *value,
                                            guint               *handle)
{
  static guint  handle_counter = 0;
  GValue        dummy_value = { 0, };
  const GValue *real_value = value;

  bar_return_val_if_fail (BAR_IS_PLUGIN_EXTERNAL_WRAPPER (external), TRUE);
  bar_return_val_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (external), TRUE);
  bar_return_val_if_fail (value == NULL || G_IS_VALUE (value), FALSE);

  if (G_UNLIKELY (handle_counter > G_MAXUINT - 2))
    handle_counter = 0;
  *handle = ++handle_counter;

  if (value == NULL)
    {
      /* we send a dummy value over dbus */
      g_value_init (&dummy_value, G_TYPE_UCHAR);
      g_value_set_uchar (&dummy_value, '\0');
      real_value = &dummy_value;
    }

  g_signal_emit (G_OBJECT (external), external_signals[REMOTE_EVENT], 0,
                 name, real_value, *handle);

  if (real_value != value)
    g_value_unset (&dummy_value);

  return TRUE;
}



static gboolean
bar_plugin_external_wrapper_dbus_provider_signal (BarPluginExternalWrapper     *external,
                                                    BladeBarPluginProviderSignal   provider_signal,
                                                    GError                        **error)
{
  bar_return_val_if_fail (BAR_IS_PLUGIN_EXTERNAL (external), FALSE);
  bar_return_val_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (external), FALSE);

  switch (provider_signal)
    {
    case PROVIDER_SIGNAL_SHOW_CONFIGURE:
      BAR_PLUGIN_EXTERNAL (external)->show_configure = TRUE;
      break;

    case PROVIDER_SIGNAL_SHOW_ABOUT:
      BAR_PLUGIN_EXTERNAL (external)->show_about = TRUE;
      break;

    default:
      /* other signals are handled in bar-applications.c */
      blade_bar_plugin_provider_emit_signal (BLADE_BAR_PLUGIN_PROVIDER (external),
                                              provider_signal);
      break;
    }

  return TRUE;
}



static gboolean
bar_plugin_external_wrapper_dbus_remote_event_result (BarPluginExternalWrapper  *external,
                                                        guint                        handle,
                                                        gboolean                     result,
                                                        GError                     **error)
{
  bar_return_val_if_fail (BAR_IS_PLUGIN_EXTERNAL (external), FALSE);

  g_signal_emit (G_OBJECT (external), external_signals[REMOTE_EVENT_RESULT], 0,
                 handle, result);

  return TRUE;
}



GtkWidget *
bar_plugin_external_wrapper_new (BarModule  *module,
                                   gint          unique_id,
                                   gchar       **arguments)
{
  bar_return_val_if_fail (BAR_IS_MODULE (module), NULL);
  bar_return_val_if_fail (unique_id != -1, NULL);

  return g_object_new (BAR_TYPE_PLUGIN_EXTERNAL_WRAPPER,
                       "module", module,
                       "unique-id", unique_id,
                       "arguments", arguments, NULL);
}
