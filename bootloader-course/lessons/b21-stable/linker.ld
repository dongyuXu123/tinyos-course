/* Lesson B13: stage2 链接脚本
 *
 * stage2（stage2.S + loader.c）由 SeaBIOS 连同 stage1 一起作为 El Torito
 * 引导镜像连续加载，stage1 在 0x7C00，本文件从 0x8400（= 0x7C00 + 2048）
 * 开始铺排。实模式代码、保护模式代码与 C 代码按顺序排列，C 的字符串常量
 * 放 .rodata，全局变量放 .data/.bss，全部跟随在 .text 之后（不额外对齐，
 * 避免二进制输出出现大段空洞导致镜像体积膨胀）。
 */
ENTRY(stage2_entry)
SECTIONS
{
    . = 0x8400;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
