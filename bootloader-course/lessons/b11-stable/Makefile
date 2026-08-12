# Lesson B11: 装载框架（load/boot 分离）
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))
KERNEL_SECT := 16          # test-kernel（含 4K 栈 bss），16 扇区
KERNEL_LBA  := 9           # 好内核从 LBA 9 起
BAD_LBA     := 27          # 坏内核（checksum 被破坏）从 LBA 27 起

.PHONY: all check run clean

all: $(BUILD)/b11.img

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

$(BUILD)/test-kernel-main.o: test-kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test-kernel.elf: $(BUILD)/test-kernel.o $(BUILD)/test-kernel-main.o test-kernel.ld
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o $(BUILD)/test-kernel-main.o

$(BUILD)/kernel.bin: $(BUILD)/test-kernel.elf
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

# bad.bin：破坏 test-kernel.elf 的 Multiboot2 checksum（文件偏移 0x100C）
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
	@objdump -t $(BUILD)/loader.o | grep -q "loader_set" \
	  || { printf 'FAIL: loader_set symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "loader_boot" \
	  || { printf 'FAIL: loader_boot symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mmap_collect" \
	  || { printf 'FAIL: mmap_collect symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/test-kernel-main.o | grep -q "kernel_main" \
	  || { printf 'FAIL: kernel_main symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "mb2_boot" \
	  || { printf 'FAIL: mb2_boot symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/test-kernel.elf \
	  || { printf 'FAIL: test-kernel.elf Multiboot2 check\n' >&2; exit 1; }
	@printf 'B11 check PASS: stage1=512+55aa stage2=%sB loader_set/boot framework present\n' \
	  "$(STAGE2_BYTES)"

$(BUILD)/b11.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/bad.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=$(KERNEL_LBA) conv=notrunc status=none
	dd if=$(BUILD)/bad.bin of=$@ bs=512 seek=$(BAD_LBA) conv=notrunc status=none

run: $(BUILD)/b11.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b11.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
