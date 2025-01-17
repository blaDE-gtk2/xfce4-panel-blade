/*
 * Copyright (C) 2008-2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
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
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>
#include <libbladeutil/libbladeutil.h>

#include <common/bar-private.h>
#include <libbladebar/blade-bar-macros.h>
#include <libbladebar/blade-bar-image.h>
#include <libbladebar/blade-bar-convenience.h>
#include <libbladebar/libbladebar-alias.h>



/**
 * SECTION: blade-bar-image
 * @title: BladeBarImage
 * @short_description: Scalable image suitable for bar plugins
 * @include: libbladebar/libbladebar.h
 *
 * The #BladeBarImage is a widgets suitable for for example bar
 * buttons where the developer does not exacly know the size of the
 * image (due to theming and user setting).
 *
 * The #BladeBarImage widget automatically scales to the allocated
 * size of the widget. Because of that nature it never requests a size,
 * so this will only work if you pack the image in another widget
 * that will expand it.
 * If you want to force an image size you can use blade_bar_image_set_size()
 * to set a pixel size, in that case the widget will request an fixed size
 * which makes it usefull for usage in dialogs.
 **/



/* design limit for the bar, to reduce the uncached pixbuf size */
#define MAX_PIXBUF_SIZE (128)

#define blade_bar_image_unref_null(obj)   G_STMT_START { if ((obj) != NULL) \
                                             { \
                                               g_object_unref (G_OBJECT (obj)); \
                                               (obj) = NULL; \
                                             } } G_STMT_END



struct _BladeBarImagePrivate
{
  /* pixbuf set by the user */
  GdkPixbuf *pixbuf;

  /* internal cached pixbuf (resized) */
  GdkPixbuf *cache;

  /* source name */
  gchar     *source;

  /* fixed size */
  gint       size;

  /* whether we round to fixed icon sizes */
  guint      force_icon_sizes : 1;

  /* cached width and height */
  gint       width;
  gint       height;

  /* idle load timeout */
  guint      idle_load_id;
};

enum
{
  PROP_0,
  PROP_SOURCE,
  PROP_PIXBUF,
  PROP_SIZE
};



static void       blade_bar_image_get_property         (GObject         *object,
                                                         guint            prop_id,
                                                         GValue          *value,
                                                         GParamSpec      *pspec);
static void       blade_bar_image_set_property         (GObject         *object,
                                                         guint            prop_id,
                                                         const GValue    *value,
                                                         GParamSpec      *pspec);
static void       blade_bar_image_finalize             (GObject         *object);
#if GTK_CHECK_VERSION (3, 0, 0)
static void       blade_bar_image_get_preferred_width  (GtkWidget       *widget,
                                                         gint            *minimum_width,
                                                         gint            *natural_width);
static void       blade_bar_image_get_preferred_height (GtkWidget       *widget,
                                                         gint            *minimum_height,
                                                         gint            *natural_height);
#else
static void       blade_bar_image_size_request         (GtkWidget       *widget,
                                                         GtkRequisition  *requisition);
#endif
static void       blade_bar_image_size_allocate        (GtkWidget       *widget,
                                                         GtkAllocation   *allocation);
#if GTK_CHECK_VERSION (3, 0, 0)
static gboolean   blade_bar_image_draw                 (GtkWidget       *widget,
                                                         cairo_t         *cr);
#else
static gboolean   blade_bar_image_expose_event         (GtkWidget       *widget,
                                                         GdkEventExpose  *event);
#endif
#if GTK_CHECK_VERSION (3, 0, 0)
static void       blade_bar_image_style_updated        (GtkWidget       *widget);
#else
static void       blade_bar_image_style_set            (GtkWidget       *widget,
                                                         GtkStyle        *previous_style);
#endif
static gboolean   blade_bar_image_load                 (gpointer         data);
static void       blade_bar_image_load_destroy         (gpointer         data);
static GdkPixbuf *blade_bar_image_scale_pixbuf         (GdkPixbuf       *source,
                                                         gint             dest_width,
                                                         gint             dest_height);



G_DEFINE_TYPE (BladeBarImage, blade_bar_image, GTK_TYPE_WIDGET)



static void
blade_bar_image_class_init (BladeBarImageClass *klass)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  g_type_class_add_private (klass, sizeof (BladeBarImagePrivate));

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = blade_bar_image_set_property;
  gobject_class->get_property = blade_bar_image_get_property;
  gobject_class->finalize = blade_bar_image_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
