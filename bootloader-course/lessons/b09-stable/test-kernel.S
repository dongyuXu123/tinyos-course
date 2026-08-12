# Lesson B09: test-kernel — Multiboot2 测试内核（C 版，MBI walker）
#
# 自写测试内核升级为 C：入口在汇编（建栈 + 传参），逻辑在 C
# （kernel_main 检查 magic 并遍历 MBI tag 链），结构对照 TinyOS 主线。
# B09 的 loader 会传一个带 type-2（boot loader name）tag 的 MBI，
# 本内核 walker 逐个打印 tag，直到 end tag。

.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_end - mb2_start
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)

.section .multiboot, "a"
.align 8
mb2_start:
    .long MB2_HEADER_MAGIC
    .long MB2_ARCHITECTURE_I386
    .long MB2_HEADER_LENGTH
    .long MB2_CHECKSUM
    .short 0                     /* end tag: type 0 */
    .short 0
    .long 8
mb2_end:

.section .text
.globl _start
.code32
_start:
    cli
    movl $stack_top, %esp        # 内核自建栈（loader 不提供）
    pushl %ebx                   # mbi 地址
    pushl %eax                   # magic
    call kernel_main             # void kernel_main(u32 magic, u32 mbi)
1:  hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
    .skip 4096
stack_top:
