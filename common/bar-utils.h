/*
 * Copyright (C) 2009-2010 Nick Schermer <nick@xfce.org>
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

#ifndef __BAR_BUILDER_H__
#define __BAR_BUILDER_H__

#include <gtk/gtk.h>
#include <libbladebar/libbladebar.h>

/* Hook to make sure GtkBuilder knows are the XfceTitledDialog object */
#define BAR_UTILS_LINK_4UI \
  if (xfce_titled_dialog_get_type () == 0) \
    return;

GtkBuilder *bar_utils_builder_new    (BladeBarPlugin  *bar_plugin,
                                        const gchar      *buffer,
                                        gsize             length,
                                        GObject         **dialog_return) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

void        bar_utils_show_help      (GtkWindow        *parent,
                                        const gchar      *page,
                                        const gchar      *offset);

gboolean    bar_utils_grab_available (void);

void        bar_utils_set_atk_info   (GtkWidget        *widget,
                                        const gchar      *name,
                                        const gchar      *description);

#endif /* !__BAR_BUILDER_H__ */
