#!/bin/sh
# Mini-GRUB 课程验证入口。
# Usage: validate-course.sh <lesson> [check|qemu]   或   validate-course.sh all [check|qemu]
#   lesson: b01..b23（b01-b22 已实现可执行；b23 终课=验收课，无启动镜像）
#   all: 终课回归——遍历 b01..b23，每课执行 check/qemu，输出 PASS/FAIL 汇总
# check 模式：拷贝到临时目录 -> make -> make check（不改写提交的 build/）
# qemu 模式：额外执行 QEMU 冒烟 + VGA 文本校验（scripts/qemu-text-check.sh）
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LESSON=$1
MODE=${2:-check}

# ---- all 模式：全课程回归（B23 终课验收入口）--------------------------------
if [ "$LESSON" = "all" ]; then
  case "$MODE" in
    check|qemu) ;;
    *) printf 'usage: %s all [check|qemu]\n' "$0" >&2; exit 2 ;;
  esac
  OUTLOG=${MINI_GRUB_ALL_LOG:-${TMPDIR:-/tmp}/mini-grub-all-$$}
  mkdir -p "$OUTLOG"
  pass=0; fail=0; failed=""
  for n in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 \
           20 21 22 23; do
    L="b$n"
    log="$OUTLOG/$L-$MODE.log"
    if "$0" "$L" "$MODE" >"$log" 2>&1; then
      pass=$((pass + 1)); printf '%-6s %-5s PASS\n' "$L" "$MODE"
    else
      fail=$((fail + 1)); failed="$failed $L"
      printf '%-6s %-5s FAIL\n' "$L" "$MODE"
      tail -5 "$log" | sed 's/^/      /' >&2
    fi
  done
  printf '\n=== Mini-GRUB all-%s summary: %d passed, %d failed ===\n' \
    "$MODE" "$pass" "$fail"
  [ "$fail" -eq 0 ] || printf 'failed:%s (logs in %s)\n' "$failed" "$OUTLOG" >&2
  [ "$fail" -eq 0 ]
  exit $?
fi

case "$LESSON" in
  b[0-9][0-9]) ;;
  *) printf 'usage: %s bNN [check|qemu]\n' "$0" >&2; exit 2 ;;
esac
case "$MODE" in
  check|qemu) ;;
  *) printf 'usage: %s bNN [check|qemu]\n' "$0" >&2; exit 2 ;;
esac

dir="$ROOT/lessons/$LESSON-stable"
[ -d "$dir" ] || { printf 'missing lesson dir %s\n' "$dir" >&2; exit 1; }
[ -f "$dir/README.md" ] || { printf 'missing README in %s\n' "$dir" >&2; exit 1; }

# 设计文档课（b05-b23）：无 Makefile，只做文档结构检查
if [ ! -f "$dir/Makefile" ]; then
  python3 "$ROOT/scripts/check-lesson-doc.py" "$LESSON"
  printf 'lesson-%s design-doc validation: PASS\n' "$LESSON"
  exit 0
fi

# 已实现课：拷贝到临时目录验证，绝不改写提交的 stable 产物
OUT=${MINI_GRUB_VALIDATE_OUT:-${TMPDIR:-/tmp}/mini-grub-validate-$$}
mkdir -p "$OUT"
work="$OUT/$LESSON-stable"
rm -rf "$work"
cp -a "$dir" "$work"
chmod -R u+w "$work"

export TINYOS_ROOT="$(cd "$ROOT/.." && pwd)/lessons"   # B12 只读复用 TinyOS 主线产物
export MINI_GRUB_COURSE_ROOT="$ROOT"                  # B23 course.py 定位真实课程根

