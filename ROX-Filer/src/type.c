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

/* type.c - code for dealing with filetypes */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/param.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <fcntl.h>
#include <math.h>
#include <gio/gdesktopappinfo.h>

#ifdef WITH_GNOMEVFS
# include <libgnomevfs/gnome-vfs.h>
# include <libgnomevfs/gnome-vfs-mime.h>
# include <libgnomevfs/gnome-vfs-mime-handlers.h>
# include <libgnomevfs/gnome-vfs-application-registry.h>
#endif

#include "global.h"

#include "string.h"
#include "fscache.h"
#include "main.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "diritem.h"
#include "dnd.h"
#include "usericons.h"
#include "options.h"
#include "filer.h"
#include "action.h"		/* (for action_chmod) */
#include "xml.h"
#include "dropbox.h"
#include "xdgmime.h"
#include "xtypes.h"
#include "run.h"
#include "view_iface.h"

#define TYPE_NS "http://www.freedesktop.org/standards/shared-mime-info"
enum {SET_MEDIA, SET_TYPE};

/* Colours for file types (same order as base types) */
static gchar *opt_type_colours[][2] = {
	{"display_err_colour",  "#ff0000"},
	{"display_unkn_colour", "#ffe700"},
	{"display_dir_colour",  "#61fffe"},
	{"display_pipe_colour", "#dccbff"},
	{"display_sock_colour", "#ff90ff"},
	{"display_file_colour", "#ffffff"},
	{"display_cdev_colour", "#ffc488"},
	{"display_bdev_colour", "#ffe9cf"},
	{"display_door_colour", "#ff00ff"},
	{"display_exec_colour", "#75ff66"},
	{"display_adir_colour", "#00e100"}
};
#define NUM_TYPE_COLOURS\
		(sizeof(opt_type_colours) / sizeof(opt_type_colours[0]))

/* Parsed colours for file types */
Option o_display_colour_types;
static Option o_type_colours[NUM_TYPE_COLOURS];

GdkRGBA type_colours[NUM_TYPE_COLOURS];

/* Static prototypes */
static void alloc_type_colours(void);
static void options_changed(void);
static char *get_action_save_path(GtkWidget *dialog);
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create);
static gboolean remove_handler_with_confirm(const gchar *path);
static void set_icon_theme(void);
static GList *build_icon_theme(Option *option, xmlNode *node, gchar *label);
static GList *build_reset_colours(Option *option, xmlNode *node, gchar *label);

/* Hash of all allocated MIME types, indexed by "media/subtype".
 * MIME_type structs are never freed; this table prevents memory leaks
 * when rereading the config files.
 */
static GHashTable *type_hash = NULL;

/* Most things on Unix are text files, so this is the default type */
MIME_type *text_plain;
MIME_type *inode_directory;
MIME_type *inode_mountpoint;
MIME_type *inode_pipe;
MIME_type *inode_socket;
MIME_type *inode_block_dev;
MIME_type *inode_char_dev;
MIME_type *application_executable;
MIME_type *application_octet_stream;
MIME_type *application_x_shellscript;
MIME_type *application_x_desktop;
MIME_type *inode_unknown;
MIME_type *inode_door;

static Option o_icon_theme;

static GtkIconTheme *icon_theme = NULL;
static GtkIconTheme *rox_theme = NULL;
static GtkIconTheme *gnome_theme = NULL;

static GMutex m_xdg;

void type_init(void)
{
	int	    i;

	icon_theme = gtk_icon_theme_new();

	type_hash = g_hash_table_new(g_str_hash, g_str_equal);

	text_plain = get_mime_type("text/plain", TRUE);
	inode_directory = get_mime_type("inode/directory", TRUE);
	inode_mountpoint = get_mime_type("inode/mount-point", TRUE);
	inode_pipe = get_mime_type("inode/fifo", TRUE);
	inode_socket = get_mime_type("inode/socket", TRUE);
	inode_block_dev = get_mime_type("inode/blockdevice", TRUE);
	inode_char_dev = get_mime_type("inode/chardevice", TRUE);
	application_executable = get_mime_type("application/x-executable", TRUE);
	application_octet_stream = get_mime_type("application/octet-stream", TRUE);
	application_x_shellscript = get_mime_type("application/x-shellscript", TRUE);
	application_x_desktop = get_mime_type("application/x-desktop", TRUE);
	application_x_desktop->executable = TRUE;
	inode_unknown = get_mime_type("inode/unknown", TRUE);
	inode_door = get_mime_type("inode/door", TRUE);

	option_add_string(&o_icon_theme, "icon_theme", "DEFAULT");
	option_add_int(&o_display_colour_types, "display_colour_types", TRUE);
	option_register_widget("icon-theme-chooser", build_icon_theme);
	option_register_widget("reset-type-colours", build_reset_colours);

	for (i = 0; i < NUM_TYPE_COLOURS; i++)
		option_add_string(&o_type_colours[i],
				  opt_type_colours[i][0],
				  opt_type_colours[i][1]);
	alloc_type_colours();

	set_icon_theme();

	option_add_notify(options_changed);
}

/* Read-load all the glob patterns.
 * Note: calls filer_update_all.
 */
void reread_mime_files(void)
{
	gtk_icon_theme_rescan_if_needed(icon_theme);

	xdg_mime_shutdown();

	filer_update_all();
}

/* Returns the MIME_type structure for the given type name. It is looked
 * up in type_hash and returned if found. If not found (and can_create is
 * TRUE) then a new MIME_type is made, added to type_hash and returned.
 * NULL is returned if type_name is not in type_hash and can_create is
 * FALSE, or if type_name does not contain a '/' character.
 */
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create)
{
        MIME_type *mtype;
	gchar *slash;

	mtype = g_hash_table_lookup(type_hash, type_name);
	if (mtype || !can_create)
		return mtype;

	slash = strchr(type_name, '/');
	if (slash == NULL)
	{
		g_warning("MIME type '%s' does not contain a '/' character!",
			  type_name);
		return NULL;
	}

	mtype = g_new(MIME_type, 1);
	mtype->media_type = g_strndup(type_name, slash - type_name);
	mtype->subtype = g_strdup(slash + 1);
	mtype->image = NULL;
	mtype->comment = NULL;

	g_mutex_lock(&m_xdg);
	mtype->executable = xdg_mime_mime_type_subclass(type_name,
						"application/x-executable");
	g_mutex_unlock(&m_xdg);

	g_hash_table_insert(type_hash, g_strdup(type_name), mtype);

	return mtype;
}

const char *basetype_name(DirItem *item)
{
	if (item->flags & ITEM_FLAG_SYMLINK)
		return _("Sym link");
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return _("Mount point");
	else if (item->flags & ITEM_FLAG_APPDIR)
		return _("App dir");

	switch (item->base_type)
	{
		case TYPE_FILE:
			return _("File");
		case TYPE_DIRECTORY:
			return _("Dir");
		case TYPE_CHAR_DEVICE:
			return _("Char dev");
		case TYPE_BLOCK_DEVICE:
			return _("Block dev");
		case TYPE_PIPE:
			return _("Pipe");
		case TYPE_SOCKET:
			return _("Socket");
		case TYPE_DOOR:
			return _("Door");
	}

	return _("Unknown");
}

struct mime_list {
	GList *list;
	gboolean only_regular;
};

static void append_names(gpointer key, gpointer value, gpointer udata)
{
	struct mime_list *mlist = (struct mime_list*) udata;

	if(!mlist->only_regular || strncmp((char *)key, "inode/", 6)!=0)
		mlist->list = g_list_prepend(mlist->list, key);
}

