/*
 * Copyright (C) 2007-2010 Nick Schermer <nick@xfce.org>
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

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <blxo/blxo.h>
#include <gtk/gtk.h>
#include <libbladebar/libbladebar.h>
#include <common/bar-private.h>
#include <common/bar-debug.h>

#include "systray-box.h"
#include "systray-socket.h"

#define SPACING    (2)
#define OFFSCREEN  (-9999)

/* some icon implementations request a 1x1 size for invisible icons */
#define REQUISITION_IS_INVISIBLE(child_req) ((child_req).width <= 1 && (child_req).height <= 1)



static void     systray_box_get_property          (GObject         *object,
                                                   guint            prop_id,
                                                   GValue          *value,
                                                   GParamSpec      *pspec);
static void     systray_box_finalize              (GObject         *object);
static void     systray_box_size_request          (GtkWidget       *widget,
                                                   GtkRequisition  *requisition);
static void     systray_box_size_allocate         (GtkWidget       *widget,
                                                   GtkAllocation   *allocation);
static void     systray_box_add                   (GtkContainer    *container,
                                                   GtkWidget       *child);
static void     systray_box_remove                (GtkContainer    *container,
                                                   GtkWidget       *child);
static void     systray_box_forall                (GtkContainer    *container,
                                                   gboolean         include_internals,
                                                   GtkCallback      callback,
                                                   gpointer         callback_data);
static GType    systray_box_child_type            (GtkContainer    *container);
static gint     systray_box_compare_function      (gconstpointer    a,
                                                   gconstpointer    b);



enum
{
  PROP_0,
  PROP_HAS_HIDDEN
};

struct _SystrayBoxClass
{
  GtkContainerClass __parent__;
};

struct _SystrayBox
{
  GtkContainer  __parent__;

  /* all the icons packed in this box */
  GSList       *childeren;

  /* orientation of the box */
  guint         horizontal : 1;

  /* hidden childeren counter */
  gint          n_hidden_childeren;
  gint          n_visible_children;

  /* whether hidden icons are visible */
  guint         show_hidden : 1;

  /* maximum icon size */
  gint          size_max;

  /* allocated size by the plugin */
  gint          size_alloc;
};



BLADE_BAR_DEFINE_TYPE (SystrayBox, systray_box, GTK_TYPE_CONTAINER)



static void
systray_box_class_init (SystrayBoxClass *klass)
{
  GObjectClass      *gobject_class;
  GtkWidgetClass    *gtkwidget_class;
  GtkContainerClass *gtkcontainer_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = systray_box_get_property;
  gobject_class->finalize = systray_box_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->size_request = systray_box_size_request;
  gtkwidget_class->size_allocate = systray_box_size_allocate;

  gtkcontainer_class = GTK_CONTAINER_CLASS (klass);
  gtkcontainer_class->add = systray_box_add;
  gtkcontainer_class->remove = systray_box_remove;
  gtkcontainer_class->forall = systray_box_forall;
  gtkcontainer_class->child_type = systray_box_child_type;

  g_object_class_install_property (gobject_class,
                                   PROP_HAS_HIDDEN,
                                   g_param_spec_boolean ("has-hidden",
                                                         NULL, NULL,
                                                         FALSE,
                                                         BLXO_PARAM_READABLE));
}



static void
systray_box_init (SystrayBox *box)
{
  GTK_WIDGET_SET_FLAGS (box, GTK_NO_WINDOW);

  box->childeren = NULL;
  box->size_max = SIZE_MAX_DEFAULT;
  box->size_alloc = SIZE_MAX_DEFAULT;
  box->n_hidden_childeren = 0;
  box->n_visible_children = 0;
  box->horizontal = TRUE;
  box->show_hidden = FALSE;
}



