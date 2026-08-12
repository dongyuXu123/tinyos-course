# Lesson B19: 模块系统 —— El Torito 光盘镜像（核心 + hexdump.mod）
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

all: $(BUILD)/b19.img

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

# ---- 测试内核（hexdump 的演示文件，B08 复用）--------------------------------
$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

# ---- 示例模块：ET_REL 可重定位 ELF ------------------------------------------
$(BUILD)/hexdump.mod: mod/hexdump.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- CD 树 ------------------------------------------------------------------
$(BUILD)/cdroot:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/hexdump.mod: $(BUILD)/hexdump.mod | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/grub/grub.cfg: grub.cfg | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b19.img: $(BUILD)/boot.bin $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/boot/kernel.elf \
                  $(BUILD)/cdroot/boot/hexdump.mod \
                  $(BUILD)/cdroot/boot/grub/grub.cfg
	xorriso -as mkisofs -quiet -V B19TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

# ---- 主机侧 tokenizer 单元测试 ----------------------------------------------
$(BUILD)/test_script: test_script.c script.c | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_script.c script.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/kernel.elf $(BUILD)/hexdump.mod $(BUILD)/b19.img \
       $(BUILD)/test_script
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_script \
	  || { printf 'FAIL: tokenizer unit test\n' >&2; exit 1; }
	# hexdump.mod 必须是 ET_REL 可重定位 ELF（GRUB .mod 同款格式）
	@test "$$(LC_ALL=C readelf -h $(BUILD)/hexdump.mod | awk '/Type:/{print $$2}')" = "REL" \
	  || { printf 'FAIL: hexdump.mod not ET_REL\n' >&2; exit 1; }
	@objdump -t $(BUILD)/hexdump.mod | grep -q "grub_mod_init" \
	  || { printf 'FAIL: grub_mod_init missing in module\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_register" \
	  || { printf 'FAIL: cmd_register symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mod_load" \
	  || { printf 'FAIL: mod_load symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_insmod" \
	  || { printf 'FAIL: insmod command missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "cmd_lsmod" \
	  || { printf 'FAIL: lsmod command missing\n' >&2; exit 1; }
	@grub-script-check grub.cfg \
	  || { printf 'FAIL: grub.cfg syntax\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b19.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract/boot/grub
	@xorriso -osirrox on -indev $(BUILD)/b19.img \
	  -extract /boot/hexdump.mod $(BUILD)/extract/boot/hexdump.mod >/dev/null 2>&1
	@cmp $(BUILD)/extract/boot/hexdump.mod $(BUILD)/hexdump.mod \
	  || { printf 'FAIL: extracted hexdump.mod differs\n' >&2; exit 1; }
	@printf 'B19 check PASS: hexdump.mod is ET_REL, module loader symbols present\n'

run: $(BUILD)/b19.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b19.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