#if GTK_CHECK_VERSION (3, 0, 0)
  gtkwidget_class->get_preferred_width = blade_bar_image_get_preferred_width;
  gtkwidget_class->get_preferred_height = blade_bar_image_get_preferred_height;
#else
  gtkwidget_class->size_request = blade_bar_image_size_request;
#endif
  gtkwidget_class->size_allocate = blade_bar_image_size_allocate;
#if GTK_CHECK_VERSION (3, 0, 0)
  gtkwidget_class->draw = blade_bar_image_draw;
  gtkwidget_class->style_updated = blade_bar_image_style_updated;
#else
  gtkwidget_class->expose_event = blade_bar_image_expose_event;
  gtkwidget_class->style_set = blade_bar_image_style_set;
#endif

  g_object_class_install_property (gobject_class,
                                   PROP_SOURCE,
                                   g_param_spec_string ("source",
                                                        "Source",
                                                        "Icon or filename",
                                                        NULL,
                                                        G_PARAM_READWRITE
                                                        | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_PIXBUF,
                                   g_param_spec_object ("pixbuf",
                                                        "Pixbuf",
                                                        "Pixbuf image",
                                                        GDK_TYPE_PIXBUF,
                                                        G_PARAM_READWRITE
                                                        | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_SIZE,
                                   g_param_spec_int ("size",
                                                     "Size",
                                                     "Pixel size of the image",
                                                     -1, MAX_PIXBUF_SIZE, -1,
                                                     G_PARAM_READWRITE
                                                     | G_PARAM_STATIC_STRINGS));

  gtk_widget_class_install_style_property (gtkwidget_class,
                                           g_param_spec_boolean ("force-gtk-icon-sizes",
                                                                 NULL,
                                                                 "Force the image to fix to GtkIconSizes",
                                                                 FALSE,
                                                                 G_PARAM_READWRITE
                                                                 | G_PARAM_STATIC_STRINGS));
}



static void
blade_bar_image_init (BladeBarImage *image)
{
  gtk_widget_set_has_window (GTK_WIDGET (image), FALSE);

  image->priv = G_TYPE_INSTANCE_GET_PRIVATE (image, XFCE_TYPE_BAR_IMAGE, BladeBarImagePrivate);

  image->priv->pixbuf = NULL;
  image->priv->cache = NULL;
  image->priv->source = NULL;
  image->priv->size = -1;
  image->priv->width = -1;
  image->priv->height = -1;
  image->priv->force_icon_sizes = FALSE;
}



static void
blade_bar_image_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (object)->priv;

  switch (prop_id)
    {
    case PROP_SOURCE:
      g_value_set_string (value, priv->source);
      break;

    case PROP_PIXBUF:
      g_value_set_object (value, priv->pixbuf);
      break;

    case PROP_SIZE:
      g_value_set_int (value, priv->size);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
blade_bar_image_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_SOURCE:
      blade_bar_image_set_from_source (BLADE_BAR_IMAGE (object),
                                        g_value_get_string (value));
      break;

    case PROP_PIXBUF:
      blade_bar_image_set_from_pixbuf (BLADE_BAR_IMAGE (object),
                                        g_value_get_object (value));
      break;

    case PROP_SIZE:
      blade_bar_image_set_size (BLADE_BAR_IMAGE (object),
                                 g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
blade_bar_image_finalize (GObject *object)
{
  blade_bar_image_clear (BLADE_BAR_IMAGE (object));

  (*G_OBJECT_CLASS (blade_bar_image_parent_class)->finalize) (object);
}



//#if GTK_CHECK_VERSION (3, 0, 0) && !GTK_CHECK_VERSION (3, 10, 0)
#if GTK_CHECK_VERSION (3, 0, 0)
#define GTK_BUTTON_SIZING_FIX
#endif

#ifdef GTK_BUTTON_SIZING_FIX
/* When can_focus is true, GtkButton allocates larger size than requested *
 * and causes the bar image to grow indefinitely.                       *
 * This workaround compensates for this difference.                       *
 * Details in https://bugzilla.gnome.org/show_bug.cgi?id=698030           *
 */
static gint
blade_bar_image_padding_correction (GtkWidget *widget)
{
  GtkWidget             *parent;
  GtkStyleContext       *context;
  gint                   focus_width;
  gint                   focus_pad;
  gint                   correction;

  parent = gtk_widget_get_parent (widget);
  if (parent != NULL &&
      GTK_IS_BUTTON (parent) &&
      !gtk_widget_get_can_focus (parent))
    {
      context = gtk_widget_get_style_context (parent);
      gtk_style_context_get_style (context,
                                   "focus-line-width", &focus_width,
                                   "focus-padding", &focus_pad,
                                   NULL);
      correction = (focus_width + focus_pad) * 2;
    }
  else
    {
      correction = 0;
    }

  return correction;
}
#endif


#if GTK_CHECK_VERSION (3, 0, 0)
static void
blade_bar_image_get_preferred_width (GtkWidget *widget,
                                      gint      *minimum_width,
                                      gint      *natural_width)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  GtkAllocation          alloc;
  gint                   width, width_min;

  if (priv->size > 0)
    width = priv->size;
  else if (priv->pixbuf != NULL)
    width = gdk_pixbuf_get_width (priv->pixbuf);
  else
    {
      gtk_widget_get_allocation (widget, &alloc);
      width = alloc.width;
    }

#ifdef GTK_BUTTON_SIZING_FIX
  width -= blade_bar_image_padding_correction (widget);
  width = MAX (width, 0);
#endif

  if (priv->size > 0)
    width_min = width;
  else
    width_min = 0;

  if (minimum_width != NULL)
    *minimum_width = width_min;

  if (natural_width != NULL)
    *natural_width = width;
}



static void
blade_bar_image_get_preferred_height (GtkWidget *widget,
                                       gint      *minimum_height,
                                       gint      *natural_height)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  GtkAllocation          alloc;
  gint                   height, height_min;

  if (priv->size > 0)
    height = height_min = priv->size;
  else if (priv->pixbuf != NULL)
    height = gdk_pixbuf_get_height (priv->pixbuf);
  else
    {
      gtk_widget_get_allocation (widget, &alloc);
      height = alloc.height;
    }

#ifdef GTK_BUTTON_SIZING_FIX
  height -= blade_bar_image_padding_correction (widget);
  height = MAX (height, 0);
#endif

  if (priv->size > 0)
    height_min = height;
  else
    height_min = 0;

  if (minimum_height != NULL)
    *minimum_height = height_min;

  if (natural_height != NULL)
    *natural_height = height;
}
#endif



#if !GTK_CHECK_VERSION (3, 0, 0)
static void
blade_bar_image_size_request (GtkWidget      *widget,
                               GtkRequisition *requisition)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;

  if (priv->size > 0)
    {
      requisition->width = priv->size;
      requisition->height = priv->size;
    }
  else if (priv->pixbuf != NULL)
    {
      requisition->width = gdk_pixbuf_get_width (priv->pixbuf);
      requisition->height = gdk_pixbuf_get_height (priv->pixbuf);
    }
  else
    {
      requisition->width = widget->allocation.width;
      requisition->height = widget->allocation.height;
    }
}
#endif



