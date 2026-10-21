/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
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
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* gui_support.c - general (GUI) support routines */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "support.h"
#include "pixmaps.h"
#include "choices.h"
#include "options.h"
#include "run.h"
#include "filer.h"

gint	screen_width, screen_height;

gint		n_monitors;
GdkRectangle	*monitor_geom = NULL;
gint		monitor_width, monitor_height;
MonitorAdjacent *monitor_adjacent;

static GdkAtom xa_cardinal;
GdkAtom xa__NET_WORKAREA = GDK_NONE;
GdkAtom xa__NET_WM_DESKTOP = GDK_NONE;
GdkAtom xa__NET_CURRENT_DESKTOP = GDK_NONE;
GdkAtom xa__NET_NUMBER_OF_DESKTOPS = GDK_NONE;

static GtkWidget *current_dialog = NULL;

/* Topmost open prompt (error/info popup, replace confirm, unmount question),
 * so a later prompt such as the --exit confirmation can parent to the window
 * that actually has the user's attention instead of a stale filer window.
 * Cleared automatically when the dialog is destroyed. [20260919080217]
 */
static GtkWidget *current_modal_dialog = NULL;

static void current_modal_dialog_gone(gpointer data, GObject *gone)
{
	if (current_modal_dialog == (GtkWidget *) gone)
		current_modal_dialog = NULL;
}

/* Remember a visible prompt as the transient parent for prompts that open
 * on top of it. Replaces any previous registration.
 */
void rox_dialog_note_modal(GtkWidget *dialog)
{
	g_return_if_fail(GTK_IS_WINDOW(dialog));

	if (current_modal_dialog == dialog)
		return;

	if (current_modal_dialog)
	{
		g_object_weak_unref(G_OBJECT(current_modal_dialog),
				current_modal_dialog_gone,
				current_modal_dialog);
	}

	current_modal_dialog = dialog;
	g_object_weak_ref(G_OBJECT(dialog),
			current_modal_dialog_gone, dialog);
}

static GtkWidget *tip_widget = NULL;
static time_t tip_time = 0; 	/* Time tip widget last closed */
static gint tip_timeout = 0;	/* When primed */

/* Static prototypes */
static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args);
static GType simple_image_get_type(void);
static void gui_get_monitor_adjacent(int monitor, MonitorAdjacent *adj);

void gui_store_screen_geometry(GdkScreen *screen)
{
	gint mon;
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *prim = gdk_display_get_primary_monitor(display);

	if (monitor_adjacent)
		g_free(monitor_adjacent);

	monitor_width = monitor_height = G_MAXINT;
	n_monitors = gdk_display_get_n_monitors(display);
	if (monitor_geom)
		g_free(monitor_geom);
	monitor_geom = g_new(GdkRectangle, n_monitors ? n_monitors : 1);

	if (n_monitors)
	{
		int min_x = G_MAXINT, min_y = G_MAXINT;
		int max_x = 0, max_y = 0;

		for (mon = 0; mon < n_monitors; ++mon)
		{
			gdk_monitor_get_geometry(gdk_display_get_monitor(display, mon),
					&monitor_geom[mon]);
			if (monitor_geom[mon].width < monitor_width)
				monitor_width = monitor_geom[mon].width;
			if (monitor_geom[mon].height < monitor_height)
				monitor_height = monitor_geom[mon].height;
			if (monitor_geom[mon].x < min_x)
				min_x = monitor_geom[mon].x;
			if (monitor_geom[mon].y < min_y)
				min_y = monitor_geom[mon].y;
			if (monitor_geom[mon].x + monitor_geom[mon].width > max_x)
				max_x = monitor_geom[mon].x + monitor_geom[mon].width;
			if (monitor_geom[mon].y + monitor_geom[mon].height > max_y)
				max_y = monitor_geom[mon].y + monitor_geom[mon].height;
		}
		screen_width = max_x - min_x;
		screen_height = max_y - min_y;
		monitor_adjacent = g_new(MonitorAdjacent, n_monitors);
		for (mon = 0; mon < n_monitors; ++mon)
		{
			gui_get_monitor_adjacent(mon, &monitor_adjacent[mon]);
		}
	}
	else
	{
		n_monitors = 1;
		gdk_monitor_get_geometry(prim, &monitor_geom[0]);
		screen_width = monitor_geom[0].width;
		screen_height = monitor_geom[0].height;
		monitor_width = monitor_geom[0].width;
		monitor_height = monitor_geom[0].height;
		monitor_adjacent = g_new0(MonitorAdjacent, 1);
	}

}

void gui_support_init()
{
	gpointer klass;

	xa_cardinal = gdk_atom_intern("CARDINAL", FALSE);
        xa__NET_WORKAREA = gdk_atom_intern("_NET_WORKAREA", FALSE);
        xa__NET_WM_DESKTOP = gdk_atom_intern("_NET_WM_DESKTOP", FALSE);
        xa__NET_CURRENT_DESKTOP = gdk_atom_intern("_NET_CURRENT_DESKTOP",
                                                  FALSE);
        xa__NET_NUMBER_OF_DESKTOPS = gdk_atom_intern("_NET_NUMBER_OF_DESKTOPS",
                                                     FALSE);

	gui_store_screen_geometry(gdk_screen_get_default());

	/* Work around the scrollbar placement bug */
	klass = g_type_class_ref(gtk_scrolled_window_get_type());
	((GtkScrolledWindowClass *) klass)->scrollbar_spacing = 0;
	/* (don't unref, ever) */
}

/*
 * GTK3 has no stock text for icon names, so when a caller passes NULL text
 * we supply the standard localized label for the handful of icons used that
 * way. Pass the icon name through unchanged if there is no known label.
 */
static const gchar *stock_label_for_icon(const gchar *stock)
{
	if (!strcmp(stock, "process-stop"))
		return _("_Cancel");
	if (!strcmp(stock, "edit-delete"))
		return _("_Delete");
	if (!strcmp(stock, "window-close"))
		return _("_Close");
	return stock;
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 *
 * If a dialog is already open, returns -1 without waiting AND
 * brings the current dialog to the front.
 *
 * Each button has two arguments, a GTK_STOCK icon and some text. If the
 * text is NULL, the stock's text is used.
 */
int get_choice(const char *title,
	       const char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*button = NULL;
	int		i, retval;
	va_list	ap;

	if (current_dialog)
	{
		gtk_widget_hide(current_dialog);
		gtk_widget_show(current_dialog);
		gtk_window_present(GTK_WINDOW(current_dialog)); /* [20260917083050] */
		return -1;
	}

	current_dialog = dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_NONE,
					"%s", message);
	rox_dialog_set_parent_full(dialog, TRUE);
	/* [20260917083050] Block --exit while asking, without changing
	 * destroy-with-parent; never dangle current_dialog for nested
	 * re-entry after an external destroy. */
	rox_dialog_block_exit(dialog);
	g_signal_connect(dialog, "destroy",
			G_CALLBACK(gtk_widget_destroyed), &current_dialog);
	/* [20260919080217] Parent to the prompt that actually has the user's
	 * attention (e.g. an error popup) instead of a stale filer window,
	 * so it stacks above the parent. */
	if (current_modal_dialog && current_modal_dialog != dialog)
	{
		gtk_window_set_transient_for(GTK_WINDOW(dialog),
				GTK_WINDOW(current_modal_dialog));
	}
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	va_start(ap, number_of_buttons);

	for (i = 0; i < number_of_buttons; i++)
	{
		const char *stock = va_arg(ap, char *);
		const char *text = va_arg(ap, char *);

		if (text)
			button = button_new_mixed(stock, text);
		else
			button = button_new_mixed(stock,
					stock_label_for_icon(stock));

		gtk_widget_set_can_default(button, TRUE);
		gtk_widget_show(button);

		gtk_dialog_add_action_widget(GTK_DIALOG(current_dialog),
						button, i);
	}

	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), i - 1);

	va_end(ap);

	/* Show before present: present() on an unmapped window is a
	 * no-op, and gtk_dialog_run() would otherwise show it without
	 * ever requesting focus. [20260917083050] */
	gtk_widget_show(dialog);
	gtk_window_present(GTK_WINDOW(dialog));

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_NONE)
		retval = -1;
	gtk_widget_destroy(dialog);

	current_dialog = NULL;

	return retval;
}

void info_message(const char *message, ...)
{
        va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_INFO, message, args);
}

/* Display a message in a window with "ROX-Filer" as title */
void report_error(const char *message, ...)
{
	va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_ERROR, message, args);
}

