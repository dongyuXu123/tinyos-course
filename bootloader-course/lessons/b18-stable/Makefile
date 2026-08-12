# Lesson B18: menuentry 菜单与 timeout/default —— El Torito 光盘镜像
# 两个镜像：b18.img（timeout=3 交互菜单）与 b18-t0.img（timeout=0 自动启动）
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

.PHONY: all check run run-t0 clean

all: $(BUILD)/b18.img $(BUILD)/b18-t0.img

$(BUILD):
	mkdir -p $@

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

$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

# ---- 两个 CD 树（仅 grub.cfg 不同）------------------------------------------
$(BUILD)/cdroot:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot-t0:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot-t0/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot-t0
	cp $< $@

$(BUILD)/cdroot/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot-t0/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot-t0
	cp $< $@

$(BUILD)/cdroot/boot/grub/grub.cfg: grub.cfg | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot-t0/boot/grub/grub.cfg: grub-t0.cfg | $(BUILD)/cdroot-t0
	cp $< $@

$(BUILD)/b18.img: $(BUILD)/boot.bin $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/boot/kernel.elf \
                  $(BUILD)/cdroot/boot/grub/grub.cfg
	xorriso -as mkisofs -quiet -V B18TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

$(BUILD)/b18-t0.img: $(BUILD)/boot.bin $(BUILD)/cdroot-t0/BOOT.BIN \
                     $(BUILD)/cdroot-t0/boot/kernel.elf \
                     $(BUILD)/cdroot-t0/boot/grub/grub.cfg
	xorriso -as mkisofs -quiet -V B18T0 -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot-t0

# ---- 主机侧 tokenizer 单元测试 ----------------------------------------------
$(BUILD)/test_script: test_script.c script.c | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_script.c script.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/kernel.elf $(BUILD)/b18.img $(BUILD)/b18-t0.img \
       $(BUILD)/test_script
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_script \
	  || { printf 'FAIL: tokenizer unit test\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "menu_add" \
	  || { printf 'FAIL: menu_add symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "menu_run" \
	  || { printf 'FAIL: menu_run symbol missing\n' >&2; exit 1; }
	@test "$$(grep -c '^menuentry' grub.cfg)" -ge 2 \
	  || { printf 'FAIL: grub.cfg needs >= 2 menuentry\n' >&2; exit 1; }
	@grub-script-check grub.cfg && grub-script-check grub-t0.cfg \
	  || { printf 'FAIL: grub.cfg syntax (grub-script-check)\n' >&2; exit 1; }
	@grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf \
	  || { printf 'FAIL: KERNEL.ELF not a valid Multiboot2 kernel\n' >&2; exit 1; }
	@rm -rf $(BUILD)/extract && mkdir -p $(BUILD)/extract
	@xorriso -osirrox on -indev $(BUILD)/b18.img \
	  -extract /boot/grub/grub.cfg $(BUILD)/extract/grub.cfg >/dev/null 2>&1
	@cmp $(BUILD)/extract/grub.cfg grub.cfg \
	  || { printf 'FAIL: extracted grub.cfg differs\n' >&2; exit 1; }
	@xorriso -osirrox on -indev $(BUILD)/b18-t0.img \
	  -extract /boot/grub/grub.cfg $(BUILD)/extract/grub-t0.cfg >/dev/null 2>&1
	@cmp $(BUILD)/extract/grub-t0.cfg grub-t0.cfg \
	  || { printf 'FAIL: extracted grub-t0.cfg differs\n' >&2; exit 1; }
	@printf 'B18 check PASS: menu_add/menu_run present, 2 menuentries, cfg byte-identical\n'

run: $(BUILD)/b18.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b18.img \
	  -boot order=d -no-reboot -no-shutdown

run-t0: $(BUILD)/b18-t0.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b18-t0.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
