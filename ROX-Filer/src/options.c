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

/* options.c - code for handling user choices */

/* How it works:
 *
 * On startup:
 *
 * - The <Choices>/PROJECT/Options file is read in, which contains a list of
 *   name/value pairs, and these are stored in the 'loading' hash table.
 *
 * - Each part of the filer then calls option_add_int(), or a related function,
 *   supplying the name for each option and a default value. Once an option is
 *   registered, it is removed from the loading table.
 *
 * - If things need to happen when values change, modules register with
 *   option_add_notify().
 *
 * - option_register_widget() can be used during initialisation (any time
 *   before the Options box is displayed) to tell the system how to render a
 *   particular type of option.
 *
 * - Finally, all notify callbacks are called. Use the Option->has_changed
 *   field to work out what has changed from the defaults.
 *
 * When the user opens the Options box:
 *
 * - The Options.xml file is read and used to create the Options dialog box.
 *   Each element in the file has a key corresponding to an option named
 *   above.
 *
 * - For each widget in the box, the current value of the option is used to
 *   set the widget's state.
 *
 * - All current values are saved for a possible Revert later.
 *
 * When the user changes an option or clicks on Revert:
 *
 * - The option values are updated.
 *
 * - All notify callbacks are called. Use the Option->has_changed field
 *   to see what changed.
 *
 * When OK is clicked:
 *
 * - If anything changed then:
 *   - All the options are written to the filesystem
 *   - The saver_callbacks are called.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <libxml/parser.h>

#include "global.h"

#include "choices.h"
#include "options.h"
#include "main.h"
#include "gui_support.h"
#include "support.h"

/* The Options window. NULL if not yet created. */
static GtkWidget *window = NULL;

/* "filer_unique" -> (Option *) */
static GHashTable *option_hash = NULL;

/* A mapping (name -> value) for options which have been loaded by not
 * yet registered. The options in this table cannot be used until
 * option_add_*() is called to move them into option_hash.
 */
static GHashTable *loading = NULL;

/* A mapping (XML name -> OptionBuildFn). When reading the Options.xml
 * file, this table gives the function used to create the widgets.
 */
static GHashTable *widget_builder = NULL;

/* A mapping (name -> GtkSizeGroup) of size groups used by the widgets
 * in the options box. This hash table is created/destroyed every time
 * the box is opened/destroyed.
 */
static GHashTable *size_groups = NULL;

/* List of functions to call after all option values are updated */
static GList *notify_callbacks = NULL;

/* List of functions to call after all options are saved */
static GList *saver_callbacks = NULL;

static int updating_widgets = 0;	/* Ignore change signals when set */

static GtkWidget *revert_widget = NULL;

static GtkTreePath *lasttreepath = NULL;

/* Static prototypes */
static void save_options(void);
static void revert_options(GtkWidget *widget, gpointer data);
static void build_options_window(void);
static GtkWidget *build_window_frame(GtkTreeView **tree_view);
static void update_option_widgets(void);
static void option_add(Option *option, const gchar *key);
static void set_not_changed(gpointer key, gpointer value, gpointer data);
static void load_options(xmlDoc *doc);
static gboolean check_anything_changed(void);
static int get_int(xmlNode *node, gchar *attr);
static void may_add_tip(GtkWidget *widget, xmlNode *element);
static void add_to_size_group(xmlNode *node, GtkWidget *widget);

static const char *process_option_line(gchar *line);

static GList *build_label(Option *option, xmlNode *node, gchar *label);
static GList *build_spacer(Option *option, xmlNode *node, gchar *label);
static GList *build_frame(Option *option, xmlNode *node, gchar *label);
static GList *build_grid(Option *option, xmlNode *node, gchar *label);

static GList *build_toggle(Option *option, xmlNode *node, gchar *label);
static GList *build_slider(Option *option, xmlNode *node, gchar *label);
static GList *build_entry(Option *option, xmlNode *node, gchar *label);
static GList *build_radio_group(Option *option, xmlNode *node, gchar *label);
static GList *build_colour(Option *option, xmlNode *node, gchar *label);
static GList *build_menu(Option *option, xmlNode *node, gchar *label);
static GList *build_font(Option *option, xmlNode *node, gchar *label);
static GList *build_numentry(Option *option, xmlNode *node, gchar *label);
static void update_numentry(Option *option);
static gchar *read_numentry(Option *option);

static gboolean updating_file_format = FALSE;

/* Configuration file format version, saved as a version attribute on
 * the <Options> root element [20260928124821]. Absent means a legacy
 * (or pre-tag) file, treated as version 0. Unknown to the legacy filer,
 * whose loader skips it along with everything that is not <Option>.
 */
#define OPTIONS_FILE_VERSION 1
static int options_file_version = 0;

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void options_init(void)
{
	char	*path;
	xmlDoc	*doc;

	loading = g_hash_table_new(g_str_hash, g_str_equal);
	option_hash = g_hash_table_new(g_str_hash, g_str_equal);
	widget_builder = g_hash_table_new(g_str_hash, g_str_equal);

	path = choices_find_xdg_path_load("Options", project_name, SITE);
	if (path)
	{
		/* Load in all the options set in the filer, storing them
		 * temporarily in the loading hash table.
		 * They get moved to option_hash when they're registered.
		 */
		doc = xmlParseFile(path);
		if (doc)
		{
			load_options(doc);
			xmlFreeDoc(doc);
		}
		else
		{
			parse_file(path, process_option_line);
			updating_file_format = TRUE;
		}

		g_free(path);
	}

	option_register_widget("label", build_label);
	option_register_widget("spacer", build_spacer);
	option_register_widget("frame", build_frame);
	option_register_widget("grid", build_grid);

	option_register_widget("toggle", build_toggle);
	option_register_widget("slider", build_slider);
	option_register_widget("entry", build_entry);
	option_register_widget("numentry", build_numentry);
	option_register_widget("radio-group", build_radio_group);
	option_register_widget("colour", build_colour);
	option_register_widget("menu", build_menu);
	option_register_widget("font", build_font);
}