void set_cardinal_property(GdkWindow *window, GdkAtom prop, gulong value)
{
	gdk_property_change(window, prop, xa_cardinal, 32,
				GDK_PROP_MODE_REPLACE, (const guchar *) &value, 1);
}

gboolean get_cardinal_property(GdkWindow *window, GdkAtom prop, gulong length,
                               gulong *data, gint *actual_length)
{
        GdkAtom actual_type;
        gint actual_format, act_length;
        guchar *d;
        gulong *p;
        int i;
        gboolean ok;

        /* Cardinals are format=32 so the length in bytes is 4 * number of
         * cardinals */
        ok=gdk_property_get(window, prop, xa_cardinal,
                                     0, length*4, FALSE,
                                     &actual_type, &actual_format,
                                     &act_length, &d);

        if(!ok)
                return FALSE;

        /* Check correct format */
        if(actual_format!=32)
        {
                g_free(d);
                return FALSE;
        }

        /* Actual data for cardinals returned as longs, which may be 64 bit */
        if(act_length/sizeof(gulong)>length)
        {
                g_free(d);
                return FALSE;
        }

        /* Copy data into return array */
        p=(gulong *) d;
        for(i=0; i<act_length/sizeof(gulong); i++)
                data[i]=p[i];
        g_free(d);
        *actual_length=act_length/sizeof(gulong);

        return ok;
}

int get_current_desktop(void)
{
        gint act_len;
        gulong current;
        Window root=gdk_x11_get_default_root_xwindow();
        GdkWindow *gdk_root=gdk_x11_window_lookup_for_display(gdk_display_get_default(), root);
        int desk=0;

        if(get_cardinal_property(gdk_root, xa__NET_CURRENT_DESKTOP, 1,
                                 &current, &act_len) && act_len==1)
                desk=(int) current;

        return desk;
}

int get_number_of_desktops(void)
{
        gint act_len;
        gulong num;
        Window root=gdk_x11_get_default_root_xwindow();
        GdkWindow *gdk_root=gdk_x11_window_lookup_for_display(gdk_display_get_default(), root);
        int desks=1;

        if(get_cardinal_property(gdk_root, xa__NET_NUMBER_OF_DESKTOPS, 1,
                                 &num, &act_len) && act_len==1)
                desks=(int) num;

        return desks;
}

/* Get the working area for the desktop, excluding things like the Gnome
 * panels. */
void get_work_area(int *x, int *y, int *width, int *height)
{
        gint act_len;
        gulong *work_area;
        Window root=gdk_x11_get_default_root_xwindow();
        GdkWindow *gdk_root=gdk_x11_window_lookup_for_display(gdk_display_get_default(), root);
        int x0, y0, w0, h0;
        int idesk, ndesk, nval;

        idesk=get_current_desktop();
        ndesk=get_number_of_desktops();
        nval=4*ndesk;
        work_area=g_new(gulong, nval);

        if(get_cardinal_property(gdk_root, xa__NET_WORKAREA, nval,
                                         work_area, &act_len) &&
           act_len==nval)
        {
                x0 = work_area[idesk*4+0];
                y0 = work_area[idesk*4+1];
                w0 = work_area[idesk*4+2];
                h0 = work_area[idesk*4+3];
        }
        else
        {
                x0 = y0 = 0;
                w0 = screen_width;
                h0 = screen_height;
        }

        g_free(work_area);

        if(x)
                *x = x0;
        if(y)
                *y = y0;
        if(width)
                *width = w0;
        if(height)
                *height = h0;
}

/* NB: Also used for pinned icons.
 * TODO: Set the level here too.
 */
void make_panel_window(GtkWidget *widget)
{
	static gboolean need_init = TRUE;
	static GdkAtom xa_state, xa_atom, xa_hints, xa_win_hints;
	GdkWindow *window = gtk_widget_get_window(widget);
	long wm_hints_values[] = {1, False, 0, 0, 0, 0, 0, 0};
	GdkAtom	wm_protocols[2];

	g_return_if_fail(window != NULL);

	if (o_override_redirect.int_value)
	{
		gdk_window_set_override_redirect(window, TRUE);
		return;
	}

	if (need_init)
	{
		xa_win_hints = gdk_atom_intern("_WIN_HINTS", FALSE);
		xa_state = gdk_atom_intern("_WIN_STATE", FALSE);
		xa_atom = gdk_atom_intern("ATOM", FALSE);
		xa_hints = gdk_atom_intern("WM_HINTS", FALSE);

		need_init = FALSE;
	}

	gdk_window_set_decorations(window, 0);
	gdk_window_set_functions(window, 0);
	gtk_window_set_resizable(GTK_WINDOW(widget), FALSE);

	/* Don't hide panel/pinboard windows initially (WIN_STATE_HIDDEN).
	 * Needed for IceWM - Christopher Arndt <chris.arndt@web.de>
	 */
	set_cardinal_property(window, xa_state,
			WIN_STATE_STICKY |
			WIN_STATE_FIXED_POSITION | WIN_STATE_ARRANGE_IGNORE);

	set_cardinal_property(window, xa_win_hints,
			WIN_HINTS_SKIP_FOCUS | WIN_HINTS_SKIP_WINLIST |
			WIN_HINTS_SKIP_TASKBAR);

	/* Appear on all workspaces */
	set_cardinal_property(window, xa__NET_WM_DESKTOP, 0xffffffff);

	gdk_property_change(window, xa_hints, xa_hints, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_hints_values,
			sizeof(wm_hints_values) / sizeof(long));

	wm_protocols[0] = gdk_atom_intern("WM_DELETE_WINDOW", FALSE);
	wm_protocols[1] = gdk_atom_intern("_NET_WM_PING", FALSE);
	gdk_property_change(window,
			gdk_atom_intern("WM_PROTOCOLS", FALSE), xa_atom, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_protocols,
			sizeof(wm_protocols) / sizeof(GdkAtom));

	gdk_window_set_skip_taskbar_hint(window, TRUE);
	gdk_window_set_skip_pager_hint(window, TRUE);

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(widget),
					"accept_focus"))
	{
		GValue vfalse = { 0, };
		g_value_init(&vfalse, G_TYPE_BOOLEAN);
		g_value_set_boolean(&vfalse, FALSE);
		g_object_set_property(G_OBJECT(widget),
					"accept_focus", &vfalse);
		g_value_unset(&vfalse);
	}
}

static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;

	report_error("%s", *error);
	null_g_free(error);

	one_less_window();
	return FALSE;
}

/* Display an error with "ROX-Filer" as title next time we are idle.
 * If multiple errors are reported this way before the window is opened,
 * all are displayed in a single window.
 * If an error is reported while the error window is open, it is discarded.
 */
void delayed_error(const char *error, ...)
{
	static char *delayed_error_data = NULL;
	char *old, *new;
	va_list args;

	g_return_if_fail(error != NULL);

	old = delayed_error_data;

	va_start(args, error);
	new = g_strdup_vprintf(error, args);
	va_end(args);

	if (old)
	{
		delayed_error_data = g_strconcat(old,
				_("\n---\n"),
				new, NULL);
		g_free(old);
		g_free(new);
	}
	else
	{
		delayed_error_data = new;
		g_idle_add(error_idle_cb, &delayed_error_data);

		number_of_windows++;
	}
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(const char *pathname, char **data_out, long *length_out)
{
	gsize len;
	GError *error = NULL;

	if (!g_file_get_contents(pathname, data_out, &len, &error))
	{
		delayed_error("%s", error->message);
		g_error_free(error);
		return FALSE;
	}

	if (length_out)
		*length_out = len;
	return TRUE;
}

GtkWidget *new_help_button(HelpFunc show_help, gpointer data)
{
	GtkWidget	*b, *icon;

	b = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	icon = gtk_image_new_from_icon_name("help-browser",
					GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(b), icon);
	g_signal_connect_swapped(b, "clicked", G_CALLBACK(show_help), data);

	gtk_widget_set_can_focus(b, FALSE);

	return b;
}

/* Read file into memory. Call parse_line(gchar *line) for each line
 * in the file. Callback returns NULL on success, or an error message
 * if something went wrong. Only the first error is displayed to the user.
 */
void parse_file(const char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;
	gboolean	seen_error = FALSE;

	if (load_file(path, &data, &length))
	{
		char *eol;
		const char *error;
		char *line = data;
		int  line_number = 1;

		if (strncmp(data, "<?xml ", 6) == 0)
		{
			delayed_error(_("Attempt to read an XML file as "
					"a text file. File '%s' may be "
					"corrupted."), path);
			return;
		}

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error && !seen_error)
			{
				delayed_error(
		_("Error in '%s' file at line %d: "
		"\n\"%s\"\n"
		"This may be due to upgrading from a previous version of "
		"ROX-Filer. Open the Options window and try changing something "
		"and then changing it back (causing the file to be resaved).\n"
		"Further errors will be ignored."),
					path,
					line_number,
					error);
				seen_error = TRUE;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Returns the position of the pointer.
 * TRUE if any modifier keys or mouse buttons are pressed.
 */
gboolean get_pointer_xy(int *x, int *y)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);
	GdkWindow *root;
	GdkModifierType mask;

	root = gdk_get_default_root_window();
	gdk_window_get_device_position(root, device, x, y, &mask);

	return mask != 0;
}

GdkWindow *gui_get_pointer(GdkWindow *relative_to,
			   gint *x, gint *y, GdkModifierType *mask)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);

	if (!relative_to)
		relative_to = gdk_get_default_root_window();

	return gdk_window_get_device_position(relative_to, device, x, y, mask);
}

