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

/* menu.c - code for handling the popup menus */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>

#include "global.h"

#include "main.h"
#include "menu.h"
#include "run.h"
#include "action.h"
#include "filer.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "gtksavebox.h"
#include "mount.h"
#include "minibuffer.h"
#include "i18n.h"
#include "pinboard.h"
#include "dir.h"
#include "diritem.h"
#include "appmenu.h"
#include "usericons.h"
#include "infobox.h"
#include "view_iface.h"
#include "icon.h"
#include "display.h"
#include "bookmarks.h"
#include "panel.h"
#include "bulk_rename.h"
#include "xtypes.h"
#include "log.h"
#include "dnd.h"

typedef enum {
	FILE_DUPLICATE_ITEM,
	FILE_RENAME_ITEM,
	FILE_LINK_ITEM,
	FILE_OPEN_FILE,
	FILE_PROPERTIES,
	FILE_RUN_ACTION,
	FILE_SET_ICON,
	FILE_DELETE,
	FILE_USAGE,
	FILE_CHMOD_ITEMS,
	FILE_FIND,
	FILE_SET_TYPE,
	FILE_COPY_TO_CLIPBOARD,
	FILE_CUT_TO_CLIPBOARD,
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	FILE_XATTRS,
#endif
} FileOp;

typedef void (*ActionFn)(GList *paths,
			 const char *dest_dir, const char *leaf, int quiet);

typedef gboolean (*SaveCb)(GObject *savebox,
			   const gchar *current, const gchar *new);

GtkAccelGroup	*filer_keys = NULL;

static GtkWidget *popup_menu = NULL;	/* Currently open menu */

static gint updating_menu = 0;		/* Non-zero => ignore activations */
static GList *send_to_paths = NULL;

static Option o_menu_iconsize, o_menu_xterm, o_menu_quick;

/* clipboard targets */
static const GtkTargetEntry clipboard_targets[] = {
	{"text/uri-list", 0, TARGET_URI_LIST},
	{"x-special/gnome-copied-files", 0, TARGET_GNOME_COPIED_FILES},
};
static GtkClipboard *clipboard;
static const char *clipboard_action = NULL;
static GList *selected_paths = NULL;

/* Shortcuts editor data */
typedef struct {
	guint           accel_key;
	GdkModifierType accel_mods;
} AccelSnapshot;

static GHashTable *shortcuts_snapshot = NULL;
static GtkWidget  *shortcuts_dialog  = NULL;
static GtkTreeStore *shortcuts_store = NULL;

/* Whether a system/user menus2 file was loaded at startup or saved
since. Reset/Revert save immediately only when TRUE, to avoid saving
a pristine setup as a significant change [20260916082846]. */
static gboolean menus_file_existed = FALSE;

/* Static prototypes */

static void save_menus(void);
static void menu_closed(GtkWidget *widget);
static void menu_iconsize_changed(void);
static void shade_file_menu_items(gboolean shaded);
static void savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action);
static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data);
static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new);
static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path);
static gboolean rename_cb(GObject *savebox,
			const gchar *initial, const gchar *path);
static void select_nth_item(GtkMenuShell *shell, int n);
static void menu_seed_move_current(GtkWidget *widget,
		GtkMenuDirectionType dir, gpointer data);
static void new_file_type(gchar *templ);
static void do_send_to(gchar *templ);
static void show_send_to_menu(FilerWindow *fw, GdkEvent *event);
static void show_dir_send_to_menu(GdkEvent *event);
static GList *set_keys_button(Option *option, xmlNode *node, gchar *label);
static void   shortcuts_dialog_show(void);
static void   shortcuts_dialog_destroyed(GtkWidget *widget, gpointer data);
static void   revert_shortcuts(GtkWidget *button, gpointer data);
static void   reset_to_factory(GtkWidget *button, gpointer data);
static gboolean clear_leaf_shortcut(GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter *iter, gpointer data);
static void   snapshot_take(void);
static void   snapshot_free(void);
static void   snapshot_restore_entry(gpointer key, gpointer value, gpointer data);
static void   populate_from_accel_map(void);
static void   accel_foreach_cb(gpointer data, const gchar *accel_path,
                               guint accel_key, GdkModifierType accel_mods,
                               gboolean changed);
static void   walk_menu_items(GtkWidget *menu, GtkTreeIter *parent,
                              GtkTreeStore *store);

/* Note that for most of these callbacks none of the arguments are used. */

static void view_type(gpointer data, guint action, GtkWidget *widget);

/* (action used in these three - DetailsType) */
static void change_size(gpointer data, guint action, GtkWidget *widget);
static void change_size_auto(gpointer data, guint action, GtkWidget *widget);
static void set_with(gpointer data, guint action, GtkWidget *widget);

static void set_sort(gpointer data, guint action, GtkWidget *widget);
static void reverse_sort(gpointer data, guint action, GtkWidget *widget);

static void filter_directories(gpointer data, guint action, GtkWidget *widget);
static void hidden(gpointer data, guint action, GtkWidget *widget);
static void only_dirs(gpointer data, guint action, GtkWidget *widget);
static void show_thumbs(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);
static void refresh_thumbs(gpointer data, guint action, GtkWidget *widget);
static void save_settings(gpointer data, guint action, GtkWidget *widget);
static void save_settings_parent(gpointer data, guint action, GtkWidget *widget);

static void file_op(gpointer data, FileOp action, GtkWidget *widget);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void invert_selection(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void new_file(gpointer data, guint action, GtkWidget *widget);
static void customise_new(gpointer data);
static GList *add_sendto_shared(GtkWidget *menu,
		const gchar *type, const gchar *subtype, CallbackFn swapped_func);
static void customise_directory_menu(gpointer data);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);

static void open_parent_same(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);
static void home_directory(gpointer data, guint action, GtkWidget *widget);
static void show_bookmarks(gpointer data, guint action, GtkWidget *widget);
static void show_log(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
/* static void new_user(gpointer data, guint action, GtkWidget *widget); */
static void close_window(gpointer data, guint action, GtkWidget *widget);
static void follow_symlinks(gpointer data, guint action, GtkWidget *widget);

/* (action used in this - MiniType) */
static void mini_buffer(gpointer data, guint action, GtkWidget *widget);
static void resize(gpointer data, guint action, GtkWidget *widget);

/* clipboard */
static void clipboard_get(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data);
static void clipboard_clear(GtkClipboard *clipboard, gpointer user_data);
static void paste_from_clipboard(gpointer data, guint action, GtkWidget *widget);

#define MENUS_NAME "menus2"

static GtkWidget	*filer_menu = NULL;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */

/* The "Pinboard" submenu of the filer menu and the two items it holds. */
static GtkWidget	*filer_pinboard_menu;
static GtkWidget	*filer_pinboard_backdrop_item;
static GtkWidget	*filer_pinboard_panel_item;

/*
 * The two identical File menus: the "File" submenu nested in the ROX menu and
 * the standalone right-click popup. Built together by
 * build_file_menu_contents(); per-popup state updates must be applied to both.
 */
static FileMenu		file_submenu;
static FileMenu		file_popup;

static GtkWidget	*filer_auto_size_menu;	/* The Automatic item */
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*filer_files_only_menu;
static GtkWidget	*filer_dirs_only_menu;
static GtkWidget	*filer_filter_dirs_menu;/* The Filter Dirs item */

/* The Sort items */
static GtkWidget	*filer_sort_name_menu;
static GtkWidget	*filer_sort_type_menu;
static GtkWidget	*filer_sort_date_a_menu;
static GtkWidget	*filer_sort_date_c_menu;
static GtkWidget	*filer_sort_date_m_menu;
static GtkWidget	*filer_sort_size_menu;
static GtkWidget	*filer_sort_perm_menu;
static GtkWidget	*filer_sort_owner_menu;
static GtkWidget	*filer_sort_group_menu;

static GtkWidget	*filer_reverse_menu;	/* The Reversed item */
static GtkWidget	*filer_thumb_menu;	/* The Show Thumbs item */
static GtkWidget    *filer_new_window;	/* The New Window item */
static GtkWidget    *filer_new_menu;        /* The New submenu */
static GtkWidget    *filer_follow_sym;      /* Follow symbolic links item */

/*
 * Connect 'item's activate callback and accelerator path, then append it to
 * 'menu'.
 */
static void item_common(GtkWidget *menu, GtkWidget *item,
			const gchar *label, MenuCB cb, guint action)
{
	if (cb)
		g_signal_connect(item, "activate",
				G_CALLBACK(cb),
				GUINT_TO_POINTER(action));

	char *accel = g_strconcat(gtk_menu_get_accel_path(GTK_MENU(menu)), label, NULL);
	gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), accel);
	g_free(accel);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

#define adi menu_add_item
GtkWidget *menu_add_item(GtkWidget *menu, gchar *label, MenuCB cb, guint action)
{
	GtkWidget *item = gtk_menu_item_new_with_label(_(label));

	item_common(menu, item, label, cb, action);

	return item;
}

/* Strip mnemonic markers from a msgid. The stripped string is used to
 * build accelerator paths. Paths are built from the untranslated msgid only.
 * Stripping every '_' is right for the closed set of labels this file owns.
 * Not to be applied to AppMenu XML or panel strings.
 * Used by menu_add_submenu() and menu_start_full(). [20261001070501]
 */
static void strip_mnemonic(const gchar *label, GString *out)
{
	const gchar *p;

	for (p = label; *p; p++)
	{
		if (*p != '_')
			g_string_append_c(out, *p);
	}
}

/* Create a submenu parent item with a mnemonic. 'label' carries the
 * underscore marker (and is the msgid); the accel path uses the
 * stripped form. [20261001070501]
 */
static GtkWidget *menu_add_submenu(GtkWidget *menu, gchar *label)
{
	GtkWidget *item;
	GString *plain;

	item = gtk_menu_item_new_with_mnemonic(_(label));

	plain = g_string_new(NULL);
	strip_mnemonic(label, plain);
	item_common(menu, item, plain->str, NULL, 0);
	g_string_free(plain, TRUE);

	return item;
}
/*
 * Load a named menu icon at an explicit pixel size.
 *
 * This deliberately goes through the default theme, the same one that
 * gtk_image_new_from_icon_name() uses, because pixmaps_init() appends
 * app_dir to that theme's search path so the bundled rox-* icons resolve
 * there. The helper theme_load_icon() uses a different theme object that
 * never receives that path, so it cannot see them.
 *
 * Asked for an explicit size, the theme returns its nearest match.
 */
static GtkWidget *menu_named_icon(const char *icon_name, gint pixels)
{
	GtkIconTheme	*theme = gtk_icon_theme_get_default();
	GdkPixbuf	*pixbuf;
	GtkWidget	*image;

	if (!theme)
		return NULL;

	pixbuf = gtk_icon_theme_load_icon(theme, icon_name, pixels, 0, NULL);
	if (!pixbuf)
		return NULL;

	image = gtk_image_new_from_pixbuf(pixbuf);
	g_object_unref(pixbuf);

	return image;
}

/* Named icon at the configured size. NULL when the caller has no name or
 * icons are switched off, so that callers can build a label-only row
 * instead of an item holding a NULL image.
 */
GtkWidget *menu_icon_image(const char *icon_name)
{
	GtkWidget	*image;

	if (!icon_name || get_menu_icon_pixels() <= 0)
		return NULL;

	image = menu_named_icon(icon_name, get_menu_icon_pixels());
	if (!image)
		image = gtk_image_new_from_icon_name(icon_name,
				GTK_ICON_SIZE_MENU);

	return image;
}

/* Row carrying a named icon at the configured size, or a label-only
 * row when there is no name to show or no icon to show it with. Every
 * menu row that takes its icon from the icon-size option goes through
 * here or through menu_add_stock().
 *
 * 'label' is translated by the caller.
 */
GtkWidget *menu_item_new_named_image(const char *label, const char *icon_name)
{
	GtkWidget	*image = menu_icon_image(icon_name);
	GtkWidget	*item;

	if (image)
		item = menu_item_new_with_image(label, image);
	else
		item = gtk_menu_item_new_with_label(label);

	/* Carry the name for in-place re-rendering when the icon size
	 * option setting changes. Rows without a name (label-only,
	 * separators, submenu parents, check/radio rows) carry nothing
	 * and will be skipped [20260925122040].
	 */
	if (icon_name)
	{
		g_object_set_data_full(G_OBJECT(item), "rox-icon-name",
				g_strdup(icon_name), g_free);
	}

	return item;
}

/* As above, but '_' in the label marks a mnemonic. Only for labels
 * that carry mnemonics; all other callers stay with the literal form.
 * [20261001070501]
 */
GtkWidget *menu_item_new_named_image_mnemonic(const char *label,
		const char *icon_name)
{
	GtkWidget	*image = menu_icon_image(icon_name);
	GtkWidget	*item;

	if (image)
		item = menu_item_new_with_image_mnemonic(label, image);
	else
		item = gtk_menu_item_new_with_mnemonic(label);

	if (icon_name)
	{
		g_object_set_data_full(G_OBJECT(item), "rox-icon-name",
				g_strdup(icon_name), g_free);
	}

	return item;
}
#define ads menu_add_stock
GtkWidget *menu_add_stock(GtkWidget *menu,
		gchar *label, MenuCB cb, guint action, const gchar *icon_name)
{
	GtkWidget	*item = menu_item_new_named_image(_(label), icon_name);

	item_common(menu, item, label, cb, action);

	return item;
}

/* Temporary placeholder for rows with no icon of their own yet. [20260925081702] */
#define adim(menu, label, cb, action) \
	menu_add_stock(menu, label, cb, action, "image-missing")
/* Same placeholder, for rows that wire their own signal. */
GtkWidget *menu_placeholder_item(const char *label)
{
	return menu_item_new_named_image(label, "image-missing");
}
static GtkWidget *adt(GtkWidget *menu, gchar *label, MenuCB cb, guint action, GtkWidget **w)
{
	GtkWidget *item = gtk_check_menu_item_new_with_label(_(label));

	*w = item;
	item_common(menu, item, label, cb, action);

	return item;
}
static GSList *adr(GtkWidget *menu,
		const gchar *label, MenuCB cb, guint action, GSList *group, GtkWidget **w)
{
	GtkWidget *item = gtk_radio_menu_item_new_with_label(group, _(label));

	*w = item;
	item_common(menu, item, label, cb, action);

	return gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
}

#define add_separator menu_add_separator
void menu_add_separator(GtkWidget *menu)
{ gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new()); }

static void sta(GtkWidget *item, guint accel_key, GdkModifierType accel_mods)
{
	gtk_accel_map_add_entry(
			gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item)),
			accel_key,
			accel_mods);
}

/*
 * Create a new menu. With a non-NULL parent, a "label" item is added to the
 * parent and the new menu becomes its submenu. Returns the new menu; if
 * item_out is non-NULL it receives the parent menu item (NULL when parent is
 * NULL).
 */
