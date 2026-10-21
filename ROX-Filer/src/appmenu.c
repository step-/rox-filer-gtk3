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

/* appmenu.c - handles application-specific menus read from XMLwrapper.xml */

/* XXX: This handles all File menu extensions. Needs renaming! */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <libxml/parser.h>

#include "global.h"

#include "choices.h"
#include "fscache.h"
#include "gui_support.h"
#include "support.h"
#include "main.h"
#include "menu.h"
#include "filer.h"
#include "appmenu.h"
#include "dir.h"
#include "type.h"
#include "appinfo.h"
#include "xml.h"
#include "run.h"
#include "diritem.h"
#include "action.h"
#include "options.h"
#include "pixmaps.h"

/* Static prototypes */
static void apprun_menu(GtkWidget *item, gpointer data);
static GtkWidget *create_menu_item(xmlNode *node, const char *app_dir);
static GtkWidget *appmenu_add_submenu(xmlNode *subm_node, const gchar *app_dir);
static void show_app_help(GtkWidget *item, gpointer data);
static void build_menu_for_type(GList *paths, MIME_type *type, GList **items, gboolean track);
static void show_sendto_tree(GtkWidget *w);

/* Show a SendTo item and its whole subtree, except the submenus themselves:
 * menu_from_dir() never shows what it builds, so submenu * children would stay
 * hidden in popups shown without show_all (pinboard, panel) [20260921205103].
 */
static void show_sendto_tree(GtkWidget *w)
{
	GtkWidget *sub;

	gtk_widget_show(w);
	if (!GTK_IS_MENU_ITEM(w))
	{
		return;
	}
	sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));
	if (!sub)
	{
		return;
	}
	gtk_container_foreach(GTK_CONTAINER(sub),
			(GtkCallback) show_sendto_tree, NULL);
}

/* Static prototypes (continued) */
static gboolean build_app_menu(const char *app_dir, DirItem *app_item, GList **items);
static void mnt_eject(GtkWidget *item, gpointer data);

/* There can only be one menu open at a time... we store: */
static GtkWidget *current_menu = NULL;	/* The GtkMenu */
static gchar *current_app_path = NULL;	/* The path of the application */
static GList *current_items = NULL;	/* The GtkMenuItems we added directly
					 * to it --- not submenu items.
					 */
/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Removes all appmenu menu items */
void appmenu_remove(void)
{
	GList *next;

	if (!current_menu)
		return;

	for (next = current_items; next; next = next->next)
		gtk_widget_destroy((GtkWidget *) next->data);

	null_g_free(&current_app_path);
	current_menu = NULL;

	g_list_free(current_items);
	current_items = NULL;
}

static gboolean menu_has_items(GtkWidget *menu)
{
	GList	*children = gtk_container_get_children(GTK_CONTAINER(menu));
	gboolean has = children != NULL;

	g_list_free(children);
	return has;
}

/* Add AppMenu entries to 'menu', if appropriate.
 * 'app_dir' is the pathname of the application directory, and 'item'
 * is the corresponding DirItem.
 * Used for the pinboard/panel icon menu: prepends the AppMenu entries
 * flat and a "Send To..." submenu to the top of 'menu'.
 * Call appmenu_remove() to undo the effect.
 */