int get_monitor_under_pointer(void)
{
	int x, y, i;
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *mon;

	get_pointer_xy(&x, &y);
	mon = gdk_display_get_monitor_at_point(display, x, y);
	for (i = 0; i < n_monitors; i++)
	{
		if (gdk_display_get_monitor(display, i) == mon)
			return i;
	}
	return 0;
}

#define DECOR_BORDER 32

/* Centre the window at these coords */
void centre_window(GdkWindow *window, int x, int y)
{
	int	w, h;
	int m;
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *mon;

	g_return_if_fail(window != NULL);

	mon = gdk_display_get_monitor_at_point(display, x, y);
	for (m = 0; m < n_monitors; m++)
	{
		if (gdk_display_get_monitor(display, m) == mon)
			break;
	}
	if (m == n_monitors)
		m = 0;

	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);

	x -= w / 2;
	y -= h / 2;

	gdk_window_move(window,
		CLAMP(x, DECOR_BORDER + monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width
			- w - DECOR_BORDER),
		CLAMP(y, DECOR_BORDER + monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height
			- h - DECOR_BORDER));
}

static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args)
{
	GtkWidget *dialog;
	gchar *s;

	g_return_if_fail(message != NULL);

	s = g_strdup_vprintf(message, args);
	va_end(args);

	dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					type,
					GTK_BUTTONS_OK,
					"%s", s);
	gtk_window_set_title(GTK_WINDOW(dialog), "ROX-Filer");
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	/* [20260919080217] Later prompts parent to this one. Cleared on destroy. */
	rox_dialog_note_modal(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	g_free(s);
}

static GtkWidget *current_wink_widget = NULL;
static gint	wink_timeout = -1;	/* Called when it's time to stop */
static gulong	wink_destroy;		/* Called if the widget dies first */

static gboolean end_wink(gpointer data)
{
	gtk_drag_unhighlight(current_wink_widget);

	g_signal_handler_disconnect(current_wink_widget, wink_destroy);

	current_wink_widget = NULL;

	return FALSE;
}

static void cancel_wink(void)
{
	g_source_remove(wink_timeout);
	end_wink(NULL);
}

static void wink_widget_died(gpointer data)
{
	current_wink_widget = NULL;
	g_source_remove(wink_timeout);
}

/* Draw a black box around this widget, briefly.
 * Note: uses the drag highlighting code for now.
 */
void wink_widget(GtkWidget *widget)
{
	g_return_if_fail(widget != NULL);

	if (current_wink_widget)
		cancel_wink();

	current_wink_widget = widget;
	gtk_drag_highlight(current_wink_widget);

	wink_timeout = g_timeout_add(300, (GSourceFunc) end_wink, NULL);

	wink_destroy = g_signal_connect_swapped(widget, "destroy",
				G_CALLBACK(wink_widget_died), NULL);
}

static gboolean idle_destroy_cb(GtkWidget *widget)
{
	g_object_unref(widget);
	gtk_widget_destroy(widget);
	return FALSE;
}

/* Destroy the widget in an idle callback */
void destroy_on_idle(GtkWidget *widget)
{
	g_object_ref(widget);
	g_idle_add((GSourceFunc) idle_destroy_cb, widget);
}

/* Spawn a child process (as spawn_full), and report errors.
 * Returns the child's PID on succes, or 0 on failure.
 */
gint rox_spawn(const gchar *dir, const gchar **argv)
{
	GError	*error = NULL;
	gint pid = 0;

	if (!g_spawn_async_with_pipes(dir, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,		/* Child setup fn */
			&pid,			/* Child PID */
			NULL, NULL, NULL,	/* Standard pipes */
			&error))
	{
		delayed_error("%s", error ? error->message : "(null)");
		g_error_free(error);

		return 0;
	}

	return pid;
}

GtkWidget *button_new_image_text(GtkWidget *image, const char *message)
{
	GtkWidget *button, *hbox, *label;

	button = gtk_button_new();
	label = gtk_label_new_with_mnemonic(message);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(hbox, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(button), hbox);
	gtk_widget_show_all(hbox);

	return button;
}

GtkWidget *button_new_mixed(const char *stock, const char *message)
{
	return button_new_image_text(gtk_image_new_from_icon_name(stock,
					       GTK_ICON_SIZE_BUTTON),
					message);
}

/* Highlight entry in red if 'error' is TRUE */
void entry_set_error(GtkWidget *entry, gboolean error)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context(entry);
	static GtkCssProvider *provider = NULL;

	if (!provider)
		provider = gtk_css_provider_new();

	if (error)
	{
		const char *css = "entry { color: #ff0000; background-color: #ffffff; }";
		gtk_css_provider_load_from_data(provider, css, -1, NULL);
		gtk_style_context_add_provider(ctx,
				GTK_STYLE_PROVIDER(provider),
				GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	else
	{
		gtk_style_context_remove_provider(ctx,
				GTK_STYLE_PROVIDER(provider));
	}
}

/* Change stacking position of higher to be just above lower.
 * If lower is NULL, put higher at the bottom of the stack.
 */
void window_put_just_above(GdkWindow *higher, GdkWindow *lower)
{
	if (o_override_redirect.int_value && lower)
	{
		XWindowChanges restack;
		GdkDisplay *display = gdk_display_get_default();

		gdk_x11_display_error_trap_push(display);

		restack.stack_mode = Above;

		restack.sibling = GDK_WINDOW_XID(lower);

		XConfigureWindow(GDK_DISPLAY_XDISPLAY(display), GDK_WINDOW_XID(higher),
				CWSibling | CWStackMode, &restack);

		gdk_display_flush(display);
		if (gdk_x11_display_error_trap_pop(display))
			g_warning("window_put_just_above()\n");
	}
	else
		gdk_window_lower(higher);	/* To bottom of stack */
}

/* Like gtk_fixed_move(), except not insanely slow */
void fixed_move_fast(GtkFixed *fixed, GtkWidget *widget, int x, int y)
{
	gtk_fixed_move(fixed, widget, x, y);
}

/* When the tips window closed, record the time. If we try to open another
 * tip soon, it will appear more quickly.
 */
static void tooltip_destroyed(gpointer data)
{
	time(&tip_time);
}

/* Display a tooltip-like widget near the pointer with 'text'. If 'text' is
 * NULL, close any current tooltip.
 */
void tooltip_show(gchar *text)
{
	GtkWidget *label;
	int	x, y, py;
	int	w, h;
	int m;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkDevice *device;
	GtkAllocation alloc;

	if (tip_timeout)
	{
		g_source_remove(tip_timeout);
		tip_timeout = 0;
	}

	if (tip_widget)
	{
		gtk_widget_destroy(tip_widget);
		tip_widget = NULL;
	}

	if (!text)
		return;

	/* Show the tip */
	tip_widget = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_set_name(tip_widget, "gtk-tooltips");
	gtk_style_context_add_class(
			gtk_widget_get_style_context(tip_widget), "rox-tooltip");

	label = gtk_label_new(text);
	gtk_widget_set_margin_start(label, 4);
	gtk_widget_set_margin_end(label, 4);
	gtk_widget_set_margin_top(label, 2);
	gtk_widget_set_margin_bottom(label, 2);
	gtk_container_add(GTK_CONTAINER(tip_widget), label);
	gtk_widget_show(label);
	gtk_widget_realize(tip_widget);

	gtk_widget_get_allocation(tip_widget, &alloc);
	w = alloc.width;
	h = alloc.height;
	display = gdk_display_get_default();
	seat = gdk_display_get_default_seat(display);
	device = gdk_seat_get_pointer(seat);
	gdk_device_get_position(device, NULL, &x, &py);

	{
		GdkMonitor *at_mon = gdk_display_get_monitor_at_point(display, x, py);
		for (m = 0; m < n_monitors; m++)
		{
			if (gdk_display_get_monitor(display, m) == at_mon)
				break;
		}
		if (m == n_monitors)
			m = 0;
	}

	x -= w / 2;
	y = py + 12; /* I don't know the pointer height so I use a constant */

	/* Now check for screen boundaries */
	x = CLAMP(x, monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width - w);
	y = CLAMP(y, monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height - h);

	/* And again test if pointer is over the tooltip window */
	if (py >= y && py <= y + h)
		y = py - h - 2;
	gtk_window_move(GTK_WINDOW(tip_widget), x, y);
	gtk_widget_show(tip_widget);

	g_signal_connect_swapped(tip_widget, "destroy",
			G_CALLBACK(tooltip_destroyed), NULL);
	time(&tip_time);
}

/* Call callback(user_data) after a while, unless cancelled.
 * Object is refd now and unref when cancelled / after callback called.
 */
void tooltip_prime(GSourceFunc callback, GObject *object)
{
	time_t  now;
	int	delay;

	g_return_if_fail(tip_timeout == 0);

	time(&now);
	delay = now - tip_time > 2 ? 1000 : 200;

	g_object_ref(object);
	tip_timeout = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
					 delay,
					 callback,
					 object,
					 g_object_unref);
}

