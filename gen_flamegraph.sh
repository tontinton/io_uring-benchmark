#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
perf script | "$SCRIPT_DIR/FlameGraph/stackcollapse-perf.pl" | "$SCRIPT_DIR/FlameGraph/flamegraph.pl" > out.svg
