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

/* wrapped.c - a replacement for GtkLabel that wraps sensibly */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"

#include "wrapped.h"

static gpointer parent_class = NULL;

/* Static prototypes */
static void wrapped_label_finialize(GObject *object);
static void wrapped_label_class_init(gpointer gclass, gpointer data);
static void wrapped_label_init(GTypeInstance *object, gpointer gclass);
static void wrapped_label_get_preferred_width(GtkWidget *widget,
					       gint *minimum_width,
					       gint *natural_width);
static void wrapped_label_get_preferred_height(GtkWidget *widget,
						gint *minimum_height,
						gint *natural_height);
static gboolean wrapped_label_draw(GtkWidget *widget, cairo_t *cr);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *wrapped_label_new(const char *text, gint width)
{
	WrappedLabel *wl;

	wl = g_object_new(wrapped_label_get_type(), NULL);
	wl->width = width;

	wrapped_label_set_text(wl, text);

	return GTK_WIDGET(wl);
}

void wrapped_label_set_text(WrappedLabel *wl, const char *text)
{
	GtkWidget *widget = GTK_WIDGET(wl);

	if (!wl->layout)
	{
		wl->layout = gtk_widget_create_pango_layout(widget, text);
		pango_layout_set_width(wl->layout, wl->width * PANGO_SCALE);
		pango_layout_set_wrap(wl->layout, PANGO_WRAP_WORD);
		pango_layout_set_alignment(wl->layout, PANGO_ALIGN_CENTER);
	}
	else
		pango_layout_set_text(wl->layout, text, -1);

	gtk_widget_queue_resize(widget);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void wrapped_label_finialize(GObject *object)
{
	WrappedLabel *wl = (WrappedLabel *) object;

	if (wl->layout)
	{
		g_object_unref(G_OBJECT(wl->layout));
		wl->layout = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void wrapped_label_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = wrapped_label_finialize;

	widget_class->get_preferred_width = wrapped_label_get_preferred_width;
	widget_class->get_preferred_height = wrapped_label_get_preferred_height;
	widget_class->draw = wrapped_label_draw;
}

static void wrapped_label_init(GTypeInstance *object, gpointer gclass)
{
	WrappedLabel *wl = (WrappedLabel *) object;
	GtkWidget *widget = (GtkWidget *) object;

	gtk_widget_set_has_window(widget, FALSE);

	wl->layout = NULL;
	wl->width = -1;
	wl->text_width = -1;
	/* Sensible defaults so the label is visible before the caller
	 * sets explicit colors.  Black text on a transparent background. */
	wl->fg_col.red = 0.0;
	wl->fg_col.green = 0.0;
	wl->fg_col.blue = 0.0;
	wl->fg_col.alpha = 1.0;
	wl->bg_col.red = 0.0;
	wl->bg_col.green = 0.0;
	wl->bg_col.blue = 0.0;
	wl->bg_col.alpha = 0.0;
}

GType wrapped_label_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (WrappedLabelClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			wrapped_label_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(WrappedLabel),
			0,			/* n_preallocs */
			wrapped_label_init
		};

		type = g_type_register_static(GTK_TYPE_WIDGET, "WrappedLabel",
					      &info, 0);
	}

	return type;
}

static void wrapped_label_get_preferred_width(GtkWidget *widget,
					       gint *minimum_width,
					       gint *natural_width)
{
	PangoRectangle logical_rect;
	WrappedLabel *wl = (WrappedLabel *) widget;

	g_return_if_fail(wl->layout != NULL);

	pango_layout_get_extents(wl->layout, NULL, &logical_rect);

	wl->x_off = PANGO_PIXELS(logical_rect.x);
	wl->text_width = PANGO_PIXELS(logical_rect.width);

	*minimum_width = PANGO_PIXELS(logical_rect.width);
	*natural_width = PANGO_PIXELS(logical_rect.width);
}

static void wrapped_label_get_preferred_height(GtkWidget *widget,
						gint *minimum_height,
						gint *natural_height)
{
	PangoRectangle logical_rect;
	WrappedLabel *wl = (WrappedLabel *) widget;

	g_return_if_fail(wl->layout != NULL);

	pango_layout_get_extents(wl->layout, NULL, &logical_rect);

	wl->y_off = PANGO_PIXELS(logical_rect.y);

	*minimum_height = PANGO_PIXELS(logical_rect.height);
	*natural_height = PANGO_PIXELS(logical_rect.height);
}

static gboolean wrapped_label_draw(GtkWidget *widget, cairo_t *cr)
{
	WrappedLabel *wl = (WrappedLabel *) widget;
	int x;
	GtkAllocation alloc;

	g_return_val_if_fail(wl->layout != NULL, FALSE);

	gtk_widget_get_allocation(widget, &alloc);

	/* Paint the background with the stored color. */
	gdk_cairo_set_source_rgba(cr, &wl->bg_col);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_fill(cr);

	/* Set the cairo source to the foreground color before rendering. */
	gdk_cairo_set_source_rgba(cr, &wl->fg_col);

	/* Calculate X relative to the widget's local 0,0 origin. */
	x = - ((wl->text_width - alloc.width) >> 1);

	/* Move pen using only local offsets. */
	cairo_move_to(cr, x - wl->x_off, - wl->y_off);
	pango_cairo_show_layout(cr, wl->layout);

	return FALSE;
}

/*
 * Set the foreground color used by WrappedLabel.  Installs a Pango
 * foreground attribute on the underlying PangoLayout.  Must be called
 * from the color-option handler (not from the draw handler) to avoid
 * an infinite re-layout loop.
 */
void wrapped_label_set_fg_color(WrappedLabel *wl, const GdkRGBA *color)
{
	PangoAttrList *attrs;
	PangoAttribute *attr;

	g_return_if_fail(wl != NULL);
	g_return_if_fail(wl->layout != NULL);
	g_return_if_fail(color != NULL);

	wl->fg_col = *color;

	attrs = pango_attr_list_new();
	attr = pango_attr_foreground_new(
		(guint16)(color->red * 65535),
		(guint16)(color->green * 65535),
		(guint16)(color->blue * 65535));
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(attrs, attr);
	pango_layout_set_attributes(wl->layout, attrs);
	pango_attr_list_unref(attrs);
}