/* When parsing the XML file, process an element named 'name' by
 * calling 'builder(option, xml_node, label)'.
 * builder returns the new widgets to add to the options box.
 * 'name' should be a static string. Call 'option_check_widget' when
 * the widget's value is modified.
 *
 * Functions to set or get the widget's state can be stored in 'option'.
 * If the option doesn't have a name attribute in Options.xml then
 * ui will be NULL on entry (this is used for buttons).
 */
void option_register_widget(char *name, OptionBuildFn builder)
{
	g_hash_table_insert(widget_builder, name, builder);
}

/* This is called when the widget's value is modified by the user.
 * Reads the new value of the widget into the option and calls
 * the notify callbacks.
 */
void option_check_widget(Option *option)
{
	gchar		*new = NULL;

	if (updating_widgets)
		return;		/* Not caused by the user... */

	g_return_if_fail(option->read_widget != NULL);

	new = option->read_widget(option);

	g_return_if_fail(new != NULL);

	g_hash_table_foreach(option_hash, set_not_changed, NULL);

	option->has_changed = strcmp(option->value, new) != 0;

	if (!option->has_changed)
	{
		g_free(new);
		return;
	}

	g_free(option->value);
	option->value = new;
	option->int_value = atoi(new);

	options_notify();
}

/* Call all the notify callbacks. This should happen after any options
 * have their values changed.
 * Set each option->has_changed flag before calling this function.
 */
void options_notify(void)
{
	GList	*next;

	for (next = notify_callbacks; next; next = next->next)
	{
		OptionNotify *cb = (OptionNotify *) next->data;

		cb();
	}

	if (updating_file_format)
	{
		updating_file_format = FALSE;
		save_options();
		info_message(_("ROX-Filer has converted your Options file "
				"to the new XML format"));
	}

	if (revert_widget)
		gtk_widget_set_sensitive(revert_widget,
					 check_anything_changed());
}

/* Store values used by Revert */
static void store_backup(gpointer key, gpointer value, gpointer data)
{
	Option *option = (Option *) value;

	g_free(option->backup);
	option->backup = g_strdup(option->value);
}

/* Allow the user to edit the options. Returns the window widget (you don't
 * normally need this). NULL if already open.
 */
GtkWidget *options_show(void)
{
	/* For debugging
	if (g_hash_table_size(loading) != 0)
	{
		g_print(PROJECT ": Some options loaded but not used:\n");
		g_hash_table_foreach(loading, (GHFunc) puts, NULL);
	}
	*/

	if (window)
	{
		gtk_window_present(GTK_WINDOW(window));
		return NULL;
	}

	g_hash_table_foreach(option_hash, store_backup, NULL);

	build_options_window();

	update_option_widgets();

	return window;
}

/* Initialise and register a new integer option */
void option_add_int(Option *option, const gchar *key, int value)
{
	option->value = g_strdup_printf("%d", value);
	option->int_value = value;
	option_add(option, key);
}

void option_add_string(Option *option, const gchar *key, const gchar *value)
{
	option->value = g_strdup(value);
	option->int_value = atoi(value);
	option_add(option, key);
}

/* Add a callback which will be called after any options have changed their
 * values. If several options change at once, this is called after all
 * changes.
 */
void option_add_notify(OptionNotify *callback)
{
	g_return_if_fail(callback != NULL);

	notify_callbacks = g_list_append(notify_callbacks, callback);
}

/* Call 'callback' after all the options have been saved */
void option_add_saver(OptionNotify *callback)
{
	g_return_if_fail(callback != NULL);

	saver_callbacks = g_list_append(saver_callbacks, callback);
}

/* Base class for building numentry widgets with particular ranges */
GList *build_numentry_base(Option *option, xmlNode *node,
				  gchar *label, GtkAdjustment *adj)
{
	GtkWidget	*hbox;
	GtkWidget	*spin;
	GtkWidget	*label_wid;
	gchar		*unit;
	int		width;

	width = get_int(node, "width");
	unit = (gchar *) xmlGetProp(node, BAD_CAST "unit");

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (label)
	{
		label_wid = gtk_label_new(_(label));
		gtk_widget_set_halign(label_wid, GTK_ALIGN_END);
		gtk_widget_set_valign(label_wid, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), label_wid, FALSE, TRUE, 0);
		add_to_size_group(node, label_wid);
	}

	spin = gtk_spin_button_new(adj, gtk_adjustment_get_step_increment(adj), 0);
	gtk_entry_set_width_chars(GTK_ENTRY(spin),
			width > 1 ? width + 1 : 2);
	gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, TRUE, 0);
	may_add_tip(spin, node);

	if (unit)
	{
		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(unit)),
				FALSE, TRUE, 0);
		g_free(unit);
	}

	option->update_widget = update_numentry;
	option->read_widget = read_numentry;
	option->widget = spin;

	g_signal_connect_swapped(spin, "value-changed",
			G_CALLBACK(option_check_widget), option);

	return g_list_append(NULL, hbox);
}

/* Returns the value of the option as a string.
 * g_free() the result.
 */
gchar *option_get(const gchar *key)
{
	Option *option;

	g_return_val_if_fail(option_hash != NULL, NULL);
	option = g_hash_table_lookup(option_hash, key);
	g_return_val_if_fail(option != NULL, NULL);

	return g_strdup(option->value);
}

void option_set(const gchar *key, const gchar *new_value)
{
	Option *option;

	g_return_if_fail(option_hash != NULL);
	option = g_hash_table_lookup(option_hash, key);
	g_return_if_fail(option != NULL);
	g_return_if_fail(new_value != NULL);

	option->has_changed = strcmp(option->value, new_value) != 0;

	if (!option->has_changed)
		return;

	g_free(option->value);
	option->value = g_strdup(new_value);
	option->int_value = atoi(new_value);

	save_options();
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* Option should contain the default value.
 * It must never be destroyed after being registered (Options are typically
 * statically allocated).
 * The key corresponds to the option's name in Options.xml, and to the key
 * in the saved options file.
 *
 * On exit, the value will have been updated to the loaded value, if
 * different to the default.
 */
static void option_add(Option *option, const gchar *key)
{
	gpointer okey, value;

	g_return_if_fail(option_hash != NULL);
	g_return_if_fail(g_hash_table_lookup(option_hash, key) == NULL);
	g_return_if_fail(option->value != NULL);

	option->has_changed = FALSE;

	option->widget = NULL;
	option->update_widget = NULL;
	option->read_widget = NULL;
	option->backup = NULL;

	g_hash_table_insert(option_hash, (gchar *) key, option);

	/* Use the value loaded from the file, if any */
	if (g_hash_table_lookup_extended(loading, key, &okey, &value))
	{
		g_free(option->value);
		option->value = value;
		option->int_value = atoi(value);
		g_hash_table_remove(loading, key);
		g_free(okey);
	}
}

static GtkFontChooserDialog *current_fontsel_box = NULL;

static void font_chosen(GtkWidget *dialog, gint response, Option *option)
{
	gchar *font;

	if (response != GTK_RESPONSE_OK)
		goto out;

	font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));

	gtk_label_set_text(GTK_LABEL(option->widget), font);

	g_free(font);

	option_check_widget(option);