void appmenu_add(FilerWindow *slctwin, const gchar *app_dir, DirItem *app_item, GtkWidget *menu)
{
	GList		*items = NULL, *sendto_items = NULL, *next, *rev;
	GtkWidget	*sendto_menu, *sendto_item;
	gboolean	 is_app = FALSE;

	g_return_if_fail(menu != NULL);

	/* Should have called appmenu_remove() already... */
	g_return_if_fail(current_menu == NULL);
	g_return_if_fail(current_items == NULL);

	if (!slctwin)
		is_app = build_app_menu(app_dir, app_item, &items);

	if (slctwin || !is_app)
	{
		GList *paths =    slctwin ? filer_selected_items(slctwin) :
			g_list_append(NULL, g_strdup(app_dir));
		MIME_type *type = slctwin ? menu_selection_type(slctwin) :
			app_item->mime_type;
		build_menu_for_type(paths, type, &sendto_items, FALSE);
	}

	if (sendto_items)
	{
		/* Dedicated "Send To..." submenu [20260923205153].
		 * menu_start_detached() leaves the parent item unparented.
		 * Destroying the parent cascades to the submenu children. */
		sendto_menu = menu_start_detached(N_("_Send To..."), menu,
				&sendto_item);
		for (next = sendto_items; next; next = next->next)
			gtk_menu_shell_append(GTK_MENU_SHELL(sendto_menu),
					GTK_WIDGET(next->data));
		g_list_free(sendto_items);
		show_sendto_tree(sendto_item);
		items = g_list_append(items, sendto_item);
		current_items = g_list_prepend(current_items, sendto_item);
	}

	if (app_item && app_item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		GtkWidget *item = gtk_menu_item_new_with_label(_("Eject"));
		gtk_widget_show(item);
		g_signal_connect(item, "activate", G_CALLBACK(mnt_eject), NULL);
		items = g_list_append(items, item);
		current_items = g_list_prepend(current_items, item);
	}

	if (items)
	{
		GtkWidget *sep = gtk_separator_menu_item_new();
		gtk_widget_show(sep);
		items = g_list_append(items, sep);
		current_items = g_list_prepend(current_items, sep);
	}

	/* Prepend the group to the top of 'menu', preserving order.
	 * g_list_reverse() mutates 'items' in place and returns the new head,
	 * so 'rev' and 'items' share the same nodes: free 'rev' only.
	 */
	rev = g_list_reverse(items);
	for (next = rev; next; next = next->next)
		gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), GTK_WIDGET(next->data));
	g_list_free(rev);

	current_menu = menu;
	current_app_path = g_strdup(app_dir);
}

/* Populate the given File menu instance's "App..." and "SendTo..." submenus.
 * For mount points, also inserts a flat "Eject" item above Delete.
 * Call appmenu_remove() to undo the effect.
 */
void appmenu_add_submenus(FilerWindow *slctwin, const gchar *app_dir, DirItem *app_item, FileMenu *fm)
{
	GtkWidget	*app_menu = fm->app_menu;
	GtkWidget	*sendto_menu = fm->sendto_menu;
	GtkWidget	*menu = fm->menu;
	GList		*app_items = NULL, *sendto_items = NULL, *next;
	gboolean	 is_app = FALSE;

	g_return_if_fail(fm != NULL);
	g_return_if_fail(menu != NULL);

	/* Should have called appmenu_remove() already... */
	g_return_if_fail(current_menu == NULL);
	g_return_if_fail(current_items == NULL);

	if (!slctwin)
		is_app = build_app_menu(app_dir, app_item, &app_items);

	if (slctwin || !is_app)
	{
		GList *paths =    slctwin ? filer_selected_items(slctwin) :
			g_list_append(NULL, g_strdup(app_dir));
		MIME_type *type = slctwin ? menu_selection_type(slctwin) :
			app_item->mime_type;
		build_menu_for_type(paths, type, &sendto_items, TRUE);
	}

	for (next = app_items; next; next = next->next)
		gtk_menu_shell_append(GTK_MENU_SHELL(app_menu), GTK_WIDGET(next->data));
	for (next = sendto_items; next; next = next->next)
		gtk_menu_shell_append(GTK_MENU_SHELL(sendto_menu), GTK_WIDGET(next->data));

	g_list_free(app_items);
	g_list_free(sendto_items);

	gtk_widget_set_sensitive(fm->app_item, menu_has_items(app_menu));
	gtk_widget_set_sensitive(fm->sendto_item, menu_has_items(sendto_menu));

	if (app_item && app_item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		GtkWidget *item = gtk_menu_item_new_with_label(_("Eject"));
		gtk_widget_show(item);
		g_signal_connect(item, "activate", G_CALLBACK(mnt_eject), NULL);
		gtk_menu_shell_insert(GTK_MENU_SHELL(menu), item, FILE_MENU_DELETE);
		current_items = g_list_prepend(current_items, item);
	}

	current_menu = menu;
	current_app_path = g_strdup(app_dir);
}


