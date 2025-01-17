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

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <libbladeutil/libbladeutil.h>
#include <libbladeui/libbladeui.h>
#include <libwnck/libwnck.h>

#include <common/bar-private.h>
#include <common/bar-debug.h>
#include <libbladebar/libbladebar.h>
#include <bar/bar-application.h>
#include <bar/bar-dbus-service.h>
#include <bar/bar-dbus-client.h>
#include <bar/bar-preferences-dialog.h>



static gint       opt_preferences = -1;
static gint       opt_add_items = -1;
static gboolean   opt_save = FALSE;
static gchar     *opt_add = NULL;
static gboolean   opt_restart = FALSE;
static gboolean   opt_quit = FALSE;
static gboolean   opt_version = FALSE;
static gboolean   opt_disable_wm_check = FALSE;
static gchar     *opt_plugin_event = NULL;
static gchar    **opt_arguments = NULL;
static gchar     *opt_socket_id = NULL;



static gboolean bar_callback_handler (const gchar  *name,
                                        const gchar  *value,
                                        gpointer      user_data,
                                        GError      **error);



/* command line options */
#define BAR_CALLBACK_OPTION G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, bar_callback_handler
static GOptionEntry option_entries[] =
{
  { "preferences", 'p', BAR_CALLBACK_OPTION, N_("Show the 'Bar Preferences' dialog"), N_("BAR-NUMBER") },
  { "add-items", 'a', BAR_CALLBACK_OPTION, N_("Show the 'Add New Items' dialog"), N_("BAR-NUMBER") },
  { "save", 's', 0, G_OPTION_ARG_NONE, &opt_save, N_("Save the bar configuration"), NULL },
  { "add", '\0', 0, G_OPTION_ARG_STRING, &opt_add, N_("Add a new plugin to the bar"), N_("PLUGIN-NAME") },
  { "restart", 'r', 0, G_OPTION_ARG_NONE, &opt_restart, N_("Restart the running bar instance"), NULL },
  { "quit", 'q', 0, G_OPTION_ARG_NONE, &opt_quit, N_("Quit the running bar instance"), NULL },
  { "disable-wm-check", 'd', 0, G_OPTION_ARG_NONE, &opt_disable_wm_check, N_("Do not wait for a window manager on startup"), NULL },
  { "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version, N_("Print version information and exit"), NULL },
  { "plugin-event", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &opt_plugin_event, NULL, NULL },
  { "socket-id", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &opt_socket_id, NULL, NULL },
  { G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &opt_arguments, NULL, NULL },
  { NULL }
};



static gboolean
bar_callback_handler (const gchar  *name,
                        const gchar  *value,
                        gpointer      user_data,
                        GError      **error)
{
  bar_return_val_if_fail (name != NULL, FALSE);

  if (strcmp (name, "--preferences") == 0
      || strcmp (name, "-p") == 0)
    {
      opt_preferences = value != NULL ? MAX (0, atoi (value)) : 0;
    }
  else if (strcmp (name, "--add-items") == 0
           || strcmp (name, "-a") == 0)
    {
      opt_add_items = value != NULL ? MAX (0, atoi (value)) : 0;
    }
  else
    {
      bar_assert_not_reached ();
      return FALSE;
    }

  return TRUE;
}



static void
bar_signal_handler (gint signum)
{
  static gboolean was_triggered = FALSE;

  /* avoid recursing this handler if we receive a
   * signal before the mainloop is started */
  if (was_triggered)
    return;
  was_triggered = TRUE;

  bar_debug (BAR_DEBUG_MAIN,
               "received signal %s <%d>, %s bar",
               g_strsignal (signum), signum,
               signum == SIGUSR1 ? "restarting" : "quitting");

  bar_dbus_service_exit_bar (signum == SIGUSR1);
}



static void
bar_sm_client_quit (XfceSMClient *sm_client)
{
  bar_return_if_fail (XFCE_IS_SM_CLIENT (sm_client));
  bar_return_if_fail (!bar_dbus_service_get_restart ());

  bar_debug (BAR_DEBUG_MAIN,
               "terminate bar for session manager");

  gtk_main_quit ();
}



static void
bar_debug_notify_proxy (void)
{
  gchar       *path;
  const gchar *proxy_cmd;

  if (G_UNLIKELY (bar_debug_has_domain (BAR_DEBUG_GDB)))
    proxy_cmd = "gdb";
  else if (G_UNLIKELY (bar_debug_has_domain (BAR_DEBUG_VALGRIND)))
    proxy_cmd = "valgrind";
  else
    return;

  path = g_find_program_in_path (proxy_cmd);
  if (G_LIKELY (path != NULL))
    {
      bar_debug (BAR_DEBUG_MAIN,
                   "running external plugins with %s, logs stored in %s",
                   path, g_get_tmp_dir ());
      g_free (path);

      if (bar_debug_has_domain (BAR_DEBUG_GDB))
        {
          /* performs sanity checks on the released memory slices */
          g_setenv ("G_SLICE", "debug-blocks", TRUE);
        }
      else if (bar_debug_has_domain (BAR_DEBUG_VALGRIND))
        {
          /* use g_malloc() and g_free() instead of slices */
          g_setenv ("G_SLICE", "always-malloc", TRUE);
          g_setenv ("G_DEBUG", "gc-friendly", TRUE);
        }
    }
  else
    {
      bar_debug (BAR_DEBUG_MAIN, "%s not found in PATH", proxy_cmd);
    }
}



gint
main (gint argc, gchar **argv)
{
  GOptionContext   *context;
  BarApplication *application;
  GError           *error = NULL;
  BarDBusService *dbus_service;
  gboolean          succeed = FALSE;
  gboolean          remote_succeed;
  guint             i;
  const gint        signums[] = { SIGINT, SIGQUIT, SIGTERM, SIGABRT, SIGUSR1 };
  const gchar      *error_msg;
  XfceSMClient     *sm_client;

  bar_debug (BAR_DEBUG_MAIN,
               "version %s on gtk+ %d.%d.%d (%d.%d.%d), glib %d.%d.%d (%d.%d.%d)",
               LIBBLADEBAR_VERSION,
               gtk_major_version, gtk_minor_version, gtk_micro_version,
               GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION,
               glib_major_version, glib_minor_version, glib_micro_version,
               GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);

  /* inform the user about usage of gdb/valgrind */
  bar_debug_notify_proxy ();

  /* set translation domain */
  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

#ifdef G_ENABLE_DEBUG
  /* do NOT remove this line for now, If something doesn't work,
   * fix your code instead! */
  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
#endif

  /* parse context options */
  context = g_option_context_new (_("[ARGUMENTS...]"));
  g_option_context_add_main_entries (context, option_entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  g_option_context_add_group (context, xfce_sm_client_get_option_group (argc, argv));
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("%s: %s.\n", PACKAGE_NAME, error->message);
      g_print (_("Type \"%s --help\" for usage."), G_LOG_DOMAIN);
      g_print ("\n");
      g_error_free (error);

      return EXIT_FAILURE;
    }
  g_option_context_free (context);

  gtk_init (&argc, &argv);

  if (opt_version)
    {
      /* print version information */
      if (opt_arguments != NULL && *opt_arguments != NULL)
        g_print ("%s (%s)", *opt_arguments, PACKAGE_NAME);
      else
        g_print ("%s", PACKAGE_NAME);
      g_print (" %s (Xfce %s)\n\n", PACKAGE_VERSION, xfce_version_string ());
      g_print ("%s\n", "Copyright (c) 2004-2011");
      g_print ("\t%s\n\n", _("The Xfce development team. All rights reserved."));
      g_print (_("Please report bugs to <%s>."), PACKAGE_BUGREPORT);
      g_print ("\n");

      return EXIT_SUCCESS;
    }
  else if (opt_preferences >= 0)
    {
      /* send a signal to the running instance to show the preferences dialog */
      succeed = bar_dbus_client_display_preferences_dialog (opt_preferences, opt_socket_id, &error);
      goto dbus_return;
    }
  else if (opt_add_items >= 0)
    {
      /* send a signal to the running instance to show the add items dialog */
      succeed = bar_dbus_client_display_items_dialog (opt_add_items, &error);
      goto dbus_return;
    }
  else if (opt_save)
    {
      /* send a save signal to the running instance */
      succeed = bar_dbus_client_save (&error);
      goto dbus_return;
    }
  else if (opt_add != NULL)
    {
      /* send a add-new-item signal to the running instance */
      succeed = bar_dbus_client_add_new_item (opt_add, opt_arguments, &error);
      goto dbus_return;
    }
  else if (opt_restart || opt_quit)
    {
      /* send a terminate signal to the running instance */
      succeed = bar_dbus_client_terminate (opt_restart, &error);
      goto dbus_return;
    }
  else if (opt_plugin_event != NULL)
    {
      /* send the plugin event to the running instance */
      remote_succeed = FALSE;
      succeed = bar_dbus_client_plugin_event (opt_plugin_event, &remote_succeed, &error);

      /* the bar returns EXIT_FAILURE if the dbus event succeeds, but
       * no suitable plugin was found on the service side */
      if (succeed && !remote_succeed)
        succeed = FALSE;

      goto dbus_return;
    }

  launch_bar:

  /* start dbus service */
  dbus_service = bar_dbus_service_get ();
  if (!bar_dbus_service_is_owner (dbus_service))
    {
      /* quit without error if an instance is running */
      succeed = TRUE;

      g_print ("%s: %s\n\n", G_LOG_DOMAIN, _("There is already a running instance"));
      goto dbus_return;
    }

  /* start session management */
  sm_client = xfce_sm_client_get ();
  xfce_sm_client_set_restart_style (sm_client, XFCE_SM_CLIENT_RESTART_IMMEDIATELY);
  xfce_sm_client_set_priority (sm_client, XFCE_SM_CLIENT_PRIORITY_CORE);
  g_signal_connect (G_OBJECT (sm_client), "quit",
      G_CALLBACK (bar_sm_client_quit), NULL);
  if (!xfce_sm_client_connect (sm_client, &error))
    {
      g_printerr ("%s: Failed to connect to session manager: %s\n",
                  G_LOG_DOMAIN, error->message);
      g_clear_error (&error);
    }

  /* setup signal handlers to properly quit the main loop */
  for (i = 0; i < G_N_ELEMENTS (signums); i++)
    signal (signums[i], bar_signal_handler);

  /* set EWMH source indication */
  wnck_set_client_type (WNCK_CLIENT_TYPE_PAGER);

  application = bar_application_get ();
  bar_application_load (application, opt_disable_wm_check);

  /* open dialog if we started from launch_bar */
  if (opt_preferences >= 0)
    bar_preferences_dialog_show_from_id (opt_preferences, opt_socket_id);

  gtk_main ();

  /* make sure there are no incomming events when we close */
  g_object_unref (G_OBJECT (dbus_service));

  /* destroy all the opened dialogs */
  bar_application_destroy_dialogs (application);

  g_object_unref (G_OBJECT (application));
  g_object_unref (G_OBJECT (sm_client));

  if (bar_dbus_service_get_restart ())
    {
      /* spawn ourselfs again */
      g_print ("%s: %s\n\n", G_LOG_DOMAIN, _("Restarting..."));
      g_spawn_command_line_async (argv[0], NULL);
    }

  return EXIT_SUCCESS;

dbus_return:

  /* stop any running startup notification */
  gdk_notify_startup_complete ();

  if (G_UNLIKELY (error != NULL))
    {
      /* get suitable error message */
      if (opt_preferences >= 0)
        error_msg = _("Failed to show the preferences dialog");
      else if (opt_add_items >= 0)
        error_msg = _("Failed to show the add new items dialog");
      else if (opt_save)
        error_msg = _("Failed to save the bar configuration");
      else if (opt_add)
        error_msg = _("Failed to add a plugin to the bar");
      else if (opt_restart)
        error_msg = _("Failed to restart the bar");
      else if (opt_quit)
        error_msg = _("Failed to quit the bar");
      else
        error_msg = _("Failed to send D-Bus message");

      /* show understandable message for this common error */
      if (error->code == DBUS_GERROR_NAME_HAS_NO_OWNER)
        {
          /* normally start the bar */
          if (opt_preferences >= 0 || opt_restart)
            {
              g_clear_error (&error);

              if (xfce_dialog_confirm (NULL, GTK_STOCK_EXECUTE, NULL,
                                       _("Do you want to start the bar? If you do, make sure "
                                         "you save the session on logout, so the bar is "
                                         "automatically started the next time you login."),
                                       _("No running instance of %s was found"), G_LOG_DOMAIN))
                {
                  bar_debug (BAR_DEBUG_MAIN, "user confirmed to start the bar");
                  goto launch_bar;
                }
              else
                {
                  return EXIT_FAILURE;
                }
            }
          else
            {
              /* I18N: %s is replaced with blade-bar */
              g_clear_error (&error);
              g_set_error (&error, 0, 0, _("No running instance of %s was found"), G_LOG_DOMAIN);
            }
        }

      /* show error dialog */
      xfce_dialog_show_error (NULL, error, "%s", error_msg);
      g_error_free (error);
    }

  return succeed ? EXIT_SUCCESS : EXIT_FAILURE;
}
