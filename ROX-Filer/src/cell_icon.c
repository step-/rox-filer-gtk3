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

/* cell_icon.c - a GtkCellRenderer used for the icons in details mode
 *
 * Based on gtkcellrendererpixbuf.c.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "global.h"

#include "view_details.h"
#include "cell_icon.h"
#include "filer.h"
#include "display.h"
#include "view_utils.h"
#include "diritem.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "fscache.h"

typedef struct _CellIcon CellIcon;
typedef struct _CellIconClass CellIconClass;

struct _CellIcon {
	GtkCellRenderer parent;

	ViewDetails *view_details;
	ViewItem *item;
	GdkRGBA background;
};

struct _CellIconClass {
	GtkCellRendererClass parent_class;
};


/* Static prototypes */
static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec);
static void cell_icon_init       (CellIcon *cell);
static void cell_icon_class_init (CellIconClass *class);
static void cell_icon_get_size   (GtkCellRenderer	*cell,
				      GtkWidget		*widget,
				      const GdkRectangle	*rectangle,
				      gint		*x_offset,
				      gint		*y_offset,
				      gint		*width,
				      gint		*height);
static void cell_icon_render     (GtkCellRenderer		*cell,
				      cairo_t		*cr,
				      GtkWidget		*widget,
				      const GdkRectangle	*background_area,
				      const GdkRectangle	*cell_area,
				      GtkCellRendererState	flags);
static GType cell_icon_get_type(void);

