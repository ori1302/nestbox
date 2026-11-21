#!/bin/sh
# Configures, builds and runs the tests with CTest.
#   scripts/test_all.sh            every test (about 5 minutes: the network tests run a real simulator)
#   scripts/test_all.sh --quick    only the quick ones (about a minute)
# NESTBOX_TEST_PETS=<folder> adds that folder's .egg files to the eggs the tests check.
# NESTBOX_BUILD_DIR=<dir> builds there instead of build/.
set -e
cd "$(dirname "$0")/.."
build="${NESTBOX_BUILD_DIR:-build}"
label=""
[ "${1:-}" = "--quick" ] && label="-L quick"
cmake -S . -B "$build" -DNESTBOX_TEST_PETS="${NESTBOX_TEST_PETS:-}" > /dev/null
cmake --build "$build" -j
cd "$build"
# shellcheck disable=SC2086
ctest $label -j 4 --output-on-failure
