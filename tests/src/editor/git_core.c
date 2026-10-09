#include <config.h>

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <stdlib.h>  // realpath()
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "src/plugins/git/git-core.h"

static char *top = NULL;

/* git in the repository of the test, which has to do what it is told */
static char *
git (const char *first, ...)
{
    GPtrArray *args = g_ptr_array_new ();
    git_output_t o;
    const char *a;
    va_list ap;
    char *out;

    va_start (ap, first);
    for (a = first; a != NULL; a = va_arg (ap, const char *))
        g_ptr_array_add (args, (gpointer) a);
    va_end (ap);
    g_ptr_array_add (args, NULL);
    g_assert_true (git_run (top, (const char *const *) args->pdata, NULL, &o));
    if (o.status != 0)
        g_printerr ("git %s: %s\n", first, o.err->str);
    g_assert_cmpint (o.status, ==, 0);
    out = g_strdup (g_strchomp (o.out->str));
    git_output_clear (&o);
    g_ptr_array_free (args, TRUE);
    return out;
}

static void
put (const char *rel, const char *text)
{
    char *path = g_build_filename (top, rel, (char *) NULL);

    g_assert_true (g_file_set_contents (path, text, -1, NULL));
    g_free (path);
}

static void
commit (const char *rel, const char *text, const char *message)
{
    put (rel, text);
    g_free (git ("add", rel, NULL));
    g_free (git ("commit", "-q", "-m", message, NULL));
}

/* --------------------------------------------------------------------------------------------- */

