# Lesson B07: Multiboot2 header 校验 — 精讲文档

> **课号**：Lesson B07（Mini-GRUB 从零写 GRUB 课程第 7 课，可执行课）
> **主题**：Multiboot2 header：搜索范围、8 对齐、magic/arch/length/checksum、header tags
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 2 课
> **前置课程**：[`b06-stable/README.md`](../b06-stable/README.md)（ELF32 解析）；
> 研读支线 0.5（Multiboot2 header 校验与 ABI）
> **后续课程**：[`b08-stable/README.md`](../b08-stable/README.md)（PT_LOAD 装载与首个交接）
> **一句话目标**：在 ELF 镜像中定位并校验 Multiboot2 header，决定"这个内核能不能
> 按 Multiboot2 启动"，对合法/非法镜像分别给出明确判定。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能对任意镜像做 Multiboot2 header 校验，打印
每个校验项的通过/失败，并拒绝不合法的镜像。

- **在课程中的位置**：B06 能解析 ELF；但"能解析"不等于"能启动"。Multiboot2 是
  loader 与内核之间的协议契约：内核必须声明自己是 Multiboot2 客户。本课实现
  GRUB `loader/multiboot_mbi2.c` 的 `find_header` 语义，B08 才允许装载。
- **前置知识清单**：
  1. B06：ELF 文件读取与结构体解析；
  2. Multiboot2 规范：magic=0xe85250d6、arch=0、length、checksum，四字段求和为 0；
  3. 研读支线 0.5（header 搜索：前 32768 字节、8 字节对齐）。
- **本课交付**：`build/b07.img` 含 `ok.elf`（test-kernel）与 `bad.elf`（checksum
  被破坏的副本）；QEMU 上两个镜像分别打印"通过"与"拒绝(code=04)"。

---

## 2. 核心概念精讲

### 2.1 概念一：搜索范围与对齐

**定义**：Multiboot2 header 必须完全落在镜像**前 32768 字节**内且 **8 字节对齐**。

**为什么需要**：内核镜像开头可能有 ELF 头、压缩数据等任意字节，header 位置不固定，
loader 必须扫描；限制范围与对齐保证扫描有界且规范兼容。

**工作机制**（GRUB `find_header` 的循环，本课镜像）：

```c
    for (off = 0; off + 16u <= limit; off += MB2_HEADER_ALIGN) {
        const struct mb2_header *h =
            (const struct mb2_header *)(base + off);
        if (h->magic != MB2_HEADER_MAGIC)
            continue;                    /* 不是 header，继续搜 */
        ...
    }
```

`MB2_SEARCH_LIMIT = 32768`、`MB2_HEADER_ALIGN = 8`。test-kernel 的 header 在文件
偏移 0x1000（首个 LOAD 的 p_offset，ELF 头之后），扫描在第 0x1000 步命中。

### 2.2 概念二：四字段校验

**定义**：header 固定 16 字节四字段：`magic`、`architecture`、`length`、
`checksum`；校验条件为 `magic == 0xe85250d6`、`architecture == 0`（i386）、
`magic + architecture + length + checksum ≡ 0 (mod 2^32)`。

**为什么需要**：magic 确认"这是 Multiboot2 header"；arch 确认协议面向 i386；
checksum 防伪/防损坏——三者同时成立才放行。

**工作机制**（本课 `mb2_header_check` 的检查顺序）：

```c
        if (h->architecture != MB2_ARCHITECTURE_I386)
            return -2;                   /* arch 不匹配 */
        if (h->length < 16u || off + h->length > size)
            return -3;                   /* length 越界/过小 */
        if (h->magic + h->architecture + h->length + h->checksum != 0u)
            return -4;                   /* checksum 不匹配 */
```

错误码：-1 未找到（magic 从未命中）、-2 arch、-3 length、-4 checksum、-5 tag
越界。bad.elf（破坏 checksum）实测报 code=04。

### 2.3 概念三：header tags 与 end tag

**定义**：16 字节固定部分之后是可选 header tags：`{u16 type, u16 flags, u32 size}`，
8 字节对齐，以 end tag（`type=0, size=8`）结束。TinyOS L05 用 type-1
（信息请求，请求 type-6 mmap）；L61 用 type-5（图形请求）。

**为什么需要**：内核通过 header tags 向 loader 提要求（"请给我内存图"、
"请设图形模式"）；loader 校验 tag 链完整（end tag 存在、无越界）后才能承诺。

**工作机制**（本课 tag 遍历，步长 `(size+7)&~7`）：

```c
            u32 t = 16u;
            while (t < h->length) {
                const struct mb2_header_tag *tag =
                    (const struct mb2_header_tag *)((const u8 *)h + t);
                if (tag->size < 8u || t + tag->size > h->length)
                    return -5;           /* tag 越界 */
                if (tag->type == 0)
                    break;               /* end tag */
                t += (tag->size + 7u) & ~7u;
            }
```