static GtkWidget *menu_start_full(gchar *label, GtkWidget *parent,
		GtkWidget **item_out, gboolean attach)
{
	const gchar *path = "";
	GtkWidget *new_menu = gtk_menu_new();

	if (item_out)
		*item_out = NULL;

	if (parent)
	{
		GtkWidget *parent_item;

		path = gtk_menu_get_accel_path(GTK_MENU(parent));
		if (attach)
		{
			parent_item = menu_add_submenu(parent, label);
		}
		else
		{
			GString *plain = g_string_new(NULL);
			gchar *accel;

			/* Accel path uses the stripped msgid. [20261001070501]
			 */
			strip_mnemonic(label, plain);
			accel = g_strconcat(path, plain->str, NULL);

			parent_item = gtk_menu_item_new_with_mnemonic(_(label));
			gtk_menu_item_set_accel_path(GTK_MENU_ITEM(parent_item),
					accel);
			g_free(accel);
			g_string_free(plain, TRUE);
		}
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent_item), new_menu);
		gtk_menu_set_accel_group(GTK_MENU(new_menu),
				gtk_menu_get_accel_group(GTK_MENU(parent)));

		if (item_out)
			*item_out = parent_item;
	}

	if (label)
	{
		GString *plain = g_string_new(NULL);
		gchar *accel;

		/* Accel path uses the stripped msgid. [20261001070501] */
		strip_mnemonic(label, plain);
		accel = g_strconcat(path, plain->str, "/", NULL);
		gtk_menu_set_accel_path(GTK_MENU(new_menu), accel);
		g_free(accel);
		g_string_free(plain, TRUE);
	}

	return new_menu;
}

#define start_menu menu_start
GtkWidget *menu_start(gchar *label, GtkWidget *parent, GtkWidget **item_out)
{
	return menu_start_full(label, parent, item_out, TRUE);
}

/*
 * As menu_start(), but the parent item is left unparented so the caller can
 * place it itself. Removing an already-attached item is not an option because
 * gtk_widget_unparent() would drop the parent's ref, which finalizes it.
 */
GtkWidget *menu_start_detached(gchar *label, GtkWidget *parent,
		GtkWidget **item_out)
{
	return menu_start_full(label, parent, item_out, FALSE);
}

void menu_init(void)
{
	char *menurc = choices_find_xdg_path_load(MENUS_NAME, project_name, SITE);
	if (menurc)
	{
		menus_file_existed = TRUE;
		gtk_accel_map_load(menurc);
		g_free(menurc);
	}

	option_add_string(&o_menu_xterm, "menu_xterm", "xterm");
	option_add_int(&o_menu_iconsize, "menu_iconsize", MIS_SMALL);
	option_add_int(&o_menu_quick, "menu_quick", TRUE);
	option_add_saver(save_menus);
	option_add_notify(menu_iconsize_changed);

	option_register_widget("menu-set-keys", set_keys_button);

	filer_keys = gtk_accel_group_new();

	clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
}

MenuIconStyle get_menu_icon_style(void)
{
	MenuIconStyle mis;
	int display;

	mis = o_menu_iconsize.int_value;

	switch (mis)
	{
		case MIS_NONE: case MIS_SMALL: case MIS_LARGE:
			return mis;
		default:
			break;
	}

	if (mis == MIS_CURRENT && window_with_focus)
	{
		switch (window_with_focus->display_style)
		{
			case HUGE_ICONS:
			case LARGE_ICONS:
				return MIS_LARGE;
			case SMALL_ICONS:
				return MIS_SMALL;
			default:
				break;
		}
	}

	display = o_display_size.int_value;
	switch (display)
	{
		case HUGE_ICONS:
		case LARGE_ICONS:
			return MIS_LARGE;
		case SMALL_ICONS:
			return MIS_SMALL;
		default:
			break;
	}

	return MIS_SMALL;
}

/* Menu icon sizes. Each is produced by scaling the full-size image.
 * Note the icons in ROX-Filer/ROX/MIME are bitmaps of assorted and
 * often modest size, so 16 and 24 downscale cleanly from all of them
 * while 32 softens some. The theme's own scalable icons are only reached
 * once ROX's built-ins stop taking precedence [20260830082750]. */
/* The (16,24,32) tiers math GTK3's discrete stock sizes ->[20260929195006]. */
#define MENU_ICON_PIXELS_SMALL 16
#define MENU_ICON_PIXELS_LARGE 24
#define MENU_ICON_PIXELS_HUGE 32

/* Resolve the icon size setting to a pixel count. Returns 0 for "no icons";
 * callers rely on 0 meaning "draw no icon at all".
 *
 * MIS_CURRENT and MIS_DEFAULT are withdrawn: making menu icon size track the
 * icon view size is not how menus behave elsewhere, and the "same as default"
 * setting has been silently resolving to the smallest size since 2002.
 */
gint get_menu_icon_pixels(void)
{
	MenuIconStyle mis = o_menu_iconsize.int_value;

	switch (mis)
	{
		case MIS_NONE:
			return 0;
		case MIS_SMALL:
			return MENU_ICON_PIXELS_SMALL;
		case MIS_LARGE:
			return MENU_ICON_PIXELS_LARGE;
		case MIS_HUGE:
			return MENU_ICON_PIXELS_HUGE;
		case MIS_CURRENT:
		case MIS_DEFAULT:
		default:
			return MENU_ICON_PIXELS_SMALL;
	}
}

/* Re-render cached menu's rows from their stored icon names,
 * descending into submenus [20260925122040].
 */
static void refresh_menu_icons(GtkWidget *menu)
{
	GList *kids, *l;

	kids = gtk_container_get_children(GTK_CONTAINER(menu));
	for (l = kids; l; l = l->next)
	{
		GtkWidget *item = (GtkWidget *) l->data;

		if (GTK_IS_MENU_ITEM(item))
		{
			const char *icon_name;
			GtkWidget *submenu;

			icon_name = g_object_get_data(G_OBJECT(item),
					"rox-icon-name");
			if (icon_name)
			{
				menu_item_set_image(item,
						menu_icon_image(icon_name));
			}
			submenu = gtk_menu_item_get_submenu(
					GTK_MENU_ITEM(item));
			if (submenu)
				refresh_menu_icons(submenu);
		}
	}
	g_list_free(kids);
}

/* Re-render the cached menus' icons at the current size setting. Same
 * widgets, same order, same accelerators -- only the images are swapped,
 * so no saved row pointer or shade index is disturbed.
 */
void menu_refresh_icons(void)
{
	if (filer_menu)
		refresh_menu_icons(filer_menu);
	if (file_popup.menu)
		refresh_menu_icons(file_popup.menu);
	if (icon_menu)
		refresh_menu_icons(icon_menu);
}

/* Notify callback: re-render only when the pixel count actually changed.
 * Registered for all option changes, so most invocations are no-op here.
 */
static int last_menu_icon_pixels = -1;
static void menu_iconsize_changed(void)
{
	int pixels = get_menu_icon_pixels();

	if (pixels == last_menu_icon_pixels)
		return;
	last_menu_icon_pixels = pixels;
	menu_refresh_icons();
}

/* Apply menu_set_items_shaded to both File menu instances */
static void shade_both(int from, int n, gboolean shaded)
{
	menu_set_items_shaded(file_submenu.menu, shaded, from, n);
	menu_set_items_shaded(file_popup.menu, shaded, from, n);
}

/* Shade items that only work on single files */
static void shade_file_menu_items(gboolean shaded)
{
	shade_both(FILE_MENU_DUPLICATE, 1, shaded); /* Duplicate... */
	shade_both(FILE_MENU_LINK, 1, shaded); /* Link... */
	shade_both(FILE_MENU_SHIFT_OPEN, 1, shaded); /* Shift Open */
	shade_both(FILE_MENU_RUN_ACTION, 2, shaded); /* Set Run Action... + Set Icon... */
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	shade_both(FILE_MENU_XATTRS, 1, shaded); /* Extended Attributes... */
#endif
}

/*
 * Build a menu image for an item at the requested pixel size, or NULL when
 * there is no icon to draw.
 *
 * 'pixels' 0 means "no icons". Otherwise the two sizes the filer
 * already caches are reused when they fit, and anything between them is
 * scaled from the cached full-size image on demand. The two cached sizes
 * are ICON_WIDTH x ICON_HEIGHT and the small tier (16); only the size
 * between them has to be produced here.
 *
 * Note that the requested size is a bounding box, not an exact size:
 * scale_pixbuf() preserves the source's aspect ratio, so a square icon
 * asked for 32 comes out square and a non-square one is fitted inside.
 *
 * The base pixbuf is taken with a reference of its own. This function
 * releases exactly one reference to 'base' and one to 'lined' on
 * every path, and the cached images themselves are left alone.
 */
GtkWidget *menu_make_image(DirItem *ditem, gint pixels)
{
	MaskedPixmap	*image;
	GdkPixbuf	*base;
	GdkPixbuf	*lined = NULL;
	GdkPixbuf	*shown;
	GtkWidget	*ret;

	if (pixels <= 0 || !di_image(ditem))
		return NULL;

	image = di_image(ditem);

	base = scale_pixbuf(image->pixbuf, pixels, pixels);

	if (!base)
		return NULL;

	shown = base;

	if (ditem->label)
	{
		lined = pixmap_make_lined(base, ditem->label);
		if (lined)
			shown = lined;
	}

	ret = gtk_image_new_from_pixbuf(shown);

	if (lined)
		g_object_unref(lined);
	g_object_unref(base);

	return ret;
}
static GtkWidget *make_send_to_item(DirItem *ditem, const char *label,
			gint pixels)
{
	GtkWidget *img;

	if ((img = menu_make_image(ditem, pixels)))
		return menu_item_new_with_image_mnemonic(label, img);

	return gtk_menu_item_new_with_label(label);
}

static GList *menu_from_dir(GtkWidget *menu, const gchar *dir_name,
			    gint pixels, CallbackFn func,
			    gboolean separator, gboolean strip_ext,
			    gboolean recurse)
{
	GList *widgets = NULL;
	DirItem *ditem;
	int i;
	GtkWidget *item;
	char *dname = NULL;
	GPtrArray *names;

	dname = g_strdup(dir_name);

	names = list_dir(dname);
	if (!names)
		goto out;

	for (i = 0; i < names->len; i++)
	{
		char	*leaf = names->pdata[i];
		gchar	*fname;

		if (separator)
		{
			item = gtk_separator_menu_item_new();
			widgets = g_list_append(widgets, item);
			if (menu)
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			separator = FALSE;
		}

		fname = g_strconcat(dname, "/", leaf, NULL);

		/* Strip off extension, if any */
		if (strip_ext)
		{
			char	*dot;
			dot = strchr(leaf, '.');
			if (dot)
				*dot = '\0';
		}

		ditem = diritem_new("");
		diritem_restat(fname, ditem, NULL, TRUE);

		item = make_send_to_item(ditem, leaf, pixels);

		/* If it is a directory (but NOT an AppDir) and we are
		 * recursing then set up a sub menu.
		 */
		if (recurse && ditem->base_type == TYPE_DIRECTORY &&
			   !(ditem->flags & ITEM_FLAG_APPDIR))
		{
			GtkWidget *sub = gtk_menu_new();
			const gchar *apath = menu ?
				gtk_menu_get_accel_path(GTK_MENU(menu)) : NULL;
			if (apath)
			{
				gchar *p = g_strconcat(apath, leaf, "/", NULL);
				gtk_menu_set_accel_group(GTK_MENU(sub),
						gtk_menu_get_accel_group(GTK_MENU(menu)));
				gtk_menu_set_accel_path(GTK_MENU(sub), p);
				g_free(p);
			}

			g_list_free(
				menu_from_dir(sub, fname, pixels, func,
						separator, strip_ext, TRUE)
			);

			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
		}
		else
			g_signal_connect_swapped(item, "activate",
					G_CALLBACK(func), fname);

		g_free(leaf);
		diritem_free(ditem);

		if (menu)
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		g_signal_connect_swapped(item, "destroy",
				G_CALLBACK(g_free), fname);

		widgets = g_list_append(widgets, item);
	}

	g_ptr_array_free(names, TRUE);
out:
	g_free(dname);

	return widgets;
}

/* Scan the templates dir and create entries for the New menu */
static void update_new_files_menu()
{
	static GList *widgets = NULL;

	gchar	*templ_dname = NULL;

	if (widgets)
	{
		GList	*next;

		for (next = widgets; next; next = next->next)
			gtk_widget_destroy((GtkWidget *) next->data);

		g_list_free(widgets);
		widgets = NULL;
	}

	templ_dname = choices_find_xdg_path_load("Templates", "", SITE);
	if (templ_dname)
	{
		widgets = menu_from_dir(filer_new_menu, templ_dname,
					get_menu_icon_pixels(),
					(CallbackFn) new_file_type, TRUE, TRUE,
					FALSE);
		g_free(templ_dname);
	}
	gtk_widget_show_all(filer_new_menu);
}

static void directory_cb(const gchar *app)
{
	GList *file_list = g_list_prepend(NULL, window_with_focus->sym_path);
	run_with_files(app, file_list, FALSE);
	g_list_free(file_list);
}
static void update_directory_menu()
{
	static GList *widgets = NULL;

	for (; widgets; widgets = g_list_delete_link(widgets, widgets))
		gtk_widget_destroy((GtkWidget *) widgets->data);

	widgets = add_sendto_shared(filer_menu,
			inode_directory->media_type, NULL, (CallbackFn) directory_cb);
	if (widgets)
	{
		/* Divider between Customise and added entries [20260921093115]. */
		GList *kids = gtk_container_get_children(
				GTK_CONTAINER(filer_menu));
		GtkWidget *sep = gtk_separator_menu_item_new();
		gtk_menu_shell_insert(GTK_MENU_SHELL(filer_menu), sep,
				g_list_length(kids) - g_list_length(widgets));
		g_list_free(kids);
		widgets = g_list_prepend(widgets, sep);
	}
	/* Tag dot-inode items for shortcut editor [20260901075838]. */
	for (GList *l = widgets; l; l = l->next)
		g_object_set_data(G_OBJECT(l->data),
				"is_inode_sendto", GINT_TO_POINTER(1));
}

/*
 * Build the body of a File menu (App..., SendTo..., Copy...Find) into 'menu'
 * and record its dynamic widgets in 'fm'. Called twice: once for the "File"
 * submenu nested in the ROX menu and once for the standalone right-click
 * popup. The two instances must stay identical; per-popup state updates are
 * applied to both.
 */
static void build_file_menu_contents(GtkWidget *menu, FileMenu *fm)
{
	fm->menu = menu;

	/* TRANSLATORS: see also the "App dir" msgid => "App..." */
	fm->app_menu = start_menu(N_("_App..."), menu, &fm->app_item);

	/* Merged Send To entry [20260911082953]: a pure submenu. The detached
	 * equivalent popup remains available via Shift+right-click, Shift+F10,
	 * Shift+`|` and Shift+Menu, which need no menu entry.
	 */
	fm->sendto_menu = start_menu(N_("_Send To..."), menu, &fm->sendto_item);

	sta(ads(menu, N_("Copy"), file_op, FILE_COPY_TO_CLIPBOARD,
				"edit-copy"),
			GDK_KEY_c, GDK_CONTROL_MASK);
	sta(ads(menu, N_("Cut" ), file_op, FILE_CUT_TO_CLIPBOARD,
				"edit-cut"),
			GDK_KEY_x, GDK_CONTROL_MASK);
	sta(ads(menu, N_("Duplicate..."), file_op, FILE_DUPLICATE_ITEM,
				"edit-copy"),
			GDK_KEY_d, GDK_CONTROL_MASK);
	ads(menu, N_("Rename..."), file_op, FILE_RENAME_ITEM,
				"edit-copy");
	ads(menu, N_("Link..."  ), file_op, FILE_LINK_ITEM,
				"emblem-symbolic-link");
	sta(ads(menu, N_("Delete"   ), file_op, FILE_DELETE, "edit-delete"),
			GDK_KEY_Delete, 0);

	add_separator(menu);

	fm->shift_item = ads(menu, N_("Shift Open"), file_op, FILE_OPEN_FILE,
			"text-x-generic");

	add_separator(menu);

	sta(ads(menu, N_("Set Run Action..."), file_op, FILE_RUN_ACTION,
				"system-run"),
			GDK_KEY_asterisk, 0);
	ads(menu, N_("Set Icon..."      ), file_op, FILE_SET_ICON,
				"insert-image");
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	fm->xattrs = gtk_bin_get_child(GTK_BIN(
			ads(menu, N_("Extended attributes..."), file_op,
					FILE_XATTRS, "emblem-system")));
#endif
	sta(ads(menu, N_("Properties" ), file_op, FILE_PROPERTIES,
				"document-properties"),
			GDK_KEY_p, GDK_CONTROL_MASK);
	ads(menu, N_("Count"      ), file_op, FILE_USAGE,
				"drive-harddisk");
	fm->set_type = gtk_bin_get_child(GTK_BIN(
			ads(menu, N_("Set Type..."), file_op, FILE_SET_TYPE,
				"preferences-other")));
	ads(menu, N_("Permissions"), file_op, FILE_CHMOD_ITEMS,
				"emblem-readonly");

	add_separator(menu);

	sta(ads(menu, N_("Find"), file_op, FILE_FIND, "edit-find"),
			GDK_KEY_f, GDK_CONTROL_MASK);
}

