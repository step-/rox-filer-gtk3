/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
 * Copyright (C) 2026, step <step.commons@gmail.com>.
 *
 * The collection widget provides an area for displaying a collection of
 * objects (such as files). It allows the user to choose a selection of
 * them and provides signals to allow popping up menus, detecting
 * double-clicks etc.
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

#include "config.h"

#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "global.h"

#include "collection.h"
#include "filer.h"
#include "options.h"
#include "gui_support.h"
#include "view_utils.h"

#define MIN_WIDTH 80
#define MIN_HEIGHT 60
#define MINIMUM_ITEMS 16

#define MAX_WINKS 7		/* Should be an odd number */

/* Macro to emit the "selection_changed" signal only if allowed */
#define EMIT_SELECTION_CHANGED(collection, time) \
	if (!collection->block_selection_changed) \
		g_signal_emit(collection, \
				collection_signals[SELECTION_CHANGED], 0, time)

enum
{
	PROP_0,
	PROP_VADJUSTMENT
};

/* Signals:
 *
 * void gain_selection(collection, time, user_data)
 * 	We've gone from no selected items to having a selection.
 * 	Time is the time of the event that caused the change, or
 * 	GDK_CURRENT_TIME if not known.
 *
 * void lose_selection(collection, time, user_data)
 * 	We've dropped to having no selected items.
 * 	Time is the time of the event that caused the change, or
 * 	GDK_CURRENT_TIME if not known.
 *
 * void selection_changed(collection, user_data)
 * 	The set of selected items has changed.
 * 	Time is the time of the event that caused the change, or
 * 	GDK_CURRENT_TIME if not known.
 */
enum
{
	GAIN_SELECTION,
	LOSE_SELECTION,
	SELECTION_CHANGED,
	LAST_SIGNAL
};

static guint collection_signals[LAST_SIGNAL] = { 0 };

static guint32 current_event_time = GDK_CURRENT_TIME;

static GtkWidgetClass *parent_class = NULL;

/* Static prototypes */
static void draw_one_item(Collection 	*collection,
			  int 		item,
			  GdkRectangle 	*area,
			  cairo_t	*cr);
static void collection_class_init(GObjectClass *gclass, gpointer data);
static void collection_init(GTypeInstance *object, gpointer g_class);
static void collection_dispose(GObject *object);
static void collection_finalize(GObject *object);
static void collection_realize(GtkWidget *widget);
static void collection_map(GtkWidget *widget);
static void collection_get_preferred_width(GtkWidget *widget,
					   gint *minimum_width,
					   gint *natural_width);
static void collection_get_preferred_height(GtkWidget *widget,
					    gint *minimum_height,
					    gint *natural_height);
static void collection_size_allocate(GtkWidget 		*widget,
			     GtkAllocation 	*allocation);
static void collection_set_adjustment(Collection 	*collection,
				      GtkAdjustment 	*vadj);
static void collection_get_property(GObject    *object,
				    guint      prop_id,
				    GValue     *value,
				    GParamSpec *pspec);
static void collection_set_property(GObject      *object,
				    guint        prop_id,
				    const GValue *value,
				    GParamSpec   *pspec);
static gboolean collection_draw(GtkWidget *widget, cairo_t *cr);
static void default_draw_item(GtkWidget *widget,
				int idx,
				GdkRectangle *area,
				gpointer user_data,
				gboolean cursor,
				cairo_t *cr);
static gboolean	default_test_point(Collection *collection,
				   int point_x, int point_y,
				   CollectionItem *data,
				   int width, int height,
				   gpointer user_data);
static gint collection_motion_notify(GtkWidget *widget,
				     GdkEventMotion *event);
static void add_lasso_box(Collection *collection);
static void abort_lasso(Collection *collection);
static void remove_lasso_box(Collection *collection);
static void draw_lasso_box(Collection *collection);
static void cancel_wink(Collection *collection);
static gint collection_key_press(GtkWidget *widget, GdkEventKey *event);
static void get_visible_limits(Collection *collection, int *first, int *last);
static void scroll_to_show(Collection *collection, int item);
static void collection_item_set_selected(Collection *collection,
                                         gint item,
                                         gboolean selected,
					 gboolean signal);
static gint collection_scroll_event(GtkWidget *widget, GdkEventScroll *event);
static int collection_get_rows(const Collection *collection);
static int collection_get_cols(const Collection *collection);


/* The number of rows, at least 1.  */
static inline int collection_get_rows(const Collection *collection)
{
	int rows = (collection->number_of_items + collection->columns - 1) /
		collection->columns;
	return MAX(rows, 1);
}

/* The number of columns _actually_ displayed, at least 1.  This
 * function is required in vertical_order layout-based manipulation
 * such as moving the cursor to detect the last column.  */
static inline int collection_get_cols(const Collection *collection)
{
	if (collection->vertical_order)
	{
		int rows = collection_get_rows(collection);
		int cols = (collection->number_of_items + rows - 1) / rows;
		return MAX(1, cols);
	}
	else
		return collection->columns;
}

static void draw_one_item(Collection *collection, int item, GdkRectangle *area, cairo_t *cr)
{
	if (item < collection->number_of_items)
	{
		cairo_save(cr);
		collection->draw_item((GtkWidget *) collection,
				item,
				area,
				collection->cb_user_data,
				item == collection->cursor_item,
				cr);
		cairo_restore(cr);
	}
}

