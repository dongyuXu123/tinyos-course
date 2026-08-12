# Lesson B08: PT_LOAD 装载与 Multiboot2 交接
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))
KERNEL_SECT := 18          # test-kernel.elf = 8896 字节，18 扇区
KERNEL_LBA  := 9           # 放在 stage2 之后

.PHONY: all check run clean

all: $(BUILD)/b08.img

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

$(BUILD)/kernel.bin: $(BUILD)/test-kernel.elf
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o
	truncate -s $(STAGE2_BYTES) $@   # 补齐到 8 个扇区

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq 512 \
	  || { printf 'FAIL: stage1 size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/stage1.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: stage1 missing 0xAA55 signature\n' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -eq $(STAGE2_BYTES) \
	  || { printf 'FAIL: stage2 size != %s bytes\n' "$(STAGE2_BYTES)" >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "elf_load" \
	  || { printf 'FAIL: elf_load symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "mb2_boot" \
	  || { printf 'FAIL: mb2_boot symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "bios_interrupt" \
	  || { printf 'FAIL: bios_interrupt symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/test-kernel.elf \
	  || { printf 'FAIL: test-kernel.elf Multiboot2 check\n' >&2; exit 1; }
	@printf 'B08 check PASS: stage1=512+55aa stage2=%sB elf_load+mb2_boot present\n' \
	  "$(STAGE2_BYTES)"

$(BUILD)/b08.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=$(KERNEL_LBA) conv=notrunc status=none

run: $(BUILD)/b08.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b08.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
