# Lesson B15: El Torito 光盘引导 —— boot image + core image 模型
# 与 B13/B14 的"整包镜像"不同：xorriso 只把 stage1（2048 B，含
# -boot-info-table 写入的 BIF）作为 El Torito boot image，stage1 用 BIF 的
# bi_file/bi_length 从 CD 读入 stage2+loader（core）到 0x8400。
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

all: $(BUILD)/b15.img

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

# boot.bin = stage1（BIOS 加载）+ core（stage1 从 CD 读入）
$(BUILD)/boot.bin: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s $$(( ($$(stat -c%s $@) + 2047) / 2048 * 2048 )) $@

# ---- 测试内核：CD 上的 /BOOT/KERNEL.ELF（B08 复用）-------------------------
$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

$(BUILD)/cdroot:
	mkdir -p $@/BOOT

# 注意：xorriso 的 -boot-info-table 会启用"保留大小写"（relaxed filenames），
# 源文件名的写法就是 CD 上的 ISO9660 名字。按 ISO9660 惯例用大写文件名，
# 使 loader 的路径 "/BOOT/KERNEL.ELF" 与 CD 实际命名一致（B13/B14 用默认
# 大写转换，文件名小写即可；B15 起必须注意此差异）。
$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/BOOT/KERNEL.ELF: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

# -boot-load-size 4：BIOS 只加载 stage1（2048 B），core 由 stage1 读入；
# -boot-info-table：xorriso 在 boot image 偏移 8-63 写 BIF（bi_pvd/file/length）
$(BUILD)/b15.img: $(BUILD)/boot.bin $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/BOOT/KERNEL.ELF
	xorriso -as mkisofs -quiet -V B15TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/kernel.elf $(BUILD)/b15.img
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "file_open" \
	  || { printf 'FAIL: file_open symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "elf_load" \
	  || { printf 'FAIL: elf_load symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "mb2_boot" \
	  || { printf 'FAIL: mb2_boot symbol missing\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf \
	  || { printf 'FAIL: KERNEL.ELF not a valid Multiboot2 kernel\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b15.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract
	@xorriso -osirrox on -indev $(BUILD)/b15.img \
	  -extract /BOOT.BIN $(BUILD)/extract/BOOT.BIN >/dev/null 2>&1
	@python3 -c "import struct,sys; d=open(sys.argv[1],'rb').read(); orig=open(sys.argv[2],'rb').read(); pvd,bf,bl,c=struct.unpack_from('<IIII',d,8); assert pvd==16, pvd; assert bl==len(orig), (bl,len(orig)); assert bl==len(d); print('BIF ok: pvd=%d file=%d len=%d'%(pvd,bf,bl))" \
	  $(BUILD)/extract/BOOT.BIN $(BUILD)/boot.bin
	@printf 'B15 check PASS: BIF fields valid, El Torito record has boot-info-table, symbols present\n'

run: $(BUILD)/b15.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b15.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