/* Like gtk_widget_modify_font, but copes with font_desc == NULL.
 * Uses a per-widget CssProvider stored as object data so we can
 * replace it without leaking or touching the global cascade. */
void widget_modify_font(GtkWidget *widget, PangoFontDescription *font_desc)
{
	GtkStyleContext *ctx;
	GtkCssProvider *old, *provider;
	gchar *css, *size_str;

	g_return_if_fail(GTK_IS_WIDGET(widget));

	ctx = gtk_widget_get_style_context(widget);

	/* Remove previous per-widget font provider */
	old = g_object_get_data(G_OBJECT(widget), "rox-font-provider");
	if (old) {
		gtk_style_context_remove_provider(ctx, GTK_STYLE_PROVIDER(old));
		g_object_unref(old);
		g_object_set_data(G_OBJECT(widget), "rox-font-provider", NULL);
	}

	if (!font_desc)
		return;

	if (pango_font_description_get_size_is_absolute(font_desc))
		size_str = g_strdup_printf("%dpx",
				pango_font_description_get_size(font_desc));
	else
		size_str = g_strdup_printf("%dpt",
				pango_font_description_get_size(font_desc) / PANGO_SCALE);

	css = g_strdup_printf("* { font-family: %s; font-size: %s; }",
			pango_font_description_get_family(font_desc),
			size_str);
	g_free(size_str);

	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data(G_OBJECT(widget), "rox-font-provider", provider);
	g_free(css);
}

/* Set a per-widget background color via a private CssProvider.
 * Safe to call repeatedly — removes the old provider first. */
void widget_set_bg_color(GtkWidget *widget, const GdkRGBA *color)
{
	GtkStyleContext *ctx;
	GtkCssProvider *old, *provider;
	gchar *css;

	g_return_if_fail(GTK_IS_WIDGET(widget));

	ctx = gtk_widget_get_style_context(widget);

	old = g_object_get_data(G_OBJECT(widget), "rox-bg-provider");
	if (old) {
		gtk_style_context_remove_provider(ctx, GTK_STYLE_PROVIDER(old));
		g_object_unref(old);
		g_object_set_data(G_OBJECT(widget), "rox-bg-provider", NULL);
	}

	provider = gtk_css_provider_new();
	css = g_strdup_printf("* { background-color: rgba(%d, %d, %d, %d); }",
			(int)(color->red * 255), (int)(color->green * 255),
			(int)(color->blue * 255), (int)(color->alpha * 255));
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data(G_OBJECT(widget), "rox-bg-provider", provider);
	g_free(css);
}

/* Set a per-widget foreground color via a private CssProvider.
 * Safe to call repeatedly — removes the old provider first. */
void widget_set_fg_color(GtkWidget *widget, const GdkRGBA *color)
{
	GtkStyleContext *ctx;
	GtkCssProvider *old, *provider;
	gchar *css;

	g_return_if_fail(GTK_IS_WIDGET(widget));

	ctx = gtk_widget_get_style_context(widget);

	old = g_object_get_data(G_OBJECT(widget), "rox-fg-provider");
	if (old) {
		gtk_style_context_remove_provider(ctx, GTK_STYLE_PROVIDER(old));
		g_object_unref(old);
		g_object_set_data(G_OBJECT(widget), "rox-fg-provider", NULL);
	}

	provider = gtk_css_provider_new();
	css = g_strdup_printf("* { color: rgba(%d, %d, %d, %d); }",
			(int)(color->red * 255), (int)(color->green * 255),
			(int)(color->blue * 255), (int)(color->alpha * 255));
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data(G_OBJECT(widget), "rox-fg-provider", provider);
	g_free(css);
}

/*
 * Make 'dialog' behave as a child of the filer window that launched
 * the action: transient plus destroy-with-parent.  Window managers use
 * this relationship to focus the dialog instead of demoting it as a
 * focus stealer.  No-op when no filer window has focus
 * (pinboard/panel-only contexts keep current behaviour).
 */
void rox_dialog_set_parent(GtkWidget *dialog)
{
	rox_dialog_set_parent_full(dialog, TRUE);
}

static GList *exit_blockers = NULL;

static void exit_blocker_gone(gpointer data, GObject *dead)
{
	(void) dead;

	exit_blockers = g_list_remove(exit_blockers, data);
}

/*
 * As rox_dialog_set_parent(), but with control over destroy-with-parent:
 * transient action dialogs pass TRUE; persistent tool windows (Options,
 * shortcut editor, bookmarks editor) pass FALSE so they survive their
 * launching filer window.
 *
 * [20260914054209] FALSE dialogs additionally join the exit-blocker
 * registry (on close failed, new callers block --exit until they opt
 * out via rox_dialog_allow_exit()). Removal is automatic on destroy.
 */
void rox_dialog_set_parent_full(GtkWidget *dialog,
		gboolean destroy_with_parent)
{
	g_return_if_fail(GTK_IS_WINDOW(dialog));

	/* [20260917163517] Blocking --exit must not depend on focus. */

	if (!destroy_with_parent)
	{
		/* Dedup */
		exit_blockers = g_list_remove(exit_blockers, dialog);
		exit_blockers = g_list_append(exit_blockers, dialog);
		g_object_weak_ref(G_OBJECT(dialog), exit_blocker_gone, dialog);
	}

	if (!window_with_focus || !window_with_focus->window)
		return;

	gtk_window_set_transient_for(GTK_WINDOW(dialog),
			GTK_WINDOW(window_with_focus->window));
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),
			destroy_with_parent);
}

/* Opt out of blocking --exit.
 */
void rox_dialog_allow_exit(GtkWidget *dialog)
{
	g_return_if_fail(GTK_IS_WINDOW(dialog));

	exit_blockers = g_list_remove(exit_blockers, dialog);
}

/* Block --exit while the dialog lives, without changing destroy-with-
 * parent semantics. Removal is automatic on destroy via the same weak
 * ref as rox_dialog_set_parent_full(,FALSE). [20260917083050]
 */
void rox_dialog_block_exit(GtkWidget *dialog)
{
	g_return_if_fail(GTK_IS_WINDOW(dialog));

	/* Dedup */
	exit_blockers = g_list_remove(exit_blockers, dialog);
	exit_blockers = g_list_append(exit_blockers, dialog);
	g_object_weak_ref(G_OBJECT(dialog), exit_blocker_gone, dialog);
}

/* Return live dialogs that are currently blocking --exit.
 * Copy before destroying from return list.
 */
GList *rox_exit_blockers(void)
{
	return exit_blockers;
}

/* Confirm the action with the user. If action is NULL, the text from stock
 * is used.
 */
gboolean confirm(const gchar *message, const gchar *stock, const gchar *action)
{
	return get_choice(PROJECT, message, 2,
			  "process-stop", NULL,
			  stock, action) == 1;
}

struct _Radios {
	GList *widgets;