GType collection_get_type(void)
{
	static GType my_type = 0;

	if (!my_type)
	{
		static const GTypeInfo info =
		{
			sizeof(CollectionClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			(GClassInitFunc) collection_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(Collection),
			0,			/* n_preallocs */
			collection_init
		};

		my_type = g_type_register_static(gtk_widget_get_type(),
					"Collection", &info, 0);
	}

	return my_type;
}

typedef void (*FinalizeFn)(GObject *object);

static void collection_class_init(GObjectClass *gclass, gpointer data)
{
	CollectionClass *collection_class = (CollectionClass *) gclass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) gclass;

	parent_class = g_type_class_ref(gtk_widget_get_type());

	gclass->dispose = collection_dispose;
	gclass->finalize = collection_finalize;

	widget_class->realize = collection_realize;
	widget_class->draw = collection_draw;
	widget_class->get_preferred_width = collection_get_preferred_width;
	widget_class->get_preferred_height = collection_get_preferred_height;
	widget_class->size_allocate = collection_size_allocate;

	widget_class->key_press_event = collection_key_press;

	widget_class->motion_notify_event = collection_motion_notify;
	widget_class->map = collection_map;
	widget_class->scroll_event = collection_scroll_event;

	gtk_widget_class_set_css_name(widget_class, "Collection");

	gclass->set_property = collection_set_property;
	gclass->get_property = collection_get_property;

	collection_class->gain_selection = NULL;
	collection_class->lose_selection = NULL;
	collection_class->selection_changed = NULL;

	collection_signals[GAIN_SELECTION] = g_signal_new("gain_selection",
					G_TYPE_FROM_CLASS(gclass),
					G_SIGNAL_RUN_LAST,
					G_STRUCT_OFFSET(CollectionClass,
							gain_selection),
					NULL, NULL,
					g_cclosure_marshal_VOID__INT,
					G_TYPE_NONE, 1,
					G_TYPE_INT);

	collection_signals[LOSE_SELECTION] = g_signal_new("lose_selection",
					G_TYPE_FROM_CLASS(gclass),
					G_SIGNAL_RUN_LAST,
					G_STRUCT_OFFSET(CollectionClass,
							lose_selection),
					NULL, NULL,
					g_cclosure_marshal_VOID__INT,
					G_TYPE_NONE, 1,
					G_TYPE_INT);

	collection_signals[SELECTION_CHANGED] = g_signal_new(
					"selection_changed",
					G_TYPE_FROM_CLASS(gclass),
					G_SIGNAL_RUN_LAST,
					G_STRUCT_OFFSET(CollectionClass,
							selection_changed),
					NULL, NULL,
					g_cclosure_marshal_VOID__INT,
					G_TYPE_NONE, 1,
					G_TYPE_INT);

	g_object_class_install_property(gclass,
		PROP_VADJUSTMENT,
		g_param_spec_object("vadjustment",
			"Vertical Adjustment",
			"The GtkAdjustment for the vertical position.",
			GTK_TYPE_ADJUSTMENT,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void collection_init(GTypeInstance *instance, gpointer g_class)
{
	Collection *object = (Collection *) instance;

	g_return_if_fail(object != NULL);
	g_return_if_fail(IS_COLLECTION(object));

	gtk_widget_set_can_focus(GTK_WIDGET(object), TRUE);

	object->number_of_items = 0;
	object->number_selected = 0;
	object->block_selection_changed = 0;
	object->columns = 1;
	object->vertical_order = FALSE;
	object->center_wink = TRUE;
	object->item_width = 1;
	object->item_height = 1;
	object->reached_scale = .0;
	object->old_height = object->old_pos = 0;
	object->vadj = NULL;

	object->items = g_new(CollectionItem, MINIMUM_ITEMS);
	object->cursor_item = -1;
	object->cursor_item_old = -1;
	object->wink_item = -1;
	object->wink_on_map = -1;
	object->array_size = MINIMUM_ITEMS;
	object->draw_item = default_draw_item;
	object->test_point = default_test_point;
	object->free_item = NULL;
}

GtkWidget* collection_new(void)
{
	return GTK_WIDGET(gtk_widget_new(collection_get_type(), NULL));
}

/* After this we are unusable, but our data (if any) is still hanging around.
 * It will be freed later with finalize. Drop references here.
 */
static void collection_dispose(GObject *object)
{
	Collection *collection;

	collection = COLLECTION(object);

	collection_clear(collection);

	if (collection->vadj)
	{
		g_object_unref(G_OBJECT(collection->vadj));
		collection->vadj = NULL;
	}

	if (G_OBJECT_CLASS(parent_class)->dispose)
		G_OBJECT_CLASS(parent_class)->dispose(object);
}

/* This is the last thing that happens to us. Free all data. */
static void collection_finalize(GObject *object)
{
	Collection *collection;

	collection = COLLECTION(object);

	g_return_if_fail(collection->number_of_items == 0);

	g_free(collection->items);

	if (G_OBJECT_CLASS(parent_class)->finalize)
		G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void collection_map(GtkWidget *widget)
{
	Collection *collection = COLLECTION(widget);

	if (GTK_WIDGET_CLASS(parent_class)->map)
		(*GTK_WIDGET_CLASS(parent_class)->map)(widget);

	if (collection->wink_on_map >= 0)
	{
		scroll_to_show(collection, collection->wink_on_map);
		collection_wink_item(collection, collection->wink_on_map);
		collection->wink_on_map = -1;
	}

	collection->center_wink = FALSE;
}

static void collection_realize(GtkWidget *widget)
{
	GdkWindowAttr 	attributes;
	gint 		attributes_mask;
	GtkAllocation 	alloc;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_COLLECTION(widget));
	g_return_if_fail(gtk_widget_get_parent(widget) != NULL);

	gtk_widget_set_realized(widget, TRUE);

	gtk_widget_get_allocation(widget, &alloc);
	attributes.x = alloc.x;
	attributes.y = alloc.y;
	attributes.width = alloc.width;
	attributes.height = alloc.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events(widget) |
		GDK_EXPOSURE_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
		GDK_BUTTON3_MOTION_MASK;
	attributes.visual = gtk_widget_get_visual(widget);

	attributes_mask = GDK_WA_X | GDK_WA_Y |
				GDK_WA_VISUAL;
	gtk_widget_set_window(widget, gdk_window_new(gtk_widget_get_parent_window(widget),
			&attributes, attributes_mask));

	gdk_window_set_user_data(gtk_widget_get_window(widget), widget);
}

static void collection_get_preferred_width(GtkWidget *widget,
					   gint *minimum_width,
					   gint *natural_width)
{
	*minimum_width = MIN_WIDTH;
	*natural_width = MIN_WIDTH;
}

static void collection_get_preferred_height(GtkWidget *widget,
					    gint *minimum_height,
					    gint *natural_height)
{
	Collection *collection = COLLECTION(widget);
	int rows = collection_get_rows(collection);
	int height = rows * collection->item_height;
	*minimum_height = height;
	*natural_height = height;
}

static gboolean scroll_after_alloc(Collection *collection)
{
	if (collection->wink_item != -1)
		scroll_to_show(collection, collection->wink_item);
	else if (collection->cursor_item != -1)
		scroll_to_show(collection, collection->cursor_item);
	g_object_unref(G_OBJECT(collection));

	if (collection->wink_on_map < 0)
		collection->center_wink = FALSE;

	return FALSE;
}

static void collection_size_allocate(GtkWidget *widget,
				GtkAllocation *allocation)
{
	Collection 	*collection;
	int		old_columns;
	gboolean	cursor_visible = FALSE;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_COLLECTION(widget));
	g_return_if_fail(allocation != NULL);

	collection = COLLECTION(widget);

	if (collection->cursor_item != -1)
	{
		int	first, last;
		int	crow, ccol;

		collection_item_to_rowcol(collection, collection->cursor_item,
					  &crow, &ccol);

		get_visible_limits(collection, &first, &last);

		cursor_visible = crow >= first && crow <= last;
	}

	old_columns = collection->columns;

	gtk_widget_set_allocation(widget, allocation);

	collection->columns = allocation->width / collection->item_width;
	if (collection->columns < 1)
		collection->columns = 1;

	if (gtk_widget_get_realized(widget))
	{
		gdk_window_move_resize(gtk_widget_get_window(widget),
				allocation->x, allocation->y,
				allocation->width, allocation->height);

		if (cursor_visible)
			scroll_to_show(collection, collection->cursor_item);
	}

	if (collection->old_pos != 0 &&
		gtk_adjustment_get_value(collection->vadj) > 0
	){
		if (collection->winks_left > 0)
			collection->winks_left += MAX_WINKS - 1;

		gtk_adjustment_set_value(collection->vadj,
			CLAMP(
				(allocation->height / collection->old_height) *
				(collection->old_pos + gtk_adjustment_get_page_size(collection->vadj) * 3/7) -
				gtk_adjustment_get_page_size(collection->vadj) * 3/7,
				0,
				allocation->height - gtk_adjustment_get_page_size(collection->vadj)));

		collection->old_height = collection->old_pos = 0;
		gtk_widget_queue_draw(widget);
	}


	if (old_columns != collection->columns)
	{
		collection->old_height = allocation->height;
		collection->old_pos = gtk_adjustment_get_value(collection->vadj);

		/* Need to go around again... */
		gtk_widget_queue_resize(widget);
	}
	else if (collection->wink_item != -1
			|| (collection->cursor_item != -1 && cursor_visible)) /* [20260910150134] */
	{
		/* Viewport resets the adjustments after the alloc */
		g_object_ref(G_OBJECT(collection));
		scroll_after_alloc(collection);
	}
	else if (collection->wink_on_map < 0)
		collection->center_wink = FALSE;
}

/* Return the area occupied by the item at (row, col) by filling
 * in 'area'.
 */
static void collection_get_item_area(Collection *collection,
					int row, int col,
					GdkRectangle *area)

{
	area->x = col * collection->item_width;
	area->y = row * collection->item_height;

	area->width = collection->item_width;
	area->height = collection->item_height;
	if (col == collection->columns - 1)
		area->width <<= 1;
}

static gboolean collection_draw(GtkWidget *widget, cairo_t *cr)
{
	Collection	*collection;
	GdkRectangle	item_area;
	int		row, col;
	int		item;
	int		start_row, last_row;
	int		start_col, last_col;
	int		phys_last_col;
	gdouble x1, y1, x2, y2;
	GdkRectangle clip_area;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);

	{
		GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
		gtk_render_background(ctx, cr, 0, 0,
			gtk_widget_get_allocated_width(widget),
			gtk_widget_get_allocated_height(widget));
	}
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	collection = COLLECTION(widget);
	if (collection->item_width == 1) return FALSE;

	cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
	clip_area.x = (int) x1;
	clip_area.y = (int) y1;
	clip_area.width = (int) (x2 - x1);
	clip_area.height = (int) (y2 - y1);

	/* Calculate the ranges to plot */
	start_row = clip_area.y / collection->item_height;
	last_row = (clip_area.y + clip_area.height - 1)
		   / collection->item_height;

	if (last_row >= collection_get_rows(collection))
		last_row = collection_get_rows(collection) - 1;

	start_col = clip_area.x / collection->item_width;
	phys_last_col = (clip_area.x + clip_area.width - 1)
			/ collection->item_width;

	/* The right-most column may be wider than the others.
	 * Therefore, to redraw the area after the last 'real' column
	 * we may have to draw the right-most column.
	 */
	if (start_col >= collection->columns)
		start_col = collection->columns - 1;

	if (phys_last_col >= collection->columns)
		last_col = collection->columns - 1;
	else
		last_col = phys_last_col;

	for(row = start_row; row <= last_row; row++)
		for(col = start_col; col <= last_col; col++)
		{
			item = collection_rowcol_to_item(collection, row, col);
			if (item == 0 || item < collection->number_of_items) {
				collection_get_item_area(collection,
						row, col, &item_area);
				draw_one_item(collection, item, &item_area, cr);
			}
		}

	if (collection->lasso_box)
	{
		int x, y, width, height;

		x = MIN(collection->drag_box_x[0], collection->drag_box_x[1]);
		y = MIN(collection->drag_box_y[0], collection->drag_box_y[1]);
		width = abs(collection->drag_box_x[1] - collection->drag_box_x[0]);
		height = abs(collection->drag_box_y[1] - collection->drag_box_y[0]);

		if (width && height)
			view_utils_draw_rubberband(widget, cr, x, y, width, height);
	}

	return FALSE;
}

static void default_draw_item(
		GtkWidget *widget,
		int idx,
		GdkRectangle *area,
		gpointer user_data,
		gboolean cursor,
		cairo_t *cr)
{
	GdkRGBA color;
	GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
	if (COLLECTION(widget)->items[idx].selected) {
		gtk_style_context_get_color(ctx, GTK_STATE_FLAG_SELECTED, &color);
	} else {
		gtk_style_context_get_color(ctx, GTK_STATE_NORMAL, &color);
	}
	cairo_set_source_rgb(cr, color.red, color.green, color.blue);
	cairo_arc(cr, area->x + COLLECTION(widget)->item_width / 2.0,
		  area->y + area->height / 2.0,
		  MIN(COLLECTION(widget)->item_width, area->height) / 2.0,
		  0, 2 * G_PI);
	cairo_fill(cr);
}


static gboolean	default_test_point(Collection *collection,
				   int point_x, int point_y,
				   CollectionItem *item,
				   int width, int height,
				   gpointer user_data)
{
	float	f_x, f_y;

	/* Convert to point in unit circle */
	f_x = ((float) point_x / width) - 0.5;
	f_y = ((float) point_y / height) - 0.5;

	return (f_x * f_x) + (f_y * f_y) <= .25;
}

static void collection_set_property(GObject      *object,
				    guint        prop_id,
				    const GValue *value,
				    GParamSpec   *pspec)
{
	Collection *collection;

	collection = COLLECTION(object);

	switch (prop_id)
	{
		case PROP_VADJUSTMENT:
			collection_set_adjustment(collection,
						  g_value_get_object(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,
							  prop_id, pspec);
			break;
	}
}

static void collection_set_adjustment(Collection    *collection,
				      GtkAdjustment *vadj)
{
	if (vadj)
		g_return_if_fail(GTK_IS_ADJUSTMENT(vadj));
	else
		vadj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0,
							 0.0, 0.0,
							 0.0, 0.0, 0.0));

	if (collection->vadj == vadj)
		return;

	if (collection->vadj)
		g_object_unref(G_OBJECT(collection->vadj));

	collection->vadj = vadj;
	g_object_ref_sink(collection->vadj);
}

