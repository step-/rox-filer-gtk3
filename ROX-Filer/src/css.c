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

/* css.c - consolidated CSS provider management */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"
#include "choices.h"
#include "options.h"
#include "filer.h"
#include "css.h"

Option o_view_show_focus_rect;

static GtkCssProvider *rox_css_provider = NULL;
static GtkCssProvider *ext_css_provider = NULL;
static GtkCssProvider *rox_override_provider = NULL;
static gboolean theme_checked = FALSE;
static gboolean theme_needs_help = FALSE;
static gboolean focus_css_active = FALSE;
static GdkRGBA list_bg_color;
static gboolean list_bg_active = FALSE;

/* Build the full CSS string from current state.
 * Rule order within the string determines the cascade winner at equal
 * specificity: later rules beat earlier rules within the same provider.
 * Pure GLib string building — no GTK init required.
 *
 * If 'dump' is TRUE, return value suitable for --dump-css;
 * otherwise return value matching the internal current state.
 */
GString *css_build_internal(gboolean dump)
{
	GString *css = g_string_new(NULL);

	/* The public named colors a theme should always define:
	 * https://gitlab.gnome.org/GNOME/gtk/blob/gtk-3-24/gtk/theme/Adwaita/_colors-public.scss
	*
	* Core Colors
	* @theme_bg_color / @theme_fg_color: Default window background and foreground (text).
	* @theme_base_color / @theme_text_color: Text entry and list/tree view background and text.
	* @theme_selected_bg_color / @theme_selected_fg_color: Selected items and highlight text.
	*
	* State Colors
	* @insensitive_bg_color / @insensitive_fg_color: Background and text for disabled widgets.
	* @insensitive_base_color: Background for disabled text entries.
	* @unfocused_bg_color / @unfocused_fg_color: Used when the window loses focus.
	*
	* Semantic Colors
	* @warning_color: Standard orange/yellow for warnings.
	* @error_color: Standard red for destructive actions or errors.
	* @success_color: Standard green for completed/successful actions.
	 */

	/* Color definitions (always) */
	g_string_append(css,
		"/* Selection and active colors. */\n"
		"@define-color rox_selection_bg @theme_selected_bg_color;\n"
		"@define-color rox_selection_fg @theme_selected_fg_color;\n"
		"@define-color rox_active_bg #dba11c;\n"
		"@define-color rox_active_fg @theme_selected_fg_color;\n");

	/* Menu dividers. Guarantee a visible
	 * divider while staying in the theme palette */
	g_string_append(css, "\n"
		"/* Menu dividers. */\n"
		"menu separator {\n"
		"  margin: 6px 6px;\n"
		"  background-color: mix(@theme_fg_color, @theme_bg_color, 0.50);\n"
		"}\n");

	/* Focus overrides (conditional) */
	if (dump || focus_css_active) {
		g_string_append(css, "\n"
			"/* Fallback keyboard focus indicator. */\n"
			"/* Active when the GTK theme lacks outline support. */\n"
			"/* Disable via Options dialog. */\n"
			"treeview.view:focus, Collection:focus {\n"
			"  outline-style: solid;\n"
			"  outline-width: 2px;\n"
			"  outline-color: @theme_selected_bg_color;\n"
			"  outline-offset: -2px;\n"
			"}\n");
	}

	/* List background (BEFORE rubberband — rubberband wins at equal
	 * specificity because it appears later in the cascade) */
	if (dump || list_bg_active) {
		if (dump) {
			g_string_append(css, "\n"
				"/* List view background color. */\n"
				"/* Uncomment to override the background color set in Options > Colours. */\n"
				"/*\n"
				"treeview.view.rox-custom-bg {\n"
				"  background-color: @theme_bg_color;\n"
				"}\n"
				"*/\n");
		} else {
			g_string_append(css, "\n"
				"/* List view background color. */\n");
			g_string_append_printf(css,
				"treeview.view.rox-custom-bg {\n"
				"  background-color: rgb(%d, %d, %d);\n"
				"}\n",
				(int)(list_bg_color.red * 255),
				(int)(list_bg_color.green * 255),
				(int)(list_bg_color.blue * 255));
		}
	}

	/* Rubberband selection (always — rendered via gtk_render_background/frame;
	 * placed AFTER list background so it wins at equal specificity (0,3,0)) */
	g_string_append(css, "\n"
		"/* Rubberband selection rectangle.  Always active. */\n"
		"treeview.view.rubberband, Collection.rubberband {\n"
		"  background-color: rgba(222, 241, 255, 0.5);\n"
		"  border-width: 1px;\n"
		"  border-style: solid;\n"
		"  border-color: #1b6acb;\n"
		"}\n");

	/* Pinboard rubberband — no fill, border visible on dark and light wallpapers */
	g_string_append(css, "\n"
		"/* Pinboard lasso selection rectangle. */\n"
		"#rox-pinboard .rubberband {\n"
		"  background-color: transparent;\n"
		"  border-width: 2px;\n"
		"  border-style: dotted;\n"
		"  border-color: #1b6acb;\n"
		"}\n");

	/* Wink outline (always, AFTER rubberband to win at equal
	 * specificity: Collection.wink-outline beats Collection:focus;
	 * treeview.view.wink-outline beats treeview.view:focus) */
	g_string_append(css, "\n"
		"/* Wink outline. */\n"
		"/* Flashed on the source item after copy/move/link/go-up. */\n"
		"Collection.wink-outline, treeview.view.wink-outline {\n"
		"  outline-style: solid;\n"
		"  outline-width: 2px;\n"
		"  outline-color: @rox_active_bg;\n"
		"  outline-offset: 0px;\n"
		"}\n");

	/* @keyframes and animation required for view_details_wink_item() */
	if (!dump) {
		g_string_append(css,
			"@keyframes wink-outline-flash {\n"
			"  0% { outline-width: 1px; }\n"
			"  100% { outline-width: 0px; }\n"
			"}\n"
			"Collection.wink-outline, treeview.view.wink-outline {\n"
			"  animation: wink-outline-flash 70ms linear 7;\n"
			"}\n");
	}

	/* Minibuffer — opaque white background to prevent bleed through. */
	g_string_append(css, "\n"
		"/* Minibuffer entry.  Activated with !, ?, g, etc. */\n"
		"#rox-minibuffer {\n"
		"  background-color: @theme_base_color;\n"
		"  color: @theme_text_color;\n"
		"}\n"
		"\n"
		"/* Running-as notice. */\n"
		"#rox-filer-message {\n"
		"  background-color: @theme_base_color;\n"
		"}\n"
		"\n"
		"/* Minibuffer area (help button + label + entry). */\n"
		"#rox-minibuffer-area {\n"
		"  background-color: @theme_bg_color;\n"
		"}\n");

	/* Toolbar label spacing.
	 * Allow users to compensate gaps between icon and label.
	 * The reference Adwaita theme needs no compensation → 0px margins.
	 * Use GTK3 CSS :dir() pseudo-class for LTR/RTL support. */
	g_string_append(css, "\n"
		"/* Toolbar label margin (text beside icon layout). */\n"
		"#rox-toolbar box.horizontal label:dir(ltr) {\n"
		"  margin-left: 0px;\n"
		"}\n"
		"#rox-toolbar box.horizontal label:dir(rtl) {\n"
		"  margin-right: 0px;\n"
		"}\n"
		"\n"
		"/* Toolbar label margin (text under icon layout). */\n"
		"#rox-toolbar box.vertical label:dir(ltr) {\n"
		"  margin-left: 0px;\n"
		"}\n"
		"#rox-toolbar box.vertical label:dir(rtl) {\n"
		"  margin-right: 0px;\n"
		"}\n"
		"\n"
		"/* Toolbar border. */\n"
		"/* If visible, set a bg color or the desktop will bleed through. */\n"
		"#rox-toolbar {\n"
		"  background-color: @theme_bg_color; background-image: none; border-bottom-style: none;\n"
		"}\n");

	/* Scrollbar trough — opaque to prevent desktop bleed-through
	 * when the filer window uses RGBA visual. */
	g_string_append(css, "\n"
		"/* Scrollbar. */\n"
		"/* If visible, set a bg color or the desktop will bleed through. */\n"
		"scrollbar, scrollbar trough {\n"
		"  background-color: @theme_bg_color;\n"
		"}\n"
		"scrollbar slider {\n"
		"  background-color: alpha(currentColor, 0.5);\n"
		"}\n");

	return css;
}

