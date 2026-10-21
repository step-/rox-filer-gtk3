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

/* bookmarks.c - handles the bookmarks menu */

#include "config.h"

#include <stdbool.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/param.h>

#include "global.h"

#include "bookmarks.h"
#include "choices.h"
#include "filer.h"
#include "minibuffer.h"
#include "run.h"
#include "xml.h"
#include "support.h"
#include "gui_support.h"
#include "i18n.h"
#include "main.h"
#include "mount.h"
#include "action.h"
#include "options.h"
#include "bind.h"
#include "menu.h"
#include "diritem.h"
#include "pixmaps.h"

static GList *history = NULL;		/* Most recent first */
static GList *history_tail = NULL;	/* Oldest item */
static GHashTable *history_hash = NULL;	/* Path -> GList link */
static gint history_free = 30;		/* Space left in history */

static XMLwrapper *bookmarks = NULL;
static GtkWidget *bookmarks_window = NULL;

/* Static prototypes */
static void update_bookmarks(void);
static xmlNode *bookmark_find(const gchar *mark);
static void bookmarks_save(void);
static void bookmarks_add(GtkMenuItem *menuitem, gpointer user_data);
static void bookmarks_activate(GtkMenuShell *item, FilerWindow *filer_window);
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window);
static void cell_edited(GtkCellRendererText *cell,
	     const gchar *path_string,
	     const gchar *new_text,
	     gpointer data);
static void reorder_up(GtkButton *button, GtkTreeView *view);
static void reorder_down(GtkButton *button, GtkTreeView *view);
static void edit_response(GtkWidget *window, gint response,
			  GtkTreeModel *model);
static void edit_delete(GtkButton *button, GtkTreeView *view);
static gboolean dir_dropped(GtkWidget *window, GdkDragContext *context,
			    int x, int y,
			    GtkSelectionData *selection_data, guint info,
			    guint time, GtkTreeView *view);
static void bookmarks_add_dir(const gchar *dir);
static void commit_edits(GtkTreeModel *model);



//menu icons
typedef struct {
	GtkWidget *fix;
	GtkWidget *lbl;
	DirItem *ditem;
	char *path;
	GtkWidget *icon;
} BItem;
static int pixels;
static int iconw;
static GList *items;
static GList *itemshist; //temp
static guint iconloop;
static GThread *icont;
static bool iconfinish;
static GMutex itemm;
static GtkSizeGroup *icon_group;
static GtkSizeGroup *title_group;
static GtkSizeGroup *dirname_group;
static void resetitems()
{
	if (iconloop)
	{
		iconfinish = true;
		if (icont)
			g_thread_join(icont);
		icont = NULL;
		g_source_remove(iconloop);
		iconloop = 0;
	}

	/* pixels <= 0 means the No Menu Icons" option is set. */
	pixels = get_menu_icon_pixels();
	iconw = pixels > 0 ? pixels + 1 : small_width / 6;

	if (items)
	{
		for (GList *next = items; next; next = next->next)
		{
			BItem *bi = next->data;
			g_free(bi->path); //free the path
			if (bi->ditem)
				diritem_free(bi->ditem);
		}
		g_list_free_full(items, g_free);
		items = NULL;
	}
}
static gpointer icon_thread(gpointer data)
{
	for (GList *next = items; next; next = next->next)
	{
		if (iconfinish) break;
		BItem *bi = next->data;

		DirItem *ditem = diritem_new("");
		diritem_restat(bi->path, ditem, NULL, TRUE);

		g_mutex_lock(&itemm);
		bi->ditem = ditem;
		g_mutex_unlock(&itemm);
	}

	iconfinish = true;
	return NULL;
}
static gboolean iconloopcb(gpointer p)
{
	g_mutex_lock(&itemm);
	for (GList *next = items; next; next = next->next)
	{
		GtkImageType type;
		GtkWidget *ri;
		BItem *bi = next->data;
		if (!bi->ditem) continue;

		if(!bi->icon) goto next;
		ri = menu_make_image(bi->ditem, pixels);
		/* Options > Menus > "Size of icons in menus" "No Icons" */
		if (!ri) goto next;

		/* Replace the placeholder image */
		if(GTK_IS_IMAGE(ri)) {
			type = gtk_image_get_storage_type(GTK_IMAGE(ri));
			if (type == GTK_IMAGE_PIXBUF) {
				gtk_image_set_from_pixbuf(GTK_IMAGE(bi->icon),
					gtk_image_get_pixbuf(GTK_IMAGE(ri)));
			}
			gtk_widget_show(bi->icon);
		}
		gtk_widget_destroy(ri);

next:
		diritem_free(bi->ditem);
		bi->ditem = NULL;
	}
	bool finish = iconfinish;
	g_mutex_unlock(&itemm);

	if (!finish) return TRUE;

	resetitems();
	return FALSE;
}
static void makeicons()
{
	iconfinish = false;
	icont = g_thread_new("b_icon_t", icon_thread, NULL);
	iconloop = g_idle_add(iconloopcb, NULL);
}

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Shows the bookmarks menu */
void bookmarks_show_menu(FilerWindow *filer_window, GtkWidget *widget)
{
	GdkEvent *event;
	static GtkMenu *menu = NULL;

	event = gtk_get_current_event();
	if (event)
	{
		gdk_event_free(event);
	}

	resetitems();
	if (menu)
		gtk_widget_destroy((GtkWidget *) menu);

	menu = GTK_MENU(bookmarks_build_menu(filer_window));

	if (widget)
		gtk_menu_popup_at_pointer(menu, NULL);
	else
		gtk_menu_popup_at_pointer(menu, NULL);
}