gboolean ensure_filer_menu(void)
{
	if (filer_menu) return FALSE;

	filer_menu = start_menu("<filer>", NULL, NULL);
	gtk_menu_set_accel_group(GTK_MENU(filer_menu), filer_keys);

	GtkWidget *display_menu = start_menu(N_("_Display"), filer_menu, NULL);

	adim(display_menu, N_("Icons View"            ), view_type, VIEW_TYPE_COLLECTION);
	adim(display_menu, N_("Icons With Sizes"      ), set_with , DETAILS_SIZE);
	adim(display_menu, N_("Icons With Times"      ), set_with , DETAILS_TIMES);
	adim(display_menu, N_("Icons With Permissions"), set_with , DETAILS_PERMISSIONS);
	adim(display_menu, N_("Icons With Types"      ), set_with , DETAILS_TYPE);

	ads(display_menu, N_("List View"), view_type, VIEW_TYPE_DETAILS,
			ROX_STOCK_SHOW_DETAILS);

	add_separator(display_menu);

	sta(ads(display_menu, N_("Bigger Icons"), change_size, 1, "zoom-in"),
			GDK_KEY_plus, 0);
	sta(ads(display_menu, N_("Smaller Icons"), change_size, -1, "zoom-out"),
			GDK_KEY_minus, 0);
	sta(adt(display_menu, N_("Automatic"), change_size_auto, 0,
				&filer_auto_size_menu),
			GDK_KEY_equal, 0);

	add_separator(display_menu);

	GSList *sg = NULL;
	sg = adr(display_menu, N_("Sort by Name"        ), set_sort, SORT_NAME , sg, &filer_sort_name_menu  );
	sg = adr(display_menu, N_("Sort by Type"        ), set_sort, SORT_TYPE , sg, &filer_sort_type_menu  );
	sg = adr(display_menu, N_("Sort by Date (atime)"), set_sort, SORT_DATEA, sg, &filer_sort_date_a_menu);
	sg = adr(display_menu, N_("Sort by Date (ctime)"), set_sort, SORT_DATEC, sg, &filer_sort_date_c_menu);
	sg = adr(display_menu, N_("Sort by Date (mtime)"), set_sort, SORT_DATEM, sg, &filer_sort_date_m_menu);
	sg = adr(display_menu, N_("Sort by Size"        ), set_sort, SORT_SIZE , sg, &filer_sort_size_menu  );
	sg = adr(display_menu, N_("Sort by permissions" ), set_sort, SORT_PERM , sg, &filer_sort_perm_menu  );
	sg = adr(display_menu, N_("Sort by Owner"       ), set_sort, SORT_OWNER, sg, &filer_sort_owner_menu );
	     adr(display_menu, N_("Sort by Group"       ), set_sort, SORT_GROUP, sg, &filer_sort_group_menu );

	adt(display_menu, N_("Reversed"), reverse_sort, 0, &filer_reverse_menu);

	add_separator(display_menu);

	sta(adt(display_menu, N_("Show Hidden"), hidden, 0, &filer_hidden_menu),
			GDK_KEY_h, GDK_CONTROL_MASK);
	adt(display_menu, N_("Show Only Files"              ), only_dirs, 0, &filer_files_only_menu);
	adt(display_menu, N_("Show Only Directories"        ), only_dirs, 1, &filer_dirs_only_menu);
	adim(display_menu, N_("Filter Files..."              ), mini_buffer, MINI_FILTER);
	adim(display_menu, N_("Temp Filter..."               ), mini_buffer, MINI_TEMP_FILTER);
	adt(display_menu, N_("Filter Directories With Files"), filter_directories, 0,
			&filer_filter_dirs_menu);
	adt(display_menu, N_("Show Thumbnails"              ), show_thumbs   , 0,
			&filer_thumb_menu);
	ads(display_menu, N_("Refresh"                      ), refresh       , 0,
			"view-refresh");
	ads(display_menu, N_("Refresh Thumbs"               ), refresh_thumbs, 0,
			"view-refresh");

	adim(display_menu, N_("Save Display Settings..."           ), save_settings, 0);
	adim(display_menu, N_("Save Display Settings to parent ..."), save_settings_parent, 0);


	GtkWidget *file_item;
	/* Only the no-selection state is fixed English text that can take
	 * a mnemonic marker ("Next _Click"). [20261001070501] */
	GtkWidget *file_menu = start_menu("File", filer_menu, &file_item);
	filer_file_item = gtk_bin_get_child(GTK_BIN(file_item));
	build_file_menu_contents(file_menu, &file_submenu);

	/* The standalone popup twin shares the nested menu's accelerator path */
	{
		GtkWidget *popup = gtk_menu_new();

		gtk_menu_set_accel_group(GTK_MENU(popup), filer_keys);
		gtk_menu_set_accel_path(GTK_MENU(popup),
				gtk_menu_get_accel_path(GTK_MENU(file_menu)));
		build_file_menu_contents(popup, &file_popup);

		g_signal_connect(popup, "selection-done",
				G_CALLBACK(menu_closed), NULL);
	}

	GtkWidget *select_menu = start_menu(N_("_Select"), filer_menu, NULL);

	sta(adi(select_menu, N_("Select All"       ), select_all, 0),
			GDK_KEY_a, GDK_CONTROL_MASK);
	adi(select_menu, N_("Clear Selection"  ), clear_selection, 0);
	adi(select_menu, N_("Invert Selection" ), invert_selection, 0);
	sta(adi(select_menu, N_("Select by Name..."), mini_buffer, MINI_SELECT_BY_NAME),
			GDK_KEY_period, 0);
	sta(adi(select_menu, N_("Reg Select..."    ), mini_buffer, MINI_REG_SELECT),
			GDK_KEY_asciicircum, 0);
	sta(adi(select_menu, N_("Select If..."     ), mini_buffer, MINI_SELECT_IF),
			GDK_KEY_question, GDK_SHIFT_MASK);

	ads(filer_menu, N_("Options..."), menu_show_options, 0, "preferences-system");
	sta(ads(filer_menu, N_("Paste"), paste_from_clipboard, 0, "edit-paste"),
			GDK_KEY_v, GDK_CONTROL_MASK);

	filer_new_menu = start_menu(N_("_New"), filer_menu, NULL);

	ads(filer_new_menu, N_("Directory"        ), new_directory, 0, "folder");
	ads(filer_new_menu, N_("Blank file"       ), new_file     , 0, "document-new");
	adim(filer_new_menu, N_("Customise Menu..."), customise_new, 0);


	GtkWidget *window_menu = start_menu(N_("_Window"), filer_menu, NULL);

	ads(window_menu, N_("Parent, New Window"   ), open_parent     , 0, "go-up");
	ads(window_menu, N_("Parent, Same Window"  ), open_parent_same, 0, "go-up");
	filer_new_window =
	ads(window_menu, N_("New Window"           ), new_window      , 0, "window-new");
	sta(ads(window_menu, N_("Home Directory"       ), home_directory  , 0,
				"go-home"),
			GDK_KEY_Home, GDK_CONTROL_MASK);
	sta(ads(window_menu, N_("Show Bookmarks"       ), show_bookmarks  , 0,
				ROX_STOCK_BOOKMARKS),
			GDK_KEY_b, GDK_CONTROL_MASK);
	ads(window_menu, N_("Show Log"             ), show_log        , 0,
			"dialog-information");
	filer_follow_sym =
	adim(window_menu, N_("Follow Symbolic Links"), follow_symlinks , 0);
	sta(adim(window_menu, N_("Resize Window"        ), resize          , 0),
			GDK_KEY_e, GDK_CONTROL_MASK);
	sta(ads(window_menu, N_("Close Window"         ), close_window    , 0,
				"window-close"),
			GDK_KEY_q, GDK_CONTROL_MASK);

	add_separator(window_menu);

	sta(adim(window_menu, N_("Enter Path..."     ), mini_buffer, MINI_PATH),
			GDK_KEY_slash, 0);
	sta(adim(window_menu, N_("Shell Command..."  ), mini_buffer, MINI_SHELL),
			GDK_KEY_exclam, GDK_SHIFT_MASK);
	sta(adim(window_menu, N_("Terminal Here"     ), xterm_here , FALSE),
			GDK_KEY_grave, 0);
	adim(window_menu, N_("Switch to Terminal"), xterm_here , TRUE);

	filer_pinboard_menu = start_menu(N_("_Pinboard"), filer_menu, NULL);

	filer_pinboard_backdrop_item = ads(filer_pinboard_menu, N_("Backdrop..."),
			pinboard_show_backdrop_box, 0, "preferences-system");

	filer_pinboard_panel_item = ads(filer_pinboard_menu, N_("Add _Panel"),
			NULL, 0, "list-add");


	GtkWidget *help_menu = start_menu(N_("_Help"), filer_menu, NULL);

	ads(help_menu, N_("About ROX-Filer..."), menu_rox_help,  HELP_ABOUT,
				"help-about");
	sta(ads(help_menu, N_("Show Help Files"   ), menu_rox_help,  HELP_DIR,
				"help-browser"),
			GDK_KEY_F1, 0);
	ads(help_menu, N_("Manual"), menu_rox_help,  HELP_MANUAL,
				"help-contents");

	adim(filer_menu, N_("Customise Dir Menu..."), customise_directory_menu, 0);


	g_signal_connect(filer_menu, "selection-done",
			G_CALLBACK(menu_closed), NULL);

	/* Keyboard pre-seed: the first Up/Down after a painted-only
	 * pre-select is translated relative to that item (see
	 * menu_seed_move_current). */
	g_signal_connect(filer_menu, "move-current",
			G_CALLBACK(menu_seed_move_current), NULL);

	/* Accelerators persist on dialog close instead (see shortcuts dialog) */

	//for accel
	update_new_files_menu();
	update_directory_menu();
	gtk_widget_show_all(filer_menu);

	return TRUE;
}

/*
 * One-shot "size-allocate" handler that runs during LAYOUT, before PAINT:
 * - conditionally pre-selects 'item' if invoked via keyboard, bypassing
 *   the physical mouse pointer conflict.
 * - invalidates the reused toplevel surface so the stale highlight pixels
 *   left over from the previous popup session are cleared before this
 *   session is painted.
 */
static void menu_select_repaint_alloc(GtkWidget *widget,
		GdkRectangle *allocation, gpointer data)
{
	int item = GPOINTER_TO_INT(data);
	GdkWindow *win;

	g_signal_handlers_disconnect_by_func(widget,
			menu_select_repaint_alloc, data);

	if (item >= 0)
		select_nth_item(GTK_MENU_SHELL(widget), item);
	else if (item == -1)
		/* Mouse popup: no row may be lit when the menu opens.
		 * GTK lights the row under the pointer from its own
		 * enter-notify, before this handler runs; deselect it
		 * now so the frame is painted clean. [20261001070501]
		 */
		gtk_menu_shell_deselect(GTK_MENU_SHELL(widget));
	else
		/* FILER_MENU_NO_PRESELECT: keyboard popup,
		 * nothing pre-lit. [20261002090324]
		 */
		;

	win = gtk_widget_get_window(widget);
	if (win)
		gdk_window_invalidate_rect(win, NULL, TRUE);
	gtk_widget_queue_draw(widget);
}

/*
 * 'item' is the number of the logical item to pre-select for keyboard users.
 * Unlike GTK2 ROX-Filer, physical pointer alignment is surrendered to GTK.
 */
void show_popup_menu(GtkWidget *menu, GdkEvent *event, int item)
{
	gboolean is_keyboard = (event && event->type == GDK_KEY_PRESS);

	/* A seed left over from a previous keyboard popup of this (possibly
	 * reused) menu must not fire for mouse-driven sessions. */
	if (!is_keyboard)
		g_object_set_data(G_OBJECT(menu), "rox-seed-item", NULL);

	/* show_all shows the children; show() additionally refreshes the
	 * accelerator paths so the menu's own preferred width includes the
	 * toggle/accelerator/padding columns. */
	gtk_widget_show_all(menu);
	gtk_widget_show(menu);

	g_signal_connect(menu, "size-allocate",
			G_CALLBACK(menu_select_repaint_alloc),
			GINT_TO_POINTER(is_keyboard ? item : -1));

	gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
}

/* Hide the popup menu, if any */
void menu_popdown(void)
{
	if (popup_menu)
		gtk_menu_popdown(GTK_MENU(popup_menu));
}

static void clipboardcb(
		GtkClipboard *clipboard,
		GtkSelectionData *data,
		gpointer p)
{
	if (gtk_selection_data_get_length(data) > 0)
		menu_set_items_shaded(filer_menu, false, 4, 1);
	else if (GPOINTER_TO_INT(p))
		gtk_clipboard_request_contents(
				clipboard, text_uri_list, clipboardcb, GUINT_TO_POINTER(0));
}

/* Rebuild the "Add Panel" submenu, so a side that gained or lost a panel since
 * the last show is reflected. The old submenu must be destroyed first:
 * gtk_menu_item_set_submenu() only detaches it, which would leak. [20261008152442]
 */
static void rebuild_pinboard_panel_submenu(void)
{
	GtkWidget	*old, *sub;

	old = gtk_menu_item_get_submenu(GTK_MENU_ITEM(filer_pinboard_panel_item));
	if (old)
		gtk_widget_destroy(old);

	sub = panel_new_panel_submenu();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(filer_pinboard_panel_item), sub);
	gtk_menu_set_accel_group(GTK_MENU(sub),
			gtk_menu_get_accel_group(GTK_MENU(filer_pinboard_menu)));
}