static void collection_get_property(GObject    *object,
				    guint      prop_id,
				    GValue     *value,
				    GParamSpec *pspec)
{
	Collection *collection;

	collection = COLLECTION(object);

	switch (prop_id)
	{
		case PROP_VADJUSTMENT:
			g_value_set_object(value, G_OBJECT(collection->vadj));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,
							  prop_id, pspec);
			break;
	}
}

static void resize_arrays(Collection *collection, guint new_size)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(new_size >= collection->number_of_items);

	collection->items = g_realloc(collection->items,
					sizeof(CollectionItem) * new_size);
	collection->array_size = new_size;
}

static gint collection_key_press(GtkWidget *widget, GdkEventKey *event)
{
	Collection *collection;
	int	   key;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = (Collection *) widget;

	key = event->keyval;
	if (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
	{
		if (key == GDK_KEY_Left || key == GDK_KEY_KP_Left || \
			key == GDK_KEY_Right || key == GDK_KEY_KP_Right || \
			key == GDK_KEY_Up || key == GDK_KEY_KP_Up || \
			key == GDK_KEY_Down || key == GDK_KEY_KP_Down)
		{ /* nothing */ }
		else
			return FALSE;
	}

	switch (key)
	{
		case GDK_KEY_Left:
		case GDK_KEY_KP_Left:
			collection_move_cursor(collection, 0, -1, event->state);
			break;
		case GDK_KEY_Right:
		case GDK_KEY_KP_Right:
			collection_move_cursor(collection, 0, 1, event->state);
			break;
		case GDK_KEY_Up:
		case GDK_KEY_KP_Up:
			collection_move_cursor(collection, -1, 0, event->state);
			break;
		case GDK_KEY_Down:
		case GDK_KEY_KP_Down:
			collection_move_cursor(collection, 1, 0, event->state);
			break;
		case GDK_KEY_Home:
		case GDK_KEY_KP_Home:
			collection_set_cursor_item(collection, 0, TRUE);
			break;
		case GDK_KEY_End:
		case GDK_KEY_KP_End:
			collection_set_cursor_item(collection,
				MAX((gint) collection->number_of_items - 1, 0),
				TRUE);
			break;
		case GDK_KEY_Page_Up:
		case GDK_KEY_KP_Page_Up:
		  {
		        int first, last;
		       	get_visible_limits(collection, &first, &last);
			collection_move_cursor(collection, first - last, 0, 0);
			break;
		  }
		case GDK_KEY_Page_Down:
		case GDK_KEY_KP_Page_Down:
		  {
		        int first, last;
		       	get_visible_limits(collection, &first, &last);
			collection_move_cursor(collection, last - first, 0, 0);
			break;
		  }
		default:
			return FALSE;
	}

	return TRUE;
}

static int scrollbackorder = 0;
static int scrollfunc = 0;
static gboolean scroll_idle(gpointer data) {
	Collection *collection = COLLECTION(data);
	int diff = scrollbackorder;

	if (collection->vadj && diff)
	{
		int	old_value = gtk_adjustment_get_value(collection->vadj);
		int	new_value = 0;
		gboolean box = collection->lasso_box;

		int	step = gtk_adjustment_get_page_increment(collection->vadj) / o_scroll_speed.int_value;

		new_value = CLAMP(old_value + diff * step, 0.0,
				gtk_adjustment_get_upper(collection->vadj)
				- gtk_adjustment_get_page_size(collection->vadj));
		diff = new_value - old_value;
		if (diff)
		{
			if (box)
			{
				remove_lasso_box(collection);
				collection->drag_box_y[1] += diff;
			}
			gtk_adjustment_set_value(collection->vadj, new_value);
			if (box)
				add_lasso_box(collection);
		}
/*
		else
		{
			FilerWindow *fw =
				g_object_get_data(
						G_OBJECT(gtk_widget_get_toplevel(GTK_WIDGET(collection))),
						"filer_window");

			if (fw->left_link)
				filer_dir_link_next(fw->left_link,
					scrollbackorder < 0 ? GDK_SCROLL_UP : GDK_SCROLL_DOWN, TRUE);
		}
*/
	}


	g_object_unref(collection);
	scrollbackorder = 0;
	g_source_remove(scrollfunc);
	scrollfunc = 0;
	return FALSE;
}

/* Wheel mouse scrolling */
static gint collection_scroll_event(GtkWidget *widget, GdkEventScroll *event)
{
	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	if (!event->send_event && event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
	{
		FilerWindow *fw =
			g_object_get_data(
					G_OBJECT(gtk_widget_get_toplevel(widget)),
					"filer_window");

		if (event->state & GDK_CONTROL_MASK)
			filer_dir_link_next(fw, event->direction, FALSE);
		else if (fw->right_link)//shift
			filer_send_event_to_view(fw->right_link, (GdkEvent *) event);

		return FALSE;
	}

	int diff = 0;

	if (event->direction == GDK_SCROLL_UP)
		diff = -1;
	else if (event->direction == GDK_SCROLL_DOWN)
		diff = 1;
	else
		return FALSE;

	scrollbackorder += diff;
	if (scrollfunc)
		return FALSE;

	g_object_ref(widget);
	scrollfunc = g_idle_add(scroll_idle, widget);

	return FALSE;
}

/* 'from' and 'to' are pixel positions. 'step' is the size of each item.
 * Returns the index of the first item covered, and the number of items.
 */
static void get_range(int from, int to, int step, gint *pos, gint *len)
{
	int	margin = MIN(step / 4, 40);

	if (from > to)
	{
		int tmp = to;
		to = from;
		from = tmp;
	}

	from = (from + margin) / step;	/* First item */
	to = (to + step - margin) / step;	/* Last item (inclusive) */

	*pos = MAX(from, 0);
	*len = to - *pos;
}

/* Fills in the area with a rectangle corresponding to the current
 * size of the lasso box (units of items, not pixels).
 *
 * The box will only span valid columns, but the total number
 * of items is not taken into account (rows or cols).
 */
static void find_lasso_area(Collection *collection, GdkRectangle *area)
{
	int	cols = collection->columns;
	int	dx = collection->drag_box_x[0] - collection->drag_box_x[1];
	int	dy = collection->drag_box_y[0] - collection->drag_box_y[1];

	if (ABS(dx) < 8 && ABS(dy) < 8)
	{
		/* Didn't move far enough - ignore */
		area->x = area->y = 0;
		area->width = 0;
		area->height = 0;
		return;
	}

	get_range(collection->drag_box_x[0], collection->drag_box_x[1],
		  collection->item_width, &area->x, &area->width);

	if (area->x >= cols)
		area->width = 0;
	else if (area->x + area->width > cols)
		area->width = cols - area->x;

	get_range(collection->drag_box_y[0], collection->drag_box_y[1],
		  collection->item_height, &area->y, &area->height);
}

static void collection_process_area(Collection	 *collection,
				    GdkRectangle *area,
				    GdkFunction   fn,
				    guint32	 time)
{
	int		x, y;
	int             rows = collection_get_rows(collection);
	int             cols = collection->columns;
	guint32		stacked_time;
	int		item;
	gboolean	changed = FALSE;
	guint		old_selected;

	g_return_if_fail(fn == GDK_SET || fn == GDK_INVERT);

	old_selected = collection->number_selected;

	stacked_time = current_event_time;
	current_event_time = time;

	collection->block_selection_changed++;

	for (y = area->y; y < area->y + area->height && y < rows; y++)
		for (x = area->x; x < area->x + area->width && x < cols; x++)
	{
		item = collection_rowcol_to_item(collection, y, x);
		if (item < collection->number_of_items)
		{
			if (fn == GDK_INVERT)
					collection_item_set_selected(
					    collection, item,
					    !collection-> items[item].selected,
					FALSE);
			else
					collection_item_set_selected(
						collection, item, TRUE, FALSE);

			changed = TRUE;
		}
	}

	if (collection->number_selected && !old_selected)
		g_signal_emit(collection,
				collection_signals[GAIN_SELECTION], 0,
				current_event_time);
	else if (!collection->number_selected && old_selected)
		g_signal_emit(collection,
				collection_signals[LOSE_SELECTION], 0,
				current_event_time);

	collection_unblock_selection_changed(collection,
					current_event_time, changed);
	current_event_time = stacked_time;
}

static gint collection_motion_notify(GtkWidget *widget,
				     GdkEventMotion *event)
{
	Collection    	*collection;
	gint		x, y;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = COLLECTION(widget);

	if (!collection->lasso_box)
		return FALSE;

	if (event->window != gtk_widget_get_window(widget))
		gui_get_pointer(gtk_widget_get_window(widget), &x, &y, NULL);
	else
	{
		x = event->x;
		y = event->y;
	}

	remove_lasso_box(collection);
	collection->drag_box_x[1] = x;
	collection->drag_box_y[1] = y;
	add_lasso_box(collection);
	return TRUE;
}

static void add_lasso_box(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->lasso_box == FALSE);

	collection->lasso_box = TRUE;
	draw_lasso_box(collection);
}

static void draw_lasso_box(Collection *collection)
{
	GtkWidget	*widget;
	int		x, y, width, height;

	widget = GTK_WIDGET(collection);

	x = MIN(collection->drag_box_x[0], collection->drag_box_x[1]);
	y = MIN(collection->drag_box_y[0], collection->drag_box_y[1]);
	width = abs(collection->drag_box_x[1] - collection->drag_box_x[0]);
	height = abs(collection->drag_box_y[1] - collection->drag_box_y[0]);

	if (width || height)
		gtk_widget_queue_draw_area(widget, x, y, width, height);
}

static void abort_lasso(Collection *collection)
{
	if (collection->lasso_box)
		remove_lasso_box(collection);
}

static void remove_lasso_box(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->lasso_box == TRUE);

	draw_lasso_box(collection);

	collection->lasso_box = FALSE;

	return;
}

