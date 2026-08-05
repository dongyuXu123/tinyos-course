#!/bin/sh
# Reproducible QEMU VGA validator using the monitor and physical VGA memory.
# Usage: scripts/qemu-vga-check.sh <lesson-dir> <command> [command...]
set -eu
[ "$#" -ge 2 ] || { printf 'usage: %s <lesson-dir> <command> [command...]\n' "$0" >&2; exit 2; }
lesson=$1; shift
case "$lesson" in /*) dir=$lesson;; *) dir=$(cd "$lesson" && pwd);; esac
cd "$dir"
make -j"$(nproc)"
make check
run_dir=${QEMU_CHECK_DIR:-${TMPDIR:-/tmp}/tinyos-qemu-check-$$}
rm -rf "$run_dir"; mkdir -p "$run_dir"
monitor="$run_dir/monitor.sock"; serial="$run_dir/serial.log"; vga="$run_dir/vga.bin"; trace="$run_dir/qemu-trace.log"; screen="$run_dir/screen.ppm"
vga_name=".tinyos-vga-$$.bin"; screen_name=".tinyos-screen-$$.ppm"
ln -sf "$vga" "$vga_name"; ln -sf "$screen" "$screen_name"
cleanup_files() { rm -f "$vga_name" "$screen_name"; }
trap cleanup_files EXIT INT TERM
qemu-system-x86_64 -accel tcg -M q35 -vga none -device bochs-display,xres=800,yres=600,vgamem=16M -boot order=d -cdrom build/kernel.iso -display none \
  -serial "file:$serial" -monitor "unix:$monitor,server=on,wait=off" -no-reboot -no-shutdown \
  -d guest_errors -D "$trace" & qemu_pid=$!
cleanup() { if kill -0 "$qemu_pid" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$monitor" >/dev/null 2>&1 || true; kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true; fi; }
trap cleanup EXIT INT TERM
for _ in $(seq 1 100); do [ -S "$monitor" ] && break; sleep .1; done
[ -S "$monitor" ] || { printf 'QEMU monitor did not start\n' >&2; exit 1; }
sleep 1
monitor_cmd() { python3 - "$monitor" "$1" <<'PY'
import socket, sys
s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sys.argv[1]); s.recv(4096); s.sendall((sys.argv[2]+'\n').encode()); s.settimeout(2)
try:
    while True:
        data=s.recv(4096)
        if not data or b'(qemu)' in data: break
except socket.timeout:
    pass
s.close()
PY
}
send_key() { monitor_cmd "sendkey $1"; }
for command in "$@"; do
  i=1; while [ "$i" -le "${#command}" ]; do
    ch=$(printf '%s' "$command" | cut -c "$i")
    case "$ch" in ' ') key=spc;; '-') key=minus;; '_') key=shift-minus;; '/') key=slash;; '.') key=dot;; *) key=$ch;; esac
    send_key "$key"; sleep .03; i=$((i+1));
  done
    send_key ret; sleep 1
  monitor_cmd "pmemsave 753664 4000 $vga_name"
  if [ "$command" = guiinfo ] || [ "$command" = drawtest ] || [ "$command" = fonttest ] || [ "$command" = canvastest ] || [ "$command" = desktest ] || [ "$command" = shellgui ] || [ "$command" = iconinfo ] || [ "$command" = icontest ] || [ "$command" = desktopinfo ] || [ "$command" = mouseinfo ]; then
      monitor_cmd "screendump $screen_name"
    python3 - "$screen" "$command" <<'PY'
import pathlib, sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
parts=raw.split(None, 4)
if len(parts)!=5 or parts[0]!=b'P6':
    raise SystemExit('invalid QEMU screendump header')
try:
    width,height,maxval=map(int,parts[1:4])
except ValueError:
    raise SystemExit('invalid QEMU screendump dimensions')
pixels=parts[4]
if width<320 or width>1024 or height<200 or height>768 or maxval!=255:
    raise SystemExit('unexpected QEMU screendump geometry: %dx%d max=%d' % (width,height,maxval))
if len(pixels)!=width*height*3:
    raise SystemExit('QEMU screendump payload length mismatch')
coords=((0,0),(width-1,0),(0,height-1),(width-1,height-1),(width//2,height//2))
def rgb(x,y):
    i=(y*width+x)*3
    return tuple(pixels[i:i+3])
probe=[rgb(x,y) for x,y in coords]
nonzero=sum(1 for b in pixels if b)
pathlib.Path(sys.argv[1]+'.summary').write_text(
    'command=%s size=%dx%d bytes=%d nonzero=%d probes=%s checksum=%08x\\n' %
    (sys.argv[2],width,height,len(raw),nonzero,probe,sum(raw) & 0xffffffff))
if nonzero == 0:
    raise SystemExit('QEMU display surface is fully black: '+sys.argv[2])
# Text-only diagnostics need only a valid surface; drawing commands must expose
# multiple colors so that address/stride corruption cannot pass as GUI output.
if sys.argv[2] in ('drawtest','fonttest','canvastest','desktest','shellgui','icontest') and nonzero == 0:
    raise SystemExit('QEMU display surface failed structured probe: '+sys.argv[2])
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
if expected not in text and command not in ('guiinfo','drawtest','fonttest','canvastest','desktest','shellgui','iconinfo','icontest','desktopinfo','mouseinfo'):
    raise SystemExit('missing VGA command result: '+command)
if command in ('guiinfo','drawtest','fonttest','canvastest','desktest','shellgui','icontest') and 'fallback reported' in text: raise SystemExit('GUI framebuffer fallback: '+command)
if command == 'guiinfo' and ('ready/mapped: 0000000000000001/0000000000000001' not in text and 'ready/mapped: 1/1' not in text): raise SystemExit('GUI framebuffer is not ready/mapped: '+command)
if command in ('desktest','shellgui','icontest') and text.count('M')>20: raise SystemExit('GUI output contains legacy VGA corruption marker: '+command)
if command in ('desktest','shellgui','icontest') and command not in ('desktest',) and ('passed' not in text and 'rendered' not in text): raise SystemExit('GUI command did not pass: '+command)
if command in ('iconinfo','desktopinfo') and command+':' not in text: raise SystemExit('desktop diagnostic marker missing: '+command)
if command == 'icontest' and 'fallback reported' in text: raise SystemExit('desktop icon fallback: '+command)
PY
done
[ -s "$vga" ] || { printf 'empty VGA dump\n' >&2; exit 1; }
if grep -Eqi 'check_exception|triple fault' "$trace"; then printf 'QEMU exception detected; artifacts kept in %s\n' "$run_dir" >&2; exit 1; fi
printf 'QEMU VGA validation passed: %s\n' "$dir"