/* iter->peek() is the clicked item, or NULL if none */
void show_filer_menu(FilerWindow *filer_window, GdkEvent *event, ViewIter *iter)
{
	DirItem		*file_item = NULL;
	GdkModifierType	state = 0;
	int		n_selected;

	g_return_if_fail(event != NULL);

	n_selected = view_count_selected(filer_window->view);

	ensure_filer_menu();

	updating_menu++;

	/* Remove previous AppMenu, if any */
	appmenu_remove();

	window_with_focus = filer_window;

	if (event->type == GDK_BUTTON_PRESS)
		state = ((GdkEventButton *) event)->state;
	else if (event->type == GDK_KEY_PRESS)
		state = ((GdkEventKey *) event)->state;

	if (n_selected == 0 && iter && iter->peek(iter) != NULL)
	{
		filer_window->temp_item_selected = TRUE;
		view_set_selected(filer_window->view, iter, TRUE);
		n_selected = view_count_selected(filer_window->view);
	}
	else
	{
		filer_window->temp_item_selected = FALSE;
	}

	/* Determine whether to shade "Paste" option */
	menu_set_items_shaded(filer_menu, true, 4, 1);
	gtk_clipboard_request_contents(
			clipboard, gnome_copied_files, clipboardcb, GUINT_TO_POINTER(1));

	/* Short-cut to the Send To menu */
	if (state & GDK_SHIFT_MASK)
	{
		updating_menu--;

		if (n_selected == 0)
		{
			show_dir_send_to_menu(event);
			return;
		}

		/* (paths eaten) */
		show_send_to_menu(filer_window, event);
		return;
	}

	{
		GtkWidget	*file_label;
		GString		*buffer;
		DirItem		*item;
		gboolean	with_mnemonic;

		file_label = filer_file_item;
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_thumb_menu),
				filer_window->show_thumbs);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_hidden_menu),
				filer_window->show_hidden);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_files_only_menu),
				filer_window->files_only);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_dirs_only_menu),
				filer_window->dirs_only);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_filter_dirs_menu),
				filer_window->filter_directories);

		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_name_menu  ),
				filer_window->sort_type == SORT_NAME );
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_type_menu  ),
				filer_window->sort_type == SORT_TYPE );
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_date_a_menu),
				filer_window->sort_type == SORT_DATEA);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_date_c_menu),
				filer_window->sort_type == SORT_DATEC);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_date_m_menu),
				filer_window->sort_type == SORT_DATEM);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_size_menu  ),
				filer_window->sort_type == SORT_SIZE );
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_perm_menu  ),
				filer_window->sort_type == SORT_PERM );
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_owner_menu ),
				filer_window->sort_type == SORT_OWNER);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_sort_group_menu ),
				filer_window->sort_type == SORT_GROUP);

		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_reverse_menu),
				filer_window->sort_order != GTK_SORT_ASCENDING);
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(filer_auto_size_menu),
			filer_window->display_style_wanted == AUTO_SIZE_ICONS);
		buffer = g_string_new(NULL);

		/* Only the "nothing selected" text is fixed English  that
		 * can carry a mnemonic; the other two states embed a type
		 * name or a count. [20261001070501]
		 */
		with_mnemonic = FALSE;

		switch (n_selected)
		{
			case 0:
				/* TRANSLATORS: underscore (_) marks the mnemonic */
				g_string_assign(buffer, _("Next _Click"));
				with_mnemonic = TRUE;
				shade_file_menu_items(FALSE);
				break;
			case 1:
				item = filer_selected_item(filer_window);
				if (item->base_type == TYPE_UNKNOWN)
					dir_update_item(filer_window->directory,
							item->leafname);
				shade_file_menu_items(FALSE);
				file_item = filer_selected_item(filer_window);
				g_string_printf(buffer, _("%s '%s'"),
					basetype_name(file_item),
					g_utf8_validate(file_item->leafname,
							-1, NULL)
						? file_item->leafname
						: _("(bad utf-8)"));
				if (!can_set_run_action(file_item))
					shade_both(FILE_MENU_RUN_ACTION, 1,
							TRUE);
				break;
			default:
				shade_file_menu_items(TRUE);
				g_string_printf(buffer, _("%d items"),
						 n_selected);
				break;
		}
		if (with_mnemonic)
		{
			gtk_label_set_text_with_mnemonic(GTK_LABEL(file_label),
					buffer->str);
		}
		else
		{
			gtk_label_set_text(GTK_LABEL(file_label), buffer->str);
		}
		g_string_free(buffer, TRUE);

		menu_show_shift_action(file_submenu.shift_item, file_item,
					n_selected == 0);
		menu_show_shift_action(file_popup.shift_item, file_item,
					n_selected == 0);

		/* Choose the menu to pop up before populating its dynamic
		 * App/SendTo content; only the shown instance gets populated.
		 */
		if (n_selected && o_menu_quick.int_value)
			popup_menu = (state & GDK_CONTROL_MASK)
					? filer_menu
					: file_popup.menu;
		else
			popup_menu = (state & GDK_CONTROL_MASK)
					? file_popup.menu
					: filer_menu;

		if (n_selected)
		{
			FileMenu *shown = popup_menu == file_popup.menu
					? &file_popup : &file_submenu;

			if (file_item)
				appmenu_add_submenus(NULL, make_path(filer_window->sym_path, file_item->leafname),
						file_item, shown);
			else
				appmenu_add_submenus(filer_window, NULL, NULL, shown);
		}
		else
		{
			/*
			 * No selection: nothing populates App.../SendTo...,
			 * so the headers must not open their empty submenus
			 * ("Next Click"), whatever a previous populate cycle
			 * left behind.
			 */
			gtk_widget_set_sensitive(file_submenu.app_item,
					FALSE);
			gtk_widget_set_sensitive(file_submenu.sendto_item,
					FALSE);
			gtk_widget_set_sensitive(file_popup.app_item, FALSE);
			gtk_widget_set_sensitive(file_popup.sendto_item,
					FALSE);
		}
	}

	update_new_files_menu();
	update_directory_menu();

	gtk_widget_set_sensitive(filer_new_window,
			!o_unique_filer_windows.int_value);
	gtk_widget_set_sensitive(filer_follow_sym,
		strcmp(filer_window->sym_path, filer_window->real_path) != 0);
	{
		gboolean type_ok = xattr_supported(filer_window->real_path);
		FileMenu *const groups[2] = {&file_submenu, &file_popup};
		int i;

		for (i = 0; i < 2; i++)
		{
			gtk_widget_set_sensitive(groups[i]->set_type, type_ok);
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
			gtk_widget_set_sensitive(groups[i]->xattrs,
					type_ok && n_selected <= 1);
#endif
		}
	}

	gtk_widget_set_sensitive(filer_pinboard_backdrop_item,
			current_pinboard != NULL);
	rebuild_pinboard_panel_submenu();

	updating_menu--;

	show_popup_menu(popup_menu, event,
			n_selected == 0 ? FILER_MENU_NO_PRESELECT :
			popup_menu == file_popup.menu ? FILE_MENU_COPY : FILER_MENU_NEXT_CLICK);
}

static void menu_closed(GtkWidget *widget)
{
	if (window_with_focus == NULL || widget != popup_menu)
		return;			/* Close panel item chosen? */

	popup_menu = NULL;

	menu_set_items_shaded(filer_menu, false, 4, 1);

	/* Drop any keyboard-navigation seed from this popup session.
	 * The hand-painted PRELIGHT must go with it: GTK clears only
	 * priv->active_menu_item on popdown (gtkmenu.c), and a seeded row
	 * was never registered there, so a flag left set keeps that row lit
	 * in every later popup of this reused menu. [20261001070501]
	 */
	{
		GtkWidget *seed = g_object_get_data(G_OBJECT(widget),
				"rox-seed-item");

		if (seed)
		{
			gtk_widget_unset_state_flags(seed,
					GTK_STATE_FLAG_PRELIGHT);
			gtk_widget_queue_draw(seed);
		}

		g_object_set_data(G_OBJECT(widget), "rox-seed-item", NULL);
	}

	if (window_with_focus->temp_item_selected)
	{
		view_clear_selection(window_with_focus->view);
		window_with_focus->temp_item_selected = FALSE;
	}

	appmenu_remove();
}

static void target_callback(FilerWindow *filer_window,
			ViewIter *iter,
			gpointer action)
{
	g_return_if_fail(filer_window != NULL);

	window_with_focus = filer_window;

	/* Don't grab the primary selection */
	filer_window->temp_item_selected = TRUE;

	view_wink_item(filer_window->view, iter);
	view_select_only(filer_window->view, iter);
	file_op(NULL, GPOINTER_TO_INT(action), NULL);

	view_clear_selection(filer_window->view);
	filer_window->temp_item_selected = FALSE;
}
/* Set the text and icon of the 'Shift Open...' menu item.
 * If item is NULL, reset the text and also shade it, unless 'next'.
 * The icon follows the action from the item's own image, the same one
 * the SendTo menu shows, at the current menu icon size, so mount points,
 * symlinks, directories and files each get their own picture. With no item
 * (or with menu icons disabled, MIS_NONE) the row shows no icon, leaving
 * the gutter empty as SendTo entries do [20260923070143].
 */
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next)
{
	gchar		*shift_action = NULL;
	GtkWidget	*image = NULL;

	if (item)
	{
		if (item->flags & ITEM_FLAG_MOUNT_POINT)
		{
			if (item->flags & ITEM_FLAG_MOUNTED)
				shift_action = N_("Unmount");
			else
				shift_action = N_("Open unmounted");
		}
		else if (item->flags & ITEM_FLAG_SYMLINK)
			shift_action = N_("Show Target");
		else if (item->base_type == TYPE_DIRECTORY)
			shift_action = N_("Look Inside");
		else if (item->base_type == TYPE_FILE)
			shift_action = N_("Open As Text");

		image = menu_make_image(item, get_menu_icon_pixels());
	}

	gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(menu_item))),
			shift_action ? _(shift_action)
				     : _("Shift Open"));

	menu_item_set_image(menu_item, image);

	gtk_widget_set_sensitive(menu_item, shift_action != NULL || next);
}

/* Actions */

static void view_type(gpointer data, guint action, GtkWidget *widget)
{
	ViewType view_type = (ViewType) action;

	g_return_if_fail(window_with_focus != NULL);

	if (view_type == VIEW_TYPE_COLLECTION)
		display_set_layout(window_with_focus,
				window_with_focus->display_style_wanted,
				DETAILS_NONE, FALSE);

	filer_set_view_type(window_with_focus, (ViewType) action);
}

static void change_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_change_size(window_with_focus, action == 1);
}

static void change_size_auto(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (updating_menu)
		return;

	if (window_with_focus->display_style_wanted == AUTO_SIZE_ICONS)
		display_set_layout(window_with_focus,
				   window_with_focus->display_style,
				   window_with_focus->details_type, FALSE);
	else
		display_set_layout(window_with_focus, AUTO_SIZE_ICONS,
				   window_with_focus->details_type, FALSE);
}

static void set_with(gpointer data, guint action, GtkWidget *widget)
{
	DisplayStyle size;

	g_return_if_fail(window_with_focus != NULL);

	size = window_with_focus->display_style_wanted;

	filer_set_view_type(window_with_focus, VIEW_TYPE_COLLECTION);
	display_set_layout(window_with_focus, size, action, FALSE);
}
static void wink_if(FilerWindow *fw)
{
	if (!fw->temp_item_selected &&
			view_count_selected(fw->view) == 1)
	{
		ViewIter iter;
		view_get_iter(fw->view, &iter, VIEW_ITER_SELECTED);
		if (iter.next(&iter))
			view_wink_item(fw->view, &iter);
	} else {
		view_scroll_to_top(fw->view);
	}
}
static void set_sort(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_type(window_with_focus, action, GTK_SORT_ASCENDING);
	wink_if(window_with_focus);
}

static void reverse_sort(gpointer data, guint action, GtkWidget *widget)
{
	GtkSortType order;

	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	order = window_with_focus->sort_order;
	if (order == GTK_SORT_ASCENDING)
		order = GTK_SORT_DESCENDING;
	else
		order = GTK_SORT_ASCENDING;

	display_set_sort_type(window_with_focus, window_with_focus->sort_type,
			      order);
	wink_if(window_with_focus);
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_hidden(window_with_focus,
			   !window_with_focus->show_hidden);
}
static void only_dirs(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);
	FilerWindow *fw = window_with_focus;

	if (action) //dir
	{
		fw->dirs_only = !fw->dirs_only;
		fw->files_only = FALSE;
	}
	else //faile
	{
		fw->dirs_only = FALSE;
		fw->files_only = !fw->files_only;
	}
	display_update_hidden(fw);
}

static void filter_directories(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_filter_directories(window_with_focus,
			   !window_with_focus->filter_directories);
}

static void show_thumbs(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_thumbs(window_with_focus, !window_with_focus->show_thumbs);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_refresh(window_with_focus);
}

static void refresh_thumbs(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_refresh_thumbs(window_with_focus);
}

static void save_settings(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_save_settings(window_with_focus, FALSE);
}

static void save_settings_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_save_settings(window_with_focus, TRUE);
}

static void delete(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_delete(paths);
	destroy_glist(&paths);
}

static void usage(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_usage(paths);
	destroy_glist(&paths);
}

static void chmod_items(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_chmod(paths, FALSE, NULL);
	destroy_glist(&paths);
}

static void set_type_items(FilerWindow *filer_window)
{
	GList *paths, *p;
	int npass=0, nfail=0;

	paths = filer_selected_items(filer_window);
	for(p=paths; p; p=g_list_next(p)) {
		if(xattr_supported((const char *) p->data))
			npass++;
		else
			nfail++;
	}
	if(npass==0)
		report_error(_("Extended attributes, used to store types, are not supported for this "
				"file or files.\n"
				"This may be due to lack of support from the filesystem or the C library, "
				"or it may simply be that the filesystem needs to be mounted with "
				"the right mount option ('user_xattr' on Linux)."));
	else if(nfail>0)
		report_error(_("Setting type not supported for some of these files"));
	if(npass>0)
		action_settype(paths, FALSE, NULL);
	destroy_glist(&paths);
}

static void find(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_find(paths);
	destroy_glist(&paths);
}

static gboolean last_symlink_check_relative = TRUE;
static gboolean last_symlink_check_sympath = FALSE;

/* This creates a new savebox widget, and allows the user to pick a new path
 * for the file.
 * Once the new path has been picked, the callback will be called with
 * both the current and new paths.
 * NOTE: This function unrefs 'image'!
 */
static void savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action)
{
	GtkWidget *savebox = NULL;
	GtkWidget *check_relative = NULL, *check_sympath = NULL;

	g_return_if_fail(image != NULL);

	savebox = gtk_savebox_new(action);
	gtk_savebox_set_action(GTK_SAVEBOX(savebox), dnd_action);

	if (callback == link_cb)
	{
		check_relative = gtk_check_button_new_with_mnemonic(
							_("_Relative link"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_relative),
					     last_symlink_check_relative);

		gtk_widget_set_can_focus(check_relative, FALSE);
		gtk_widget_set_tooltip_text(check_relative,
			_("If on, the symlink will store the path from the "
			"symlink to the target file. Use this if the symlink "
			"and the target will be moved together.\n"
			"If off, the path from the root directory is stored - "
			"use this if the symlink may move but the target will "
			"stay put."));
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(savebox))),
				check_relative, FALSE, TRUE, 0);
		gtk_widget_show(check_relative);


		check_sympath = gtk_check_button_new_with_mnemonic(
							_("_Sym path"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_sympath),
					     last_symlink_check_sympath);

		gtk_widget_set_can_focus(check_sympath, FALSE);
		gtk_widget_set_tooltip_text(check_sympath,
			_("If on, the symlink will use target path as Sym path."));
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(savebox))),
				check_sympath, FALSE, TRUE, 0);
		gtk_widget_show(check_sympath);

		gtk_window_set_default_size(GTK_WINDOW(savebox), 400, -1);
	}
	else if (callback == rename_cb)
	{
		gtk_window_set_default_size(GTK_WINDOW(savebox), 400, -1);
	}

	g_signal_connect(savebox, "save_to_file",
				G_CALLBACK(save_to_file), NULL);

	g_object_set_data_full(G_OBJECT(savebox), "current_path",
				g_strdup(path), g_free);
	g_object_set_data(G_OBJECT(savebox), "action_callback", callback);
	g_object_set_data(G_OBJECT(savebox), "check_relative", check_relative);
	g_object_set_data(G_OBJECT(savebox), "check_sympath", check_sympath);

	gtk_window_set_title(GTK_WINDOW(savebox), action);

	if (g_utf8_validate(path, -1, NULL))
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), path);
	else
	{
		gchar *u8, *dir, *base;
		dir = g_path_get_dirname(path);
		base = g_path_get_basename(path);
		u8 = to_utf8(base);
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox),
				make_path(dir, u8));
		g_free(u8);
		g_free(dir);
		g_free(base);
	}
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixbuf);
	g_object_unref(image);

	rox_dialog_set_parent_full(savebox, FALSE);
	number_of_windows++;
	g_signal_connect(savebox, "destroy",
			G_CALLBACK(one_less_window), NULL);
	gtk_widget_show(savebox);
}

