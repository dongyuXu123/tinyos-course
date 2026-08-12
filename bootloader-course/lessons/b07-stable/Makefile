# Lesson B07: Multiboot2 header 校验
AS := as
CC := gcc
LD := ld
OBJCOPY := objcopy
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))
KERNEL_SECT := 18          # test-kernel.elf = 8896 字节（含节头表），18 扇区
KERNEL_LBA  := 9           # ok.elf 从 LBA 9 起
BAD_LBA     := 27          # bad.elf 从 LBA 27 起

.PHONY: all check run clean

all: $(BUILD)/b07.img

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/loader.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test-kernel.o: test-kernel.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/test-kernel.elf: $(BUILD)/test-kernel.o test-kernel.ld
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none -o $@ $<

# ok.elf：ELF 文件本体，补齐到 17 扇区
$(BUILD)/kernel.bin: $(BUILD)/test-kernel.elf
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

# bad.elf：破坏 test-kernel.elf 的 Multiboot2 checksum（文件偏移 0x100C）。
# .multiboot 段位于文件偏移 0x1000（首个 LOAD 的 p_offset）；magic/arch/length/
# checksum 依次在其后，checksum 在第 0x100C 字节。
# 注意：POSIX printf 不支持 \x 转义，用八进制 \000 输出空字节（dash 兼容）。
$(BUILD)/bad.bin: $(BUILD)/test-kernel.elf
	cp $< $@
	printf '\000' | dd of=$@ bs=1 seek=4108 conv=notrunc status=none
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o
	truncate -s $(STAGE2_BYTES) $@   # 补齐到 8 个扇区

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/bad.bin
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq 512 \
	  || { printf 'FAIL: stage1 size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/stage1.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: stage1 missing 0xAA55 signature\n' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -eq $(STAGE2_BYTES) \
	  || { printf 'FAIL: stage2 size != %s bytes\n' "$(STAGE2_BYTES)" >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mb2_header_check" \
	  || { printf 'FAIL: mb2_header_check symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "bios_interrupt" \
	  || { printf 'FAIL: bios_interrupt symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/test-kernel.elf \
	  || { printf 'FAIL: ok.elf Multiboot2 check\n' >&2; exit 1; }
	@if grub-file --is-x86-multiboot2 $(BUILD)/bad.bin; then \
	  printf 'FAIL: bad.elf should be rejected\n' >&2; exit 1; fi
	@printf 'B07 check PASS: ok=MB2 valid, bad=MB2 rejected, loader checks present\n'

$(BUILD)/b07.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/bad.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=$(KERNEL_LBA) conv=notrunc status=none
	dd if=$(BUILD)/bad.bin of=$@ bs=512 seek=$(BAD_LBA) conv=notrunc status=none

run: $(BUILD)/b07.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b07.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