/* Make sure that 'item' is fully visible (vertically), scrolling if not. */
static void scroll_to_show(Collection *collection, int item)
{
        int     first, last, row, col;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	collection_item_to_rowcol(collection, item, &row, &col);
	if (collection->center_wink)
	{
		GtkAllocation alloc;
		gtk_widget_get_allocation(GTK_WIDGET(collection), &alloc);
		gtk_adjustment_set_value(collection->vadj, MIN(
			row * collection->item_height - (gtk_adjustment_get_page_size(collection->vadj) - collection->item_height) * 3/7,
			alloc.height - gtk_adjustment_get_page_size(collection->vadj)
		));
		return;
	}

	get_visible_limits(collection, &first, &last);
	if (row <= first)
	{
		gtk_adjustment_set_value(collection->vadj,
				row * collection->item_height);
	}
	else if (row >= last)
	{
		GtkWidget	*widget = (GtkWidget *) collection;
		gint 		height;

		if (gtk_widget_get_realized(widget))
		{
			height = gtk_adjustment_get_page_size(collection->vadj);
			gtk_adjustment_set_value(collection->vadj,
				(row + 1) * collection->item_height - height);
		}
	}
}

/* Return the first and last rows which are [partly] visible. Does not
 * ensure that the rows actually exist (contain items).
 */