	void (*changed)(Radios *, gpointer data);
	gpointer changed_data;
};

/* Create a new set of radio buttons.
 * Use radios_add to add options, then radios_pack to put them into something.
 * The radios object will self-destruct with the first widget it contains.
 * changed(data) is called (if not NULL) when pack is called, and on any
 * change after that.
 */
Radios *radios_new(void (*changed)(Radios *, gpointer data), gpointer data)
{
	Radios *radios;

	radios = g_new(Radios, 1);

	radios->widgets = NULL;
	radios->changed = changed;
	radios->changed_data = data;

	return radios;
}

static void radios_free(GtkWidget *radio, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	g_list_free(radios->widgets);
	g_free(radios);
}

void radios_add(Radios *radios, const gchar *tip, gint value,
		const gchar *label, ...)
{
	GtkWidget *radio;
	GSList *group = NULL;
	gchar *s;
	va_list args;

	g_return_if_fail(radios != NULL);
	g_return_if_fail(label != NULL);

	va_start(args, label);
	s = g_strdup_vprintf(label, args);
	va_end(args);

	if (radios->widgets)
	{
		GtkRadioButton *first = GTK_RADIO_BUTTON(radios->widgets->data);
		group = gtk_radio_button_get_group(first);
	}

	radio = gtk_radio_button_new_with_label(group, s);
	gtk_label_set_line_wrap(GTK_LABEL(gtk_bin_get_child(GTK_BIN(radio))), TRUE);
	gtk_widget_show(radio);
	if (tip)
		gtk_widget_set_tooltip_text(radio, tip);
	if (!group)
		g_signal_connect(G_OBJECT(radio), "destroy",
				G_CALLBACK(radios_free), radios);

	radios->widgets = g_list_prepend(radios->widgets, radio);
	g_object_set_data(G_OBJECT(radio), "rox-radios-value",
			  GINT_TO_POINTER(value));
}

static void radio_toggled(GtkToggleButton *button, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	if (button && !gtk_toggle_button_get_active(button))
		return;	/* Stop double-notifies */

	if (radios->changed)
		radios->changed(radios, radios->changed_data);
}

void radios_pack(Radios *radios, GtkBox *box)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = g_list_last(radios->widgets); next; next = next->prev)
	{
		GtkWidget *button = GTK_WIDGET(next->data);

		gtk_box_pack_start(box, button, FALSE, TRUE, 0);
		g_signal_connect(button, "toggled",
				G_CALLBACK(radio_toggled), radios);
	}
	radio_toggled(NULL, radios);
}

void radios_set_value(Radios *radios, gint value)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);
		int radio_value;

		radio_value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(radio),
						"rox-radios-value"));

		if (radio_value == value)
		{
			gtk_toggle_button_set_active(radio, TRUE);
			return;
		}
	}

	g_warning("Value %d not in radio group!", value);
}

gint radios_get_value(Radios *radios)
{
	GList *next;

	g_return_val_if_fail(radios != NULL, -1);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);

		if (gtk_toggle_button_get_active(radio))
			return GPOINTER_TO_INT(g_object_get_data(
					G_OBJECT(radio), "rox-radios-value"));
	}

	g_warning("Nothing in the radio group is selected!");

	return -1;
}

/* Convert a list of URIs as a string into a GList of EscapedPath URIs.
 * No unescaping is done.
 * Lines beginning with # are skipped.
 * The text block passed in is zero terminated (after the final CRLF)
 */
GList *uri_list_to_glist(const char *uri_list)
{
	GQueue gq = G_QUEUE_INIT;

	while (*uri_list)
	{
		char	*linebreak;
		int	length;

		linebreak = strchr(uri_list, 13);

		if (!linebreak || linebreak[1] != 10)
		{
			/* If this is the first, append it anyway (Firefox
			 * 3.5) */
			if (!gq.length && uri_list[0] != '#')
				g_queue_push_tail(&gq, g_strdup(uri_list));
			else
				g_warning("uri_list_to_glist: %s",
						_("Incorrect or missing line "
							"break in text/uri-list data"));
			return gq.head;
		}

		length = linebreak - uri_list;

		if (length && uri_list[0] != '#')
			g_queue_push_tail(&gq, g_strndup(uri_list, length));

		uri_list = linebreak + 2;
	}

	return gq.head;
}

typedef struct _SimpleImageClass SimpleImageClass;
typedef struct _SimpleImage SimpleImage;

struct _SimpleImageClass {
	GtkWidgetClass parent;
};

struct _SimpleImage {
	GtkWidget widget;

	GdkPixbuf *pixbuf;
	int	  width, height;
};

#define SIMPLE_IMAGE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
				simple_image_get_type(), SimpleImage))

static void simple_image_finialize(GObject *object)
{
	SimpleImage *image = SIMPLE_IMAGE(object);

	g_object_unref(G_OBJECT(image->pixbuf));
	image->pixbuf = NULL;
}

static void simple_image_get_preferred_width(GtkWidget *widget,
					     int *minimal_width,
					     int *natural_width)
{
	SimpleImage *image = (SimpleImage *) widget;

	*natural_width = *minimal_width = image->width;
}

static void simple_image_get_preferred_height(GtkWidget *widget,
					      int *minimal_height,
					      int *natural_height)
{
	SimpleImage *image = (SimpleImage *) widget;

	*natural_height = *minimal_height = image->height;
}

/* Render a pixbuf without messing up the clipping */
void render_pixbuf(GdkPixbuf *pixbuf, GdkWindow *target, cairo_t *cr,
		   int x, int y, int width, int height)
{
	cairo_save(cr);
	gdk_cairo_set_source_pixbuf(cr, pixbuf, x, y);
	if (width > 0 && height > 0)
		cairo_rectangle(cr, x, y, width, height);
	cairo_paint(cr);
	cairo_restore(cr);
}

static gboolean simple_image_draw(GtkWidget *widget, cairo_t *cr)
{
	SimpleImage *image = (SimpleImage *) widget;
	GtkAllocation alloc;
	int x;

	gtk_widget_get_allocation(widget, &alloc);

	x = alloc.x +
		(alloc.width - image->width) / 2;

	render_pixbuf(image->pixbuf, gtk_widget_get_window(widget), cr,
			x, alloc.y,
			image->width, image->height);

	return FALSE;
}

static void simple_image_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;
	GtkWidgetClass *widget = (GtkWidgetClass *) gclass;

	object->finalize = simple_image_finialize;
	widget->get_preferred_width = simple_image_get_preferred_width;
	widget->get_preferred_height = simple_image_get_preferred_height;
	widget->draw = simple_image_draw;
}

static void simple_image_init(GTypeInstance *object, gpointer gclass)
{
	gtk_widget_set_has_window(GTK_WIDGET(object), FALSE);
}

static GType simple_image_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (SimpleImageClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			simple_image_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(SimpleImage),
			0,			/* n_preallocs */
			simple_image_init,
		};

		type = g_type_register_static(gtk_widget_get_type(),
						"SimpleImage", &info, 0);
	}

	return type;
}

GtkWidget *simple_image_new(GdkPixbuf *pixbuf)
{
	SimpleImage *image;

	g_return_val_if_fail(pixbuf != NULL, NULL);

	image = g_object_new(simple_image_get_type(), NULL);

	image->pixbuf = pixbuf;
	g_object_ref(G_OBJECT(pixbuf));

	image->width = gdk_pixbuf_get_width(pixbuf);
	image->height = gdk_pixbuf_get_height(pixbuf);

	return GTK_WIDGET(image);
}

/* Whether a line l1 long starting from n1 overlaps a line l2 from n2 */
inline static gboolean gui_ranges_overlap(int n1, int l1, int n2, int l2)
{
	return (n1 > n2 && n1 < n2 + l2) ||
		(n1 + l1 > n2 && n1 + l1 < n2 + l2) ||
		(n1 <= n2 && n1 + l1 >= n2 + l2);
}

static void gui_get_monitor_adjacent(int monitor, MonitorAdjacent *adj)
{
	int m;

	adj->left = adj->right = adj->top = adj->bottom = FALSE;

	for (m = 0; m < n_monitors; ++m)
	{
		if (m == monitor)
			continue;
		if (gui_ranges_overlap(monitor_geom[m].y,
				monitor_geom[m].height,
				monitor_geom[monitor].y,
				monitor_geom[monitor].height))
		{
			if (monitor_geom[m].x < monitor_geom[monitor].x)
			{
				adj->left = TRUE;
			}
			else if (monitor_geom[m].x > monitor_geom[monitor].x)
			{
				adj->right = TRUE;
			}
		}
		if (gui_ranges_overlap(monitor_geom[m].x,
				monitor_geom[m].width,
				monitor_geom[monitor].x,
				monitor_geom[monitor].width))
		{
			if (monitor_geom[m].y < monitor_geom[monitor].y)
			{
				adj->top = TRUE;
			}
			else if (monitor_geom[m].y > monitor_geom[monitor].y)
			{
				adj->bottom = TRUE;
			}
		}
	}
}

