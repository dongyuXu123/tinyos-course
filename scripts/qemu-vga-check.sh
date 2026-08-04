#!/bin/sh
# Reproducible QEMU VGA validator using the monitor and physical VGA memory.
# Usage: scripts/qemu-vga-check.sh <lesson-dir> <command> [command...]
set -eu
[ "$#" -ge 2 ] || { printf 'usage: %s <lesson-dir> <command> [command...]\n' "$0" >&2; exit 2; }
lesson=$1; shift
case "$lesson" in /*) dir=$lesson;; *) dir=$(cd "$lesson" && pwd);; esac
cd "$dir"
make clean
make -j"$(nproc)"
make check
run_dir=${QEMU_CHECK_DIR:-build/qemu-check}
rm -rf "$run_dir"; mkdir -p "$run_dir"
monitor="$run_dir/monitor.sock"; serial="$run_dir/serial.log"; vga="$run_dir/vga.bin"; trace="$run_dir/qemu-trace.log"
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom build/kernel.iso -display none \
  -serial "file:$serial" -monitor "unix:$monitor,server=on,wait=off" -no-reboot -no-shutdown \
  -d guest_errors -D "$trace" & qemu_pid=$!
cleanup() { if kill -0 "$qemu_pid" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$monitor" >/dev/null 2>&1 || true; kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true; fi; }
trap cleanup EXIT INT TERM
for _ in $(seq 1 100); do [ -S "$monitor" ] && break; sleep .1; done
[ -S "$monitor" ] || { printf 'QEMU monitor did not start\n' >&2; exit 1; }
sleep 1
monitor_cmd() { python3 - "$monitor" "$1" <<'PY'
import socket, sys
s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sys.argv[1]); s.recv(4096); s.sendall((sys.argv[2]+'\n').encode()); s.recv(4096); s.close()
PY
}
send_key() { monitor_cmd "sendkey $1"; }
for command in "$@"; do
  i=1; while [ "$i" -le "${#command}" ]; do
    ch=$(printf '%s' "$command" | cut -c "$i")
    case "$ch" in ' ') key=spc;; '-') key=minus;; '_') key=shift-minus;; '/') key=slash;; '.') key=dot;; *) key=$ch;; esac
    send_key "$key"; sleep .03; i=$((i+1));
  done
  send_key ret; sleep .5
  monitor_cmd "pmemsave 0xb8000 4000 $vga"
  if [ "$command" = guiinfo ] || [ "$command" = drawtest ] || [ "$command" = fonttest ] || [ "$command" = canvastest ] || [ "$command" = desktest ] || [ "$command" = shellgui ]; then
    monitor_cmd "pmemsave 0x20000000 4096 $run_dir/framebuffer.bin"
    python3 - "$run_dir/framebuffer.bin" "$command" <<'PY'
import pathlib, sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[1]+'.summary').write_text(
    'command=%s bytes=%d nonzero=%d checksum=%08x\n' %
    (sys.argv[2], len(raw), sum(1 for b in raw if b), sum(raw) & 0xffffffff))
PY
  fi
  python3 - "$vga" "$command" "$run_dir/vga-all.txt" <<'PY'
import pathlib, sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
text=''.join(chr(raw[i]) if 32 <= raw[i] < 127 else ' ' for i in range(0, len(raw)-1, 2)).replace('\x00',' ')
pathlib.Path(sys.argv[1]+'.txt').write_text(text)
pathlib.Path(sys.argv[3]).write_text(pathlib.Path(sys.argv[3]).read_text() + text if pathlib.Path(sys.argv[3]).exists() else text)
command=sys.argv[2]
if command in ('shellrun','execpath'): expected='shellrun:'
elif command=='initinfo': expected='init pid/ready'
elif command=='resourceinfo': expected='resources as/'
elif command=='adoptioninfo': expected='adoption init/'
elif command=='waitblockinfo': expected='wait block/'
elif command=='lifecycleinfo': expected='lifecycle parent/'
elif command=='sessioninfo': expected='session init/'
elif command=='timertest': expected='timertest:'
elif command=='moduletest': expected='st: module'
elif command=='lockatomictest': expected='mictest:'
else: expected=command+':'
if expected not in text: raise SystemExit('missing VGA command result: '+command)
if command in ('guiinfo','drawtest','fonttest','canvastest','desktest','shellgui') and 'fallback reported' in text: raise SystemExit('GUI framebuffer fallback: '+command)
PY
done
[ -s "$vga" ] || { printf 'empty VGA dump\n' >&2; exit 1; }
if grep -Eqi 'check_exception|triple fault' "$trace"; then printf 'QEMU exception detected; artifacts kept in %s\n' "$run_dir" >&2; exit 1; fi
printf 'QEMU VGA validation passed: %s\n' "$dir"