static void get_visible_limits(Collection *collection, int *first, int *last)
{
	GtkWidget	*widget = (GtkWidget *) collection;
	gint		scroll = 0, height;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(first != NULL && last != NULL);

	if (!gtk_widget_get_realized(widget))
	{
		*first = 0;
		*last = 0;
	}
	else
	{
		scroll = gtk_adjustment_get_value(collection->vadj);
		height = gtk_adjustment_get_page_size(collection->vadj);

		*first = MAX(scroll / collection->item_height, 0);
		*last = (scroll + height - 1) /collection->item_height;

		if (*last < *first)
			*last = *first;
	}
}

/* Cancel the current wink effect. */
static void cancel_wink(Collection *collection)
{
	gint	item;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->wink_item != -1);

	item = collection->wink_item;

	collection->wink_item = -1;
	g_source_remove(collection->wink_timeout);

	gtk_style_context_remove_class(
		gtk_widget_get_style_context(GTK_WIDGET(collection)),
		"wink-outline");

	collection_draw_item(collection, item, TRUE);
}

/* Invalidate the area around collection->wink_item for redraw */
static void invert_wink(Collection *collection)
{
	GdkRectangle area;
	gint	row, col;

	g_return_if_fail(collection->wink_item >= 0);

	collection_item_to_rowcol(collection, collection->wink_item,
				  &row, &col);
	collection_get_item_area(collection, row, col, &area);

	gtk_widget_queue_draw_area(GTK_WIDGET(collection),
				  area.x, area.y,
				  area.width, area.height);
}