out:
	gtk_widget_destroy(dialog);

}

static void toggle_active_font(GtkToggleButton *toggle, Option *option)
{
	if (current_fontsel_box)
		gtk_widget_destroy(GTK_WIDGET(current_fontsel_box));

	if (gtk_toggle_button_get_active(toggle))
	{
		gtk_widget_set_sensitive(gtk_widget_get_parent(option->widget), TRUE);
		gtk_label_set_text(GTK_LABEL(option->widget), "Sans 12");
	}
	else
	{
		gtk_widget_set_sensitive(gtk_widget_get_parent(option->widget), FALSE);
		gtk_label_set_text(GTK_LABEL(option->widget),
				   _("(use default)"));
	}

	option_check_widget(option);
}

static void open_fontsel(GtkWidget *button, Option *option)
{
	if (current_fontsel_box)
		gtk_widget_destroy(GTK_WIDGET(current_fontsel_box));

	current_fontsel_box = GTK_FONT_CHOOSER_DIALOG(
				gtk_font_chooser_dialog_new(PROJECT, NULL));

	gtk_window_set_position(GTK_WINDOW(current_fontsel_box),
				GTK_WIN_POS_MOUSE);

	g_signal_connect(current_fontsel_box, "destroy",
			G_CALLBACK(gtk_widget_destroyed), &current_fontsel_box);

	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(current_fontsel_box),
					option->value);

	g_signal_connect(current_fontsel_box, "response",
			G_CALLBACK(font_chosen), option);

	gtk_widget_show(GTK_WIDGET(current_fontsel_box));
}

/* These are used during parsing... */
static xmlDocPtr options_doc = NULL;

#define DATA(node) ((gchar *) xmlNodeListGetString(options_doc, node->xmlChildrenNode, 1))

static void may_add_tip(GtkWidget *widget, xmlNode *element)
{
	gchar	*data, *tip;

	data = DATA(element);
	if (!data)
		return;

	tip = g_strstrip(g_strdup(data));
	g_free(data);
	if (*tip)
		gtk_widget_set_tooltip_text(widget, _(tip));
	g_free(tip);
}

/* Returns zero if attribute is not present */
static int get_int(xmlNode *node, gchar *attr)
{
	gchar *txt;
	int	retval;

	txt = (gchar *) xmlGetProp(node, (const xmlChar *) attr);
	if (!txt)
		return 0;

	retval = atoi(txt);
	g_free(txt);

	return retval;
}

/* Adds 'widget' to the GtkSizeGroup selected by 'index'.  This function
 * does nothing if 'node' has no "sizegroup" attribute.
 * The value of "sizegroup" is either a key. All widgets with the same
 * key request the same size.
 * Size groups are created on the fly and get destroyed when the options
 * box is closed.
 */
static void add_to_size_group(xmlNode *node, GtkWidget *widget)
{
	GtkSizeGroup	*sg;
	gchar		*name;

	g_return_if_fail(node != NULL);
	g_return_if_fail(widget != NULL);

	name = (gchar *) xmlGetProp(node, BAD_CAST "sizegroup");
	if (!name)
		return;

	if (size_groups == NULL)
		size_groups = g_hash_table_new_full(g_str_hash, g_str_equal,
				g_free, NULL);

	sg = (GtkSizeGroup *) g_hash_table_lookup(size_groups, name);
	if (sg == NULL)
	{

		sg = (GtkSizeGroup *) gtk_size_group_new(
				GTK_SIZE_GROUP_HORIZONTAL);
		g_hash_table_insert(size_groups, name, sg);
		gtk_size_group_add_widget(sg, widget);
		g_object_unref(G_OBJECT(sg));
	}
	else
	{
		gtk_size_group_add_widget(sg, widget);
		g_free(name);
	}
}

static GtkWidget *build_radio(xmlNode *radio, GtkWidget *prev)
{
	GtkWidget	*button;
	GtkRadioButton	*prev_button = (GtkRadioButton *) prev;
	gchar		*label;

	label = (gchar *) xmlGetProp(radio, BAD_CAST "label");

	button = gtk_radio_button_new_with_label(
			prev_button ? gtk_radio_button_get_group(prev_button)
				    : NULL,
			_(label));
	g_free(label);

	may_add_tip(button, radio);

	g_object_set_data(G_OBJECT(button), "value",
			  xmlGetProp(radio, BAD_CAST "value"));

	return button;
}

