/** \file content-frame.h
 *  \brief Header: the messages of a stream framed by a "Content-Length" header
 *
 *  The Debug Adapter Protocol and the Language Server Protocol send each message after a header
 *  "Content-Length: N\r\n\r\n", N the length of the body in bytes.  The reader takes the bytes as
 *  they come, torn anywhere, and gives the bodies whole.
 */

#ifndef MC__CONTENT_FRAME_H
#define MC__CONTENT_FRAME_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* a header longer than that is no header */
#define CONTENT_FRAME_MAX_HEADER 8192

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct content_frame_reader_t content_frame_reader_t;

/* A body: @len bytes and a '\0' after them; it is the reader's, gone when @fn returns */
typedef void (*content_frame_fn) (const char *body, gsize len, void *data);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* A reader of bodies of @max_body bytes at most */
content_frame_reader_t *content_frame_reader_new (gsize max_body);
/* The bytes that came: @fn gets each body they end.  FALSE when the stream is broken: a header
   too long, without its length or with two, or a body too big; what is buffered is then dropped
   and the reader is clean for a new stream.  @fn must not free the reader. */
gboolean content_frame_reader_feed (content_frame_reader_t *reader, const char *bytes, gsize len,
                                    content_frame_fn fn, void *data);
/* What is buffered dropped: a new stream starts; from @fn too, and the feed then stops */
void content_frame_reader_reset (content_frame_reader_t *reader);
void content_frame_reader_free (content_frame_reader_t *reader);

/* A body with its header, as it goes on the stream, @len bytes; free with g_free() */
char *content_frame_wrap (const char *body, gsize body_len, gsize *len);

/*** inline functions ****************************************************************************/

#endif
