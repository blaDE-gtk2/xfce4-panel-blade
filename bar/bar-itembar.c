/*
 * Copyright (C) 2008-2011 Nick Schermer <nick@xfce.org>
 * Copyright (C)      2011 Andrzej <ndrwrdck@gmail.com>
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

#include <math.h>
#include <gtk/gtk.h>
#include <blxo/blxo.h>

#include <common/bar-private.h>
#include <libbladebar/libbladebar.h>

#include <bar/bar-itembar.h>

#define IS_HORIZONTAL(itembar) ((itembar)->mode == BLADE_BAR_PLUGIN_MODE_HORIZONTAL)
#define HIGHLIGHT_SIZE         2



typedef struct _BarItembarChild BarItembarChild;



static void               bar_itembar_set_property       (GObject         *object,
                                                            guint            prop_id,
                                                            const GValue    *value,
                                                            GParamSpec      *pspec);
static void               bar_itembar_get_property       (GObject         *object,
                                                            guint            prop_id,
                                                            GValue          *value,
                                                            GParamSpec      *pspec);
static void               bar_itembar_finalize           (GObject         *object);
static void               bar_itembar_size_request       (GtkWidget       *widget,
                                                            GtkRequisition  *requisition);
static void               bar_itembar_size_allocate      (GtkWidget       *widget,
                                                            GtkAllocation   *allocation);
static gboolean           bar_itembar_expose_event       (GtkWidget       *widget,
                                                            GdkEventExpose  *event);
static void               bar_itembar_add                (GtkContainer    *container,
                                                            GtkWidget       *child);
static void               bar_itembar_remove             (GtkContainer    *container,
                                                            GtkWidget       *child);
static void               bar_itembar_forall             (GtkContainer    *container,
                                                            gboolean         include_internals,
                                                            GtkCallback      callback,
                                                            gpointer         callback_data);
static GType              bar_itembar_child_type         (GtkContainer    *container);
static void               bar_itembar_set_child_property (GtkContainer    *container,
                                                            GtkWidget       *widget,
                                                            guint            prop_id,
                                                            const GValue    *value,
                                                            GParamSpec      *pspec);
static void               bar_itembar_get_child_property (GtkContainer    *container,
                                                            GtkWidget       *widget,
                                                            guint            prop_id,
                                                            GValue          *value,
                                                            GParamSpec      *pspec);
static BarItembarChild *bar_itembar_get_child          (BarItembar    *itembar,
                                                            GtkWidget       *widget);



struct _BarItembarClass
{
  GtkContainerClass __parent__;
};

struct _BarItembar
{
  GtkContainer __parent__;

  GSList              *children;

  /* some properties we clone from the bar window */
  BladeBarPluginMode  mode;
  gint                 size;
  gint                 nrows;

  /* dnd support */
  gint                 highlight_index;
  gint                 highlight_x, highlight_y, highlight_length;
  gboolean             highlight_small;
};

typedef enum
{
  CHILD_OPTION_NONE,
  CHILD_OPTION_EXPAND,
  CHILD_OPTION_SHRINK,
  CHILD_OPTION_SMALL
}
ChildOptions;

struct _BarItembarChild
{
  GtkWidget    *widget;
  ChildOptions  option;
  gint          row;
};

enum
{
  PROP_0,
  PROP_MODE,
  PROP_SIZE,
  PROP_NROWS
};

enum
{
  CHILD_PROP_0,
  CHILD_PROP_EXPAND,
  CHILD_PROP_SHRINK,
  CHILD_PROP_SMALL
};

enum
{
  CHANGED,
  LAST_SIGNAL
};



static guint itembar_signals[LAST_SIGNAL];



G_DEFINE_TYPE (BarItembar, bar_itembar, GTK_TYPE_CONTAINER)