### 2.4 概念四：校验工具的一致性（loader vs grub-file）

**定义**：`grub-file --is-x86-multiboot2` 是主机侧只读校验工具；本课实现的是
运行时校验（GRUB 装载器内部逻辑）。两者对同一镜像结论必须一致。

**为什么需要**：`make check` 用 grub-file 做静态断言（ok 通过、bad 拒绝），QEMU
用 loader 运行时判定——两边一致才说明实现正确。

**工作机制**（Makefile check 片段）：

```makefile
	@grub-file --is-x86-multiboot2 $(BUILD)/test-kernel.elf \
	  || { printf 'FAIL: ok.elf Multiboot2 check\n' >&2; exit 1; }
	@if grub-file --is-x86-multiboot2 $(BUILD)/bad.bin; then \
	  printf 'FAIL: bad.elf should be rejected\n' >&2; exit 1; fi
```

### 2.5 概念五：两个构建陷阱（本课调试实证）

**陷阱一：`printf '\x00'` 在 dash 下输出字面量**。Makefile 的 recipe 由
`/bin/sh`（dash）执行，POSIX printf 不支持 `\x` 十六进制转义——`printf '\x00'`
输出的是 4 个字符 `\x00`（字节 5c 78 30 30）而非空字节。必须用八进制
`printf '\000'`。

**陷阱二：节头表里的假 magic**。破坏 magic 后，扫描继续，可能在文件后部的节头表
（section header table）里"撞见"一个巧合的 `d6 50 52 e8` 序列（arch=0 但
length 是垃圾）→ 报出误导性的 code=03。因此本课改为**破坏 checksum**（文件偏移
0x100C），让校验在 header 原位就干净地失败（code=04）。这也是一个教学点：
magic 只是"候选"，完整的四字段 + 长度校验才能做出可靠判定。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B06） |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | 引导链与 BIOS 回调 | 未变化 |
| `loader.c` | ELF 解析（B06）+ `mb2_header_check` | 新增 Multiboot2 校验 |
| `test-kernel.S`/`test-kernel.ld` | 测试内核（B06 复用） | 未变化 |
| `Makefile` | 新增 `bad.bin`（破坏 checksum）；KERNEL_SECT=18 | 修改 |
| `build/b07.img` | LBA0=stage1, 1-8=stage2, 9-26=ok.elf, 27-44=bad.elf | 新增 |

### 3.2 `loader.c` 精讲

**常量与结构**：

```c
#define MB2_HEADER_MAGIC       0xe85250d6u
#define MB2_ARCHITECTURE_I386  0u
#define MB2_HEADER_ALIGN       8u
#define MB2_SEARCH_LIMIT       32768u

struct mb2_header {
    u32 magic;        /* +0  */
    u32 architecture; /* +4  */
    u32 length;       /* +8  */
    u32 checksum;     /* +12 */
};

struct mb2_header_tag {
    u16 type;
    u16 flags;
    u32 size;
};
```

**`mb2_header_check(buf, size, &off)`**：扫描（2.1）→ 四字段校验（2.2）→ tag
遍历（2.3）。成功输出 header 偏移。`check_one(label, lba)` 封装"读盘 + ELF 解析
+ MB2 校验 + 打印"：

```c
static int check_one(const char *label, u32 lba)
{
    ...
    r = mb2_header_check((const void *)KERNEL_BUF, KERNEL_SECT * 512u, &hdr_off);
    if (r < 0) {
        vga_puts(label);
        vga_puts(": mb2 header rejected (code=");
        vga_hex((u32)(-r), 2);
        vga_puts(")\n");
        return r;
    }
    vga_puts(label);
    vga_puts(": mb2 header ok @");
    vga_hex(hdr_off, 8);
    vga_puts(" magic=ok arch=ok checksum=ok\n");
    return 0;
}
```

**`loader_main`**：先校验 ok.elf（LBA 9），再校验 bad.elf（LBA 27），最后打印
`B07 done: multiboot2 header check OK`。

### 3.3 构建管线

```text
test-kernel.elf --cp--> bad.bin --printf '\000'|dd seek=4108--> 破坏 checksum
  --truncate 18 扇区--> bad.bin(9216B) --dd seek=27--> b07.img
ok: 同 B06（LBA 9 起 18 扇区）
```

`make check` 用 `grub-file` 断言 ok 通过、bad 拒绝，与 loader 运行时判定一致。

---

## 4. 数据流与运行逻辑

```text
ok.elf / bad.elf --int 0x13--> 0x68000（KERNEL_BUF）
  → elf_parse（B06，确认能解析）
  → mb2_header_check：8 对齐扫描 0..32768 → magic/arch/length/checksum → tags
  → ok: "mb2 header ok @00001000 magic=ok arch=ok checksum=ok"
  → bad: "mb2 header rejected (code=04)"
```