static void rox_wmspec_change_state(gboolean add, GdkWindow *window,
				    GdkAtom state1, GdkAtom state2)
{
	GdkDisplay *display = gdk_window_get_display(window);
	XEvent xev;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.send_event = True;
	xev.xclient.window = GDK_WINDOW_XID(window);
	xev.xclient.message_type = gdk_x11_get_xatom_by_name_for_display(
			display, "_NET_WM_STATE");
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.xclient.data.l[1] = gdk_x11_atom_to_xatom_for_display(display,
			state1);
	xev.xclient.data.l[2] = gdk_x11_atom_to_xatom_for_display(display,
			state2);
	xev.xclient.data.l[3] = 0;
	xev.xclient.data.l[4] = 0;

	XSendEvent(GDK_DISPLAY_XDISPLAY(display),
		   gdk_x11_get_default_root_xwindow(),
		   False,
		   SubstructureRedirectMask | SubstructureNotifyMask,
		   &xev);
}

/* Tell the window manager whether to keep this window below others. */
void keep_below(GdkWindow *window, gboolean setting)
{
	g_return_if_fail(GDK_IS_WINDOW(window));

	if (gdk_window_is_destroyed(window))
		return;

	if (gdk_window_is_visible(window))
	{
		if (setting)
		{
			rox_wmspec_change_state(FALSE, window,
				gdk_atom_intern("_NET_WM_STATE_ABOVE", FALSE),
				GDK_NONE);
		}
		rox_wmspec_change_state(setting, window,
				gdk_atom_intern("_NET_WM_STATE_BELOW", FALSE),
				GDK_NONE);
	}
#if 0
	else
	{
#if GTK_CHECK_VERSION(2,4,0)
	  gdk_synthesize_window_state(window,
				setting ? GDK_WINDOW_STATE_ABOVE :
					GDK_WINDOW_STATE_BELOW,
				setting ? GDK_WINDOW_STATE_BELOW : 0);
#endif
	}
#endif
}

static void
size_prepared_cb (GdkPixbufLoader *loader,
		  int              width,
		  int              height,
		  gpointer         data)
{
	struct {
		gint width;
		gint height;
		gboolean preserve_aspect_ratio;
	} *info = data;

	g_return_if_fail (width > 0 && height > 0);

	if(info->preserve_aspect_ratio) {
		if ((double)height * (double)info->width >
		    (double)width * (double)info->height) {
			width = 0.5 + (double)width * (double)info->height / (double)height;
			height = info->height;
		} else {
			height = 0.5 + (double)height * (double)info->width / (double)width;
			width = info->width;
		}
	} else {
		width = info->width;
		height = info->height;
	}

	gdk_pixbuf_loader_set_size (loader, width, height);
}

/**
 * rox_pixbuf_new_from_file_at_scale:
 * @filename: Name of file to load.
 * @width: The width the image should have
 * @height: The height the image should have
 * @preserve_aspect_ratio: %TRUE to preserve the image's aspect ratio
 * @error: Return location for an error
 *
 * Creates a new pixbuf by loading an image from a file.  The file format is
 * detected automatically. If %NULL is returned, then @error will be set.
 * Possible errors are in the #GDK_PIXBUF_ERROR and #G_FILE_ERROR domains.
 * The image will be scaled to fit in the requested size, optionally preserving
 * the image's aspect ratio.
 *
 * Return value: A newly-created pixbuf with a reference count of 1, or %NULL
 * if any of several error conditions occurred:  the file could not be opened,
 * there was no loader for the file's format, there was not enough memory to
 * allocate the image buffer, or the image file contained invalid data.
 *
 * Taken from GTK 2.6.
 **/
GdkPixbuf *
rox_pixbuf_new_from_file_at_scale (const char *filename,
				   int         width,
				   int         height,
				   gboolean    preserve_aspect_ratio,
				   GError    **error)
{

	GdkPixbufLoader *loader;
	GdkPixbuf       *pixbuf;

	gchar buffer [4096];
	int length;
	FILE *f;
	struct {
		gint width;
		gint height;
		gboolean preserve_aspect_ratio;
	} info;

	g_return_val_if_fail (filename != NULL, NULL);
        g_return_val_if_fail (width > 0 && height > 0, NULL);

	f = fopen (filename, "rb");
	if (!f) {
                gchar *utf8_filename = g_filename_to_utf8 (filename, -1,
                                                           NULL, NULL, NULL);
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             _("Failed to open file '%s': %s"),
                             utf8_filename ? utf8_filename : "???",
                             g_strerror (errno));
                g_free (utf8_filename);
		return NULL;
        }

	loader = gdk_pixbuf_loader_new ();

	info.width = width;
	info.height = height;
        info.preserve_aspect_ratio = preserve_aspect_ratio;

	g_signal_connect (loader, "size-prepared", G_CALLBACK (size_prepared_cb), &info);

	while (!feof (f) && !ferror (f)) {
		length = fread (buffer, 1, sizeof (buffer), f);
		if (length > 0)
			if (!gdk_pixbuf_loader_write (loader, (const guchar *) buffer, length, error)) {
				gdk_pixbuf_loader_close (loader, NULL);
				fclose (f);
				g_object_unref (loader);
				return NULL;
			}
	}

	fclose (f);

	if (!gdk_pixbuf_loader_close (loader, error)) {
		g_object_unref (loader);
		return NULL;
	}

	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

	if (!pixbuf) {
                gchar *utf8_filename = g_filename_to_utf8 (filename, -1,
                                                           NULL, NULL, NULL);

		g_object_unref (loader);

                g_set_error (error,
                             GDK_PIXBUF_ERROR,
                             GDK_PIXBUF_ERROR_FAILED,
                             _("Failed to load image '%s': reason not known, probably a corrupt image file"),
                             utf8_filename ? utf8_filename : "???");
                g_free (utf8_filename);
		return NULL;
	}

	g_object_ref (pixbuf);

	g_object_unref (loader);

	return pixbuf;
}

/* Make the name bolder and larger.
 * scale_factor can be PANGO_SCALE_X_LARGE, etc.
 */
void make_heading(GtkWidget *label, double scale_factor)
{
	PangoAttribute *attr;
	PangoAttrList *list;

	list = pango_attr_list_new();

	attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	attr = pango_attr_scale_new(scale_factor);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	gtk_label_set_attributes(GTK_LABEL(label), list);
	pango_attr_list_unref(list);
}

/* Launch a program using 0launch.
 * If button-3 is used, open the GUI with -g.
 */
void launch_uri(GObject *button, const char *uri)
{
	const char *argv[] = {"0launch", NULL, NULL, NULL};
	const char *uri_0launch = "/uri/0install/zero-install.sourceforge.net"
				  "/bin/0launch";

	if (!available_in_path(argv[0]))
	{
		if (access(uri_0launch, X_OK) == 0)
			argv[0] = uri_0launch;
		else
		{
			const char *appname=g_object_get_data(button,
							      "appname");

			if (appname)
			{
				gchar *path=find_app(appname);
				if(path)
				{
					run_by_path(path);
					g_free(path);
					return;
				}
			}

			delayed_error(_("This program (%s) cannot be run, "
				"as the 0launch command is not available. "
				"It can be downloaded from here:\n\n"
				"http://0install.net/injector.html"),
				uri);
			return;
		}
	}

	if (current_event_button() == 3)
	{
		argv[1] = "-g";
		argv[2] = uri;
	}
	else
		argv[1] = uri;

	rox_spawn(NULL, argv);
}

static gint button3_button_pressed(GtkButton *button,
				GdkEventButton *event,
				gpointer date)
{
	if (event->button == 3)
	{
		gtk_device_grab_add(GTK_WIDGET(button), NULL, FALSE);

		return TRUE;
	}

	return FALSE;
}

static gint button3_button_released(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == 3)
	{
		gtk_device_grab_remove(GTK_WIDGET(button), NULL);

		return TRUE;
	}

	return FALSE;
}

