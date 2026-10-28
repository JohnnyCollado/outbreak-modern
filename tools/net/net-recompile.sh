#!/usr/bin/env bash
# Baseline: main ELF + all overlays (with the online ones) recompiled and the runner built in the net-* trees.
source "$(dirname "$0")/net-env.sh"
bash "$(dirname "$0")/../../tools/ob.sh" recompile