/* Show the Edit Bookmarks dialog */
void bookmarks_edit(void)
{
	GtkListStore *model;
	GtkWidget *list, *hbox, *button, *swin, *caption;
	GtkTreeSelection *selection;
	GtkCellRenderer *cell;
	xmlNode *node;
	GtkTreeIter iter;

	if (bookmarks_window)
	{
		gtk_window_present(GTK_WINDOW(bookmarks_window));
		return;
	}

	update_bookmarks();

	bookmarks_window = gtk_dialog_new();
	number_of_windows++;

	gtk_window_set_position(GTK_WINDOW(bookmarks_window),
			GTK_WIN_POS_MOUSE);
	gtk_window_set_default_size(GTK_WINDOW(bookmarks_window), 520, 420);


	button = button_new_mixed("window-close", _("_Close"));
	gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
	gtk_dialog_add_action_widget(GTK_DIALOG(bookmarks_window),
			button, GTK_RESPONSE_CLOSE);

	g_signal_connect(bookmarks_window, "destroy",
			 G_CALLBACK(gtk_widget_destroyed), &bookmarks_window);
	g_signal_connect(bookmarks_window, "destroy",
			 G_CALLBACK(one_less_window), NULL);

	swin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(swin),
			GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swin),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(bookmarks_window))),
			swin, TRUE, TRUE, 0);

	caption = gtk_label_new(
			_("Titles use accelerator notation: "
			  "'_x' underlines x; write '__' for a literal underscore."));
	gtk_label_set_xalign(GTK_LABEL(caption), 0.0);
	gtk_widget_set_halign(caption, GTK_ALIGN_START);
	gtk_widget_set_margin_start(caption, 12);
	gtk_widget_set_margin_end(caption, 12);
	gtk_widget_set_margin_top(caption, 12);
	gtk_widget_set_margin_bottom(caption, 12);

	gtk_box_pack_start(
			GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(bookmarks_window))),
			caption, FALSE, TRUE, 0);
	gtk_box_reorder_child(
			GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(bookmarks_window))),
			caption, 0);

	model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING,
			GDK_TYPE_PIXBUF);

	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));

	cell = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), 0,
			"", cell, "pixbuf", 2, NULL);

	cell = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(cell), "edited",
		    G_CALLBACK(cell_edited), model);
	g_object_set(G_OBJECT(cell), "editable", TRUE, NULL);
	g_object_set_data(G_OBJECT(cell), "column", GINT_TO_POINTER(1));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		_("Title"), cell, "text", 1, NULL);

	cell = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(cell), "edited",
		    G_CALLBACK(cell_edited), model);
	g_object_set(G_OBJECT(cell), "editable", TRUE, NULL);
	g_object_set_data(G_OBJECT(cell), "column", GINT_TO_POINTER(0));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		_("Path"), cell, "text", 0, NULL);

	gtk_tree_view_column_set_resizable(
			gtk_tree_view_get_column(GTK_TREE_VIEW(list), 0),
			TRUE);

	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(list), TRUE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), TRUE);

	node = xmlDocGetRootElement(bookmarks->doc);
	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		GtkTreeIter iter;
		gchar *mark, *title;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, BAD_CAST "bookmark") != 0)
			continue;

		mark = (gchar *) xmlNodeListGetString(bookmarks->doc,
					    node->xmlChildrenNode, 1);
		if (!mark)
			continue;

		title = (gchar *) xmlGetProp(node, BAD_CAST "title");
		if(!title)
			title=mark;

		gtk_list_store_append(model, &iter);

		GdkPixbuf *pixbuf = NULL;
		char *full = expand_path(mark);
		if (full)
		{
			DirItem *ditem = diritem_new("");
			diritem_restat(full, ditem, NULL, TRUE);
			MaskedPixmap *im = di_image(ditem);
			if (im)
			{
				if (!im->sm_pixbuf)
					pixmap_make_small(im);
				pixbuf = g_object_ref(im->sm_pixbuf ?
						im->sm_pixbuf : im->pixbuf);
			}
			diritem_free(ditem);
			g_free(full);
		}
		gtk_list_store_set(model, &iter, 0, mark, 1, title,
				2, pixbuf, -1);
		if (pixbuf)
			g_object_unref(pixbuf);
		if(title!=mark)
			xmlFree(title);

		xmlFree(mark);
	}

	gtk_widget_set_size_request(list, 600, 300);
	gtk_container_add(GTK_CONTAINER(swin), list);

	hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(bookmarks_window))),
			hbox, FALSE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);

	button = button_new_mixed("edit-delete", _("_Delete"));
	gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(edit_delete), list);

	button = button_new_mixed("go-up", _("_Up"));
	gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(reorder_up), list);
	button = button_new_mixed("go-down", _("_Down"));
	gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(reorder_down), list);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	/* Select the first item, otherwise the first click starts edit
	 * mode, which is very confusing!
	 */
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		gtk_tree_selection_select_iter(selection, &iter);

	g_signal_connect(bookmarks_window, "response",
			 G_CALLBACK(edit_response), model);

	/* Allow directories to be dropped in */
	{
		GtkTargetEntry targets[] = { {"text/uri-list", 0, 0} };
		gtk_drag_dest_set(bookmarks_window, GTK_DEST_DEFAULT_ALL,
				targets, G_N_ELEMENTS(targets),
				GDK_ACTION_COPY |GDK_ACTION_PRIVATE);
		g_signal_connect(bookmarks_window, "drag-data-received",
				G_CALLBACK(dir_dropped), list);
	}

	g_signal_connect_swapped(model, "row-changed",
			 G_CALLBACK(commit_edits), model);
	g_signal_connect_swapped(model, "row-inserted",
			 G_CALLBACK(commit_edits), model);
	g_signal_connect_swapped(model, "row-deleted",
			 G_CALLBACK(commit_edits), model);
	g_signal_connect_swapped(model, "rows-reordered",
			 G_CALLBACK(commit_edits), model);

	rox_dialog_set_parent_full(bookmarks_window, FALSE);
	gtk_widget_show_all(bookmarks_window);

	gtk_window_present(GTK_WINDOW(bookmarks_window));
	gtk_widget_grab_focus(list);
}

