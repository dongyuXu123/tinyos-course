# Lesson B20: VBE framebuffer —— El Torito 光盘镜像（核心 + VBE 模式设置 + LFB 测试图案）
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

all: $(BUILD)/b20.img

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/vbe.o: vbe.c vbe.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/loader.o: loader.c vbe.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext $(BOOT_BASE) --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE1_BYTES) $@

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o $(BUILD)/vbe.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o $(BUILD)/vbe.o

$(BUILD)/boot.bin: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s $$(( ($$(stat -c%s $@) + 2047) / 2048 * 2048 )) $@

# ---- CD 树（B20 无文件系统需求，仅引导镜像）-------------------------------
$(BUILD)/cdroot:
	mkdir -p $@

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b20.img: $(BUILD)/cdroot/BOOT.BIN
	xorriso -as mkisofs -quiet -V B20TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  -o $@ $(BUILD)/cdroot

# ---- 主机侧 vbe.c 单元测试 --------------------------------------------------
$(BUILD)/test_vbe: test_vbe.c vbe.c vbe.h | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_vbe.c vbe.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/b20.img $(BUILD)/test_vbe
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_vbe \
	  || { printf 'FAIL: vbe unit test\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "vbe_get_info" \
	  || { printf 'FAIL: vbe_get_info symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "vbe_find_mode" \
	  || { printf 'FAIL: vbe_find_mode symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "vbe_set_mode" \
	  || { printf 'FAIL: vbe_set_mode symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/vbe.o | grep -q "vbe_parse_mode_info" \
	  || { printf 'FAIL: vbe_parse_mode_info symbol missing\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b20.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@printf 'B20 check PASS: VBE parse/matches/fb unit tests + symbols present\n'

run: $(BUILD)/b20.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b20.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
