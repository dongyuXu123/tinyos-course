/* Lesson B04: stage2 链接脚本
 *
 * stage2 由 stage1 读入 0x7E00，实模式代码、保护模式代码与 C 代码
 * （loader.c）按顺序铺在从 0x7E00 开始的物理地址上。C 的字符串常量放
 * .rodata，全局变量放 .data/.bss，全部跟随在 .text 之后（不额外对齐，
 * 避免二进制输出出现大段空洞导致超出 stage1 读取的扇区数）。
 */
ENTRY(stage2_entry)
SECTIONS
{
    . = 0x7E00;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