void allow_right_click(GtkWidget *button)
{
	g_signal_connect(button, "button_press_event",
		G_CALLBACK(button3_button_pressed), NULL);
	g_signal_connect(button, "button_release_event",
		G_CALLBACK(button3_button_released), NULL);
}

/* Return mouse button used in the current event, or -1 if none (no event,
 * or not a click).
 */
gint current_event_button(void)
{
	GdkEventButton *bev;
	gint button = -1;

	bev = (GdkEventButton *) gtk_get_current_event();

	if (bev &&
	    (bev->type == GDK_BUTTON_PRESS || bev->type == GDK_BUTTON_RELEASE))
		button = bev->button;

	gdk_event_free((GdkEvent *) bev);

	return button;
}

/* Create a new pixbuf by colourizing 'src' to 'color'. If the function fails,
 * 'src' will be returned (with an increased reference count, so it is safe to
 * g_object_unref() the return value whether the function fails or not).
 */
GdkPixbuf *create_spotlight_pixbuf(GdkPixbuf *src, GdkRGBA *color)
{
	guchar opacity = 88;//127;//192;
	guchar alpha = 255 - opacity;
	GdkPixbuf *dst;
	GdkColorspace colorspace;
	int width, height, src_rowstride, dst_rowstride, x, y;
	int n_channels, bps;
	int r, g, b;
	guchar *spixels, *dpixels, *src_pixels, *dst_pixels;
	gboolean has_alpha;

	has_alpha = gdk_pixbuf_get_has_alpha(src);
	colorspace = gdk_pixbuf_get_colorspace(src);
	n_channels = gdk_pixbuf_get_n_channels(src);
	bps = gdk_pixbuf_get_bits_per_sample(src);

	if ((colorspace != GDK_COLORSPACE_RGB) ||
	    (!has_alpha && n_channels != 3) ||
	    (has_alpha && n_channels != 4) ||
	    (bps != 8))
		goto error;

	width = gdk_pixbuf_get_width(src);
	height = gdk_pixbuf_get_height(src);

	dst = gdk_pixbuf_new(colorspace, has_alpha, bps, width, height);
	if (dst == NULL)
		goto error;

	src_pixels = gdk_pixbuf_get_pixels(src);
	dst_pixels = gdk_pixbuf_get_pixels(dst);
	src_rowstride = gdk_pixbuf_get_rowstride(src);
	dst_rowstride = gdk_pixbuf_get_rowstride(dst);

	if (color == NULL)
		goto error;
	r = opacity * (color->red * 255);
	g = opacity * (color->green * 255);
	b = opacity * (color->blue * 255);

	for (y = 0; y < height; y++)
	{
		spixels = src_pixels + y * src_rowstride;
		dpixels = dst_pixels + y * dst_rowstride;
		for (x = 0; x < width; x++)
		{
			*dpixels++ = (*spixels++ * alpha + r) >> 8;
			*dpixels++ = (*spixels++ * alpha + g) >> 8;
			*dpixels++ = (*spixels++ * alpha + b) >> 8;
			if (has_alpha)
				*dpixels++ = *spixels++;
		}

	}
	return dst;

error:
	g_object_ref(src);
	return src;
}

/* Load the Templates.ui file and build a component. */
GtkBuilder *get_gtk_builder(gchar **ids)
{
	GError	*error = NULL;
	char *path;
	GtkBuilder *builder = NULL;

	builder = gtk_builder_new();
	gtk_builder_set_translation_domain(builder, "ROX-Filer");

	path = g_build_filename(app_dir, "Templates.ui", NULL);
	if (!gtk_builder_add_objects_from_file(builder, path, ids, &error))
	{
		g_warning("Failed to load builder file %s: %s",
				path, error->message);
		g_error_free(error);
	}

	g_free(path);

	return builder;
}

/*
 * A GtkMenuItem that keeps its label as the bin child and draws an icon in
 * the toggle gutter reserved by the parent menu, mirroring the layout of the
 * deprecated GtkImageMenuItem so decorated and undecorated entries share one
 * hanging-indent text column.
 */

#define ROX_MENU_ITEM_SPACING 6

typedef struct _RoxMenuItem RoxMenuItem;
typedef struct _RoxMenuItemClass RoxMenuItemClass;

struct _RoxMenuItem
{
	GtkMenuItem	menu_item;

	GtkWidget	*image;
	gint		toggle_size;	/* gutter width assigned by the menu */
};

struct _RoxMenuItemClass
{
	GtkMenuItemClass parent_class;
};

static void rox_menu_item_destroy(GtkWidget *widget);
static void rox_menu_item_remove(GtkContainer *container,
			GtkWidget *child);
static void rox_menu_item_size_allocate(GtkWidget *widget,
			GtkAllocation *allocation);static void rox_menu_item_get_preferred_height(GtkWidget *widget,
			gint *minimum, gint *natural);
static void rox_menu_item_get_preferred_height_for_width(GtkWidget *widget,
			gint width, gint *minimum, gint *natural);
static void rox_menu_item_toggle_size_request(GtkMenuItem *menu_item,
			gint *requisition);
static void rox_menu_item_toggle_size_allocate(GtkMenuItem *menu_item,
			gint allocation);
static void rox_menu_item_forall(GtkContainer *container,
			gboolean include_internals, GtkCallback callback,
			gpointer callback_data);

GType rox_menu_item_get_type(void);

G_DEFINE_TYPE(RoxMenuItem, rox_menu_item, GTK_TYPE_MENU_ITEM)

static void rox_menu_item_class_init(RoxMenuItemClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	GtkMenuItemClass *menu_item_class = GTK_MENU_ITEM_CLASS(klass);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);

	widget_class->destroy = rox_menu_item_destroy;
	widget_class->size_allocate = rox_menu_item_size_allocate;
	widget_class->get_preferred_height =
			rox_menu_item_get_preferred_height;
	widget_class->get_preferred_height_for_width =
			rox_menu_item_get_preferred_height_for_width;

	menu_item_class->toggle_size_request =
			rox_menu_item_toggle_size_request;
	menu_item_class->toggle_size_allocate =
			rox_menu_item_toggle_size_allocate;

	container_class->forall = rox_menu_item_forall;
	container_class->remove = rox_menu_item_remove;
}

static void rox_menu_item_init(RoxMenuItem *item)
{
	item->image = NULL;
	item->toggle_size = 0;
}

static void rox_menu_item_destroy(GtkWidget *widget)
{
	RoxMenuItem *item = (RoxMenuItem *) widget;

	if (item->image)
	{
		gtk_container_remove(GTK_CONTAINER(item), item->image);
	}

	if (GTK_WIDGET_CLASS(rox_menu_item_parent_class)->destroy)
		GTK_WIDGET_CLASS(rox_menu_item_parent_class)->
				destroy(widget);
}

/*
 * The image is an internal child, not the bin child, so GtkBin's remove
 * would reject it. Unparent it directly; anything else goes to GtkBin.
 */
static void rox_menu_item_remove(GtkContainer *container,
			GtkWidget *child)
{
	RoxMenuItem *item = (RoxMenuItem *) container;

	if (child == item->image)
	{
		item->image = NULL;
		gtk_widget_unparent(child);
		return;
	}

	GTK_CONTAINER_CLASS(rox_menu_item_parent_class)->remove(container,
			child);
}

/* Width of the icon plus the gap between icon and label */
static gint rox_menu_item_icon_extent(RoxMenuItem *item, gint *width,
			gint *height)
{
	GtkRequisition requisition;

	*width = 0;
	*height = 0;

	if (!(item->image && gtk_widget_get_visible(item->image)))
		return 0;

	gtk_widget_get_preferred_size(item->image, &requisition, NULL);
	*width = requisition.width;
	*height = requisition.height;

	return *width > 0 ? *width + ROX_MENU_ITEM_SPACING : 0;
}

static void rox_menu_item_get_image_position(RoxMenuItem *item,
			GtkAllocation *allocation, gint image_width,
			gint image_height, GtkAllocation *child_allocation)
{
	GtkStyleContext *context;
	GtkStateFlags state;
	GtkBorder padding;
	gint border_width;
	gint x, y;

	context = gtk_widget_get_style_context(GTK_WIDGET(item));
	state = gtk_widget_get_state_flags(GTK_WIDGET(item));
	gtk_style_context_get_padding(context, state, &padding);
	border_width = gtk_container_get_border_width(GTK_CONTAINER(item));

	if (gtk_widget_get_direction(GTK_WIDGET(item)) ==
			GTK_TEXT_DIR_LTR)
	{
		x = border_width + padding.left +
			(item->toggle_size - ROX_MENU_ITEM_SPACING -
					image_width) / 2;
	}
	else
	{
		x = allocation->width - border_width - padding.right -
			item->toggle_size + ROX_MENU_ITEM_SPACING +
			(item->toggle_size - ROX_MENU_ITEM_SPACING -
					image_width) / 2;
	}

	y = (allocation->height - image_height) / 2;

	child_allocation->x = allocation->x + MAX(x, 0);
	child_allocation->y = allocation->y + MAX(y, 0);
	child_allocation->width = image_width;
	child_allocation->height = image_height;
}

