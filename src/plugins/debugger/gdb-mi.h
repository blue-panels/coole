#ifndef MC__GDB_MI_H
#define MC__GDB_MI_H

#include <glib.h>

typedef struct gdb_mi_session_t gdb_mi_session_t;
typedef void (*gdb_mi_record_fn) (const char *record, void *data);

gdb_mi_session_t *gdb_mi_session_new (gdb_mi_record_fn callback, void *data);
gboolean gdb_mi_session_start (gdb_mi_session_t *session, const char *gdb_path, GError **error);
gboolean gdb_mi_session_send (gdb_mi_session_t *session, const char *command);
gboolean gdb_mi_session_alive (const gdb_mi_session_t *session);
void gdb_mi_session_stop (gdb_mi_session_t *session);
void gdb_mi_session_free (gdb_mi_session_t *session);

/* Quote a single GDB/MI argument; caller frees the result. */
char *gdb_mi_quote (const char *value);
/* Extract a quoted string field from a record; caller frees the result. */
char *gdb_mi_field (const char *record, const char *name);

#endif
