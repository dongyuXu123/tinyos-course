# Lesson B17: grub.cfg 解析执行 —— El Torito 光盘镜像
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

all: $(BUILD)/b17.img

$(BUILD):
	mkdir -p $@/boot/grub

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/script.o: script.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/loader.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext $(BOOT_BASE) --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE1_BYTES) $@

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o $(BUILD)/script.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o $(BUILD)/script.o

$(BUILD)/boot.bin: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s $$(( ($$(stat -c%s $@) + 2047) / 2048 * 2048 )) $@

# ---- 测试内核：CD 上的 /boot/kernel.elf（B08 复用）--------------------------
$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

# ---- CD 树：boot image + boot/grub/grub.cfg + boot/kernel.elf ----------------
# xorriso -boot-info-table 启用"保留大小写"：源文件名即 CD 上的名字，
# 因此 loader 用 /boot/grub/grub.cfg 与 /boot/kernel.elf（小写，同 GRUB 惯例）。
$(BUILD)/cdroot:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/grub/grub.cfg: grub.cfg | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b17.img: $(BUILD)/boot.bin $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/boot/grub/grub.cfg \
                  $(BUILD)/cdroot/boot/kernel.elf
	xorriso -as mkisofs -quiet -V B17TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

# ---- 主机侧 tokenizer 单元测试 ----------------------------------------------
$(BUILD)/test_script: test_script.c script.c | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_script.c script.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/kernel.elf $(BUILD)/b17.img $(BUILD)/test_script
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_script \
	  || { printf 'FAIL: tokenizer unit test\n' >&2; exit 1; }
	@objdump -t $(BUILD)/script.o | grep -q "script_tokenize" \
	  || { printf 'FAIL: script_tokenize symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "script_run_file" \
	  || { printf 'FAIL: script_run_file symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_multiboot2" \
	  || { printf 'FAIL: multiboot2 command missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_boot\b" \
	  || { printf 'FAIL: boot command missing\n' >&2; exit 1; }
	@grub-script-check grub.cfg \
	  || { printf 'FAIL: grub.cfg syntax (grub-script-check)\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf \
	  || { printf 'FAIL: KERNEL.ELF not a valid Multiboot2 kernel\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b17.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract/boot/grub
	@xorriso -osirrox on -indev $(BUILD)/b17.img \
	  -extract /boot/grub/grub.cfg $(BUILD)/extract/boot/grub/grub.cfg \
	  >/dev/null 2>&1
	@cmp $(BUILD)/extract/boot/grub/grub.cfg grub.cfg \
	  || { printf 'FAIL: extracted grub.cfg differs\n' >&2; exit 1; }
	@printf 'B17 check PASS: tokenizer unit test, grub-script-check, symbols present\n'

run: $(BUILD)/b17.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b17.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
