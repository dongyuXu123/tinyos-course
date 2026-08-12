# Lesson B16: 命令注册表与极简 grub> 命令行 —— El Torito 光盘镜像
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
BOOT_BASE := 0x7C00
STAGE1_BYTES := 2048          # stage1 = 1 个 CD 扇区（SeaBIOS 只加载它）
STAGE2_BASE := 0x8400         # core 链接基址 = 0x7C00 + 2048
MAX_STAGE2 := 0x7000

.PHONY: all check run clean

all: $(BUILD)/b16.img

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/loader.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext $(BOOT_BASE) --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE1_BYTES) $@

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o

$(BUILD)/boot.bin: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s $$(( ($$(stat -c%s $@) + 2047) / 2048 * 2048 )) $@

# xorriso -boot-info-table 启用"保留大小写"：源文件名即 CD 上的名字
$(BUILD)/cdroot:
	mkdir -p $@

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b16.img: $(BUILD)/boot.bin $(BUILD)/cdroot/BOOT.BIN
	xorriso -as mkisofs -quiet -V B16TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/b16.img
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_register" \
	  || { printf 'FAIL: cmd_register symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_execute" \
	  || { printf 'FAIL: cmd_execute symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "env_set" \
	  || { printf 'FAIL: env_set symbol missing\n' >&2; exit 1; }
	# 命令表静态检查：help/set/boot 三个命令的注册实例存在
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_help\b" \
	  || { printf 'FAIL: help command missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_set\b" \
	  || { printf 'FAIL: set command missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_boot\b" \
	  || { printf 'FAIL: boot command missing\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b16.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@printf 'B16 check PASS: command table (help/set/boot) registered, symbols present\n'

run: $(BUILD)/b16.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b16.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
