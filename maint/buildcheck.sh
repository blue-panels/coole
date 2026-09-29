#!/bin/sh
#
# Run what .github/workflows/ci-ubuntu.yml runs, in a container, on this machine.
#
# Usage:
#   maint/buildcheck.sh [ref]
#
# Environment:
#   BUILDCHECK_IMAGE        image of the build job (default: ubuntu:24.04, what
#                           ubuntu-latest is)
#   BUILDCHECK_JOBS         parallel build jobs (default: all cores)
#
# The build runs as an unprivileged user, as it does on the CI runner.

set -eu

REF=${1:-HEAD}
IMAGE=${BUILDCHECK_IMAGE:-ubuntu:24.04}
NAME=coole-buildcheck-$$

srcdir=$(cd "$(dirname "$0")/.." && pwd)
test -d "$srcdir/.git" || { echo "buildcheck: $srcdir is not a git checkout" >&2; exit 1; }
command -v docker >/dev/null || { echo "buildcheck: docker not found" >&2; exit 1; }

sha=$(git -C "$srcdir" rev-parse "$REF")
echo "buildcheck: $IMAGE, $(git -C "$srcdir" log --oneline -1 "$sha")"

cleanup() { docker rm -f "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

fail=0

docker run -d --name "$NAME" -v "$srcdir":/repo:ro "$IMAGE" sleep infinity >/dev/null

docker exec -i -e SHA="$sha" -e JOBS="${BUILDCHECK_JOBS:-}" "$NAME" sh -s <<'INNER' || fail=1
set -eu

JOBS=${JOBS:-}; test -n "$JOBS" || JOBS=$(nproc)
inner_fail=0
step() { printf '\n=== %s\n' "$1"; }
ok() { printf 'PASS %s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1"; inner_fail=1; }

export DEBIAN_FRONTEND=noninteractive
step "install dependencies"
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends \
    git meson ninja-build build-essential gettext pkg-config check xz-utils \
    liblua5.4-dev libglib2.0-dev libgpm-dev libncurses-dev libslang2-dev libx11-dev >/dev/null 2>&1 || { bad "dependencies"; exit 1; }

id -u build >/dev/null 2>&1 || useradd -m build
git config --global --add safe.directory '*'
rm -rf /work
git clone -q /repo /work
cd /work
git checkout -q "$SHA"
chown -R build /work

as_build() { su build -c "cd $1 && $2"; }

configuration() {
    name=$1; shift
    step "configuration: $name"
    rm -rf "/work/build-$name"
    if ! as_build /work "meson setup build-$name --prefix=/work/build-$name/install $* >/tmp/cf-$name.log 2>&1"; then
        bad "$name: setup"; tail -8 "/tmp/cf-$name.log"; return
    fi
    if ! as_build /work "ninja -C build-$name -j$JOBS >/tmp/mk-$name.log 2>&1"; then
        bad "$name: build"; grep -E 'error:' "/tmp/mk-$name.log" | head -8; return
    fi
    if ! as_build /work "meson test -C build-$name >/tmp/ck-$name.log 2>&1"; then
        bad "$name: tests"; grep -E ' (FAIL|ERROR|TIMEOUT) ' "/tmp/ck-$name.log" | head -8; return
    fi
    ok "$name"
}

configuration full -Dlua=enabled -Dwerror=true
as_build /work "meson install -C build-full >/tmp/inst-full.log 2>&1" && ok "install" || bad "install"

configuration ncurses -Dscreen=ncurses -Dwerror=true

configuration release --buildtype=release -Dwerror=true

configuration minimal -Dnls=disabled -Dassert=false -Dlua=disabled -Dx11=disabled \
    -Dgpm=disabled -Dspell=disabled -Dtests=enabled -Dwerror=true

step "distribution archive"
if as_build /work "meson dist -C build-full --formats=xztar --no-tests >/tmp/dist.log 2>&1"; then
    ok "distribution archive"
else
    bad "distribution archive"; tail -5 /tmp/dist.log
fi

exit "$inner_fail"
INNER

printf '\n=== result\n'
test "$fail" -eq 0 && echo "buildcheck: all green" || echo "buildcheck: something failed"
exit "$fail"
