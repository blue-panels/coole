m       make
        TMPFILE=`mktemp ${COOLE_TMPDIR:-/tmp}/up.XXXXXX` || exit 1
        make 2> $TMPFILE
        coole $TMPFILE
        rm $TMPFILE
