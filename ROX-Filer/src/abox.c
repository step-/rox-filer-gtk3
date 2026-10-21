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

/* abox.c - the dialog box widget used for filer operations.
 *
 * The actual code for specific operations is in action.c.
 */

#include "config.h"

#include <string.h>

#include "global.h"

#include "main.h"
#include "abox.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "support.h"
#include "dir.h"
#include "diritem.h"
#include "gtksavebox.h"
#include "menu.h"
#include "pixmaps.h"
#include "type.h"

#define RESPONSE_QUIET 1
// RESPONSE_SEQNO 2
// RESPONSE_SEQNO_ALL 3

/* Smallest height of the log window, and cap for window growth. */
#define LOG_HEIGHT_MIN 70
#define LOG_SCREEN_SHARE (1.0 / 3) /* NB: floating point division */

/* Smallest height of the Find results pane [20260928074452]. */
#define RESULTS_HEIGHT_MIN 100

/* Static prototypes */
static void abox_class_init(GObjectClass *gclass, gpointer data);
static void abox_init(GTypeInstance *object, gpointer gclass);
static gboolean abox_delete(GtkWidget *dialog, GdkEventAny *event);
static void response(GtkDialog *dialog, gint response_id);
static void abox_finalise(GObject *object);
static void shade(ABox *abox);
static gboolean results_button_press(GtkWidget *widget,
				GdkEventButton *event, ABox *abox);
static void results_copy_paths(GtkWidget *widget, ABox *abox);
static void results_export_table(GtkWidget *widget, ABox *abox);
static void results_select_all(GtkWidget *widget, ABox *abox);

/* A selected row of the Find results grid. */
typedef struct
{
	gchar *leaf;
	gchar *dir;
} ResultRow;

GType abox_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ABoxClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			(GClassInitFunc) abox_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ABox),
			0,			/* n_preallocs */
			(GInstanceInitFunc) abox_init
		};

		type = g_type_register_static(GTK_TYPE_DIALOG,
						"ABox", &info, 0);
	}

	return type;
}

GtkWidget* abox_new(const gchar *title, gboolean quiet)
{
	GtkWidget *widget;
	ABox	  *abox;

	widget = GTK_WIDGET(gtk_widget_new(abox_get_type(), NULL));
	abox = ABOX(widget);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->quiet), quiet);

	gtk_window_set_title(GTK_WINDOW(widget), title);
//	gtk_dialog_set_has_separator(GTK_DIALOG(widget), FALSE);

	return widget;
}