static gboolean wink_timeout(Collection *collection)
{
	gint	item;

	g_return_val_if_fail(collection != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(collection), FALSE);
	g_return_val_if_fail(collection->wink_item != -1, FALSE);

	item = collection->wink_item;

	if (collection->winks_left-- > 0)
	{
		invert_wink(collection);
		return TRUE;
	}

	collection->wink_item = -1;

	gtk_style_context_remove_class(
		gtk_widget_get_style_context(GTK_WIDGET(collection)),
		"wink-outline");

	collection_draw_item(collection, item, TRUE);

	return FALSE;
}

/* Change the selected state of an item.
 * Send GAIN/LOSE signals if 'signal' is TRUE.
 * Send SELECTION_CHANGED unless blocked.
 * Updates number_selected and redraws the item.
 */
static void collection_item_set_selected(Collection *collection,
                                         gint item,
                                         gboolean selected,
					 gboolean signal)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= 0 && item < collection->number_of_items);

	if (collection->items[item].selected == selected)
		return;

	collection->items[item].selected = selected;
	collection_draw_item(collection, item, TRUE);

	if (selected)
	{
		collection->number_selected++;
		if (signal && collection->number_selected == 1)
			g_signal_emit(collection,
					collection_signals[GAIN_SELECTION], 0,
					current_event_time);
	}
	else
	{
		collection->number_selected--;
		if (signal && collection->number_selected == 0)
			g_signal_emit(collection,
					collection_signals[LOSE_SELECTION], 0,
					current_event_time);
	}

	EMIT_SELECTION_CHANGED(collection, current_event_time);
}

/* Functions for managing collections */

/* Remove all objects from the collection */
void collection_clear(Collection *collection)
{
	collection->reached_scale = .0;
	collection->item_width = collection->item_height = 1;
	collection_delete_if(collection, NULL, NULL);
	collection->center_wink = TRUE;
	collection->old_height = collection->old_pos = 0;
	if (collection->vadj)
		gtk_adjustment_set_value(collection->vadj, 0);
}

/* Inserts a new item at the end. The new item is unselected, and its
 * number is returned.
 */
gint collection_insert(Collection *collection, gpointer data, gpointer view)
{
	int	item;

	/* g_return_val_if_fail(IS_COLLECTION(collection), -1); (slow) */

	item = collection->number_of_items;

	if (item >= collection->array_size)
		resize_arrays(collection, item + (item >> 1));

	collection->items[item].data = data;
	collection->items[item].view_data = view;
	collection->items[item].selected = FALSE;

	collection->number_of_items++;

	return item;
}

void collection_unselect_item(Collection *collection, gint item)
{
	collection_item_set_selected(collection, item, FALSE, TRUE);
}

void collection_select_item(Collection *collection, gint item)
{
	collection_item_set_selected(collection, item, TRUE, TRUE);
}


/* Select all items in the collection */
void collection_select_all(Collection *collection)
{
	int		item = 0;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	if (collection->number_selected == collection->number_of_items)
		return;		/* Nothing to do */

	while (collection->number_selected < collection->number_of_items)
	{
		while (collection->items[item].selected)
			item++;

		collection->items[item].selected = TRUE;
		collection_draw_item(collection, item, TRUE);
		item++;

		collection->number_selected++;
	}

	g_signal_emit(collection, collection_signals[GAIN_SELECTION], 0,
			current_event_time);
	EMIT_SELECTION_CHANGED(collection, current_event_time);
}

/* Toggle all items in the collection */
void collection_invert_selection(Collection *collection)
{
	int		item;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	if (collection->number_selected == 0)
	{
		collection_select_all(collection);
		return;
	}
	else if (collection->number_of_items == collection->number_selected)
	{
		collection_clear_selection(collection);
		return;
	}

	for (item = 0; item < collection->number_of_items; item++)
		collection->items[item].selected =
			!collection->items[item].selected;

	collection->number_selected = collection->number_of_items -
				      collection->number_selected;

	/* Have to redraw everything... */
	gtk_widget_queue_draw(GTK_WIDGET(collection));

	EMIT_SELECTION_CHANGED(collection, current_event_time);
}

/* Unselect all items except number item, which is selected (-1 to unselect
 * everything).
 */
void collection_clear_except(Collection *collection, gint item)
{
	int		i = 0;
	int		end;		/* Selected items to end up with */

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= -1 && item < collection->number_of_items);

	if (item == -1)
		end = 0;
	else
	{
		collection_select_item(collection, item);
		end = 1;
	}

	if (collection->number_selected == 0)
		return;

	while (collection->number_selected > end)
	{
		while (i == item || !collection->items[i].selected)
			i++;

		collection->items[i].selected = FALSE;
		collection_draw_item(collection, i, TRUE);
		i++;

		collection->number_selected--;
	}

	if (end == 0)
		g_signal_emit(collection, collection_signals[LOSE_SELECTION], 0,
				current_event_time);
	EMIT_SELECTION_CHANGED(collection, current_event_time);
}

/* Unselect all items in the collection */
void collection_clear_selection(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	collection_clear_except(collection, -1);
}

/* Force a redraw of the specified item, if it is visible */
void collection_draw_item(Collection *collection, gint item, gboolean blank)
{
	GdkRectangle	area;
	GtkWidget	*widget;
	int		row, col;

	g_return_if_fail(collection != NULL);
	/* g_return_if_fail(IS_COLLECTION(collection)); (slow) */
	g_return_if_fail(item >= 0 &&
			(item == 0 || item < collection->number_of_items));

	widget = GTK_WIDGET(collection);
	if (!gtk_widget_get_realized(widget))
		return;

	collection_item_to_rowcol(collection, item, &row, &col);

	collection_get_item_area(collection, row, col, &area);

	gtk_widget_queue_draw_area(widget, area.x, area.y, area.width, area.height);
}

void collection_set_item_size(Collection *collection, int width, int height)
{
	GtkWidget	*widget;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(width > 4 && height > 4);

	if (collection->item_width == width &&
			collection->item_height == height)
		return;

	widget = GTK_WIDGET(collection);

	collection->item_width = width;
	collection->item_height = height;

	if (gtk_widget_get_realized(widget))
	{
		int window_width;

		window_width = gdk_window_get_width(gtk_widget_get_window(widget));
		collection->columns = MAX(window_width / collection->item_width,
					  1);
		if (collection->cursor_item != -1)
			scroll_to_show(collection, collection->cursor_item);
		gtk_widget_queue_draw(widget);
	}

	{
		GtkAllocation alloc;
		gtk_widget_get_allocation(GTK_WIDGET(collection), &alloc);
		collection->old_height = alloc.height;
	}
	collection->old_pos = gtk_adjustment_get_value(collection->vadj);
	gtk_widget_queue_resize(GTK_WIDGET(collection));
}

