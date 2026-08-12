# Lesson B02: 两段式引导
AS := as
LD := ld
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘 = 80 柱面 * 2 磁头 * 18 扇区
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))

.PHONY: all check run clean

all: $(BUILD)/b02.img

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.o: stage1.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage2.o: stage2.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.o
	$(LD) -m elf_i386 -N -e stage2_entry -Ttext 0x7E00 --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE2_BYTES) $@   # 补齐到 8 个扇区（stage1 按 8 扇区读入）

check: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	@test "$$(stat -c%s $(BUILD)/stage1.bin)" -eq 512 \
	  || { printf 'FAIL: stage1 size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/stage1.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: stage1 missing 0xAA55 signature\n' >&2; exit 1; }
	@test "$$(stat -c%s $(BUILD)/stage2.bin)" -eq $(STAGE2_BYTES) \
	  || { printf 'FAIL: stage2 size != %s bytes\n' "$(STAGE2_BYTES)" >&2; exit 1; }
	@objdump -t $(BUILD)/stage2.o | grep -q "stage2_entry" \
	  || { printf 'FAIL: stage2_entry symbol missing\n' >&2; exit 1; }
	@printf 'B02 check PASS: stage1=512+55aa stage2=%sB entry=stage2_entry\n' \
	  "$(STAGE2_BYTES)"

$(BUILD)/b02.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none

run: $(BUILD)/b02.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b02.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
