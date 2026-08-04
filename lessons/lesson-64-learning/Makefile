CC := gcc
LD := ld
OBJCOPY := objcopy
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
CFLAGS64 := -m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib
BUILD := build
ISO_ROOT := $(BUILD)/iso

.PHONY: all check run clean
all: $(BUILD)/kernel.iso
$(BUILD):
	mkdir -p $@
$(BUILD)/kernel64.o: kernel64.c | $(BUILD)
	$(CC) $(CFLAGS64) -c $< -o $@
$(BUILD)/kernel64.bin: $(BUILD)/kernel64.o kernel64.ld
	$(LD) -m elf_x86_64 -T kernel64.ld -nostdlib -o $(BUILD)/kernel64.elf $(BUILD)/kernel64.o
	$(OBJCOPY) -O binary $(BUILD)/kernel64.elf $@
$(BUILD)/boot.o: boot.S $(BUILD)/kernel64.bin | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel.o: kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel.elf: $(BUILD)/boot.o $(BUILD)/kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot.o $(BUILD)/kernel.o
$(ISO_ROOT)/boot/grub/grub.cfg: grub.cfg
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $< $@
$(ISO_ROOT)/boot/kernel.elf: $(BUILD)/kernel.elf
	mkdir -p $(ISO_ROOT)/boot
	cp $< $@
$(BUILD)/kernel.iso: $(ISO_ROOT)/boot/kernel.elf $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT)
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '窗口、widget 与事件分发' README.md
	@grep -q 'gui' README.md
	@grep -q 'Lesson 64' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 64 checks passed.'
run: $(BUILD)/kernel.iso
	qemu-system-x86_64 -accel tcg -boot order=d -cdrom $(BUILD)/kernel.iso -serial stdio -no-reboot -no-shutdown
clean:
	rm -rf $(BUILD)