static void
blade_bar_image_size_allocate (GtkWidget     *widget,
                                GtkAllocation *allocation)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;

  gtk_widget_set_allocation (widget, allocation);

  /* check if the available size changed */
  if ((priv->pixbuf != NULL || priv->source != NULL)
      && allocation->width > 0
      && allocation->height > 0
      && (allocation->width != priv->width
      || allocation->height != priv->height))
    {
      /* store the new size */
      priv->width = allocation->width;
      priv->height = allocation->height;

      /* free cache */
      blade_bar_image_unref_null (priv->cache);

      if (priv->pixbuf == NULL)
        {
          /* delay icon loading */
          priv->idle_load_id = gdk_threads_add_idle_full (G_PRIORITY_DEFAULT_IDLE, blade_bar_image_load,
                                                          widget, blade_bar_image_load_destroy);
        }
      else
        {
          /* directly render pixbufs */
          blade_bar_image_load (widget);
        }
    }
}



#if GTK_CHECK_VERSION (3, 0, 0)
static gboolean
blade_bar_image_draw (GtkWidget *widget,
                       cairo_t   *cr)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  gint                   source_width, source_height;
  gint                   dest_x, dest_y;
  GtkIconSource         *source;
  GdkPixbuf             *rendered = NULL;
  GdkPixbuf             *pixbuf = priv->cache;
  GtkStyleContext       *context;

  if (G_LIKELY (pixbuf != NULL))
    {
      /* get the size of the cache pixbuf */
      source_width = gdk_pixbuf_get_width (pixbuf);
      source_height = gdk_pixbuf_get_height (pixbuf);

      /* position */
      dest_x = (priv->width - source_width) / 2;
      dest_y = (priv->height - source_height) / 2;

      context = gtk_widget_get_style_context (widget);

      if (!gtk_widget_is_sensitive (widget))
        {
          source = gtk_icon_source_new ();
          gtk_icon_source_set_pixbuf (source, pixbuf);
          rendered = gtk_render_icon_pixbuf (context, source, -1);
          gtk_icon_source_free (source);

          if (G_LIKELY (rendered != NULL))
            pixbuf = rendered;
        }

      /* draw the icon */
      gtk_render_icon (context, cr, pixbuf, dest_x, dest_y);

      if (rendered != NULL)
        g_object_unref (G_OBJECT (rendered));
    }

  return FALSE;
}
#endif



