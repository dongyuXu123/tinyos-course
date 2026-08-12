#!/bin/sh
# Reproducible QEMU VGA-text validator for Mini-GRUB lessons.
# Boots the lesson's floppy (or CD, with QEMU_CD=1) image, dumps physical VGA
# text memory at 0xB8000 via the monitor, and greps for the expected markers.
# Usage: scripts/qemu-text-check.sh <lesson-dir> <marker> [marker...]
set -eu
[ "$#" -ge 2 ] || { printf 'usage: %s <lesson-dir> <marker> [marker...]\n' "$0" >&2; exit 2; }
lesson=$1; shift
case "$lesson" in /*) dir=$lesson;; *) dir=$(cd "$lesson" && pwd);; esac
cd "$dir"
make -j"$(nproc)"
make check
run_dir=${QEMU_CHECK_DIR:-${TMPDIR:-/tmp}/mini-grub-qemu-$$}
rm -rf "$run_dir"; mkdir -p "$run_dir"
monitor="$run_dir/monitor.sock"; trace="$run_dir/qemu-trace.log"; vga="$run_dir/vga.bin"
name=$(basename "$dir" | sed 's/-stable$//')   # b01 -> build/b01.img
img=${QEMU_IMAGE:-"$dir/build/$name.img"}
[ -f "$img" ] || { printf 'missing disk image %s\n' "$img" >&2; exit 1; }
# 在临时目录启动 QEMU：QEMU monitor 的 pmemsave 对绝对路径文件名解析不稳，
# 用相对文件名（落在 run_dir）最可靠。
cd "$run_dir"
if [ "${QEMU_CD:-0}" = 1 ]; then
  # CD 课（B13/B15/B20/B21）：从 El Torito 光盘启动；
  # QEMU_BOCHS=1 时用 bochs-display（B21 内核 GUI banner 需要，同主线约定）
  if [ "${QEMU_BOCHS:-0}" = 1 ]; then
    vgadev="-vga none -device bochs-display,xres=800,yres=600,vgamem=16M"
    machine="-M q35"
  else
    vgadev="-vga std"
    machine="-M pc"
  fi
  qemu-system-x86_64 -accel tcg $machine -boot order=d \
    -cdrom "$img" -display none $vgadev \
    -serial "file:$run_dir/serial.log" \
    -monitor "unix:$monitor,server=on,wait=off" -no-reboot -no-shutdown \
    -d guest_errors -D "$trace" & qemu_pid=$!
else
  qemu-system-x86_64 -accel tcg -M pc -vga std -boot order=a \
    -drive file="$img",format=raw,if=floppy -display none \
    -serial "file:$run_dir/serial.log" \
    -monitor "unix:$monitor,server=on,wait=off" -no-reboot -no-shutdown \
    -d guest_errors -D "$trace" & qemu_pid=$!
fi
cleanup() {
  if kill -0 "$qemu_pid" 2>/dev/null; then
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM
for _ in $(seq 1 100); do [ -S "$monitor" ] && break; sleep .1; done
[ -S "$monitor" ] || { printf 'QEMU monitor did not start\n' >&2; exit 1; }
# TCG 模拟下 CD 引导较慢：串口课轮询等 boot 输出就绪（最多 10s），VGA 课固定等待
if [ "${QEMU_SERIAL:-0}" = 1 ]; then
  for _ in $(seq 1 50); do [ -s "$run_dir/serial.log" ] && break; sleep .2; done
else
  sleep 1
fi
monitor_cmd() { python3 - "$monitor" "$1" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1]); s.recv(4096)
s.sendall((sys.argv[2] + '\n').encode()); s.settimeout(2)
try:
    while True:
        data = s.recv(4096)
        if not data or b'(qemu)' in data:
            break
except socket.timeout:
    pass
s.close()
PY
}
# 可选：通过 monitor sendkey 发送按键（如交互式内核输入 "mmap" + 回车）
if [ -n "${SEND_KEYS:-}" ]; then
  for key in $SEND_KEYS; do
    monitor_cmd "sendkey $key"
    sleep .05
  done
  sleep 1
fi
# 慢启动内核（B21 等）的 banner 可能晚于 loader 串口输出出现：重试 dump
# VGA 文本直到全部 marker 命中或超时（每次间隔 2s，最多 6 次）。
# 图形课（B20/B21）：VBE 模式切换后 0xB8000 文本失效，marker 从串口日志取；
# 内核接管后的输出（如 L61 banner）仍在 VGA 文本，串口没有的 marker 回退 VGA。
missing=""
attempt=0
while [ "$attempt" -lt 6 ]; do
  attempt=$((attempt + 1))
  monitor_cmd "pmemsave 753664 4000 vga.bin"
  [ -s "$vga" ] || { printf 'empty VGA dump\n' >&2; exit 1; }
  text=$(python3 - "$vga" <<'PY'
import pathlib, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
print(''.join(chr(raw[i]) if 32 <= raw[i] < 127 else ' ' for i in range(0, len(raw) - 1, 2)))
PY
)
  missing=""
  if [ "${QEMU_SERIAL:-0}" = 1 ]; then
    serial="$run_dir/serial.log"
    [ -s "$serial" ] || { printf 'empty serial log\n' >&2; exit 1; }
    stext=$(cat "$serial")
    for marker in "$@"; do
      case "$stext" in *"$marker"*) ;; *)
        case "$text" in *"$marker"*) ;; *)
          missing="$missing
missing marker (serial+VGA): $marker";; esac
      esac
    done
  else
    for marker in "$@"; do
      case "$text" in *"$marker"*) ;; *) missing="$missing
missing VGA marker: $marker";; esac
    done
  fi
  [ -z "$missing" ] && break
  [ "$attempt" -lt 6 ] && sleep 2
done
if [ -n "$missing" ]; then
  printf '%s\n' "$missing" >&2
  exit 1
fi
# 图形课（B20/B21）：QEMU_SCREENDUMP=1 时额外 screendump 像素探针。
# QEMU_PIXELS="x,y,hexcolor;x,y,hexcolor" 逐个探测 LFB 测试图案锚点。
if [ "${QEMU_SCREENDUMP:-0}" = 1 ]; then
  screen="$run_dir/screen.ppm"
  monitor_cmd "screendump screen.ppm"
  [ -s "$screen" ] || { printf 'empty screendump\n' >&2; exit 1; }
  python3 - "$screen" "$QEMU_PIXELS" <<'PY'
import pathlib, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
parts = raw.split(None, 4)
if len(parts) != 5 or parts[0] != b'P6':
    raise SystemExit('invalid QEMU screendump header')
width, height, maxval = map(int, parts[1:4])
pixels = parts[4]
if maxval != 255 or len(pixels) != width * height * 3:
    raise SystemExit('unexpected screendump geometry')
probes = [p for p in sys.argv[2].split(';') if p]
if not probes:
    raise SystemExit('QEMU_PIXELS empty')
for p in probes:
    x, y, want = p.split(',')
    x, y = int(x), int(y)
    want = int(want, 16)
    if x >= width or y >= height:
        raise SystemExit('probe (%d,%d) out of range %dx%d' % (x, y, width, height))
    i = (y * width + x) * 3
    got = (pixels[i] << 16) | (pixels[i + 1] << 8) | pixels[i + 2]
    if got != want:
        raise SystemExit('pixel (%d,%d): got %06x want %06x' % (x, y, got, want))
print('screendump pixel probes passed: %s' % sys.argv[2])
PY
fi
if grep -Eqi 'check_exception|triple fault' "$trace"; then
  printf 'QEMU exception detected; artifacts kept in %s\n' "$run_dir" >&2
  exit 1
fi
printf 'QEMU VGA-text validation passed: %s (%s)\n' "$dir" "$*"
