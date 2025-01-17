/*
 * Copyright (C) 2008-2009 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <gtk/gtk.h>
#include <common/bar-private.h>
#include <common/bar-dbus.h>
#include <libbladeutil/libbladeutil.h>
#include <libbladebar/libbladebar.h>
#include <libbladebar/blade-bar-plugin-provider.h>

#include <wrapper/wrapper-plug.h>
#include <wrapper/wrapper-module.h>
#include <wrapper/wrapper-dbus-client-infos.h>



static GQuark   plug_quark = 0;
static gboolean gproxy_destroyed = FALSE;
static gint     retval = PLUGIN_EXIT_FAILURE;



static void
wrapper_gproxy_set (DBusGProxy              *dbus_gproxy,
                    const GPtrArray         *array,
                    BladeBarPluginProvider *provider)
{
  WrapperPlug                    *plug;
  guint                           i;
  GValue                         *value;
  BladeBarPluginProviderPropType type;
  GValue                          msg = { 0, };

  bar_return_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (provider));

  g_value_init (&msg, BAR_TYPE_DBUS_SET_PROPERTY);

  for (i = 0; i < array->len; i++)
    {
      g_value_set_static_boxed (&msg, g_ptr_array_index (array, i));
      if (!dbus_g_type_struct_get (&msg,
                                   DBUS_SET_TYPE, &type,
                                   DBUS_SET_VALUE, &value,
                                   G_MAXUINT))
        {
          bar_assert_not_reached ();
          continue;
        }

      switch (type)
        {
        case PROVIDER_PROP_TYPE_SET_SIZE:
          blade_bar_plugin_provider_set_size (provider, g_value_get_int (value));
          break;

        case PROVIDER_PROP_TYPE_SET_MODE:
          blade_bar_plugin_provider_set_mode (provider, g_value_get_int (value));
          break;

        case PROVIDER_PROP_TYPE_SET_SCREEN_POSITION:
          blade_bar_plugin_provider_set_screen_position (provider, g_value_get_int (value));
          break;

        case PROVIDER_PROP_TYPE_SET_NROWS:
          blade_bar_plugin_provider_set_nrows (provider, g_value_get_int (value));
          break;

        case PROVIDER_PROP_TYPE_SET_LOCKED:
          blade_bar_plugin_provider_set_locked (provider, g_value_get_boolean (value));
          break;

        case PROVIDER_PROP_TYPE_SET_SENSITIVE:
          gtk_widget_set_sensitive (GTK_WIDGET (provider), g_value_get_boolean (value));
          break;

        case PROVIDER_PROP_TYPE_SET_BACKGROUND_ALPHA:
        case PROVIDER_PROP_TYPE_SET_BACKGROUND_COLOR:
        case PROVIDER_PROP_TYPE_SET_BACKGROUND_IMAGE:
        case PROVIDER_PROP_TYPE_ACTION_BACKGROUND_UNSET:
          plug = g_object_get_qdata (G_OBJECT (provider), plug_quark);

          if (type == PROVIDER_PROP_TYPE_SET_BACKGROUND_ALPHA)
            wrapper_plug_set_background_alpha (plug, g_value_get_double (value));
          else if (type == PROVIDER_PROP_TYPE_SET_BACKGROUND_COLOR)
            wrapper_plug_set_background_color (plug, g_value_get_string (value));
          else if (type == PROVIDER_PROP_TYPE_SET_BACKGROUND_IMAGE)
            wrapper_plug_set_background_image (plug, g_value_get_string (value));
          else /* PROVIDER_PROP_TYPE_ACTION_BACKGROUND_UNSET */
            wrapper_plug_set_background_color (plug, NULL);
          break;

        case PROVIDER_PROP_TYPE_ACTION_REMOVED:
          blade_bar_plugin_provider_removed (provider);
          break;

        case PROVIDER_PROP_TYPE_ACTION_SAVE:
          blade_bar_plugin_provider_save (provider);
          break;

        case PROVIDER_PROP_TYPE_ACTION_QUIT_FOR_RESTART:
          retval = PLUGIN_EXIT_SUCCESS_AND_RESTART;
        case PROVIDER_PROP_TYPE_ACTION_QUIT:
          gtk_main_quit ();
          break;

        case PROVIDER_PROP_TYPE_ACTION_SHOW_CONFIGURE:
          blade_bar_plugin_provider_show_configure (provider);
          break;

        case PROVIDER_PROP_TYPE_ACTION_SHOW_ABOUT:
          blade_bar_plugin_provider_show_about (provider);
          break;

        case PROVIDER_PROP_TYPE_ACTION_ASK_REMOVE:
          blade_bar_plugin_provider_ask_remove (provider);
          break;

        default:
          bar_assert_not_reached ();
          break;
        }

      g_value_unset (value);
      g_free (value);
    }
}