static void abox_class_init(GObjectClass *gclass, gpointer data)
{
	GtkWidgetClass *widget = (GtkWidgetClass *) gclass;
	GtkDialogClass *dialog = (GtkDialogClass *) gclass;
	ABoxClass *abox = (ABoxClass *) gclass;

	widget->delete_event = abox_delete;
	dialog->response = response;
	abox->flag_toggled = NULL;
	abox->abort_operation = NULL;

	g_signal_new("flag_toggled", G_TYPE_FROM_CLASS(gclass),
		G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(ABoxClass, flag_toggled),
		NULL, NULL, g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	g_signal_new("abort_operation", G_TYPE_FROM_CLASS(gclass),
		G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(ABoxClass, abort_operation),
		NULL, NULL, g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	gclass->finalize = abox_finalise;
}

static void abox_init(GTypeInstance *object, gpointer gclass)
{
	GtkWidget *frame, *scrolledwin, *button;
	ABox *abox = ABOX(object);
	GtkDialog *dialog = GTK_DIALOG(object);
	GdkMonitor *monitor;
	GdkRectangle monitor_geom;
	int i;

	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	abox->src_label = gtk_label_new(_("<src>"));
	gtk_widget_set_size_request(abox->src_label, 8, -1);
	gtk_label_set_ellipsize(GTK_LABEL(abox->src_label), PANGO_ELLIPSIZE_MIDDLE);
	make_heading(abox->src_label, 1.0);
	gtk_widget_set_halign(abox->src_label, GTK_ALIGN_START);
	gtk_widget_set_valign(abox->src_label, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)),
				abox->src_label, FALSE, TRUE, 0);

	abox->results = NULL;
	abox->results_scroll = NULL;
	abox->results_pane = NULL;
	abox->results_menu = NULL;
	abox->entry = NULL;
	abox->question = FALSE;

	abox->log_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)),
				abox->log_hbox, TRUE, TRUE, 4);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(abox->log_hbox), frame, TRUE, TRUE, 0);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	abox->log = gtk_text_view_new();
	scrolledwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(scrolledwin), abox->log);
	gtk_container_add(GTK_CONTAINER(frame), scrolledwin);
	abox->log_scroll = scrolledwin;
	abox->log_scroll_id = 0;

	gtk_text_buffer_create_tag(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log)),
			"error", "foreground", "red",
			NULL);
	gtk_text_buffer_create_tag(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log)),
			"question", "weight", PANGO_WEIGHT_BOLD,
			NULL);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(abox->log), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(abox->log), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(abox->log), GTK_WRAP_WORD);
	gtk_widget_set_size_request(abox->log, 400, -1);
	gtk_widget_set_size_request(abox->log_scroll, -1, LOG_HEIGHT_MIN);

	/* Let the log grow with its output, up to 1/3 of the screen height. */
	abox->log_height = LOG_HEIGHT_MIN;
	abox->log_max_height = LOG_HEIGHT_MIN;
	monitor = gdk_display_get_primary_monitor(
			gtk_widget_get_display(GTK_WIDGET(dialog)));
	if (monitor)
	{
		gdk_monitor_get_geometry(monitor, &monitor_geom);
		abox->log_max_height = MAX(LOG_HEIGHT_MIN,
				monitor_geom.height * LOG_SCREEN_SHARE);
	}

	abox->btn_cancel = button_new_mixed("gtk-cancel", _("_Cancel"));
	gtk_widget_set_tooltip_text(abox->btn_cancel,
			_("Cancel remaining operations"));
	gtk_dialog_add_action_widget(dialog, abox->btn_cancel, GTK_RESPONSE_CANCEL);
	abox->btn_close = button_new_mixed("window-close", _("_Close"));
	gtk_widget_set_tooltip_text(abox->btn_close, _("Close log"));
	gtk_dialog_add_action_widget(dialog, abox->btn_close, GTK_RESPONSE_CANCEL);

	abox->btn_seqno = button_new_mixed("edit-copy", _("+.Num")); /* [20260803140905] ad interim */
	gtk_widget_set_tooltip_text(abox->btn_seqno,
			_("Keep both - auto-number incoming file (this conflict only),"
			  " e.g. photo.jpg → photo.jpg.1, photo.jpg.1 → photo.jpg.2..."));
	gtk_dialog_add_action_widget(dialog, abox->btn_seqno, 2);
	abox->btn_seqno_all = button_new_mixed("go-last", _("+.Num"));
	gtk_widget_set_tooltip_text(abox->btn_seqno_all,
			_("Keep both - auto-number incoming file"
			  " (this and all remaining conflicts, quietly)"));
	gtk_dialog_add_action_widget(dialog, abox->btn_seqno_all, 3);

	button = button_new_mixed("gtk-no", _("_No"));
	gtk_widget_set_tooltip_text(button, _("Skip this file"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(dialog, button, GTK_RESPONSE_NO);
	button = button_new_mixed("gtk-yes", _("_Yes"));
	gtk_widget_set_tooltip_text(button, _("Overwrite destination"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(dialog, button, GTK_RESPONSE_YES);

	abox->cmp_area = gtk_grid_new();
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)),
				abox->cmp_area, FALSE, FALSE, 2);
	gtk_grid_set_row_spacing(GTK_GRID(abox->cmp_area), 2);
	gtk_grid_set_column_spacing(GTK_GRID(abox->cmp_area), 2);

	for (i = 0; i < 2; i++)
	{

		abox->cmp_icon[i] = gtk_image_new();
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_icon[i],
				1, i, 1, 1);
		abox->cmp_name[i] = gtk_label_new("");
		gtk_label_set_line_wrap(GTK_LABEL(abox->cmp_name[i]), TRUE);
		gtk_label_set_line_wrap_mode(GTK_LABEL(abox->cmp_name[i]),
				PANGO_WRAP_WORD_CHAR);
		gtk_widget_set_halign(abox->cmp_name[i], GTK_ALIGN_START);
		gtk_widget_set_valign(abox->cmp_name[i], GTK_ALIGN_CENTER);
		gtk_widget_set_hexpand(abox->cmp_name[i], TRUE);
		gtk_widget_set_vexpand(abox->cmp_name[i], TRUE);
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_name[i],
				2, i, 1, 1);
		abox->cmp_size[i] = gtk_label_new("");
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_size[i],
				3, i, 1, 1);
		abox->cmp_date[i] = gtk_label_new("");
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_date[i],
				4, i, 1, 1);
	}
	abox->cmp_arrow=gtk_image_new_from_icon_name("go-down", GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_size_request(abox->cmp_arrow, 32, 64);
	gtk_widget_set_vexpand(abox->cmp_arrow, TRUE);
	gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_arrow,
				0, 0, 1, 2);

	abox->progress=NULL;

	abox->flag_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(dialog)),
				abox->flag_box, FALSE, TRUE, 2);

	abox->fileprog = gtk_progress_bar_new();
	gtk_widget_set_size_request(abox->fileprog, -1, small_height * (1./3.));
	gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(dialog)),
				abox->fileprog, FALSE, TRUE, 2);

	button = button_new_mixed("go-last", _("_Quiet"));
	gtk_widget_set_tooltip_text(button,
			_("Answer Yes without asking (still respects overwrite flags)"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(dialog, button, RESPONSE_QUIET);
	gtk_dialog_set_default_response(dialog, RESPONSE_QUIET);

	gtk_widget_show_all(gtk_dialog_get_content_area(dialog));
	gtk_widget_hide(abox->cmp_area);
	gtk_widget_hide(abox->fileprog);
	gtk_widget_hide(abox->btn_close);
	gtk_widget_hide(abox->btn_seqno);
	gtk_widget_hide(abox->btn_seqno_all);

	abox->quiet = abox_add_flag(abox,
			_("Quiet"), _("Don't confirm every operation"),
			'Q', FALSE);

	shade(abox);
}

static void flag_toggled(GtkToggleButton *toggle, ABox *abox)
{
	gint	code;

	code = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(toggle),
						 "abox-response"));

	if (code == 'Q')
		shade(abox);

	g_signal_emit_by_name(abox, "flag_toggled", code);

}

