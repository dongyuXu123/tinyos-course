# Lesson B19: 模块系统（核心 + 按需加载）— 精讲文档

> **课号**：Lesson B19（Mini-GRUB 从零写 GRUB 课程第 19 课，阶段五第 1 课）
> **主题**：自定义 .mod 模块格式、运行时加载与重定位、命令由模块注册
> **课程位置**：阶段五「模块系统与图形」第 1 课
> **前置课程**：[`b18-stable/README.md`](../b18-stable/README.md)（menuentry 菜单）
> **后续课程**：[`b20-stable/README.md`](../b20-stable/README.md)（VBE framebuffer）
> **一句话目标**：核心镜像只带最小命令集，其余功能以 `.mod` 模块从磁盘加载
> 注册——复刻 GRUB「最小核心 + 297 个 .mod 按需加载」的架构思想。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能读取一个自定义格式的模块文件（
`hexdump.mod`），把它搬到内存、解析符号、应用重定位、调用模块入口注册新命令
（`hexdump`）——核心代码不再需要把全部功能焊死。

- **在课程中的位置**：研读支线 0.1 里 GRUB 的 297 个 `.mod` 是它可扩展性的
  核心。B16–B18 的命令（`echo`/`set`/`insmod`…）都焊在核心镜像里；本课把它们
  拆出去，理解「核心 + 模块」的工程权衡。对照 `grub-core/kern/dl.c` 的
  `grub_dl_load`（节搬移 + 重定位 + 符号解析）与 `include/grub/dl.h`
  （`struct grub_dl` 与导出符号）。
- **前置知识清单**：
  1. B16：命令注册表（模块最终调 `cmd_register` 注册自己的命令）；
  2. B06：ELF 结构（模块是 ELF32 可重定位目标文件，`ET_REL`）；
  3. B14：`file_open`/`file_read`（模块文件从 CD 读取）。
- **本课交付**：`build/b19.img`（CD：核心 + `boot/hexdump.mod` + grub.cfg）；
  QEMU 上脚本演示模块生命周期：`hexdump` 未加载报错 → `insmod` 加载 →
  `lsmod` 列出 → `hexdump` 可用（dump 出 kernel.elf 的 ELF magic）→ `halt`。

---

## 2. 核心概念精讲

### 2.1 概念一：模块格式 = ET_REL 可重定位 ELF

GRUB 的模块是**可重定位目标文件**（`ET_REL`），不是可执行文件
（`ET_EXEC`）。两者区别：

| | ET_EXEC（B06 内核） | ET_REL（本课模块） |
|---|---|---|
| 段地址 | 链接时已定（如 1M） | 未定（`sh_addr=0`），加载时才铺排 |
| 引用外部符号 | 已全部解析 | 留 `R_386_32`/`R_386_PC32` 重定位项 |
| 入口 | `e_entry` | 无单一入口，约定 `grub_mod_init` |

本课 `mod/hexdump.c` 用 `gcc -m32 -ffreestanding -Os -c` 编译，不做链接，
产物就是 ET_REL——`make check` 用 `readelf -h` 断言 `Type: REL`。

### 2.2 概念二：加载 = 节搬移 + 重定位

`mod_load()` 分四步（对照 `dl.c` 的 `grub_dl_load`）：

1. **读文件**：`file_open`/`file_read` 把模块读进 `CD_BUF_MOD`（0x6C000）；
2. **铺排 SHF_ALLOC 节**：凡 `sh_flags & SHF_ALLOC`（`.text`/`.data`/
   `.rodata`/`.bss`）顺序拷贝到 `MODULE_BASE`（0x200000）起的内存，
   `.bss`（`SHT_NOBITS`）清零，非 ALLOC 节（符号表、重定位表）不搬；
3. **应用重定位**：对每个 `SHT_REL` 节，按 `r_info` 拆出符号下标与重定位
   类型（`ELF32_R_SYM`/`ELF32_R_TYPE`），算出符号地址后回填：
   ```c
   if (type == R_386_32)        *loc += sym_addr;              /* 绝对地址 */
   else if (type == R_386_PC32) *loc += sym_addr - (u32)loc;    /* PC 相对 */
   ```
4. **调用入口**：在符号表里找 `grub_mod_init`（本模块定义的符号），取它的
   加载后地址调用——模块在此注册命令。

### 2.3 概念三：核心导出符号表

模块里引用 `cmd_register`/`vga_puts`/`file_open` 等核心函数时，符号是
`SHN_UNDEF`（未定义）。核心侧维护一张导出表（GRUB 的 `grub_symbol` 表）：

```c
static const struct core_symbol core_syms[] = {
    { "cmd_register", (void *)(u32)cmd_register },
    { "vga_puts",     (void *)(u32)vga_puts },
    { "vga_hex",      (void *)(u32)vga_hex },
    { "file_open",    (void *)(u32)file_open },
    ...
};
```

重定位遇到 `SHN_UNDEF` 符号时查这张表；查不到 → 打印
`B19 mod: undefined symbol: <name>` 并加载失败（错误路径 B22 再展开）。