static void build_widget(xmlNode *widget, GtkWidget *box)
{
	const char *name = (const char *) widget->name;
	OptionBuildFn builder;
	gchar	*oname;
	Option	*option;
	gchar	*label;

	label = (gchar *) xmlGetProp(widget, BAD_CAST "label");

	if (strcmp(name, "hbox") == 0 || strcmp(name, "vbox") == 0)
	{
		GtkWidget *nbox;
		xmlNode	  *hw;

		if (name[0] == 'h')
			nbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		else
			nbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		if (label)
			gtk_box_pack_start(GTK_BOX(nbox),
				gtk_label_new(_(label)), FALSE, TRUE, 4);
		if (GTK_IS_GRID(box))
			gtk_grid_attach(GTK_GRID(box), nbox, get_int(widget, "col"), get_int(widget, "row"), 1, 1);
		else
			gtk_box_pack_start(GTK_BOX(box), nbox, FALSE, TRUE, 0);

		for (hw = widget->xmlChildrenNode; hw; hw = hw->next)
		{
			if (hw->type == XML_ELEMENT_NODE)
				build_widget(hw, nbox);
		}

		g_free(label);
		return;
	}

	oname = (gchar *) xmlGetProp(widget, BAD_CAST "name");

	if (oname)
	{
		option = g_hash_table_lookup(option_hash, oname);

		if (!option)
		{
			g_warning("No Option for '%s'!\n", oname);
			g_free(oname);
			return;
		}

		g_free(oname);
	}
	else
		option = NULL;

	builder = g_hash_table_lookup(widget_builder, name);
	if (builder)
	{
		GList *widgets, *next;

		if (option && option->widget)
			g_warning("Widget for option already exists!");

		widgets = builder(option, widget, label);

		for (next = widgets; next; next = next->next)
		{
			GtkWidget *w = (GtkWidget *) next->data;
			if (GTK_IS_GRID(box))
				gtk_grid_attach(GTK_GRID(box), w, get_int(widget, "col"), get_int(widget, "row"), 1, 1);
			else
				gtk_box_pack_start(GTK_BOX(box), w, FALSE, TRUE, 0);
		}
		g_list_free(widgets);
	}
	else
		g_warning("Unknown option type '%s'\n", name);

	g_free(label);
}

static void build_section(xmlNode *section, GtkWidget *notebook,
			  GtkTreeStore *tree_store, GtkTreeIter *parent)
{
	gchar 		*title = NULL;
	GtkWidget   	*sw, *page;
	GtkTreeIter	iter;
	xmlNode		*widget;

	title = (gchar *) xmlGetProp(section, BAD_CAST "title");

	/* Wrap each page in a GtkScrolledWindow so the dialog width is
	 * constrained by max_content_width while allowing vertical scroll
	 * for pages whose content exceeds the available height. */
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
			GTK_POLICY_NEVER,
			GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_width(
			GTK_SCROLLED_WINDOW(sw), 400);
	gtk_scrolled_window_set_max_content_width(
			GTK_SCROLLED_WINDOW(sw), 600);
	gtk_scrolled_window_set_shadow_type(
			GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(page), 4);
	gtk_container_add(GTK_CONTAINER(sw), page);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, NULL);

	gtk_tree_store_append(tree_store, &iter, parent);
	gtk_tree_store_set(tree_store, &iter, 0, _(title), 1, sw, -1);
	g_free(title);

	widget = section->xmlChildrenNode;
	for (; widget; widget = widget->next)
	{
		if (widget->type == XML_ELEMENT_NODE)
		{
			if (xmlStrcmp(widget->name, BAD_CAST "section") == 0)
				build_section(widget, notebook,
						tree_store, &iter);
			else
				build_widget(widget, page);
		}
	}
}

/* The cursor has been changed in the tree view, so switch to the new
 * page in the notebook.
 */
static void tree_cursor_changed(GtkTreeView *tv, gpointer data)
{
	GtkTreePath	*path = NULL;
	GtkNotebook	*nbook = GTK_NOTEBOOK(data);
	GtkTreeModel	*model;
	GtkWidget	*page = NULL;
	GtkTreeIter	iter;

	gtk_tree_view_get_cursor(tv, &path, NULL);
	if (!path)
		return;

	model = gtk_tree_view_get_model(tv);
	gtk_tree_model_get_iter(model, &iter, path);
	if (lasttreepath)
		gtk_tree_path_free(lasttreepath);
	lasttreepath = path;

	gtk_tree_model_get(model, &iter, 1, &page, -1);

	if (page)
	{
		gtk_notebook_set_current_page(nbook,
				gtk_notebook_page_num(nbook, page));
		g_object_unref(page);
	}
}

/* Parse <app_dir>/Options.xml to create the options window.
 * Sets the global 'window' variable.
 */
static void build_options_window(void)
{
	GtkTreeView	*tree;
	GtkTreeStore	*store;
	GtkWidget	*notebook;
	xmlDocPtr	options_doc;
	xmlNode		*options, *section;
	gchar		*path;

	notebook = build_window_frame(&tree);

	path = g_strconcat(app_dir, "/Options.xml", NULL);
	options_doc = xmlParseFile(path);

	if (!options_doc)
	{
		report_error(_("Internal error: %s unreadable"), path);
		g_free(path);
		return;
	}

	g_free(path);

	options = xmlDocGetRootElement(options_doc);
	if (xmlStrcmp(options->name, BAD_CAST "options") == 0)
	{
		store = (GtkTreeStore *) gtk_tree_view_get_model(tree);
		section = options->xmlChildrenNode;
		for (; section; section = section->next)
			if (section->type == XML_ELEMENT_NODE)
				build_section(section, notebook, store, NULL);

		gtk_tree_view_expand_all(tree);

		rox_dialog_set_parent_full(window, FALSE);
		gtk_widget_show_all(window);

		if (lasttreepath)
		{
			gtk_tree_view_set_cursor(tree, lasttreepath, NULL, FALSE);
		}
		else
		{
			lasttreepath = gtk_tree_path_new_first();
			gtk_tree_view_set_cursor(tree, lasttreepath, NULL, FALSE);
		}
	}

	xmlFreeDoc(options_doc);
	options_doc = NULL;
}

static void null_widget(gpointer key, gpointer value, gpointer data)
{
	Option	*option = (Option *) value;

	option->widget = NULL;
}

static void options_destroyed(GtkWidget *widget, gpointer data)
{
	if (current_fontsel_box)
		gtk_widget_destroy(GTK_WIDGET(current_fontsel_box));

	revert_widget = NULL;

	if (check_anything_changed())
		save_options();

	if (widget == window)
	{
		window = NULL;

		g_hash_table_foreach(option_hash, null_widget, NULL);

		if (size_groups)
		{
			g_hash_table_destroy(size_groups);
			size_groups = NULL;

		}
	}
}

/* Creates the window and adds the various buttons to it.
 * Returns the notebook to add sections to and sets the global
 * 'window'. If 'tree_view' is non-NULL, it stores the address
 * of the tree view widget there.
 */