/* Return list of all mime type names. Caller must free the list
 * but NOT the strings it contains (which are never freed).
 If only_regular is true then inode types are excluded.
 */
GList *mime_type_name_list(gboolean only_regular)
{
	struct mime_list list;

	list.list=NULL;
	list.only_regular=only_regular;

	g_hash_table_foreach(type_hash, append_names, &list);
	list.list = g_list_sort(list.list, (GCompareFunc) strcmp);

	return list.list;
}

/*			MIME-type guessing 			*/

/* Get the type of this file - stats the file and uses that if
 * possible. For regular or missing files, uses the pathname.
 */
MIME_type *type_get_type(const gchar *path)
{
	DirItem		*item;
	MIME_type	*type = NULL;

	item = diritem_new("");
	diritem_restat(path, item, NULL, TRUE);
	if (item->base_type != TYPE_ERROR)
		type = item->mime_type;
	diritem_free(item);

	if (type)
		return type;

	type = type_from_path(path);

	if (!type)
		return text_plain;

	return type;
}

/* Returns a pointer to the MIME-type.
 *
 * Tries all enabled methods:
 * - Look for extended attribute
 * - If no attribute, check file name
 * - If no name rule, check contents
 *
 * NULL if we can't think of anything.
 */
MIME_type *type_from_path(const char *path)
{
	MIME_type *mime_type = NULL;
	const char *type_name;

	/* Check for extended attribute first */
	mime_type = xtype_get(path);
	if (mime_type)
		return mime_type;

	/* Try name and contents next */
	g_mutex_lock(&m_xdg);
	type_name = xdg_mime_get_mime_type_for_file(path, NULL);
	g_mutex_unlock(&m_xdg);

	if (type_name)
		return get_mime_type(type_name, TRUE);

	return NULL;
}

/* Returns the file/dir in Choices for handling this type.
 * NULL if there isn't one. g_free() the result.
 */
char *handler_for(MIME_type *type)
{
	char	*type_name;
	char	*open;
	char	*target;

	if (type == NULL)
		open = choices_find_xdg_path_load("all", "MIME-types", SITE);
	else
	{
		type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);
		open = choices_find_xdg_path_load(type_name, "MIME-types", SITE);
		g_free(type_name);

		if (!open)
			open = choices_find_xdg_path_load(type->media_type,
							  "MIME-types", SITE);
	}

	if (!open)
		return NULL;

	/* Some programs behave differently depending on the command
	 * name (e.g., 'vim' vs 'gvim'), so symlinks need to be followed here.
	 */
	target = readlink_dup(open);
	if (!target)
	{
		return open;
	}

	if (target[0] == '/')
	{
		/* Absolute path */
		g_free(open);
		return target;
	}
	else
	{
		/* Relative path (shouldn't normally be needed) */
		gchar *dir;
		char *abs_path;

		dir = g_path_get_dirname(open);
		g_free(open);

		abs_path = g_strconcat(dir, "/", target, NULL);
		g_free(target);
		g_free(dir);

		return abs_path;
	}
}

MIME_type *mime_type_lookup(const char *type)
{
	return get_mime_type(type, TRUE);
}

static void init_aux_theme(GtkIconTheme **ptheme, const char *name)
{
	if (*ptheme)
		return;
	*ptheme = gtk_icon_theme_new();
	gtk_icon_theme_set_custom_theme(*ptheme, name);
}

inline static void init_rox_theme(void)
{
	init_aux_theme(&rox_theme, "ROX");
}

inline static void init_gnome_theme(void)
{
	gchar *name;
	g_object_get(gtk_settings_get_default(),
			"gtk-icon-theme-name", &name, NULL);

	init_aux_theme(&gnome_theme, name ? name : "gnome");


}

/* We don't want ROX to override configured theme so try all possibilities
 * in icon_theme first */
static GtkIconInfo *mime_type_lookup_icon_info(GtkIconTheme *theme,
		MIME_type *type)
{
	char *type_name = g_strconcat(type->media_type, "-", type->subtype, NULL);
	GtkIconInfo *full = gtk_icon_theme_lookup_icon(theme, type_name, thumb_size,
			GTK_ICON_LOOKUP_GENERIC_FALLBACK);

	g_free(type_name);
	if (!full)
	{
		/* Ugly hack... try for a GNOME icon */
		if (type == inode_directory)
			type_name = g_strdup("gnome-fs-directory");
		else
			type_name = g_strconcat("gnome-mime-", type->media_type,
					"-", type->subtype, NULL);
		full = gtk_icon_theme_lookup_icon(theme, type_name, thumb_size, 0);
		g_free(type_name);
	}
	if (!full)
	{
		/* Try for a media type */
		type_name = g_strconcat(type->media_type, "-x-generic", NULL);
		full = gtk_icon_theme_lookup_icon(theme, type_name, thumb_size, 0);
		g_free(type_name);
	}
	if (!full)
	{
		/* Ugly hack... try for a GNOME default media icon */
		type_name = g_strconcat("gnome-mime-", type->media_type, NULL);

		full = gtk_icon_theme_lookup_icon(theme, type_name, thumb_size, 0);
		g_free(type_name);
	}
	return full;
}

/*			Actions for types 			*/

/* Shared walk over the MIME icon sources, in the order the filer uses.
 *
 * Each candidate filename is passed to try_path() in turn; the walk stops
 * as soon as try_path() returns TRUE. Walking by callback rather than
 * returning a single path matters because a source that exists but cannot be
 * loaded must not hide the sources behind it, and only the loading caller
 * knows whether that happened.
 *
 * Icon sources, in order: <Choices>/MIME-icons/media_subtype.png, then
 * icon_theme, rox_theme and gnome_theme (skipping whichever is the same
 * object as icon_theme). inode/mount-point retries as inode/directory,
 * as type_to_icon() has always done.
 *
 * Returns TRUE if try_path() accepted a path. [20261006083027]
 */
static gboolean walk_mime_icon_sources(MIME_type *type,
		gboolean (*try_path)(const gchar *path, gpointer data),
		gpointer data)
{
	GtkIconInfo *full;
	gchar *type_name, *path;

	g_return_val_if_fail(type != NULL, FALSE);

	type_name = g_strconcat(type->media_type, "_", type->subtype,
				".png", NULL);
	path = choices_find_xdg_path_load(type_name, "MIME-icons", SITE);
	g_free(type_name);
	if (path)
	{
		gboolean accepted;

		accepted = try_path(path, data);
		g_free(path);
		if (accepted)
			return TRUE;
	}

	full = mime_type_lookup_icon_info(icon_theme, type);
	if (!full && icon_theme != rox_theme)
	{
		init_rox_theme();
		full = mime_type_lookup_icon_info(rox_theme, type);
	}
	if (!full && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		full = mime_type_lookup_icon_info(gnome_theme, type);
	}
	if (full)
	{
		const gchar *filename;
		gboolean accepted;

		filename = gtk_icon_info_get_filename(full);
		accepted = filename ? try_path(filename, data) : FALSE;
		g_object_unref(full);
		if (accepted)
			return TRUE;
	}

	/* Try to use the inode/directory icon for inode/mount-point */
	if (type != inode_mountpoint)
		return FALSE;

	return walk_mime_icon_sources(inode_directory, try_path, data);
}

/* try_path() for type_to_icon(): load the file into the type's cache.
 */