自动化验证 marker：`B07 mb2 header ok`（合法镜像通过）、`B07 done`（收尾）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b07-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b07 check
bootloader-course/scripts/validate-course.sh b07 qemu
```

### 5.2 期望输出

- `make check`：`B07 check PASS: ok=MB2 valid, bad=MB2 rejected, loader checks present`
- QEMU：ok.elf 打印 `mb2 header ok @00001000`；bad.elf 打印
  `mb2 header rejected (code=04)`。

### 5.3 成功判据

合法镜像校验通过并输出 header 偏移；坏镜像被拒绝（code=04）且不进行任何装载；
grub-file 与 loader 判定一致；QEMU trace 无异常。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| ok.elf 也被拒绝 | header 不在 8 对齐处或 checksum 常量错 | `od -An -tx1 -j4096 -N16` 对照四字段；确认 `MB2_HEADER_MAGIC` |
| bad.elf 报 code=03 | 扫描撞见文件后部的假 magic（如节头表） | 改用破坏 checksum 让校验在 header 原位失败；或扩大 length 校验 |
| bad.bin 里出现 `5c 78 30 30` | `printf '\x00'` 在 dash 下输出字面量 | 改用 `printf '\000'`（八进制） |
| `grub-file` 与 loader 判定不一致 | 校验顺序或常量差异 | 逐字段对照；`grub-file` 只看 header 前 32 KiB |
| 找不到 header（code=01） | 镜像超过搜索限制或未 8 对齐 | 确认 header 在文件前 32768 字节；`readelf -l` 看 p_offset |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/loader/multiboot_mbi2.c` 的 `find_header`：

```c
for (header = (struct multiboot_header *) buffer;
     ((char *) header <= (char *) buffer + len - 12);
     header = (struct multiboot_header *) ((grub_uint32_t *) header + MULTIBOOT_HEADER_ALIGN / 4))
  {
    if (header->magic == MULTIBOOT2_HEADER_MAGIC
	&& !(header->magic + header->architecture
	     + header->header_length + header->checksum)
	&& header->architecture == MULTIBOOT2_ARCHITECTURE_CURRENT)
      return header;
  }
```

对照点：

- **相同**：8 字节步进扫描（`MULTIBOOT_HEADER_ALIGN / 4` 个 dword）；四字段求和
  为 0；arch 匹配；返回第一个命中；
- **简化**：GRUB 把校验四合一（不区分错误码）；本课拆成 -1..-5 便于教学诊断；
  边界用 `off + 16 <= limit`（GRUB 用 `len - 12`，语义等价且更安全）；
- **后续**：GRUB 的 `grub_multiboot_load_elf` 在校验后解析信息请求 tag 并构建
  MBI（B09/B10），本课 B08 先做装载与交接。

Linux 对照：`arch/x86/boot/header.S` 的 boot protocol 校验（magic `HdrS`）与
本课四字段校验思想同源；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么搜索步长是 8 字节而不是 4？header 的什么性质决定了这个步长？
2. 动手实验（临时副本）：把 `bad.bin` 改为破坏 magic（`printf '\000'` seek=4096），
   观察报出的错误码，解释节头表假 magic 现象。
3. 动手观察：`od -An -tx1 -j4096 -N16 build/kernel.bin`，手算四字段求和，
   验证 checksum 成立。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "MULTIBOOT2_HEADER_MAGIC\|MULTIBOOT_HEADER_ALIGN" include/grub/multiboot.h`，
   确认常量与本课一致。
5. 综合：画出扫描 0..32768 步进 8 的示意图，标出 test-kernel 的 header 在
   0x1000 命中的路径与每步检查的字段。

---

## 9. 本课小结与下一课预告

**小结**：本课实现了 Multiboot2 header 校验。关键收获：(1) 搜索限定"前 32768 字节
+ 8 对齐"，保证扫描有界；(2) 四字段（magic/arch/length/checksum）缺一不可，
checksum 求和为 0 是核心防伪；(3) header tags 链要以 end tag 收尾并做越界检查；
(4) loader 运行时判定必须与 grub-file 静态判定一致；(5) 两个构建陷阱：dash 的
`\x` 转义、节头表假 magic——最终用"破坏 checksum"得到干净的 code=04。至此，
loader 确认了"这个内核是合法的 Multiboot2 客户"，可以装载了。

**下一课预告**：进入 [`b08-stable/README.md`](../b08-stable/README.md)。按
PT_LOAD 把 ELF 段装载到 `p_paddr`、清零 .bss 尾部、设置交接寄存器
（EAX=0x36d76289、EBX=占位 MBI）、跳转 `e_entry`——自写引导器首次启动一个
Multiboot2 内核，对照 GRUB `multiboot_elfxx.c` 与 `commands/boot.c`。