static GtkWidget *build_window_frame(GtkTreeView **tree_view)
{
	GtkWidget	*notebook;
	GtkWidget	*tl_vbox, *hbox, *frame, *tv;
	GtkWidget	*actions, *button;
	GtkTreeStore	*model;
	char		*string, *save_path;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(window), _("Options"));
	g_signal_connect(window, "destroy",
			G_CALLBACK(options_destroyed), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);

	tl_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add(GTK_CONTAINER(window), tl_vbox);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(tl_vbox), hbox, TRUE, TRUE, 0);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_end(GTK_BOX(hbox), frame, TRUE, TRUE, 0);

	notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
	gtk_container_add(GTK_CONTAINER(frame), notebook);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, TRUE, 0);

	/* tree view */
	model = gtk_tree_store_new(2, G_TYPE_STRING, GTK_TYPE_WIDGET);
	tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	g_object_unref(model);
	gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)),
			GTK_SELECTION_BROWSE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tv), -1,
			NULL, gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_container_add(GTK_CONTAINER(frame), tv);
	g_signal_connect(tv, "cursor_changed",
			G_CALLBACK(tree_cursor_changed), notebook);

	actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(actions),
				  GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(actions), 10);

	gtk_box_pack_start(GTK_BOX(tl_vbox), actions, FALSE, TRUE, 0);

	revert_widget = button_new_mixed("edit-undo", _("_Revert"));
	gtk_widget_set_can_default(revert_widget, TRUE);
	gtk_box_pack_start(GTK_BOX(actions), revert_widget, FALSE, TRUE, 0);
	g_signal_connect(revert_widget, "clicked",
			 G_CALLBACK(revert_options), NULL);

	gtk_widget_set_tooltip_text(revert_widget,
			_("Restore all choices to how they were when the "
			  "Options box was opened."));
	gtk_widget_set_sensitive(revert_widget, check_anything_changed());

	button = button_new_mixed("gtk-ok", _("_OK"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	g_signal_connect_swapped(button, "clicked",
				G_CALLBACK(gtk_widget_destroy), window);
	gtk_widget_grab_default(button);
	gtk_widget_grab_focus(tv);

	save_path = choices_find_xdg_path_save("...", project_name, SITE, FALSE);
	if (save_path)
	{
		string = g_strdup_printf(_("Choices will be saved as:\n%s"),
					save_path);
		gtk_widget_set_tooltip_text(button, string);
		g_free(string);
		g_free(save_path);
	}
	else
		gtk_widget_set_tooltip_text(button,
				_("(saving disabled by CHOICESPATH)"));

	if (tree_view)
		*tree_view = GTK_TREE_VIEW(tv);

	return notebook;
}

/* Given the last radio button in the group, select whichever
 * radio button matches the given value.
 */
static void radio_group_set_value(GtkRadioButton *last, gchar *value)
{
	GSList	*next;

	for (next = gtk_radio_button_get_group(last); next; next = next->next)
	{
		GtkToggleButton *button = (GtkToggleButton *) next->data;
		gchar	*val;

		val = g_object_get_data(G_OBJECT(button), "value");
		g_return_if_fail(val != NULL);

		if (strcmp(val, value) == 0)
		{
			gtk_toggle_button_set_active(button, TRUE);
			return;
		}
	}

	g_warning("Can't find radio button with value %s\n", value);
}

/* Given the last radio button in the group, return a copy of the
 * value for the selected radio item.
 */
static gchar *radio_group_get_value(GtkRadioButton *last)
{
	GSList	*next;

	for (next = gtk_radio_button_get_group(last); next; next = next->next)
	{
		GtkToggleButton *button = (GtkToggleButton *) next->data;

		if (gtk_toggle_button_get_active(button))
		{
			gchar	*val;

			val = g_object_get_data(G_OBJECT(button), "value");
			g_return_val_if_fail(val != NULL, NULL);

			return g_strdup(val);
		}
	}

	return NULL;
}

static void update_menu(Option *option)
{
	/* Select this item with this value */
	GtkComboBox *om = GTK_COMBO_BOX(option->widget);
	GPtrArray *values = g_object_get_data(G_OBJECT(om), "om_values");

	gint i = 0;
	for (; i < values->len; i++)
		if (strcmp(values->pdata[i], option->value) == 0)
		{
			gtk_combo_box_set_active(om, i);
			break;
		}
}

static gchar *read_menu(Option *option)
{
	GtkComboBox *om = GTK_COMBO_BOX(option->widget);
	GPtrArray *values = g_object_get_data(G_OBJECT(om), "om_values");
	gint i = gtk_combo_box_get_active(om);

	return i < 0 ? NULL : g_strdup(values->pdata[i]);
}

static void restore_backup(gpointer key, gpointer value, gpointer data)
{
	Option *option = (Option *) value;

	g_return_if_fail(option->backup != NULL);

	option->has_changed = strcmp(option->value, option->backup) != 0;
	if (!option->has_changed)
		return;

	g_free(option->value);
	option->value = g_strdup(option->backup);
	option->int_value = atoi(option->value);
}

static void revert_options(GtkWidget *widget, gpointer data)
{
	g_hash_table_foreach(option_hash, restore_backup, NULL);
	options_notify();
	update_option_widgets();
}

static void check_changed_cb(gpointer key, gpointer value, gpointer data)
{
	Option *option = (Option *) value;
	gboolean *changed = (gboolean *) data;

	g_return_if_fail(option->backup != NULL);

	if (*changed)
		return;

	if (strcmp(option->value, option->backup) != 0)
		*changed = TRUE;
}

static gboolean check_anything_changed(void)
{
	gboolean retval = FALSE;

	g_hash_table_foreach(option_hash, check_changed_cb, &retval);

	return retval;
}

static void write_option(gpointer key, gpointer value, gpointer data)
{
	xmlNodePtr doc = (xmlNodePtr) data;
	Option *option = (Option *) value;
	xmlNodePtr tree;

	tree = xmlNewTextChild(doc, NULL, BAD_CAST "Option",
			      (const xmlChar *) option->value);
	xmlSetProp(tree, BAD_CAST "name", (const xmlChar *) key);
}

static void save_options(void)
{
	xmlDoc	*doc;
	GList	*next;
	gchar	*save, *save_new;

	save = choices_find_xdg_path_save("Options", project_name, SITE, TRUE);
	if (!save)
		goto out;

	save_new = g_strconcat(save, ".new", NULL);

	doc = xmlNewDoc(BAD_CAST "1.0");
	xmlDocSetRootElement(doc, xmlNewDocNode(doc, NULL, BAD_CAST "Options", NULL));
	{
		xmlNodePtr root = xmlDocGetRootElement(doc);
		gchar *version = g_strdup_printf("%d", OPTIONS_FILE_VERSION);

		xmlSetProp(root, BAD_CAST "version", BAD_CAST version);
		g_free(version);

		g_hash_table_foreach(option_hash, write_option, root);
	}

	if (save_xml_file(doc, save_new) || rename(save_new, save))
		report_error(_("Error saving %s: %s"), save, g_strerror(errno));

	g_free(save_new);
	g_free(save);
	xmlFreeDoc(doc);

	for (next = saver_callbacks; next; next = next->next)
	{
		OptionNotify *cb = (OptionNotify *) next->data;
		cb();
	}

out:
	if (window)
		gtk_widget_destroy(window);
}

/* Make the widget reflect the current value of the option */
static void update_cb(gpointer key, gpointer value, gpointer data)
{
	Option *option = (Option *) value;

	g_return_if_fail(option != NULL);
	if (option->widget == NULL)
		return;

	updating_widgets++;

	if (option->update_widget)
		option->update_widget(option);

	updating_widgets--;
}

/* Reflect the values in the Option structures by changing the widgets
 * in the Options window.
 */
static void update_option_widgets(void)
{
	g_hash_table_foreach(option_hash, update_cb, NULL);
}

/* Each of the following update the widget to make it show the current
 * value of the option.
 */

static void update_toggle(Option *option)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option->widget),
			option->int_value);
}

