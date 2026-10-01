/*
   lib - the services plugins offer one another

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/lib/plugin_service"

#include "tests/mctest.h"

#include "lib/plugin-service.h"

/* --------------------------------------------------------------------------------------------- */

/* A service that echoes its arguments and counts its calls; "fail" fails */
static int calls;

static GVariant *
echo_call (void *data, const char *method, GVariant *args, GError **error)
{
    GVariantDict dict;

    calls++;
    ck_assert_ptr_eq (data, &calls);

    if (strcmp (method, "fail") == 0)
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_FAILED, "failed on purpose");
        return NULL;
    }
    if (strcmp (method, "echo") != 0)
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD, "no %s", method);
        return NULL;
    }

    g_variant_dict_init (&dict, args);
    g_variant_dict_insert (&dict, "method", "s", method);
    return g_variant_dict_end (&dict);
}

/* --------------------------------------------------------------------------------------------- */

static int heard;
static char *heard_signal;
static gint64 heard_id;

static void
listener (const char *name, const char *signal, GVariant *args, void *user_data)
{
    ck_assert_str_eq (name, "echo");
    heard += GPOINTER_TO_INT (user_data);
    g_free (heard_signal);
    heard_signal = g_strdup (signal);
    heard_id = -1;
    (void) g_variant_lookup (args, "id", "x", &heard_id);
}

/* --------------------------------------------------------------------------------------------- */

static guint self_id;

static void
self_disconnecting (const char *name, const char *signal, GVariant *args, void *user_data)
{
    (void) name;
    (void) signal;
    (void) args;
    (void) user_data;

    heard += 100;
    mc_service_disconnect (self_id);
}

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    calls = 0;
    heard = 0;
    heard_id = 0;
    ck_assert (mc_service_register ("echo", echo_call, &calls, NULL));
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    mc_service_shutdown ();
    g_clear_pointer (&heard_signal, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* A call goes to the service with its arguments, and the answer comes back */
START_TEST (test_call)
{
    GVariantDict dict;
    GVariant *answer;
    GError *error = NULL;
    const char *text = NULL;
    const char *method = NULL;

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "text", "s", "hello");
    answer = mc_service_call ("echo", "echo", g_variant_dict_end (&dict), &error);

    ck_assert_ptr_null (error);
    ck_assert_ptr_nonnull (answer);
    ck_assert (g_variant_lookup (answer, "text", "&s", &text));
    ck_assert_str_eq (text, "hello");
    ck_assert (g_variant_lookup (answer, "method", "&s", &method));
    ck_assert_str_eq (method, "echo");
    ck_assert_int_eq (calls, 1);
    g_variant_unref (answer);

    // no arguments is an empty dictionary
    answer = mc_service_call ("echo", "echo", NULL, &error);
    ck_assert_ptr_nonnull (answer);
    ck_assert (g_variant_lookup (answer, "method", "&s", &method));
    g_variant_unref (answer);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* What cannot be called says why */
START_TEST (test_errors)
{
    GError *error = NULL;
    GError *twice = NULL;

    ck_assert_ptr_null (mc_service_call ("nothing", "echo", NULL, &error));
    ck_assert (g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_NOT_FOUND));
    g_clear_error (&error);

    ck_assert_ptr_null (mc_service_call ("echo", "fail", NULL, &error));
    ck_assert (g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_FAILED));
    g_clear_error (&error);

    ck_assert_ptr_null (mc_service_call ("echo", "other", NULL, &error));
    ck_assert (g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD));
    g_clear_error (&error);

    // the arguments are a dictionary or nothing
    ck_assert_ptr_null (mc_service_call ("echo", "echo", g_variant_new_string ("x"), &error));
    ck_assert (g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS));
    g_clear_error (&error);
    ck_assert_int_eq (calls, 2);

    // one service to a name
    ck_assert (!mc_service_register ("echo", echo_call, &calls, &twice));
    ck_assert (g_error_matches (twice, MC_SERVICE_ERROR, MC_SERVICE_ERROR_EXISTS));
    g_clear_error (&twice);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A service that goes is not called any more */
START_TEST (test_unregister)
{
    GError *error = NULL;

    ck_assert (mc_service_exists ("echo"));
    mc_service_unregister ("echo");
    ck_assert (!mc_service_exists ("echo"));
    ck_assert_ptr_null (mc_service_call ("echo", "echo", NULL, &error));
    ck_assert (g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_NOT_FOUND));
    g_clear_error (&error);
    ck_assert_int_eq (calls, 0);

    // and its name can be taken again
    ck_assert (mc_service_register ("echo", echo_call, &calls, NULL));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A signal reaches those who listen to that service, and only while they listen */
START_TEST (test_signals)
{
    GVariantDict dict;
    guint a, b, other;

    a = mc_service_connect ("echo", listener, GINT_TO_POINTER (1));
    b = mc_service_connect ("echo", listener, GINT_TO_POINTER (10));
    other = mc_service_connect ("other", listener, GINT_TO_POINTER (1000));
    ck_assert_uint_ne (a, b);

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", (gint64) 7);
    mc_service_emit ("echo", "closed", g_variant_dict_end (&dict));
    ck_assert_int_eq (heard, 11);
    ck_assert_str_eq (heard_signal, "closed");
    ck_assert_int_eq (heard_id, 7);

    mc_service_disconnect (a);
    mc_service_emit ("echo", "closed", NULL);
    ck_assert_int_eq (heard, 21);
    ck_assert_int_eq (heard_id, -1);

    mc_service_disconnect (b);
    mc_service_disconnect (other);
    mc_service_emit ("echo", "closed", NULL);
    ck_assert_int_eq (heard, 21);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A listener may disconnect itself while it is told */
START_TEST (test_disconnect_in_signal)
{
    guint b;

    self_id = mc_service_connect ("echo", self_disconnecting, NULL);
    b = mc_service_connect ("echo", listener, GINT_TO_POINTER (1));

    mc_service_emit ("echo", "closed", NULL);
    ck_assert_int_eq (heard, 101);
    mc_service_emit ("echo", "closed", NULL);
    ck_assert_int_eq (heard, 102);

    mc_service_disconnect (b);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_call);
    tcase_add_test (tc_core, test_errors);
    tcase_add_test (tc_core, test_unregister);
    tcase_add_test (tc_core, test_signals);
    tcase_add_test (tc_core, test_disconnect_in_signal);

    return mctest_run_all (tc_core);
}