static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data)
{
	SaveCb		callback;
	const gchar	*current_path;

	callback = g_object_get_data(savebox, "action_callback");
	current_path = g_object_get_data(savebox, "current_path");

	g_return_val_if_fail(callback != NULL, GTK_XDS_SAVE_ERROR);
	g_return_val_if_fail(current_path != NULL, GTK_XDS_SAVE_ERROR);

	return callback(savebox, current_path, pathname)
			? GTK_XDS_SAVED : GTK_XDS_SAVE_ERROR;
}

static gboolean copy_cb(GObject *savebox,
			const gchar *current, const gchar *new)
{
	return action_with_leaf(action_copy, current, new);
}

static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new)
{
	const char	*leaf;
	char		*new_dir;
	GList		*local_paths;

	if (new[0] != '/')
	{
		report_error(_("New pathname is not absolute"));
		return FALSE;
	}

	if (new[strlen(new) - 1] == '/')
	{
		new_dir = g_strdup(new);
		leaf = NULL;
	}
	else
	{
		const gchar *slash;

		slash = strrchr(new, '/');
		new_dir = g_strndup(new, slash - new);
		leaf = slash + 1;
	}

	local_paths = g_list_append(NULL, (gchar *) current);
	action(local_paths, new_dir, leaf, -1);
	g_list_free(local_paths);

	g_free(new_dir);

	return TRUE;
}

/* Open a savebox to act on the selected file.
 * Call 'callback' later to perform the operation.
 */
static void src_dest_action_item(const gchar *path, MaskedPixmap *image,
			 const gchar *action, SaveCb callback,
			 GdkDragAction dnd_action)
{
	g_object_ref(image);
	savebox_show(action, path, image, callback, dnd_action);
}

static gboolean rename_cb(GObject *savebox,
			  const gchar *current, const gchar *new)
{
	return action_with_leaf(action_move, current, new);
}

static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path)
{
	GtkToggleButton *check_relative, *check_sympath;
	struct stat info;
	int	err;
	gchar	*link_path;

	check_relative = g_object_get_data(savebox, "check_relative");
	check_sympath = g_object_get_data(savebox, "check_sympath");

	last_symlink_check_relative = gtk_toggle_button_get_active(check_relative);
	last_symlink_check_sympath = gtk_toggle_button_get_active(check_sympath);

	if (last_symlink_check_relative)
	{
		if (last_symlink_check_sympath)
			link_path = get_relative_path(path, initial);
		else
		{
			gchar *real = pathdup(initial);
			link_path = get_relative_path(path, real);
			g_free(real);
		}
	}
	else
		link_path = last_symlink_check_sympath ?
			g_strdup(initial) : pathdup(initial);

	if (mc_lstat(path, &info) == 0 && S_ISLNK(info.st_mode))
	{
		GtkWidget *box, *button;
		gint ans;

		box = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_CANCEL,
				_("Symlink from '%s' already exists. "
				"Replace it with a link to '%s'?"),
				path, link_path);

		gtk_window_set_title(GTK_WINDOW(box), _("Symlink"));
		gtk_window_set_position(GTK_WINDOW(box), GTK_WIN_POS_MOUSE);

		/* [20260919080217] Later prompts parent to this one. */
		rox_dialog_note_modal(box);

		button = button_new_mixed("gtk-ok", _("_Replace"));
		gtk_widget_show(button);
		gtk_widget_set_can_default(button, TRUE);
		gtk_dialog_add_action_widget(GTK_DIALOG(box),
					     button, GTK_RESPONSE_OK);
		gtk_dialog_set_default_response(GTK_DIALOG(box),
						GTK_RESPONSE_OK);

		ans = gtk_dialog_run(GTK_DIALOG(box));
		gtk_widget_destroy(box);

		if (ans != GTK_RESPONSE_OK)
		{
			g_free(link_path);
			return FALSE;
		}

		unlink(path);
	}

	err = symlink(link_path, path);
	g_free(link_path);

	if (err)
	{
		report_error("symlink: %s", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);

	return TRUE;
}

static void run_action(DirItem *item)
{
	if (can_set_run_action(item))
		type_set_handler_dialog(item->mime_type);
	else
		report_error(
			_("You can only set the run action for a "
			"regular file"));
}

void open_home(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(home_dir, NULL, NULL, FALSE);
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_select_all(window_with_focus->view);
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_clear_selection(window_with_focus->view);
}

static gboolean invert_cb(ViewIter *iter, gpointer data)
{
	return !view_get_selected((ViewIface *) data, iter);
}

static void invert_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;

	view_select_if(window_with_focus->view, invert_cb,
		       window_with_focus->view);
}

void menu_show_options(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget *win;

	win = options_show();

	if (win)
	{
		number_of_windows++;
		g_signal_connect(win, "destroy",
				G_CALLBACK(one_less_window), NULL);
	}
}

static gchar *add_seqnum(const gchar *base) {
	gchar *ret = NULL;
	gboolean pass;
	int i;

	/* check exists names */
	for (i = 1; i <= 9999; i++) {
		pass = TRUE;

		g_free(ret);
		if (i == 1)
			ret = g_strdup_printf("%s", base);
		else
			ret = g_strdup_printf("%s%d", base, i);

		DirItem  *item;
		ViewIter iter;
		view_get_iter(window_with_focus->view, &iter, 0);
		while ((item = iter.next(&iter)))
			if (strcmp(item->leafname, ret) == 0)
			{
				pass = FALSE;
				break;
			}

		if (pass)
			break;

	}
	return ret;
}

static gboolean new_directory_cb(GObject *savebox,
				 const gchar *initial, const gchar *path)
{
	gchar *clean;		/* path without trailing slashes */
	gchar *ancestor;	/* deepest already-existing ancestor */
	gchar *top;		/* topmost created directory */
	const gchar *rest, *slash;

	g_return_val_if_fail(path != NULL, FALSE);

	/* Strip trailing slashes but keep a lone "/". */
	clean = g_strdup(path);
	while (strlen(clean) > 1 && clean[strlen(clean) - 1] == '/')
	{
		clean[strlen(clean) - 1] = '\0';
	}

	if (g_file_test(clean, G_FILE_TEST_EXISTS))
	{
		report_error(_("'%s' already exists"), clean);
		g_free(clean);
		return FALSE;
	}

	/* Find the deepest existing ancestor, so that the view can be
	 * notified at the topmost created directory. [20261001132555]
	 */
	ancestor = g_path_get_dirname(clean);
	while (!g_file_test(ancestor, G_FILE_TEST_EXISTS))
	{
		gchar *up = g_path_get_dirname(ancestor);
		if (!strcmp(up, ancestor))
		{
			g_free(up);
			break;
		}
		g_free(ancestor);
		ancestor = up;
	}

	if (g_mkdir_with_parents(clean,
			S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error(_("Error creating '%s': %s"),
				clean, g_strerror(errno));
		g_free(ancestor);
		g_free(clean);
		return FALSE;
	}

	if (!strcmp(ancestor, "/"))
	{
		rest = clean + 1;
	}
	else
	{
		rest = clean + strlen(ancestor) + 1;
	}
	slash = strchr(rest, '/');
	if (slash)
	{
		top = g_strndup(clean, slash - clean);
	}
	else
	{
		top = g_strdup(clean);
	}
	g_free(ancestor);
	g_free(clean);

	/* Check top path across all open filer windows
	 * and in the cache; all other cases no-op. */
	dir_check_this(top);

	if (filer_exists(window_with_focus))
	{
		const gchar *base = window_with_focus->sym_path;
		const gchar *first = NULL;
		gchar *shown = NULL;

		/* Wink the first tree node under the displayed directory: it
		 * necessarily exists and is the only new-tree node visible here.
		 * A tree created outside the displayed directory must not leave
		 * a stale pending select behind. [20261001132555]
		 */
		if (!strcmp(base, "/"))
		{
			if (top[0] == '/' && top[1] != '\0')
			{
				first = top + 1;
			}
		}
		else
		{
			gsize baselen = strlen(base);
			if (strlen(top) > baselen &&
					!strncmp(top, base, baselen) &&
					top[baselen] == '/' &&
					top[baselen + 1] != '\0')
			{
				first = top + baselen + 1;
			}
		}

		if (first)
		{
			const gchar *end = strchr(first, '/');
			if (end)
			{
				shown = g_strndup(top, end - top);
			}
			else
			{
				shown = g_strdup(top);
			}
		}

		if (shown && strcmp(shown, top))
		{
			dir_check_this(shown);
		}
		if (shown)
		{
			gchar *leaf = g_path_get_basename(shown);
			display_set_autoselect(window_with_focus, leaf);
			g_free(leaf);
			g_free(shown);
		}
	}
	g_free(top);

	return TRUE;
}

void show_new_directory(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	new_directory(NULL, 0, NULL);
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	gchar *leaf = add_seqnum(_("NewDir"));

	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, leaf),
		type_to_icon(inode_directory), new_directory_cb,
		GDK_ACTION_COPY);

	g_free(leaf);
}

static gboolean new_file_cb(GObject *savebox,
			    const gchar *initial, const gchar *path)
{
	int fd;

	fd = open(path, O_CREAT | O_EXCL, 0666);

	if (fd == -1)
	{
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));
		return FALSE;
	}

	if (close(fd))
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));

	dir_check_this(path);

	if (filer_exists(window_with_focus))
	{
		gchar	*leaf;
		leaf = strrchr(path, '/');
		if (leaf)
			display_set_autoselect(window_with_focus, leaf + 1);
	}

	return TRUE;
}

void show_new_file(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	new_file(NULL, 0, NULL);
}

static void new_file(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	gchar *leaf = add_seqnum(_("NewFile"));

	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, leaf),
		type_to_icon(text_plain),
		new_file_cb, GDK_ACTION_COPY);

	g_free(leaf);
}

static gboolean new_file_type_cb(GObject *savebox,
			         const gchar *initial, const gchar *path)
{
	const gchar *oleaf, *leaf;
	gchar *templ, *rtempl, *templ_dname, *dest, *base;
	GList *paths;

	/* We can work out the template path from the initial name */
	base = g_path_get_basename(initial);
	oleaf = base;
	templ_dname = choices_find_xdg_path_load("Templates", "", SITE);
	if (!templ_dname)
	{
		report_error(
		_("Error creating file: could not find the template for %s"),
				oleaf);
		return FALSE;
	}

	templ = g_strconcat(templ_dname, "/", oleaf, NULL);
	g_free(templ_dname);
	rtempl = pathdup(templ);
	g_free(templ);
	g_free(base);

	base = g_path_get_basename(path);
	dest = g_path_get_dirname(path);
	leaf = base;
	paths = g_list_append(NULL, rtempl);

	action_copy(paths, dest, leaf, TRUE);

	g_list_free(paths);
	g_free(dest);
	g_free(rtempl);
	g_free(base);

	if (filer_exists(window_with_focus))
		display_set_autoselect(window_with_focus, leaf);

	return TRUE;
}

static void do_send_to(gchar *templ)
{
	g_return_if_fail(send_to_paths != NULL);

	run_with_files(templ, send_to_paths, FALSE);
}

static void new_file_type(gchar *templ)
{
	const gchar *leaf;
	MIME_type *type;
	gchar *base;

	g_return_if_fail(window_with_focus != NULL);

	base = g_path_get_basename(templ);
	leaf = base;
	type = type_get_type(templ);

	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, leaf),
		type_to_icon(type),
		new_file_type_cb, GDK_ACTION_COPY);
	g_free(base);
}

/* Scope of one Customise action: which SendTo folder to open and how to
 * describe it. All strings owned by the struct [20260911082953].
 */
typedef struct {
	gchar *leaf;	/* e.g. ".text_html" — folder to open */
	gchar *media;	/* e.g. "text" (or "all") — display */
	gchar *subtype;	/* e.g. "html", or NULL — display */
} SendToScope;

/* Snapshot the (possibly static) MIME type into owned strings. */
gpointer make_sendto_scope(MIME_type *type)
{
	SendToScope *scope = g_new(SendToScope, 1);

	scope->leaf = g_strconcat(".",
			type && type->media_type ? type->media_type : "all",
			type && type->subtype ? "_" : NULL,
			type ? type->subtype : NULL, NULL);
	scope->media = g_strdup(type && type->media_type
			? type->media_type : "all");
	scope->subtype = g_strdup(type && type->subtype ? type->subtype : NULL);

	return scope;
}

void sendto_scope_free(gpointer data, GClosure *closure)
{
	SendToScope *scope = data;

	(void) closure;

	g_free(scope->leaf);
	g_free(scope->media);
	g_free(scope->subtype);
	g_free(scope);
}

/* "activate" carries (item, scope); the single-argument
 * customise_send_to() * must receive the scope, not the item
 * (g_signal_connect_data appends data * after the instance).
 */
void customise_send_to_item(GtkMenuItem *item, gpointer data)
{
	(void) item;

	customise_send_to(data);
}

/* Enforce 520x420 utility-window floor (never shrink). */
static void customise_floor_size(FilerWindow *fw)
{
	gint w, h;

	gtk_window_get_size(GTK_WINDOW(fw->window), &w, &h);
	if (w < 520 || h < 420)
		gtk_window_resize(GTK_WINDOW(fw->window),
				MAX(w, 520), MAX(h, 420));
}

/* State for one modeless Customise help dialog. The dialog
 * outlives the menu, so it owns a copy of the leaf, never the scope.
 */
typedef struct {
	gchar *leaf;		/* "" = SendTo root */
	gchar *dir;		/* Opened folder path, NULL until first open */
	GtkWidget *button;	/* Open/Close action widget */
	gboolean opened;
} CustomiseHelp;

static GtkWidget *customise_help_dialog = NULL;
static CustomiseHelp *customise_help = NULL;

static void customise_help_destroy(GtkWidget *dialog, gpointer data)
{
	CustomiseHelp *help = data;

	(void) dialog;

	customise_help_dialog = NULL;
	customise_help = NULL;
	g_free(help->leaf);
	g_free(help->dir);
	g_free(help);
}

/* Open (or re-open) the scope folder at the mouse, hidden-shown and floored.
 * Remembers the path so later presses can check it is still open.
 */
