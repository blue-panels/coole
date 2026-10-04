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

/* A value of a GDB/MI record: a string, a tuple {a=1,b=2} or a list [1,2] or [a=1,a=2].  Each
   value in a tuple, and in a list of results, has its name. */
typedef enum
{
    GDB_MI_STRING,
    GDB_MI_TUPLE,
    GDB_MI_LIST
} gdb_mi_value_kind_t;

typedef struct gdb_mi_value_t
{
    gdb_mi_value_kind_t kind;
    char *name;       /* NULL in a list of values */
    char *string;     /* GDB_MI_STRING, unquoted */
    GPtrArray *items; /* gdb_mi_value_t of a tuple or a list */
} gdb_mi_value_t;

typedef enum
{
    GDB_MI_RECORD_RESULT,  /* ^done, ^running, ^error, ^exit */
    GDB_MI_RECORD_EXEC,    /* *stopped, *running */
    GDB_MI_RECORD_STATUS,  /* + */
    GDB_MI_RECORD_NOTIFY,  /* =breakpoint-modified, =thread-group-exited */
    GDB_MI_RECORD_CONSOLE, /* ~ what GDB says */
    GDB_MI_RECORD_TARGET,  /* @ what the program says */
    GDB_MI_RECORD_LOG,     /* & GDB's log */
    GDB_MI_RECORD_PROMPT,  /* (gdb) */
    GDB_MI_RECORD_OTHER    /* a line that is no record, stderr of GDB for one: in text */
} gdb_mi_record_kind_t;

typedef struct
{
    gdb_mi_record_kind_t kind;
    gboolean has_token;
    unsigned long token;
    char *klass;             /* done, error, stopped... */
    gdb_mi_value_t *results; /* a tuple, empty when the record has none */
    char *text;              /* of a stream record, unquoted, and of GDB_MI_RECORD_OTHER */
} gdb_mi_record_t;

/* Parse one line of GDB/MI output; never NULL.  Free with gdb_mi_record_free(). */
gdb_mi_record_t *gdb_mi_parse (const char *line);
void gdb_mi_record_free (gdb_mi_record_t *record);

/* The value of that name in a tuple, NULL when there is none or @tuple is NULL */
const gdb_mi_value_t *gdb_mi_get (const gdb_mi_value_t *tuple, const char *name);
/* The string of that name in a tuple, NULL when there is none or it is no string */
const char *gdb_mi_get_string (const gdb_mi_value_t *tuple, const char *name);
/* The string of that name in the results of a record */
const char *gdb_mi_record_string (const gdb_mi_record_t *record, const char *name);

#endif