static gboolean try_path_load_icon(const gchar *path, gpointer data)
{
	MIME_type *type = data;

	/* Get the actual icon through our cache, not through GTK, because
	 * GTK doesn't cache icons.
	 */
	type->image = g_fscache_lookup(pixmap_cache, path);

	return type->image != NULL;
}

/* try_path() for type_icon_source_path(): keep the path, load nothing.
 * data is a gchar ** that receives a newly-allocated copy.
 */
static gboolean try_path_capture(const gchar *path, gpointer data)
{
	gchar **slot = data;

	*slot = g_strdup(path);

	return TRUE;
}

/* Find the file that supplies an item's icon, without loading it.
 *
 * Returns a newly allocated path, or NULL when no file could be found.
 * The caller must g_free() the result. *marker, if not NULL, receives a
 * newly allocated human-readable note when the icon is one of the
 * generated fallbacks; otherwise it is set to NULL.
 *
 * The order below mirrors diritem.c exactly, because that is the order
 * in which item->_image is decided:
 *
 *   1. a user-assigned icon                check_globicon()
 *   2. .DirIcon, for any directory         diritem_examine_dir()
 *   3. AppIcon.xpm, for an AppDir           diritem_examine_dir()
 *   4. Icon= from a .desktop file           pixmap_from_desktop_file()
 *   5. the MIME icon chain                  _diritem_get_image()
 *
 * Steps 2 and 3 are reached only from diritem_examine_dir(), which
 * skips mount points and resolves names against the directory's
 * realpath, so a symlink to a directory takes its icon from the target.
 * [20261006083027]
 */
gchar *type_icon_source_path(const gchar *path, DirItem *item, gchar **marker)
{
	gchar *result;
	gchar *icon_name;
	gchar *icon = NULL;
	GtkIconInfo *info;
	GdkPixbuf *pixbuf;
	MIME_type *type;
	int tmp_fd;

	if (marker)
		*marker = NULL;

	if (path == NULL || item == NULL)
		return NULL;

	/* A user-assigned icon outranks everything else. */
	result = get_globicon_path(path);
	if (result)
		return result;

	if (item->base_type == TYPE_DIRECTORY &&
	    !(item->flags & ITEM_FLAG_MOUNT_POINT))
	{
		/* Look in the directory itself, following symlinks as diritem.c
		 * does via pathdup(). diritem.c also rejects a .DirIcon whose
		 * owner differs from the directory's, or an icon larger than
		 * MAX_ICON_SIZE. We display the path regardless.
		 */
		gchar *rpath;

		rpath = pathdup(path);
		if (rpath)
		{
			result = g_build_filename(rpath, ".DirIcon", NULL);
			if (file_exists(result))
			{
				g_free(rpath);
				return result;
			}
			g_free(result);

			if (item->flags & ITEM_FLAG_APPDIR)
			{
				result = g_build_filename(rpath,
						"AppIcon.xpm", NULL);
				if (file_exists(result))
				{
					g_free(rpath);
					return result;
				}
				g_free(result);
			}
			g_free(rpath);
		}

		/* An AppDir with no icon at all is drawn with im_appdir,
		 * which comes from <filer_app_dir>/images/application.png.
		 */
		if ((item->flags & ITEM_FLAG_APPDIR) && app_dir)
		{
			result = g_build_filename(app_dir, "images",
					"application.png", NULL);
			if (file_exists(result))
				return result;
			g_free(result);
		}
	}

	/* Icon= from a .desktop file. An absolute path names the file
	 * directly; a name is resolved through theme_load_icon(), whose
	 * cascade order differs from the MIME chain's in pixmaps.c.
	 */
	if (item->mime_type == application_x_desktop)
	{
		GError *error = NULL;

		icon = get_value_from_desktop_file(path,
				"Desktop Entry", "Icon", &error);
		if (error)
		{
			/* Behave as pixmap_from_desktop_file() does: warn once,
			 * give up on this file, try the MIME icon chain (below)
			 */
			g_error_free(error);
			error = NULL;
		}
		if (icon)
		{
			if (icon[0] == '/')
			{
				result = icon;
				icon = NULL;
				return result;
			}

			/* Some icon names carry an extension; drop it,
			 * as pixmap_from_desktop_file() does.
			 */
			{
				char *extension = strrchr(icon, '.');

				if (extension && (
					strcmp(extension, ".png") == 0 ||
					strcmp(extension, ".xpm") == 0 ||
					strcmp(extension, ".svg") == 0))
					*extension = '\0';
			}

			/* Icon name is resolved through theme_load_icon(),
			 * whose cascade order differs from the MIME
			 * chain's, so it is resolved here rather than
			 * through walk_mime_icon_sources().
			 */
			/* The SVG reader is noisy; redirect stderr. */
			tmp_fd = dup(2);
			dup2(1, 2);
			pixbuf = theme_load_icon(icon, thumb_size, 0, NULL);
			dup2(tmp_fd, 2);
			close(tmp_fd);

			if (pixbuf)
			{
				GtkIconInfo *dinfo;

				dinfo = theme_lookup_icon(icon, thumb_size, 0);
				g_object_unref(pixbuf);
				if (dinfo)
				{
					const gchar *filename;

					filename = gtk_icon_info_get_filename(
							dinfo);
					result = filename ?
						g_strdup(filename) : NULL;
					g_object_unref(dinfo);
				}
				if (result)
				{
					g_free(icon);
					return result;
				}
			}
			g_free(icon);
			icon = NULL;
		}
	}

	/* The MIME icon chain, walked exactly as type_to_icon() walks it.
	 */
	type = item->mime_type;

	if (type)
	{
		result = NULL;
		walk_mime_icon_sources(type, try_path_capture, &result);
		if (result)
			return result;
	}

	/* No file anywhere. Report the icon the filer will actually draw.
	 */
	if (item->base_type == TYPE_ERROR)
		icon_name = "dialog-warning";
	else
		icon_name = "dialog-question";

	info = gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(),
			icon_name, 48, 0);
	if (info)
	{
		const gchar *filename;

		filename = gtk_icon_info_get_filename(info);
		if (filename)
			result = g_strdup(filename);
		g_object_unref(info);
	}

	if (result && marker)
		*marker = g_strdup(item->base_type == TYPE_ERROR ?
				_("(error icon)") : _("(unknown-type icon)"));

	return result;
}

MaskedPixmap *type_to_icon(MIME_type *type)
{
	static GMutex funcm;
	MaskedPixmap *ret;
	time_t	now;

	if (type == NULL)
	{
		g_object_ref(im_unknown);
		return im_unknown;
	}

	now = time(NULL);

	g_mutex_lock(&funcm);

	/* Already got an image? */
	if (type->image)
	{
		/* Yes - don't recheck too often */
		if (labs(now - type->image_time) < 9)
		{
			ret = g_object_ref(type->image);
			g_mutex_unlock(&funcm);
			return ret;
		}
		g_object_unref(type->image);
		type->image = NULL;
	}

	walk_mime_icon_sources(type, try_path_load_icon, type);

	if (!type->image)
	{
		/* One ref from the type structure, one returned */
		type->image = im_unknown;
		g_object_ref(im_unknown);
	}

	type->image_time = now;

	ret = g_object_ref(type->image);
	g_mutex_unlock(&funcm);
	return ret;
}

GdkAtom type_to_atom(MIME_type *type)
{
	char	*str;
	GdkAtom	retval;

	g_return_val_if_fail(type != NULL, GDK_NONE);

	str = g_strconcat(type->media_type, "/", type->subtype, NULL);
	retval = gdk_atom_intern(str, FALSE);
	g_free(str);

	return retval;
}

