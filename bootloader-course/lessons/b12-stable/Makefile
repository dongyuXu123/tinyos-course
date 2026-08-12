# Lesson B12: 启动 TinyOS 主线（checkpoint）—— 自写引导器替代真 GRUB
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))
KERNEL_SECT := 20          # L05 kernel.elf = 10144 字节，20 扇区
L01_LBA := 9               # L01 kernel.elf 从 LBA 9 起（11 扇区 = 5632 字节）
L05_LBA := 27              # L05 kernel.elf 从 LBA 27 起（20 扇区）

# 只读复用 TinyOS 主线产物（不修改 lessons/ 任何文件）。
# 默认相对路径适用于在本目录内构建；验证脚本会导出 TINYOS_ROOT 指向仓库根。
TINYOS_ROOT ?= ../../../lessons
L01_ELF := $(TINYOS_ROOT)/lesson-01-stable/build/kernel.elf
L05_ELF := $(TINYOS_ROOT)/lesson-05-stable/build/kernel.elf

.PHONY: all check run-l01 run-l05 clean

all: $(BUILD)/b12-l01.img $(BUILD)/b12-l05.img

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/loader-l01.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -DTARGET_LBA=$(L01_LBA) -c $< -o $@

$(BUILD)/loader-l05.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -DTARGET_LBA=$(L05_LBA) -c $< -o $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<

$(BUILD)/stage2-l01.bin: $(BUILD)/stage2.o $(BUILD)/loader-l01.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader-l01.o
	truncate -s $(STAGE2_BYTES) $@

$(BUILD)/stage2-l05.bin: $(BUILD)/stage2.o $(BUILD)/loader-l05.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader-l05.o
	truncate -s $(STAGE2_BYTES) $@

$(BUILD)/l01-kernel.bin: $(L01_ELF)
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

$(BUILD)/l05-kernel.bin: $(L05_ELF)
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

check: $(BUILD)/stage1.bin $(BUILD)/stage2-l01.bin $(BUILD)/stage2-l05.bin \
       $(BUILD)/l01-kernel.bin $(BUILD)/l05-kernel.bin
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq 512 \
	  || { printf 'FAIL: stage1 size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/stage1.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: stage1 missing 0xAA55 signature\n' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2-l05.bin)" -eq $(STAGE2_BYTES) \
	  || { printf 'FAIL: stage2 size != %s bytes\n' "$(STAGE2_BYTES)" >&2; exit 1; }
	@objdump -t $(BUILD)/loader-l05.o | grep -q "loader_set" \
	  || { printf 'FAIL: loader_set symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "mb2_boot" \
	  || { printf 'FAIL: mb2_boot symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(L01_ELF) \
	  || { printf 'FAIL: TinyOS L01 kernel Multiboot2 check\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(L05_ELF) \
	  || { printf 'FAIL: TinyOS L05 kernel Multiboot2 check\n' >&2; exit 1; }
	@printf 'B12 check PASS: TinyOS L01+L05 kernels valid, loader framework present\n'

$(BUILD)/b12-l01.img: $(BUILD)/stage1.bin $(BUILD)/stage2-l01.bin $(BUILD)/l01-kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2-l01.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/l01-kernel.bin of=$@ bs=512 seek=$(L01_LBA) conv=notrunc status=none

$(BUILD)/b12-l05.img: $(BUILD)/stage1.bin $(BUILD)/stage2-l05.bin $(BUILD)/l05-kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2-l05.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/l05-kernel.bin of=$@ bs=512 seek=$(L05_LBA) conv=notrunc status=none

run-l01: $(BUILD)/b12-l01.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b12-l01.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

run-l05: $(BUILD)/b12-l05.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b12-l05.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
