/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2026, step <step.commons@gmail.com>.
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

/* view_utils.c - shared utilities for list view and icon view */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"
#include "view_utils.h"

/* Draw rubberband selection rectangle using CSS-defined colors and border.
 * The .rubberband CSS class is added to the widget's style context so that
 * gtk_render_background() and gtk_render_frame() pick up the rule from css.c.
 */
void view_utils_draw_rubberband(GtkWidget *widget, cairo_t *cr,
                                int x, int y, int width, int height)
{
	GtkStyleContext *ctx;

	ctx = gtk_widget_get_style_context(widget);
	gtk_style_context_save(ctx);
	gtk_style_context_add_class(ctx, "rubberband");
	gtk_style_context_set_state(ctx,
		gtk_widget_get_state_flags(widget));
	gtk_render_background(ctx, cr, x, y, width, height);
	gtk_render_frame(ctx, cr, x, y, width, height);
	gtk_style_context_restore(ctx);
}

/* Look up the selection/active background color from @define-color named
 * colors, falling back to the theme's background-color property.
 * Returns TRUE if a color was obtained.
 */
gboolean view_utils_get_selection_color(GtkStyleContext *ctx,
                                        GtkStateFlags sf,
                                        GdkRGBA *color_out)
{
	const char *name = NULL;

	if (sf & GTK_STATE_FLAG_SELECTED)
		name = "rox_selection_bg";
	else if (sf & GTK_STATE_FLAG_ACTIVE)
		name = "rox_active_bg";

	if (name && gtk_style_context_lookup_color(ctx, name, color_out))
		return TRUE;

	gtk_style_context_get(ctx, sf, "background-color", color_out, NULL);
	return TRUE;
}

/* Look up the selection/active foreground color from @define-color named
 * colors, falling back to the theme's color property.
 * Returns TRUE if a color was obtained.
 */
gboolean view_utils_get_selection_fg(GtkStyleContext *ctx,
                                     GtkStateFlags sf,
                                     GdkRGBA *color_out)
{
	const char *name = NULL;

	if (sf & GTK_STATE_FLAG_SELECTED)
		name = "rox_selection_fg";
	else if (sf & GTK_STATE_FLAG_ACTIVE)
		name = "rox_active_fg";

	if (name && gtk_style_context_lookup_color(ctx, name, color_out))
		return TRUE;

	gtk_style_context_get_color(ctx, sf, color_out);
	return TRUE;
}
