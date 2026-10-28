#!/usr/bin/env bash
# First real-game test: hidden boot, network adapter on, the game's server names -> the alias 192.0.2.2, which the
# adapter maps to 127.0.0.1 where ob_server.exe listens by default (the game cannot use 127.x: report entry 34;
# upstream/pcsx2 0003); live pad on \\.\pipe\obnet, shots in C:\ob\frames-net-live.
source "$(dirname "$0")/net-env.sh"
export PS2X_DEV9=pcsx2 PS2X_NET_HOSTS="www01.kddi-mmbb.jp=192.0.2.2;gate1.jp.dnas.playstation.org=192.0.2.2"
export PS2X_NET_REMAP="192.0.2.2=127.0.0.1"
# Network play needs a memory card (network settings live there): a formatted card kept for net runs.
export PS2X_SAVES='C:\ob\saves-net'
# The game's network settings served by the client (patch 0097): no settings on the card are needed.
export PS2X_NETCNF="${PS2X_NETCNF:-dhcp}"
bash "$(dirname "$0")/../../tools/ob.sh" boot --pad obnet --secs ${1:-2400} --tag net-live
