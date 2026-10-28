#!/usr/bin/env bash
# The gate on the net trees (rebased build: main + online-play patches 0087-0092), adapter off as single player.
source "$(dirname "$0")/net-env.sh"
unset PS2X_DEV9 PS2X_NET_HOSTS PS2X_SAVES
export PS2X_GS="${PS2X_GS:-pcsx2-sw}"  # the gate's green runs use PCSX2's software renderer (reports: "gate 3/3 pcsx2-sw")
bash "$(dirname "$0")/../../tools/ob.sh" gate "$@"