enum {
	PROP_ZERO,
	PROP_ITEM,
	PROP_BACKGROUND_GDK,
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkCellRenderer *cell_icon_new(ViewDetails *view_details)
{
	GtkCellRenderer *cell;

	cell = GTK_CELL_RENDERER(g_object_new(cell_icon_get_type(), NULL));
	((CellIcon *) cell)->view_details = view_details;

	return cell;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


static GType cell_icon_get_type(void)
{
	static GType cell_icon_type = 0;

	if (!cell_icon_type)
	{
		static const GTypeInfo cell_icon_info =
		{
			sizeof (CellIconClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) cell_icon_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (CellIcon),
			0,              /* n_preallocs */
			(GInstanceInitFunc) cell_icon_init,
		};

		cell_icon_type = g_type_register_static(GTK_TYPE_CELL_RENDERER,
							"CellIcon",
							&cell_icon_info, 0);
	}

	return cell_icon_type;
}

static void cell_icon_init(CellIcon *icon)
{
	icon->view_details = NULL;
}

static void cell_icon_class_init(CellIconClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS(class);

	object_class->set_property = cell_icon_set_property;

	cell_class->get_size = cell_icon_get_size;
	cell_class->render = cell_icon_render;

	g_object_class_install_property(object_class,
			PROP_ITEM,
			g_param_spec_pointer("item",
				"DirItem",
				"The item to render.",
				G_PARAM_WRITABLE));

	g_object_class_install_property(object_class,
			PROP_BACKGROUND_GDK,
			g_param_spec_boxed("background_gdk",
				"Background color",
				"Background color as a GdkRGBA",
				GDK_TYPE_RGBA,
				G_PARAM_WRITABLE));
}

static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec)
{
	CellIcon *icon = (CellIcon *) object;

	switch (param_id)
	{
		case PROP_ITEM:
			icon->item = (ViewItem *) g_value_get_pointer(value);
			break;
		case PROP_BACKGROUND_GDK:
		{
			GdkRGBA *bg = g_value_get_boxed(value);
			if (bg)
				icon->background = *bg;
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,
							  param_id, pspec);
			break;
	}
}

/* Return the size to display this icon at. If huge, ensures that the image
 * exists in that size, if present at all.
 */
static DisplayStyle get_style(GtkCellRenderer *cell)
{
	CellIcon *icon = (CellIcon *) cell;
	ViewItem *view_item = icon->item;
	DisplayStyle size;
	DirItem *item = view_item->item;

	if (!view_item->image)
	{
		FilerWindow *filer_window = icon->view_details->filer_window;

		if (filer_window->show_thumbs && item->base_type == TYPE_FILE)
		{
			if (!view_item->thumb) {
				gchar *path = pathdup(
						make_path(filer_window->real_path, item->leafname));
				view_item->thumb = pixmap_load_thumb(path);
				g_free(path);
			}
		}
		else
			g_clear_object(&(view_item->thumb));


		if (!view_item->thumb && !view_item->image)
		{
			view_item->image = di_image(item);
			if (view_item->image)
				g_object_ref(view_item->image);
		}
	}

	size = icon->view_details->filer_window->display_style_wanted;

	if (size == AUTO_SIZE_ICONS)
	{
		size = SMALL_ICONS;
	}

	return size;
}

static void cell_icon_get_size(GtkCellRenderer *cell,
				   GtkWidget       *widget,
				   const GdkRectangle *cell_area,
				   gint            *x_offset,
				   gint            *y_offset,
				   gint            *width,
				   gint            *height)
{
	MaskedPixmap *image;
	DisplayStyle size;
	int w, h;

	ViewItem *view_item = ((CellIcon *) cell)->item;

	FilerWindow *fw = ((CellIcon *) cell)->view_details->filer_window;
	gfloat scale = fw->icon_scale;

	size = get_style(cell);
	image = view_item->image;

	if (x_offset)
		*x_offset = 0;
	if (y_offset)
		*y_offset = 0;

	switch (size)
	{
		case SMALL_ICONS:
			w = small_width;
			h = small_height;
			break;
		case LARGE_ICONS:
			if (image)
			{
				w = image->width;
				h = image->height;
			}
			else
			{
				w = ICON_WIDTH;
				h = ICON_HEIGHT;
			}
			break;
		case HUGE_ICONS:
			if (view_item->thumb)
			{
				w = gdk_pixbuf_get_width(view_item->thumb);
				h = gdk_pixbuf_get_height(view_item->thumb);
			}
			else if (image)
			{
				w = image->huge_width;
				h = image->huge_height;
			}
			else
				w = h = huge_size;


			if (!view_item->thumb &&
					w <= ICON_WIDTH &&
					h <= ICON_HEIGHT)
				scale = 1.0;
			else
				scale *= (gfloat) huge_size / MAX(w, h);

			w *= scale;
			h *= scale;
			break;
		default:
			w = 2;
			h = 2;
			break;
	}

	if (width)
		*width = w;
	if (height)
		*height = h;
}

static void cell_icon_render(GtkCellRenderer    *cell,
			     cairo_t		*cr,
			     GtkWidget		*widget,
			     const GdkRectangle	*background_area,
			     const GdkRectangle	*cell_area,
			     GtkCellRendererState	flags)
{
	CellIcon *icon = (CellIcon *) cell;
	ViewItem *view_item = icon->item;
	DirItem *item = view_item->item;
	DisplayStyle size;
	gboolean selected = (flags & GTK_CELL_RENDERER_SELECTED) != 0;
	FilerWindow  *fw = icon->view_details->filer_window;
	GtkStyleContext *ctx;
	GdkRGBA bg_color;

	g_return_if_fail(view_item != NULL);

	size = get_style(cell);
	MaskedPixmap *image = view_item->image;
	GdkPixbuf *sendi = view_item->thumb;

	if (!image && !sendi)
		return;

	ctx = gtk_widget_get_style_context(widget);
	{
		GtkStateFlags sf = (fw->selection_state == GTK_STATE_SELECTED)
				? GTK_STATE_FLAG_SELECTED : GTK_STATE_FLAG_ACTIVE;
		view_utils_get_selection_color(ctx, sf, &bg_color);
	}

	/* Draw the icon */
	GdkRectangle area = *cell_area;

	if (size == SMALL_ICONS)
	{
		area.width = MIN(area.width, small_width);
		area.x = cell_area->x + cell_area->width - area.width;

		area.height = MIN(area.height, small_height);
		area.y = cell_area->y + (cell_area->height - area.height) / 2;
	}

	int scale_factor = gtk_widget_get_scale_factor(widget);

	if (!sendi)
	{
		switch (size) {
		case SMALL_ICONS:
		{
			if (!image->sm_pixbuf)
				pixmap_make_small(image);

			sendi =image->sm_pixbuf;
			break;
		}
		case LARGE_ICONS:
			/* On HiDPI prefer the source-resolution pixbuf so
			 * draw_huge_icon can render at physical pixels.
			 */
			sendi = scale_factor > 1
					? image->src_pixbuf
					: image->pixbuf;
			break;
		case HUGE_ICONS:
			sendi =image->src_pixbuf;
			break;
		default:
			g_warning("Unknown size %d\n", size);
			break;
		}
	}

	draw_huge_icon(cr, &area, item,
			sendi, selected, &bg_color,
			scale_factor);
}