static void
systray_box_get_property (GObject      *object,
                          guint         prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  SystrayBox *box = XFCE_SYSTRAY_BOX (object);

  switch (prop_id)
    {
    case PROP_HAS_HIDDEN:
      g_value_set_boolean (value, box->n_hidden_childeren > 0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
systray_box_finalize (GObject *object)
{
  SystrayBox *box = XFCE_SYSTRAY_BOX (object);

  /* check if we're leaking */
  if (G_UNLIKELY (box->childeren != NULL))
    {
      /* free the child list */
      g_slist_free (box->childeren);
      g_debug ("Not all icons has been removed from the systray.");
    }

  G_OBJECT_CLASS (systray_box_parent_class)->finalize (object);
}



static void
systray_box_size_get_max_child_size (SystrayBox *box,
                                     gint        alloc_size,
                                     gint       *rows_ret,
                                     gint       *row_size_ret,
                                     gint       *offset_ret)
{
  GtkWidget *widget = GTK_WIDGET (box);
  gint       size;
  gint       rows;
  gint       row_size;

  alloc_size -= 2 * GTK_CONTAINER (widget)->border_width;

  /* count the number of rows that fit in the allocated space */
  for (rows = 1;; rows++)
    {
      size = rows * box->size_max + (rows - 1) * SPACING;
      if (size < alloc_size)
        continue;

      /* decrease rows if the new size doesn't fit */
      if (rows > 1 && size > alloc_size)
        rows--;

      break;
    }

  row_size = (alloc_size - (rows - 1) * SPACING) / rows;
  row_size = MIN (box->size_max, row_size);

  if (rows_ret != NULL)
    *rows_ret = rows;

  if (row_size_ret != NULL)
    *row_size_ret = row_size;

  if (offset_ret != NULL)
    {
      rows = MIN (rows, box->n_visible_children);
      *offset_ret = (alloc_size - (rows * row_size + (rows - 1) * SPACING)) / 2;
      if (*offset_ret < 1)
        *offset_ret = 0;
    }
}



static void
systray_box_size_request (GtkWidget      *widget,
                          GtkRequisition *requisition)
{
  SystrayBox     *box = XFCE_SYSTRAY_BOX (widget);
  GtkWidget      *child;
  gint            border;
  GtkRequisition  child_req;
  gint            n_hidden_childeren = 0;
  gint            rows;
  gdouble         cols;
  gint            row_size;
  gdouble         cells;
  gint            min_seq_cells = -1;
  gdouble         ratio;
  GSList         *li;
  gboolean        hidden;
  gint            col_px;
  gint            row_px;

  box->n_visible_children = 0;

  /* get some info about the n_rows we're going to allocate */
  systray_box_size_get_max_child_size (box, box->size_alloc, &rows, &row_size, NULL);

  for (li = box->childeren, cells = 0.00; li != NULL; li = li->next)
    {
      child = GTK_WIDGET (li->data);
      bar_return_if_fail (XFCE_IS_SYSTRAY_SOCKET (child));

      gtk_widget_size_request (child, &child_req);

      /* skip invisible requisitions (see macro) or hidden widgets */
      if (REQUISITION_IS_INVISIBLE (child_req)
          || !GTK_WIDGET_VISIBLE (child))
        continue;

      hidden = systray_socket_get_hidden (XFCE_SYSTRAY_SOCKET (child));
      if (hidden)
        n_hidden_childeren++;

      /* if we show hidden icons */
      if (!hidden || box->show_hidden)
        {
          /* special handling for non-squared icons. this only works if
           * the icon size ratio is > 1.00, if this is lower then 1.00
           * the icon implementation should respect the tray orientation */
          if (G_UNLIKELY (child_req.width != child_req.height))
            {
              ratio = (gdouble) child_req.width / (gdouble) child_req.height;
              if (!box->horizontal)
                ratio = 1 / ratio;

              if (ratio > 1.00)
                {
                  if (G_UNLIKELY (rows > 1))
                    {
                      /* align to whole blocks if we have multiple rows */
                      ratio = ceil (ratio);

                      /* update the min sequential number of blocks */
                      min_seq_cells = MAX (min_seq_cells, ratio);
                    }

                  cells += ratio;

                  continue;
                }
            }

          /* don't do anything with the actual size,
           * just count the number of cells */
          cells += 1.00;
          box->n_visible_children++;
        }
    }

  bar_debug_filtered (BAR_DEBUG_SYSTRAY,
      "requested cells=%g, rows=%d, row_size=%d, children=%d",
      cells, rows, row_size, box->n_visible_children);

  if (cells > 0.00)
    {
      cols = cells / (gdouble) rows;
      if (rows > 1)
        cols = ceil (cols);
      if (cols * rows < cells)
        cols += 1.00;

      /* make sure we have enough columns to fix the minimum amount of cells */
      if (min_seq_cells != -1)
        cols = MAX (min_seq_cells, cols);

      col_px = row_size * cols + (cols - 1) * SPACING;
      row_px = row_size * rows + (rows - 1) * SPACING;

      if (box->horizontal)
        {
          requisition->width = col_px;
          requisition->height = row_px;
        }
      else
        {
          requisition->width = row_px;
          requisition->height = col_px;
        }
    }
  else
    {
      requisition->width = 0;
      requisition->height = 0;
    }

  /* emit property if changed */
  if (box->n_hidden_childeren != n_hidden_childeren)
    {
      bar_debug_filtered (BAR_DEBUG_SYSTRAY,
          "hidden children changed (%d -> %d)",
          n_hidden_childeren, box->n_hidden_childeren);

      box->n_hidden_childeren = n_hidden_childeren;
      g_object_notify (G_OBJECT (box), "has-hidden");
    }

  /* add border size */
  border = GTK_CONTAINER (widget)->border_width * 2;
  requisition->width += border;
  requisition->height += border;
}



static void
systray_box_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  SystrayBox     *box = XFCE_SYSTRAY_BOX (widget);
  GtkWidget      *child;
  GtkAllocation   child_alloc;
  GtkRequisition  child_req;
  gint            border;
  gint            rows;
  gint            row_size;
  gdouble         ratio;
  gint            x, x_start, x_end;
  gint            y, y_start, y_end;
  gint            offset;
  GSList         *li;
  gint            alloc_size;
  gint            idx;

  widget->allocation = *allocation;

  border = GTK_CONTAINER (widget)->border_width;

  alloc_size = box->horizontal ? allocation->height : allocation->width;

  systray_box_size_get_max_child_size (box, alloc_size, &rows, &row_size, &offset);

  bar_debug_filtered (BAR_DEBUG_SYSTRAY, "allocate rows=%d, row_size=%d, w=%d, h=%d, horiz=%s, border=%d",
                        rows, row_size, allocation->width, allocation->height,
                        BAR_DEBUG_BOOL (box->horizontal), border);

  /* get allocation bounds */
  x_start = allocation->x + border;
  x_end = allocation->x + allocation->width - border;

  y_start = allocation->y + border;
  y_end = allocation->y + allocation->height - border;

  /* add offset to center the tray contents */
  if (box->horizontal)
    y_start += offset;
  else
    x_start += offset;

  restart_allocation:

  x = x_start;
  y = y_start;

  for (li = box->childeren; li != NULL; li = li->next)
    {
      child = GTK_WIDGET (li->data);
      bar_return_if_fail (XFCE_IS_SYSTRAY_SOCKET (child));

      if (!GTK_WIDGET_VISIBLE (child))
        continue;

      gtk_widget_get_child_requisition (child, &child_req);

      if (REQUISITION_IS_INVISIBLE (child_req)
          || (!box->show_hidden
              && systray_socket_get_hidden (XFCE_SYSTRAY_SOCKET (child))))
        {
          /* position hidden icons offscreen if we don't show hidden icons
           * or the requested size looks like an invisible icons (see macro) */
          child_alloc.x = child_alloc.y = OFFSCREEN;

          /* some implementations (hi nm-applet) start their setup on
           * a size-changed signal, so make sure this event is triggered
           * by allocation a normal size instead of 1x1 */
          child_alloc.width = child_alloc.height = row_size;
        }
      else
        {
          /* special case handling for non-squared icons */
          if (G_UNLIKELY (child_req.width != child_req.height))
            {
              ratio = (gdouble) child_req.width / (gdouble) child_req.height;

              if (box->horizontal)
                {
                  child_alloc.height = row_size;
                  child_alloc.width = row_size * ratio;
                  child_alloc.y = child_alloc.x = 0;

                  if (rows > 1)
                    {
                      ratio = ceil (ratio);
                      child_alloc.x = ((ratio * row_size) - child_alloc.width) / 2;
                    }
                }
              else
                {
                  ratio = 1 / ratio;

                  child_alloc.width = row_size;
                  child_alloc.height = row_size * ratio;
                  child_alloc.x = child_alloc.y = 0;

                  if (rows > 1)
                    {
                      ratio = ceil (ratio);
                      child_alloc.y = ((ratio * row_size) - child_alloc.height) / 2;
                    }
                }
            }
          else
            {
              /* fix icon to row size */
              child_alloc.width = row_size;
              child_alloc.height = row_size;
              child_alloc.x = 0;
              child_alloc.y = 0;

              ratio = 1.00;
            }

          if ((box->horizontal && x + child_alloc.width > x_end)
              || (!box->horizontal && y + child_alloc.height > y_end))
            {
              if (ratio >= 2
                  && li->next != NULL)
                {
                  /* child doesn't fit, but maybe we still have space for the
                   * next icon, so move the child 1 step forward in the list
                   * and restart allocating the box */
                  idx = g_slist_position (box->childeren, li);
                  box->childeren = g_slist_delete_link (box->childeren, li);
                  box->childeren = g_slist_insert (box->childeren, child, idx + 1);

                  goto restart_allocation;
                }

              if (box->horizontal)
                {
                  x = x_start;
                  y += row_size + SPACING;

                  if (y > y_end)
                    {
                      /* we overflow the number of rows, restart
                       * allocation with 1px smaller icons */
                      row_size--;

                      bar_debug_filtered (BAR_DEBUG_SYSTRAY,
                          "y overflow (%d > %d), restart with row_size=%d",
                          y, y_end, row_size);

                      goto restart_allocation;
                    }
                }
              else
                {
                  y = y_start;
                  x += row_size + SPACING;

                  if (x > x_end)
                    {
                      /* we overflow the number of rows, restart
                       * allocation with 1px smaller icons */
                      row_size--;

                      bar_debug_filtered (BAR_DEBUG_SYSTRAY,
                          "x overflow (%d > %d), restart with row_size=%d",
                          x, x_end, row_size);

                      goto restart_allocation;
                    }
                }
            }

          child_alloc.x += x;
          child_alloc.y += y;

          if (box->horizontal)
            x += row_size * ratio + SPACING;
          else
            y += row_size * ratio + SPACING;
        }

      bar_debug_filtered (BAR_DEBUG_SYSTRAY, "allocated %s[%p] at (%d,%d;%d,%d)",
          systray_socket_get_name (XFCE_SYSTRAY_SOCKET (child)), child,
          child_alloc.x, child_alloc.y, child_alloc.width, child_alloc.height);

      gtk_widget_size_allocate (child, &child_alloc);
    }
}



static void
systray_box_add (GtkContainer *container,
                 GtkWidget    *child)
{
  SystrayBox *box = XFCE_SYSTRAY_BOX (container);

  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));
  bar_return_if_fail (GTK_IS_WIDGET (child));
  bar_return_if_fail (child->parent == NULL);

  box->childeren = g_slist_insert_sorted (box->childeren, child,
                                          systray_box_compare_function);

  gtk_widget_set_parent (child, GTK_WIDGET (box));

  gtk_widget_queue_resize (GTK_WIDGET (container));
}