static void update_entry(Option *option)
{
	gtk_entry_set_text(GTK_ENTRY(option->widget), option->value);
}

static void update_numentry(Option *option)
{
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(option->widget),
			option->int_value);
}

static void update_radio_group(Option *option)
{
	radio_group_set_value(GTK_RADIO_BUTTON(option->widget), option->value);
}

static void update_slider(Option *option)
{
	gtk_adjustment_set_value(
			gtk_range_get_adjustment(GTK_RANGE(option->widget)),
			option->int_value);
}

static void update_font(Option *option)
{
	GtkToggleButton *active;
	gboolean have_font = option->value[0] != '\0';

	active = g_object_get_data(G_OBJECT(option->widget), "rox_override");

	if (active)
	{
		gtk_toggle_button_set_active(active, have_font);
		gtk_widget_set_sensitive(gtk_widget_get_parent(option->widget), have_font);
	}

	gtk_label_set_text(GTK_LABEL(option->widget),
			   have_font ? option->value
				     : (gchar *) _("(use default)"));
}

static void update_colour(Option *option)
{
	GdkRGBA colour;

	gdk_rgba_parse(&colour, option->value);
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(option->widget), &colour);
}

/* Each of these read_* calls get the new (string) value of an option
 * from the widget.
 */

static gchar *read_toggle(Option *option)
{
	GtkToggleButton *toggle = GTK_TOGGLE_BUTTON(option->widget);

	return g_strdup_printf("%d", gtk_toggle_button_get_active(toggle));
}

static gchar *read_entry(Option *option)
{
	return gtk_editable_get_chars(GTK_EDITABLE(option->widget), 0, -1);
}

static gchar *read_numentry(Option *option)
{
	return g_strdup_printf("%d", (int)
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(option->widget)));
}

static gchar *read_slider(Option *option)
{
	return g_strdup_printf("%d", (int)
		gtk_adjustment_get_value(gtk_range_get_adjustment(GTK_RANGE(option->widget))));
}

static gchar *read_radio_group(Option *option)
{
	return radio_group_get_value(GTK_RADIO_BUTTON(option->widget));
}

static gchar *read_font(Option *option)
{
	GtkToggleButton *active;

	active = g_object_get_data(G_OBJECT(option->widget), "rox_override");
	if (active && !gtk_toggle_button_get_active(active))
		return g_strdup("");

	return g_strdup(gtk_label_get_text(GTK_LABEL(option->widget)));
}

static gchar *read_colour(Option *option)
{
	GdkRGBA colour;

	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(option->widget), &colour);

	return g_strdup_printf("#%04x%04x%04x",
			(int)(colour.red * 65535),
			(int)(colour.green * 65535),
			(int)(colour.blue * 65535));
}

static void set_not_changed(gpointer key, gpointer value, gpointer data)
{
	Option	*option = (Option *) value;

	option->has_changed = FALSE;
}

/* Builders for decorations (no corresponding option) */

static GList *build_label(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *widget;
	gchar *text;
	int help;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label == NULL, NULL);

	text = DATA(node);
	widget = gtk_label_new(_(text));
	g_free(text);

	help = get_int(node, "help");

	gtk_widget_set_halign(widget, GTK_ALIGN_START);
	gtk_widget_set_valign(widget, help ? GTK_ALIGN_CENTER : GTK_ALIGN_END);
	gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);

	if (help)
	{
		GtkWidget *hbox, *image, *spacer;

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		image = gtk_image_new_from_icon_name("dialog-information",
				GTK_ICON_SIZE_BUTTON);

		gtk_widget_set_halign(image, GTK_ALIGN_START);
		gtk_widget_set_valign(image, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, TRUE, 0);

		spacer = gtk_event_box_new();
		gtk_widget_set_size_request(spacer, 6, 6);

		return g_list_append(g_list_append(NULL, hbox), spacer);
	}

	return g_list_append(NULL, widget);
}

static GList *build_spacer(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *eb;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label == NULL, NULL);

	eb = gtk_event_box_new();
	gtk_widget_set_size_request(eb, 12, 12);

	return g_list_append(NULL, eb);
}


static GList *build_grid(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *grid;
	xmlNode	  *hw;

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), get_int(node, "row-spacing"));
	gtk_grid_set_column_spacing(GTK_GRID(grid), get_int(node, "column-spacing"));

	for (hw = node->xmlChildrenNode; hw; hw = hw->next)
		if (hw->type == XML_ELEMENT_NODE)
			build_widget(hw, grid);

	return g_list_append(NULL, grid);
}

