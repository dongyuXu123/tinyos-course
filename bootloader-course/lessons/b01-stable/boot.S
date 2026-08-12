# Lesson B01: 512 字节引导扇区 Hello
#
# BIOS 从软盘 LBA 0 把引导扇区读入物理 0x7C00，检查第 510/511 字节的 0x55AA
# 签名后，跳转到 0x0000:0x7C00 执行。本课只做两件事：初始化段寄存器，然后
# 用 BIOS INT 10 在 VGA 文本模式打印一行字符串。
# 参照：grub-core/boot/i386/pc/boot.S（GRUB 的 512 字节第一段）。
#
# 汇编语法：AT&T；实模式代码必须用 .code16。

.code16
.section .text
.globl _start

# ---- 常量 ----------------------------------------------------------------
.set BOOT_ADDR, 0x7C00      # BIOS 约定：引导扇区加载地址

# ---- 入口：BIOS 以 0x0000:0x7C00 跳入 -------------------------------------
_start:
    cli                     # 关中断：段寄存器设置期间不允许被打断
    xorw %ax, %ax
    movw %ax, %ds           # DS = 0，所有地址按 0x0000:offset 解释
    movw %ax, %es           # ES = 0
    movw %ax, %ss           # SS = 0（栈段）
    movw $BOOT_ADDR, %sp    # 栈顶 = 0x7C00，向下增长，避开 IVT/BDA
    sti

    movw $msg_hello, %si    # DS:SI -> 待打印字符串
    call puts

1:  hlt                     # 打印完成，挂起（没有操作系统可以"返回"）
    jmp 1b

# ---- puts: INT 10 逐字符打印以 0 结尾的字符串 ------------------------------
# 输入：DS:SI = 字符串指针；输出：无；破坏：AX、SI
puts:
    lodsb                   # AL = DS:[SI]，SI += 1
    testb %al, %al
    jz 2f                   # 遇到 '\0' 结束
    movb $0x0E, %ah         # INT 10 功能号 0x0E：teletype 输出
    movb $0x00, %bh         # 页号 0
    int $0x10               # BIOS 视频服务
    jmp puts
2:  ret

# ---- 数据 ----------------------------------------------------------------
msg_hello:
    .asciz "B01 Mini-GRUB stage1: 512-byte boot sector OK"

# ---- 引导签名 --------------------------------------------------------------
# BIOS 要求第 510/511 字节为 0x55 0xAA。若代码超过 510 字节，.fill 的填充数
# 为负数，汇编器直接报错——这本身就是一次内置的"扇区大小"检查。
    .fill 510 - (. - _start), 1, 0
    .word 0xAA55
