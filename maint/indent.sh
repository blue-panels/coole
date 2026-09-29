#!/bin/sh
# Format the C sources with clang-format: "meson compile -C <builddir> indent".
set -e
cd "${1:-${MESON_SOURCE_ROOT:-.}}"
git ls-files -z -- 'lib/*.[ch]' 'src/*.[ch]' 'tests/*.[ch]' | xargs -0 clang-format -i
