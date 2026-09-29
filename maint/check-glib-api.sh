#!/bin/sh
#
# coole - check the GLib 2.58 API baseline.
#
# Copyright (C) 2026
# Ilia Maslakov <il.smind@gmail.com>
#
# This file is part of coole.
#
# coole is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# coole is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# Usage: sh maint/check-glib-api.sh [meson options...]
# Requires Docker. GLIB_API_CHECK_JOBS sets parallelism (default: 4).
# Logs and build files are kept in the printed temporary directory.

set -eu

srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
builddir=$(mktemp -d "${TMPDIR:-/tmp}/coole-glib-api.XXXXXX")
printf 'Checking against GLib 2.58; build and logs: %s\n' "$builddir"

# The image is described here rather than in a Dockerfile of the tree: it
# needs no build context, so docker reads it from standard input.
if ! docker build -t coole-glib-api-258 - > "$builddir/image.log" 2>&1 <<'DOCKERFILE'
# Debian 10 provides the project's minimum GLib, 2.58.3.
FROM debian@sha256:bb3dc79fddbca7e8903248ab916bb775c96ec61014b3d02b4f06043b604726dc

RUN printf 'deb http://archive.debian.org/debian buster main\ndeb http://archive.debian.org/debian-security buster/updates main\n' > /etc/apt/sources.list \
    && apt-get -o Acquire::Check-Valid-Until=false update \
    && apt-get install -y --no-install-recommends \
        build-essential pkg-config gettext python3-pip \
        libglib2.0-dev libncursesw5-dev libslang2-dev perl \
    && rm -rf /var/lib/apt/lists/*

# The meson of Debian 10 is older than the build needs; the Python there runs a newer one.
RUN pip3 install --no-cache-dir 'meson==1.3.2' 'ninja==1.11.1.1'

# Keep the test framework current while GLib stays at the supported minimum.
ADD https://deb.debian.org/debian/pool/main/c/check/check_0.15.2.orig.tar.gz /tmp/check.tar.gz
RUN echo '8451b68ac5d6f3157b24f22eceff575bcf566264f6d78f3852f89d4e08cf42e1  /tmp/check.tar.gz' | sha256sum -c - \
    && tar -xzf /tmp/check.tar.gz -C /tmp \
    && cd /tmp/check-0.15.2 \
    && ./configure --disable-static \
    && make -j4 \
    && make install \
    && ldconfig \
    && rm -rf /tmp/check-0.15.2 /tmp/check.tar.gz

ENV LANG=C.UTF-8
DOCKERFILE
then
    tail -n 60 "$builddir/image.log"
    exit 1
fi
docker run --rm --user "$(id -u):$(id -g)" \
    -e GLIB_API_CHECK_JOBS="${GLIB_API_CHECK_JOBS:-4}" \
    -v "$srcdir:/src:ro" -v "$builddir:/work" \
    coole-glib-api-258 sh -eu -c '
        version=$(pkg-config --modversion glib-2.0)
        case "$version" in
            2.58.*) printf "GLib: %s\n" "$version" ;;
            *) printf "Expected GLib 2.58, found %s\n" "$version" >&2; exit 1 ;;
        esac
        mkdir /work/src
        tar -C /src --exclude=.git --exclude=.claude --exclude=.agents --exclude=.codex \
            --exclude="./build*" -cf - . | tar -C /work/src -xf -
        if ! CFLAGS="-Werror=implicit-function-declaration" meson setup /work/build /work/src \
            -Dtests=enabled "$@" > /work/configure.log 2>&1; then
            tail -n 40 /work/configure.log; exit 1
        fi
        if ! ninja -C /work/build -j"$GLIB_API_CHECK_JOBS" > /work/build.log 2>&1; then
            tail -n 60 /work/build.log; exit 1
        fi
        if ! meson test -C /work/build --num-processes "$GLIB_API_CHECK_JOBS" --print-errorlogs \
            > /work/check.log 2>&1; then
            tail -n 60 /work/check.log; exit 1
        fi
        printf "PASS: build and tests with GLib %s.\n" "$version"
    ' sh "$@"
