#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/nsfb-input.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
# Linux evdev headers and the native libevdev development package are required.
${HOST_CC:-cc} -std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror \
    ${TEST_CFLAGS:--O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer} \
    -I"$source_dir/include" -I"$source_dir/src" \
    $(pkg-config --cflags libevdev) \
    "$source_dir/test/remarkable-input.c" \
    "$source_dir/src/surface/remarkable/ringbuf.c" \
    $(pkg-config --libs libevdev) -pthread -o "$test_dir/remarkable-input"
"$test_dir/remarkable-input"