/* Rebuild the full CSS string and reload the internal provider. */
static void update_rox_css(void)
{
	GString *css = css_build_internal(FALSE);

	if (!rox_css_provider) {
		rox_css_provider = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(rox_css_provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	gtk_css_provider_load_from_data(rox_css_provider,
		css->str, css->len, NULL);
	g_string_free(css, TRUE);
}

static void options_changed(void)
{
	if (!o_view_show_focus_rect.has_changed)
		return;

	focus_css_active = theme_needs_help &&
			   o_view_show_focus_rect.int_value;
	update_rox_css();

	GList *next;
	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *fw = (FilerWindow *) next->data;
		gtk_widget_queue_draw(fw->window);
	}
}

/* Load or reload the external stylesheet from the Choices directory.
 * At USER priority (700) it overrides internal rules at APPLICATION
 * priority (600) at equal specificity.
 */
static void css_load_external(void)
{
	gchar *path;

	path = choices_find_xdg_path_load("style.css", project_name, SITE);

	if (!path) {
		/* No external stylesheet — remove stale provider if any */
		if (ext_css_provider) {
			gtk_style_context_remove_provider_for_screen(
				gdk_screen_get_default(),
				GTK_STYLE_PROVIDER(ext_css_provider));
			g_object_unref(ext_css_provider);
			ext_css_provider = NULL;
		}
		return;
	}

	if (!ext_css_provider)
		ext_css_provider = gtk_css_provider_new();

	if (gtk_css_provider_load_from_path(ext_css_provider, path, NULL)) {
		gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(ext_css_provider),
			GTK_STYLE_PROVIDER_PRIORITY_USER);
	} else {
		g_warning("Failed to load external stylesheet: %s", path);
		gtk_style_context_remove_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(ext_css_provider));
		g_object_unref(ext_css_provider);
		ext_css_provider = NULL;
	}

	g_free(path);
}