case "$MODE" in
  check)
    make -C "$work" -j"$(nproc)"
    make -C "$work" check
    ;;
  qemu)
    # 每课的 VGA 文本 marker（与 make run 的可视结果一致）
    case "$LESSON" in
      b12)
        # B12 用 L05 镜像做严格校验：交互式发送 mmap 命令
        export QEMU_IMAGE="$work/build/b12-l05.img"
        export SEND_KEYS="m m a p ret"
        markers='TinyOS lesson 5 available'
        ;;
      b13)
        # B13 从 CD 启动（QEMU_CD=1 走 -cdrom），校验 ISO9660 解析结果
        export QEMU_CD=1
        markers=$(printf '%s\n' 'B13 iso9660' 'root dir extent' 'TEST.TXT' 'content match')
        ;;
      b14)
        # B14 从 CD 启动，校验四层文件抽象与路径查找（含错误路径）
        export QEMU_CD=1
        markers=$(printf '%s\n' 'B14 file: open /BOOT/KERNEL.ELF' \
          'B14 file: KERNEL.ELF magic ok' 'B14 error: file not found')
        ;;
      b15)
        # B15 从 CD 启动：stage1 读 core image + 按路径装载并启动测试内核
        export QEMU_CD=1
        markers=$(printf '%s\n' 'B15 eltorito: open /BOOT/KERNEL.ELF' \
          'B15 boot: jumping' 'test-kernel')
        ;;
      b16)
        # B16 交互式提示符：sendkey 输入 help / set foo=1 / set / badcmd
        export QEMU_CD=1
        export SEND_KEYS="h e l p ret s e t spc f o o equal 1 ret \
          s e t ret b a d c m d ret"
        markers=$(printf '%s\n' 'grub>' 'available commands' 'foo=1' \
          'command not found')
        ;;
      b17)
        # B17 自动执行 grub.cfg（无需输入）：变量展开/错误继续/multiboot2/boot
        export QEMU_CD=1
        markers=$(printf '%s\n' 'B17 script: grub.cfg executing' \
          'root is (cd0)' 'B17 boot: jumping' 'test-kernel')
        ;;
      b18)
        # B18 用 timeout=0 变体：无需按键自动启动 default（菜单+倒计时在
        # b18.img 的 make run / 手动 sendkey 验证中覆盖）
        export QEMU_CD=1
        export QEMU_IMAGE="$work/build/b18-t0.img"
        markers=$(printf '%s\n' 'B18 multiboot2' 'B18 boot: jumping' \
          'test-kernel')
        ;;
      b19)
        # B19 模块系统：脚本里先 hexdump（未加载报错）-> insmod -> lsmod
        # -> hexdump（加载后可用，dump 出 ELF magic 7f 45 4c 46）-> halt
        export QEMU_CD=1
        markers=$(printf '%s\n' 'B19 mod: Mini-GRUB module system' \
          'B19 script: module demo' \
          'B19 error: command not found: hexdump' \
          'B19 mod: insmod /boot/hexdump.mod' \
          'B19 lsmod: /boot/hexdump.mod' \
          'B19 hexdump: /boot/kernel.elf' '7f 45 4c 46' 'B19: halted')
        ;;
      b20)
        # B20 VBE 图形课：VBE 模式切换后 0xB8000 文本失效，marker 走串口；
        # 另用 screendump 像素探针验证 LFB 测试图案三色锚点
        export QEMU_CD=1
        export QEMU_SERIAL=1
        export QEMU_SCREENDUMP=1
        export QEMU_PIXELS="125,125,ff0000;425,325,00ff00;675,475,0000ff"
        markers=$(printf '%s\n' 'B20 vbe: VBE controller OK' \
          'B20 vbe: mode=' '800x600x32' 'lfb=' 'pitch=' \
          'B20 vbe: mode set, drawing test pattern to LFB' \
          'B20 vbe: pattern drawn, halting')
        ;;
      b21)
        # B21 交接课：loader 串口日志（装载内核 + VBE + mmap + boot），
        # 内核接管后在 VGA 文本显示 L61 banner 与 tinyos> 提示符即成功。
        # 注意：L61 内核的 framebuffer ready/mapped 状态取决于 bochs-display
        # 环境（实测真 GRUB 同配置下 guiinfo 亦为 ready=0/0），不属于引导器
        # 可达成判据；type-8 tag 的字节级正确性由 make check 校验。
        export QEMU_CD=1
        export QEMU_SERIAL=1
        export QEMU_BOCHS=1
        export QEMU_IMAGE="$work/build/b21.img"
        markers=$(printf '%s\n' 'B21 multiboot2: loaded' 'graphics request' \
          'B21 vbe: mode set' 'B21 mmap:' 'B21 boot: jumping' \
          'Lesson 61: Multiboot2 framebuffer' 'tinyos>')
        ;;
      b22)
        # B22 故障调试与 rescue（对照 grub-core/kern/err.c + normal/main.c）：
        # 第一步：主镜像错误路径演示——坏 ELF / 坏 header / 缺文件 / 缺模块
        # 全部被捕获并继续执行，随后正常装载内核并 boot。
        # 注意 ISO 用 -iso-level 3 -relaxed-filenames：Level 1 默认 8.3 会
        # 把 badheader.elf 截断成 badheade.elf 导致 file not found。
        export QEMU_CD=1
        export QEMU_SERIAL=1
        sh "$ROOT/scripts/qemu-text-check.sh" "$work" \
          'B22 err: Mini-GRUB fault debugging' \
          'B22 error: invalid ELF header' \
          'B22 error: invalid multiboot2 header' \
          'B22 error: file not found' \
          'B22 script: errors handled, continuing' \
          'B22 boot: jumping to entry=00100018'
        # 第二步：rescue 变体（无 grub.cfg）——loader 降级到 rescue> 提示符
        export QEMU_IMAGE="$work/build/b22-rescue.img"
        sh "$ROOT/scripts/qemu-text-check.sh" "$work" \
          'B22 error: config file not found' \
          'B22 rescue: core incomplete' \
          'rescue> '
        skip=1
        ;;
      b23)
        # 终课验收课：无启动镜像，qemu 模式做验收产物完整性检查；
        # 全课程回归本体由 validate-course.sh all check / all qemu 执行。
        [ -f "$ROOT/docs/source-to-screen.md" ] \
          || { printf 'B23 acceptance: missing docs/source-to-screen.md\n' >&2; exit 1; }
        grep -q '"all"' "$ROOT/scripts/validate-course.sh" \
          || { printf 'B23 acceptance: all mode missing in validate-course.sh\n' >&2; exit 1; }
        n=0
        for i in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 \
                 19 20 21 22; do
          [ -f "$ROOT/lessons/b$i-stable/Makefile" ] && n=$((n + 1))
        done
        [ "$n" -eq 22 ] \
          || { printf 'B23 acceptance: implemented lessons %d != 22\n' "$n" >&2; exit 1; }
        printf 'B23 acceptance: source-to-screen doc + all mode + 22 implemented lessons OK\n'
        skip=1
        ;;
      *)
        markers=$(case "$LESSON" in
          b01) printf '%s\n' 'B01 Mini-GRUB stage1' ;;
          b02) printf '%s\n' 'B02 stage2' ;;
          b03) printf '%s\n' 'B03 Mini-GRUB in 32-bit protected mode' ;;
          b04) printf '%s\n' 'B04 Mini-GRUB loader_main' ;;
          b05) printf '%s\n' 'B05 Mini-GRUB' 'signature 55 aa' ;;
          b06) printf '%s\n' 'B06 elf: entry=' 'B06 done' ;;
          b07) printf '%s\n' 'B07 mb2 header ok' 'B07 done' ;;
          b08) printf '%s\n' 'B08 boot: jumping' 'B08 test-kernel' ;;
          b09) printf '%s\n' 'B09 test-kernel: magic ok' 'B09 walker done' ;;
          b10) printf '%s\n' 'type=0006' 'B10 walker done' ;;
          b11) printf '%s\n' 'B11 error: bad kernel rejected' 'B11 boot: jumping' 'B11 test-kernel: magic ok' ;;
        esac)
        ;;
    esac
    # b22 在 case 内自包含地跑完了两步 qemu 验证，跳过共享的 markers 逻辑
    if [ "${skip:-0}" != 1 ]; then
      [ -n "$markers" ] || { printf 'no VGA markers defined for %s\n' "$LESSON" >&2; exit 1; }
      # shellcheck disable=SC2086
      sh "$ROOT/scripts/qemu-text-check.sh" "$work" $markers
    fi
    ;;
esac
printf 'lesson-%s %s: PASS\n' "$LESSON" "$MODE"
