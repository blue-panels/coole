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

#include <gmodule.h>

#include "lib/global.h"

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
