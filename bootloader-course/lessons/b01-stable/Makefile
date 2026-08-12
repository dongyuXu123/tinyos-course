# Lesson B01: 512 字节引导扇区 Hello
AS := as
LD := ld
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘 = 80 柱面 * 2 磁头 * 18 扇区

.PHONY: all check run clean

all: $(BUILD)/b01.img

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: boot.S | $(BUILD)
	$(AS) --32 -o $@ $<

$(BUILD)/boot.bin: $(BUILD)/boot.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<

check: $(BUILD)/boot.bin
	@test "$$(stat -c%s $(BUILD)/boot.bin)" -eq 512 \
	  || { printf 'FAIL: boot.bin size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/boot.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: missing 0xAA55 signature\n' >&2; exit 1; }
	@printf 'B01 check PASS: size=512 signature=55aa\n'

$(BUILD)/b01.img: $(BUILD)/boot.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/boot.bin of=$@ conv=notrunc status=none

run: $(BUILD)/b01.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b01.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
