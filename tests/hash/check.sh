#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

node "$ROOT_DIR/run_tests.js" --build-dir "$BUILD_DIR" "$@"