static void customise_help_open(GtkDialog *dialog, CustomiseHelp *help)
{
	gchar *dir = choices_find_xdg_path_save(help->leaf,
			"SendTo", SITE, TRUE);
	mkdir(dir, 0777);
	{
		FilerWindow *fw = filer_opendir(dir, window_with_focus,
				NULL, FALSE);
		if (fw)
		{
			display_set_hidden(fw, TRUE);
			customise_floor_size(fw);
			gtk_window_set_transient_for(GTK_WINDOW(dialog),
					GTK_WINDOW(fw->window));
		}
	}
	g_free(help->dir);
	help->dir = dir;
}

/* Build the help text for a scope; caller frees. */
static GString *customise_help_text(SendToScope *scope,
		const gchar *dirs, gboolean can_open)
{
	GString *text = g_string_new(NULL);

	/* Scope headline (leaf-split display): ".text_html" -> text/html,
	 * ".text" -> text files, ".all" -> all files. */
	if (scope)
	{
		if (scope->subtype)
			g_string_printf(text,
				_("Customising `Send To' menu for files of type %s/%s.\n"
				"Click the button to proceed to the SendTo/.%s_%s folder; "
				"symlink (Ctrl+Shift drag) applications into it "
				"to list them for these files only.\n\n"),
				scope->media, scope->subtype,
				scope->media, scope->subtype);
		else
			g_string_printf(text,
				_("Customising `Send To' menu for %s files.\n"
				"Click the button to proceed to the SendTo/.%s folder; "
				"symlink (Ctrl+Shift drag) applications into it "
				"to list them for these files.\n\n"),
				scope->media, scope->media);
	}

	g_string_append_printf(text,
		_("The `Send To' menu provides a quick way to send some files "
		"to an application. The applications listed are those in "
		"the following directories:\n\n%s\n%s\n"
		"The `Send To' menu may be opened by Shift+Menu clicking "
		"over a file.\n\n"
		"Advanced use:\n"
		"You can also create subdirectories called "
		"`.text_html', `.text', etc which will only be "
		"shown for files of that type and shared with the file menu. "
		"In addition, `.group' is shown only when multiple files are selected. "
		"`.all' is all."),
		dirs,
		can_open ? _("I'll show you your SendTo directory now; you should "
			"symlink (Ctrl+Shift drag) any applications you want "
			"into it.")
		     : _("Your CHOICESPATH variable setting prevents "
			 "customisations - sorry."));

	return text;
}

static void customise_help_response(GtkDialog *dialog, gint response,
		gpointer data)
{
	CustomiseHelp *help = data;

	if (response == GTK_RESPONSE_OK && !help->opened)
	{
		customise_help_open(dialog, help);

		help->opened = TRUE;
		gtk_button_set_label(GTK_BUTTON(help->button), _("Close"));
		return;
	}

	if (response == GTK_RESPONSE_OK && help->dir &&
			!find_filer_window(help->dir, NULL))
	{
		/* The folder was closed meanwhile: reopen. */
		customise_help_open(dialog, help);
		return;
	}

	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void customise_directory_menu(gpointer data)
{
	char *path;
	char *leaf = g_strconcat(".", inode_directory->media_type, NULL);

	path = choices_find_xdg_path_save(leaf, "SendTo", SITE, TRUE);
	g_free(leaf);

	mkdir(path, 0755);
	/* At the mouse, with hidden files shown and a usable minimum size
	 * [20260911082953]. */
	{
		FilerWindow *fw = filer_opendir(path, window_with_focus,
				NULL, FALSE);
		if (fw)
		{
			display_set_hidden(fw, TRUE);
			customise_floor_size(fw);
		}
	}
	g_free(path);

	info_message(
			_("Symlink any programs you want into this directory. \n\n"
			"Tip: Directories and `Set Icon' may make it more useful."));
}

void show_menu_new(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	ensure_filer_menu();
	update_new_files_menu();
	show_popup_menu(filer_new_menu, NULL, 1);
}

/* Unified SendTo customise action [20260911082953]: one entry everywhere.
 * 'data' is a SendToScope (see make_sendto_scope), or NULL for the SendTo
 * root. Always opens the narrower scope folder, with hidden files shown, at
 * the mouse and at least 520x420.
 */
void customise_send_to(gpointer data)
{
	SendToScope	*scope = data;
	GPtrArray	*path;
	gchar		*save;
	gboolean	can_open;
	GString		*dirs;
	GString		*text;
	GtkWidget	*dialog;
	CustomiseHelp	*help;
	gchar		*open_leaf;
	gint		i;

	dirs = g_string_new(NULL);

	path = choices_list_xdg_dirs("", SITE);
	for (i = 0; i < path->len; i++)
	{
		gchar *old = (gchar *) path->pdata[i];

		g_string_append(dirs, old);
		g_string_append(dirs, "/SendTo\n");
	}
	choices_free_list(path);

	save = choices_find_xdg_path_save("", "SendTo", SITE, TRUE);
	if (save)
		mkdir(save, 0777);
	can_open = save != NULL;

	/* Leaf keeps its leading dot (".text_html"); "" means the SendTo root. */
	open_leaf = scope ? scope->leaf : NULL;

	text = customise_help_text(scope, dirs->str, can_open);

	g_string_free(dirs, TRUE);
	g_free(save);

	/* Singleton: refresh to the new scope and re-present at the mouse *
	 * rather than stacking dialogs [20260911082953]. * WIN_POS_MOUSE must
	 * be stored before show: the constraint applies * at map time only.
	 */
	if (customise_help_dialog)
	{
		GtkWindow *win = GTK_WINDOW(customise_help_dialog);
		const gchar *new_leaf = open_leaf ? open_leaf : "";

		if (g_strcmp0(customise_help->leaf, new_leaf) != 0)
		{
			GtkWidget *area;
			GList *kids;

			g_free(customise_help->leaf);
			customise_help->leaf = g_strdup(new_leaf);
			customise_help->opened = FALSE;
			gtk_button_set_label(
				GTK_BUTTON(customise_help->button),
				can_open ? _("Open") : _("Close"));
			if (!can_open)
				customise_help->opened = TRUE;

			area = gtk_message_dialog_get_message_area(
					GTK_MESSAGE_DIALOG(
						customise_help_dialog));
			kids = gtk_container_get_children(
					GTK_CONTAINER(area));
			if (kids)
			{
				gtk_label_set_text(GTK_LABEL(kids->data),
						text->str);
				g_list_free(kids);
			}
		}

		if (window_with_focus)
			gtk_window_set_transient_for(win,
				GTK_WINDOW(window_with_focus->window));

		gtk_widget_hide(customise_help_dialog);
		gtk_window_deiconify(win);
		gtk_window_set_position(win, GTK_WIN_POS_MOUSE);
		gtk_widget_show(customise_help_dialog);
		gtk_window_present(win);

		g_string_free(text, TRUE);
		return;
	}

	help = g_new(CustomiseHelp, 1);
	help->leaf = g_strdup(open_leaf ? open_leaf : "");
	help->opened = FALSE;

	dialog = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_INFO,
			GTK_BUTTONS_NONE, "%s", text->str);
	g_string_free(text, TRUE);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Customise `Send To' Menu"));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	if (window_with_focus)
		gtk_window_set_transient_for(GTK_WINDOW(dialog),
				GTK_WINDOW(window_with_focus->window));

	help->button = gtk_dialog_add_button(GTK_DIALOG(dialog),
			save ? _("Open") : _("Close"), GTK_RESPONSE_OK);
	if (!save)
		help->opened = TRUE;
	gtk_widget_show(help->button);

	g_signal_connect(dialog, "response",
			G_CALLBACK(customise_help_response), help);
	g_signal_connect(dialog, "destroy",
			G_CALLBACK(customise_help_destroy), help);

	customise_help_dialog = dialog;
	customise_help = help;
	gtk_widget_show(dialog);
}

static void customise_new(gpointer data)
{
	GPtrArray	*path;
	gchar		*save;
	GString		*dirs;
	int		i;

	dirs = g_string_new(NULL);

	path = choices_list_xdg_dirs("", SITE);
	for (i = 0; i < path->len; i++)
	{
		gchar *old = (gchar *) path->pdata[i];

		g_string_append(dirs, old);
		g_string_append(dirs, "/Templates\n");
	}
	choices_free_list(path);

	save = choices_find_xdg_path_save("", "Templates", SITE, TRUE);
	if (save)
		mkdir(save, 0777);

	info_message(
		_("Any files placed in your Templates directories will "
		"appear on the `New' menu. Choosing one of them will make "
		"a copy of it as the new file.\n\n"
		"The following directories contain templates:\n\n%s\n%s\n"),
		dirs->str,
		save ? _("I'll show you your Templates directory now; you "
			 "should place any template files you want inside it.")
		     : _("Your CHOICESPATH variable setting prevents "
			 "customisations - sorry."));

	g_string_free(dirs, TRUE);

	if (save)
		filer_opendir(save, NULL, NULL, FALSE);
}

/* Add everything in the directory <Choices>/SendTo/[.type[_subtype]]
 * to the menu.
 */
static GList *add_sendto_shared(GtkWidget *menu,
		const gchar *type, const gchar *subtype, CallbackFn swapped_func)
{
	gchar *searchdir;
	GPtrArray *paths;
	GList *widgets = NULL;
	int i;

	if (subtype)
		searchdir = g_strdup_printf("SendTo/.%s_%s", type, subtype);
	else if (type)
		searchdir = g_strdup_printf("SendTo/.%s", type);
	else
		searchdir = g_strdup("SendTo");

	paths = choices_list_xdg_dirs(searchdir, SITE);
	g_free(searchdir);

	for (i = 0; i < paths->len; i++)
	{
		gchar	*dir = (gchar *) paths->pdata[i];

		widgets = g_list_concat(widgets,
				menu_from_dir(menu, dir, get_menu_icon_pixels(),
					swapped_func, FALSE, FALSE, TRUE)
			);
	}

	choices_free_list(paths);
	return widgets;
}
static void add_sendto(GList **list, const gchar *type, const gchar *subtype)
{
	GList *new = add_sendto_shared(NULL, type, subtype, (CallbackFn) do_send_to);
	if (!new) return;
	if (*list)
		new = g_list_prepend(new, gtk_separator_menu_item_new());
	*list = g_list_concat(*list, new);
}


MIME_type *menu_selection_type(FilerWindow *fw)
{
	MIME_type *type=NULL;
	gboolean same=TRUE, same_media=TRUE;

	ViewIter iter;
	DirItem *item;
	view_get_iter(fw->view, &iter, VIEW_ITER_SELECTED);
	while ((item = iter.next(&iter)))
	{
		if (item->mime_type == NULL)
		{
			same = same_media = FALSE;
			break;
		}

		if(!type)
			type=item->mime_type;
		else
		{
			if(type!=item->mime_type)
			{
				same=FALSE;
				if(strcmp(type->media_type,
					  item->mime_type->media_type)!=0)
				{
					same_media=FALSE;
					break;
				}
			}
		}
	}

	static MIME_type ret;
	ret.media_type = ret.subtype = NULL;
	if(type)
	{
		if(same)
			ret.subtype    = type->subtype;
		if(same_media)
			ret.media_type = type->media_type;
	}
	return &ret;
}

/* Don't free paths. With include_generic, appends the generic (MIME-less)
 * SendTo root, unifying the inline submenu and the detached popup content
 * [20260911082953].
 */
GList *menu_sendto_for_type(GList *paths, MIME_type *type,
				gboolean include_generic)
{
	GList *ret = NULL;
	if (!paths)
		;
	if (!paths->next)
	{
		add_sendto(&ret, type->media_type, type->subtype);
		add_sendto(&ret, type->media_type, NULL);
	}
	else
	{
		if(type->subtype)
			add_sendto(&ret, type->media_type, type->subtype);
		if(type->media_type)
			add_sendto(&ret, type->media_type, NULL);

		add_sendto(&ret, "group", NULL);
	}

	add_sendto(&ret, "all", NULL);

	if (include_generic)
		add_sendto(&ret, NULL, NULL);

	if (send_to_paths) destroy_glist(&send_to_paths);
	send_to_paths = paths;
	return ret;
}

/* Scan the SendTo dir and create and show the Send To menu.
 * The 'paths' list and every path in it is claimed, and will be
 * freed later -- don't free it yourself!
 */
static void show_send_to_menu(FilerWindow *fw, GdkEvent *event)
{
	GtkWidget	*menu, *item;
	MIME_type	*type;

	menu = gtk_menu_new();

	type = menu_selection_type(fw);
	GList *list = menu_sendto_for_type(
			filer_selected_items(fw), type, TRUE);

	gboolean have_entries = (list != NULL);
	for (; list; list = g_list_delete_link(list, list))
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), list->data);

	/* Divider above Customise when entries exist [20260921093115]. */
	if (have_entries)
		gtk_menu_shell_append(GTK_MENU_SHELL(menu),
				gtk_separator_menu_item_new());

	/* Scope-owned by the closure, like the submenu entry [20260911082953]. */
	item = menu_placeholder_item(_("Customise..."));
	g_signal_connect_data(item, "activate",
			G_CALLBACK(customise_send_to_item), make_sendto_scope(type),
			(GClosureNotify) sendto_scope_free, 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	g_signal_connect(menu, "selection-done", G_CALLBACK(menu_closed), NULL);
	g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);

	popup_menu = menu;
	show_popup_menu(menu, event, 0);
}

static void show_dir_send_to_menu(GdkEvent *event)
{
	GtkWidget	*menu, *item;

	menu = gtk_menu_new();

	GList *widgets = add_sendto_shared(menu,
			inode_directory->media_type, NULL, (CallbackFn) directory_cb);
	if (widgets)
	{
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		g_list_free(widgets);
	}

	item = menu_placeholder_item(_("Customise Dir menu..."));
	g_signal_connect_swapped(item, "activate",
				G_CALLBACK(customise_directory_menu), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	g_signal_connect(menu, "selection-done", G_CALLBACK(menu_closed), NULL);
	g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);

	popup_menu = menu;
	show_popup_menu(menu, event, 0);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	const char *argv[] = {"sh", "-c", NULL, NULL};
	gboolean close = action;

	argv[2] = o_menu_xterm.value;

	g_return_if_fail(window_with_focus != NULL);

	if (rox_spawn(window_with_focus->sym_path, argv) && close)
		gtk_widget_destroy(window_with_focus->window);
}

static void home_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_change_to(window_with_focus, home_dir, NULL);
}

static void show_bookmarks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	bookmarks_show_menu(window_with_focus, NULL);
}

static void show_log(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	log_show_window();
}

static void follow_symlinks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (strcmp(window_with_focus->real_path, window_with_focus->sym_path))
		filer_change_to(window_with_focus,
				window_with_focus->real_path, NULL);
	else
		delayed_error(_("This is already the canonical name "
				"for this directory."));
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_open_parent(window_with_focus);
}

static void open_parent_same(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	change_to_parent(window_with_focus);
}

static void resize(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	view_autosize(window_with_focus->view, TRUE);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (o_unique_filer_windows.int_value)
	{
		report_error(_("You can't open a second view onto "
			"this directory because the `Unique Windows' option "
			"is turned on in the Options window."));
	}
	else
		filer_opendir(window_with_focus->sym_path, window_with_focus, NULL, FALSE);
}

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (!filer_window_delete(window_with_focus->window, NULL,
				 window_with_focus))
		gtk_widget_destroy(window_with_focus->window);
}

