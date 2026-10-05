#!/usr/bin/env bash
set -euo pipefail
LAB_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
SRC=tp-vf-src
DST=tp-vf-dst
STATE="$LAB_DIR/build/namespaces-owned"
exists() { ip netns list | awk -v n="$1" '$1 == n { found=1 } END { exit !found }'; }
[[ $EUID == 0 ]] || { echo 'Run with sudo.' >&2; exit 1; }
case "${1:-}" in
 up)
  if exists "$SRC" || exists "$DST" || ip link show tp-vf-a &>/dev/null || ip link show tp-vf-b &>/dev/null; then
   echo 'Lab names already in use; refusing to overwrite.' >&2; exit 1
  fi
  mkdir -p "$LAB_DIR/build"
  A=0; B=0
  rollback() { [[ $A == 0 ]] || ip netns del "$SRC"; [[ $B == 0 ]] || ip netns del "$DST"; }
  trap rollback ERR
  ip netns add "$SRC"; A=1
  ip netns add "$DST"; B=1
  ip link add tp-vf-a netns "$SRC" type veth peer name tp-vf-b netns "$DST"
  ip -n "$SRC" addr add 10.23.0.1/24 dev tp-vf-a
  ip -n "$DST" addr add 10.23.0.2/24 dev tp-vf-b
  ip -n "$SRC" link set lo up
  ip -n "$DST" link set lo up
  ip -n "$SRC" link set tp-vf-a up
  ip -n "$DST" link set tp-vf-b up
  printf 'tp-vf-src tp-vf-dst\n' > "$STATE"
  trap - ERR
  ;;
 down)
  [[ -f "$STATE" ]] || { echo 'No ownership marker; refusing cleanup.' >&2; exit 1; }
  for ns in "$SRC" "$DST"; do
   if exists "$ns" && [[ -n $(ip netns pids "$ns") ]]; then
    echo "Stop processes in $ns first." >&2; exit 1
   fi
  done
  if exists "$SRC"; then ip netns del "$SRC"; fi
  if exists "$DST"; then ip netns del "$DST"; fi
  rm -f "$STATE"
  ;;
 *) echo 'Usage: sudo bash scripts/lab.sh up|down' >&2; exit 2 ;;
esac