static void history_remove(const char *path)
{
	GList *old;

	old = g_hash_table_lookup(history_hash, path);
	if (old)
	{
		g_hash_table_remove(history_hash, path);

		if (history_tail == old)
			history_tail = old->prev;
		g_free(old->data);
		history = g_list_delete_link(history, old);

		history_free++;
	}
}

/* Add this path to the global history of visited directories. If it
 * already exists there, make it the most recent.
 *
 * The sequence is kept complete (no pruning) so that middle-clicking
 * the Bookmarks button can always reach the previously visited
 * directory; the Recently Visited menu trims nested entries at display
 * time via its common-prefix logic. (Upstream pruned the parent here,
 * r3620, which broke that Back path for ancestor navigation.)
 */
void bookmarks_add_history(const gchar *path)
{
	char *new;

	new = g_strdup(path);
	ensure_utf8(&new);

	if (!history_hash)
		history_hash = g_hash_table_new(g_str_hash, g_str_equal);

	history_remove(new);

	history = g_list_prepend(history, new);
	if (!history_tail)
		history_tail = history;
	g_hash_table_insert(history_hash, new, history);

	history_free--;
	if (history_free == -1)
	{
		g_return_if_fail(history_tail != NULL);
		history_remove((char *) history_tail->data);
	}
}

gchar *bookmarks_get_recent(void)
{
	if (history->next)
		return history->next->data;
	else
		return NULL;
}

gchar *bookmarks_get_top(void)
{
	static gchar *path;
	gchar *mark;
	xmlNode *node;

	g_free(path);

	update_bookmarks();
	node = xmlDocGetRootElement(bookmarks->doc);
	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, BAD_CAST "bookmark") != 0)
			continue;

		mark = (gchar *) xmlNodeListGetString(bookmarks->doc,
							node->xmlChildrenNode, 1);
		if (mark)
		{
			path = expand_path(mark);
			xmlFree(mark);
			return path;
		}
	}
		return NULL;
}

