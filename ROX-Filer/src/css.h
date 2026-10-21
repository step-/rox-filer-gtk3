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

#ifndef __CSS_H__
#define __CSS_H__

#include <gtk/gtk.h>
#include "options.h"

extern Option o_view_show_focus_rect;

void css_init(void);
void css_check_theme_once(GtkWidget *widget);
void css_set_list_bg(const GdkRGBA *color);
GString *css_build_internal(gboolean dump);
void css_reload_external(void);

#endif /* __CSS_H__ */
