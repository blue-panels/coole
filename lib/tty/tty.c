/*
   Interface to the terminal controlling library.

   Copyright (C) 2005-2026
   Free Software Foundation, Inc.
   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Roland Illig <roland.illig@gmx.de>, 2005.
   Andrew Borodin <aborodin@vmail.ru>, 2009.
   Ilia Maslakov <il.smind@gmail.com>, 2011, 2026.

   This file is part of coole,
   a text editor based on GNU Midnight Commander.

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

/** \file tty.c
 *  \brief Source: %interface to the terminal controlling library
 */

#include <config.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>  // memset()

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#endif
#include <unistd.h>  // exit()

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

/* In some systems (like Solaris 11.4 SPARC), TIOCSWINSZ is defined in termios.h */
#include <termios.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/util.h"

#include "tty.h"
#include "tty-internal.h"
#include "color.h"  // tty_set_normal_attrs()
#include "mouse.h"  // use_mouse_p
#include "win.h"

/*** global variables ****************************************************************************/

mc_tty_char_t mc_tty_frm[MC_TTY_FRM_MAX];

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static SIG_ATOMIC_VOLATILE_T got_interrupt = 0;

typedef struct
{
    tty_painter_fn fn;
    void *data;
} tty_painter_t;

static GSList *painters = NULL;
static gboolean painting = FALSE;