void bookmarks_add_uri(const EscapedPath *uri)
{
	char *path;
	struct stat info;

	path = get_local_path(uri);

	if (!path)
	{
		delayed_error(_("Can't bookmark non-local resource '%s'\n"),
					uri);
		return;
	}

	if (mc_stat(path, &info) == 0 && S_ISDIR(info.st_mode))
		bookmarks_add_dir(path);
	else
		delayed_error(_("'%s' isn't a directory"), path);
	g_free(path);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Initialise the bookmarks document to be empty. Does not save. */
static void bookmarks_new(void)
{
	if (bookmarks)
		g_object_unref(G_OBJECT(bookmarks));
	bookmarks = xml_new(NULL);
	bookmarks->doc = xmlNewDoc(BAD_CAST "1.0");
	xmlDocSetRootElement(bookmarks->doc,
		xmlNewDocNode(bookmarks->doc, NULL, BAD_CAST "bookmarks", NULL));
}


/* Makes sure that 'bookmarks' is up-to-date, reloading from file if it has
 * changed. If no bookmarks were loaded and there is no file then initialise
 * bookmarks to an empty document.
 */
static void update_bookmarks()
{
	gchar *path;

	/* Update the bookmarks, if possible */
	path = choices_find_xdg_path_load("Bookmarks.xml", project_name, SITE);
	if (path)
	{
		XMLwrapper *wrapper;
		wrapper = xml_cache_load(path);
		if (wrapper)
		{
			if (bookmarks)
				g_object_unref(bookmarks);
			bookmarks = wrapper;
		}

		g_free(path);
	}

	if (!bookmarks)
		bookmarks_new();
}

/* Return TRUE if there are some bookmarks. Call after update_bookmarks(). */
static gboolean has_bookmarks(void)
{
	xmlNode *n = xmlDocGetRootElement(bookmarks->doc);
	for (n = n ? n->xmlChildrenNode : NULL; n; n = n->next) {
		if (n->type == XML_ELEMENT_NODE && !xmlStrcmp(n->name, BAD_CAST "bookmark"))
			return TRUE;
	}
	return FALSE;
}

/* Return the node for the 'mark' bookmark */
static xmlNode *bookmark_find(const gchar *mark)
{
	xmlNode *node;

	update_bookmarks();

	node = xmlDocGetRootElement(bookmarks->doc);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		gchar *path;
		gboolean same;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, BAD_CAST "bookmark") != 0)
			continue;

		path = (gchar *) xmlNodeListGetString(bookmarks->doc,
					node->xmlChildrenNode, 1);
		if (!path)
			continue;

		same = strcmp(mark, path) == 0;
		xmlFree(path);

		if (same)
			return node;
	}

	return NULL;
}

/* Save the bookmarks to a file */
static void bookmarks_save()
{
	gchar	*save_path;

	save_path = choices_find_xdg_path_save("Bookmarks.xml", project_name, SITE,
					       TRUE);
	if (save_path)
	{
		save_xml_file(bookmarks->doc, save_path);
		g_free(save_path);
	}
}

/* Add a bookmark if it doesn't already exist, and save the
 * bookmarks.
 */
static void bookmarks_add(GtkMenuItem *menuitem, gpointer user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	bookmarks_add_dir(filer_window->sym_path);
}

static void bookmarks_add_dir(const gchar *dir)
{
	xmlNode	*bookmark, *node;
	gchar *basename, *path, *title, *alist = g_new0(gchar, 27);

	path = collapse_path(dir);
	if (bookmark_find(path)){
		g_free(path);
		return;
	}

	basename = g_path_get_basename(path);
	node = xmlDocGetRootElement(bookmarks->doc);
	bookmark = xmlNewTextChild(node, NULL, BAD_CAST "bookmark", (const xmlChar *) path);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		title = (gchar *) xmlGetProp(node, BAD_CAST "title");
		if (title){
			get_mnemonic(title, alist);
			xmlFree(title);
		}
	}

	xmlSetProp(bookmark, BAD_CAST "title", (const xmlChar *) add_mnemonic(basename, alist));

	bookmarks_save();

	if (bookmarks_window)
		gtk_widget_destroy(bookmarks_window);

	g_free(basename);
	g_free(path);
	g_free(alist);
}

/* Called when a bookmark has been chosen */
static void bookmarks_activate(GtkMenuShell *item, FilerWindow *filer_window)
{
	const gchar *mark;
	GtkLabel *label;
	GdkEvent *event;
	gboolean new_win=FALSE;

	mark=g_object_get_data(G_OBJECT(item), "bookmark-path");
	if(!mark) {
		label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(item)));
		mark = gtk_label_get_text(label);
	}

	event=gtk_get_current_event();
	if(event)
	{
		if(event->type==GDK_BUTTON_PRESS ||
		   event->type==GDK_BUTTON_RELEASE)
		{
			GdkEventButton *button=(GdkEventButton *) event;

			new_win=o_new_button_1.int_value?
				button->button==1: button->button!=1;
		}
		gdk_event_free(event);
	}

	if (strcmp(mark, filer_window->sym_path) != 0)
	{
		if(new_win)
			filer_opendir(mark, filer_window, NULL, FALSE);
		else
			filer_change_to(filer_window, mark, NULL);
	}
	if (g_hash_table_lookup(fstab_mounts, filer_window->real_path) &&
		!mount_is_mounted(filer_window->real_path, NULL, NULL))
	{
		GList	*paths;

		paths = g_list_prepend(NULL, filer_window->real_path);
		action_mount(paths, FALSE, TRUE, -1);
		g_list_free(paths);
	}
}