static void
wrapper_gproxy_remote_event (DBusGProxy              *dbus_gproxy,
                             const gchar             *name,
                             const GValue            *value,
                             guint                    handle,
                             BladeBarPluginProvider *provider)
{
  const GValue *real_value;
  gboolean      result;

  bar_return_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (provider));

  if (G_VALUE_HOLDS_UCHAR (value)
     && g_value_get_uchar (value) == '\0')
    real_value = NULL;
  else
    real_value = value;

  result = blade_bar_plugin_provider_remote_event (provider, name, real_value, NULL);

  wrapper_dbus_remote_event_result (dbus_gproxy, handle, result, NULL);
}



static void
wrapper_marshal_VOID__STRING_BOXED_UINT (GClosure     *closure,
                                         GValue       *return_value G_GNUC_UNUSED,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint G_GNUC_UNUSED,
                                         gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__STRING_BOXED_UINT) (gpointer     data1,
                                                        gpointer     arg_1,
                                                        gpointer     arg_2,
                                                        guint        arg_3,
                                                        gpointer     data2);
  register GMarshalFunc_VOID__STRING_BOXED_UINT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  bar_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }

  callback = (GMarshalFunc_VOID__STRING_BOXED_UINT) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            g_value_peek_pointer (param_values + 1),
            g_value_peek_pointer (param_values + 2),
            g_value_get_uint (param_values + 3),
            data2);
}



static void
wrapper_gproxy_provider_signal (BladeBarPluginProvider       *provider,
                                BladeBarPluginProviderSignal  provider_signal,
                                DBusGProxy                    *dbus_gproxy)
{
  bar_return_if_fail (BLADE_IS_BAR_PLUGIN_PROVIDER (provider));

  /* send the provider signal to the bar */
  wrapper_dbus_provider_signal (dbus_gproxy, provider_signal, NULL);
}



static void
wrapper_gproxy_destroyed (DBusGProxy *dbus_gproxy)
{
  /* we lost communication with the bar, silently close the wrapper */
  gproxy_destroyed = TRUE;

  gtk_main_quit ();
}



