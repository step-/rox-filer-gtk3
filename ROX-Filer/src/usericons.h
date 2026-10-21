/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * Diego Zamboni, Feb 7, 2001
 */

#ifndef _USERICONS_H_
#define _USERICONS_H_

/* Public interface */
void read_globicons(void);
void check_globicon(const gchar *path, DirItem *item);
MaskedPixmap *get_globicon(const gchar *path);
gchar *get_globicon_path(const gchar *path);
void icon_set_handler_dialog(DirItem *item, const gchar *path);
void add_globicon(const gchar *path, const gchar *icon);
void delete_globicon(const gchar *path);

#endif