static void
test_strip (void)
{
    char *s;

    s = git_message_strip ("\n\nsubject  \n\n\n\nbody\n# comment\n\n");
    g_assert_cmpstr (s, ==, "subject\n\nbody\n");
    g_free (s);
    s = git_message_strip ("# only\n#comments\n\n");
    g_assert_cmpstr (s, ==, "");
    g_free (s);
    // git commit -v: the diff under the scissors is no part of it
    s = git_message_strip ("subject\n# ------------------------ >8 ------------------------\n"
                           "diff --git a/f b/f\n+line\n");
    g_assert_cmpstr (s, ==, "subject\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_graph (void)
{
    // main: m, a, c; side: b, taken in by the merge m and gone off from c
    const char log_text[] = "m\037A\0371\037HEAD -> main\037a b\037merge\036"
                            "a\037A\0371\037\037c\037first\036"
                            "b\037A\0371\037side\037c\037second\036"
                            "c\037A\0371\037\037\037base\036";
    // develop merged and gone, named by the merge; main known by its remote
    const char gone_text[] = "m\037A\0371\037origin/main\037a b\037Merge branch 'develop'\036"
                             "b\037A\0371\037\037a\037on develop\036"
                             "a\037A\0371\037\037\037base\036";
    const char *const remotes[] = { "origin", NULL };
    GPtrArray *commits = git_parse_log (log_text, sizeof (log_text) - 1);
    GPtrArray *rows = git_graph_build (commits, NULL);
    const git_commit_t *merge = g_ptr_array_index (commits, 0);
    const git_graph_row_t *r;

    g_assert_cmpuint (commits->len, ==, 4);
    g_assert_cmpint (merge->parents, ==, 2);
    g_assert_cmpstr (merge->parent_shas[0], ==, "a");
    g_assert_cmpstr (merge->parent_shas[1], ==, "b");

    r = g_ptr_array_index (rows, 0);
    g_assert_cmpint (r->cols, ==, 2);
    g_assert_cmpint (r->node, ==, 0);
    g_assert_cmpint (r->line[0], ==,
                     GIT_GRAPH_NODE | GIT_GRAPH_MERGE | GIT_GRAPH_DOWN | GIT_GRAPH_RIGHT);
    g_assert_cmpint (r->line[1], ==, GIT_GRAPH_DOWN | GIT_GRAPH_LEFT);
    g_assert_cmpint (r->link[0], ==, GIT_GRAPH_LINK_TO_LEFT);
    g_assert_cmpint (r->color[0], ==, GIT_GRAPH_COLOR_MAIN);
    g_assert_cmpint (r->color[1], ==, GIT_GRAPH_COLOR_OTHER);
    r = g_ptr_array_index (rows, 1);
    g_assert_cmpint (r->node, ==, 0);
    g_assert_cmpint (r->line[1], ==, GIT_GRAPH_UP | GIT_GRAPH_DOWN);
    r = g_ptr_array_index (rows, 2);
    g_assert_cmpint (r->node, ==, 1);
    g_assert_cmpint (r->line[0], ==, GIT_GRAPH_UP | GIT_GRAPH_DOWN);
    r = g_ptr_array_index (rows, 3);
    g_assert_cmpint (r->line[0], ==, GIT_GRAPH_NODE | GIT_GRAPH_UP | GIT_GRAPH_RIGHT);
    g_assert_cmpint (r->line[1], ==, GIT_GRAPH_UP | GIT_GRAPH_LEFT);
    g_assert_cmpint (r->link[0], ==, GIT_GRAPH_LINK_LINE);
    g_ptr_array_unref (rows);
    g_ptr_array_unref (commits);

    commits = git_parse_log (gone_text, sizeof (gone_text) - 1);
    rows = git_graph_build (commits, remotes);
    r = g_ptr_array_index (rows, 0);
    g_assert_cmpint (r->color[0], ==, GIT_GRAPH_COLOR_MAIN);
    r = g_ptr_array_index (rows, 1);
    g_assert_cmpint (r->node, ==, 1);
    g_assert_cmpint (r->color[1], ==, GIT_GRAPH_COLOR_DEVELOP);
    g_ptr_array_unref (rows);
    g_ptr_array_unref (commits);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_trailer (void)
{
    char *s;

    // after an empty line, before the lines git leaves out
    s = git_message_add_trailer ("subject\n\nbody\n\n# comment\n#\n", "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==, "subject\n\nbody\n\nSigned-off-by: A <a@b>\n\n# comment\n#\n");
    g_free (s);
    // into the block of trailers there is, once
    s = git_message_add_trailer ("subject\n\nFixes: 123 (\"x\")\n", "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==, "subject\n\nFixes: 123 (\"x\")\nSigned-off-by: A <a@b>\n");
    g_free (s);
    s = git_message_add_trailer ("subject\n\nSigned-off-by: A <a@b>\n", "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==, "subject\n\nSigned-off-by: A <a@b>\n");
    g_free (s);
    // a subject that looks like a trailer is no block of trailers
    s = git_message_add_trailer ("editor: fix it\n", "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==, "editor: fix it\n\nSigned-off-by: A <a@b>\n");
    g_free (s);
    // above the scissors of git commit -v, not after the diff
    s = git_message_add_trailer ("subject\n\n# ------------------------ >8 "
                                 "------------------------\n+line\n",
                                 "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==,
                     "subject\n\nSigned-off-by: A <a@b>\n\n# ------------------------ >8 "
                     "------------------------\n+line\n");
    g_free (s);
    // no message yet
    s = git_message_add_trailer ("\n# comment\n", "Signed-off-by: A <a@b>");
    g_assert_cmpstr (s, ==, "\n\nSigned-off-by: A <a@b>\n\n# comment\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_replace (void)
{
    char *s;

    s = git_message_replace ("old\n\nSigned-off-by: A <a@b>\n\n# comment\n",
                             "new: subject\n\nnew body\n", TRUE);
    g_assert_cmpstr (s, ==, "new: subject\n\nnew body\n\nSigned-off-by: A <a@b>\n\n# comment\n");
    g_free (s);
    s = git_message_replace ("old\n\nSigned-off-by: A <a@b>\n# comment\n", "back\n", FALSE);
    g_assert_cmpstr (s, ==, "back\n\n# comment\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_check (void)
{
    char *s;

    s = git_message_check ("editor: a short subject\n\nbody\n# a comment that is long enough to be "
                           "over seventy-two characters, and is no part of it\n");
    g_assert_cmpstr (s, ==, "");
    g_free (s);
    s = git_message_check ("A subject that is much longer than the fifty characters.\nbody\n");
    g_assert_nonnull (strstr (s, "over 50"));
    g_assert_nonnull (strstr (s, "period"));
    g_assert_nonnull (strstr (s, "second line"));
    g_free (s);
    s = git_message_check ("# only\n");
    g_assert_nonnull (strstr (s, "empty"));
    g_free (s);
    // the lines numbered as in the file, comments and all; bytes that are no UTF-8 counted
    s = git_message_check (
        "# a comment\n\nsubject\n\n"
        "a body line that is long enough to be well over the seventy-two characters\xe9\n");
    g_assert_cmpstr (s, ==, "Line 5 is 75 characters long, over 72.\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_menu (void)
{
    char *text;
    GPtrArray *menu;
    const git_menu_item_t *item;
    GError *error = NULL;

    // the menu of coole has the items of the plugin, in their order
    g_assert_true (g_file_get_contents (GIT_MENU_INI, &text, NULL, NULL));
    menu = git_menu_parse (text, &error);
    g_free (text);
    g_assert_no_error (error);
    g_assert_cmpuint (menu->len, ==, 9);
    item = g_ptr_array_index (menu, 0);
    g_assert_cmpint (item->key, ==, 's');
    g_assert_cmpstr (item->action, ==, "signoff");
    g_assert_null (item->label);
    item = g_ptr_array_index (menu, 3);
    g_assert_cmpint (item->key, ==, 'r');
    g_assert_true (git_menu_item_in_mode (item, "reword"));
    g_assert_false (git_menu_item_in_mode (item, "commit"));
    g_ptr_array_free (menu, TRUE);

    // a command of the user; a group with nothing to do is no item
    menu = git_menu_parse ("[x]\nlabel = Mine\ncommand = echo 'A: b'\noutput = trailer\n"
                           "[y]\nlabel = Nothing\n",
                           &error);
    g_assert_no_error (error);
    g_assert_cmpuint (menu->len, ==, 1);
    item = g_ptr_array_index (menu, 0);
    g_assert_cmpint (item->key, ==, 'x');
    g_assert_cmpstr (item->command, ==, "echo 'A: b'");
    g_assert_cmpstr (item->output, ==, "trailer");
    g_assert_null (item->action);
    g_assert_true (git_menu_item_in_mode (item, "commit"));
    g_ptr_array_free (menu, TRUE);

    // the backslashes of a command are those of sh
    menu = git_menu_parse ("[g]\ncommand = grep -P '\\d+' | sed 's/a\\nb/c/'\n", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (menu->len, ==, 1);
    item = g_ptr_array_index (menu, 0);
    g_assert_cmpstr (item->command, ==, "grep -P '\\d+' | sed 's/a\\nb/c/'");
    g_ptr_array_free (menu, TRUE);

    g_assert_null (git_menu_parse ("not an ini", &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_parse_status (void)
{
    // a file in the index and changed again, a rename, a new file not tracked, a conflict
    static const char text[] = "# branch.oid 0123\0# branch.head master\0"
                               "# branch.upstream origin/master\0# branch.ab +2 -1\0"
                               "1 MM N... 100644 100644 100644 a b one.c\0"
                               "2 R. N... 100644 100644 100644 a b R100 new name.c\0old.c\0"
                               "u UU N... 1 2 3 4 a b c both.c\0"
                               "? junk.txt\0";
    GPtrArray *staged = g_ptr_array_new_with_free_func (git_change_free);
    GPtrArray *unstaged = g_ptr_array_new_with_free_func (git_change_free);
    git_branch_t branch = { 0 };
    const git_change_t *c;

    git_parse_status (text, sizeof (text) - 1, staged, unstaged, &branch);
    g_assert_cmpstr (branch.head, ==, "master");
    g_assert_cmpstr (branch.upstream, ==, "origin/master");
    g_assert_cmpint (branch.ahead, ==, 2);
    g_assert_cmpint (branch.behind, ==, 1);

    g_assert_cmpuint (staged->len, ==, 2);
    c = g_ptr_array_index (staged, 0);
    g_assert_cmpint (c->code, ==, 'M');
    g_assert_cmpstr (c->path, ==, "one.c");
    c = g_ptr_array_index (staged, 1);
    g_assert_cmpint (c->code, ==, 'R');
    g_assert_cmpstr (c->path, ==, "new name.c");
    g_assert_cmpstr (c->orig, ==, "old.c");

    g_assert_cmpuint (unstaged->len, ==, 3);
    c = g_ptr_array_index (unstaged, 0);
    g_assert_cmpint (c->code, ==, 'M');
    c = g_ptr_array_index (unstaged, 1);
    g_assert_cmpint (c->code, ==, 'U');
    g_assert_cmpstr (c->path, ==, "both.c");
    c = g_ptr_array_index (unstaged, 2);
    g_assert_cmpint (c->code, ==, '?');
    g_assert_cmpstr (c->path, ==, "junk.txt");

    git_branch_clear (&branch);
    g_ptr_array_free (staged, TRUE);
    g_ptr_array_free (unstaged, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_parse_name_status (void)
{
    static const char text[] = "M\0a.c\0R087\0old.h\0new.h\0D\0gone.c\0";
    GPtrArray *files = git_parse_name_status (text, sizeof (text) - 1);
    const git_change_t *c;

    g_assert_cmpuint (files->len, ==, 3);
    c = g_ptr_array_index (files, 1);
    g_assert_cmpint (c->code, ==, 'R');
    g_assert_cmpstr (c->path, ==, "new.h");
    g_assert_cmpstr (c->orig, ==, "old.h");
    c = g_ptr_array_index (files, 2);
    g_assert_cmpint (c->code, ==, 'D');
    g_assert_cmpstr (c->path, ==, "gone.c");
    g_ptr_array_free (files, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_repository (void)
{
    char *log, *tree, *head, *message, *sub, *text;
    GPtrArray *commits;
    const git_commit_t *c;
    char *new_head = NULL;
    GError *error = NULL;
    git_output_t o;
    const char *status_args[] = { "status", "--porcelain=v2", "-z", "--branch", NULL };
    const char *log_args[] = { "log", GIT_LOG_FORMAT, NULL };
    GPtrArray *staged, *unstaged;
    git_branch_t branch = { 0 };

    g_free (git ("init", "-q", "-b", "master", NULL));
    commit ("a.txt", "one\n", "first");
    commit ("a.txt", "two\n", "second\n\nwith a body");
    commit ("b.txt", "three\n", "third");
    sub = g_build_filename (top, "sub", (char *) NULL);
    g_assert_cmpint (g_mkdir (sub, 0700), ==, 0);

    // the work tree from a directory in it
    {
        char *found = git_toplevel (sub);
        // git gives the path with its links resolved: /var is /private/var on macOS
        char *real = realpath (top, NULL);

        g_assert_nonnull (real);
        g_assert_cmpstr (found, ==, real);
        free (real);
        g_free (found);
    }

    g_assert_true (git_run (top, log_args, NULL, &o));
    commits = git_parse_log (o.out->str, o.out->len);
    git_output_clear (&o);
    g_assert_cmpuint (commits->len, ==, 3);
    c = g_ptr_array_index (commits, 0);
    g_assert_cmpstr (c->subject, ==, "third");
    g_assert_cmpint (c->parents, ==, 1);
    g_assert_nonnull (strstr (c->refs, "master"));
    c = g_ptr_array_index (commits, 2);
    g_assert_cmpint (c->parents, ==, 0);

    // changes not committed stay as they are through the reword
    put ("a.txt", "dirty\n");
    put ("c.txt", "new\n");
    g_free (git ("add", "c.txt", NULL));
    tree = git ("rev-parse", "HEAD^{tree}", NULL);
    c = g_ptr_array_index (commits, 1);
    g_assert_true (git_reword (top, c->sha, "second, better\n\nbody too\n", &new_head, &error));
    g_assert_no_error (error);
    head = git ("rev-parse", "HEAD", NULL);
    g_assert_cmpstr (head, ==, new_head);
    g_free (head);
    head = git ("rev-parse", "HEAD^{tree}", NULL);
    g_assert_cmpstr (head, ==, tree);
    g_free (head);
    message = git ("log", "-1", "--format=%B", "HEAD~1", NULL);
    g_assert_cmpstr (message, ==, "second, better\n\nbody too");
    g_free (message);
    message = git ("log", "-1", "--format=%s", "HEAD", NULL);
    g_assert_cmpstr (message, ==, "third");
    g_free (message);
    log = git ("rev-list", "--count", "HEAD", NULL);
    g_assert_cmpstr (log, ==, "3");
    g_free (log);
    text = NULL;
    {
        char *path = g_build_filename (top, "a.txt", (char *) NULL);

        g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
        g_free (path);
    }
    g_assert_cmpstr (text, ==, "dirty\n");
    g_free (text);

    // the root commit too
    c = g_ptr_array_index (commits, 2);
    g_assert_true (git_reword (top, c->sha, "first of all\n", NULL, &error));
    g_assert_no_error (error);
    message = git ("log", "-1", "--format=%s", "HEAD~2", NULL);
    g_assert_cmpstr (message, ==, "first of all");
    g_free (message);

    // what git status says of it
    g_assert_true (git_run (top, status_args, NULL, &o));
    staged = g_ptr_array_new_with_free_func (git_change_free);
    unstaged = g_ptr_array_new_with_free_func (git_change_free);
    git_parse_status (o.out->str, o.out->len, staged, unstaged, &branch);
    git_output_clear (&o);
    g_assert_cmpstr (branch.head, ==, "master");
    g_assert_nonnull (branch.sha);
    g_assert_cmpuint (staged->len, ==, 1);
    g_assert_cmpstr (((git_change_t *) g_ptr_array_index (staged, 0))->path, ==, "c.txt");
    g_assert_cmpint (((git_change_t *) g_ptr_array_index (staged, 0))->code, ==, 'A');
    g_assert_cmpuint (unstaged->len, ==, 1);
    g_assert_cmpstr (((git_change_t *) g_ptr_array_index (unstaged, 0))->path, ==, "a.txt");

    // a commit that is not there
    g_assert_false (
        git_reword (top, "0123456789abcdef0123456789abcdef01234567", "x\n", NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    git_branch_clear (&branch);
    g_ptr_array_free (staged, TRUE);
    g_ptr_array_free (unstaged, TRUE);
    g_ptr_array_free (commits, TRUE);
    g_free (new_head);
    g_free (tree);
    g_free (sub);
}

/* --------------------------------------------------------------------------------------------- */

static void
remove_tree (const char *dir)
{
    GDir *d = g_dir_open (dir, 0, NULL);
    const char *name;

    while (d != NULL && (name = g_dir_read_name (d)) != NULL)
    {
        char *path = g_build_filename (dir, name, (char *) NULL);

        if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
            remove_tree (path);
        else
            (void) g_unlink (path);
        g_free (path);
    }
    if (d != NULL)
        g_dir_close (d);
    (void) g_rmdir (dir);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    char *git_path;
    int result;

    g_test_init (&argc, &argv, NULL);
    top = g_dir_make_tmp ("git_core_XXXXXX", NULL);
    g_assert_nonnull (top);
    // git of its own: no configuration of the user, a name for the commits
    g_setenv ("HOME", top, TRUE);
    g_setenv ("XDG_CONFIG_HOME", top, TRUE);
    g_setenv ("GIT_CONFIG_NOSYSTEM", "1", TRUE);
    g_setenv ("GIT_AUTHOR_NAME", "Tester", TRUE);
    g_setenv ("GIT_AUTHOR_EMAIL", "tester@example.org", TRUE);
    g_setenv ("GIT_COMMITTER_NAME", "Tester", TRUE);
    g_setenv ("GIT_COMMITTER_EMAIL", "tester@example.org", TRUE);

    g_test_add_func ("/git/strip", test_strip);
    g_test_add_func ("/git/graph", test_graph);
    g_test_add_func ("/git/trailer", test_trailer);
    g_test_add_func ("/git/replace", test_replace);
    g_test_add_func ("/git/check", test_check);
    g_test_add_func ("/git/menu", test_menu);
    g_test_add_func ("/git/parse_status", test_parse_status);
    g_test_add_func ("/git/parse_name_status", test_parse_name_status);
    git_path = g_find_program_in_path ("git");
    if (git_path != NULL)
        g_test_add_func ("/git/repository", test_repository);
    g_free (git_path);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    return result;
}
