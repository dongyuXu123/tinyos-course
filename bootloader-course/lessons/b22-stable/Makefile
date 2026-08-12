# Lesson B22: 故障调试与 rescue —— El Torito 光盘镜像（错误路径演示 + rescue）
# FAULT 注入：构建坏镜像变体（只在临时副本执行，绝不改写 stable 产物）：
#   FAULT=bad-elf      -> 把 kernel.elf 第一个字节改成 0xFF（坏 magic）
#   FAULT=bad-header   -> 把 kernel.elf 的 mb2 header checksum 改坏
#   FAULT=missing      -> 移除一个文件（grub.cfg 引用不存在文件）
#   FAULT=no-cfg       -> 不装 grub.cfg（loader 直接进 rescue>）
# 验证脚本在临时目录分别构建这些变体并 QEMU 验证错误消息与 rescue 提示符。
AS := as
CC := gcc
LD := ld
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
BUILD := build
BOOT_BASE := 0x7C00
STAGE1_BYTES := 2048
STAGE2_BASE := 0x8400
MAX_STAGE2 := 0x7000
FAULT :=

.PHONY: all check run run-rescue clean

all: $(BUILD)/b22.img $(BUILD)/b22-rescue.img

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

# ---- 测试内核（B08 复用）----------------------------------------------------
$(BUILD)/kernel.elf: test-kernel.S test-kernel.ld | $(BUILD)
	$(AS) --32 -o $(BUILD)/test-kernel.o test-kernel.S
	$(LD) -m elf_i386 -T test-kernel.ld -nostdlib --build-id=none \
	  -o $@ $(BUILD)/test-kernel.o

# ---- 故障注入变体（在 cdroot 上派生，不改源文件）-----------------------------
$(BUILD)/bad.elf: $(BUILD)/kernel.elf
	cp $(BUILD)/kernel.elf $@
	python3 -c "d=bytearray(open('$@','rb').read()); d[0]=0xff; open('$@','wb').write(bytes(d))"

$(BUILD)/badheader.elf: $(BUILD)/kernel.elf
	cp $(BUILD)/kernel.elf $@
	# mb2 header 在 ELF 文件偏移 0x1000（.multiboot 段）：
	# checksum @ 0x1000+12；改成 0 使 magic+arch+len+checksum != 0
	python3 -c "d=bytearray(open('$@','rb').read()); d[0x100c:0x1010]=b'\x00\x00\x00\x00'; open('$@','wb').write(bytes(d))"

# ---- CD 树（正常 + rescue 变体）---------------------------------------------
$(BUILD)/cdroot:
	mkdir -p $@/boot/grub

$(BUILD)/cdroot/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/bad.elf: $(BUILD)/bad.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/badheader.elf: $(BUILD)/badheader.elf | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/cdroot/boot/grub/grub.cfg: grub.cfg | $(BUILD)/cdroot
	cp $< $@

$(BUILD)/b22.img: $(BUILD)/cdroot/BOOT.BIN \
                  $(BUILD)/cdroot/boot/kernel.elf \
                  $(BUILD)/cdroot/boot/bad.elf \
                  $(BUILD)/cdroot/boot/badheader.elf \
                  $(BUILD)/cdroot/boot/grub/grub.cfg
	# -relaxed-filenames：ISO9660 Level 1 默认 8.3 大写字幕会截断长名
	# （badheader.elf -> badheade.elf），真 GRUB 用 Rock Ridge/宽松名保留真名
	xorriso -as mkisofs -quiet -V B22TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table -iso-level 3 -relaxed-filenames \
	  -o $@ $(BUILD)/cdroot

# ---- rescue 变体：没有 grub.cfg，loader 直接进 rescue> ----------------------
$(BUILD)/cdroot-rescue:
	mkdir -p $@/boot

$(BUILD)/cdroot-rescue/BOOT.BIN: $(BUILD)/boot.bin | $(BUILD)/cdroot-rescue
	cp $< $@