GtkWidget *abox_add_flag(ABox *abox, const gchar *label, const gchar *tip,
		   	 gint response, gboolean default_value)
{
	GtkWidget	*check;

	check = gtk_check_button_new_with_label(label);
	gtk_widget_set_tooltip_text(check, tip);
	g_object_set_data(G_OBJECT(check), "abox-response",
			  GINT_TO_POINTER(response));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), default_value);
	g_signal_connect(check, "toggled", G_CALLBACK(flag_toggled), abox);
	gtk_box_pack_end(GTK_BOX(abox->flag_box), check, FALSE, TRUE, 0);
	gtk_widget_show(check);

	return check;
}

static void response(GtkDialog *dialog, gint response_id)
{
	ABox *abox = ABOX(dialog);

	if (response_id == GTK_RESPONSE_CANCEL)
		g_signal_emit_by_name(abox, "abort_operation");
	else if (response_id == RESPONSE_QUIET)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->quiet), TRUE);
		gtk_dialog_response(dialog, GTK_RESPONSE_YES);
	}
	else if (response_id == GTK_RESPONSE_YES ||
					response_id == GTK_RESPONSE_NO ||
					response_id == 2 || response_id == 3) //seqno
	{
		abox->question = FALSE;
		shade(abox);
	}
}

/* Display the question. Unshade the Yes, No and entry box (if any).
 * Will send a response signal when the user makes a choice.
 */
void abox_ask(ABox *abox, const gchar *question)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(question != NULL);
	g_return_if_fail(IS_ABOX(abox));

	abox_log(abox, question, "question", TRUE);

	abox->question = TRUE;
	shade(abox);
}

void abox_cancel_ask(ABox *abox)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	abox->question = FALSE;
	shade(abox);
}