static void
bar_itembar_class_init (BarItembarClass *klass)
{
  GObjectClass      *gobject_class;
  GtkWidgetClass    *gtkwidget_class;
  GtkContainerClass *gtkcontainer_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = bar_itembar_set_property;
  gobject_class->get_property = bar_itembar_get_property;
  gobject_class->finalize = bar_itembar_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->size_request = bar_itembar_size_request;
  gtkwidget_class->size_allocate = bar_itembar_size_allocate;
  gtkwidget_class->expose_event = bar_itembar_expose_event;

  gtkcontainer_class = GTK_CONTAINER_CLASS (klass);
  gtkcontainer_class->add = bar_itembar_add;
  gtkcontainer_class->remove = bar_itembar_remove;
  gtkcontainer_class->forall = bar_itembar_forall;
  gtkcontainer_class->child_type = bar_itembar_child_type;
  gtkcontainer_class->get_child_property = bar_itembar_get_child_property;
  gtkcontainer_class->set_child_property = bar_itembar_set_child_property;

  itembar_signals[CHANGED] =
    g_signal_new (g_intern_static_string ("changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (gobject_class,
                                   PROP_MODE,
                                   g_param_spec_enum ("mode",
                                                      NULL, NULL,
                                                      XFCE_TYPE_BAR_PLUGIN_MODE,
                                                      BLADE_BAR_PLUGIN_MODE_HORIZONTAL,
                                                      BLXO_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_SIZE,
                                   g_param_spec_uint ("size",
                                                      NULL, NULL,
                                                      16, 128, 30,
                                                      BLXO_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_NROWS,
                                   g_param_spec_uint ("nrows",
                                                      NULL, NULL,
                                                      1, 6, 1,
                                                      BLXO_PARAM_WRITABLE));

  gtk_container_class_install_child_property (gtkcontainer_class,
                                              CHILD_PROP_EXPAND,
                                              g_param_spec_boolean ("expand",
                                                                    NULL, NULL,
                                                                    FALSE,
                                                                    BLXO_PARAM_READWRITE));

  gtk_container_class_install_child_property (gtkcontainer_class,
                                              CHILD_PROP_SHRINK,
                                              g_param_spec_boolean ("shrink",
                                                                    NULL, NULL,
                                                                    FALSE,
                                                                    BLXO_PARAM_READWRITE));

  gtk_container_class_install_child_property (gtkcontainer_class,
                                              CHILD_PROP_SMALL,
                                              g_param_spec_boolean ("small",
                                                                    NULL, NULL,
                                                                    FALSE,
                                                                    BLXO_PARAM_READWRITE));
}



static void
bar_itembar_init (BarItembar *itembar)
{
  itembar->children = NULL;
  itembar->mode = BLADE_BAR_PLUGIN_MODE_HORIZONTAL;
  itembar->size = 30;
  itembar->nrows = 1;
  itembar->highlight_index = -1;
  itembar->highlight_length = -1;

  GTK_WIDGET_SET_FLAGS (GTK_WIDGET (itembar), GTK_NO_WINDOW);

  gtk_widget_set_redraw_on_allocate (GTK_WIDGET (itembar), FALSE);
}



static void
bar_itembar_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BarItembar *itembar = BAR_ITEMBAR (object);

  switch (prop_id)
    {
    case PROP_MODE:
      itembar->mode = g_value_get_enum (value);
      break;

    case PROP_SIZE:
      itembar->size = g_value_get_uint (value);
      break;

    case PROP_NROWS:
      itembar->nrows = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }

  gtk_widget_queue_resize (GTK_WIDGET (itembar));
}



static void
bar_itembar_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
bar_itembar_finalize (GObject *object)
{
  bar_return_if_fail (BAR_ITEMBAR (object)->children == NULL);

  (*G_OBJECT_CLASS (bar_itembar_parent_class)->finalize) (object);
}

#define CHILD_LENGTH(child_req, itembar) \
  (IS_HORIZONTAL (itembar) ? child_req.width : child_req.height)

static void
bar_itembar_size_request (GtkWidget      *widget,
                            GtkRequisition *requisition)
{
  BarItembar      *itembar = BAR_ITEMBAR (widget);
  GSList            *li;
  BarItembarChild *child;
  GtkRequisition     child_req;
  gint               border_width;
  gint               row_max_size;
  gint               col_count;
  gint               rows_size;
  gint               total_len;
  gint               child_len;

  /* total length we request */
  total_len = 0;

  /* counter for small child packing */
  row_max_size = 0;
  col_count = 0;

  for (li = itembar->children; li != NULL; li = li->next)
    {
      child = li->data;

      if (G_LIKELY (child != NULL))
        {
          if (!GTK_WIDGET_VISIBLE (child->widget))
            continue;

          /* get the child's size request */
          gtk_widget_size_request (child->widget, &child_req);

          /* check if the small child fits in a row */
          if (child->option == CHILD_OPTION_SMALL
              && itembar->nrows > 1)
            {
              child_len = CHILD_LENGTH (child_req, itembar);

              /* make sure we have enough space for all the children on the row.
               * so add the difference between the largest child in this column */
              if (child_len > row_max_size)
                {
                  total_len += child_len - row_max_size;
                  row_max_size = child_len;
                }

              /* reset to new row if all columns are filled */
              if (++col_count >= itembar->nrows)
                {
                  col_count = 0;
                  row_max_size = 0;
                }
            }
          else /* expanding or normal item */
            {
              total_len += CHILD_LENGTH (child_req, itembar);

              /* reset column packing */
              col_count = 0;
              row_max_size = 0;
            }
        }
      else
        {
          /* this noop item is the dnd position */
          total_len += HIGHLIGHT_SIZE;
        }
    }

  /* the size property stored in the itembar is that of a single row */
  rows_size = itembar->size * itembar->nrows;

  /* return the total size */
  border_width = GTK_CONTAINER (widget)->border_width * 2;
  if (IS_HORIZONTAL (itembar))
    {
      requisition->width = total_len + border_width;
      requisition->height = rows_size + border_width;
    }
  else
    {
      requisition->height = total_len + border_width;
      requisition->width = rows_size + border_width;
    }
}



static void
bar_itembar_size_allocate (GtkWidget     *widget,
                             GtkAllocation *allocation)
{
  BarItembar      *itembar = BAR_ITEMBAR (widget);
  GSList            *lp, *ltemp;
  BarItembarChild *child;
  GtkRequisition     child_req;
  GtkAllocation      child_alloc;
  gint               border_width;
  gint               expand_len_avail, expand_len_req;
  gint               shrink_len_avail, shrink_len_req;
  gint               itembar_len;
  gint               x, y;
  gint               x_init, y_init;
  gboolean           expand_children_fit;
  gint               new_len;
  gint               child_len;
  gint               row_max_size;
  gint               col_count;
  gint               rows_size;

  /* the maximum allocation is limited by that of the
   * bar window, so take over the assigned allocation */
  widget->allocation = *allocation;

  border_width = GTK_CONTAINER (widget)->border_width;

  if (IS_HORIZONTAL (itembar))
    itembar_len = allocation->width - 2 * border_width;
  else
    itembar_len = allocation->height - 2 * border_width;

  /* init the remaining space for expanding plugins */
  expand_len_avail = itembar_len;
  expand_len_req = 0;

  /* init the total size of shrinking plugins */
  shrink_len_avail = 0;
  shrink_len_req = 0;

  /* init counters for small child packing */
  row_max_size = 0;
  col_count = 0;

  /* get information about the expandable lengths */
  for (lp = itembar->children; lp != NULL; lp = lp->next)
    {
      child = lp->data;
      if (G_LIKELY (child != NULL))
        {
          if (!GTK_WIDGET_VISIBLE (child->widget))
            continue;

          gtk_widget_get_child_requisition (child->widget, &child_req);

          child_len = CHILD_LENGTH (child_req, itembar);

          if (G_UNLIKELY (child->option == CHILD_OPTION_SMALL
                          && itembar->nrows > 1))
            {
              /* extract from the available space */
              if (child_len > row_max_size)
                {
                  expand_len_avail -= child_len - row_max_size;
                  row_max_size = child_len;
                }

              /* reset to new row if all columns are filled */
              if (++col_count >= itembar->nrows)
                {
                  col_count = 0;
                  row_max_size = 0;
                }
            }
          else
            {
              /* reset column packing counters */
              col_count = 0;
              row_max_size = 0;

              if (G_UNLIKELY (child->option == CHILD_OPTION_EXPAND))
                {
                  expand_len_req += child_len;
                }
              else
                {
                  expand_len_avail -= child_len;

                  if (child->option == CHILD_OPTION_SHRINK)
                    shrink_len_avail += child_len;
                }
            }
        }
      else
        {
          /* dnd separator */
          expand_len_avail -= HIGHLIGHT_SIZE;
        }
    }

  /* whether the expandable items fit on this row; we use this
   * as a fast-path when there are expanding items on a bar with
   * not really enough length to expand (ie. items make the bar grow,
   * not the length set by the user) */
  expand_children_fit = expand_len_req == expand_len_avail;

  if (expand_len_avail < expand_len_req)
    {
      /* check if there are plugins on the bar we can shrink */
      if (shrink_len_avail > 0)
        shrink_len_req = expand_len_req - expand_len_avail;

      expand_len_avail = expand_len_req;
    }

  /* init coordinates for first child */
  x = x_init = allocation->x + border_width;
  y = y_init = allocation->y + border_width;

  /* init counters for small child packing */
  row_max_size = 0;
  col_count = 0;

  /* the size property stored in the itembar is that of a single row */
  rows_size = itembar->size * itembar->nrows;

  /* allocate the children on this row */
  for (lp = itembar->children; lp != NULL; lp = lp->next)
    {
      child = lp->data;

      /* the highlight item for which we keep some spare space */
      if (G_UNLIKELY (child == NULL))
        {

          ltemp = g_slist_next (lp);
          itembar->highlight_small = (col_count > 0 && ltemp && ltemp->data  && ((BarItembarChild *)ltemp->data)->option == CHILD_OPTION_SMALL);

          if (itembar->highlight_small)
            {
              itembar->highlight_x = x;
              itembar->highlight_y = y;
              if (IS_HORIZONTAL (itembar))
                y += HIGHLIGHT_SIZE;
              else
                x += HIGHLIGHT_SIZE;
            }
          else if (IS_HORIZONTAL (itembar))
            {
              itembar->highlight_x = (col_count > 0) ? x + row_max_size : x;
              itembar->highlight_y = y_init;

              x += HIGHLIGHT_SIZE;
              expand_len_avail -= HIGHLIGHT_SIZE;
            }
          else
            {
              itembar->highlight_x = x_init;
              itembar->highlight_y = (col_count > 0) ? y + row_max_size : y;

              y += HIGHLIGHT_SIZE;
              expand_len_avail -= HIGHLIGHT_SIZE;
            }

          continue;
        }

      if (!GTK_WIDGET_VISIBLE (child->widget))
        continue;

      gtk_widget_get_child_requisition (child->widget, &child_req);

      child_len = CHILD_LENGTH (child_req, itembar);

      if (G_UNLIKELY (!expand_children_fit && child->option == CHILD_OPTION_EXPAND))
        {
          /* equally share the length between the expanding plugins */
          bar_assert (expand_len_req > 0);
          new_len = expand_len_avail * child_len / expand_len_req;

          expand_len_req -= child_len;
          expand_len_avail -= new_len;

          child_len = new_len;
        }
      else if (child->option == CHILD_OPTION_SHRINK
               && shrink_len_req > 0)
        {
          /* equally shrink all shrinking plugins */
          bar_assert (shrink_len_avail > 0);
          new_len = shrink_len_req * child_len / shrink_len_avail;

          shrink_len_req -= new_len;
          shrink_len_avail -= child_len;

          child_len -= new_len;
        }

      if (G_UNLIKELY (child_len < 1))
        child_len = 1;

      if (child->option == CHILD_OPTION_SMALL
          && itembar->nrows > 1)
        {
          if (row_max_size < child_len)
            row_max_size = child_len;

          child_alloc.x = x;
          child_alloc.y = y;

          if (IS_HORIZONTAL (itembar))
            {
              child_alloc.height = itembar->size;
              child_alloc.width = child_len;

              /* pack next small item below this one */
              y += itembar->size;
            }
          else
            {
              child_alloc.width = itembar->size;
              child_alloc.height = child_len;

              /* pack next time right of this one */
              x += itembar->size;
            }

          child->row = col_count;

          /* reset to new row if all columns are filled */
          if (++col_count >= itembar->nrows)
            {
#define RESET_COLUMN_COUNTERS \
              /* update coordinates */ \
              if (IS_HORIZONTAL (itembar)) \
                { \
                  x += row_max_size; \
                  y = y_init; \
                } \
              else \
                { \
                  y += row_max_size; \
                  x = x_init; \
                } \
               \
              col_count = 0; \
              row_max_size = 0;

              RESET_COLUMN_COUNTERS
            }
        }
      else
        {
          /* reset column packing counters */
          if (col_count > 0)
            {
              RESET_COLUMN_COUNTERS
            }

          child->row = col_count;

          child_alloc.x = x;
          child_alloc.y = y;

          if (IS_HORIZONTAL (itembar))
            {
              child_alloc.height = rows_size;
              child_alloc.width = child_len;

              x += child_len;
            }
          else
            {
              child_alloc.width = rows_size;
              child_alloc.height = child_len;

              y += child_len;
            }
        }

      gtk_widget_size_allocate (child->widget, &child_alloc);
    }
}



static gboolean
bar_itembar_expose_event (GtkWidget      *widget,
                            GdkEventExpose *event)
{
  BarItembar *itembar = BAR_ITEMBAR (widget);
  gboolean      result;
  cairo_t      *cr;
  GdkWindow    *window;
  GdkRectangle  rect;
  gint          row_size;

  result = (*GTK_WIDGET_CLASS (bar_itembar_parent_class)->expose_event) (widget, event);

  if (itembar->highlight_index != -1)
    {
      row_size = (itembar->highlight_small) ? itembar->size : itembar->size * itembar->nrows;

      rect.x = itembar->highlight_x;
      rect.y = itembar->highlight_y;

      if ((IS_HORIZONTAL (itembar) && !itembar->highlight_small) ||
          (!IS_HORIZONTAL (itembar) && itembar->highlight_small))
        {
          rect.width = HIGHLIGHT_SIZE;
          rect.height = (itembar->highlight_length != -1) ?
            itembar->highlight_length : row_size;
        }
      else
        {
          rect.height = HIGHLIGHT_SIZE;
          rect.width = (itembar->highlight_length != -1) ?
            itembar->highlight_length : row_size;
        }

      /* draw highlight box */
      window = gtk_widget_get_window (widget);
      cr = gdk_cairo_create (window);
      cairo_set_source_rgb (cr, 1.00, 0.00, 0.00);

      gdk_cairo_rectangle (cr, &event->area);
      cairo_clip (cr);

      gdk_cairo_rectangle (cr, &rect);
      cairo_fill (cr);

      cairo_destroy (cr);
    }

  return result;
}



static void
bar_itembar_add (GtkContainer *container,
                   GtkWidget    *child)
{
  bar_itembar_insert (BAR_ITEMBAR (container), child, -1);
}



static void
bar_itembar_remove (GtkContainer *container,
                      GtkWidget    *widget)
{
  BarItembarChild *child;
  BarItembar      *itembar = BAR_ITEMBAR (container);

  bar_return_if_fail (BAR_IS_ITEMBAR (itembar));
  bar_return_if_fail (GTK_IS_WIDGET (widget));
  bar_return_if_fail (widget->parent == GTK_WIDGET (container));
  bar_return_if_fail (itembar->children != NULL);

  child = bar_itembar_get_child (itembar, widget);
  if (G_LIKELY (child != NULL))
    {
      itembar->children = g_slist_remove (itembar->children, child);

      gtk_widget_unparent (widget);

      g_slice_free (BarItembarChild, child);

      gtk_widget_queue_resize (GTK_WIDGET (container));

      g_signal_emit (G_OBJECT (itembar), itembar_signals[CHANGED], 0);
    }
}



static void
bar_itembar_forall (GtkContainer *container,
                      gboolean      include_internals,
                      GtkCallback   callback,
                      gpointer      callback_data)
{
  BarItembar      *itembar = BAR_ITEMBAR (container);
  GSList            *children = itembar->children;
  BarItembarChild *child;

  bar_return_if_fail (BAR_IS_ITEMBAR (container));

  while (children != NULL)
    {
      child = children->data;
      children = g_slist_next (children);

      if (G_LIKELY (child != NULL))
        (* callback) (child->widget, callback_data);
    }
}



static GType
bar_itembar_child_type (GtkContainer *container)
{
  return GTK_TYPE_WIDGET;
}



static void
bar_itembar_set_child_property (GtkContainer *container,
                                  GtkWidget    *widget,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BarItembarChild *child;
  gboolean           enable;
  ChildOptions       option;

  child = bar_itembar_get_child (BAR_ITEMBAR (container), widget);
  if (G_UNLIKELY (child == NULL))
    return;

  switch (prop_id)
    {
    case CHILD_PROP_EXPAND:
      option = CHILD_OPTION_EXPAND;
      break;

    case CHILD_PROP_SHRINK:
      option = CHILD_OPTION_SHRINK;
      break;

    case CHILD_PROP_SMALL:
      option = CHILD_OPTION_SMALL;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (container, prop_id, pspec);
      return;
    }

  /* warn if we override an old option */
  if (child->option != CHILD_OPTION_NONE
      && child->option != option)
    g_warning ("Itembar child can only enable only of expand, shrink or small.");

  /* leave if nothing changes */
  enable = g_value_get_boolean (value);
  if ((!enable && child->option == CHILD_OPTION_NONE)
      || (enable && child->option == option))
    return;

  child->option = enable ? option : CHILD_OPTION_NONE;

  gtk_widget_queue_resize (GTK_WIDGET (container));
}



static void
bar_itembar_get_child_property (GtkContainer *container,
                                  GtkWidget    *widget,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  BarItembarChild *child;

  child = bar_itembar_get_child (BAR_ITEMBAR (container), widget);
  if (G_UNLIKELY (child == NULL))
    return;

  switch (prop_id)
    {
    case CHILD_PROP_EXPAND:
      g_value_set_boolean (value, child->option == CHILD_OPTION_EXPAND);
      break;

    case CHILD_PROP_SHRINK:
      g_value_set_boolean (value, child->option == CHILD_OPTION_SHRINK);
      break;

    case CHILD_PROP_SMALL:
      g_value_set_boolean (value, child->option == CHILD_OPTION_SMALL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (container, prop_id, pspec);
      break;
    }
}



static BarItembarChild *
bar_itembar_get_child (BarItembar *itembar,
                         GtkWidget    *widget)
{
  GSList            *li;
  BarItembarChild *child;

  bar_return_val_if_fail (BAR_IS_ITEMBAR (itembar), NULL);
  bar_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);
  bar_return_val_if_fail (widget->parent == GTK_WIDGET (itembar), NULL);

  for (li = itembar->children; li != NULL; li = g_slist_next (li))
    {
      child = li->data;
      if (child != NULL
          && child->widget == widget)
        return child;
    }

  return NULL;
}



GtkWidget *
bar_itembar_new (void)
{
  return g_object_new (BAR_TYPE_ITEMBAR, NULL);
}



void
bar_itembar_insert (BarItembar *itembar,
                      GtkWidget    *widget,
                      gint          position)
{
  BarItembarChild *child;

  bar_return_if_fail (BAR_IS_ITEMBAR (itembar));
  bar_return_if_fail (GTK_IS_WIDGET (widget));
  bar_return_if_fail (widget->parent == NULL);

  child = g_slice_new0 (BarItembarChild);
  child->widget = widget;
  child->option = CHILD_OPTION_NONE;

  itembar->children = g_slist_insert (itembar->children, child, position);
  gtk_widget_set_parent (widget, GTK_WIDGET (itembar));

  gtk_widget_queue_resize (GTK_WIDGET (itembar));
  g_signal_emit (G_OBJECT (itembar), itembar_signals[CHANGED], 0);
}



void
bar_itembar_reorder_child (BarItembar *itembar,
                             GtkWidget    *widget,
                             gint          position)
{
  BarItembarChild *child;

  bar_return_if_fail (BAR_IS_ITEMBAR (itembar));
  bar_return_if_fail (GTK_IS_WIDGET (widget));
  bar_return_if_fail (widget->parent == GTK_WIDGET (itembar));

  child = bar_itembar_get_child (itembar, widget);
  if (G_LIKELY (child != NULL))
    {
      /* move in the internal list */
      itembar->children = g_slist_remove (itembar->children, child);
      itembar->children = g_slist_insert (itembar->children, child, position);

      gtk_widget_queue_resize (GTK_WIDGET (itembar));
      g_signal_emit (G_OBJECT (itembar), itembar_signals[CHANGED], 0);
    }
}



gint
bar_itembar_get_child_index (BarItembar *itembar,
                               GtkWidget    *widget)
{
  GSList            *li;
  BarItembarChild *child;
  gint               idx;

  bar_return_val_if_fail (BAR_IS_ITEMBAR (itembar), -1);
  bar_return_val_if_fail (GTK_IS_WIDGET (widget), -1);
  bar_return_val_if_fail (widget->parent == GTK_WIDGET (itembar), -1);

  for (idx = 0, li = itembar->children; li != NULL; li = g_slist_next (li), idx++)
    {
      child = li->data;
      if (G_UNLIKELY (child == NULL))
        continue;

      if (child->widget == widget)
        return idx;
    }

  return -1;
}



guint
bar_itembar_get_n_children (BarItembar *itembar)
{
  guint n;

  bar_return_val_if_fail (BAR_IS_ITEMBAR (itembar), 0);

  n = g_slist_length (itembar->children);
  if (G_UNLIKELY (itembar->highlight_index != -1))
    n--;

  return n;
}



guint
bar_itembar_get_drop_index (BarItembar *itembar,
                              gint          x,
                              gint          y)
{
  BarItembarChild *child, *child2;
  GSList            *li, *li2;
  GtkAllocation      alloc;
  guint              idx, col_start_idx, col_end_idx;
  gint               xr, yr, col_width;
  gdouble            aspect;

  bar_return_val_if_fail (BAR_IS_ITEMBAR (itembar), 0);

  /* add the itembar position */
  alloc = GTK_WIDGET (itembar)->allocation;

  if (!IS_HORIZONTAL (itembar))
    {
      SWAP_INTEGER (x, y);
      TRANSPOSE_AREA (alloc);
    }

  /* return -1 if point is outside the widget allocation */
  if (x < alloc.x || y < alloc.y ||
      x >= alloc.x + alloc.width || y >= alloc.y + alloc.height)
    return g_slist_length (itembar->children);

  col_width = -1;
  itembar->highlight_length = -1;
  idx = 0;
  col_start_idx = 0;
  col_end_idx = 0;

  for (li = itembar->children; li != NULL; li = g_slist_next (li))
    {
      child = li->data;
      if (G_UNLIKELY (child == NULL))
        continue;

      bar_assert (child->widget != NULL);
      alloc = child->widget->allocation;

      if (!IS_HORIZONTAL (itembar))
        TRANSPOSE_AREA (alloc);

      xr = x - alloc.x;
      yr = y - alloc.y;

      if (child->option == CHILD_OPTION_SMALL)
        {
          /* are we at the beginning of the column? */
          if (child->row == 0)
            {
              col_start_idx = idx;
              col_end_idx = idx + 1;
              col_width = alloc.width;
              /* find the width of the current column and the idx of last item */
              for (li2 = g_slist_next (li); li2 != NULL; li2 = g_slist_next (li2))
                {
                  child2 = li2->data;
                  if (G_UNLIKELY (child2 == NULL))
                    continue;
                  if (child2->row == 0)
                    break;
                  bar_assert (child2->widget != NULL);
                  col_end_idx++;
                  if (IS_HORIZONTAL (itembar))
                    col_width = MAX (col_width, child2->widget->allocation.width);
                  else
                    col_width = MAX (col_width, child2->widget->allocation.height);
                }
            }

          /* calculate aspect ratio */
          if (alloc.height > 0 && col_width > 0)
            aspect = (gdouble) col_width / (gdouble) alloc.height;
          else
            aspect = 1.0;

          /* before current column */
          if (xr < 0 ||
              (xr < (gint) round (yr * aspect) &&
               xr < (gint) round ((alloc.height - yr) * aspect)))
            {
              idx = col_start_idx;
              break;
            }
          /* before current child */
          if (xr < col_width &&
              xr >= (gint) round (yr * aspect) &&
              col_width - xr >= (gint) round (yr * aspect))
            {
              if (child->row != 0)
                itembar->highlight_length = col_width;
              break;
            }
          /* after current column */
          if (xr < col_width &&
              xr >= (gint) round ((alloc.height - yr) * aspect) &&
              xr >= (gint) round (yr * aspect))
            {
              idx = col_end_idx;
              break;
            }
        }
      else
        {
          if (xr < alloc.width / 2)
            break;
        }
      idx++;
    }

  return idx;
}



void
bar_itembar_set_drop_highlight_item (BarItembar *itembar,
                                       gint          idx)
{
  bar_return_if_fail (BAR_IS_ITEMBAR (itembar));

  if (idx == itembar->highlight_index)
    return;

  if (itembar->highlight_index != -1)
    itembar->children = g_slist_remove_all (itembar->children, NULL);
  if (idx != -1)
    itembar->children = g_slist_insert (itembar->children, NULL, idx);

  itembar->highlight_index = idx;

  gtk_widget_queue_resize (GTK_WIDGET (itembar));
}