static void show_shell_help(gpointer data)
{
	info_message(_("Enter a shell command which will load \"$@\" into "
			"a suitable program. Eg:\n\n"
			"gimp \"$@\""));
}

/* Called if the user clicks on the OK button. Returns FALSE if an error
 * was displayed instead of performing the action.
 */
static gboolean set_shell_action(GtkWidget *dialog)
{
	GtkEntry *entry;
	const gchar *command;
	gchar	*tmp, *path;
	int	error = 0, len;
	int	fd;

	entry = g_object_get_data(G_OBJECT(dialog), "shell_command");

	g_return_val_if_fail(entry != NULL, FALSE);

	command = gtk_entry_get_text(entry);

	if (!strchr(command, '$'))
	{
		show_shell_help(NULL);
		return FALSE;
	}

	path = get_action_save_path(dialog);
	if (!path)
		return FALSE;

	tmp = g_strdup_printf("#! /bin/sh\nexec %s\n", command);
	len = strlen(tmp);

	fd = open(path, O_CREAT | O_WRONLY, 0755);
	if (fd == -1)
		error = errno;
	else
	{
		FILE *file;

		file = fdopen(fd, "w");
		if (file)
		{
			if (fwrite(tmp, 1, len, file) < len)
				error = errno;
			if (fclose(file) && error == 0)
				error = errno;
		}
		else
			error = errno;
	}

	if (error)
		report_error(g_strerror(error));

	g_free(tmp);
	g_free(path);

	gtk_widget_destroy(dialog);

	return TRUE;
}

static void set_action_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK)
		if (!set_shell_action(dialog))
			return;
	gtk_widget_destroy(dialog);
}

/* Return the path of the file in choices that handles this type and
 * radio setting.
 * NULL if nothing is defined for it.
 */
static gchar *handler_for_radios(GObject *dialog)
{
	Radios	*radios;
	MIME_type *type;

	radios = g_object_get_data(G_OBJECT(dialog), "rox-radios");
	type = g_object_get_data(G_OBJECT(dialog), "mime_type");

	g_return_val_if_fail(radios != NULL, NULL);
	g_return_val_if_fail(type != NULL, NULL);

	switch (radios_get_value(radios))
	{
		case SET_MEDIA:
			return choices_find_xdg_path_load(type->media_type,
							  "MIME-types", SITE);
		case SET_TYPE:
		{
			gchar *tmp, *handler;
			tmp = g_strconcat(type->media_type, "_",
					  type->subtype, NULL);
			handler = choices_find_xdg_path_load(tmp,
							     "MIME-types",
							     SITE);
			g_free(tmp);
			return handler;
		}
		default:
			g_warning("Bad type");
			return NULL;
	}
}

/* (radios can be NULL if called from clear_run_action) */
static void run_action_update(Radios *radios, gpointer data)
{
	gchar *handler;
	DropBox *drop_box;
	GObject *dialog = G_OBJECT(data);

	drop_box = g_object_get_data(dialog, "rox-dropbox");

	g_return_if_fail(drop_box != NULL);

	handler = handler_for_radios(dialog);

	if (handler)
	{
		char *old = handler;

		handler = readlink_dup(old);
		if (handler)
			g_free(old);
		else
			handler = old;
	}

	drop_box_set_path(DROP_BOX(drop_box), handler);
	g_free(handler);
}

static void clear_run_action(GtkWidget *drop_box, GtkWidget *dialog)
{
	gchar *handler;

	handler = handler_for_radios(G_OBJECT(dialog));

	if (handler)
		remove_handler_with_confirm(handler);

	run_action_update(NULL, dialog);
}


static void appinfo_clicked(GtkButton *btn, GtkWidget *dialog)
{
	GDesktopAppInfo *app = g_object_get_data(G_OBJECT(btn), "appinfo");
	gchar *path = get_action_save_path(dialog);
	if (path)
	{
		if (symlink(g_desktop_app_info_get_filename(app), path))
			delayed_error("symlink: %s", g_strerror(errno));
		else
			destroy_on_idle(dialog);
	}
	g_free(path);
}

/* Called when a URI list is dropped onto the box in the Set Run Action
 * dialog. Make sure it's an application, and make that the default
 * handler.
 */
static void drag_app_dropped(GtkWidget	*drop_box,
			     const gchar *app,
			     GtkWidget	*dialog)
{
	DirItem	*item;

	item = diritem_new("");
	diritem_restat(app, item, NULL, TRUE);
	if (item->flags & ITEM_FLAG_APPDIR || EXECUTABLE_FILE(item))
	{
		gchar	*path;

		path = get_action_save_path(dialog);

		if (path)
		{
			if (symlink(app, path))
				delayed_error("symlink: %s",
						g_strerror(errno));
			else
				destroy_on_idle(dialog);

			g_free(path);
		}
	}
	else
		delayed_error(
			_("This is not a program! Give me an application "
			"instead!"));

	diritem_free(item);
}

/* Find the current command which is used to run files of this type.
 * Returns NULL on failure. g_free() the result.
 */
static gchar *get_current_command(MIME_type *type)
{
	struct stat	info;
	char *handler, *nl, *data = NULL;
	long len;
	gchar *command = NULL;

	handler = handler_for(type);

	if (!handler)
		return NULL;		/* No current handler */

	if (stat(handler, &info))
		goto out;		/* Can't stat */

	if ((!S_ISREG(info.st_mode)) || info.st_size > 256)
		goto out;		/* Only use small regular files */

	if (!load_file(handler, &data, &len))
		goto out;		/* Didn't load OK */

	if (strncmp(data, "#! /bin/sh\nexec ", 16) != 0)
		goto out;		/* Not one of ours */

	nl = strchr(data + 16, '\n');
	if (!nl)
		goto out;		/* No newline! */

	command = g_strndup(data + 16, nl - data - 16);
out:
	g_free(handler);
	g_free(data);
	return command;
}

/* Find the current command which is used to run files of this type,
 * and return a textual description of it.
 * Only call for non-executable files.
 * g_free() the result.
 */