static GList *build_frame(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget *nbox, *frame, *label_widget;
	xmlNode	  *hw;
	PangoAttrList *list;
	PangoAttribute *attr;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);

	frame = gtk_frame_new(_(label));
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);

	/* Make the title bold */
	label_widget = gtk_frame_get_label_widget(GTK_FRAME(frame));
	attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);

	attr->start_index = 0;
	attr->end_index = -1;
	list = pango_attr_list_new();
	pango_attr_list_insert(list, attr);
	gtk_label_set_attributes(GTK_LABEL(label_widget), list);

	nbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(nbox), 12);
	gtk_container_add(GTK_CONTAINER(frame), nbox);

	for (hw = node->xmlChildrenNode; hw; hw = hw->next)
		if (hw->type == XML_ELEMENT_NODE)
			build_widget(hw, nbox);

	return g_list_append(NULL, frame);
}

/* These create new widgets in the options window and set the appropriate
 * callbacks.
 */

static GList *build_toggle(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget	*toggle;

	g_return_val_if_fail(option != NULL, NULL);

	toggle = gtk_check_button_new_with_label(_(label));

	may_add_tip(toggle, node);

	option->update_widget = update_toggle;
	option->read_widget = read_toggle;
	option->widget = toggle;

	g_signal_connect_swapped(toggle, "toggled",
			G_CALLBACK(option_check_widget), option);

	return g_list_append(NULL, toggle);
}

static GList *build_slider(Option *option, xmlNode *node, gchar *label)
{
	GtkAdjustment *adj;
	GtkWidget *hbox, *slide, *label_wid;
	int	min, max;
	int	fixed;
	int	showvalue;
	gchar	*end;

	g_return_val_if_fail(option != NULL, NULL);

	min = get_int(node, "min");
	max = get_int(node, "max");
	fixed = get_int(node, "fixed");
	showvalue = get_int(node, "showvalue");

	adj = GTK_ADJUSTMENT(gtk_adjustment_new(0,
				min, max, 1, 10, 0));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (label)
	{
		label_wid = gtk_label_new(_(label));
		gtk_widget_set_halign(label_wid, GTK_ALIGN_START);
		gtk_widget_set_valign(label_wid, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), label_wid, FALSE, TRUE, 0);
		add_to_size_group(node, label_wid);
	}

	end = (gchar *) xmlGetProp(node, BAD_CAST "end");
	if (end)
	{
		gtk_box_pack_end(GTK_BOX(hbox), gtk_label_new(_(end)),
				 FALSE, TRUE, 0);
		g_free(end);
	}

	slide = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);

	if (fixed)
		gtk_widget_set_size_request(slide, gtk_adjustment_get_upper(adj), 24);
	if (showvalue)
	{
		gtk_scale_set_draw_value(GTK_SCALE(slide), TRUE);
		gtk_scale_set_value_pos(GTK_SCALE(slide),
				GTK_POS_LEFT);
		gtk_scale_set_digits(GTK_SCALE(slide), 0);
	}
	else
		gtk_scale_set_draw_value(GTK_SCALE(slide), FALSE);
	gtk_widget_set_can_focus(slide, FALSE);

	may_add_tip(slide, node);

	gtk_box_pack_start(GTK_BOX(hbox), slide, !fixed, TRUE, 0);

	option->update_widget = update_slider;
	option->read_widget = read_slider;
	option->widget = slide;

	g_signal_connect_swapped(adj, "value-changed",
			G_CALLBACK(option_check_widget), option);

	return g_list_append(NULL, hbox);
}

static GList *build_entry(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget	*hbox;
	GtkWidget	*entry;
	GtkWidget	*label_wid;

	g_return_val_if_fail(option != NULL, NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (label)
	{
		label_wid = gtk_label_new(_(label));
		gtk_widget_set_halign(label_wid, GTK_ALIGN_END);
		gtk_widget_set_valign(label_wid, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), label_wid, FALSE, TRUE, 0);
	}

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
	add_to_size_group(node, entry);
	may_add_tip(entry, node);

	option->update_widget = update_entry;
	option->read_widget = read_entry;
	option->widget = entry;

	g_signal_connect_data(entry, "changed",
			G_CALLBACK(option_check_widget), option,
			NULL, G_CONNECT_AFTER | G_CONNECT_SWAPPED);

	return g_list_append(NULL, hbox);
}

static GList *build_numentry(Option *option, xmlNode *node, gchar *label)
{
	GtkAdjustment *adj;
	int	min, max, step;

	g_return_val_if_fail(option != NULL, NULL);

	min = get_int(node, "min");
	max = get_int(node, "max");
	step = MAX(1, get_int(node, "step"));

	adj = gtk_adjustment_new(min, min, max, step, step * 10, 0);

	return build_numentry_base(option, node, label, GTK_ADJUSTMENT(adj));
}

static GList *build_radio_group(Option *option, xmlNode *node, gchar *label)
{
	GList		*list = NULL;
	GtkWidget	*button = NULL;
	xmlNode		*rn;
	int		cols;

	g_return_val_if_fail(option != NULL, NULL);

	for (rn = node->xmlChildrenNode; rn; rn = rn->next)
	{
		if (rn->type == XML_ELEMENT_NODE)
		{
			button = build_radio(rn, button);
			g_signal_connect_swapped(button, "toggled",
				G_CALLBACK(option_check_widget), option);
			list = g_list_append(list, button);
		}
	}

	option->update_widget = update_radio_group;
	option->read_widget = read_radio_group;
	option->widget = button;

	cols = get_int(node, "columns");
	if (cols > 1)
	{
		GtkWidget *grid;
		GList	*next;
		int	i, n;
		int	rows;

		n = g_list_length(list);
		rows = (n + cols - 1) / cols;

		grid = gtk_grid_new();
		gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

		i = 0;
		for (next = list; next; next = next->next)
		{
			GtkWidget *button = GTK_WIDGET(next->data);
			int left = i / rows;
			int top = i % rows;

			gtk_grid_attach(GTK_GRID(grid), button,
					left, top, 1, 1);

			i++;
		}

		g_list_free(list);
		list = g_list_prepend(NULL, grid);
	}

	return list;
}

