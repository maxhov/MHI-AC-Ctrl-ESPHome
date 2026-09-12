#!/bin/sh
# Compile and run the host-side core tests. No ESPHome toolchain needed.
set -e
dir=$(dirname "$0")
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
c++ -std=c++17 -Wall -O1 -I "$dir/stub" \
    "$dir/test_core.cpp" "$dir/../components/MhiAcCtrl/MHI-AC-Ctrl-core.cpp" \
    -o "$out/test_core"
"$out/test_core"
