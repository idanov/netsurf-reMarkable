#!/bin/sh
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/nsfb-bitmap.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

# Compile the actual RGB565 plotter and helpers with the host compiler.
# TEST_CFLAGS is intentionally word-split to allow extra compiler flags.
${HOST_CC:-cc} -std=c99 -Wall -Wextra ${TEST_CFLAGS:--O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer} \
    -I"$source_dir/include" -I"$source_dir/src" \
    "$source_dir/test/bitmap-scaling.c" \
    "$source_dir/src/plot/16bpp.c" \
    "$source_dir/src/plot/util.c" \
    "$source_dir/src/palette.c" \
    -o "$test_dir/bitmap-scaling"
"$test_dir/bitmap-scaling"