static void edit_delete(GtkButton *button, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkListStore *list;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gboolean more, any = FALSE;

	model = gtk_tree_view_get_model(view);
	list = GTK_LIST_STORE(model);

	selection = gtk_tree_view_get_selection(view);

	more = gtk_tree_model_get_iter_first(model, &iter);

	while (more)
	{
		GtkTreeIter old = iter;

		more = gtk_tree_model_iter_next(model, &iter);

		if (gtk_tree_selection_iter_is_selected(selection, &old))
		{
			any = TRUE;
			gtk_list_store_remove(list, &old);
		}
	}

	if (!any)
	{
		report_error(_("You should first select some rows to delete"));
		return;
	}
}

static void reorder(GtkTreeView *view, int dir)
{
	GtkTreeModel *model;
	GtkListStore *list;
	GtkTreePath *cursor = NULL;
	GtkTreeIter iter, old, new;
	GValue mark = {0};
	GValue title = {0};
	gboolean    ok;

	g_return_if_fail(view != NULL);
	g_return_if_fail(dir == 1 || dir == -1);

	model = gtk_tree_view_get_model(view);
	list = GTK_LIST_STORE(model);

	gtk_tree_view_get_cursor(view, &cursor, NULL);
	if (!cursor)
	{
		report_error(_("Put the cursor on an entry in the "
			       "list to move it"));
		return;
	}

	gtk_tree_model_get_iter(model, &old, cursor);
	if (dir > 0)
	{
		gtk_tree_path_next(cursor);
		ok = gtk_tree_model_get_iter(model, &iter, cursor);
	}
	else
	{
		ok = gtk_tree_path_prev(cursor);
		if (ok)
			gtk_tree_model_get_iter(model, &iter, cursor);
	}
	if (!ok)
	{
		gtk_tree_path_free(cursor);
		report_error(_("This item is already at the end"));
		return;
	}

	gtk_tree_model_get_value(model, &old, 0, &mark);
	gtk_tree_model_get_value(model, &old, 1, &title);
	if (dir > 0)
		gtk_list_store_insert_after(list, &new, &iter);
	else
		gtk_list_store_insert_before(list, &new, &iter);
	gtk_list_store_set(list, &new, 0, g_value_get_string(&mark), -1);
	gtk_list_store_set(list, &new, 1, g_value_get_string(&title), -1);
	gtk_list_store_remove(list, &old);

	g_value_unset(&mark);
	g_value_unset(&title);

	gtk_tree_view_set_cursor(view, cursor, 0, FALSE);
	gtk_tree_path_free(cursor);
}

static void reorder_up(GtkButton *button, GtkTreeView *view)
{
	reorder(view, -1);
}

static void reorder_down(GtkButton *button, GtkTreeView *view)
{
	reorder(view, 1);
}

static gboolean dir_dropped(GtkWidget *window, GdkDragContext *context,
			    int x, int y,
			    GtkSelectionData *selection_data, guint info,
			    guint time, GtkTreeView *view)
{
	GtkListStore *model;
	GList *uris, *next;

	if (!gtk_selection_data_get_data(selection_data))
	{
		/* Timeout? */
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		return TRUE;
	}

	model = GTK_LIST_STORE(gtk_tree_view_get_model(view));

	uris = uri_list_to_glist((const char *) gtk_selection_data_get_data(selection_data));

	for (next = uris; next; next = next->next)
	{
		gchar *path;

		path = get_local_path((EscapedPath *) next->data);

		if (path)
		{
			GtkTreeIter iter;
			struct stat info;

			if (mc_stat(path, &info) == 0 && S_ISDIR(info.st_mode))
			{
				gtk_list_store_append(model, &iter);
				gtk_list_store_set(model, &iter, 0, path,
						   1, path, -1);
			}
			else
				delayed_error(_("'%s' isn't a directory"),
						path);

			g_free(path);
		}
		else
			delayed_error(_("Can't bookmark non-local directories "
					"like '%s'"), (gchar *) next->data);
	}

	destroy_glist(&uris);

	return TRUE;
}

static void commit_edits(GtkTreeModel *model)
{
	GtkTreeIter iter;

	bookmarks_new();

	if (gtk_tree_model_get_iter_first(model, &iter))
	{
		GValue mark = {0}, title={0};
		xmlNode *root = xmlDocGetRootElement(bookmarks->doc);

		do
		{
			xmlNode *bookmark;

			gtk_tree_model_get_value(model, &iter, 0, &mark);
			bookmark = xmlNewTextChild(root, NULL, BAD_CAST "bookmark",
					(const xmlChar *) g_value_get_string(&mark));
			g_value_unset(&mark);
			gtk_tree_model_get_value(model, &iter, 1, &title);
			xmlSetProp(bookmark, BAD_CAST "title",
				   (const xmlChar *) g_value_get_string(&title));
			g_value_unset(&title);
		} while (gtk_tree_model_iter_next(model, &iter));
	}

	bookmarks_save();
}

