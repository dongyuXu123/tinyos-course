#!/usr/bin/env bash
# Build, check, and optionally smoke-test one stable TinyOS lesson in isolation.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TINYOS_VALIDATE_OUT:-${TMPDIR:-/tmp}/tinyos-validate-$$}
LESSON=${1:-}
MODE=${2:-check}

if [[ -z "$LESSON" || ! "$LESSON" =~ ^[0-9]{1,3}$ ]]; then
  printf 'usage: %s LESSON [check|qemu]\n' "$0" >&2
  exit 2
fi
n=$(printf '%02d' "$((10#$LESSON))")
source_dir="$ROOT/lessons/lesson-$n-stable"
[[ -d "$source_dir" ]] || { printf 'missing stable lesson: %s\n' "$n" >&2; exit 1; }
if [[ "$n" == 00 ]]; then
  printf 'Lesson 00 is documentation-only; validate lesson-01-stable as its executable baseline.\n'
  exit 0
fi

command -v make >/dev/null || { printf 'missing dependency: make\n' >&2; exit 1; }
command -v grub-file >/dev/null || { printf 'missing dependency: grub-file\n' >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null || { printf 'missing dependency: qemu-system-x86_64\n' >&2; exit 1; }
mkdir -p "$OUT"
work="$OUT/lesson-$n-stable"
cp -a "$source_dir" "$work"
chmod -R u+w "$work"
make -C "$work" -j"$(nproc)"
make -C "$work" check
printf 'lesson-%s stable build/check: PASS\n' "$n"

if [[ "$MODE" == qemu ]]; then
  timeout 15s qemu-system-x86_64 -accel tcg -boot order=d \
    -cdrom "$work/build/kernel.iso" -display none -serial file:"$OUT/lesson-$n.serial" \
    -no-reboot -no-shutdown -d guest_errors -D "$OUT/lesson-$n.trace" || status=$?
  status=${status:-0}
  if [[ "$status" -ne 0 && "$status" -ne 124 ]]; then
    printf 'lesson-%s QEMU: FAIL (exit %s)\n' "$n" "$status" >&2
    exit "$status"
  fi
  if grep -Eiq 'triple fault|check_exception|illegal instruction|fatal' "$OUT/lesson-$n.trace" 2>/dev/null; then
    printf 'lesson-%s QEMU: FAIL (fatal marker)\n' "$n" >&2
    exit 1
  fi
  printf 'lesson-%s QEMU smoke: PASS\n' "$n"
fi