/* Raise the scrolled window's height request so more of the
 * output is readable without scrolling, up to the cap set
 * when the dialog was built. No-op once the cap is reached.
 *
 * Line height is measured from the log itself so it follows font
 * and screen scale (that needs the view realized)
 *
 * Line count is the buffer's, not the layout's, so
 * the cost does not grow with the size of the log.
 */
static void abox_grow_log(ABox *abox)
{
	GtkTextBuffer *text_buffer;
	GtkTextIter line;
	GdkRectangle where;
	gint line_height, lines, want;

	/* Once the cap is reached the request cannot change, so stop measuring.
	 * The iterator lookup below validates the whole log layout, and the log
	 * grows with the operation, so measuring on every line would cost more
	 * the longer a run went on.
	 */
	if (abox->log_height >= abox->log_max_height)
		return;

	if (!gtk_widget_get_realized(GTK_WIDGET(abox->log)))
		return;

	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log));
	gtk_text_buffer_get_iter_at_line(text_buffer, &line, 0);
	gtk_text_iter_forward_to_line_end(&line);
	gtk_text_view_get_iter_location(GTK_TEXT_VIEW(abox->log), &line,
					&where);
	line_height = where.height;
	if (line_height < 1)
		return;

	lines = gtk_text_buffer_get_line_count(text_buffer);

	/* One line of slack, so the last line is not flush against the frame.
	 * small_height is a fixed 18 and is not a line height, so it is not
	 * used here; at a scale of 2 it would be four lines of padding.
	 */
	want = CLAMP((lines + 1) * line_height,
			LOG_HEIGHT_MIN, abox->log_max_height);

	if (want <= abox->log_height)
		return;

	abox->log_height = want;
	gtk_widget_set_size_request(abox->log_scroll, -1, want);
}

/* Bring the end of the output into view, but only
 * if the output no longer fits. Deferred to an idle.
 */
static gboolean abox_scroll_to_end(gpointer data)
{
	ABox *abox = (ABox *) data;
	GtkTextView *log = GTK_TEXT_VIEW(abox->log);
	GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(log);
	GdkRectangle visible, tail;
	GtkTextIter end;

	abox->log_scroll_id = 0;

	gtk_text_view_get_visible_rect(log, &visible);
	gtk_text_buffer_get_end_iter(text_buffer, &end);
	gtk_text_view_get_iter_location(log, &end, &tail);

	if (tail.y + tail.height <= visible.y + visible.height)
		return G_SOURCE_REMOVE;

	gtk_text_view_scroll_to_iter(log, &end, 0.0, TRUE, 0.0, 1.0);

	return G_SOURCE_REMOVE;
}

/* Append a line to the log. If grow is TRUE, window height is increased
 * to suit the output line count so far, and the view is scrolled to end.
 */
void abox_log(ABox *abox, const gchar *message, const gchar *style,
		gboolean grow)
{
	GtkTextIter end;
	GtkTextBuffer *text_buffer;
	GtkTextView *log = GTK_TEXT_VIEW(abox->log);
	gchar *u8 = NULL;

	if (!g_utf8_validate(message, -1, NULL))
		u8 = to_utf8(message);

	text_buffer = gtk_text_view_get_buffer(log);

	gtk_text_buffer_get_end_iter(text_buffer, &end);
	gtk_text_buffer_insert_with_tags_by_name(text_buffer,
			&end, u8 ? u8 : message, -1, style, NULL);

	if (grow)
		abox_grow_log(abox);

	/* Coalesce a burst of lines to schedule one scroll instead of many. */
	if (abox->log_scroll_id)
		g_source_remove(abox->log_scroll_id);
	abox->log_scroll_id = g_idle_add(abox_scroll_to_end, abox);

	g_free(u8);
}

static void abox_finalise(GObject *object)
{
	ABox *abox = ABOX(object);
	GObjectClass *parent_class;

	if (abox->log_scroll_id)
	{
		g_source_remove(abox->log_scroll_id);
		abox->log_scroll_id = 0;
	}

	parent_class = g_type_class_peek(GTK_TYPE_DIALOG);

	if (G_OBJECT_CLASS(parent_class)->finalize)
		(*G_OBJECT_CLASS(parent_class)->finalize)(object);
}