static void edit_response(GtkWidget *window, gint response, GtkTreeModel *model)
{
	commit_edits(model);

	gtk_widget_destroy(window);
}

static void cell_edited(GtkCellRendererText *cell,
	     const gchar *path_string,
	     const gchar *new_text,
	     gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *) data;
	GtkTreePath *path;
	GtkTreeIter iter;
	gint col;

	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_path_free(path);
	col=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column"));

	gtk_list_store_set(GTK_LIST_STORE(model), &iter, col, new_text, -1);
}

static void activate_edit(GtkMenuShell *item, gpointer data)
{
	if (!has_bookmarks()) {
		/* TRANSLATORS: Keep consistent with the actual button label. */
		report_error(_("You should first add bookmarks using the \"Add New Bookmark\" button"));
		return;
	}
	bookmarks_edit();
}

static void free_path_for_item(GtkWidget *widget, gpointer udata)
{
	gchar *path=(gchar *) udata;
	g_free(path);
}


static GtkWidget *build_history_menu(FilerWindow *filer_window)
{
	GtkWidget *menu;
	GList	  *next;

	menu = gtk_menu_new();

	if (!history)
		return menu;

	g_return_val_if_fail(history_hash != NULL, menu);
	g_return_val_if_fail(history_tail != NULL, menu);

	int count = 0;
	for (next = history; next; next = next->next)
	{
		GtkWidget *item;
		char *path = (char *) next->data;
		gchar *copy;

		if (!(next->prev) && strcmp(path, filer_window->sym_path) == 0)
			continue;

		GtkWidget *label, *fix;

		/* Input: 'path' and 'nn' string pointers (abs path or "").
		Output: 'pathp' and 'nn' point to the last common
		separator ('/'), or index 0 if no common separator exists. */
		int i = 0;
		char *pathp, *bpath, *nn = next->next ? (char *) next->next->data : "";
		pathp = path;

		/* Advance to the last common byte */
		while (*pathp != '\0' && *pathp == *nn && pathp[1] != '\0' && pathp[1] == nn[1]) {
			pathp++;
			nn++;
			i++;
		}
		/* Backtrack to the last common separator. */
		while (pathp > path && *pathp != '/') {
			pathp--;
			nn--;
			i--;
		}
/*
PROBLEMS FIXED:
1. Buffer Over-read: Checks for '\0' before evaluating memory boundaries.
2. Unsafe Fallback OOB: Prevents blind pointer increment on the "" fallback.
3. Pointer Overshoot: Uses 1-byte lookahead to break exactly on the last
   matching byte without requiring post-loop backtracking arithmetic.
4. UTF-8 Slicing & Path Integrity: Backtracks to the last common '/' to
   guarantee pointers rest on valid UTF-8 boundaries and complete directories.
 */

		bpath = g_strndup(path, i);
		item = gtk_menu_item_new();

		fix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		BItem *bi = g_new0(BItem, 1);
		bi->fix = fix;
		bi->path = g_strdup(path);
		itemshist = g_list_append(itemshist, bi);

		label =  gtk_label_new(bpath);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_halign(label, GTK_ALIGN_START);

		{
			GtkStyleContext *ctx = gtk_widget_get_style_context(label);
			GdkRGBA rgba;
			gtk_style_context_get_color(ctx, GTK_STATE_FLAG_INSENSITIVE, &rgba);
			widget_set_fg_color(label, &rgba);
		}

		gtk_box_pack_start(GTK_BOX(fix), label, FALSE, FALSE, 0);
		label =  gtk_label_new(pathp);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_halign(label, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(fix), label, TRUE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(item), fix);

		g_free(bpath);

		copy=g_strdup(path);
		g_object_set_data(G_OBJECT(item), "bookmark-path", copy);
		g_signal_connect(item, "destroy",
				 G_CALLBACK(free_path_for_item), copy);

		if (strcmp(path, filer_window->sym_path) == 0)
			gtk_widget_set_sensitive(item, FALSE);
		else
			g_signal_connect(item, "activate",
					G_CALLBACK(bookmarks_activate),
					filer_window);

		gtk_widget_show_all(item);

		//attach with 0,2 removes margin
		gtk_menu_attach(GTK_MENU(menu), item, 0, 2, count, count + 1);
		count++;
	}

	return menu;
}

/* [20260913094148] Row activation for the Numbered Groups submenu:
 * jump to the saved slot (directory + selection restore).
 */
static void groups_activate(GtkMenuItem *item, FilerWindow *filer_window)
{
	char	*name;

	name = g_object_get_data(G_OBJECT(item), "group-name");
	if (name)
		group_restore(filer_window, name);
}