static void
systray_box_remove (GtkContainer *container,
                    GtkWidget    *child)
{
  SystrayBox *box = XFCE_SYSTRAY_BOX (container);
  GSList     *li;

  /* search the child */
  li = g_slist_find (box->childeren, child);
  if (G_LIKELY (li != NULL))
    {
      bar_assert (GTK_WIDGET (li->data) == child);

      /* unparent widget */
      box->childeren = g_slist_remove_link (box->childeren, li);
      gtk_widget_unparent (child);

      /* resize, so we update has-hidden */
      gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}



static void
systray_box_forall (GtkContainer *container,
                    gboolean      include_internals,
                    GtkCallback   callback,
                    gpointer      callback_data)
{
  SystrayBox *box = XFCE_SYSTRAY_BOX (container);
  GSList     *li, *lnext;

  /* run callback for all childeren */
  for (li = box->childeren; li != NULL; li = lnext)
    {
      lnext = li->next;
      (*callback) (GTK_WIDGET (li->data), callback_data);
    }
}



static GType
systray_box_child_type (GtkContainer *container)

{
  return GTK_TYPE_WIDGET;
}



static gint
systray_box_compare_function (gconstpointer a,
                              gconstpointer b)
{
  const gchar *name_a, *name_b;
  gboolean     hidden_a, hidden_b;

  /* sort hidden icons before visible ones */
  hidden_a = systray_socket_get_hidden (XFCE_SYSTRAY_SOCKET (a));
  hidden_b = systray_socket_get_hidden (XFCE_SYSTRAY_SOCKET (b));
  if (hidden_a != hidden_b)
    return hidden_a ? 1 : -1;

  /* sort icons by name */
  name_a = systray_socket_get_name (XFCE_SYSTRAY_SOCKET (a));
  name_b = systray_socket_get_name (XFCE_SYSTRAY_SOCKET (b));

#if GLIB_CHECK_VERSION (2, 16, 0)
  return g_strcmp0 (name_a, name_b);
#else
  if (name_a == NULL)
    return -(name_a != name_b);
  if (name_b == NULL)
    return name_a != name_b;

  return strcmp (name_a, name_b);
#endif
}



GtkWidget *
systray_box_new (void)
{
  return g_object_new (XFCE_TYPE_SYSTRAY_BOX, NULL);
}



void
systray_box_set_orientation (SystrayBox     *box,
                             GtkOrientation  orientation)
{
  gboolean horizontal;

  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));

  horizontal = !!(orientation == GTK_ORIENTATION_HORIZONTAL);
  if (G_LIKELY (box->horizontal != horizontal))
    {
      box->horizontal = horizontal;

      if (box->childeren != NULL)
        gtk_widget_queue_resize (GTK_WIDGET (box));
    }
}



