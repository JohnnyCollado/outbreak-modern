#!/usr/bin/env bash
# S2 boot check: hidden boots of the net runner with the adapter off and on; what the disc's dev9.irx says.
source "$(dirname "$0")/net-env.sh"
OB="$(dirname "$0")/../../tools/ob.sh"
PS2X_DEV9=off bash "$OB" boot --secs ${1:-60} --tag dev9-off --hidden
PS2X_DEV9=pcsx2 PS2X_NET_HOSTS="gate1.jp.dnas.playstation.org=127.0.0.1;www01.kddi-mmbb.jp=127.0.0.1" \
    bash "$OB" boot --secs ${1:-60} --tag dev9-on --hidden
for t in off on; do
    echo "== dev9-$t"
    grep -a -i -E "dev9|smap|\[net|unknown dev9|speed" "$OB_LOGS/boot-dev9-$t.log" | head -12
    grep -a -c -E "\[iop\].*irq 13|intr 13" "$OB_LOGS/boot-dev9-$t.log" | sed 's/^/irq13 lines: /'
done
