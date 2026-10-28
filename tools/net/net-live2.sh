#!/usr/bin/env bash
# A second client on the same PC beside tools/net/net-live.sh (report entry 50): its own card folder
# ($PS2X_SAVES, default C:\ob\saves-net2), live pad on \\.\pipe\obnet2, logs in C:\ob\net-logs2, shots in
# C:\ob\frames-net-live2. It reaches ob_server on loopback as 127.0.0.2 (PS2X_NET_REMAP "@source", upstream/pcsx2
# 0004), so the server, which tells players apart by address, sees a second player.
source "$(dirname "$0")/net-env.sh"
export OB_LOGS=C:/ob/net-logs2
mkdir -p "$OB_LOGS"
export PS2X_DEV9=pcsx2 PS2X_NET_HOSTS="www01.kddi-mmbb.jp=192.0.2.2;gate1.jp.dnas.playstation.org=192.0.2.2"
export PS2X_NET_REMAP="192.0.2.2=127.0.0.1@127.0.0.2"
export PS2X_SAVES="${PS2X_SAVES:-C:\\ob\\saves-net2}"
export PS2X_NETCNF="${PS2X_NETCNF:-dhcp}"
bash "$(dirname "$0")/../../tools/ob.sh" boot --pad obnet2 --secs ${1:-2400} --tag net-live2
