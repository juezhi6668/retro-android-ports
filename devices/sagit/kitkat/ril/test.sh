#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/qmxd-test.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
# gcc 9.4's libasan on this WSL2 host faults inside its own SIGSEGV handling in
# about a third of runs; that report then loops ("AddressSanitizer:DEADLYSIGNAL")
# until the timeout or the disk is full. UBSan installs no signal handler and is
# deterministic here, so it gates the build and ASan stays an opt-in:
#   ASAN=1 sh devices/sagit/kitkat/ril/test.sh
asan=""
if [ "${ASAN:-0}" = 1 ]; then
    asan=-fsanitize=address
fi
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -fsanitize=undefined $asan -g "$src/test_qmxd.c" -o "$bin"
"$bin"
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -fsanitize=undefined $asan -g "$src/test_start_ril.c" -o "$bin"
"$bin"