gchar *describe_current_command(MIME_type *type)
{
	char *handler;
	char *desc = NULL;
	struct stat info;

	g_return_val_if_fail(type != NULL, NULL);

	handler = handler_for(type);

	if (!handler)
		return g_strdup(_("No run action defined"));

	if (mc_stat(handler, &info) !=0 )
	{
		desc = g_strdup_printf(_("Error in handler %s: %s"), handler,
					g_strerror(errno));
		goto out;
	}

	if (S_ISDIR(info.st_mode))
	{
		const gchar *tmp;
		uid_t dir_uid = info.st_uid;

		tmp = make_path(handler, "AppRun");

		if (mc_lstat(tmp, &info) != 0 || info.st_uid != dir_uid
			|| !(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
			desc = g_strdup_printf(
				_("Invalid application %s (bad AppRun)"),
				handler);
		/* Else, just report handler... */

		goto out;
	}

	/* It's not an application directory, and it's not a symlink... */

	if (access(handler, X_OK) != 0)
	{
		desc = g_strdup_printf(_("Non-executable %s"), handler);
		goto out;
	}

	desc = get_current_command(type);
out:
	if (!desc)
		desc = handler;
	else
		g_free(handler);

	return desc;
}

/* Display a dialog box allowing the user to set the default run action
 * for this type.
 */
void type_set_handler_dialog(MIME_type *type)
{
	gchar		*tmp;
	GtkDialog	*dialog;
	GtkWidget	*frame, *entry, *label, *button;
	GtkWidget	*hbox;
	Radios		*radios;

	g_return_if_fail(type != NULL);

	dialog = GTK_DIALOG(gtk_dialog_new());
//	gtk_dialog_set_has_separator(dialog, FALSE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	g_object_set_data(G_OBJECT(dialog), "mime_type", type);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set run action"));

	radios = radios_new(run_action_update, dialog);
	g_object_set_data(G_OBJECT(dialog), "rox-radios", radios);

	radios_add(radios,
			_("If a handler for the specific type isn't set up, "
			  "use this as the default."), SET_MEDIA,
			_("Set default for all `%s/<anything>'"),
			type->media_type);

	radios_add(radios,
			_("Use this application for all files with this MIME "
			  "type."), SET_TYPE,
			_("Only for the type `%s' (%s/%s)"),
			mime_type_comment(type),
			type->media_type, type->subtype);

	radios_set_value(radios, SET_TYPE);

	frame = drop_box_new(_("Drop a suitable application here"));

	g_object_set_data(G_OBJECT(dialog), "rox-dropbox", frame);

	radios_pack(radios, GTK_BOX(gtk_dialog_get_content_area(dialog)));
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), frame, TRUE, TRUE, 0);

	g_signal_connect(frame, "path_dropped",
			G_CALLBACK(drag_app_dropped), dialog);
	g_signal_connect(frame, "clear",
			G_CALLBACK(clear_run_action), dialog);


	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	char *typestr = g_strdup_printf("%s/%s", type->media_type, type->subtype);
	GList *apps = g_app_info_get_all_for_type(typestr);
//	apps = g_app_info_get_recommended_for_type(typestr);
//	apps = g_app_info_get_fallback_for_type(typestr);
	g_free(typestr);

	for (GList *next = apps; next; next = next->next)
	{
		GAppInfo *app = next->data;
		GtkWidget *btn = button_new_image_text(
				gtk_image_new_from_gicon(
					g_app_info_get_icon(app), GTK_ICON_SIZE_BUTTON),
				g_app_info_get_display_name(app));

		g_object_set_data(G_OBJECT(btn), "appinfo", app);
		g_object_weak_ref(G_OBJECT(btn), (GWeakNotify)g_object_unref, app);

		g_signal_connect(btn, "clicked", G_CALLBACK(appinfo_clicked), dialog);
		gtk_box_pack_start(GTK_BOX(vbox), btn, TRUE, TRUE, 0);
	}
	g_list_free(apps);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), vbox, FALSE, TRUE, 4);


	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
			TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("OR")),
						FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
			TRUE, TRUE, 0);


	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, TRUE, 0);

	label = gtk_label_new(_("Enter a shell command:")),
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button(show_shell_help, NULL), FALSE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), entry, FALSE, TRUE, 0);
	gtk_widget_grab_focus(entry);
	g_object_set_data(G_OBJECT(dialog), "shell_command", entry);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

	/* If possible, fill in the entry box with the current command */
	tmp = get_current_command(type);
	if (tmp)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), tmp);
		gtk_editable_set_position(GTK_EDITABLE(entry), -1);
		g_free(tmp);
	}
	else
	{
		gtk_entry_set_text(GTK_ENTRY(entry), " \"$@\"");
		gtk_editable_set_position(GTK_EDITABLE(entry), 0);
	}

	gtk_dialog_add_action_widget(dialog,
			button_new_mixed("window-close", _("_Close")),
			GTK_RESPONSE_CANCEL);

	button = button_new_mixed("gtk-ok", _("_Use Command"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(dialog, button, GTK_RESPONSE_OK);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, TRUE, 0);

	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);

	g_signal_connect(dialog, "response",
			G_CALLBACK(set_action_response), NULL);

	rox_dialog_set_parent_full(GTK_WIDGET(dialog), FALSE);
	number_of_windows++;
	g_signal_connect(dialog, "destroy",
			G_CALLBACK(one_less_window), NULL);
	gtk_widget_show_all(GTK_WIDGET(dialog));
}

/* path is an entry in Choices. If it's a symlink or a very small executable
 * then just get rid of it, otherwise confirm first. It it doesn't exist,
 * do nothing.
 *
 * FALSE on error (abort operation).
 */
static gboolean remove_handler_with_confirm(const gchar *path)
{
	struct stat info;

	if (lstat(path, &info) == 0)
	{
		/* A binding already exists... */
		if (S_ISREG(info.st_mode) && info.st_size > 256)
		{
			if (!confirm(_("A run action already exists and is "
				      "quite a big program - are you sure "
				      "you want to delete it?"),
				    "edit-delete", NULL))
			{
				return FALSE;
			}
		}

		if (unlink(path))
		{
			report_error(_("Can't remove %s: %s"),
				path, g_strerror(errno));
			return FALSE;
		}
	}

	return TRUE;
}

/* The user wants to set a new default action for files of this type (or just
 * clear the action). Removes the current binding if possible and returns the
 * path to save the new one to. NULL means cancel. g_free() the result.
 */
static char *get_action_save_path(GtkWidget *dialog)
{
	gchar		*path = NULL;
	gchar 		*type_name = NULL;
	MIME_type	*type;
	Radios		*radios;

	g_return_val_if_fail(dialog != NULL, NULL);

	type = g_object_get_data(G_OBJECT(dialog), "mime_type");
	radios = g_object_get_data(G_OBJECT(dialog), "rox-radios");

	g_return_val_if_fail(radios != NULL && type != NULL, NULL);

	if (radios_get_value(radios) == SET_MEDIA)
		type_name = g_strdup(type->media_type);
	else
		type_name = g_strconcat(type->media_type, "_",
				type->subtype, NULL);

	path = choices_find_xdg_path_save("", project_name, SITE, FALSE);
	if (!path)
	{
		report_error(
		_("Choices saving is disabled by CHOICESPATH variable"));
		goto out;
	}
	g_free(path);

	path = choices_find_xdg_path_save(type_name, "MIME-types", SITE, TRUE);

	if (!remove_handler_with_confirm(path))
		null_g_free(&path);
out:
	g_free(type_name);
	return path;
}

MIME_type *mime_type_from_base_type(int base_type)
{
	switch (base_type)
	{
		case TYPE_FILE:
			return text_plain;
		case TYPE_DIRECTORY:
			return inode_directory;
		case TYPE_PIPE:
			return inode_pipe;
		case TYPE_SOCKET:
			return inode_socket;
		case TYPE_BLOCK_DEVICE:
			return inode_block_dev;
		case TYPE_CHAR_DEVICE:
			return inode_char_dev;
	        case TYPE_DOOR:
	                return inode_door;
	}
	return inode_unknown;
}

/* Takes the st_mode field from stat() and returns the base type.
 * Should not be a symlink.
 */
int mode_to_base_type(int st_mode)
{
	if (S_ISREG(st_mode))
		return TYPE_FILE;
	else if (S_ISDIR(st_mode))
		return TYPE_DIRECTORY;
	else if (S_ISBLK(st_mode))
		return TYPE_BLOCK_DEVICE;
	else if (S_ISCHR(st_mode))
		return TYPE_CHAR_DEVICE;
	else if (S_ISFIFO(st_mode))
		return TYPE_PIPE;
	else if (S_ISSOCK(st_mode))
		return TYPE_SOCKET;
	else if (S_ISDOOR(st_mode))
		return TYPE_DOOR;

	return TYPE_ERROR;
}

/* Returns TRUE is this is something that is run by looking up its type
 * in MIME-types and, hence, can have its run action set.
 */
