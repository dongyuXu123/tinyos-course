# Lesson B13: ISO9660 基础读取 —— El Torito 光盘镜像
# 整个 loader（stage1 2048 字节 + stage2+loader）打包成引导镜像，xorriso
# 以 -b boot.bin -no-emul-boot -boot-load-size N 写入 CD；SeaBIOS 把镜像
# 连续加载到 0x7C00。
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
BOOT_BASE := 0x7C00
STAGE1_BYTES := 2048          # stage1 = 1 个 CD 扇区
STAGE2_BASE := 0x8400         # stage2 链接基址 = 0x7C00 + 2048
MAX_STAGE2 := 0x7000          # stage2 上限：保证 DAP(<64KB) 与镜像上沿安全

.PHONY: all check run clean

all: $(BUILD)/b13.img

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

$(BUILD)/cdroot:
	mkdir -p $@

$(BUILD)/cdroot/boot.bin: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/test.txt: test.txt | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b13.img: $(BUILD)/boot.bin $(BUILD)/cdroot/boot.bin \
                  $(BUILD)/cdroot/test.txt
	xorriso -as mkisofs -quiet -V B13TEST -b boot.bin -no-emul-boot \
	  -boot-load-size $$(( $$(stat -c%s $(BUILD)/boot.bin) / 512 )) \
	  -o $@ $(BUILD)/cdroot

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/b13.img
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cd_read_lba" \
	  || { printf 'FAIL: cd_read_lba symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "iso9660_open" \
	  || { printf 'FAIL: iso9660_open symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "iso9660_read" \
	  || { printf 'FAIL: iso9660_read symbol missing\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b13.img -toc 2>/dev/null | grep -qi "boot" \
	  || { printf 'FAIL: no El Torito boot record\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract
	@xorriso -osirrox on -indev $(BUILD)/b13.img \
	  -extract /test.txt $(BUILD)/extract/test.txt >/dev/null 2>&1 \
	  || xorriso -osirrox on -indev $(BUILD)/b13.img \
	  -extract /TEST.TXT $(BUILD)/extract/test.txt >/dev/null 2>&1
	@cmp $(BUILD)/extract/test.txt test.txt \
	  || { printf 'FAIL: extracted TEST.TXT differs from test.txt\n' >&2; exit 1; }
	@printf 'B13 check PASS: iso9660 symbols present, El Torito record present, TEST.TXT byte-identical\n'

run: $(BUILD)/b13.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b13.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