void abox_set_current_object(ABox *abox, const gchar *message)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(message != NULL);
	g_return_if_fail(IS_ABOX(abox));

	gtk_label_set_text(GTK_LABEL(abox->src_label), message);
}

static void lost_preview(GtkWidget *window, ABox *abox)
{
	abox->preview = NULL;
}

static void select_row_callback(GtkTreeView *treeview,
				GtkTreePath *path,
				GtkTreeViewColumn *col,
				ABox	    *abox)
{
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	char		*leaf, *dir;

	/* Multi-selected rows cannot be activated [20260928193827]. */
	if (gtk_tree_selection_count_selected_rows(
			gtk_tree_view_get_selection(treeview)) != 1)
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, 0, &leaf, 1, &dir, -1);

	if (abox->preview)
	{
		if (strcmp(abox->preview->real_path, dir) == 0)
			display_set_autoselect(abox->preview, leaf);
		else
			filer_change_to(abox->preview, dir, leaf);
		goto out;
	}

	abox->preview = filer_opendir(dir, NULL, NULL, FALSE);
	if (abox->preview)
	{
		display_set_autoselect(abox->preview, leaf);
		g_signal_connect_object(abox->preview->window, "destroy",
				G_CALLBACK(lost_preview), abox, 0);
	}

out:
	g_free(dir);
	g_free(leaf);
}

/* Add a list-of-results area. You must use this before adding files
 * with abox_add_filename().
 */

/* Selected Find result rows, in model order. Free with result_rows_free()
 * [20260928193827].
 */
static GList *results_selected_rows(ABox *abox)
{
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	GList *paths, *l, *rows = NULL;
	GtkTreeIter iter;
	ResultRow *row;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));
	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(abox->results));
	paths = gtk_tree_selection_get_selected_rows(sel, &model);

	for (l = paths; l; l = l->next)
	{
		row = g_new(ResultRow, 1);
		if (gtk_tree_model_get_iter(model, &iter,
					(GtkTreePath *) l->data))
		{
			gtk_tree_model_get(model, &iter,
					0, &row->leaf, 1, &row->dir, -1);
			rows = g_list_prepend(rows, row);
		}
		else
		{
			g_free(row);
		}
	}

	g_list_free_full(paths, (GDestroyNotify) gtk_tree_path_free);
	return g_list_reverse(rows);
}

static void result_rows_free(GList *rows)
{
	GList *l;
	ResultRow *row;

	for (l = rows; l; l = l->next)
	{
		row = (ResultRow *) l->data;
		g_free(row->leaf);
		g_free(row->dir);
		g_free(row);
	}
	g_list_free(rows);
}

static void results_copy_paths(GtkWidget *widget, ABox *abox)
{
	GList *rows, *l;
	GString *text;
	gchar *full;

	rows = results_selected_rows(abox);
	text = g_string_new(NULL);
	for (l = rows; l; l = l->next)
	{
		ResultRow *row = (ResultRow *) l->data;
		full = g_build_filename(row->dir, row->leaf, NULL);
		if (text->len > 0)
			g_string_append_c(text, '\n');
		g_string_append(text, full);
		g_free(full);
	}
	result_rows_free(rows);

	gtk_clipboard_set_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
			text->str, -1);
	g_string_free(text, TRUE);
}

/* Append one markdown table cell, escaping pipe, backslash and newline. */
static void md_cell(GString *out, const gchar *s)
{
	for (; *s; s++)
	{
		if (*s == '\\' || *s == '|')
			g_string_append_c(out, '\\');
		g_string_append_c(out, *s == '\n' ? ' ' : *s);
	}
}

static gint results_save_table(GObject *savebox, const gchar *path,
				gpointer data)
{
	const gchar *table = g_object_get_data(savebox, "table");
	GError *error = NULL;

	if (!g_file_set_contents(path, table, -1, &error))
	{
		report_error(_("Error writing '%s': %s"),
				path, error->message);
		g_error_free(error);
		return GTK_XDS_SAVE_ERROR;
	}

	dir_check_this(path);

	return GTK_XDS_SAVED;
}