gboolean can_set_run_action(DirItem *item)
{
	g_return_val_if_fail(item != NULL, FALSE);

	return item->base_type == TYPE_FILE; //&& !EXECUTABLE_FILE(item);
}

/* Parse file type colours */
static void alloc_type_colours(void)
{
	int		i;

	/* Parse colours */
	for (i = 0; i < NUM_TYPE_COLOURS; i++)
	{
		GdkRGBA c;

		gdk_rgba_parse(&c, o_type_colours[i].value);

		type_colours[i] = c;
	}
}

/* Linearize one sRGB channel value to the WCAG definition. */
static double colour_channel_linear(double v)
{
	if (v <= 0.03928)
		return v / 12.92;
	return pow((v + 0.055) / 1.055, 2.4);
}

/* Relative luminance of an RGB colour, per the WCAG definition.
 */
static double colour_luminance(const GdkRGBA *c)
{
	return 0.2126 * colour_channel_linear(c->red)
			+ 0.7152 * colour_channel_linear(c->green)
			+ 0.0722 * colour_channel_linear(c->blue);
}

/* Contrast ratio between two colours, per WCAG.
 */
static double colour_contrast(const GdkRGBA *a, const GdkRGBA *b)
{
	double la = colour_luminance(a);
	double lb = colour_luminance(b);
	double hi = MAX(la, lb);
	double lo = MIN(la, lb);

	return (hi + 0.05) / (lo + 0.05);
}

/* Convert an RGB colour to HSL; hue comes back in [0,1), saturation and
 * lightness in [0,1].
 */
static void colour_rgb_to_hsl(const GdkRGBA *c, double *h, double *s, double *l)
{
	double max = MAX(MAX(c->red, c->green), c->blue);
	double min = MIN(MIN(c->red, c->green), c->blue);
	double delta = max - min;

	*l = (max + min) / 2.0;

	if (delta == 0.0)
	{
		*h = 0.0;
		*s = 0.0;
	}
	else
	{
		*s = (*l <= 0.5) ? delta / (max + min) : delta / (2.0 - max - min);
		if (max == c->red)
		{
			*h = (c->green - c->blue) / delta;
			if (*h < 0.0)
				*h += 6.0;
		}
		else if (max == c->green)
			*h = (c->blue - c->red) / delta + 2.0;
		else
			*h = (c->red - c->green) / delta + 4.0;
		*h /= 6.0;
	}
}

/* One RGB channel of the HSL-to-RGB conversion.
 */
static double colour_hue_to_rgb(double p, double q, double t)
{
	if (t < 0.0)
		t += 1.0;
	if (t > 1.0)
		t -= 1.0;
	if (t < 1.0 / 6.0)
		return p + (q - p) * 6.0 * t;
	if (t < 1.0 / 2.0)
		return q;
	if (t < 2.0 / 3.0)
		return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
	return p;
}

/* Convert HSL back to RGB.
 */
static void colour_hsl_to_rgb(double h, double s, double l, GdkRGBA *c)
{
	if (s == 0.0)
	{
		c->red = l;
		c->green = l;
		c->blue = l;
	}
	else
	{
		double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
		double p = 2.0 * l - q;

		c->red = colour_hue_to_rgb(p, q, h + 1.0 / 3.0);
		c->green = colour_hue_to_rgb(p, q, h);
		c->blue = colour_hue_to_rgb(p, q, h - 1.0 / 3.0);
	}
	c->alpha = 1.0;
}

/* Adapt the shipped default filetype colours for readable contrast against
 * the resolved theme background. User-customized colours (whose option value
 * differs from the shipped default) are left untouched.
 *
 * Called once, from the first view to acquire a style context. The theme is
 * effectively fixed per session, so this runs a single time.
 */
static gboolean theme_colours_adapted = FALSE;

void type_adapt_default_colours(GtkStyleContext *ctx)
{
	GdkRGBA bg = { 1.0, 1.0, 1.0, 1.0 };
	int i;

	if (theme_colours_adapted)
		return;

	theme_colours_adapted = TRUE;

	/* The views paint theme_bg_color as their background, so resolve the
	 * same colour here via the theme's @define-color map (the reliable
	 * mechanism also used by set_bg_src()/get_icon_view_bg()). */
	if (!gtk_style_context_lookup_color(ctx, "theme_bg_color", &bg))
		return;

	for (i = 0; i < NUM_TYPE_COLOURS; i++)
	{
		GdkRGBA c;
		int steps = 0;
		double bg_lum = colour_luminance(&bg);
		double target = bg_lum >= 0.5 ? 6.5 : 5.0;

		/* Only adapt colours the user has not customized. */
		if (strcmp(o_type_colours[i].value, opt_type_colours[i][1]) != 0)
			continue;

		gdk_rgba_parse(&c, o_type_colours[i].value);
		c.alpha = 1.0;

		if (colour_contrast(&c, &bg) >= target)
			continue;

		/* On light themes, blend toward black preserving hue. On dark
		 * themes, raise the lightness instead: blending toward white
		 * would wash saturated colours (notably the error red) out
		 * to near-white. Binary-search the smallest lightness that
		 * reaches the target so the result is precise. Aim for a
		 * comfortable margin above the WCAG AA minimum (4.5:1), on
		 * both light and dark themes: between AA and AAA. */
		if (bg_lum >= 0.5)
		{
			while (colour_contrast(&c, &bg) < target && steps++ < 32)
			{
				c.red *= 0.85;
				c.green *= 0.85;
				c.blue *= 0.85;
			}
		}
		else
		{
			double h, s, l;
			double lo, hi;
			int iter;

			colour_rgb_to_hsl(&c, &h, &s, &l);
			lo = l;
			hi = 1.0;
			for (iter = 0; iter < 32; iter++)
			{
				double mid = (lo + hi) / 2.0;
				GdkRGBA probe;

				colour_hsl_to_rgb(h, s, mid, &probe);
				if (colour_contrast(&probe, &bg) < target)
					lo = mid;
				else
					hi = mid;
			}
			colour_hsl_to_rgb(h, s, lo, &c);
		}

		/* Store the adapted colour as the option value (without
		 * marking it changed), so the Options > Colours dialog and
		 * both views all show the same colour. */
		g_free(o_type_colours[i].value);
		o_type_colours[i].value =
				g_strdup_printf("#%02x%02x%02x",
						(int) (c.red * 255),
						(int) (c.green * 255),
						(int) (c.blue * 255));
	}

	alloc_type_colours();

	{
		GList *next;
		for (next = all_filer_windows; next; next = next->next)
			view_style_changed(((FilerWindow *) next->data)->view, 0);
	}
}

static void expire_timer(gpointer key, gpointer value, gpointer data)
{
	MIME_type *type = value;

	type->image_time = 0;
}

static void options_changed(void)
{
	alloc_type_colours();
	if (o_icon_theme.has_changed)
	{
		set_icon_theme();
		g_hash_table_foreach(type_hash, expire_timer, NULL);
		full_refresh();
	}

	gboolean changed = o_display_colour_types.has_changed;
	if (!changed && o_display_colour_types.int_value)
	{
		int i;
		for (i = 0; i < NUM_TYPE_COLOURS; i++)
			changed |= o_type_colours[i].has_changed;
	}
	if (changed)
	{
		GList *next;
		for (next = all_filer_windows; next; next = next->next)
			view_style_changed(((FilerWindow *) next->data)->view, 0);
	}

}

/* Return a pointer to a (static) colour for this item. If colouring is
 * off, returns normal.
 */
