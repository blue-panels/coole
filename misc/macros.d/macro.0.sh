m       make
        TMPFILE=`mktemp ${BLUERIG_TMPDIR:-/tmp}/up.XXXXXX` || exit 1
        make 2> $TMPFILE
        bluerig $TMPFILE
        rm $TMPFILE
