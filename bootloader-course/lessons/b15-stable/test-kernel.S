# Lesson B06/B08: test-kernel — 最小 Multiboot2 测试内核
#
# 本文件是 Mini-GRUB 的自写测试内核：
#   - B06 用它练习 ELF32 解析（不运行）；
#   - B07 用它练习 Multiboot2 header 校验（不运行）；
#   - B08 真正装载它并跳转（运行，打印欢迎消息后挂起）。
# header 布局对照 TinyOS 主线的 boot.S（lessons/lesson-01-stable/boot.S）。

.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_end - mb2_start
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)

.section .multiboot, "a"
.align 8
mb2_start:
    .long MB2_HEADER_MAGIC       /* magic */
    .long MB2_ARCHITECTURE_I386  /* architecture = i386 */
    .long MB2_HEADER_LENGTH      /* header 总长度 */
    .long MB2_CHECKSUM           /* 四字段求和为 0 */
    .short 0                     /* end tag: type 0 */
    .short 0                     /* end tag: flags */
    .long 8                      /* end tag: size */
mb2_end:

.section .text
.globl _start
.code32
_start:
    cli
    # 直接写 VGA 文本内存（保护模式，loader 已建好环境）
    movl $0xB8000, %edi
    movl $msg, %esi
1:  lodsb
    testb %al, %al
    jz 2f
    movb %al, (%edi)
    movb $0x1F, 1(%edi)      # 白字蓝底
    addl $2, %edi
    jmp 1b
2:  hlt
    jmp 2b

msg:
    .asciz "B08 test-kernel: hello from Multiboot2"

# 数据与 BSS：制造第二个 PT_LOAD（RW）段，练习 filesz < memsz 的装载语义
.section .data
.align 4
kernel_data:
    .long 0x11223344

.section .bss
.align 4
kernel_bss:
    .space 1024