static gboolean has_kitty_keyboard = FALSE;
static gboolean has_win32_input = FALSE;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
sigintr_handler (int signo)
{
    (void) &signo;
    got_interrupt = 1;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
tty_run_painters (void)
{
    GSList *l;

    /* A painter that refreshes would run the painters again. */
    if (painting)
        return;

    painting = TRUE;
    for (l = painters; l != NULL; l = g_slist_next (l))
    {
        const tty_painter_t *p = l->data;

        p->fn (p->data);
    }
    painting = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
tty_painter_add (tty_painter_fn fn, void *data)
{
    tty_painter_t *p = g_new (tty_painter_t, 1);

    p->fn = fn;
    p->data = data;
    painters = g_slist_append (painters, p);
}

/* --------------------------------------------------------------------------------------------- */

void
tty_painter_remove (tty_painter_fn fn, void *data)
{
    GSList *l;

    for (l = painters; l != NULL; l = g_slist_next (l))
    {
        tty_painter_t *p = l->data;

        if (p->fn == fn && p->data == data)
        {
            painters = g_slist_delete_link (painters, l);
            g_free (p);
            return;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

void
tty_raw_write (const char *data, size_t len)
{
    while (len > 0)
    {
        ssize_t n = write (STDOUT_FILENO, data, len);

        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return;
        }
        data += n;
        len -= (size_t) n;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The answers: CSI ? <flags> u, the kitty keyboard protocol, CSI ? 9001 ; <state> $ y,
   the Win32 input mode, and the DA1 one,
   CSI ? <attributes> c, which every terminal gives and gives last. The sequences
   are taken out of the buffer as they are found, and what is left is the user's. */
static void
tty_parse_terminal_reply (char *buf, size_t *len, gboolean *da1_seen)
{
    size_t i = 0;

    while (i + 2 < *len)
    {
        const char *p = buf + i + 2;
        const char *end = buf + *len;
        size_t taken = 0;

        if (buf[i] != ESC_CHAR || buf[i + 1] != '[')
        {
            i++;
            continue;
        }

        if (*p == '?')
        {
            int n = 0, first = -1;
            gboolean have = FALSE;

            for (p++; p < end; p++)
            {
                if (*p >= '0' && *p <= '9')
                {
                    n = n * 10 + (*p - '0');
                    have = TRUE;
                }
                else if (*p == 'u' && have)
                {
                    /* CSI ? <flags> u: the kitty keyboard protocol is known */
                    has_kitty_keyboard = TRUE;
                    taken = (size_t) (p + 1 - (buf + i));
                    break;
                }
                else if (*p == '$' && have && first >= 0)
                {
                    /* CSI ? <mode> ; <state> $ y: the state of a mode: 0 unknown, 1 set, 2 reset,
                       3 permanently set, 4 permanently reset (it cannot be turned on) */
                    if (p + 1 >= end)
                        break;  // still arriving
                    if (p[1] != 'y')
                        break;
                    if (first == 9001 && n >= 1 && n <= 3)
                        has_win32_input = TRUE;
                    taken = (size_t) (p + 2 - (buf + i));
                    break;
                }
                else if (*p == ';' || *p == 'c')
                {
                    if (first < 0 && have)
                        first = n;
                    n = 0;
                    have = FALSE;
                    if (*p == 'c')
                    {
                        *da1_seen = TRUE;
                        taken = (size_t) (p + 1 - (buf + i));
                        break;
                    }
                }
                else
                    break;
            }
        }

        if (taken == 0)
        {
            i++;
            continue;
        }
        memmove (buf + i, buf + i + taken, *len - i - taken);
        *len -= taken;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
tty_probe_terminal (void)
{
    const char *kitty_env = getenv ("COOLE_KITTY_KEYBOARD");
    const char *win32_env = getenv ("COOLE_WIN32_INPUT");
    const char *term = getenv ("TERM");
    char buf[512];
    size_t len = 0;
    gboolean da1_seen = FALSE;
    gboolean multiplexer =
        term != NULL && (strncmp (term, "screen", 6) == 0 || strncmp (term, "tmux", 4) == 0);
    gboolean ask_kitty = !multiplexer && (kitty_env == NULL || kitty_env[0] != '0');
    gboolean ask_win32 = !multiplexer && (win32_env == NULL || win32_env[0] != '0');
    int waited_ms = 0;

    has_kitty_keyboard = FALSE;
    has_win32_input = FALSE;

    if ((!ask_kitty && !ask_win32) || !isatty (STDIN_FILENO) || !isatty (STDOUT_FILENO))
        return;

    {
        /* Every terminal answers DA1, and answers in the order it is asked: DA1 goes
           last, so its answer comes after all the others. The kitty keyboard query and the
           DECRQM question about mode 9001 (Win32 input mode, Windows Terminal) go first. */
        static const char kitty_query[] = ESC_STR "[?u";
        static const char win32_query[] = ESC_STR "[?9001$p";
        static const char query[] = ESC_STR "[c";

        if (ask_kitty)
            tty_raw_write (kitty_query, sizeof (kitty_query) - 1);
        if (ask_win32)
            tty_raw_write (win32_query, sizeof (win32_query) - 1);
        tty_raw_write (query, sizeof (query) - 1);

        /* A terminal that answers nothing costs the whole wait once, at startup. */
        while (len < sizeof (buf) - 1 && !da1_seen && waited_ms < 1000)
        {
            fd_set fds;
            struct timeval tv = { 0, 50000 };
            ssize_t n;

            FD_ZERO (&fds);
            FD_SET (STDIN_FILENO, &fds);
            if (select (STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
            {
                waited_ms += 50;
                continue;
            }
            n = read (STDIN_FILENO, buf + len, sizeof (buf) - 1 - len);
            if (n <= 0)
                break;
            len += (size_t) n;
            tty_parse_terminal_reply (buf, &len, &da1_seen);
        }

        /* Whatever else came in was typed: it goes back to the keyboard. */
        if (len > 0)
            tty_unget_input ((const unsigned char *) buf, len);
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
tty_has_sixel (void)
{
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
tty_has_kitty_keyboard (void)
{
    return has_kitty_keyboard;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
tty_has_win32_input (void)
{
    return has_win32_input;
}

/* --------------------------------------------------------------------------------------------- */

void
tty_cell_size (int *width, int *height)
{
    *width = 0;
    *height = 0;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Check terminal type. If $TERM is not set or value is empty, coole finishes with EXIT_FAILURE.
 *
 * @param force_xterm Set forced the XTerm type
 *
 * @return true if @param force_xterm is true or value of $TERM is one of following:
 *         alacritty*
 *         contour*
 *         dtterm
 *         Eterm
 *         foot*
 *         konsole*
 *         rxvt*
 *         screen*
 *         term*
 *         tmux*
 */
gboolean
tty_check_xterm_compat (const gboolean force_xterm)
{
    static const char *xterm_compatible_terminals[] = {
        "alacritty",  //
        "contour",    //
        "dtterm",     //
        "Eterm",      //
        "foot",       //
        "konsole",    //
        "rxvt",       //
        "screen",     //
        "tmux",       //
        "xterm",      //
        NULL,
    };

    const char *termvalue = getenv ("TERM");
    if (termvalue == NULL || *termvalue == '\0')
    {
        fputs (_ ("The TERM environment variable is unset!\n"), stderr);
        my_exit (EXIT_FAILURE);
    }

    if (force_xterm)
        return TRUE;

    for (const char **p = xterm_compatible_terminals; *p != NULL; p++)
        if (strncmp (termvalue, *p, strlen (*p)) == 0)
            return TRUE;

    return FALSE;
}
/* --------------------------------------------------------------------------------------------- */

extern void
tty_start_interrupt_key (void)
{
    struct sigaction act;

    memset (&act, 0, sizeof (act));
    act.sa_handler = sigintr_handler;
    sigemptyset (&act.sa_mask);
#ifdef SA_RESTART
    act.sa_flags = SA_RESTART;
#endif
    my_sigaction (SIGINT, &act, NULL);
}

/* --------------------------------------------------------------------------------------------- */

extern void
tty_enable_interrupt_key (void)
{
    struct sigaction act;

    memset (&act, 0, sizeof (act));
    act.sa_handler = sigintr_handler;
    sigemptyset (&act.sa_mask);
    my_sigaction (SIGINT, &act, NULL);
    got_interrupt = 0;
}

/* --------------------------------------------------------------------------------------------- */

extern void
tty_disable_interrupt_key (void)
{
    struct sigaction act;

    memset (&act, 0, sizeof (act));
    act.sa_handler = SIG_IGN;
    sigemptyset (&act.sa_mask);
    my_sigaction (SIGINT, &act, NULL);
}

/* --------------------------------------------------------------------------------------------- */

extern gboolean
tty_got_interrupt (void)
{
    gboolean rv;

    rv = (got_interrupt != 0);
    got_interrupt = 0;
    return rv;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
tty_got_winch (void)
{
    fd_set fdset;
    // instant timeout
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
    int ok;

    FD_ZERO (&fdset);
    FD_SET (sigwinch_pipe[0], &fdset);

    while ((ok = select (sigwinch_pipe[0] + 1, &fdset, NULL, NULL, &timeout)) < 0)
        if (errno != EINTR)
        {
            perror (_ ("Cannot check SIGWINCH pipe"));
            exit (EXIT_FAILURE);
        }

    return (ok != 0 && FD_ISSET (sigwinch_pipe[0], &fdset));
}

/* --------------------------------------------------------------------------------------------- */

gboolean
tty_flush_winch (void)
{
    ssize_t n;
    gboolean ret = FALSE;

    // merge all SIGWINCH events raised to this moment
    do
    {
        char x[16];

        // read multiple events at a time
        n = read (sigwinch_pipe[0], &x, sizeof (x));

        // at least one SIGWINCH came
        if (n > 0)
            ret = TRUE;
    }
    while (n > 0 || (n == -1 && errno == EINTR));

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

void
tty_print_one_hline (gboolean single)
{
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_HORIZ : MC_TTY_FRM_DHORIZ]);
}

/* --------------------------------------------------------------------------------------------- */

void
tty_print_one_vline (gboolean single)
{
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_VERT : MC_TTY_FRM_DVERT]);
}

/* --------------------------------------------------------------------------------------------- */

void
tty_draw_box (int y, int x, int ys, int xs, gboolean single)
{
    int y2, x2;

    if (ys <= 0 || xs <= 0)
        return;

    ys--;
    xs--;

    y2 = y + ys;
    x2 = x + xs;

    tty_draw_vline (y, x, mc_tty_frm[single ? MC_TTY_FRM_VERT : MC_TTY_FRM_DVERT], ys);
    tty_draw_vline (y, x2, mc_tty_frm[single ? MC_TTY_FRM_VERT : MC_TTY_FRM_DVERT], ys);
    tty_draw_hline (y, x, mc_tty_frm[single ? MC_TTY_FRM_HORIZ : MC_TTY_FRM_DHORIZ], xs);
    tty_draw_hline (y2, x, mc_tty_frm[single ? MC_TTY_FRM_HORIZ : MC_TTY_FRM_DHORIZ], xs);
    tty_gotoyx (y, x);
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_LEFTTOP : MC_TTY_FRM_DLEFTTOP]);
    tty_gotoyx (y2, x);
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_LEFTBOTTOM : MC_TTY_FRM_DLEFTBOTTOM]);
    tty_gotoyx (y, x2);
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_RIGHTTOP : MC_TTY_FRM_DRIGHTTOP]);
    tty_gotoyx (y2, x2);
    tty_print_char (mc_tty_frm[single ? MC_TTY_FRM_RIGHTBOTTOM : MC_TTY_FRM_DRIGHTBOTTOM]);
}

/* --------------------------------------------------------------------------------------------- */

void
tty_draw_box_shadow (int y, int x, int rows, int cols, int shadow_color)
{
    // draw right shadow
    tty_colorize_area (y + 1, x + cols, rows - 1, 2, shadow_color);
    // draw bottom shadow
    tty_colorize_area (y + rows, x + 2, 1, cols, shadow_color);
}

/* --------------------------------------------------------------------------------------------- */

char *
mc_tty_normalize_from_utf8 (const char *str)
{
    GIConv conv;
    GString *buffer;
    const char *_system_codepage = str_detect_termencoding ();

    if (str_isutf8 (_system_codepage))
        return g_strdup (str);

    conv = g_iconv_open (_system_codepage, "UTF-8");
    if (conv == INVALID_CONV)
        return g_strdup (str);

    buffer = g_string_new ("");

    if (str_convert (conv, str, buffer) == ESTR_FAILURE)
    {
        g_string_free (buffer, TRUE);
        str_close_conv (conv);
        return g_strdup (str);
    }
    str_close_conv (conv);

    return g_string_free (buffer, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/** Resize given terminal using TIOCSWINSZ, return ioctl() result */
int
tty_resize (int fd)
{
#if defined TIOCSWINSZ
    struct winsize tty_size;

    /* Make sure to copy to the inner terminal all the fields, even the ones we don't care about,
     * including ws_xpixel and ws_ypixel. */
    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &tty_size) == -1)
        return -1;
    return ioctl (fd, TIOCSWINSZ, &tty_size);
#else
    return 0;
#endif
}

/* --------------------------------------------------------------------------------------------- */

/** Clear screen */
void
tty_clear_screen (void)
{
    tty_set_normal_attrs ();
    tty_fill_region (0, 0, LINES, COLS, ' ');
    tty_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

void
tty_init_xterm_support (gboolean is_xterm)
{
    const char *termvalue;

    termvalue = getenv ("TERM");

    // Check mouse and ca capabilities
    /* terminfo/termcap structures have been already initialized,
       in slang_init() or/and init_curses()  */
    xmouse_seq = tty_tigetstr ("kmous", "Km");
    smcup = tty_tigetstr ("smcup", "ti");
    rmcup = tty_tigetstr ("rmcup", "te");

    if (strcmp (termvalue, "cygwin") == 0)
    {
        is_xterm = TRUE;
        use_mouse_p = MOUSE_DISABLED;
    }

    if (is_xterm)
    {
        // Default to the standard xterm sequence
        if (xmouse_seq == NULL)
            xmouse_seq = ESC_STR "[M";

        // Enable mouse unless explicitly disabled by --nomouse
        if (use_mouse_p != MOUSE_DISABLED)
        {
            if (mc_global.tty.old_mouse)
                use_mouse_p = MOUSE_XTERM_NORMAL_TRACKING;
            else
            {
                // FIXME: this dirty hack to set supported type of tracking the mouse
                const char *color_term = getenv ("COLORTERM");
                if (strncmp (termvalue, "rxvt", 4) == 0
                    || (color_term != NULL && strncmp (color_term, "rxvt", 4) == 0)
                    || strcmp (termvalue, "Eterm") == 0)
                    use_mouse_p = MOUSE_XTERM_NORMAL_TRACKING;
                else
                    use_mouse_p = MOUSE_XTERM_BUTTON_EVENT_TRACKING;
            }
        }
    }

    /* There's only one termcap entry "kmous", typically containing "\E[M" or "\E[<".
     * We need the former in xmouse_seq, the latter in xmouse_extended_seq.
     * See tickets 2956, 3954, and 4063 for details. */
    if (xmouse_seq != NULL)
    {
        if (strcmp (xmouse_seq, ESC_STR "[<") == 0)
        {
            xmouse_seq = ESC_STR "[M";
            ncurses_key_mouse_means_extended = TRUE;
        }

        xmouse_extended_seq = ESC_STR "[<";
    }
}

/* --------------------------------------------------------------------------------------------- */
