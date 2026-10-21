/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2026, step.
 *
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
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __VIEW_UTILS_H__
#define __VIEW_UTILS_H__

#include <gtk/gtk.h>

void view_utils_draw_rubberband(GtkWidget *widget, cairo_t *cr,
                                int x, int y, int width, int height);
gboolean view_utils_get_selection_color(GtkStyleContext *ctx,
                                        GtkStateFlags sf,
                                        GdkRGBA *color_out);
gboolean view_utils_get_selection_fg(GtkStyleContext *ctx,
                                     GtkStateFlags sf,
                                     GdkRGBA *color_out);

#endif /* __VIEW_UTILS_H__ */
