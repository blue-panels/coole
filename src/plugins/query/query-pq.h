/** \file query-pq.h
 *  \brief Header: libpq, the client library of PostgreSQL, loaded when -Q needs it
 *
 *  coole does not link libpq: a package of coole does not need it, and the queries of MongoDB
 *  run without it.  The types are opaque, the values of the enumerations are those of
 *  libpq-fe.h, which only adds to them; the functions are found in libpq.so.5 at run time.
 */

#ifndef MC__QUERY_PQ_H
#define MC__QUERY_PQ_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;
typedef struct pg_cancel PGcancel;
typedef struct pg_cancel_conn PGcancelConn;
typedef unsigned int Oid;

typedef void (*PQnoticeReceiver) (void *arg, const PGresult *res);

/* An option of a connection and its value, of PQconndefaults () */
typedef struct
{
    char *keyword;
    char *envvar;
    char *compiled;
    char *val;  // NULL when it has none
    char *label;
    char *dispchar;
    int dispsize;
} PQconninfoOption;

/* The fields of an error, PG_DIAG_* of postgres_ext.h */
#define QUERY_PG_DIAG_SEVERITY           'S'
#define QUERY_PG_DIAG_SQLSTATE           'C'
#define QUERY_PG_DIAG_MESSAGE_PRIMARY    'M'
#define QUERY_PG_DIAG_MESSAGE_DETAIL     'D'
#define QUERY_PG_DIAG_MESSAGE_HINT       'H'
#define QUERY_PG_DIAG_STATEMENT_POSITION 'P'
#define QUERY_PG_DIAG_CONTEXT            'W'

/*** enums ***************************************************************************************/

/* ConnStatusType: the first two of them */
enum
{
    QUERY_PG_CONNECTION_OK = 0,
    QUERY_PG_CONNECTION_BAD = 1
};

/* PostgresPollingStatusType */
enum
{
    QUERY_PG_POLLING_FAILED = 0,
    QUERY_PG_POLLING_READING = 1,
    QUERY_PG_POLLING_WRITING = 2,
    QUERY_PG_POLLING_OK = 3
};

/* ExecStatusType */
enum
{
    QUERY_PG_EMPTY_QUERY = 0,
    QUERY_PG_COMMAND_OK = 1,
    QUERY_PG_TUPLES_OK = 2,
    QUERY_PG_COPY_OUT = 3,
    QUERY_PG_COPY_IN = 4,
    QUERY_PG_BAD_RESPONSE = 5,
    QUERY_PG_NONFATAL_ERROR = 6,
    QUERY_PG_FATAL_ERROR = 7,
    QUERY_PG_COPY_BOTH = 8,
    QUERY_PG_SINGLE_TUPLE = 9,
    QUERY_PG_PIPELINE_SYNC = 10,
    QUERY_PG_PIPELINE_ABORTED = 11,
    QUERY_PG_TUPLES_CHUNK = 12  // libpq 17
};

/* PGTransactionStatusType */
enum
{
    QUERY_PG_TRANS_IDLE = 0,
    QUERY_PG_TRANS_ACTIVE = 1,
    QUERY_PG_TRANS_INTRANS = 2,
    QUERY_PG_TRANS_INERROR = 3,
    QUERY_PG_TRANS_UNKNOWN = 4
};

/*** structures declarations (and typedefs of structures)*****************************************/

/* The functions of libpq; those of libpq 17 NULL with an older one */
typedef struct
{
    PGconn *(*PQconnectStartParams) (const char *const *keywords, const char *const *values,
                                     int expand_dbname);
    int (*PQconnectPoll) (PGconn *conn);
    int (*PQstatus) (const PGconn *conn);
    int (*PQsocket) (const PGconn *conn);
    char *(*PQerrorMessage) (const PGconn *conn);
    void (*PQfinish) (PGconn *conn);
    int (*PQsetnonblocking) (PGconn *conn, int arg);
    PQnoticeReceiver (*PQsetNoticeReceiver) (PGconn *conn, PQnoticeReceiver proc, void *arg);
    int (*PQclientEncoding) (const PGconn *conn);
    const char *(*pg_encoding_to_char) (int encoding);
    char *(*PQhost) (const PGconn *conn);
    char *(*PQport) (const PGconn *conn);
    char *(*PQuser) (const PGconn *conn);
    char *(*PQdb) (const PGconn *conn);
    int (*PQserverVersion) (const PGconn *conn);
    int (*PQlibVersion) (void);
    int (*PQtransactionStatus) (const PGconn *conn);
    PQconninfoOption *(*PQconndefaults) (void);
    void (*PQconninfoFree) (PQconninfoOption *options);

    int (*PQsendQuery) (PGconn *conn, const char *query);
    int (*PQflush) (PGconn *conn);
    int (*PQconsumeInput) (PGconn *conn);
    int (*PQisBusy) (PGconn *conn);
    PGresult *(*PQgetResult) (PGconn *conn);
    int (*PQsetSingleRowMode) (PGconn *conn);
    int (*PQsetChunkedRowsMode) (PGconn *conn, int chunk_size);  // libpq 17
    int (*PQputCopyEnd) (PGconn *conn, const char *errormsg);
    int (*PQgetCopyData) (PGconn *conn, char **buffer, int async);
    void (*PQfreemem) (void *ptr);

    int (*PQresultStatus) (const PGresult *res);
    char *(*PQresultErrorField) (const PGresult *res, int fieldcode);
    char *(*PQresultErrorMessage) (const PGresult *res);
    char *(*PQcmdStatus) (PGresult *res);
    int (*PQntuples) (const PGresult *res);
    int (*PQnfields) (const PGresult *res);
    char *(*PQfname) (const PGresult *res, int field_num);
    Oid (*PQftype) (const PGresult *res, int field_num);
    char *(*PQgetvalue) (const PGresult *res, int tup_num, int field_num);
    int (*PQgetisnull) (const PGresult *res, int tup_num, int field_num);
    int (*PQgetlength) (const PGresult *res, int tup_num, int field_num);
    void (*PQclear) (PGresult *res);

    PGcancel *(*PQgetCancel) (PGconn *conn);
    void (*PQfreeCancel) (PGcancel *cancel);
    int (*PQcancel) (PGcancel *cancel, char *errbuf, int errbufsize);
    // libpq 17: a cancel that does not block
    PGcancelConn *(*PQcancelCreate) (PGconn *conn);
    int (*PQcancelStart) (PGcancelConn *cancel_conn);
    int (*PQcancelPoll) (PGcancelConn *cancel_conn);
    int (*PQcancelStatus) (const PGcancelConn *cancel_conn);
    int (*PQcancelSocket) (const PGcancelConn *cancel_conn);
    char *(*PQcancelErrorMessage) (const PGcancelConn *cancel_conn);
    void (*PQcancelFinish) (PGcancelConn *cancel_conn);
} query_pq_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* libpq, loaded the first time; NULL with @error when it is not there or lacks a function */
const query_pq_t *query_pq_load (char **error);
/* Whether the libpq loaded has what libpq 17 has: rows by chunks, a cancel that does not block */
gboolean query_pq_chunks (const query_pq_t *pq);
gboolean query_pq_async_cancel (const query_pq_t *pq);
/* A connection started as coole makes them, of what the environment tells libpq: UTF8, the name
   coole, with @read_only transactions that write nothing; NULL with no memory.  The name of the
   host is looked up here, and that blocks: libpq does it so. */
PGconn *query_pq_start (const query_pq_t *pq, gboolean read_only);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_PQ_H */
