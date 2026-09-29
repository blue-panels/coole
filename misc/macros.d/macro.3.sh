#silent
S       Sort selection
        TMPFILE=`mktemp ${COOLE_TMPDIR:-/tmp}/up.XXXXXX` || exit 1
        cat %b > $TMPFILE
        cat $TMPFILE| sort >%b
        rm -f $TMPFILE