static void results_export_table(GtkWidget *widget, ABox *abox)
{
	GList *rows, *l;
	GString *table;
	GtkWidget *savebox;
	MaskedPixmap *image;
	gchar *defpath;

	rows = results_selected_rows(abox);
	table = g_string_new("| Name | Directory |\n| --- | --- |\n");
	for (l = rows; l; l = l->next)
	{
		ResultRow *row = (ResultRow *) l->data;
		g_string_append_c(table, '|');
		g_string_append_c(table, ' ');
		md_cell(table, row->leaf);
		g_string_append(table, " | ");
		md_cell(table, row->dir);
		g_string_append(table, " |\n");
	}

	defpath = g_build_filename(g_get_home_dir(), "results.md", NULL);
	result_rows_free(rows);

	/* Follow the savebox_show() pattern. */
	image = type_to_icon(text_plain);
	savebox = gtk_savebox_new(_("Export results"));
	gtk_savebox_set_action(GTK_SAVEBOX(savebox), GDK_ACTION_COPY);
	g_signal_connect(savebox, "save_to_file",
			G_CALLBACK(results_save_table), NULL);
	g_object_set_data_full(G_OBJECT(savebox), "table",
			g_string_free(table, FALSE), g_free);
	gtk_window_set_title(GTK_WINDOW(savebox), _("Export results"));
	gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), defpath);
	g_free(defpath);
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixbuf);
	g_object_unref(image);
	rox_dialog_set_parent_full(savebox, FALSE);
	number_of_windows++;
	g_signal_connect(savebox, "destroy",
			G_CALLBACK(one_less_window), NULL);
	gtk_widget_show(savebox);
}

static void results_select_all(GtkWidget *widget, ABox *abox)
{
	gtk_tree_selection_select_all(
		gtk_tree_view_get_selection(
			GTK_TREE_VIEW(abox->results)));
}

static gboolean results_button_press(GtkWidget *widget,
				GdkEventButton *event, ABox *abox)
{
	GtkTreeView *view;
	GtkTreeSelection *sel;
	GtkTreePath *path = NULL;

	if (event->type != GDK_BUTTON_PRESS ||
			event->button != GDK_BUTTON_SECONDARY)
		return FALSE;

	view = GTK_TREE_VIEW(abox->results);
	sel = gtk_tree_view_get_selection(view);
	if (gtk_tree_view_get_path_at_pos(view,
				event->x, event->y, &path, NULL, NULL, NULL))
	{
		if (!gtk_tree_selection_path_is_selected(sel, path))
		{
			gtk_tree_selection_unselect_all(sel);
			gtk_tree_selection_select_path(sel, path);
		}
		gtk_tree_path_free(path);
	}

	show_popup_menu(abox->results_menu, (GdkEvent *) event, -1);

	return TRUE;
}