#if !GTK_CHECK_VERSION (3, 0, 0)
static gboolean
blade_bar_image_expose_event (GtkWidget      *widget,
                               GdkEventExpose *event)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  gint                   source_width, source_height;
  gint                   dest_x, dest_y;
  GtkIconSource         *source;
  GdkPixbuf             *rendered = NULL;
  GdkPixbuf             *pixbuf = priv->cache;
  cairo_t               *cr;

  if (G_LIKELY (pixbuf != NULL))
    {
      /* get the size of the cache pixbuf */
      source_width = gdk_pixbuf_get_width (pixbuf);
      source_height = gdk_pixbuf_get_height (pixbuf);

      /* position */
      dest_x = widget->allocation.x + (priv->width - source_width) / 2;
      dest_y = widget->allocation.y + (priv->height - source_height) / 2;

      if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE)
        {
          source = gtk_icon_source_new ();
          gtk_icon_source_set_pixbuf (source, pixbuf);

          rendered = gtk_style_render_icon (widget->style,
                                            source,
                                            gtk_widget_get_direction (widget),
                                            GTK_WIDGET_STATE (widget),
                                            -1, widget, "blade-bar-image");
          gtk_icon_source_free (source);

          if (G_LIKELY (rendered != NULL))
            pixbuf = rendered;
        }

      /* draw the pixbuf */
      cr = gdk_cairo_create (gtk_widget_get_window (widget));
      if (G_LIKELY (cr != NULL))
        {
          gdk_cairo_set_source_pixbuf (cr, pixbuf, dest_x, dest_y);
          cairo_paint (cr);
          cairo_destroy (cr);
        }

      if (rendered != NULL)
        g_object_unref (G_OBJECT (rendered));
    }

  return FALSE;
}
#endif



#if GTK_CHECK_VERSION (3, 0, 0)
static void
blade_bar_image_style_updated (GtkWidget *widget)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  gboolean               force;

  /* let gtk update the widget style */
  (*GTK_WIDGET_CLASS (blade_bar_image_parent_class)->style_updated) (widget);

  /* get style property */
  gtk_widget_style_get (widget, "force-gtk-icon-sizes", &force, NULL);

  /* update if needed */
  if (priv->force_icon_sizes != force)
    {
      priv->force_icon_sizes = force;
      if (priv->size > 0)
        gtk_widget_queue_resize (widget);
    }

  /* update the icon if we have an icon-name source */
  /* and size is not set */
  if (priv->source != NULL
      && !g_path_is_absolute (priv->source)
      && priv->size <= 0)
    {
      /* unset the size to force an update */
      priv->width = priv->height = -1;
      gtk_widget_queue_resize (widget);
    }
}
#endif



#if !GTK_CHECK_VERSION (3, 0, 0)
static void
blade_bar_image_style_set (GtkWidget *widget,
                            GtkStyle  *previous_style)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (widget)->priv;
  gboolean               force;

  /* let gtk update the widget style */
  (*GTK_WIDGET_CLASS (blade_bar_image_parent_class)->style_set) (widget, previous_style);

  /* get style property */
  gtk_widget_style_get (widget, "force-gtk-icon-sizes", &force, NULL);

  /* update if needed */
  if (priv->force_icon_sizes != force)
    {
      priv->force_icon_sizes = force;
      if (priv->size > 0)
        gtk_widget_queue_resize (widget);
    }

  /* update the icon if we have an icon-name source */
  if (previous_style != NULL && priv->source != NULL
      && !g_path_is_absolute (priv->source))
    {
      /* unset the size to force an update */
      priv->width = priv->height = -1;
      gtk_widget_queue_resize (widget);
    }
}
#endif



