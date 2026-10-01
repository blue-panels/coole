#!/bin/sh
# A plugin module calls the functions of the program.  The program exports
# them, but only those its own code keeps: check that every symbol the module needs is there, in
# the program, in a library the program loads, or in one the module loads itself: openpty() is in
# libutil before glibc 2.34, and the module links libutil then.
#
# module_symbols.sh <program> <module>

set -e

program=$1
module=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# what the module needs; weak symbols may stay unresolved
nm -D --undefined-only "$module" | awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u >"$tmp/needed"

# what the program, its libraries and the libraries of the module give
{
    nm -D --defined-only "$program" | awk '{ print $3 }'
    {
        ldd "$program"
        ldd "$module"
    } | awk '$3 ~ /^\// { print $3 } $1 ~ /^\// { print $1 }' | sort -u | while read -r lib; do
        nm -D --defined-only "$lib" | awk '{ sub(/@.*/, "", $3); print $3 }'
    done
} | sort -u >"$tmp/given"

missing=$(comm -23 "$tmp/needed" "$tmp/given")
if [ -n "$missing" ]; then
    echo "the program does not give the module:"
    echo "$missing"
    exit 1
fi
