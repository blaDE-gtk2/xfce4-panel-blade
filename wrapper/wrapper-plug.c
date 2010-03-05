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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <wrapper/wrapper-plug.h>
#include <common/panel-private.h>



static gboolean wrapper_plug_expose_event (GtkWidget      *widget,
                                           GdkEventExpose *event);



struct _WrapperPlugClass
{
  GtkPlugClass __parent__;
};

struct _WrapperPlug
{
  GtkPlug __parent__;

  /* background alpha */
  gdouble background_alpha;

  /* whether the wrapper has a rgba colormap */
  guint   is_composited : 1;
};



/* shared internal plugin name */
gchar *wrapper_name = NULL;



G_DEFINE_TYPE (WrapperPlug, wrapper_plug, GTK_TYPE_PLUG)



static void
wrapper_plug_class_init (WrapperPlugClass *klass)
{
  GtkWidgetClass *gtkwidget_class;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->expose_event = wrapper_plug_expose_event;
}



static void
wrapper_plug_init (WrapperPlug *plug)
{
  GdkColormap *colormap = NULL;
  GdkScreen   *screen;

  plug->background_alpha = 1.00;

  gtk_widget_set_name (GTK_WIDGET (plug), "XfcePanelWindowWrapper");

  /* allow painting, else compositing won't work */
  gtk_widget_set_app_paintable (GTK_WIDGET (plug), TRUE);

  /* old versions of gtk don't support transparent tray icons, if we
   * set an argb colormap on the tray, the icons won't be embedded because
   * the socket-plugin implementation requires identical colormaps */
  if (gtk_check_version (2, 16, 0) != NULL
      && strcmp (wrapper_name, "systray") == 0)
    return;

  /* set the colormap */
  screen = gtk_window_get_screen (GTK_WINDOW (plug));
  plug->is_composited = gtk_widget_is_composited (GTK_WIDGET (plug));

  if (plug->is_composited)
    colormap = gdk_screen_get_rgba_colormap (screen);
  if (colormap == NULL)
    {
      colormap = gdk_screen_get_rgb_colormap (screen);
      plug->is_composited = FALSE;
    }

  if (colormap != NULL)
    gtk_widget_set_colormap (GTK_WIDGET (plug), colormap);
}



static gboolean
wrapper_plug_expose_event (GtkWidget      *widget,
                           GdkEventExpose *event)
{
  WrapperPlug   *plug = WRAPPER_PLUG (widget);
  cairo_t       *cr;
  GdkColor      *color;
  GtkStateType   state = GTK_STATE_NORMAL;
  gdouble        alpha = plug->is_composited ? plug->background_alpha : 1.00;

  if (GTK_WIDGET_DRAWABLE (widget) && alpha < 1.00)
    {
      /* create the cairo context */
      cr = gdk_cairo_create (widget->window);

      /* get the background gdk color */
      color = &(widget->style->bg[state]);

      /* set the cairo source color */
      cairo_set_source_rgba (cr, PANEL_GDKCOLOR_TO_DOUBLE (color),
                             plug->background_alpha);

      /* create retangle */
      cairo_rectangle (cr, event->area.x, event->area.y,
                       event->area.width, event->area.height);

      /* draw on source */
      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

      /* paint rectangle */
      cairo_fill (cr);

      /* destroy cairo context */
      cairo_destroy (cr);
    }

  return GTK_WIDGET_CLASS (wrapper_plug_parent_class)->expose_event (widget, event);
}



WrapperPlug *
wrapper_plug_new (GdkNativeWindow socket_id)
{
  WrapperPlug *plug;

  /* create new object */
  plug = g_object_new (WRAPPER_TYPE_PLUG, NULL);

  /* contruct the plug */
  gtk_plug_construct (GTK_PLUG (plug), socket_id);

  return plug;
}



void
wrapper_plug_set_background_alpha (WrapperPlug *plug,
                                   gdouble      alpha)
{
  panel_return_if_fail (WRAPPER_IS_PLUG (plug));
  panel_return_if_fail (GTK_IS_WIDGET (plug));

  /* set the alpha */
  plug->background_alpha = CLAMP (alpha, 0.00, 1.00);

  /* redraw */
  if (plug->is_composited)
    gtk_widget_queue_draw (GTK_WIDGET (plug));
}