static gboolean
blade_bar_image_load (gpointer data)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (data)->priv;
  GdkPixbuf             *pixbuf;
  GdkScreen             *screen;
  GtkIconTheme          *icon_theme = NULL;
  gint                   dest_w, dest_h;

  dest_w = priv->width;
  dest_h = priv->height;

  if (G_UNLIKELY (priv->force_icon_sizes
      && dest_w < 32
      && dest_w == dest_h))
    {
      /* we use some hardcoded values here for convienence,
       * above 32 pixels svg icons will kick in */
      if (dest_w > 16 && dest_w < 22)
        dest_w = 16;
      else if (dest_w > 22 && dest_w < 24)
        dest_w = 22;
      else if (dest_w > 24 && dest_w < 32)
        dest_w = 24;

      dest_h = dest_w;
    }

  if (priv->pixbuf != NULL)
    {
      /* use the pixbuf set by the user */
      pixbuf = g_object_ref (G_OBJECT (priv->pixbuf));

      if (G_LIKELY (pixbuf != NULL))
        {
          /* scale the icon to the correct size */
          priv->cache = blade_bar_image_scale_pixbuf (pixbuf, dest_w, dest_h);
          g_object_unref (G_OBJECT (pixbuf));
        }
    }
  else
    {
      screen = gtk_widget_get_screen (GTK_WIDGET (data));
      if (G_LIKELY (screen != NULL))
        icon_theme = gtk_icon_theme_get_for_screen (screen);

      priv->cache = blade_bar_pixbuf_from_source_at_size (priv->source, icon_theme, dest_w, dest_h);
    }

  if (G_LIKELY (priv->cache != NULL))
    gtk_widget_queue_draw (GTK_WIDGET (data));

  return FALSE;
}



static void
blade_bar_image_load_destroy (gpointer data)
{
  BLADE_BAR_IMAGE (data)->priv->idle_load_id = 0;
}



static GdkPixbuf *
blade_bar_image_scale_pixbuf (GdkPixbuf *source,
                               gint       dest_width,
                               gint       dest_height)
{
  gdouble ratio;
  gint    source_width;
  gint    source_height;

  bar_return_val_if_fail (GDK_IS_PIXBUF (source), NULL);

  /* we fail on invalid sizes */
  if (G_UNLIKELY (dest_width <= 0 || dest_height <= 0))
    return NULL;

  source_width = gdk_pixbuf_get_width (source);
  source_height = gdk_pixbuf_get_height (source);

  /* check if we need to scale */
  if (source_width <= dest_width && source_height <= dest_height)
    return g_object_ref (G_OBJECT (source));

  /* calculate the new dimensions */

  ratio = MIN ((gdouble) dest_width / (gdouble) source_width,
               (gdouble) dest_height / (gdouble) source_height);

  dest_width  = rint (source_width * ratio);
  dest_height = rint (source_height * ratio);

  return gdk_pixbuf_scale_simple (source, MAX (dest_width, 1),
                                  MAX (dest_height, 1),
                                  GDK_INTERP_BILINEAR);
}



/**
 * blade_bar_image_new:
 *
 * Creates a new empty #BladeBarImage widget.
 *
 * returns: a newly created BladeBarImage widget.
 *
 * Since: 4.8
 **/
GtkWidget *
blade_bar_image_new (void)
{
  return g_object_new (XFCE_TYPE_BAR_IMAGE, NULL);
}



/**
 * blade_bar_image_new_from_pixbuf:
 * @pixbuf : a #GdkPixbuf, or %NULL.
 *
 * Creates a new #BladeBarImage displaying @pixbuf. #BladeBarImage
 * will add its own reference rather than adopting yours. You don't
 * need to scale the pixbuf to the correct size, the #BladeBarImage
 * will take care of that based on the allocation of the widget or
 * the size set with blade_bar_image_set_size().
 *
 * returns: a newly created BladeBarImage widget.
 *
 * Since: 4.8
 **/
GtkWidget *
blade_bar_image_new_from_pixbuf (GdkPixbuf *pixbuf)
{
  g_return_val_if_fail (pixbuf == NULL || GDK_IS_PIXBUF (pixbuf), NULL);

  return g_object_new (XFCE_TYPE_BAR_IMAGE,
                       "pixbuf", pixbuf, NULL);
}



