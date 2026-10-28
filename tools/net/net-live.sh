#!/usr/bin/env bash
# First real-game test: hidden boot, network adapter on, the game's server names -> 127.0.0.2 (ob_server.exe there),
# live pad on \\.\pipe\obnet, shots in C:\ob\frames-net-live.
source "$(dirname "$0")/net-env.sh"
export PS2X_DEV9=pcsx2 PS2X_NET_HOSTS="www01.kddi-mmbb.jp=127.0.0.2;gate1.jp.dnas.playstation.org=127.0.0.2"
# Network play needs a memory card (network settings live there): a formatted card kept for net runs.
export PS2X_SAVES='C:\ob\saves-net'
# The game's network settings served by the client (patch 0097): no settings on the card are needed.
export PS2X_NETCNF="${PS2X_NETCNF:-dhcp}"
bash "$(dirname "$0")/../../tools/ob.sh" boot --pad obnet --secs ${1:-2400} --tag net-live
