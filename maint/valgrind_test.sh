#!/bin/sh
# Run one test under valgrind:
#   meson test -C <builddir> --wrap="$PWD/maint/valgrind_test.sh" --timeout-multiplier 20
# VALGRIND_LOG=<dir> puts the report of each process into that directory.

dir=$(dirname "$(readlink -f "$0")")

export CK_FORK=no G_DEBUG=gc-friendly TERM="${TERM:-xterm}"

# a shell or a script is not ours to check
case "$(basename "$1")" in
sh | bash | dash | python*) exec "$@" ;;
esac
[ "$(head -c 2 "$1")" = '#!' ] && exec "$@"

system=
for f in /usr/share/glib-2.0/valgrind/glib.supp /usr/lib/valgrind/ncurses.supp; do
    [ -r "$f" ] && system="$system --suppressions=$f"
done

exec valgrind --quiet --leak-check=full --show-leak-kinds=definite,indirect \
    --errors-for-leak-kinds=definite,indirect --error-exitcode=99 --num-callers=30 \
    --suppressions="$dir/valgrind.supp" $system \
    ${VALGRIND_LOG:+--log-file="$VALGRIND_LOG/%p.log"} "$@"