static void mini_buffer(gpointer data, guint action, GtkWidget *widget)
{
	MiniType type = (MiniType) action;

	g_return_if_fail(window_with_focus != NULL);

	/* Item needs to remain selected... */
	if (type == MINI_SHELL)
		window_with_focus->temp_item_selected = FALSE;

	minibuffer_show(window_with_focus, type, 0);
}

void menu_rox_help(gpointer data, guint action, GtkWidget *widget)
{
	if (action == HELP_ABOUT)
		infobox_new(app_dir);
	else if (action == HELP_DIR)
		filer_opendir(make_path(app_dir, "Help"), NULL, NULL, FALSE);
	else if (action == HELP_MANUAL)
	{
		gchar *manual = NULL;

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
			manual = g_strconcat(app_dir,
						"/Help/Manual.html", NULL);

		run_by_path(manual);

		g_free(manual);
	}
	else
		g_warning("Unknown help action %d\n", action);
}

/* Set n items from position 'from' in 'menu' to the 'shaded' state */
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n)
{
	GList	*items, *item;

	items = gtk_container_get_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_WIDGET(item->data), !shaded);
		item = item->next;
	}
	g_list_free(items);
}

static void save_menus(void)
{
	char *menurc = choices_find_xdg_path_save(MENUS_NAME, project_name, SITE, TRUE);
	if (menurc)
	{
		gtk_accel_map_save(menurc);
		g_free(menurc);
		menus_file_existed = TRUE;
	}
}

enum {
	COL_LABEL,
	COL_IS_SECTION,
	COL_ACCEL_KEY,
	COL_ACCEL_MODS,
	COL_ACCEL_PATH,
	N_COLUMNS
};

static const struct {
	const char *path;
	guint       key;
	GdkModifierType mods;
} factory_defaults[] = {
	{ "<filer>/Display/Bigger Icons",      GDK_KEY_plus,     0 },
	{ "<filer>/Display/Smaller Icons",     GDK_KEY_minus,    0 },
	{ "<filer>/Display/Automatic",         GDK_KEY_equal,    0 },
	{ "<filer>/Display/Show Hidden",       GDK_KEY_h,        GDK_CONTROL_MASK },
	{ "<filer>/File/Copy",                 GDK_KEY_c,        GDK_CONTROL_MASK },
	{ "<filer>/File/Cut",                  GDK_KEY_x,        GDK_CONTROL_MASK },
	{ "<filer>/File/Duplicate...",         GDK_KEY_d,        GDK_CONTROL_MASK },
	{ "<filer>/File/Delete",               GDK_KEY_Delete,   0 },
	{ "<filer>/File/Set Run Action...",    GDK_KEY_asterisk, 0 },
	{ "<filer>/File/Properties",           GDK_KEY_p,        GDK_CONTROL_MASK },
	{ "<filer>/File/Find",                 GDK_KEY_f,        GDK_CONTROL_MASK },
	{ "<filer>/Select/Select All",         GDK_KEY_a,        GDK_CONTROL_MASK },
	{ "<filer>/Select/Select by Name...",  GDK_KEY_period,   0 },
	{ "<filer>/Select/Reg Select...",      GDK_KEY_asciicircum, 0 },
	{ "<filer>/Select/Select If...",       GDK_KEY_question, GDK_SHIFT_MASK },
	{ "<filer>/Paste",                     GDK_KEY_v,        GDK_CONTROL_MASK },
	{ "<filer>/Window/Home Directory",     GDK_KEY_Home,     GDK_CONTROL_MASK },
	{ "<filer>/Window/Show Bookmarks",     GDK_KEY_b,        GDK_CONTROL_MASK },
	{ "<filer>/Window/Resize Window",      GDK_KEY_e,        GDK_CONTROL_MASK },
	{ "<filer>/Window/Close Window",       GDK_KEY_q,        GDK_CONTROL_MASK },
	{ "<filer>/Enter Path...",             GDK_KEY_slash,    0 },
	{ "<filer>/Shell Command...",          GDK_KEY_exclam,   GDK_SHIFT_MASK },
	{ "<filer>/Terminal Here",             GDK_KEY_grave,    0 },
	{ "<filer>/Help/Show Help Files",      GDK_KEY_F1,       0 },
	{ NULL, 0, 0 }
};

static void snapshot_take(void)
{
	if (shortcuts_snapshot)
		g_hash_table_destroy(shortcuts_snapshot);
	shortcuts_snapshot = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	gtk_accel_map_foreach(NULL, accel_foreach_cb);
}

static void accel_foreach_cb(gpointer data, const gchar *accel_path,
                             guint accel_key, GdkModifierType accel_mods,
                             gboolean changed)
{
	AccelSnapshot *entry;
	entry = g_new(AccelSnapshot, 1);
	entry->accel_key = accel_key;
	entry->accel_mods = accel_mods;
	g_hash_table_insert(shortcuts_snapshot,
			g_strdup(accel_path), entry);
}

static void snapshot_restore_entry(gpointer key, gpointer value, gpointer data)
{
	AccelSnapshot *entry = (AccelSnapshot *) value;
	gtk_accel_map_change_entry(
			(const gchar *) key,
			entry->accel_key,
			entry->accel_mods,
			TRUE);
}

static void snapshot_free(void)
{
	if (shortcuts_snapshot)
	{
		g_hash_table_destroy(shortcuts_snapshot);
		shortcuts_snapshot = NULL;
	}
}

static gboolean anything_changed(void)
{
	GHashTableIter iter;
	gpointer key, value;
	GtkAccelKey current;

	if (!shortcuts_snapshot)
		return FALSE;
	g_hash_table_iter_init(&iter, shortcuts_snapshot);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		AccelSnapshot *snap = (AccelSnapshot *) value;
		if (!gtk_accel_map_lookup_entry(
				(const gchar *) key, &current))
			return TRUE;
		if (current.accel_key != snap->accel_key ||
		    current.accel_mods != snap->accel_mods)
			return TRUE;
	}
	return FALSE;
}

/*
 * Return the item's label text even when the child is not a bare GtkLabel:
 * make_send_to_item() packs [image, label] into a horizontal box.
 */
static const gchar *menu_item_label_text(GtkMenuItem *item)
{
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));

	if (GTK_IS_LABEL(child))
		return gtk_label_get_text(GTK_LABEL(child));

	if (GTK_IS_CONTAINER(child))
	{
		GList *l;
		GList *children =
			gtk_container_get_children(GTK_CONTAINER(child));
		GtkWidget *found = NULL;

		for (l = children; l && !found; l = l->next)
		{
			if (GTK_IS_LABEL(l->data))
				found = l->data;
		}

		g_list_free(children);

		if (found)
			return gtk_label_get_text(GTK_LABEL(found));
	}

	return NULL;
}

static void walk_menu_items(GtkWidget *menu, GtkTreeIter *parent,
                            GtkTreeStore *store)
{
	GList *children, *l;
	GtkTreeIter iter;

	children = gtk_container_get_children(GTK_CONTAINER(menu));
	for (l = children; l; l = l->next)
	{
		GtkWidget *child = GTK_WIDGET(l->data);
		GtkWidget *submenu;
		const gchar *accel_path;
		const gchar *label;
		GtkAccelKey key;
		guint accel_key = 0;
		GdkModifierType accel_mods = 0;

		if (GTK_IS_SEPARATOR_MENU_ITEM(child))
			continue;
		if (!GTK_IS_MENU_ITEM(child))
			continue;

		/* Hide dot-inode tail from shortcut editor only; filer menu
		 * keeps it for right-click [20260901075838]. Top-level only;
		 * skipping the parent hides its entire subtree.
		 */
		if (!parent && menu == filer_menu &&
				g_object_get_data(G_OBJECT(child),
					"is_inode_sendto"))
			continue;

		submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(child));
		if (submenu)
		{
			GtkTreeIter section_iter;

			label = menu_item_label_text(GTK_MENU_ITEM(child));
			gtk_tree_store_append(store, &section_iter, parent);
			gtk_tree_store_set(store, &section_iter,
				COL_LABEL, label,
				COL_IS_SECTION, TRUE,
				COL_ACCEL_KEY, 0,
				COL_ACCEL_MODS, 0,
				COL_ACCEL_PATH, NULL,
				-1);
			walk_menu_items(submenu, &section_iter, store);
			continue;
		}

		label = menu_item_label_text(GTK_MENU_ITEM(child));
		if (!label || label[0] == '\0')
			continue;

		accel_path = gtk_menu_item_get_accel_path(
				GTK_MENU_ITEM(child));
		if (accel_path)
		{
			if (gtk_accel_map_lookup_entry(
					accel_path, &key))
			{
				accel_key = key.accel_key;
				accel_mods = key.accel_mods;
			}
		}

		gtk_tree_store_append(store, &iter, parent);
		gtk_tree_store_set(store, &iter,
			COL_LABEL, label,
			COL_IS_SECTION, FALSE,
			COL_ACCEL_PATH, accel_path,
			COL_ACCEL_KEY, accel_key,
			COL_ACCEL_MODS, accel_mods,
			-1);
	}
	g_list_free(children);
}

static void populate_from_accel_map(void)
{
	if (!shortcuts_store)
		return;

	gtk_tree_store_clear(shortcuts_store);

	ensure_filer_menu();
	walk_menu_items(filer_menu, NULL, shortcuts_store);
}

static void revert_shortcuts(GtkWidget *button, gpointer data)
{
	g_hash_table_foreach(shortcuts_snapshot, snapshot_restore_entry, NULL);
	populate_from_accel_map();

	if (menus_file_existed)
	{
		save_menus();
	}
}

/* gtk_tree_model_foreach callback: clear the shortcut of a displayed
 * leaf that currently holds a shortcut. Section rows and pathless
 * leaves are ignored (e.g., SendTo items without a stable path).
 */
static gboolean clear_leaf_shortcut(GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter *iter, gpointer data)
{
	gboolean is_section;
	guint accel_key;
	gchar *accel_path = NULL;

	gtk_tree_model_get(model, iter,
		COL_IS_SECTION, &is_section,
		COL_ACCEL_KEY, &accel_key,
		COL_ACCEL_PATH, &accel_path,
		-1);
	if (!is_section && accel_path && accel_key != 0)
	{
		gtk_accel_map_change_entry(accel_path,
			0, (GdkModifierType) 0, TRUE);
	}
	g_free(accel_path);

	return FALSE;
}

static void reset_to_factory(GtkWidget *button, gpointer data)
{
	gint i;

	if (!shortcuts_store)
	{
		return;
	}
	gtk_tree_model_foreach(GTK_TREE_MODEL(shortcuts_store),
		clear_leaf_shortcut, NULL);

	for (i = 0; factory_defaults[i].path; i++)
	{
		gtk_accel_map_change_entry(
			factory_defaults[i].path,
			factory_defaults[i].key,
			factory_defaults[i].mods,
			TRUE);
	}
	populate_from_accel_map();

	if (menus_file_existed)
	{
		save_menus();
	}
}

static void shortcuts_dialog_destroyed(GtkWidget *widget, gpointer data)
{
	if (anything_changed())
		save_menus();
	snapshot_free();
	shortcuts_store = NULL;
	shortcuts_dialog = NULL;
}

static void on_accel_edited(GtkCellRendererAccel *renderer,
                            const gchar *path_str,
                            guint accel_key,
                            GdkModifierType accel_mods,
                            guint hardware_keycode,
                            gpointer user_data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(user_data);
	GtkTreePath *path;
	GtkTreeIter iter;
	gchar *accel_path;

	path = gtk_tree_path_new_from_string(path_str);
	if (!gtk_tree_model_get_iter(model, &iter, path))
	{
		gtk_tree_path_free(path);
		return;
	}
	gtk_tree_model_get(model, &iter,
		COL_ACCEL_PATH, &accel_path, -1);
	if (accel_path)
	{
		gtk_accel_map_change_entry(accel_path,
			accel_key, accel_mods, TRUE);
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
			COL_ACCEL_KEY, accel_key,
			COL_ACCEL_MODS, accel_mods,
			-1);
		g_free(accel_path);
	}
	gtk_tree_path_free(path);
}

static void on_accel_cleared(GtkCellRendererAccel *renderer,
                             const gchar *path_str,
                             gpointer user_data)
{
	GtkTreeModel *model = GTK_TREE_MODEL(user_data);
	GtkTreePath *path;
	GtkTreeIter iter;
	gchar *accel_path;

	path = gtk_tree_path_new_from_string(path_str);
	if (!gtk_tree_model_get_iter(model, &iter, path))
	{
		gtk_tree_path_free(path);
		return;
	}
	gtk_tree_model_get(model, &iter,
		COL_ACCEL_PATH, &accel_path, -1);
	if (accel_path)
	{
		gtk_accel_map_change_entry(accel_path,
			0, (GdkModifierType) 0, TRUE);
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
			COL_ACCEL_KEY, 0,
			COL_ACCEL_MODS, 0,
			-1);
		g_free(accel_path);
	}
	gtk_tree_path_free(path);
}

static void menu_item_data_func(GtkTreeViewColumn *tree_column,
                                GtkCellRenderer *renderer,
                                GtkTreeModel *tree_model,
                                GtkTreeIter *iter,
                                gpointer data)
{
	gboolean is_section;
	gchar *label;

	gtk_tree_model_get(tree_model, iter,
		COL_IS_SECTION, &is_section,
		COL_LABEL, &label,
		-1);

	g_object_set(renderer,
		"text", label,
		"weight", is_section ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		"xalign", 0.0,
		NULL);
	g_free(label);
}