GdkRGBA *type_get_colour(DirItem *item, GdkRGBA *normal)
{
	int type = item->base_type;

	if (!o_display_colour_types.int_value)
		return normal;

	if (EXECUTABLE_FILE(item))
		type = TYPE_EXEC;
	else if (item->flags & ITEM_FLAG_APPDIR)
		type = TYPE_APPDIR;

	g_return_val_if_fail(type >= 0 && type < NUM_TYPE_COLOURS, normal);

	return &type_colours[type];
}

static char **get_xdg_data_dirs(int *n_dirs)
{
	const char *env;
	char **dirs;
	int i, n;

	env = getenv("XDG_DATA_DIRS");
	if (!env)
		env = "/usr/local/share/:/usr/share/";
	dirs = g_strsplit(env, ":", 0);
	g_return_val_if_fail(dirs != NULL, NULL);
	for (n = 0; dirs[n]; n++)
		;
	for (i = n; i > 0; i--)
		dirs[i] = dirs[i - 1];
	env = getenv("XDG_DATA_HOME");
	if (env)
		dirs[0] = g_strdup(env);
	else
		dirs[0] = g_build_filename(g_get_home_dir(), ".local",
					   "share", NULL);
	*n_dirs = n + 1;
	return dirs;
}

/* Try to fill in 'type->comment' from this document */
static void get_comment(MIME_type *type, const gchar *path)
{
	xmlNode *node;
	XMLwrapper *doc;

	doc = xml_cache_load(path);
	if (!doc)
		return;

	node = xml_get_section(doc, TYPE_NS, "comment");

	if (node)
	{
		char *val;
		g_return_if_fail(type->comment == NULL);
		val= (char *) xmlNodeListGetString(node->doc, node->xmlChildrenNode, 1);
		type->comment = g_strdup(val);
		xmlFree(val);
	}

	g_object_unref(doc);
}

/* Fill in the comment field for this MIME type */
static void find_comment(MIME_type *type)
{
	char **dirs;
	int i, n_dirs = 0;

	if (type->comment)
	{
		g_free(type->comment);
		type->comment = NULL;
	}

	dirs = get_xdg_data_dirs(&n_dirs);
	g_return_if_fail(dirs != NULL);

	for (i = 0; i < n_dirs; i++)
	{
		gchar *path;

		path = g_strdup_printf("%s/mime/%s/%s.xml", dirs[i],
				type->media_type, type->subtype);
		get_comment(type, path);
		g_free(path);
		if (type->comment)
			break;
	}

	if (!type->comment)
		type->comment = g_strdup_printf("%s/%s", type->media_type,
						type->subtype);

	for (i = 0; i < n_dirs; i++)
		g_free(dirs[i]);
	g_free(dirs);
}

const char *mime_type_comment(MIME_type *type)
{
	if (!type->comment)
		find_comment(type);

	return type->comment;
}

static void unref_icon_theme(void)
{
	if (icon_theme && icon_theme != rox_theme && icon_theme != gnome_theme)
		g_object_unref(icon_theme);
}

static void set_icon_theme(void)
{
	struct stat info;
	char *icon_home;
	const char *theme_dir;
	const char *theme_name = o_icon_theme.value;

	if (!theme_name || !*theme_name)
		theme_name = "gnome";

	if (!strcmp(theme_name, "ROX"))
	{
		unref_icon_theme();
		init_rox_theme();
		icon_theme = rox_theme;
	}
	else if (!strcmp(theme_name, "gnome"))
	{
		unref_icon_theme();
		init_gnome_theme();
		icon_theme = gnome_theme;
	}
	else
	{
		if (icon_theme == rox_theme || icon_theme == gnome_theme)
			icon_theme = gtk_icon_theme_new();
		gtk_icon_theme_set_custom_theme(icon_theme, theme_name);
	}

	/* Ensure the personal ROX theme path exists. */

	icon_home = g_build_filename(home_dir, ".icons", "ROX", NULL);
	if (stat(icon_home, &info) == 0)
	{
		g_autofree gchar *index = g_build_filename(icon_home,
				"index.theme", NULL);

		if (!g_file_test(index, G_FILE_TEST_EXISTS))
		{
			/* Warn only: grandfather existing personal themes. */
			g_warning("Missing index file:'%s'", index);
		}
		return;	/* Already exists */
	}

	/* Link the personal ROX theme to the shipped ROX theme. */

	/* First, create the .icons directory */
	theme_dir = make_path(home_dir, ".icons");
	if (!file_exists(theme_dir))
		mkdir(theme_dir, 0755);

	if (lstat(icon_home, &info) == 0)
	{
		/* Probably a broken symlink, then. Remove it. */
		if (unlink(icon_home))
			g_warning("Error removing broken symlink %s: %s", icon_home, g_strerror(errno));
		else
			g_warning("Removed broken symlink %s", icon_home);
	}

	if (symlink(make_path(app_dir, "ROX"), icon_home))
	{
		delayed_error(_("Failed to create symlink '%s':\n%s"), icon_home, g_strerror(errno));
		open_to_show(icon_home);
	}
	g_free(icon_home);

	gtk_icon_theme_rescan_if_needed(icon_theme);
}

typedef struct
{
	char	*label;
	char	*value;
} ThemePair;

static gint theme_pair_cmp(gconstpointer a, gconstpointer b)
{
	const ThemePair *pa = *(const ThemePair **)a;
	const ThemePair *pb = *(const ThemePair **)b;
	return strcmp(pa->value, pb->value);
}

/* Inheritance can be circular, so cap how far the chain is followed. */
#define MAX_THEME_INHERIT_DEPTH 8

/* Return the path of theme_name's index.theme, or NULL if the theme is
 * not installed in any search-path directory.
 */
static gchar *find_theme_index(const gchar *theme_name)
{
	gchar	**theme_dirs = NULL;
	gint	  n_dirs = 0;
	gchar	 *index_path = NULL;
	gint	  i;

	gtk_icon_theme_get_search_path(icon_theme, &theme_dirs, &n_dirs);

	for (i = 0; i < n_dirs && !index_path; i++)
	{
		gchar *path = g_build_filename(theme_dirs[i], theme_name,
						"index.theme", NULL);

		if (!access(path, F_OK))
			index_path = path;
		else
			g_free(path);
	}

	g_strfreev(theme_dirs);

	return index_path;
}

static gboolean theme_supplies_mime_icons(const gchar *theme_name,
		GHashTable *seen, gint depth);

/* Does this theme, or any theme it inherits, declare an icon directory
 * for MIME types?
 *
 * The answer is read from the theme's index.theme and never from directory
 * names: the ROX theme declares its MIME directory as "MIME", the large
 * themes declare size-prefixed "mimetypes" directories, and GTK
 * synthesises the "@2" variants [20261003061308].
 *
 * 'seen' holds the themes already visited on this chain.
 */