/* [20260913094148] Open a filer window showing Groups.xml for hand-editing.
 */
static void groups_edit_xml(GtkMenuItem *item, FilerWindow *filer_window)
{
	gchar	*path;

	(void) item;
	(void) filer_window;

	path = choices_find_xdg_path_save("Groups.xml", project_name,
					  SITE, TRUE);
	if (path)
	{
		open_to_show(path);
		g_free(path);
	}
}

/* [20261009085331] Open the manual at "Saving and restoring the
 * selection" for the Numbered Groups submenu.
 */
static void groups_show_help(GtkMenuItem *item, FilerWindow *filer_window)
{
	gchar	*manual = NULL;
	gchar	*base, *uri;
	gchar	*errmsg = NULL;

	(void) item;
	(void) filer_window;

	if (current_lang)
	{
		manual = g_strconcat(app_dir, "/Help/Manual-",
				     current_lang, ".html", NULL);
		if (!file_exists(manual) && strchr(current_lang, '_'))
		{
			/* Try again without the territory */
			strcpy(strrchr(manual, '_'), ".html");
		}
		if (!file_exists(manual))
			null_g_free(&manual);
	}

	if (!manual)
	{
		manual = g_strconcat(app_dir, "/Help/Manual.html", NULL);
	}

	if (!file_exists(manual))
	{
		/* Same single error as Help > Manual (menu_rox_help):
		 * run_by_path() reports the missing file itself, so
		 * stay silent here rather than adding a second dialog.
		 */
		run_by_path(manual);
		g_free(manual);
		return;
	}

	base = g_filename_to_uri(manual, NULL, NULL);
	if (!base)
	{
		base = g_strconcat("file://", manual, NULL);
	}
	uri = g_strconcat(base, "#save-selection", NULL);
	if (!run_by_uri(uri, &errmsg))
	{
		report_error("%s", errmsg ? errmsg : uri);
	}
	g_free(errmsg);
	g_free(uri);
	g_free(base);
	g_free(manual);
}

/* Build the Numbered Groups submenu. Done whenever the bookmarks
 * menu is built, so the rows always mirror Groups.xml.
 */
static GtkWidget *build_groups_menu(FilerWindow *filer_window)
{
	GtkWidget	*menu;
	GtkWidget	*tail;
	int		i;

	menu = gtk_menu_new();

	for (i = 0; i <= 9; i++)
	{
		GtkWidget	*item;
		char		name[2] = "0";
		char		*dir = NULL;
		int		n_items = 0;
		gchar		*label;

		name[0] = '0' + i;
		if (group_get_info(name, &dir, &n_items))
		{
			gchar *hint;

			/* Binding hint tracks group_digits_modifier */
			if (o_group_digits_modifier.int_value)
			{
				hint = g_strdup_printf("Alt+%d", i);
			}
			else
			{
				hint = g_strdup_printf("%d", i);
			}
			if (n_items > 0)
			{
				label = g_strdup_printf("%d: %s (+%d)  [%s]",
							i, dir, n_items, hint);
			}
			else
			{
				label = g_strdup_printf("%d: %s  [%s]",
							i, dir, hint);
			}
			g_free(hint);
			g_free(dir);

			item = gtk_menu_item_new_with_label(label);
			g_free(label);

			g_object_set_data_full(G_OBJECT(item), "group-name",
					g_strdup(name), g_free);
			g_signal_connect(item, "activate",
					G_CALLBACK(groups_activate),
					filer_window);
		}
		else
		{
			label = g_strdup_printf("%d: %s", i, _("(empty)"));
			item = gtk_menu_item_new_with_label(label);
			g_free(label);
			gtk_widget_set_sensitive(item, FALSE);
		}
		gtk_widget_show(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}

	/* Slot reset: open the config dir with
	 * Groups.xml wink-selected; the user deletes <group> nodes.
	 */
	tail = gtk_separator_menu_item_new();
	gtk_widget_show(tail);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), tail);

	tail = gtk_menu_item_new_with_mnemonic(_("_Edit Groups.xml"));
	g_signal_connect(tail, "activate",
			G_CALLBACK(groups_edit_xml), filer_window);
	gtk_widget_show(tail);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), tail);

	tail = gtk_menu_item_new_with_mnemonic(_("_Help topic"));
	g_signal_connect(tail, "activate",
			G_CALLBACK(groups_show_help), filer_window);
	gtk_widget_show(tail);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), tail);

	return menu;
}