void css_init(void)
{
	static GtkCssProvider *defaults = NULL;

	option_add_int(&o_view_show_focus_rect, "view_show_focus_rect", TRUE);
	option_add_notify(options_changed);

	/* Pinboard default background — FALLBACK priority so per-widget
	 * overrides via widget_set_bg_color() at APPLICATION win. */
	defaults = gtk_css_provider_new();
	gtk_css_provider_load_from_data(defaults,
		"#rox-pinboard {\n"
		"  background-color: #666666;\n"
		"}\n",
		-1, NULL);
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(defaults),
		GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);

	/* Override provider — rules that must beat user CSS
	 * (PRIORITY_USER = 800). For example, Preventing the rubberband
	 * from rendering the user's background-image requires priority
	 * higher than the user's style.css provider.  */
	rox_override_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(rox_override_provider,
		"treeview.view.rubberband, Collection.rubberband {\n"
		"  background-image: none;\n"
		"  background-color: rgba(222, 241, 255, 0.5);\n"
		"}\n",
		-1, NULL);
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(rox_override_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER + 1);

	update_rox_css();
	css_load_external();
}

void css_check_theme_once(GtkWidget *widget)
{
	GtkStyleContext *ctx;
	GtkBorderStyle style = GTK_BORDER_STYLE_NONE;
	gint width = 0;
	GdkRGBA color = { 0 };

	if (theme_checked)
		return;
	theme_checked = TRUE;

	ctx = gtk_widget_get_style_context(widget);
	gtk_style_context_save(ctx);
	gtk_style_context_set_state(ctx,
		gtk_widget_get_state_flags(widget));

	gtk_style_context_get(ctx,
		gtk_style_context_get_state(ctx),
		"outline-style", &style,
		"outline-width", &width,
		"outline-color", &color,
		NULL);
	gtk_style_context_restore(ctx);

	theme_needs_help = (width == 0 ||
			    style == GTK_BORDER_STYLE_NONE ||
			    style == GTK_BORDER_STYLE_HIDDEN ||
			    color.alpha == 0);

	if (theme_needs_help && o_view_show_focus_rect.int_value)
	{
		focus_css_active = TRUE;
		update_rox_css();
	}
}

/* Set or clear the list view (treeview.view) NORMAL background color.
 * Rebuilds the consolidated CSS with or without the background rule.
 */
void css_set_list_bg(const GdkRGBA *color)
{
	if (color)
	{
		list_bg_color = *color;
		list_bg_active = TRUE;
	}
	else
	{
		list_bg_active = FALSE;
	}

	update_rox_css();
}

/* Public reload: drop existing external provider, re-read style.css.
 * Called by config dialog button and (future) SOAP RPC.
 */
void css_reload_external(void)
{
	if (ext_css_provider) {
		gtk_style_context_remove_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(ext_css_provider));
		g_object_unref(ext_css_provider);
		ext_css_provider = NULL;
	}

	css_load_external();

	GList *next;
	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *fw = (FilerWindow *) next->data;
		gtk_widget_queue_draw(fw->window);
	}
}