void abox_add_results(ABox *abox)
{
	GtkTreeViewColumn	*column;
	GtkWidget	*scroller, *frame, *content, *item;
	GtkListStore	*model;
	GtkCellRenderer	*cell_renderer;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->results == NULL);

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	gtk_container_add(GTK_CONTAINER(frame), scroller);

	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	abox->results = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	g_object_unref(G_OBJECT(model));

	cell_renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(
				_("Name"), cell_renderer, "text", 0, NULL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
	gtk_tree_view_append_column(GTK_TREE_VIEW(abox->results), column);
	gtk_tree_view_insert_column_with_attributes(
			GTK_TREE_VIEW(abox->results),
			1, (gchar *) _("Directory"), cell_renderer,
			"text", 1, NULL);

	gtk_container_add(GTK_CONTAINER(scroller), abox->results);

	abox->results_scroll = scroller;
	gtk_widget_set_size_request(abox->results_scroll, -1,
			RESULTS_HEIGHT_MIN);

	g_signal_connect(abox->results, "row-activated",
			G_CALLBACK(select_row_callback), abox);

	gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(
				GTK_TREE_VIEW(abox->results)),
			GTK_SELECTION_MULTIPLE);
	g_signal_connect(abox->results, "button-press-event",
			G_CALLBACK(results_button_press), abox);

	/* Build once and re-pop on every press, like the filer's own menus.
	 * [20260928193827].
	 */
	abox->results_menu = gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic(_("_Copy path"));
	g_signal_connect(item, "activate",
			G_CALLBACK(results_copy_paths), abox);
	gtk_menu_shell_append(GTK_MENU_SHELL(abox->results_menu), item);
	item = gtk_menu_item_new_with_mnemonic(_("_Export to table"));
	g_signal_connect(item, "activate",
			G_CALLBACK(results_export_table), abox);
	gtk_menu_shell_append(GTK_MENU_SHELL(abox->results_menu), item);
	item = gtk_menu_item_new_with_mnemonic(_("Select _all"));
	g_signal_connect(item, "activate",
			G_CALLBACK(results_select_all), abox);
	gtk_menu_shell_append(GTK_MENU_SHELL(abox->results_menu), item);
	gtk_menu_attach_to_widget(GTK_MENU(abox->results_menu),
			GTK_WIDGET(abox), NULL);

	/* Share the expand slot between the log and the results with a
	 * draggable divider (only Find has both [20260928074452]). The
	 * unset divider gives the log its requisition size and the rest
	 * to the results. Packing results resize=TRUE keeps the divider
	 * there while the window grows. The grid sits below the divider.
	 */
	content = gtk_dialog_get_content_area(GTK_DIALOG(abox));

	g_object_ref(abox->log_hbox);
	gtk_container_remove(GTK_CONTAINER(content), abox->log_hbox);
	g_object_ref(abox->cmp_area);
	gtk_container_remove(GTK_CONTAINER(content), abox->cmp_area);

	abox->results_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

	/* No shrinking past either minimum. Note: this also keeps the
	 * divider out of the grown log space, and it is what lets window
	 * growth work at all [20260928074452].
	 */
	gtk_paned_pack1(GTK_PANED(abox->results_pane), abox->log_hbox,
			FALSE, FALSE);
	g_object_unref(abox->log_hbox);
	gtk_paned_pack2(GTK_PANED(abox->results_pane), frame, TRUE, FALSE);
	gtk_box_pack_start(GTK_BOX(content), abox->results_pane,
			TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(content), abox->cmp_area, FALSE, FALSE, 2);
	g_object_unref(abox->cmp_area);

	gtk_widget_show_all(abox->results_pane);
}

void abox_add_filename(ABox *abox, const gchar *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar	*dir;
	gchar	*base = g_path_get_basename(path);

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));

	gtk_list_store_append(GTK_LIST_STORE(model), &iter);

	dir = g_path_get_dirname(path);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter,
			   0, base,
			   1, dir, -1);
	g_free(dir);
	g_free(base);
}

/* Clear search results area */
void abox_clear_results(ABox *abox)
{
	GtkTreeModel *model;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));

	gtk_list_store_clear(GTK_LIST_STORE(model));
}

void abox_add_combo(ABox *abox, const gchar *tlabel, GList *presets,
		    const gchar *text, GtkWidget *help_button)
{
	GtkWidget *hbox, *label, *combo;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->entry == NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if (tlabel)
	{
		label = gtk_label_new(tlabel);
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	}

	combo = gtk_combo_box_text_new_with_entry();
	GList *gl = presets;
	for (; gl; gl = gl->next)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), gl->data);
	abox->entry = gtk_bin_get_child(GTK_BIN(combo));

	gtk_entry_set_activates_default(GTK_ENTRY(abox->entry), TRUE);
	gtk_entry_set_text(GTK_ENTRY(abox->entry), text);
	gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))), hbox,
				FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), help_button, FALSE, TRUE, 4);

	gtk_widget_show_all(hbox);

	shade(abox);
}

void abox_add_entry(ABox *abox, const gchar *text, GtkWidget *help_button)
{
	GtkWidget *hbox, *label;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->entry == NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Expression:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	abox->entry = gtk_entry_new();
	gtk_widget_set_name(abox->entry, "fixed-style");
	gtk_entry_set_text(GTK_ENTRY(abox->entry), text);
	gtk_box_pack_start(GTK_BOX(hbox), abox->entry, TRUE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
				hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), help_button,
				FALSE, TRUE, 4);

	gtk_entry_set_activates_default(GTK_ENTRY(abox->entry), TRUE);

	gtk_widget_show_all(hbox);

	shade(abox);
}

