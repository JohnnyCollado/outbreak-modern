#!/usr/bin/env bash
# tools/net/two-clients-to-room.sh [shot prefix]: drives two running clients (tools/net/net-live.sh, net-live2.sh;
# report entries 50-52) (pipes obnet, obnet2) from boot to: client 1 hosting a 零下 room, client 2 joined and
# ENTER pressed. Shots under C:\ob\frames-net-live{,2} with prefix $1 (default z).
W=$(cd "$(dirname "$0")/../.." && pwd)
P="python -I $W/tools/ob/pad.py"
T=${1:-z}
( $P --pipe obnet --wait 150 --timeout 400 < $W/tools/net/to-network.pad > /c/ob/net-logs/pad1.txt 2>&1 &
  $P --pipe obnet2 --wait 150 --timeout 400 < $W/tools/net/to-network.pad > /c/ob/net-logs2/pad2.txt 2>&1 & wait )
# Connect, skip registration, type あ, wait, cursor to 決定.
C='press down 8\nwait 60\npress down 8\nwait 60\npress down 8\nwait 60\npress circle 8\nwait 1800\npress right 8\nwait 30\npress circle 8\nwait 400\npress circle 8\nwait 420\n'
for i in 1 2 3; do C="${C}press down 8\nwait 40\n"; done
for i in $(seq 1 16); do C="${C}press right 8\nwait 30\n"; done
C="${C}shot ${T}01-cursor\n"
( printf "$C" | $P --pipe obnet --wait 5 --timeout 400 > /c/ob/net-logs/pad1.txt 2>&1 &
  printf "$C" | $P --pipe obnet2 --wait 5 --timeout 400 > /c/ob/net-logs2/pad2.txt 2>&1 & wait )
# 決定 twice (commit, confirm), 登録する; then the host makes a 零下 room, the guest waits, joins, ENTER.
N='press circle 8\nwait 120\npress circle 8\nwait 240\npress left 6\nwait 20\npress circle 8\nwait 400\n'
H="${N}press circle 8\nwait 300\npress circle 8\nwait 300\npress circle 8\nwait 400\n"
for i in 1 2 3 4 5 6; do H="${H}press down 8\nwait 30\n"; done
H="${H}press right 8\nwait 90\n"
for i in 1 2 3; do H="${H}press down 8\nwait 30\n"; done
H="${H}press circle 8\nwait 400\nshot ${T}02-host-room\n"
J="${N}press circle 8\nwait 300\npress circle 8\nwait 1800\nshot ${T}02-join-rooms\npress circle 8\nwait 400\nshot ${T}03-member\npress down 8\nwait 30\npress circle 8\nwait 600\nshot ${T}04-after-enter\n"
( printf "$H" | $P --pipe obnet --wait 5 --timeout 400 > /c/ob/net-logs/pad1.txt 2>&1 &
  printf "$J" | $P --pipe obnet2 --wait 5 --timeout 400 > /c/ob/net-logs2/pad2.txt 2>&1 & wait )
tail -n1 /c/ob/net-logs/pad1.txt /c/ob/net-logs2/pad2.txt 2>/dev/null || { tail -n1 /c/ob/net-logs/pad1.txt; tail -n1 /c/ob/net-logs2/pad2.txt; }