void
systray_box_set_size_max (SystrayBox *box,
                          gint        size_max)
{
  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));

  size_max = CLAMP (size_max, SIZE_MAX_MIN, SIZE_MAX_MAX);

  if (G_LIKELY (size_max != box->size_max))
    {
      box->size_max = size_max;

      if (box->childeren != NULL)
        gtk_widget_queue_resize (GTK_WIDGET (box));
    }
}



gint
systray_box_get_size_max (SystrayBox *box)
{
  bar_return_val_if_fail (XFCE_IS_SYSTRAY_BOX (box), SIZE_MAX_DEFAULT);

  return box->size_max;
}



void
systray_box_set_size_alloc (SystrayBox *box,
                            gint        size_alloc)
{
  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));

  if (G_LIKELY (size_alloc != box->size_alloc))
    {
      box->size_alloc = size_alloc;

      if (box->childeren != NULL)
        gtk_widget_queue_resize (GTK_WIDGET (box));
    }
}



void
systray_box_set_show_hidden (SystrayBox *box,
                              gboolean   show_hidden)
{
  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));

  if (box->show_hidden != show_hidden)
    {
      box->show_hidden = show_hidden;

      if (box->childeren != NULL)
        gtk_widget_queue_resize (GTK_WIDGET (box));
    }
}



gboolean
systray_box_get_show_hidden (SystrayBox *box)
{
  bar_return_val_if_fail (XFCE_IS_SYSTRAY_BOX (box), FALSE);

  return box->show_hidden;
}



void
systray_box_update (SystrayBox *box)
{
  bar_return_if_fail (XFCE_IS_SYSTRAY_BOX (box));

  box->childeren = g_slist_sort (box->childeren,
                                 systray_box_compare_function);

  /* update the box, so we update the has-hidden property */
  gtk_widget_queue_resize (GTK_WIDGET (box));
}
