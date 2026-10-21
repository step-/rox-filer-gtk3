/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <gtk/gtk.h>

typedef void OptionNotify(void);
typedef GList * (*OptionBuildFn)(Option *option, xmlNode *node, gchar *label);

struct _Option {
	gchar		*value;
	long		int_value;
	gboolean	has_changed;

	gchar		*backup;	/* Copy of value to Revert to */

	GtkWidget	*widget;		/* NULL => No UI yet */
	void		(*update_widget)(Option *option);
	gchar *	(*read_widget)(Option *option);
};

/* Prototypes */

void options_init(void);

void option_register_widget(char *name, OptionBuildFn builder);
void option_check_widget(Option *option);

void option_add_int(Option *option, const gchar *key, int value);
void option_add_string(Option *option, const gchar *key, const gchar *value);

void options_notify(void);
void option_add_notify(OptionNotify *callback);
void option_add_saver(OptionNotify *callback);
GList *build_numentry_base(Option *option, xmlNode *node,
				  gchar *label, GtkAdjustment *adj);
GList *options_build_menu(
		Option *option, gchar *label, GPtrArray *names, GPtrArray *values);

GtkWidget *options_show(void);

/* Used by rpc_GetOption, rpc_SetOption */
gchar *option_get(const gchar *key);
void option_set(const gchar *key, const gchar *new_value);

#endif /* _OPTIONS_H */
