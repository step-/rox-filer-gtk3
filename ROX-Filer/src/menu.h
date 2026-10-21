/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MENU_H
#define _MENU_H

/* 'action's for menu_rox_help */
enum {HELP_ABOUT, HELP_DIR, HELP_MANUAL};

/* Fixed child indexes of the base filer_file_menu (the "File" submenu of the
 * ROX menu). "Application..." and "Send To..." are permanent leading submenu
 * items. The mount-point "Eject" item is inserted above Delete (at
 * FILE_MENU_DELETE) per popup, so indexes after Delete describe the menu
 * before that insertion and are only used while shading (before Eject is
 * added).
 */
enum {
	FILE_MENU_APP = 0,               /* "Application..." submenu item */
	FILE_MENU_SENDTO = 1,            /* "Send To..." submenu item */
	FILE_MENU_COPY = 2,              /* "Copy" (the pre-selected entry) */
	FILE_MENU_CUT = 3,
	FILE_MENU_DUPLICATE = 4,
	FILE_MENU_RENAME = 5,
	FILE_MENU_LINK = 6,
	FILE_MENU_DELETE = 7,            /* also where "Eject" is inserted */
	FILE_MENU_SHIFT_OPEN = 9,
	/* 10 RESERVED: the removed "Send To..." leaf [20260911082953] */
	FILE_MENU_RUN_ACTION = 11,       /* "Set Run Action..." ("Set Icon..." follows) */
	FILE_MENU_SET_ICON = 12,
	FILE_MENU_XATTRS = 13,           /* valid only when HAVE_GETXATTR/HAVE_ATTROPEN */
};

/* Index of the pre-selected entry ("File", relabelled "Next Click") in the
 * ROX (filer_menu) background menu. */
#define FILER_MENU_NEXT_CLICK 1

/* Passed to show_popup_menu() to open a keyboard popup with no row
 * pre-selected. Distinct from -1, which means a mouse popup.
 * [20261002090324] */
#define FILER_MENU_NO_PRESELECT (-2)

/* The integer values are persisted in existing Options files, so they
 * must not be reordered. MIS_HUGE was reserved and never offered until
 * [20260924203220]. MIS_CURRENT and MIS_DEFAULT are withdrawn so the
 * icon view no longer drives the menu icon size.
 *
 * The option labels deliberately do not match these names. In the Options
 * menu, "Small Icons" is MIS_SMALL (16 px), "Large Icons" is MIS_LARGE
 * (24 px) and "Huge Icons" is MIS_HUGE (32 px). The words are reused from
 * the Icon View's own size setting to reuse existing translations.
 */
typedef enum menu_icon_style {
  MIS_NONE, MIS_SMALL, MIS_LARGE,
  MIS_HUGE,
  MIS_CURRENT,  /* Withdrawn. Do not reuse. */
  MIS_DEFAULT   /* Withdrawn. Do not reuse. */
} MenuIconStyle;

extern GtkAccelGroup	*filer_keys;

void menu_init(void);
MenuIconStyle get_menu_icon_style(void);
gint get_menu_icon_pixels(void);

typedef void (*MenuCB)();
typedef void (*MenuCB1)(
		gpointer callback_data, guint callback_action, GtkWidget *widget);

GtkWidget *menu_add_item(GtkWidget *menu, gchar *label, MenuCB cb, guint action);
GtkWidget *menu_add_stock(GtkWidget *menu,
		gchar *label, MenuCB cb, guint action, const gchar *stock_id);
void menu_add_separator(GtkWidget *menu);
GtkWidget *menu_icon_image(const char *icon_name);
GtkWidget *menu_item_new_named_image(const char *label,
		const char *icon_name);
GtkWidget *menu_item_new_named_image_mnemonic(const char *label,
		const char *icon_name);
GtkWidget *menu_placeholder_item(const char *label);
void menu_refresh_icons(void);
GtkWidget *menu_start(gchar *label, GtkWidget *parent, GtkWidget **item_out);
GtkWidget *menu_start_detached(gchar *label, GtkWidget *parent,
		GtkWidget **item_out);

void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n);
GtkWidget *menu_make_image(DirItem *ditem, gint pixels);
void show_popup_menu(GtkWidget *menu, GdkEvent *event, int item);

void customise_send_to(gpointer data);
void customise_send_to_item(GtkMenuItem *item, gpointer data);
gpointer make_sendto_scope(MIME_type *type);
void sendto_scope_free(gpointer data, GClosure *closure);
gboolean ensure_filer_menu(void);
void show_filer_menu(FilerWindow *filer_window,
		     GdkEvent *event,
		     ViewIter *item);
void menu_popdown(void);
MIME_type *menu_selection_type(FilerWindow *fw);
GList *menu_sendto_for_type(GList *paths, MIME_type *type,
				gboolean include_generic);

/*
 * The runtime widgets of one File menu. There are two identical instances:
 * the "File" submenu nested in the ROX menu and the standalone right-click
 * popup (see build_file_menu_contents()).
 */
typedef struct {
	GtkWidget	*menu;		/* The File GtkMenu */
	GtkWidget	*app_item;	/* "App..." submenu item */
	GtkWidget	*app_menu;	/* Its submenu */
	GtkWidget	*sendto_item;	/* "Send To..." submenu item */
	GtkWidget	*sendto_menu;	/* Its submenu */
	GtkWidget	*shift_item;	/* "Shift Open" item */
	GtkWidget	*set_type;	/* "Set Type..." label */
	GtkWidget	*xattrs;	/* "Extended attributes..." label */
} FileMenu;

/* For "New" toolbar button */
void show_new_directory(FilerWindow *filer_window);
void show_new_file(FilerWindow *filer_window);
void show_menu_new(FilerWindow *filer_window);

/* Public menu handlers */
void menu_rox_help(gpointer data, guint action, GtkWidget *widget);
void menu_show_options(gpointer data, guint action, GtkWidget *widget);
void open_home(gpointer data, guint action, GtkWidget *widget);
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next);

#endif /* _MENU_H */