### 2.4 概念四：模块入口约定（grub_mod_init / grub_mod_fini）

GRUB 约定模块导出两个入口：`grub_mod_init`（初始化，通常注册命令）与
`grub_mod_fini`（卸载清理）。本课模块 `hexdump.c`：

```c
int grub_mod_init(void) { cmd_register(&cmd_hexdump); return 0; }
void grub_mod_fini(void) { }
```

核心加载完模块后**按名字**在模块符号表里找 `grub_mod_init` 并调用——
模块的 `cmd_register` 调用经由重定位指向核心的注册表，注册即生效。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B18 的增量 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导 + BIOS 回调 | 消息文本变化 |
| `loader.c` | 核心导出表 + `mod_load`（节搬移/重定位/入口）+ `insmod`/`lsmod` | 重写模块部分 |
| `script.c`/`test_script.c` | tokenizer（B17） | 不变 |
| `mod/hexdump.c` | 示例模块：`hexdump` 命令（ET_REL） | 新增 |
| `grub.cfg` | 模块生命周期演示脚本 | 新增 |
| `test-kernel.S`/`.ld` | B08 测试内核（hexdump 的演示文件） | B15 复用 |
| `build/hexdump.mod` | 模块产物（ET_REL） | 新增 |

### 3.2 核心导出表与符号查找

```c
static __attribute__((noinline)) void *core_sym_lookup(const char *name)
{
    ... 遍历 core_syms[]，名字相等返回地址，否则 0 ...
}
```

`noinline` 是教学保护：`-Os` 会把小函数内联进调用者，符号从 `objdump -t`
消失，`make check` 的符号断言会失败（B17 踩过同样的坑）。

### 3.3 mod_load 四步实现

```c
static __attribute__((noinline)) int mod_load(const char *path)
{
    /* 1. 读取模块文件到 CD_BUF_MOD，校验 ELF magic 与 e_type == ET_REL */
    /* 2. 遍历节：SHF_ALLOC 节按 16 字节对齐铺到 MODULE_BASE，BSS 清零，
           非 ALLOC 节（symtab/strtab/rel）留读缓冲 */
    /* 3. 找 SHT_SYMTAB（符号表 + 经 sh_link 的字符串表），
           再遍历 SHT_REL 节逐个重定位：
             - 符号 st_shndx == SHN_UNDEF -> core_sym_lookup（核心导出）
             - st_shndx == SHN_ABS      -> st_value（绝对常量）
             - st_shndx < 16            -> 本模块节地址 + st_value
             - R_386_32 / R_386_PC32 两种类型，其他类型报错 */
    /* 4. 登记 loaded_mods（lsmod 用），按名字找 grub_mod_init 并调用 */
}
```

模块加载区选 `MODULE_BASE = 0x200000`（2MB）：避开核心（0x8400）、CD 缓冲
（0x68000–0x6C000）、内核（1MB 起）。模块 ≤ 4KB（`MOD_FILE_MAX`），最多
16 个节、8 个已加载模块（固定槽位，无 malloc——简化边界）。

### 3.4 命令：insmod / lsmod

```c
static int cmd_insmod_fn(int argc, char **argv)
{
    int r = mod_load(argv[1]);          /* 返回模块基址，负值失败 */
    if (r < 0) { vga_puts("B19 error: insmod failed: "); ... }
    vga_puts("B19 mod: insmod "); ... vga_hex((u32)r, 8); ...   /* 打印基址 */
}
static int cmd_lsmod_fn(...)            /* 遍历 loaded_head 链表打印 name/base */
```

`cmd_insmod`/`cmd_lsmod` 与 `echo`/`set`/`halt` 一起在 `loader_main` 里注册；
模块加载后注册的 `hexdump` 走同一个命令注册表（B16），`cmd_execute` 自然
找到它。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2 -> loader_main:
  挂载 (cd0) -> 注册核心命令 (echo/set/insmod/lsmod/halt)
  -> script_run_file("/boot/grub/grub.cfg"):
      hexdump /boot/kernel.elf          # 未加载 -> command not found
      insmod /boot/hexdump.mod          # 读 ELF -> 铺节 -> 重定位 -> grub_mod_init
                                        #   -> cmd_register(&cmd_hexdump) 生效
      lsmod                             # B19 lsmod: /boot/hexdump.mod base=00200000
      hexdump /boot/kernel.elf          # 现在可用：dump 前 48 字节
                                        #   7f 45 4c 46 ...（ELF magic）
      halt                              # B19: halted
