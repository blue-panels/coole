/*
   The values and the structure query-pq.h writes for libpq are those of libpq-fe.h.

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

/* coole loads libpq at run time without its headers: a value written wrong in query-pq.h would
   be seen only on a server.  It is checked here, when it is built: libpq-fe.h is only read. */

#include <config.h>

#include <stddef.h>

#include <libpq-fe.h>

// query-pq.h has the structure of its own, by the same name
#define PQconninfoOption query_PQconninfoOption
#include "src/plugins/query/query-pq.h"
#undef PQconninfoOption

// the two are enumerations of their own: their values are compared
#define SAME(ours, theirs) G_STATIC_ASSERT ((int) (ours) == (int) (theirs))

SAME (QUERY_PG_CONNECTION_OK, CONNECTION_OK);
SAME (QUERY_PG_CONNECTION_BAD, CONNECTION_BAD);

SAME (QUERY_PG_POLLING_FAILED, PGRES_POLLING_FAILED);
SAME (QUERY_PG_POLLING_READING, PGRES_POLLING_READING);
SAME (QUERY_PG_POLLING_WRITING, PGRES_POLLING_WRITING);
SAME (QUERY_PG_POLLING_OK, PGRES_POLLING_OK);

SAME (QUERY_PG_EMPTY_QUERY, PGRES_EMPTY_QUERY);
SAME (QUERY_PG_COMMAND_OK, PGRES_COMMAND_OK);
SAME (QUERY_PG_TUPLES_OK, PGRES_TUPLES_OK);
SAME (QUERY_PG_COPY_OUT, PGRES_COPY_OUT);
SAME (QUERY_PG_COPY_IN, PGRES_COPY_IN);
SAME (QUERY_PG_BAD_RESPONSE, PGRES_BAD_RESPONSE);
SAME (QUERY_PG_NONFATAL_ERROR, PGRES_NONFATAL_ERROR);
SAME (QUERY_PG_FATAL_ERROR, PGRES_FATAL_ERROR);
SAME (QUERY_PG_COPY_BOTH, PGRES_COPY_BOTH);
SAME (QUERY_PG_SINGLE_TUPLE, PGRES_SINGLE_TUPLE);
SAME (QUERY_PG_PIPELINE_SYNC, PGRES_PIPELINE_SYNC);
SAME (QUERY_PG_PIPELINE_ABORTED, PGRES_PIPELINE_ABORTED);
#ifdef LIBPQ_HAS_CHUNK_MODE
SAME (QUERY_PG_TUPLES_CHUNK, PGRES_TUPLES_CHUNK);
#endif

SAME (QUERY_PG_TRANS_IDLE, PQTRANS_IDLE);
SAME (QUERY_PG_TRANS_ACTIVE, PQTRANS_ACTIVE);
SAME (QUERY_PG_TRANS_INTRANS, PQTRANS_INTRANS);
SAME (QUERY_PG_TRANS_INERROR, PQTRANS_INERROR);
SAME (QUERY_PG_TRANS_UNKNOWN, PQTRANS_UNKNOWN);

SAME (QUERY_PG_DIAG_SEVERITY, PG_DIAG_SEVERITY);
SAME (QUERY_PG_DIAG_SQLSTATE, PG_DIAG_SQLSTATE);
SAME (QUERY_PG_DIAG_MESSAGE_PRIMARY, PG_DIAG_MESSAGE_PRIMARY);
SAME (QUERY_PG_DIAG_MESSAGE_DETAIL, PG_DIAG_MESSAGE_DETAIL);
SAME (QUERY_PG_DIAG_MESSAGE_HINT, PG_DIAG_MESSAGE_HINT);
SAME (QUERY_PG_DIAG_STATEMENT_POSITION, PG_DIAG_STATEMENT_POSITION);
SAME (QUERY_PG_DIAG_CONTEXT, PG_DIAG_CONTEXT);

// the options of PQconndefaults () are read in place
G_STATIC_ASSERT (sizeof (query_PQconninfoOption) == sizeof (PQconninfoOption));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, keyword)
                 == offsetof (PQconninfoOption, keyword));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, envvar) == offsetof (PQconninfoOption, envvar));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, compiled)
                 == offsetof (PQconninfoOption, compiled));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, val) == offsetof (PQconninfoOption, val));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, label) == offsetof (PQconninfoOption, label));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, dispchar)
                 == offsetof (PQconninfoOption, dispchar));
G_STATIC_ASSERT (offsetof (query_PQconninfoOption, dispsize)
                 == offsetof (PQconninfoOption, dispsize));

int
main (void)
{
    // the checks are those of the build
    return 0;
}