static void rox_menu_item_size_allocate(GtkWidget *widget,
			GtkAllocation *allocation)
{
	RoxMenuItem *item = (RoxMenuItem *) widget;
	gint width, height;

	GTK_WIDGET_CLASS(rox_menu_item_parent_class)->size_allocate(
			widget, allocation);

	if (!rox_menu_item_icon_extent(item, &width, &height))
		return;

	if (width > 0 && item->toggle_size > 0)
	{
		GtkAllocation child_allocation;

		rox_menu_item_get_image_position(item, allocation,
				width, height, &child_allocation);
		gtk_widget_size_allocate(item->image, &child_allocation);
	}
}

static void rox_menu_item_get_preferred_height(GtkWidget *widget,
			gint *minimum, gint *natural)
{
	RoxMenuItem *item = (RoxMenuItem *) widget;
	gint width, height;

	GTK_WIDGET_CLASS(rox_menu_item_parent_class)->get_preferred_height(
			widget, minimum, natural);

	rox_menu_item_icon_extent(item, &width, &height);

	*minimum = MAX(*minimum, height);
	*natural = MAX(*natural, height);
}

static void rox_menu_item_get_preferred_height_for_width(GtkWidget *widget,
			gint width, gint *minimum, gint *natural)
{
	RoxMenuItem *item = (RoxMenuItem *) widget;
	gint image_width, image_height;

	GTK_WIDGET_CLASS(rox_menu_item_parent_class)->
			get_preferred_height_for_width(widget, width,
					minimum, natural);

	rox_menu_item_icon_extent(item, &image_width, &image_height);

	*minimum = MAX(*minimum, image_height);
	*natural = MAX(*natural, image_height);
}

static void rox_menu_item_toggle_size_request(GtkMenuItem *menu_item,
			gint *requisition)
{
	RoxMenuItem *item = (RoxMenuItem *) menu_item;
	gint width, height;

	*requisition = rox_menu_item_icon_extent(item, &width, &height);
}

static void rox_menu_item_toggle_size_allocate(GtkMenuItem *menu_item,
			gint allocation)
{
	RoxMenuItem *item = (RoxMenuItem *) menu_item;

	item->toggle_size = allocation;

	GTK_MENU_ITEM_CLASS(rox_menu_item_parent_class)->
			toggle_size_allocate(menu_item, allocation);
}

static void rox_menu_item_forall(GtkContainer *container,
			gboolean include_internals, GtkCallback callback,
			gpointer callback_data)
{
	RoxMenuItem *item = (RoxMenuItem *) container;

	GTK_CONTAINER_CLASS(rox_menu_item_parent_class)->forall(container,
			include_internals, callback, callback_data);

	if (include_internals && item->image)
		(*callback)(item->image, callback_data);
}

/*
 * Create a menu item showing 'label' with 'image' in the toggle gutter.
 * The label keeps mnemonic semantics.
 */
static GtkWidget *menu_item_new_with_image_full(const gchar *label,
		GtkWidget *image, gboolean mnemonic)
{
	GtkWidget *item;
	GtkWidget *label_widget;

	g_return_val_if_fail(GTK_IS_WIDGET(image), NULL);

	item = g_object_new(rox_menu_item_get_type(), NULL);

	((RoxMenuItem *) item)->image = image;
	gtk_widget_set_parent(image, item);
	gtk_widget_show(image);

	label_widget = gtk_accel_label_new(label ?
			label : "");
	gtk_accel_label_set_accel_widget(GTK_ACCEL_LABEL(label_widget),
			GTK_WIDGET(item));
	gtk_label_set_mnemonic_widget(GTK_LABEL(label_widget), item);
	if (mnemonic)
	{
		gtk_label_set_use_underline(GTK_LABEL(label_widget), TRUE);
	}
	gtk_label_set_xalign(GTK_LABEL(label_widget), 0.0);
	gtk_container_add(GTK_CONTAINER(item), label_widget);
	gtk_widget_show(label_widget);

	return item;
}

GtkWidget *menu_item_new_with_image(const gchar *label, GtkWidget *image)
{
	return menu_item_new_with_image_full(label, image, FALSE);
}

/* As above, but '_' in the label marks a mnemonic. [20260921084135]
 * Only for labels that carried mnemonics in GTK2 (SendTo rows);
 * dynamic plain-text labels (AppMenu XML, panel applets) keep
 * the non-mnemonic form.
 */
GtkWidget *menu_item_new_with_image_mnemonic(const gchar *label,
		GtkWidget *image)
{
	return menu_item_new_with_image_full(label, image, TRUE);
}

/*
 * Replace a menu item's icon, keeping the internal-image bookkeeping the
 * custom gutter layout (rox_menu_item_icon_extent) relies on. The old
 * image is released through the same remove path used at destroy, and the
 * new image's floating reference is sunk by parenting it. Passing a NULL
 * image clears the icon (leaving no gutter), which is how a menu row
 * represents "no action". [20260923070143]
 */
void menu_item_set_image(GtkWidget *item, GtkWidget *image)
{
	g_return_if_fail(GTK_IS_WIDGET(item));

	if (!G_TYPE_CHECK_INSTANCE_TYPE(item, rox_menu_item_get_type()))
	{
		if (image)
			gtk_widget_destroy(image);
		return;
	}

	if (((RoxMenuItem *) item)->image)
		gtk_container_remove(GTK_CONTAINER(item),
				((RoxMenuItem *) item)->image);

	if (image)
	{
		((RoxMenuItem *) item)->image = image;
		gtk_widget_set_parent(image, item);
		gtk_widget_show(image);
	}

	gtk_widget_queue_resize(item);
}

/* assigned needs be filled NULL and length is 27 */
gchar get_mnemonic(gchar *text, gchar *assigned)
{
	gchar * tmp, *t;
	gchar ret = '\0';

	t = strdup(text);
	tmp = t;
	while ((tmp = g_strstr_len(tmp, -1, "__")) != NULL)
		tmp[0] = tmp[1] = 'a';

	tmp = g_strstr_len(t, -1, "_");
	if (tmp && strlen(tmp) >= 2)
		ret = g_ascii_tolower(tmp[1]);

	g_free(t);

	if (assigned &&
		ret != '\0' &&
		ret >= GDK_KEY_a &&
		ret <= GDK_KEY_z)
	{
		for (; ; assigned+=1)
		{
			if (assigned[0] == '\0')
				assigned[0] = ret;
			if (assigned[0] == ret)
				break;
		}
	}

	return ret;
}
/* return val is static.
 * assigned needs be filled NULL and length is 27 */
gchar *add_mnemonic(gchar *text, gchar *assigned)
{
	static gchar *ret;
	const gchar *as = assigned;
	gchar *tmp;
	gchar lc;
	int rlen, alen, i, j, k = 0;
	gboolean found;

	g_free(ret);
	ret = strdup(text);

	rlen = strlen(ret);
	alen = strlen(as);
	for (i = 0; i < rlen; i++)
		if (ret[i] == '_')
		{
			ret[i] = '\0';
			tmp = ret;
			i ++;
			ret = g_strconcat(ret, "__", ret + i, NULL);
			rlen ++;
			g_free(tmp);
		}

	/* If there is no unique then first [a-z] */
	while (k < 2)
	{
		for (i = 0; i < rlen; i++)
		{
			lc = g_ascii_tolower(ret[i]);
			found = FALSE;
			if (k == 0)
				for (j = 0; j < alen; j++)
					if (as[j] == lc)
					{
						found = TRUE;
						break;
					}

			if (!found &&
			lc >= GDK_KEY_a &&
			lc <= GDK_KEY_z)
			{
				if (k ==0)
					assigned[alen] = lc;

				tmp = g_strndup(ret, i);
				ret = g_strconcat(tmp, "_", ret + i, NULL);
				g_free(tmp);

				k++;
				break;
			}
		}

		k++;
	}

	return ret;
}