/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* Create a new menu and return it */
static GtkWidget *appmenu_add_submenu(xmlNode *subm_node, const gchar *app_dir)
{
	xmlNode	*node;
	GtkWidget *sub_menu;

        /* Create the new submenu */
	sub_menu = gtk_menu_new();

	/* Add the menu entries */
	for (node = subm_node->xmlChildrenNode; node; node = node->next)
	{
		GtkWidget *item;

		item = create_menu_item(node, app_dir);
		if (item)
			gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), item);
	}

	return sub_menu;
}

/* Create and return a menu item */
static GtkWidget *create_menu_item(xmlNode *node, const gchar *app_dir)
{
	GtkWidget *item;
	xmlNode *label_node;
	gchar	*label, *option = NULL;
	gchar	*icon_name = NULL;
	gboolean is_submenu;
	gint	size;

	if (node->type != XML_ELEMENT_NODE)
		return NULL;

	if (xmlStrcmp(node->name, BAD_CAST "Item") == 0)
	{
		is_submenu = FALSE;
		option = (gchar *) xmlGetProp(node, BAD_CAST "option");
	}
	else if (xmlStrcmp(node->name, BAD_CAST "AppMenu") == 0)
		is_submenu = TRUE;
	else
		return NULL;

	/* Create the item */
	label_node = get_subnode(node, NULL, BAD_CAST "Label");
	if (label_node)
	{
		label = (gchar *) xmlNodeListGetString(label_node->doc,
					label_node->xmlChildrenNode, 1);
	}
	else
	{
		label = (gchar *) xmlGetProp(node, BAD_CAST "label");
		if (!label)
			label = g_strdup(_("<missing label>"));
	}
	item = NULL;

	icon_name = (gchar *) xmlGetProp(node, BAD_CAST "icon");

	size = get_menu_icon_pixels();

	if (icon_name && size > 0)
	{
		GtkWidget *icon = NULL;

		/* ".DirIcon" renders AppDir/.DirIcon directly */
		if (strcmp(icon_name, ".DirIcon") == 0)
		{
			const gchar *base = app_dir ? app_dir : current_app_path;

			if (base)
			{
				gchar *path;
				GdkPixbuf *raw;

				path = g_build_filename(base, ".DirIcon", NULL);
				raw = gdk_pixbuf_new_from_file(path, NULL);
				if (raw)
				{
					GdkPixbuf *scaled;

					scaled = scale_pixbuf(raw, size, size);
					icon = gtk_image_new_from_pixbuf(scaled);
					g_object_unref(scaled);
					g_object_unref(raw);
				}
				g_free(path);
			}
		}
		else
		{
			GdkPixbuf *pixbuf;

			pixbuf = theme_load_icon(icon_name, size, 0, NULL);
			if (pixbuf)
			{
				icon = gtk_image_new_from_pixbuf(pixbuf);
				g_object_unref(pixbuf);
			}
			else
			{
				/* [20260908052713] Allow only _this_ deprecated use:
				 * gtk_stock_lookup, gtk_image_new_from_stock */
				G_GNUC_BEGIN_IGNORE_DEPRECATIONS;

				GtkIconTheme *theme;
				GtkStockItem stock;

				theme = gtk_icon_theme_get_default();
				if (theme && gtk_icon_theme_has_icon(theme,
							icon_name))
				{
					icon = menu_icon_image(icon_name);
				}
				else if (gtk_stock_lookup(icon_name, &stock))
				{
					/* [20260929195006]: ->[20260830082750]
					 * GTK3's discrete stock sizes match our
					 * (16,23,32) menu-size tiers exactly.
					 */
					GtkIconSize stock_size =
						GTK_ICON_SIZE_MENU;

					if (size >= 32)
					{
						stock_size =
							GTK_ICON_SIZE_DIALOG;
					}
					else if (size > 16)
					{
						stock_size =
						GTK_ICON_SIZE_LARGE_TOOLBAR;
					}
					icon = gtk_image_new_from_stock(
							icon_name,
							stock_size);
				}
				else
				{
					icon = menu_icon_image("image-missing");
				}

				G_GNUC_END_IGNORE_DEPRECATIONS;
			}
		}
		g_free(icon_name);
		if (icon)
		{
			item = menu_item_new_with_image(label, icon);
		}
	}

	if (!item)
	{
		item = gtk_menu_item_new_with_label(label);
	}

	gtk_widget_set_accel_path(item, NULL, NULL);	/* XXX */

	g_free(label);

	if (is_submenu)
	{
		/* Add submenu items */

		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
					  appmenu_add_submenu(node, app_dir));
	}
	else
	{
		/* Set up callback */

		if (option)
		{
			g_object_set_data_full(G_OBJECT(item), "option",
					g_strdup(option),
					g_free);
			g_free(option);
		}

		g_signal_connect(item, "activate", G_CALLBACK(apprun_menu),
				NULL);
	}

	gtk_widget_show(item);

	return item;
}