static void shortcuts_dialog_show(void)
{
	GtkWidget *vbox, *scrolled, *tree_view;
	GtkWidget *actions, *button;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	if (shortcuts_dialog)
	{
		gtk_window_present(GTK_WINDOW(shortcuts_dialog));
		return;
	}

	snapshot_take();

	shortcuts_store = gtk_tree_store_new(N_COLUMNS,
		G_TYPE_STRING, G_TYPE_BOOLEAN,
		G_TYPE_UINT, G_TYPE_UINT,
		G_TYPE_STRING);

	shortcuts_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(shortcuts_dialog),
			_("Keyboard Shortcuts"));
	gtk_window_set_position(GTK_WINDOW(shortcuts_dialog),
			GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(
			GTK_CONTAINER(shortcuts_dialog), 4);
	gtk_window_set_default_size(
			GTK_WINDOW(shortcuts_dialog), 500, 400);
	g_signal_connect(shortcuts_dialog, "destroy",
			G_CALLBACK(shortcuts_dialog_destroyed), NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

	/* Preamble label */
	GtkWidget *preamble = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(preamble),
		_("<b>Custom Shortcut Editor</b>\n\n"
		"This sandbox live-tests your changes. "
		"New shortcuts take effect immediately "
		"in filer windows so you can test them "
		"while this dialog remains open, but are "
		"saved permanently only after you close it.\n\n"
		"To set a shortcut, navigate to the desired "
		"menu entry, double-click the <b>Shortcut</b> "
		"cell, and press your key combination. "
		"The <b>Revert</b> button restores shortcuts "
		"to their state before opening this dialog, "
		"while <b>Reset to Defaults</b> erases all "
		"user customizations."));
	gtk_label_set_line_wrap(GTK_LABEL(preamble), TRUE);
	gtk_label_set_xalign(GTK_LABEL(preamble), 0.0);
	gtk_box_pack_start(GTK_BOX(vbox), preamble,
			FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(
			GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	tree_view = gtk_tree_view_new_with_model(
			GTK_TREE_MODEL(shortcuts_store));
	g_object_unref(shortcuts_store);
	gtk_tree_view_set_headers_visible(
			GTK_TREE_VIEW(tree_view), TRUE);
	gtk_tree_view_set_activate_on_single_click(
			GTK_TREE_VIEW(tree_view), TRUE);

	selection = gtk_tree_view_get_selection(
			GTK_TREE_VIEW(tree_view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

	/* "Menu Item" column — text with bold for section headers */
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(
			_("Menu Item"), renderer, NULL);
	gtk_tree_view_column_set_cell_data_func(column, renderer,
			menu_item_data_func, NULL, NULL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

	/* "Shortcut" column — editable accelerator */
	renderer = gtk_cell_renderer_accel_new();
	g_object_set(renderer,
			"accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER,
			"editable", TRUE,
			"mode", GTK_CELL_RENDERER_MODE_EDITABLE,
			NULL);
	g_signal_connect(renderer, "accel-edited",
			G_CALLBACK(on_accel_edited), shortcuts_store);
	g_signal_connect(renderer, "accel-cleared",
			G_CALLBACK(on_accel_cleared), shortcuts_store);
	column = gtk_tree_view_column_new_with_attributes(
			_("Shortcut"), renderer,
			"accel-key", COL_ACCEL_KEY,
			"accel-mods", COL_ACCEL_MODS,
			NULL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

	gtk_container_add(GTK_CONTAINER(scrolled), tree_view);
	gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

	actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(actions),
			GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(actions), 10);

	button = button_new_mixed("edit-undo", _("_Revert"));
	g_signal_connect(button, "clicked",
			G_CALLBACK(revert_shortcuts), NULL);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);

	button = button_new_mixed("edit-clear", _("Reset to _Defaults"));
	g_signal_connect(button, "clicked",
			G_CALLBACK(reset_to_factory), NULL);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);

	button = button_new_mixed("window-close", _("_Close"));
	gtk_widget_set_can_default(button, TRUE);
	g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(gtk_widget_destroy),
			shortcuts_dialog);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(shortcuts_dialog), vbox);
	gtk_widget_grab_default(button);

	populate_from_accel_map();

	rox_dialog_set_parent_full(shortcuts_dialog, FALSE);
	gtk_widget_show_all(shortcuts_dialog);
}

static void select_nth_item(GtkMenuShell *shell, int n)
{
	GList	  *items;
	GtkWidget *item;

	items = gtk_container_get_children(GTK_CONTAINER(shell));
	item = g_list_nth_data(items, n);

	g_return_if_fail(item != NULL);

	g_list_free(items);

	if (gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)))
	{
		/* Submenu item: prelight it without opening the submenu. Emitting
		 * "select" (gtk_menu_item_select) would run gtk_real_menu_item_select,
		 * which schedules a submenu popup (_gtk_menu_item_popup_submenu) and,
		 * from this non-event context, the popup would then warn "no trigger
		 * event". Set the prelight state directly instead.
		 *
		 * Also register the item as a navigation seed so that the first
		 * Up/Down moves relative to it instead of falling back to
		 * select-first while this hand-painted highlight lingers
		 * [20260825065744].
		 */
		gtk_menu_shell_deselect(shell);
		gtk_widget_set_state_flags(item, GTK_STATE_FLAG_PRELIGHT, FALSE);
		gtk_widget_queue_draw(item);
		g_object_set_data(G_OBJECT(shell), "rox-seed-item", item);
	}
	else
	{
		/* Leaf item: full select so it becomes the active item and can be
		 * deselected again when the pointer hovers a different entry. */
		g_object_set_data(G_OBJECT(shell), "rox-seed-item", NULL);
		gtk_menu_shell_select_item(shell, item);
	}
}

/* Consume the first Up/Down after a painted-only pre-select: move the
 * real shell selection relative to the seeded item (skipping separators
 * and insensitive entries), clear the hand-painted prelight, and halt
 * the class handler's select-first fallback. Later movements pass
 * through untouched.
 */
static void menu_seed_move_current(GtkWidget *widget,
		GtkMenuDirectionType dir, gpointer data)
{
	GtkMenuShell	*shell = GTK_MENU_SHELL(widget);
	GtkWidget	*seed = g_object_get_data(G_OBJECT(widget),
						"rox-seed-item");
	GList		*children, *node, *want = NULL;
	GtkWidget	*target = NULL;
	gboolean	 forward = dir == GTK_MENU_DIR_NEXT;

	if (!seed || (dir != GTK_MENU_DIR_NEXT && dir != GTK_MENU_DIR_PREV))
		return;		/* not seeding / Left-Right unaffected */

	children = gtk_container_get_children(GTK_CONTAINER(shell));
	node = g_list_find(children, seed);
	if (node)
	{
		/* Menus can contain plain widgets (see appmenu/Send-To
		 * additions); only real, enabled, shown items qualify. */
		want = forward ? node->next : node->prev;
		while (want &&
		       (!GTK_IS_MENU_ITEM(want->data) ||
			GTK_IS_SEPARATOR_MENU_ITEM(want->data) ||
			!gtk_widget_get_sensitive(GTK_WIDGET(want->data)) ||
			!gtk_widget_get_visible(GTK_WIDGET(want->data))))
			want = forward ? want->next : want->prev;
	}
	/* Keep the target widget, not the list position: the list is
	 * freed here, and dereferencing `want' afterwards would read
	 * recycled GList nodes [20260825065744]. */
	target = want ? GTK_WIDGET(want->data) : NULL;
	g_list_free(children);

	gtk_widget_unset_state_flags(seed, GTK_STATE_FLAG_PRELIGHT);
	g_object_set_data(G_OBJECT(widget), "rox-seed-item", NULL);

	if (target)
	{
		gtk_menu_shell_select_item(shell, target);
		/* Void signal: stop the class handler's select-first. */
		g_signal_stop_emission_by_name(widget, "move-current");
	}
}

static void clipboard_get(GtkClipboard *clipboard,
		GtkSelectionData *selection_data, guint info, gpointer user_data)
{
	if (!selected_paths) return;
	bool gnome = info == TARGET_GNOME_COPIED_FILES;
	if (!gnome && info != TARGET_URI_LIST) return;

	GString *data = g_string_new(gnome ? clipboard_action : "");
	const char *n = gnome ? "\r\n" : "\n";

	for (GList *next = selected_paths; next; next = next->next)
	{
		EscapedPath *uri = encode_path_as_uri(next->data);
		g_string_append_printf(data, "%s%s", (char *) uri, n);
		g_free(uri);
	}

	gtk_selection_data_set(selection_data,
			gnome ? gnome_copied_files : text_uri_list,
			8, (const guchar *) data->str, data->len);

	g_string_free(data, TRUE);
}

static void clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
	if (!selected_paths)
		return;

	destroy_glist(&selected_paths);
}

static void paste_from_clipboard(gpointer data, guint action, GtkWidget *unused)
{
	const gchar *error = NULL;
	gboolean ignore_no_local_paths = FALSE;

	GtkSelectionData *selection =
				gtk_clipboard_wait_for_contents(clipboard, gnome_copied_files) ?:
				gtk_clipboard_wait_for_contents(clipboard, text_uri_list);
	if (!selection)
	{
		delayed_error(_("The clipboard is empty."));
		return;
	}

	char **uri_list = gtk_selection_data_get_uris(selection);
	if (!uri_list)
	{
		char *tmp = g_strndup((const gchar *) gtk_selection_data_get_data(selection), gtk_selection_data_get_length(selection));
		uri_list = g_strsplit_set(tmp, "\r\n", -1);
		g_free(tmp);
	}

	/* Either one local URI, or a list. If everything in the list
	* isn't local then we are stuck.
	*/

	GQueue gq = G_QUEUE_INIT;
	for (gchar **uri_iter = uri_list + 1; *uri_iter; uri_iter++)
	{
		if (**uri_iter == '\0') continue;

		char *path = get_local_path((EscapedPath *) *uri_iter);
		if (path)
		{
			gchar *source_real_path = pathdup(path);
			gchar *source_dirname = g_path_get_dirname(source_real_path);
			if (strcmp(source_dirname, window_with_focus->real_path) == 0 &&
				strcmp(*uri_list, "cut") != 0)
			{
				gchar *source_basename = g_path_get_basename(path);
				gchar *new_name = NULL;
				GList *one_path = NULL;

				ignore_no_local_paths = TRUE;
				one_path = g_list_append(one_path, path);

				new_name = g_strdup_printf(_("Copy of %s"), source_basename);
				int i = 2;
				struct stat dest_info;
				while (mc_lstat(make_path(source_dirname, new_name), &dest_info) == 0)
				{
					g_free(new_name);
					new_name = g_strdup_printf(_("Copy(%d) of %s"), i, source_basename);
					i++;
				}
				action_copy(one_path, window_with_focus->sym_path, new_name, -1);

				g_free(new_name);
				g_free(source_basename);
				destroy_glist(&one_path);
			}
			else
				g_queue_push_tail(&gq, path);

			g_free(source_real_path);
			g_free(source_dirname);
		}
		else
			error = _("Some of these files are on a "
					"different machine - they will be "
					"ignored - sorry");
	}

	if (!gq.head)
	{
		if (ignore_no_local_paths == FALSE)
			error = _("None of these files are on the local "
				"machine - I can't operate on multiple "
				"remote files - sorry.");
	}
	else
	{
		if (!strcmp(*uri_list, "cut"))
		{
			action_move(gq.head, window_with_focus->sym_path, NULL, -1);
			gtk_clipboard_clear(clipboard);
		}
		else
			action_copy(gq.head, window_with_focus->sym_path, NULL, -1);

		destroy_glist(&gq.head);
	}

	if (error)
		delayed_error(_("Error getting file list: %s"), error);

	g_strfreev(uri_list);
	gtk_selection_data_free(selection);
}

static void file_op(gpointer data, FileOp action, GtkWidget *unused)
{
	DirItem	*item;
	const gchar *path;
	int	n_selected;
	ViewIter iter;

	g_return_if_fail(window_with_focus != NULL);

	n_selected = view_count_selected(window_with_focus->view);

	if (n_selected < 1)
	{
		const char *prompt;

		switch (action)
		{
			case FILE_DUPLICATE_ITEM:
				prompt = _("Duplicate ... ?");
				break;
			case FILE_RENAME_ITEM:
				prompt = _("Rename ... ?");
				break;
			case FILE_LINK_ITEM:
				prompt = _("Symlink ... ?");
				break;
			case FILE_OPEN_FILE:
				prompt = _("Shift Open ... ?");
				break;
			case FILE_PROPERTIES:
				prompt = _("Properties of ... ?");
				break;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
			case FILE_XATTRS:
				prompt = _("Extended attributes of ... ?");
				break;
#endif
			case FILE_SET_TYPE:
				prompt = _("Set type of ... ?");
				break;
			case FILE_RUN_ACTION:
				prompt = _("Set run action for ... ?");
				break;
			case FILE_SET_ICON:
				prompt = _("Set icon for ... ?");
				break;
			case FILE_DELETE:
				prompt = _("DELETE ... ?");
				break;
			case FILE_USAGE:
				prompt = _("Count the size of ... ?");
				break;
			case FILE_CHMOD_ITEMS:
				prompt = _("Set permissions on ... ?");
				break;
			case FILE_FIND:
				prompt = _("Search inside ... ?");
				break;
			case FILE_COPY_TO_CLIPBOARD:
				prompt = _("Copy ... to clipboard ?");
				break;
			case FILE_CUT_TO_CLIPBOARD:
				prompt = _("Cut ... to clipboard ?");
				break;
			default:
				g_warning("Unknown action!");
				return;
		}
		filer_target_mode(window_with_focus, target_callback,
					GINT_TO_POINTER(action), prompt);
		return;
	}

	switch (action)
	{
		case FILE_DELETE:
			delete(window_with_focus);
			return;
		case FILE_USAGE:
			usage(window_with_focus);
			return;
		case FILE_CHMOD_ITEMS:
			chmod_items(window_with_focus);
			return;
		case FILE_SET_TYPE:
			set_type_items(window_with_focus);
			return;
		case FILE_FIND:
			find(window_with_focus);
			return;
		case FILE_PROPERTIES:
		{
			GList *items;

			items = filer_selected_items(window_with_focus);
			infobox_show_list(items);
			destroy_glist(&items);
			return;
		}
		case FILE_RENAME_ITEM:
			if (n_selected > 1)
			{
				GList *items = NULL;
				ViewIter iter;

				view_get_iter(window_with_focus->view, &iter, VIEW_ITER_SELECTED);
				while ((item = iter.next(&iter)))
					items = g_list_prepend(items, item->leafname);
				items = g_list_reverse(items);

				bulk_rename(window_with_focus->sym_path, items);
				g_list_free(items);
				return;
			}
			break;	/* Not a bulk rename... see below */

		case FILE_COPY_TO_CLIPBOARD:
		case FILE_CUT_TO_CLIPBOARD:
			gtk_clipboard_clear(clipboard);

			if (action == FILE_COPY_TO_CLIPBOARD)
				clipboard_action = "copy\n";
			else
				clipboard_action = "cut\n";

			selected_paths = filer_selected_items(window_with_focus);

			gtk_clipboard_set_with_data(clipboard, clipboard_targets, 2,
				clipboard_get, clipboard_clear, NULL);
			return;
		default:
			break;
	}

	/* All the following actions require exactly one file selected */

	if (n_selected > 1)
	{
		report_error(_("You cannot do this to more than "
				"one item at a time"));
		return;
	}

	view_get_iter(window_with_focus->view, &iter, VIEW_ITER_SELECTED);

	item = iter.next(&iter);
	g_return_if_fail(item != NULL);
	/* iter may be passed to filer_openitem... */

	if (item->base_type == TYPE_UNKNOWN)
		item = dir_update_item(window_with_focus->directory,
					item->leafname);

	if (!item)
	{
		report_error(_("Item no longer exists!"));
		return;
	}

	path = make_path(window_with_focus->sym_path, item->leafname);

	switch (action)
	{
		case FILE_DUPLICATE_ITEM:
			src_dest_action_item(path, di_image(item),
					_("Duplicate"), copy_cb,
					GDK_ACTION_COPY);
			break;
		case FILE_RENAME_ITEM:
			src_dest_action_item(path, di_image(item),
					_("Rename"), rename_cb,
					GDK_ACTION_MOVE);
			break;
		case FILE_LINK_ITEM:
			src_dest_action_item(path, di_image(item),
					_("Symlink"), link_cb,
					GDK_ACTION_LINK);
			break;
		case FILE_OPEN_FILE:
			filer_openitem(window_with_focus, &iter,
				OPEN_SAME_WINDOW | OPEN_SHIFT);
			break;
		case FILE_RUN_ACTION:
			run_action(item);
			break;
		case FILE_SET_ICON:
			icon_set_handler_dialog(item, path);
			break;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
		case FILE_XATTRS:
			if(access(path, R_OK) == 0)
				xattrs_browser(item, path);
			break;
#endif
		default:
			g_warning("Unknown action!");
			return;
	}
}

static GList *set_keys_button(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *button;

	g_return_val_if_fail(option == NULL, NULL);

	button = gtk_button_new_with_label(_("Set keyboard shortcuts"));
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	g_signal_connect(button, "clicked",
			G_CALLBACK(shortcuts_dialog_show), NULL);

	return g_list_append(NULL, button);
}
