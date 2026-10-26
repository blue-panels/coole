/*
   The query plugin: libpq, the client library of PostgreSQL, loaded at run time.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** \file query-pq.c
 *  \brief Source: libpq loaded at run time, as aspell is by the spell plugin
 */

#include <config.h>

#include <string.h>

#include <gmodule.h>

#include "lib/global.h"
#include "lib/tty/key.h"  // add_select_channel ()

#include "query-pq.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// the file of the library: the soname of libpq has been 5 since PostgreSQL 8.0
#define QUERY_PQ_SONAME      "libpq.so.5"

#define QUERY_PQ_FUNCTION(f) g_module_symbol (query_pq_module, #f, (void *) &query_pq.f)

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GModule *query_pq_module = NULL;
static gboolean query_pq_probed = FALSE;
static char *query_pq_error = NULL;
static query_pq_t query_pq;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The functions of libpq 17, NULL when it is older; for a test, the older way with libpq 17 too */
static void
query_pq_load_17 (void)
{
    const char *old = g_getenv ("COOLE_PQ_FORCE_OLD");

    if (old != NULL && *old != '\0' && strcmp (old, "0") != 0)
        return;
    if (!QUERY_PQ_FUNCTION (PQsetChunkedRowsMode))
        query_pq.PQsetChunkedRowsMode = NULL;
    if (!(QUERY_PQ_FUNCTION (PQcancelCreate) && QUERY_PQ_FUNCTION (PQcancelStart)
          && QUERY_PQ_FUNCTION (PQcancelPoll) && QUERY_PQ_FUNCTION (PQcancelStatus)
          && QUERY_PQ_FUNCTION (PQcancelSocket) && QUERY_PQ_FUNCTION (PQcancelErrorMessage)
          && QUERY_PQ_FUNCTION (PQcancelFinish)))
        query_pq.PQcancelCreate = NULL;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const query_pq_t *
query_pq_load (char **error)
{
    if (!query_pq_probed)
    {
        query_pq_probed = TRUE;
        query_pq_module = g_module_open (QUERY_PQ_SONAME, G_MODULE_BIND_LAZY);
        // last resort: libpq.so, from the development package
        if (query_pq_module == NULL)
            query_pq_module = g_module_open ("libpq", G_MODULE_BIND_LAZY);
        if (query_pq_module == NULL)
            query_pq_error = g_strdup_printf (_ ("%s is not found: libpq5 should be installed"),
                                              QUERY_PQ_SONAME);
        else if (!(QUERY_PQ_FUNCTION (PQconnectStartParams) && QUERY_PQ_FUNCTION (PQconnectPoll)
                   && QUERY_PQ_FUNCTION (PQstatus) && QUERY_PQ_FUNCTION (PQsocket)
                   && QUERY_PQ_FUNCTION (PQerrorMessage) && QUERY_PQ_FUNCTION (PQfinish)
                   && QUERY_PQ_FUNCTION (PQsetnonblocking)
                   && QUERY_PQ_FUNCTION (PQsetNoticeReceiver)
                   && QUERY_PQ_FUNCTION (PQclientEncoding)
                   && QUERY_PQ_FUNCTION (pg_encoding_to_char) && QUERY_PQ_FUNCTION (PQhost)
                   && QUERY_PQ_FUNCTION (PQport) && QUERY_PQ_FUNCTION (PQuser)
                   && QUERY_PQ_FUNCTION (PQdb) && QUERY_PQ_FUNCTION (PQserverVersion)
                   && QUERY_PQ_FUNCTION (PQlibVersion) && QUERY_PQ_FUNCTION (PQtransactionStatus)
                   && QUERY_PQ_FUNCTION (PQconndefaults) && QUERY_PQ_FUNCTION (PQconninfoFree)
                   && QUERY_PQ_FUNCTION (PQsendQuery) && QUERY_PQ_FUNCTION (PQflush)
                   && QUERY_PQ_FUNCTION (PQconsumeInput) && QUERY_PQ_FUNCTION (PQisBusy)
                   && QUERY_PQ_FUNCTION (PQgetResult) && QUERY_PQ_FUNCTION (PQsetSingleRowMode)
                   && QUERY_PQ_FUNCTION (PQputCopyEnd) && QUERY_PQ_FUNCTION (PQgetCopyData)
                   && QUERY_PQ_FUNCTION (PQfreemem) && QUERY_PQ_FUNCTION (PQresultStatus)
                   && QUERY_PQ_FUNCTION (PQresultErrorField)
                   && QUERY_PQ_FUNCTION (PQresultErrorMessage) && QUERY_PQ_FUNCTION (PQcmdStatus)
                   && QUERY_PQ_FUNCTION (PQntuples) && QUERY_PQ_FUNCTION (PQnfields)
                   && QUERY_PQ_FUNCTION (PQfname) && QUERY_PQ_FUNCTION (PQftype)
                   && QUERY_PQ_FUNCTION (PQgetvalue) && QUERY_PQ_FUNCTION (PQgetisnull)
                   && QUERY_PQ_FUNCTION (PQgetlength) && QUERY_PQ_FUNCTION (PQclear)
                   && QUERY_PQ_FUNCTION (PQgetCancel) && QUERY_PQ_FUNCTION (PQfreeCancel)
                   && QUERY_PQ_FUNCTION (PQcancel)))
        {
            query_pq_error = g_strdup_printf (_ ("%s lacks a function of libpq: %s"),
                                              g_module_name (query_pq_module), g_module_error ());
            g_module_close (query_pq_module);
            query_pq_module = NULL;
        }
        else
            query_pq_load_17 ();
    }
    if (query_pq_module == NULL)
    {
        if (error != NULL)
            *error = g_strdup (query_pq_error);
        return NULL;
    }
    return &query_pq;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_pq_chunks (const query_pq_t *pq)
{
    return pq->PQsetChunkedRowsMode != NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_pq_async_cancel (const query_pq_t *pq)
{
    return pq->PQcancelCreate != NULL;
}

/* --------------------------------------------------------------------------------------------- */

PGconn *
query_pq_start (const query_pq_t *pq, gboolean read_only)
{
    const char *keywords[4] = { "client_encoding", "application_name", NULL, NULL };
    const char *values[4] = { "UTF8", "coole", NULL, NULL };

    if (read_only)
    {
        // a parameter of the connection, made again with it, no query after it
        keywords[2] = "options";
        values[2] = "-c default_transaction_read_only=on";
    }
    return pq->PQconnectStartParams (keywords, values, 0);
}

/* --------------------------------------------------------------------------------------------- */

/* The connection on its way: PQconnectPoll () each time its socket is ready */
static void
query_pq_link_poll (query_pq_link_t *link)
{
    const query_pq_t *pq = link->pq;
    int status = QUERY_PG_POLLING_FAILED, i;

    // no socket to wait for: a connection refused at once, and libpq goes on to the next address
    for (i = 0; i < 64; i++)
    {
        status = pq->PQconnectPoll (link->conn);
        if ((status != QUERY_PG_POLLING_READING && status != QUERY_PG_POLLING_WRITING)
            || pq->PQsocket (link->conn) >= 0)
            break;
    }
    if (pq->PQsocket (link->conn) < 0 && status != QUERY_PG_POLLING_OK)
        status = QUERY_PG_POLLING_FAILED;
    switch (status)
    {
    case QUERY_PG_POLLING_READING:
        query_pq_link_watch (link, FALSE);
        break;
    case QUERY_PG_POLLING_WRITING:
        query_pq_link_watch (link, TRUE);
        break;
    default:
        link->connecting = FALSE;
        query_pq_link_unwatch (link);
        if (status == QUERY_PG_POLLING_OK)
            (void) pq->PQsetnonblocking (link->conn, 1);
        link->connected (link->data, status == QUERY_PG_POLLING_OK);
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
query_pq_link_ready (int fd, void *data)
{
    query_pq_link_t *link = (query_pq_link_t *) data;

    (void) fd;
    if (link->connecting)
        query_pq_link_poll (link);
    else
        link->ready (link->data);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pq_link_set_fd (query_pq_link_t *link, int fd, gboolean write)
{
    // the socket of another address tried, or of a connection made again
    if (link->fd != fd && link->fd >= 0)
        delete_select_channel (link->fd);
    if (fd >= 0 && link->fd != fd)
        add_select_channel (fd, query_pq_link_ready, link);
    link->fd = fd;
    if (fd >= 0)
        select_channel_want_write (fd, write);
}

/* --------------------------------------------------------------------------------------------- */

void
query_pq_link_init (query_pq_link_t *link, const query_pq_t *pq,
                    void (*connected) (void *data, gboolean ok), void (*ready) (void *data),
                    void *data)
{
    memset (link, 0, sizeof (*link));
    link->pq = pq;
    link->fd = -1;
    link->connected = connected;
    link->ready = ready;
    link->data = data;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_pq_link_open (query_pq_link_t *link, gboolean read_only)
{
    query_pq_link_close (link);
    link->conn = query_pq_start (link->pq, read_only);
    if (link->conn == NULL)
        return FALSE;
    link->connecting = TRUE;
    if (link->pq->PQstatus (link->conn) == QUERY_PG_CONNECTION_BAD)
    {
        link->connecting = FALSE;
        link->connected (link->data, FALSE);
    }
    else if (link->pq->PQsocket (link->conn) < 0)
        query_pq_link_poll (link);
    else
        query_pq_link_watch (link, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
query_pq_link_close (query_pq_link_t *link)
{
    query_pq_link_unwatch (link);
    if (link->conn != NULL)
        link->pq->PQfinish (link->conn);
    link->conn = NULL;
    link->connecting = link->flushing = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
query_pq_link_watch (query_pq_link_t *link, gboolean write)
{
    query_pq_link_set_fd (link, link->pq->PQsocket (link->conn), write);
}

/* --------------------------------------------------------------------------------------------- */

void
query_pq_link_unwatch (query_pq_link_t *link)
{
    query_pq_link_set_fd (link, -1, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_pq_link_flush (query_pq_link_t *link)
{
    const int r = link->pq->PQflush (link->conn);

    link->flushing = r > 0;
    query_pq_link_watch (link, link->flushing);
    return r >= 0;
}

/* --------------------------------------------------------------------------------------------- */