static gboolean theme_index_supplies_mime_icons(const gchar *index_path,
		GHashTable *seen, gint depth)
{
	GKeyFile  *key_file;
	gchar	 **dirs = NULL;
	gboolean   result = FALSE;
	gint	   i;

	key_file = g_key_file_new();
	g_key_file_set_list_separator(key_file, ',');
	if (!g_key_file_load_from_file(key_file, index_path,
				G_KEY_FILE_NONE, NULL))
		goto out;

	dirs = g_key_file_get_string_list(key_file, "Icon Theme",
					"Directories", NULL, NULL);
	for (i = 0; dirs && dirs[i] && !result; i++)
	{
		gchar *context = g_key_file_get_string(key_file, dirs[i],
							"Context", NULL);
		if (context)
		{
			result = !strcmp(context, "MimeTypes");
			g_free(context);
		}
	}
	g_strfreev(dirs);

	/* A theme may ship no MIME icons of its own and still serve them
	 * through the themes it inherits. */
	if (!result)
	{
		gchar *inherits = g_key_file_get_string(key_file, "Icon Theme",
							"Inherits", NULL);

		if (inherits)
		{
			gchar **parents = g_strsplit(inherits, ",", -1);

			for (i = 0; parents[i] && !result; i++)
			{
				g_strstrip(parents[i]);
				result = theme_supplies_mime_icons(parents[i],
						seen, depth + 1);
			}
			g_strfreev(parents);
			g_free(inherits);
		}
	}

out:
	g_key_file_unref(key_file);

	return result;
}

static gboolean theme_supplies_mime_icons(const gchar *theme_name,
		GHashTable *seen, gint depth)
{
	gboolean	result;
	gchar	 *index_path;

	if (!theme_name || !*theme_name || depth > MAX_THEME_INHERIT_DEPTH)
		return FALSE;

	/* Inheritance can be circular, so visit each theme only once. */
	if (g_hash_table_contains(seen, theme_name))
		return FALSE;
	g_hash_table_add(seen, g_strdup(theme_name));

	/* A parent that is not installed is not an error */
	index_path = find_theme_index(theme_name);
	if (!index_path)
		return FALSE;

	result = theme_index_supplies_mime_icons(index_path, seen, depth);
	g_free(index_path);

	return result;
}

/* [20261009182925] Restore the shipped filetype colours, then re-run
 * the theme adaptation so the restored defaults stay readable.
 * Restore first: the adaptation skips anything differing from
 * shipped, so the reverse order would adapt nothing.
 */
static void reset_type_colours(GtkWidget *button, gpointer data)
{
	gint	i;

	(void) data;

	for (i = 0; i < NUM_TYPE_COLOURS; i++)
	{
		Option *o = &o_type_colours[i];

		if (strcmp(o->value, opt_type_colours[i][1]) != 0)
		{
			g_free(o->value);
			o->value = g_strdup(opt_type_colours[i][1]);
			o->int_value = atoi(o->value);
			o->has_changed = TRUE;
			if (o->widget && o->update_widget)
			{
				o->update_widget(o);
			}
		}
	}

	theme_colours_adapted = FALSE;
	type_adapt_default_colours(gtk_widget_get_style_context(button));

	options_notify();
}

static GList *build_reset_colours(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *button;

	(void) node;
	(void) label;

	g_return_val_if_fail(option == NULL, NULL);

	button = button_new_mixed("edit-clear", _("Reset to defaults"));
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	g_signal_connect(button, "clicked",
			G_CALLBACK(reset_type_colours), NULL);

	return g_list_append(NULL, button);
}

static GList *build_icon_theme(Option *option, xmlNode *node, gchar *label)
{
	GPtrArray	*pairs;
	GHashTable	*emitted;
	gchar		**theme_dirs = NULL;
	gint		 n_dirs = 0;
	int		 i;

	g_return_val_if_fail(option != NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);

	gtk_icon_theme_get_search_path(icon_theme, &theme_dirs, &n_dirs);

	pairs = g_ptr_array_new();

	emitted = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	for (i = 0; i < n_dirs; i++)
	{
		GPtrArray *list;
		int j;

		if (access(theme_dirs[i], F_OK) != 0)
			continue;

		list = list_dir(theme_dirs[i]);
		if (!list)
			continue;

		for (j = 0; j < list->len; j++)
		{
			char	*name = list->pdata[j];
			char	*index_path;
			gboolean	usable;
			GHashTable	*seen;

			/* Deduplicate pulldown list.  [20261006191023] */
			if (g_hash_table_contains(emitted, name))
			{
				g_free(name);
				continue;
			}

			index_path = g_build_filename(theme_dirs[i],
							name, "index.theme", NULL);

			/* A theme with no MIME icon directory anywhere in its
			 * chain is left out: type_to_icon() falls back to the
			 * ROX and system themes.  [20261003061308]. */
			seen = g_hash_table_new_full(g_str_hash, g_str_equal,
							g_free, NULL);
			usable = !access(index_path, F_OK)
				&& theme_index_supplies_mime_icons(index_path,
						seen, 0);
			g_hash_table_destroy(seen);

			if (usable)
			{
				ThemePair *p = g_new(ThemePair, 1);
				p->label = g_strdup(name);
				p->value = g_strdup(name);
				g_ptr_array_add(pairs, p);
				g_hash_table_add(emitted, g_strdup(name));
			}
			else
				g_free(name);

			g_free(index_path);
		}
		g_ptr_array_free(list, TRUE);
	}
	g_hash_table_destroy(emitted);
	g_strfreev(theme_dirs);

	/* "System Default" stores an invalid theme name so ROX falls back
	 * to the system default icon theme. This keeps mount point
	 * icons consistent with plain directories [20260820184912]. */
	{
		ThemePair *def = g_new(ThemePair, 1);
		def->label = g_strdup(_("System Default"));
		def->value = g_strdup("DEFAULT");
		g_ptr_array_add(pairs, def);
	}

	/* The theme in use stays always listed [20261003061308]. */
	if (o_icon_theme.value && *o_icon_theme.value)
	{
		gboolean	found = FALSE;

		for (i = 0; i < pairs->len; i++)
		{
			ThemePair *p = pairs->pdata[i];

			if (!strcmp(p->value, o_icon_theme.value))
			{
				found = TRUE;
				break;
			}
		}

		if (!found)
		{
			ThemePair *p = g_new(ThemePair, 1);
			p->label = g_strdup(o_icon_theme.value);
			p->value = g_strdup(o_icon_theme.value);
			g_ptr_array_add(pairs, p);
		}
	}

	g_ptr_array_sort(pairs, theme_pair_cmp);

	{
		GPtrArray *labels = g_ptr_array_new();
		GPtrArray *values = g_ptr_array_new();
		for (i = 0; i < pairs->len; i++)
		{
			ThemePair *p = pairs->pdata[i];
			g_ptr_array_add(labels, p->label);
			g_ptr_array_add(values, p->value);
			g_free(p);
		}
		g_ptr_array_free(pairs, TRUE);
		return options_build_menu(option, label, labels, values);
	}
}

GtkIconInfo *theme_lookup_icon(const gchar *icon_name, gint size,
		GtkIconLookupFlags flags)
{
	GtkIconInfo *result = gtk_icon_theme_lookup_icon(icon_theme,
			icon_name, size, flags);

	if (!result && icon_theme != rox_theme)
	{
		init_rox_theme();
		result = gtk_icon_theme_lookup_icon(rox_theme,
			icon_name, size, flags);
	}
	if (!result && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		result = gtk_icon_theme_lookup_icon(gnome_theme,
			icon_name, size, flags);
	}
	return result;
}

GdkPixbuf *theme_load_icon(const gchar *icon_name, gint size,
		GtkIconLookupFlags flags, GError **perror)
{
	GdkPixbuf *result = gtk_icon_theme_load_icon(icon_theme,
			icon_name, size, flags, NULL);

	if (!result && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		result = gtk_icon_theme_load_icon(gnome_theme,
			icon_name, size, flags, NULL);
	}
	if (!result && icon_theme != rox_theme)
	{
		init_rox_theme();
		result = gtk_icon_theme_load_icon(rox_theme,
			icon_name, size, flags, perror);
	}
	return result;
}

