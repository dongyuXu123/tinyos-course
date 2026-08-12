#!/bin/sh
# Mini-GRUB 逐字复刻层校验：reference/grub-2.14 与 GRUB 2.14 源码逐字节一致。
#
# 校验两层：
#   1. sha256sum -c SHA256SUMS     —— 复刻文件与仓库内清单一致（防篡改）
#   2. 逐一对比 GRUB_SRC           —— 复刻文件与已下载源码逐字节一致
#
# GRUB_SRC 环境变量可覆盖源码路径；未找到源码目录时仅做第 1 层并提示
# （课程红线：不自动下载第三方源码，只读对比本机已取得的副本）。
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/reference/grub-2.14"
GRUB_SRC=${GRUB_SRC:-"$HOME/grub-src/grub-2.14"}

[ -f "$REF/SHA256SUMS" ] || { printf 'missing %s\n' "$REF/SHA256SUMS" >&2; exit 1; }

# 第 1 层：复刻文件与清单一致
( cd "$REF" && sha256sum -c SHA256SUMS >/dev/null ) \
  || { printf 'FAIL: replicated files differ from SHA256SUMS\n' >&2; exit 1; }
printf 'layer 1: %d replicated files match SHA256SUMS\n' \
  "$(wc -l < "$REF/SHA256SUMS")"

# 第 2 层：与已下载源码逐字节一致（源码不存在时提示并跳过）
if [ ! -d "$GRUB_SRC" ]; then
  printf 'note: GRUB_SRC=%s not found; layer 2 skipped (no auto-download)\n' \
    "$GRUB_SRC"
  printf 'set GRUB_SRC=<grub-2.14 dir> to enable byte-level verification\n'
  exit 0
fi

fail=0; n=0
while read -r _ rel; do
  [ -n "${rel:-}" ] || continue
  n=$((n + 1))
  if [ ! -f "$GRUB_SRC/$rel" ]; then
    printf 'layer 2 DIFF: %s missing in GRUB_SRC\n' "$rel" >&2
    fail=1
  elif ! cmp -s "$REF/$rel" "$GRUB_SRC/$rel"; then
    printf 'layer 2 DIFF: %s\n' "$rel" >&2
    fail=1
  fi
done < "$REF/SHA256SUMS"

if [ "$fail" -ne 0 ]; then
  printf 'FAIL: %d/%d files differ from GRUB_SRC\n' "$fail" "$n" >&2
  exit 1
fi
printf 'layer 2: %d replicated files byte-identical to %s\n' "$n" "$GRUB_SRC"
printf 'reference verification PASS: core GRUB files verbatim match upstream\n'
