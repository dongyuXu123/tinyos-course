# Lesson B14: 路径查找与文件抽象 —— El Torito 光盘镜像
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
BOOT_BASE := 0x7C00
STAGE1_BYTES := 2048          # stage1 = 1 个 CD 扇区
STAGE2_BASE := 0x8400         # stage2 链接基址 = 0x7C00 + 2048
MAX_STAGE2 := 0x7000

.PHONY: all check run clean

all: $(BUILD)/b14.img

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

# ---- 测试内核：CD 上的 /BOOT/KERNEL.ELF（最小 Multiboot2 ELF，B08 复用）----
$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

$(BUILD)/cdroot:
	mkdir -p $@/BOOT

$(BUILD)/cdroot/boot.bin: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/BOOT/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/test.txt: test.txt | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b14.img: $(BUILD)/boot.bin $(BUILD)/cdroot/boot.bin \
                  $(BUILD)/cdroot/BOOT/kernel.elf $(BUILD)/cdroot/test.txt
	xorriso -as mkisofs -quiet -V B14TEST -b boot.bin -no-emul-boot \
	  -boot-load-size $$(( $$(stat -c%s $(BUILD)/boot.bin) / 512 )) \
	  -o $@ $(BUILD)/cdroot

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/kernel.elf $(BUILD)/b14.img
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "file_open" \
	  || { printf 'FAIL: file_open symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "file_read" \
	  || { printf 'FAIL: file_read symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "file_close" \
	  || { printf 'FAIL: file_close symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf \
	  || { printf 'FAIL: KERNEL.ELF not a valid Multiboot2 kernel\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b14.img -toc 2>/dev/null | grep -qi "boot" \
	  || { printf 'FAIL: no El Torito boot record\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract/BOOT
	@xorriso -osirrox on -indev $(BUILD)/b14.img \
	  -extract /BOOT/kernel.elf $(BUILD)/extract/BOOT/kernel.elf >/dev/null 2>&1 \
	  || xorriso -osirrox on -indev $(BUILD)/b14.img \
	  -extract /BOOT/KERNEL.ELF $(BUILD)/extract/BOOT/kernel.elf >/dev/null 2>&1
	@cmp $(BUILD)/extract/BOOT/kernel.elf $(BUILD)/kernel.elf \
	  || { printf 'FAIL: extracted KERNEL.ELF differs\n' >&2; exit 1; }
	@xorriso -osirrox on -indev $(BUILD)/b14.img \
	  -extract /test.txt $(BUILD)/extract/test.txt >/dev/null 2>&1 \
	  || xorriso -osirrox on -indev $(BUILD)/b14.img \
	  -extract /TEST.TXT $(BUILD)/extract/test.txt >/dev/null 2>&1
	@cmp $(BUILD)/extract/test.txt test.txt \
	  || { printf 'FAIL: extracted TEST.TXT differs from test.txt\n' >&2; exit 1; }
	@printf 'B14 check PASS: file_open/read/close symbols present, KERNEL.ELF + TEST.TXT byte-identical\n'

run: $(BUILD)/b14.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b14.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