gint
main (gint argc, gchar **argv)
{
#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
  gchar                    process_name[16];
#endif
  GModule                 *library = NULL;
  BladeBarPluginPreInit   preinit_func;
  DBusGConnection         *dbus_gconnection;
  DBusGProxy              *dbus_gproxy = NULL;
  WrapperModule           *module = NULL;
  WrapperPlug             *plug;
  GtkWidget               *provider;
  gchar                   *path;
  guint                    gproxy_destroy_id = 0;
  GError                  *error = NULL;
  const gchar             *filename;
  gint                     unique_id;
#if GTK_CHECK_VERSION (3, 0, 0)
  Window                   socket_id;
#else
  GdkNativeWindow          socket_id;
#endif
  const gchar             *name;
  const gchar             *display_name;
  const gchar             *comment;
  gchar                  **arguments;

  /* set translation domain */
  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

#ifdef G_ENABLE_DEBUG
  /* terminate the program on warnings and critical messages */
  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
#endif

  /* check if we have all the reuiqred arguments */
  if (G_UNLIKELY (argc < PLUGIN_ARGV_ARGUMENTS))
    {
      g_critical ("Not enough arguments are passed to the wrapper");
      return PLUGIN_EXIT_ARGUMENTS_FAILED;
    }

  /* put all arguments in understandable strings */
  filename = argv[PLUGIN_ARGV_FILENAME];
  unique_id = strtol (argv[PLUGIN_ARGV_UNIQUE_ID], NULL, 0);
  socket_id = strtol (argv[PLUGIN_ARGV_SOCKET_ID], NULL, 0);
  name = argv[PLUGIN_ARGV_NAME];
  display_name = argv[PLUGIN_ARGV_DISPLAY_NAME];
  comment = argv[PLUGIN_ARGV_COMMENT];
  arguments = argv + PLUGIN_ARGV_ARGUMENTS;

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
  /* change the process name to something that makes sence */
  g_snprintf (process_name, sizeof (process_name), "bar-%d-%s",
              unique_id, name);
  if (prctl (PR_SET_NAME, (gulong) process_name, 0, 0, 0) == -1)
    g_warning ("Failed to change the process name to \"%s\".", process_name);
#endif

  /* open the plugin module */
  library = g_module_open (filename, G_MODULE_BIND_LOCAL);
  if (G_UNLIKELY (library == NULL))
    {
      g_set_error (&error, 0, 0, "Failed to open plugin module \"%s\": %s",
                   filename, g_module_error ());
      goto leave;
    }

  /* check for a plugin preinit function */
  if (g_module_symbol (library, "blade_bar_module_preinit", (gpointer) &preinit_func)
      && preinit_func != NULL
      && (*preinit_func) (argc, argv) == FALSE)
    {
      retval = PLUGIN_EXIT_PREINIT_FAILED;
      goto leave;
    }

  gtk_init (&argc, &argv);

  /* connect the dbus proxy */
  dbus_gconnection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (G_UNLIKELY (dbus_gconnection == NULL))
    goto leave;

  path = g_strdup_printf (BAR_DBUS_WRAPPER_PATH, unique_id);
  dbus_gproxy = dbus_g_proxy_new_for_name_owner (dbus_gconnection,
                                                 BAR_DBUS_NAME,
                                                 path,
                                                 BAR_DBUS_WRAPPER_INTERFACE,
                                                 &error);
  g_free (path);
  if (G_UNLIKELY (dbus_gproxy == NULL))
    goto leave;

  /* quit when the proxy is destroyed (bar segfault for example) */
  gproxy_destroy_id = g_signal_connect (G_OBJECT (dbus_gproxy), "destroy",
      G_CALLBACK (wrapper_gproxy_destroyed), NULL);

  /* create the type module */
  module = wrapper_module_new (library);

  /* create the plugin provider */
  provider = wrapper_module_new_provider (module,
                                          gdk_screen_get_default (),
                                          name, unique_id,
                                          display_name, comment,
                                          arguments);

  if (G_LIKELY (provider != NULL))
    {
      /* create the wrapper plug */
      plug = wrapper_plug_new (socket_id);
      gtk_container_add (GTK_CONTAINER (plug), GTK_WIDGET (provider));
      g_object_add_weak_pointer (G_OBJECT (plug), (gpointer *) &plug);
      gtk_widget_show (GTK_WIDGET (plug));

      /* set plug data to provider */
      plug_quark = g_quark_from_static_string ("plug-quark");
      g_object_set_qdata (G_OBJECT (provider), plug_quark, plug);

      /* monitor provider signals */
      g_signal_connect (G_OBJECT (provider), "provider-signal",
          G_CALLBACK (wrapper_gproxy_provider_signal), dbus_gproxy);

      /* connect to service signals */
      dbus_g_proxy_add_signal (dbus_gproxy, "Set",
          BAR_TYPE_DBUS_SET_SIGNAL, G_TYPE_INVALID);
      dbus_g_proxy_connect_signal (dbus_gproxy, "Set",
          G_CALLBACK (wrapper_gproxy_set), g_object_ref (provider),
          (GClosureNotify) g_object_unref);

      dbus_g_object_register_marshaller (wrapper_marshal_VOID__STRING_BOXED_UINT,
          G_TYPE_NONE, G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_UINT, G_TYPE_INVALID);
      dbus_g_proxy_add_signal (dbus_gproxy, "RemoteEvent",
          G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_UINT, G_TYPE_INVALID);
      dbus_g_proxy_connect_signal (dbus_gproxy, "RemoteEvent",
          G_CALLBACK (wrapper_gproxy_remote_event), g_object_ref (provider),
          (GClosureNotify) g_object_unref);

      /* show the plugin */
      gtk_widget_show (GTK_WIDGET (provider));

      gtk_main ();

      /* disconnect signals */
      if (!gproxy_destroyed)
        {
          dbus_g_proxy_disconnect_signal (dbus_gproxy, "Set",
              G_CALLBACK (wrapper_gproxy_set), provider);
          dbus_g_proxy_disconnect_signal (dbus_gproxy, "RemoteEvent",
              G_CALLBACK (wrapper_gproxy_remote_event), provider);
        }

      /* destroy the plug and provider */
      if (plug != NULL)
        gtk_widget_destroy (GTK_WIDGET (plug));

      if (retval != PLUGIN_EXIT_SUCCESS_AND_RESTART)
        retval = PLUGIN_EXIT_SUCCESS;
    }
  else
    {
      retval = PLUGIN_EXIT_NO_PROVIDER;
    }

leave:
  if (G_LIKELY (dbus_gproxy != NULL))
    {
      if (G_LIKELY (gproxy_destroy_id != 0 && !gproxy_destroyed))
        g_signal_handler_disconnect (G_OBJECT (dbus_gproxy), gproxy_destroy_id);

      g_object_unref (G_OBJECT (dbus_gproxy));
    }

  if (G_LIKELY (module != NULL))
    g_object_unref (G_OBJECT (module));

  if (G_LIKELY (library != NULL))
    g_module_close (library);

  if (G_UNLIKELY (error != NULL))
    {
      g_critical ("Wrapper %s-%d: %s.", name,
                  unique_id, error->message);
      g_error_free (error);
    }

  return retval;
}
