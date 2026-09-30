/** \file plugin-service.h
 *  \brief Header: the services plugins offer one another
 *
 * A plugin offers a service under a name, with one function every call goes through: the name
 * of a method and its arguments, a dictionary of simple values (a{sv}), and an answer of the same
 * kind.  Whoever calls, a plugin in C or a script, calls the same way; the program passes the
 * calls on and knows nothing of what they mean.  A service tells those who listen what has
 * happened by a signal, with arguments of the same kind.
 */

#ifndef MC__PLUGIN_SERVICE_H
#define MC__PLUGIN_SERVICE_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MC_SERVICE_ERROR mc_service_error_quark ()

/* The arguments and the answers of the services */
#define MC_SERVICE_ARGS_TYPE G_VARIANT_TYPE_VARDICT

/*** enums ***************************************************************************************/

typedef enum
{
    MC_SERVICE_ERROR_NOT_FOUND,  // no service of that name
    MC_SERVICE_ERROR_EXISTS,     // a service of that name is there already
    MC_SERVICE_ERROR_METHOD,     // the service has no such method
    MC_SERVICE_ERROR_ARGS,       // the arguments are not what the method takes
    MC_SERVICE_ERROR_FAILED      // the method failed
} mc_service_error_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/* A call of @method with @args, a{sv}.  The answer is a new a{sv}, or NULL with @error set.  The
   service does not keep @args. */
typedef GVariant *(*mc_service_call_fn) (void *data, const char *method, GVariant *args,
                                         GError **error);

/* @signal of the service @name happened, with @args, a{sv} */
typedef void (*mc_service_signal_fn) (const char *name, const char *signal, GVariant *args,
                                      void *user_data);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

GQuark mc_service_error_quark (void);

/* Offer a service; FALSE with @error when the name is taken */
gboolean mc_service_register (const char *name, mc_service_call_fn call, void *data,
                              GError **error);
void mc_service_unregister (const char *name);
gboolean mc_service_exists (const char *name);

/* Call a method of a service.  @args is a{sv}, or NULL for none; a floating reference is taken.
   The answer is a{sv}, which the caller unrefs, or NULL with @error set. */
GVariant *mc_service_call (const char *name, const char *method, GVariant *args, GError **error);

/* Listen to the signals of the service @name, whether it is there yet or not.  The id is what
   mc_service_disconnect() takes. */
guint mc_service_connect (const char *name, mc_service_signal_fn fn, void *user_data);
void mc_service_disconnect (guint id);
/* Tell those who listen to the service @name about @signal.  @args is a{sv}, or NULL; a floating
   reference is taken. */
void mc_service_emit (const char *name, const char *signal, GVariant *args);

/* Forget every service and listener: the program ends */
void mc_service_shutdown (void);

/*** inline functions ****************************************************************************/

#endif
