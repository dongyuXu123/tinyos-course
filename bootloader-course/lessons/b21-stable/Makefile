# Lesson B21: type-8 framebuffer tag —— 启动 TinyOS L61 GUI 内核
# 核心 = B17 引导链 + B20 VBE + B11 E820；内核 = TinyOS 主线 L61 的
# kernel.elf（只读复用，验证脚本导出 TINYOS_ROOT 指向仓库根 lessons/）。
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
BOOT_BASE := 0x7C00
STAGE1_BYTES := 2048
STAGE2_BASE := 0x8400
MAX_STAGE2 := 0x7000

TINYOS_ROOT ?= ../../../lessons
L61_ELF := $(TINYOS_ROOT)/lesson-61-stable/build/kernel.elf

.PHONY: all check run clean

all: $(BUILD)/b21.img

$(BUILD):
	mkdir -p $@/boot/grub

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/vbe.o: vbe.c vbe.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/script.o: script.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/loader.o: loader.c vbe.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext $(BOOT_BASE) --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE1_BYTES) $@

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o $(BUILD)/script.o \
                     $(BUILD)/vbe.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o \
	  $(BUILD)/script.o $(BUILD)/vbe.o

$(BUILD)/boot.bin: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s $$(( ($$(stat -c%s $@) + 2047) / 2048 * 2048 )) $@

# ---- CD 树（TinyOS L61 kernel.elf 只读复用）-------------------------------
$(BUILD)/cdroot:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/kernel.elf: $(L61_ELF) | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/grub/grub.cfg: grub.cfg | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b21.img: $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/boot/kernel.elf \
                  $(BUILD)/cdroot/boot/grub/grub.cfg
	xorriso -as mkisofs -quiet -V B21TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

# ---- 主机侧 vbe.c 单元测试（B20 复用）---------------------------------------
$(BUILD)/test_vbe: test_vbe.c vbe.c vbe.h | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_vbe.c vbe.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/b21.img $(BUILD)/test_vbe
	@test -f "$(L61_ELF)" \
	  || { printf 'FAIL: TinyOS L61 kernel.elf missing (%s)\n' '$(L61_ELF)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_vbe >/dev/null \
	  || { printf 'FAIL: vbe unit test\n' >&2; exit 1; }
	# 内核须是 ELF32 Multiboot2（graphics request 800x600x32）
	@test "$$(LC_ALL=C readelf -h $(L61_ELF) | awk '/Class:/{print $$2}')" = "ELF32" \
	  || { printf 'FAIL: L61 kernel not ELF32\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mb2_graphics_request" \
	  || { printf 'FAIL: mb2_graphics_request symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "vbe_find_mode" \
	  || { printf 'FAIL: vbe_find_mode symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mbi_build" \
	  || { printf 'FAIL: mbi_build symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mmap_collect" \
	  || { printf 'FAIL: mmap_collect symbol missing\n' >&2; exit 1; }
	@grub-script-check grub.cfg \
	  || { printf 'FAIL: grub.cfg syntax\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b21.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract/boot
	@xorriso -osirrox on -indev $(BUILD)/b21.img \
	  -extract /boot/kernel.elf $(BUILD)/extract/boot/kernel.elf >/dev/null 2>&1
	@cmp $(BUILD)/extract/boot/kernel.elf $(L61_ELF) \
	  || { printf 'FAIL: extracted kernel differs\n' >&2; exit 1; }
	@python3 check-gfx-request.py "$(L61_ELF)" \
	  || { printf 'FAIL: L61 graphics request not 800x600x32\n' >&2; exit 1; }
	@printf 'B21 check PASS: L61 kernel ELF32+mb2 graphics request + symbols present\n'

run: $(BUILD)/b21.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b21.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
