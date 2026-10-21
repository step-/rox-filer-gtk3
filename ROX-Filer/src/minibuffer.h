/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MINIBUFFER_H
#define _MINIBUFFER_H

#include <gtk/gtk.h>

#include "options.h"

/* [20260911061827] Bare digit starts Reg Select and Alt+digit goes to
 * numbered group directory while TRUE; default FALSE keeps classic bindings.
 */
extern Option o_group_digits_modifier;

void minibuffer_init(void);
void create_minibuffer(FilerWindow *filer_window);
void minibuffer_show(FilerWindow *filer_window, MiniType mini_type, guint keyval);
void minibuffer_hide(FilerWindow *filer_window);
void minibuffer_add(FilerWindow *filer_window, const gchar *leafname);

#endif /* _MINIBUFFER_H */