$(BUILD)/cdroot-rescue/boot/kernel.elf: $(BUILD)/kernel.elf | $(BUILD)/cdroot-rescue
	cp $< $@

$(BUILD)/b22-rescue.img: $(BUILD)/cdroot-rescue/BOOT.BIN \
                         $(BUILD)/cdroot-rescue/boot/kernel.elf
	xorriso -as mkisofs -quiet -V B22RESCUE -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table -iso-level 3 -relaxed-filenames \
	  -o $@ $(BUILD)/cdroot-rescue

# ---- 主机侧 tokenizer 单元测试（B17 复用）------------------------------------
$(BUILD)/test_script: test_script.c script.c | $(BUILD)
	$(CC) -Wall -Wextra -o $@ test_script.c script.c

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/boot.bin \
       $(BUILD)/b22.img $(BUILD)/b22-rescue.img $(BUILD)/test_script
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq $(STAGE1_BYTES) \
	  || { printf 'FAIL: stage1 size != %s\n' '$(STAGE1_BYTES)' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -lt $$(( $(MAX_STAGE2) )) \
	  || { printf 'FAIL: stage2 too big (> %s)\n' '$(MAX_STAGE2)' >&2; exit 1; }
	@test "$$(( $$(stat -c%s $(BUILD)/boot.bin) % 2048 ))" -eq 0 \
	  || { printf 'FAIL: boot.bin not 2048-aligned\n' >&2; exit 1; }
	@$(BUILD)/test_script >/dev/null \
	  || { printf 'FAIL: tokenizer unit test\n' >&2; exit 1; }
	# 错误处理符号与五类错误消息
	@objdump -t $(BUILD)/loader.o | grep -q "err_set" \
	  || { printf 'FAIL: err_set symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "err_print" \
	  || { printf 'FAIL: err_print symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "rescue_shell" \
	  || { printf 'FAIL: rescue_shell symbol missing\n' >&2; exit 1; }
	@objdump -t $(BUILD)/loader.o | grep -q "mb2_header_check" \
	  || { printf 'FAIL: mb2_header_check symbol missing\n' >&2; exit 1; }
	@for m in "invalid ELF header" "invalid multiboot2 header" \
	          "file not found" "command not found" "config file not found"; do \
	  strings $(BUILD)/loader.o | grep -qF "$$m" \
	    || { printf 'FAIL: missing error message: %s\n' "$$m" >&2; exit 1; }; \
	done
	# 坏 ELF 变体：第一个字节不是 0x7F；坏 header 变体：checksum 被破坏
	@test "$$(od -An -tx1 -N1 $(BUILD)/bad.elf | tr -d ' ')" != "7f" \
	  || { printf 'FAIL: bad.elf not corrupted\n' >&2; exit 1; }
	@python3 -c "import struct;d=open('$(BUILD)/badheader.elf','rb').read();m,a,l,c=struct.unpack_from('<IIII',d,0x1000);assert (m+a+l+c)!=0,'checksum intact'" \
	  || { printf 'FAIL: badheader.elf checksum not corrupted\n' >&2; exit 1; }
	@grub-script-check grub.cfg \
	  || { printf 'FAIL: grub.cfg syntax\n' >&2; exit 1; }
	@xorriso -indev $(BUILD)/b22.img -report_el_torito 2>/dev/null \
	  | grep -q "boot-info-table" \
	  || { printf 'FAIL: El Torito record missing boot-info-table\n' >&2; exit 1; }
	@printf 'B22 check PASS: err_set/err_print/rescue_shell symbols + 5 error messages + fault variants\n'

run: $(BUILD)/b22.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b22.img \
	  -boot order=d -no-reboot -no-shutdown

run-rescue: $(BUILD)/b22-rescue.img
	qemu-system-x86_64 -accel tcg -vga std -cdrom $(BUILD)/b22-rescue.img \
	  -boot order=d -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