/**
 * blade_bar_image_new_from_source:
 * @source : source of the image. This can be an absolute path or
 *           an icon-name or %NULL.
 *
 * Creates a new #BladeBarImage displaying @source. #BladeBarImage
 * will detect if @source points to an absolute file or it and icon-name.
 * For icon-names it will also look for files in the pixbuf folder or
 * strip the extensions, which makes it suitable for usage with icon
 * keys in .desktop files.
 *
 * returns: a newly created BladeBarImage widget.
 *
 * Since: 4.8
 **/
GtkWidget *
blade_bar_image_new_from_source (const gchar *source)
{
  g_return_val_if_fail (source == NULL || *source != '\0', NULL);

  return g_object_new (XFCE_TYPE_BAR_IMAGE,
                       "source", source, NULL);
}



/**
 * blade_bar_image_set_from_pixbuf:
 * @image  : an #BladeBarImage.
 * @pixbuf : a #GdkPixbuf, or %NULL.
 *
 * See blade_bar_image_new_from_pixbuf() for details.
 *
 * Since: 4.8
 **/
void
blade_bar_image_set_from_pixbuf (BladeBarImage *image,
                                  GdkPixbuf      *pixbuf)
{
  g_return_if_fail (BLADE_IS_BAR_IMAGE (image));
  g_return_if_fail (pixbuf == NULL || GDK_IS_PIXBUF (pixbuf));

  blade_bar_image_clear (image);

  /* set the new pixbuf, scale it to the maximum size if needed */
  image->priv->pixbuf = blade_bar_image_scale_pixbuf (pixbuf,
      MAX_PIXBUF_SIZE, MAX_PIXBUF_SIZE);

  gtk_widget_queue_resize (GTK_WIDGET (image));
}



/**
 * blade_bar_image_set_from_source:
 * @image  : an #BladeBarImage.
 * @source : source of the image. This can be an absolute path or
 *           an icon-name or %NULL.
 *
 * See blade_bar_image_new_from_source() for details.
 *
 * Since: 4.8
 **/
void
blade_bar_image_set_from_source (BladeBarImage *image,
                                  const gchar    *source)
{
  g_return_if_fail (BLADE_IS_BAR_IMAGE (image));
  g_return_if_fail (source == NULL || *source != '\0');

  blade_bar_image_clear (image);

  image->priv->source = g_strdup (source);

  gtk_widget_queue_resize (GTK_WIDGET (image));
}



/**
 * blade_bar_image_set_size:
 * @image : an #BladeBarImage.
 * @size  : a new size in pixels.
 *
 * This will force an image size, instead of looking at the allocation
 * size, see introduction for more details. You can set a @size of
 * -1 to turn this off.
 *
 * Since: 4.8
 **/
void
blade_bar_image_set_size (BladeBarImage *image,
                           gint            size)
{

  g_return_if_fail (BLADE_IS_BAR_IMAGE (image));

  if (G_LIKELY (image->priv->size != size))
    {
      image->priv->size = size;
      gtk_widget_queue_resize (GTK_WIDGET (image));
    }
}



/**
 * blade_bar_image_get_size:
 * @image : an #BladeBarImage.
 *
 * The size of the image, set by blade_bar_image_set_size() or -1
 * if no size is forced and the image is scaled to the allocation size.
 *
 * Returns: icon size in pixels of the image or -1.
 *
 * Since: 4.8
 **/
gint
blade_bar_image_get_size (BladeBarImage *image)
{
  g_return_val_if_fail (BLADE_IS_BAR_IMAGE (image), -1);
  return image->priv->size;
}



/**
 * blade_bar_image_clear:
 * @image : an #BladeBarImage.
 *
 * Resets the image to be empty.
 *
 * Since: 4.8
 **/
void
blade_bar_image_clear (BladeBarImage *image)
{
  BladeBarImagePrivate *priv = BLADE_BAR_IMAGE (image)->priv;

  g_return_if_fail (BLADE_IS_BAR_IMAGE (image));

  if (priv->idle_load_id != 0)
    g_source_remove (priv->idle_load_id);

  if (priv->source != NULL)
    {
     g_free (priv->source);
     priv->source = NULL;
    }

  blade_bar_image_unref_null (priv->pixbuf);
  blade_bar_image_unref_null (priv->cache);

  /* reset values */
  priv->width = -1;
  priv->height = -1;
}



#define __BLADE_BAR_IMAGE_C__
#include <libbladebar/libbladebar-aliasdef.c>