static void shade(ABox *abox)
{
	GtkDialog *dialog = (GtkDialog *) abox;
	gboolean quiet, on = abox->question;

	quiet = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abox->quiet));

	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_YES, on);
	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_NO, on);

	if (on && !quiet)
		gtk_dialog_set_response_sensitive(dialog, RESPONSE_QUIET, TRUE);
	else
		gtk_dialog_set_response_sensitive(dialog, RESPONSE_QUIET, FALSE);

	if (!on)
	{
		gtk_widget_set_sensitive(abox->btn_seqno, on);
		gtk_widget_set_sensitive(abox->btn_seqno_all, on);
	}

	/* Unsetting the focus means that set_default will put it in the
	 * right place...
	 */
	gtk_window_set_focus(GTK_WINDOW(abox), NULL);
	/* Note: Gtk+-2.0.0 will segfault on Return if an insensitive
	 * widget is the default.
	 */
	if (quiet)
		gtk_dialog_set_default_response(dialog, GTK_RESPONSE_YES);
	else
		gtk_dialog_set_default_response(dialog, RESPONSE_QUIET);

	if (abox->entry)
	{
		gtk_widget_set_sensitive(abox->entry, on);
		if (on)
			gtk_widget_grab_focus(abox->entry);
	}
}

static gboolean abox_delete(GtkWidget *dialog, GdkEventAny *event)
{
	g_signal_emit_by_name(dialog, "abort_operation");
	return TRUE;
}

void abox_show_compare(ABox *abox, gboolean show)
{
	if (show)
		gtk_widget_show(abox->cmp_area);
	else
		gtk_widget_hide(abox->cmp_area);
}

void abox_set_file(ABox *abox, int i, const gchar *path)
{
	DirItem *item;
	gchar *base;

	g_return_if_fail(i >= 0 && i < 2);
	g_return_if_fail(IS_ABOX(abox));

	if (!path || !path[0])
	{
		gtk_widget_hide(abox->cmp_icon[i]);
		gtk_widget_hide(abox->cmp_name[i]);
		gtk_widget_hide(abox->cmp_size[i]);
		gtk_widget_hide(abox->cmp_date[i]);
		gtk_widget_hide(abox->cmp_arrow);
		return;
	}

	base = g_path_get_basename(path);
	item = diritem_new(base);
	g_free(base);
	diritem_restat(path, item, NULL, TRUE);

	gtk_image_set_from_pixbuf(GTK_IMAGE(abox->cmp_icon[i]),
				  di_image(item)->pixbuf);
	gtk_widget_show(abox->cmp_icon[i]);

	gtk_label_set_text(GTK_LABEL(abox->cmp_name[i]), item->leafname);
	gtk_widget_show(abox->cmp_name[i]);
	gtk_widget_show(abox->cmp_arrow);

	if (item->lstat_errno)
	{
		gtk_label_set_text(GTK_LABEL(abox->cmp_size[i]), "Error");
		gtk_label_set_text(GTK_LABEL(abox->cmp_date[i]),
				g_strerror(item->lstat_errno));
	}
	else
	{
		gchar *str;

		gtk_label_set_text(GTK_LABEL(abox->cmp_size[i]),
				format_size_aligned(item->size));

		str = pretty_time(&item->mtime);
		gtk_label_set_text(GTK_LABEL(abox->cmp_date[i]), str);
		g_free(str);
	}

	gtk_widget_show(abox->cmp_size[i]);
	gtk_widget_show(abox->cmp_date[i]);

	diritem_free(item);
}

static void _abox_set_percentage(ABox *abox, GtkWidget **prog, int per)
{
	if(!*prog) {
		*prog = gtk_progress_bar_new();
		gtk_widget_set_size_request(*prog, -1, small_height * (2./3.));
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
				*prog, FALSE, FALSE, 2);
		gtk_widget_show(*prog);
	}
	if(per<0 || per>100) {
		gtk_widget_hide(*prog);
		return;
	}
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(*prog), per/100.);
}
void abox_set_percentage(ABox *abox, int per)
{
	_abox_set_percentage(abox, &abox->progress, per);
}
void abox_set_file_percentage(ABox *abox, int per)
{
	_abox_set_percentage(abox, &abox->fileprog, per);
}
