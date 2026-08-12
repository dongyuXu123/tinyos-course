# Lesson B02: stage2 — 由 stage1 读入 0x7E00 的第二段代码
#
# stage2 仍运行在实模式（B03 才切保护模式）。本课它只打印一条消息证明：
#   - stage1 的 INT 13 读盘成功（字节完整到达 0x7E00）；
#   - 远跳 ljmp $0x0000, $0x7E00 成功（CS=0、IP=0x7E00）。
# 参照：grub-core/boot/i386/pc/diskboot.S（core image 第一段的角色）。

.code16
.section .text
.globl stage2_entry

stage2_entry:
    # DL = 启动盘号（stage1 原样传过来；本课未用到，B05 起需要）
    movw $msg_stage2, %si
    call puts
1:  hlt
    jmp 1b

puts:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movb $0x00, %bh
    int $0x10
    jmp puts
2:  ret

msg_stage2:
    .asciz "B02 stage2: loaded at 0x7E00, two-stage boot OK"