static int (*cmp_callback)(const void *a, const void *b) = NULL;
static int collection_cmp(const void *a, const void *b)
{
	return cmp_callback(((CollectionItem *) a)->data,
			    ((CollectionItem *) b)->data);
}
static int collection_rcmp(const void *a, const void *b)
{
	return -cmp_callback(((CollectionItem *) a)->data,
			     ((CollectionItem *) b)->data);
}

/* Cursor is positioned on item with the same data as before the sort.
 * Same for the wink item.
 */
void collection_qsort(Collection *collection,
		      int (*compar)(const void *, const void *),
		      GtkSortType order)
{
	int	cursor, wink, items, wink_on_map;
	gpointer cursor_data = NULL;
	gpointer wink_data = NULL;
	gpointer wink_on_map_data = NULL;
	CollectionItem *array;
	int	i;
	int	mul = order == GTK_SORT_ASCENDING ? 1 : -1;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(compar != NULL);
	g_return_if_fail(cmp_callback == NULL);

	/* Check to see if it needs sorting (saves redrawing) */
	if (collection->number_of_items < 2)
		return;

	array = collection->items;
	for (i = 1; i < collection->number_of_items; i++)
	{
		if (mul * compar(array[i - 1].data, array[i].data) > 0)
			break;
	}
	if (i == collection->number_of_items)
		return;		/* Already sorted */

	items = collection->number_of_items;

	wink_on_map = collection->wink_on_map;
	if (wink_on_map >= 0 && wink_on_map < items)
	{
		wink_on_map_data = collection->items[wink_on_map].data;
		collection->wink_on_map = -1;
	}

	wink = collection->wink_item;
	if (wink >= 0 && wink < items)
	{
		wink_data = collection->items[wink].data;
		collection->wink_item = -1;
		gtk_style_context_remove_class(
			gtk_widget_get_style_context(GTK_WIDGET(collection)),
			"wink-outline");
	}
	else
		wink = -1;

	cursor = collection->cursor_item;
	if (cursor >= 0 && cursor < items)
		cursor_data = collection->items[cursor].data;
	else
		cursor = -1;

	cmp_callback = compar;
	qsort(collection->items, items, sizeof(collection->items[0]),
			order == GTK_SORT_ASCENDING ? collection_cmp
						    : collection_rcmp);
	cmp_callback = NULL;

	if (cursor > -1 || wink > -1 || wink_on_map > -1)
	{
		int	item;

		for (item = 0; item < items; item++)
		{
			if (collection->items[item].data == cursor_data)
				collection_set_cursor_item(collection, item,
						TRUE);
			if (collection->items[item].data == wink_on_map_data)
				collection->wink_on_map = item;
			if (collection->items[item].data == wink_data)
			{
				collection->cursor_item_old = item;
				collection->wink_item = item;
				gtk_style_context_add_class(
					gtk_widget_get_style_context(
					(GTK_WIDGET(collection))),
					"wink-outline");
				scroll_to_show(collection, item);
			}
		}
	}

	gtk_widget_queue_draw(GTK_WIDGET(collection));
}

/* Find an item in a sorted collection.
 * Returns the item number, or -1 if not found.
 */
int collection_find_item(Collection *collection, gpointer data,
		         int (*compar)(const void *, const void *),
			 GtkSortType order)
{
	int	lower, upper;
	int	mul = order == GTK_SORT_ASCENDING ? 1 : -1;

	g_return_val_if_fail(collection != NULL, -1);
	g_return_val_if_fail(IS_COLLECTION(collection), -1);
	g_return_val_if_fail(compar != NULL, -1);

	/* If item is here, then: lower <= i < upper */
	lower = 0;
	upper = collection->number_of_items;

	while (lower < upper)
	{
		int	i, cmp;

		i = (lower + upper) >> 1;

		cmp = mul * compar(collection->items[i].data, data);
		if (cmp == 0)
			return i;

		if (cmp > 0)
			upper = i;
		else
			lower = i + 1;
	}

	return -1;
}

/* Return the number of the item under the point (x,y), or -1 for none.
 * This may call your test_point callback. The point is relative to the
 * collection's origin.
 */
int collection_get_item(Collection *collection, int x, int y)
{
	int		row, col;
	int		width;
	int		item;

	g_return_val_if_fail(collection != NULL, -1);

	col = x / collection->item_width;
	row = y / collection->item_height;

	if (col >= collection->columns)
		col = collection->columns - 1;

	if (col < 0 || row < 0)
		return -1;

	if (col == collection->columns - 1)
		width = collection->item_width << 1;
	else
		width = collection->item_width;

	item = collection_rowcol_to_item(collection, row, col);
	if (item >= collection->number_of_items)
		return -1;

	x -= col * collection->item_width;
	y -= row * collection->item_height;

	if (collection->test_point(collection, x, y,
	    &collection->items[item], width, collection->item_height,
	    collection->cb_user_data))
		return item;

	return -1;
}

/* Set the cursor/highlight over the given item. Passing -1
 * hides the cursor. As a special case, you may set the cursor item
 * to zero when there are no items.
 */
void collection_set_cursor_item(Collection *collection, gint item,
				gboolean may_scroll)
{
	int	old_item;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= -1 &&
		(item < collection->number_of_items || item == 0));

	old_item = collection->cursor_item;

	if (old_item == item)
		return;

	collection->cursor_item = item;

	if (old_item != -1)
		collection_draw_item(collection, old_item, TRUE);

	if (item != -1)
	{
		collection_draw_item(collection, item, TRUE);
		if (may_scroll)
			scroll_to_show(collection, item);
	}
	else if (old_item != -1)
		collection->cursor_item_old = old_item;
}

/* Briefly highlight an item to draw the user's attention to it.
 * -1 cancels the effect, as does deleting items, sorting the collection
 * or starting a new wink effect.
 * Otherwise, the effect will cancel itself after a short pause.
 * */
void collection_wink_item(Collection *collection, gint item)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= -1 && item < collection->number_of_items);

	if (collection->wink_item != -1)
		cancel_wink(collection);
	if (item == -1)
		return;

	if (!gtk_widget_get_mapped(GTK_WIDGET(collection)))
	{
		collection->wink_on_map = item;
		return;
	}

	collection->cursor_item_old = collection->wink_item = item;
	collection->winks_left = MAX_WINKS;

	gtk_style_context_add_class(
		gtk_widget_get_style_context(GTK_WIDGET(collection)),
		"wink-outline");

	collection->wink_timeout = g_timeout_add(70,
					   (GSourceFunc) wink_timeout,
					   collection);

	if (!collection->center_wink)
		scroll_to_show(collection, item);
}

