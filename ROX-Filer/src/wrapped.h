/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef __WRAPPED_H__
#define __WRAPPED_H__

#include <gtk/gtk.h>

typedef struct _WrappedLabelClass WrappedLabelClass;

struct _WrappedLabelClass {
	GtkWidgetClass parent;
};

struct _WrappedLabel {
	GtkWidget widget;
	PangoLayout *layout;
	GdkRGBA fg_col;
	GdkRGBA bg_col;
	gint width, text_width;
	int x_off, y_off;
};

#define TYPE_WRAPPED_LABEL wrapped_label_get_type()
#define WRAPPED_LABEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_WRAPPED_LABEL, WrappedLabel))

GType wrapped_label_get_type(void);
GtkWidget *wrapped_label_new(const char *text, gint width);
void wrapped_label_set_text(WrappedLabel *wl, const char *text);
void wrapped_label_set_fg_color(WrappedLabel *wl, const GdkRGBA *color);

#endif /* __WRAPPED_H__ */