/* Function called to execute an AppMenu item */
static void apprun_menu(GtkWidget *item, gpointer data)
{
	gchar	*option;
	gchar *argv[3];

	g_return_if_fail(current_app_path != NULL);

	option = g_object_get_data(G_OBJECT(item), "option");

	argv[0] = g_strconcat(current_app_path, "/AppRun", NULL);
	argv[1] = option;	/* (may be NULL) */
	argv[2] = NULL;

	rox_spawn(NULL, (const gchar **) argv);

	g_free(argv[0]);
}

static void show_app_help(GtkWidget *item, gpointer data)
{
	g_return_if_fail(current_app_path != NULL);

	show_help_files(current_app_path);
}

static void mnt_eject(GtkWidget *item, gpointer data)
{
	GList *dirs;

	g_return_if_fail(current_app_path != NULL);
	dirs = g_list_prepend(NULL, current_app_path);
	action_eject(dirs);
	g_list_free(dirs);
}

/* When 'track' is FALSE the built widgets are left out of current_items.
 * The caller owns a parent that cascades destroy (see appmenu_add).
 */
static void build_menu_for_type(GList *paths, MIME_type *type, GList **items, gboolean track)
{
	GtkWidget *item;
	GList *widgets, *next;

	if (!paths)
		return;

	widgets = menu_sendto_for_type(paths, type, TRUE);
	for (next = widgets; next; next = next->next)
	{
		GtkWidget *w = GTK_WIDGET(next->data);
		show_sendto_tree(w);
		*items = g_list_append(*items, w);
		if (track)
		{
			current_items = g_list_prepend(current_items, w);
		}
	}
	g_list_free(widgets);

	/* Single scope-aware Customise entry; the scope outlives the
	 * menu_selection_type() static via the closure [20260911082953]. */
	item = menu_placeholder_item(_("Customise..."));
	gtk_widget_show(item);
	*items = g_list_append(*items, item);
	if (track)
	{
		current_items = g_list_prepend(current_items, item);
	}
	g_signal_connect_data(item, "activate",
			G_CALLBACK(customise_send_to_item),
			make_sendto_scope(type),
			(GClosureNotify) sendto_scope_free, 0);
}

static inline gboolean is_dir(const char *dir)
{
	struct stat info;
	return stat(dir, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Adds to current_items */
static gboolean build_app_menu(const char *app_dir, DirItem *app_item, GList **items)
{
	XMLwrapper	*ai = NULL;
	xmlNode		*node;
	GtkWidget	*item;
	char		*help_dir;

	ai = appinfo_get(app_dir, app_item);
	if (ai)
	{
		node = xml_get_section(ai, NULL, "AppMenu");
		if (node)
			node = node->xmlChildrenNode;
	}
	else
	{
		if (app_item->flags & ITEM_FLAG_APPDIR)
			node = NULL;
		else
		{
			/* Not an application AND no AppInfo */
			return FALSE;
		}
	}

	/* Add the menu entries */
	for (; node; node = node->next)
	{
		item = create_menu_item(node, app_dir);

		if (item)
		{
			*items = g_list_append(*items, item);
			current_items = g_list_prepend(current_items, item);
		}
	}

	help_dir = g_build_filename(app_dir, "Help", NULL);
	if (is_dir(help_dir))
	{
		item = menu_item_new_named_image(_("Help"), "help-browser");
		gtk_widget_show(item);
		*items = g_list_append(*items, item);
		current_items = g_list_prepend(current_items, item);
		g_signal_connect(item, "activate", G_CALLBACK(show_app_help), NULL);
		gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(item))), _("Help"));
	}
	g_free(help_dir);

	if (ai)
		g_object_unref(ai);

	return TRUE;
}