```

QEMU VGA 实测输出：

```
B19 mod: Mini-GRUB module system
B19 mod: boot drive = e0
B19 script: module demo
B19 error: command not found: hexdump        <- insmod 前不可用
B19 mod: insmod /boot/hexdump.mod -> base=00200000
B19 lsmod: /boot/hexdump.mod base=00200000
B19 hexdump: /boot/kernel.elf:
7f 45 4c 46 01 01 01 00                          <- ELF magic + class
00 00 00 00 00 00 00 00
B19: halted
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make              # 构建 b19.img（核心 + hexdump.mod + kernel.elf + grub.cfg）
make check        # tokenizer 单测 + ET_REL 断言 + 核心导出符号 + grub-script-check
make run          # QEMU 打开窗口，脚本自动演示模块生命周期
./scripts/validate-course.sh b19 check
./scripts/validate-course.sh b19 qemu    # 自动 VGA 文本校验 8 个 marker
```

### 5.2 成功判据

1. `make check` 全绿：`hexdump.mod` 是 ET_REL（`LC_ALL=C readelf -h`）、
   `grub_mod_init` 在模块里、`core_sym_lookup`/`mod_load`/`cmd_insmod`/
   `cmd_lsmod` 符号存在（`noinline` 保护）、`grub.cfg` 过 `grub-script-check`、
   从 ISO 抽取的 `hexdump.mod` 与源文件字节一致；
2. QEMU：`hexdump` 命令**未加载时报错、insmod 后可用**——模块系统闭环；
3. `lsmod` 列出模块路径与加载基址（`00200000`）。

---

## 6. 调试地图

1. **`-Os` 内联吞掉符号**：`mod_load`/`core_sym_lookup`/`name_eq` 是静态
   函数，`-Os` 内联进调用者后 `objdump -t` 找不到 → `make check` 失败。
   解法：`static __attribute__((noinline))`。注意 attribute 要放在
   `static` 之后、函数名之前（`static int f(...) __attribute__` 在定义处
   会报 "attributes should be specified before the declarator"）。
2. **属性声明加了分号**：`static int f(...) __attribute__((noinline));`
   再接函数体 `{...}` 会得到重复大括号/孤立 `{` 的编译错误——声明式结尾
   的分号要删掉，属性直接贴在定义上。
3. **重定位类型不认识**：`mod_load` 打印 `B19 mod: unsupported relocation
   type <hex>` 并失败——先 `objdump -r hexdump.mod` 看实际类型，确认
   `-fno-pie`（否则出现 `R_386_PC32` 之外的 32 位 GOT/PLT 重定位）。
4. **未定义符号**：打印 `B19 mod: undefined symbol: <name>`——模块声明了
   核心没有导出的函数，或核心导出表少了一行；对照 `core_syms[]` 与模块里
   `extern` 声明逐一核对。
5. **模块覆盖内存**：加载区选错（如 < 1MB）会踩到核心/缓冲/内核——固定
   `MODULE_BASE=0x200000` 并检查节总长 < `MODULE_MAX`。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `mod_load`（节搬移+重定位+入口） | `kern/dl.c` 的 `grub_dl_load` | 无压缩/依赖解析/上下文 |
| `core_syms[]` 导出表 | `include/grub/dl.h` `grub_symbol` 表 | 静态表 vs 动态登记 |
| `R_386_32`/`R_386_PC32` | `kern/i386/dl.c` 重定位表 | 只支持两种最常用类型 |
| `grub_mod_init`/`grub_mod_fini` | `include/grub/dl.h` 入口约定 | 一致 |
| `insmod`/`lsmod` 命令 | `commands/insmod.c`、`commands/lsmod.c` | 无 `rmmod`/依赖解析 |
| ET_REL 模块编译 | `grub-mkimage` 模块模型 | 本课不做打进 core image |

---

## 8. 思考题与练习

1. 模块 `hexdump.c` 里重复声明了核心的 `struct grub_file`/`struct cmd`——
   如果核心改了字段顺序，模块会悄悄出错。设计一个"版本/布局校验"机制
   （GRUB 用共享头文件解决）。
2. 实现 `rmmod`：卸载模块并调用 `grub_mod_fini`（从命令链表摘除、释放槽位）。
3. 给 `insmod` 加依赖解析：模块 A 引用模块 B 导出的符号时，能否先加载 B
   再解析 A？（提示：符号解析失败时暂存重定位，等 B 加载后补解析——GRUB 的
   `grub_dl_add` 就是这么做的。）
4. 把 `mod_load` 支持 `R_386_PC16`/`R_386_16`（16 位重定位）——想一想为什么
   内核模块一般用不到它们。
5. 用 `objdump -r -t build/hexdump.mod` 分析模块的符号与重定位表，列出
   每一条 `SHN_UNDEF` 符号对应的核心导出名，验证 `core_syms[]` 覆盖齐全。

---

## 9. 本课小结与下一课预告

**小结**：本课实现 Mini-GRUB 自己的模块系统——ET_REL 模块格式、核心导出
符号表、`mod_load` 的节搬移 + 重定位 + 入口调用、`insmod`/`lsmod` 命令。
QEMU 上完整演示了「未加载报错 → insmod 生效 → lsmod 可见 → 命令可用」的
闭环，`make check` 断言模块是 ET_REL 且核心符号齐全。阶段五开始，功能开始
从核心剥离、按需装配。

**下一课** [`b20-stable/README.md`](../b20-stable/README.md)：模块系统让功能
可插拔，图形就是最典型的需求——B20 用 VBE BIOS 扩展（INT 10 4F01/4F02）
设置 800x600x32 framebuffer，对照 GRUB `video/i386_pc/vbe.c`。
