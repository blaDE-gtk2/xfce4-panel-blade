/* $Id$ */
/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __PANEL_GLUE_H__
#define __PANEL_GLUE_H__

#include <gtk/gtk.h>
#include <panel/panel-window.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4panel/xfce-panel-plugin-provider.h>

G_BEGIN_DECLS

void               panel_glue_popup_menu          (PanelWindow             *window);

XfceScreenPosition panel_glue_get_screen_position (PanelWindow             *window);

void               panel_glue_set_size            (PanelWindow             *window,
                                                   gint                     size);

void               panel_glue_set_orientation     (PanelWindow             *window,
                                                   GtkOrientation           orientation);

void               panel_glue_set_screen_position (PanelWindow             *window);

void               panel_glue_set_provider_info   (XfcePanelPluginProvider *provider);

G_END_DECLS

#endif /* !__PANEL_GLUE_H__ */