/* Build the bookmarks menu. Done on Go icon clicked.
 */
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window)
{
	GtkWidget *menu;
	GtkWidget *item;
	xmlNode *node;
	gboolean need_separator = TRUE;
	int count = 6;

	if (!title_group)
		title_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	if (!dirname_group)
		dirname_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	if (!icon_group)
		icon_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	menu = gtk_menu_new();

	/* Per-window navigation [20260825105933]. The menu is rebuilt
	 * on every popup, so sensitivity mirrors the current stacks. */
	item = gtk_menu_item_new_with_label(_("Back"));
	gtk_widget_set_sensitive(item, filer_window->back_stack != NULL);
	g_signal_connect_swapped(item, "activate",
			G_CALLBACK(filer_go_back), filer_window);
	gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), "<filer>/Back");
	gtk_accel_label_set_accel(GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(item))),
			GDK_KEY_Left, GDK_MOD1_MASK);
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 0, 1, 0, 1);

	item = gtk_menu_item_new_with_label(_("Forward"));
	gtk_widget_set_sensitive(item, filer_window->fwd_stack != NULL);
	g_signal_connect_swapped(item, "activate",
			G_CALLBACK(filer_go_forward), filer_window);
	gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), "<filer>/Forward");
	gtk_accel_label_set_accel(GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(item))),
			GDK_KEY_Right, GDK_MOD1_MASK);
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 1, 2, 0, 1);

	/* TRANSLATORS: This label is cited in an error message. */
	item = gtk_menu_item_new_with_label(_("Add New Bookmark"));
	g_signal_connect(item, "activate",
			 G_CALLBACK(bookmarks_add), filer_window);
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 0, 1, 1, 2);
	gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label(_("Edit Bookmarks"));
	g_signal_connect(item, "activate", G_CALLBACK(activate_edit), NULL);
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 0, 1, 2, 3);

	/* TRANSLATORS: "Recently Visited" is cited by the toolbar "Go" tooltip */
	item = gtk_menu_item_new_with_mnemonic(_("_Recently Visited"));
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 1, 2, 1, 2);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			build_history_menu(filer_window));

	/* [20260913094148] Numbered-group slots (Groups.xml) */
	item = gtk_menu_item_new_with_mnemonic(_("_Numbered Groups"));
	gtk_widget_show(item);
	gtk_menu_attach(GTK_MENU(menu), item, 1, 2, 2, 3);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			build_groups_menu(filer_window));

	/* Now add all the bookmarks to the menu */

	update_bookmarks();

	node = xmlDocGetRootElement(bookmarks->doc);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		gchar *mark, *title, *path, *dirname;
		GtkWidget *label, *fix;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, BAD_CAST "bookmark") != 0)
			continue;

		mark = (gchar *) xmlNodeListGetString(bookmarks->doc,
					    node->xmlChildrenNode, 1);
		if (!mark)
			continue;

		path=expand_path(mark);

		title = (gchar *) xmlGetProp(node, BAD_CAST "title");
		if(!title)
			title=mark;

		item = gtk_menu_item_new();

		g_object_set_data(G_OBJECT(item), "bookmark-path", path);
		g_signal_connect(item, "destroy",
				 G_CALLBACK(free_path_for_item), path);

		g_signal_connect(item, "activate",
				G_CALLBACK(bookmarks_activate),
				filer_window);

		if (need_separator)
		{
			GtkWidget *sep;
			sep = gtk_separator_menu_item_new();
			gtk_widget_show(sep);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
			need_separator = FALSE;
		}

		fix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		BItem *bi = g_new0(BItem, 1);
		bi->fix = fix;
		bi->path = g_strdup(path);

		/* Placeholder image */
		bi->icon = gtk_image_new();
		gtk_widget_set_size_request(bi->icon, iconw, iconw);
		gtk_box_pack_start(GTK_BOX(fix), bi->icon, FALSE, FALSE, 0);
		gtk_size_group_add_widget(icon_group, bi->icon);

		items = g_list_append(items, bi);

		label =  gtk_label_new_with_mnemonic(title);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_halign(label, GTK_ALIGN_START);

		gtk_size_group_add_widget(title_group, label);
		gtk_box_pack_start(GTK_BOX(fix), label, FALSE, TRUE, 0);

		dirname = g_path_get_dirname(mark);
		label = gtk_label_new(dirname);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_halign(label, GTK_ALIGN_START);
		bi->lbl = label;

		{
			GtkStyleContext *ctx = gtk_widget_get_style_context(label);
			GdkRGBA rgba;
			gtk_style_context_get_color(ctx, GTK_STATE_FLAG_INSENSITIVE, &rgba);
			widget_set_fg_color(label, &rgba);
		}

		gtk_label_set_max_width_chars(GTK_LABEL(label), 30);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);

		gtk_size_group_add_widget(dirname_group, label);
		gtk_box_pack_start(GTK_BOX(fix), label, TRUE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(item), fix);
		gtk_menu_attach(GTK_MENU(menu), item, 0, 2, count, count + 1);

		gtk_widget_show_all(item);

		count++;

		if(title!=mark)
			xmlFree(title);
		xmlFree(mark);

		g_free(dirname);
	}

	items = g_list_concat(items, itemshist);
	itemshist = NULL;
	makeicons();

	return menu;
}