/* Call test(item, data) on each item in the collection.
 * Remove all items for which it returns TRUE. test() should
 * free the data before returning TRUE. The collection is in an
 * inconsistant state during this call (ie, when test() is called).
 *
 * If test is NULL, remove all items.
 */
void collection_delete_if(Collection *collection,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	int	in, out = 0;
	int	selected = 0;
	int	cursor;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	cursor = collection->cursor_item;

	for (in = 0; in < collection->number_of_items; in++)
	{
		if (test && !test(collection->items[in].data, data))
		{
			/* Keep item */
			if (collection->items[in].selected)
			{
				collection->items[out].selected = TRUE;
				selected++;
			}
			else
				collection->items[out].selected = FALSE;

			collection->items[out].data =
				collection->items[in].data;
			collection->items[out].view_data =
				collection->items[in].view_data;
			out++;
		}
		else
		{
			/* Remove item */
			if (collection->free_item)
				collection->free_item(collection,
							&collection->items[in]);

			if (collection->cursor_item >= in)
				cursor--;
		}
	}

	if (in != out)
	{
		collection->cursor_item = cursor;
		collection->cursor_item_old = -1;

		if (collection->wink_item != -1)
		{
			collection->wink_item = -1;
			g_source_remove(collection->wink_timeout);
			gtk_style_context_remove_class(
				gtk_widget_get_style_context(
				(GTK_WIDGET(collection))),
				"wink-outline");
		}

		collection->number_of_items = out;
		if (collection->number_selected && !selected)
		{
			/* We've lost all the selected items */
			g_signal_emit(collection,
					collection_signals[LOSE_SELECTION], 0,
					current_event_time);
		}

		collection->number_selected = selected;
		resize_arrays(collection,
			MAX(collection->number_of_items, MINIMUM_ITEMS));

		if (gtk_widget_get_realized(GTK_WIDGET(collection)))
			gtk_widget_queue_draw(GTK_WIDGET(collection));

		gtk_widget_queue_resize(GTK_WIDGET(collection));
	}
}

/* Move the cursor by the given row and column offsets.
 * Moving by (0,0) can be used to simply make the cursor appear.
 */
void collection_move_cursor(Collection *collection, int drow, int dcol, int event_state)
{
	int	row, col, item;
	int	first, last, total_rows, total_cols;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	if (!collection->number_of_items)
	{
		/* Show the cursor, even though there are no items */
		collection_set_cursor_item(collection, 0, TRUE);
		return;
	}

	get_visible_limits(collection, &first, &last);
	total_rows = collection_get_rows(collection);
	total_cols = collection_get_cols(collection);
	item = collection->cursor_item;

	if (item == -1)
		item = MIN(collection->cursor_item_old,
			collection->number_of_items - 1);

	if (item == -1)
	{
		if (drow >= 0 && dcol >= 0)
		{
			row = first;
			col = 0;
		}
		else
		{
			row = last;
			col = total_cols - 1;
		}
	}
	else
	{
		collection_item_to_rowcol(collection, item, &row, &col);

		if (event_state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
		{
			collection_item_set_selected(collection, item,
					!(collection->items[item].selected &&
						(event_state & GDK_CONTROL_MASK)),
					TRUE);
		}

		col += dcol;
		if (collection->vertical_order)
		{
			col = MAX(0,col);
			col = MIN(col, total_cols - 1);
		}

		if (row < first)
			row = first;
		else if (row > last)
			row = last;
		else {
			if (collection->vertical_order)
			{
				if (drow > 0 && row == total_rows - 1)
				{
					if (col != total_cols - 1)
					{
						row = 0;
						col += 1;
					}
				}
				else if (drow < 0 && row == 0)
				{
					if (col != 0)
					{
						row = total_rows - 1;
						col -= 1;
					}
				}
				else
					row += drow;

				row = CLAMP(row, 0, total_rows - 1);
			}
			else
			{
				row += drow;
				row = MAX(row, 0);
				row = MIN(row, total_rows - 1);
			}
		}
	}

	item = collection_rowcol_to_item(collection, row, col);

	item = MAX(item, 0);
	item = MIN(item, collection->number_of_items-1);

	if (event_state & GDK_SHIFT_MASK)
		collection_item_set_selected(collection, item,
				TRUE,
				TRUE);

	collection_set_cursor_item(collection, item, TRUE);
}

/* Start a lasso box drag */
void collection_lasso_box(Collection *collection, int x, int y)
{
	collection->drag_box_x[0] = x;
	collection->drag_box_y[0] = y;
	collection->drag_box_x[1] = x;
	collection->drag_box_y[1] = y;

	add_lasso_box(collection);
}

/* Remove the lasso box. Applies fn to each item inside the box.
 * fn may be GDK_INVERT, GDK_SET, GDK_NOOP or GDK_CLEAR.
 */
void collection_end_lasso(Collection *collection, GdkFunction fn)
{
	if (fn != GDK_CLEAR)
	{
		GdkRectangle	region;

		find_lasso_area(collection, &region);

		collection_process_area(collection, &region, fn,
				GDK_CURRENT_TIME);
	}

	abort_lasso(collection);
}

/* Unblock the selection_changed signal, emitting the signal if the
 * block counter reaches zero and emit is TRUE.
 */
void collection_unblock_selection_changed(Collection	*collection,
					  guint		time,
					  gboolean	emit)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->block_selection_changed > 0);

	collection->block_selection_changed--;

	if (emit && !collection->block_selection_changed)
		g_signal_emit(collection,
				collection_signals[SELECTION_CHANGED], 0, time);
}

/* Translate the item number to the (row, column) form */
void    collection_item_to_rowcol       (const Collection *collection,
					 int item, int *row, int *col)
{
	if (!collection->vertical_order)
	{
		*row = item / collection->columns;
		*col = item % collection->columns;
	}
	else
	{
		int rows = collection_get_rows(collection);
		*row = item % rows;
		*col = item / rows;
	}
}



/* Translate the (row, column) form to the item number.
 * May return a number >= collection->number_of_items.
 */
int collection_rowcol_to_item(const Collection *collection, int row, int col)
{
	if (!collection->vertical_order)
		return row * collection->columns + col;
	else
	{
		int rows = collection_get_rows(collection);
		if (row >= rows)
			return collection->number_of_items;
		return row + col * rows;
	}
}
