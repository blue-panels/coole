/*
   The services plugins offer one another.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** \file plugin-service.c
 *  \brief Source: the services plugins offer one another
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"

#include "lib/plugin-service.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_service_call_fn call;
    void *data;
} service_t;

typedef struct
{
    guint id;
    char *name;
    mc_service_signal_fn fn;
    void *user_data;
} listener_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GHashTable *services = NULL;  // name -> service_t
static GList *listeners = NULL;      // listener_t, in the order they came
static guint last_listener_id = 0;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
listener_free (gpointer data)
{
    listener_t *l = (listener_t *) data;

    g_free (l->name);
    g_free (l);
}

/* --------------------------------------------------------------------------------------------- */

/* The arguments as a service takes them: a{sv}, an empty one for none */
static GVariant *
service_args (GVariant *args)
{
    if (args == NULL)
        return g_variant_ref_sink (g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0));

    return g_variant_ref_sink (args);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

GQuark
mc_service_error_quark (void)
{
    return g_quark_from_static_string ("mc-service-error-quark");
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_service_register (const char *name, mc_service_call_fn call, void *data, GError **error)
{
    service_t *s;

    g_return_val_if_fail (name != NULL && call != NULL, FALSE);

    if (services == NULL)
        services = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    if (g_hash_table_contains (services, name))
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_EXISTS,
                     "the service %s is there already", name);
        return FALSE;
    }

    s = g_new (service_t, 1);
    s->call = call;
    s->data = data;
    g_hash_table_insert (services, g_strdup (name), s);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_service_unregister (const char *name)
{
    if (services != NULL && name != NULL)
        g_hash_table_remove (services, name);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_service_exists (const char *name)
{
    return (services != NULL && name != NULL && g_hash_table_contains (services, name));
}

/* --------------------------------------------------------------------------------------------- */

GVariant *
mc_service_call (const char *name, const char *method, GVariant *args, GError **error)
{
    const service_t *s = NULL;
    GVariant *a, *answer;
    GError *e = NULL;

    g_return_val_if_fail (name != NULL && method != NULL, NULL);

    a = service_args (args);

    if (!g_variant_is_of_type (a, MC_SERVICE_ARGS_TYPE))
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS,
                     "the arguments of %s.%s are not a{sv}", name, method);
        g_variant_unref (a);
        return NULL;
    }

    if (services != NULL)
        s = (const service_t *) g_hash_table_lookup (services, name);

    if (s == NULL)
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_NOT_FOUND, "no service %s", name);
        g_variant_unref (a);
        return NULL;
    }

    answer = s->call (s->data, method, a, &e);
    g_variant_unref (a);

    if (answer == NULL)
    {
        if (e == NULL)
            e = g_error_new (MC_SERVICE_ERROR, MC_SERVICE_ERROR_FAILED, "%s.%s failed", name,
                             method);
        g_propagate_error (error, e);
        return NULL;
    }

    g_clear_error (&e);
    return g_variant_ref_sink (answer);
}

/* --------------------------------------------------------------------------------------------- */

guint
mc_service_connect (const char *name, mc_service_signal_fn fn, void *user_data)
{
    listener_t *l;

    g_return_val_if_fail (name != NULL && fn != NULL, 0);

    l = g_new (listener_t, 1);
    l->id = ++last_listener_id;
    l->name = g_strdup (name);
    l->fn = fn;
    l->user_data = user_data;
    listeners = g_list_append (listeners, l);

    return l->id;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_service_disconnect (guint id)
{
    GList *i;

    for (i = listeners; i != NULL; i = g_list_next (i))
    {
        listener_t *l = (listener_t *) i->data;

        if (l->id == id)
        {
            listeners = g_list_delete_link (listeners, i);
            listener_free (l);
            return;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mc_service_emit (const char *name, const char *signal, GVariant *args)
{
    GVariant *a;
    GArray *ids;
    guint n;

    g_return_if_fail (name != NULL && signal != NULL);

    a = service_args (args);

    /* A listener may disconnect itself, or others, when it is told: the ones to tell are taken
       first, and each is looked for again before it is told. */
    ids = g_array_new (FALSE, FALSE, sizeof (guint));
    for (GList *i = listeners; i != NULL; i = g_list_next (i))
    {
        const listener_t *l = (const listener_t *) i->data;

        if (strcmp (l->name, name) == 0)
            g_array_append_val (ids, l->id);
    }

    for (n = 0; n < ids->len; n++)
    {
        const guint id = g_array_index (ids, guint, n);

        for (GList *i = listeners; i != NULL; i = g_list_next (i))
        {
            const listener_t *l = (const listener_t *) i->data;

            if (l->id == id)
            {
                l->fn (name, signal, a, l->user_data);
                break;
            }
        }
    }

    g_array_free (ids, TRUE);
    g_variant_unref (a);
}

/* --------------------------------------------------------------------------------------------- */

void
mc_service_shutdown (void)
{
    if (services != NULL)
    {
        g_hash_table_destroy (services);
        services = NULL;
    }

    g_list_free_full (listeners, listener_free);
    listeners = NULL;
}

/* --------------------------------------------------------------------------------------------- */