static GList *build_colour(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget	*hbox, *button, *label_wid;

	g_return_val_if_fail(option != NULL, NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (label)
	{
		label_wid = gtk_label_new(_(label));
		gtk_widget_set_halign(label_wid, GTK_ALIGN_END);
		gtk_widget_set_valign(label_wid, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), label_wid, TRUE, TRUE, 0);
	}

	button = gtk_color_button_new();
	gtk_widget_set_size_request(button, 64, -1);

	may_add_tip(button, node);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);

	option->update_widget = update_colour;
	option->read_widget = read_colour;
	option->widget = button;

	g_signal_connect_swapped(button, "color-set",
			G_CALLBACK(option_check_widget), option);

	return g_list_append(NULL, hbox);
}

//freeing names is caller's duty
GList *options_build_menu(
		Option *option, gchar *label, GPtrArray *names, GPtrArray *values)
{
	GtkWidget *hbox, *om, *label_wid;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	label_wid = gtk_label_new(_(label));
	gtk_widget_set_halign(label_wid, GTK_ALIGN_END);
		gtk_widget_set_valign(label_wid, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(hbox), label_wid, TRUE, TRUE, 0);

	om = gtk_combo_box_text_new();
	g_object_set_data(G_OBJECT(om), "om_values", values);

	gtk_box_pack_start(GTK_BOX(hbox), om, FALSE, TRUE, 0);

	gint i = 0;
	for (; i < names->len; i++)
		gtk_combo_box_text_append_text(
					GTK_COMBO_BOX_TEXT(om), names->pdata[i]);

//	gtk_widget_show(om);//todo check this

	option->update_widget = update_menu;
	option->read_widget = read_menu;
	option->widget = om;

	g_signal_connect_data(om, "changed",
			G_CALLBACK(option_check_widget), option,
			NULL, G_CONNECT_AFTER | G_CONNECT_SWAPPED);

	return g_list_append(NULL, hbox);
}

static GList *build_menu(Option *option, xmlNode *node, gchar *label)
{
	g_return_val_if_fail(option != NULL, NULL);

	xmlNode   *item;
	GPtrArray *names = g_ptr_array_new();
	GPtrArray *values = g_ptr_array_new();

	for (item = node->xmlChildrenNode; item; item = item->next)
	{
		if (item->type == XML_ELEMENT_NODE &&
				xmlStrcmp(item->name, BAD_CAST "item") == 0)
		{
			gchar *lbl = (gchar *) xmlGetProp(item, BAD_CAST "label");
			gchar *gt = _(lbl);
			if (gt != lbl)
			{
				g_free(lbl);
				gt = g_strdup(gt);
			}
			g_ptr_array_add(names, gt);

			g_ptr_array_add(values, xmlGetProp(item, BAD_CAST "value"));
		}
	}

	GList *ret =
		options_build_menu(option, label, names, values);

	g_ptr_array_free(names, TRUE);

	add_to_size_group(node, option->widget);

	return ret;
}

static GList *build_font(Option *option, xmlNode *node, gchar *label)
{
	GtkWidget	*hbox, *button;
	GtkWidget	*active = NULL;
	int		override;

	g_return_val_if_fail(option != NULL, NULL);

	override = get_int(node, "override");

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (override)
	{
		/* Add a check button to enable the font chooser. If off,
		 * the option's value is "".
		 */
		active = gtk_check_button_new_with_label(_(label));
		gtk_box_pack_start(GTK_BOX(hbox), active, FALSE, TRUE, 0);
		g_signal_connect(active, "toggled",
				 G_CALLBACK(toggle_active_font), option);
	}
	else
		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
				FALSE, TRUE, 0);

	button = gtk_button_new_with_label("");
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);

	option->update_widget = update_font;
	option->read_widget = read_font;
	option->widget = gtk_bin_get_child(GTK_BIN(button));
	may_add_tip(button, node);

	g_object_set_data(G_OBJECT(option->widget), "rox_override", active);

	g_signal_connect(button, "clicked", G_CALLBACK(open_fontsel), option);

	return g_list_append(NULL, hbox);
}


static void load_options(xmlDoc *doc)
{
	xmlNode *root, *node;
	xmlChar *version;

	root = xmlDocGetRootElement(doc);

	g_return_if_fail(xmlStrcmp(root->name, BAD_CAST "Options") == 0);

	/* Absent means a legacy (or pre-tag) file: version 0.
	 * A newer-than-known version warns and continues [20260928124821].
	 */
	options_file_version = 0;
	version = xmlGetProp(root, BAD_CAST "version");
	if (version)
	{
		options_file_version = atoi((const char *) version);
		xmlFree(version);
		if (options_file_version > OPTIONS_FILE_VERSION)
		{
			g_warning("Options file version %d is newer than "
				"the supported version %d; continuing",
				options_file_version, OPTIONS_FILE_VERSION);
		}
	}

	for (node = root->xmlChildrenNode; node; node = node->next)
	{
		gchar *value, *name;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, BAD_CAST "Option") != 0)
			continue;
		name = (gchar *) xmlGetProp(node, BAD_CAST "name");
		if (!name)
			continue;

		value = (gchar *) xmlNodeGetContent(node);

		if (g_hash_table_lookup(loading, name))
			g_warning("Duplicate option found!");

		g_hash_table_insert(loading, name, value);

		/* (don't need to free name or value) */
	}
}

/* Process one line from the options file (\0 term'd).
 * Returns NULL on success, or a pointer to an error message.
 * The line is modified.
 */
static const char *process_option_line(gchar *line)
{
	gchar 		*eq, *c;
	gchar		*name = line;

	g_return_val_if_fail(option_hash != NULL, "No registered options!");

	eq = strchr(line, '=');
	if (!eq)
		return _("Missing '='");

	c = eq - 1;
	while (c > line && (*c == ' ' || *c == '\t'))
		c--;
	c[1] = '\0';
	c = eq + 1;
	while (*c == ' ' || *c == '\t')
		c++;

	if (g_hash_table_lookup(loading, name))
		return "Duplicate option found!";

	g_hash_table_insert(loading, g_strdup(name), g_strdup(g_strstrip(c)));

	return NULL;
}
