# Lesson B17: grub.cfg 解析执行 — 精讲文档

> **课号**：Lesson B17（Mini-GRUB 从零写 GRUB 课程第 17 课，阶段四第 2 课）
> **主题**：grub.cfg 读取与执行：tokenizer、语句、变量展开、块（menuentry）
> **课程位置**：阶段四「配置与交互」第 2 课
> **前置课程**：[`b16-stable/README.md`](../b16-stable/README.md)（命令注册表与命令行）
> **后续课程**：[`b18-stable/README.md`](../b18-stable/README.md)（menuentry 菜单）
> **一句话目标**：loader 启动时自动读取并执行光盘上的 `grub.cfg`——配置即脚本。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能把 `grub.cfg` 里的一行行配置当作脚本
执行：`set timeout=0`、`menuentry "..." { ... }`、`multiboot2 /boot/kernel.elf`、
`boot` 都能按预期工作——QEMU 启动后**无需人工输入**，配置自动驱动内核装载与启动。

- **在课程中的位置**：研读支线 0.2 的核心结论——grub.cfg 是脚本、命令是运行时
  注册的。B16 有了命令引擎，本课让"配置文本 → 命令流"发生。对照 GRUB
  `grub-core/script/` 的 tokenizer/parser 与 `normal/main.c` 的
  `grub_normal_execute`。
- **前置知识清单**：
  1. B16：命令表与 `set` 环境变量；
  2. B15：ELF 装载与 Multiboot2 交接（`multiboot2`/`boot` 命令的语义）；
  3. B14：`file_open`（读 grub.cfg 文件）。
- **本课交付**：`build/b17.img`（CD，含 `/boot/grub/grub.cfg` 与
  `/boot/kernel.elf`）；QEMU 启动后 loader 自动执行配置并启动测试内核。

---

## 2. 核心概念精讲

### 2.1 概念一：tokenizer（script.c）

GRUB 的脚本不是逐字执行的文本，而是先经 **tokenizer** 切成词（token），再
分发到命令表。B17 的 `script_tokenize` 实现核心语义：

- **引号**：`"..."` 内的空白保留为同一个 token（`echo "hello world"` →
  `echo` + `hello world`）；
- **变量展开**：`$name` 与 `${name}` 在 token 阶段展开——**引号内也展开**
  （GRUB 语义，与 shell 不同）；未定义变量展开为空串；
- **注释**：`#` 起忽略到行尾；
- **切词**：空白/制表符分隔；`{`/`}` 作为独立 token。

tokenizer 放在**无硬件依赖**的 `script.c` 里（只依赖环境变量），因此可以在
主机上用 `test_script.c` 做单元测试（`make check` 执行）——这是 freestanding
引导代码里难得的可单测组件。

### 2.2 概念二：语句执行与错误语义

一行 = 命令 + 参数。`cmd_execute(argc, argv)` 按首词查表分发（B16）。
**错误语义**（GRUB 相同）：一行执行失败（命令不存在、参数错）只打印
`error:`，**不中断整个脚本**——坏行之后的行继续执行。

### 2.3 概念三：块结构（menuentry）

`menuentry "Test Kernel" { ... }` 是带块体的语句。B17 只做**识别与跳过**：
遇到 `menuentry` 打印一行说明，然后进入"跳过状态"，直到单独一行的 `}`。
块体的真实语义（菜单显示、回车选择）是 B18 的内容。

### 2.4 概念四：multiboot2 与 boot 命令

B15 的装载链（file → ELF → MBI → 交接）被包成两个命令：

```text
multiboot2 /boot/kernel.elf   # file_open + file_read + elf_load，记录 entry
boot                          # mbi_build + mb2_boot(entry, mbi)
```

`multiboot2` 是"装载"（load），`boot` 是"交接"（boot）——对应 GRUB
`commands/multiboot2.c` 与 `commands/boot.c` 的职责分离（B11 的
loader_set/loader_boot 概念）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B16 的增量 |
|---|---|---|
| `script.c` | 环境变量 + tokenizer（无硬件依赖） | 新增（可主机单测） |
| `test_script.c` | tokenizer 主机单元测试 | 新增 |
| `loader.c` | 脚本执行器 + `multiboot2`/`boot` 命令 + 装载链 | 改造 |
| `grub.cfg` | 光盘上的配置脚本 | 新增 |
| `Makefile` | CD 含 boot/grub/grub.cfg + boot/kernel.elf | 修改 |
| `build/b17.img` | 配置驱动启动的 CD | 新增 |

### 3.2 脚本执行器（loader.c）

```c
static void script_execute_line(const char *l)
{
    int argc;
    if (in_menuentry_skip) {        /* 块体：跳过直到 '}' */
        ... if (*p == '}') in_menuentry_skip = 0;
        return;
    }
    argc = script_tokenize(l, argv_list, ARGV_MAX);
    if (argc == 0) return;
    if (name_eq(argv_list[0], "menuentry")) {
        vga_puts("B17 script: menuentry '...' body skipped (B18)\n");
        in_menuentry_skip = 1;
        return;
    }
    cmd_execute(argc, argv_list);   /* 复用 B16 的命令表 */
}

static int script_run_file(const char *path)   /* noinline：check 断言符号 */
{
    file_open(path) -> file_read(到 0x68000)   /* 8KiB 上限 */
    while (行未读完) { 截断到 '\n'; script_execute_line(p); }
}
```

### 3.3 grub.cfg 与 CD 布局

```text
set timeout=0
set root=(cd0)
echo "B17 script: grub.cfg executing"
echo "root is $root"          # 引号内展开 -> "root is (cd0)"
badcmd this line errors but does not stop
menuentry "Test Kernel" {     # 识别 + 跳过块体（B18 实现菜单）
    ...
}
multiboot2 /boot/kernel.elf
echo "B17 script: kernel loaded, booting"
boot
```

CD 布局（`-boot-info-table` 保留大小写）：`BOOT.BIN` +
`boot/grub/grub.cfg` + `boot/kernel.elf`——loader 用 `/boot/grub/grub.cfg`
等**小写**路径（与 GRUB 惯例一致；B13/B14 默认模式转大写，B15+ 松弛模式
保留大小写，两条路径规则在本课程文档里有对照）。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2 -> loader_main:
  挂载 (cd0) -> 注册命令 -> script_run_file("/boot/grub/grub.cfg"):
    set timeout=0                  # 环境变量
    echo "root is $root"           # 引号内变量展开
    badcmd ...                     # 报错但继续
    menuentry ... { ... }          # 识别 + 跳过块体
    multiboot2 /boot/kernel.elf    # 读文件 + elf_load，记录 entry
    boot                           # mbi_build + mb2_boot -> 内核接管
```

期望输出（VGA 文本，验证脚本 marker 加粗）：

```
**B08 test-kernel: hello from Multiboot2**         ← 内核接管第 0 行
B17 cfg: boot drive = e0
**B17 script: grub.cfg executing**
**root is (cd0)**
B17 error: command not found: badcmd
B17 script: menuentry 'Test Kernel' body skipped (B18 will implement the menu)
B17 multiboot2: loaded /boot/kernel.elf entry=00100018
B17 script: kernel loaded, booting
**B17 boot: jumping to entry=00100018**
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make            # 构建 build/b17.img
make check      # tokenizer 主机单测 + grub-script-check + 符号 + 文件对照
make run        # QEMU 从 CD 启动（自动执行 cfg 并启动内核）
./scripts/validate-course.sh b17 check
./scripts/validate-course.sh b17 qemu   # 自动执行 + VGA 文本校验
```

### 5.2 成功判据

1. `make check` 全绿：tokenizer 单元测试（引号/展开/注释/块）通过、
   `grub-script-check` 校验 cfg 语法、`script_tokenize`/`script_run_file`/
   `cmd_multiboot2`/`cmd_boot` 符号存在、抽取的 grub.cfg 与源文件一致；
2. QEMU 启动后**无需输入**，cfg 逐行执行，内核自动启动；
3. 验证脚本 grep 到 `B17 script: grub.cfg executing`、`root is (cd0)`、
   `B17 boot: jumping`、`test-kernel`。

---

## 6. 调试地图

1. **引号内变量不展开**：初版 tokenizer 只在引号外展开 `$`，`"root is $root"`
   输出字面量。GRUB 语义是引号内也展开（与 shell 不同）——修正后单测用例
   4b 覆盖。
2. **注释产生空 token**：`set x=1 # comment` 初版在词边界识别 `#` 前先开了
   一个新 token。修正：跳过空白后若遇到 `#` 直接结束本行。
3. **static + -Os 内联**：`script_run_file` 被内联后符号缺失——核心函数加
   `noinline` 便于 `make check` 断言。
4. **缓冲重叠**：cfg 缓冲（0x68000）与目录缓冲（0x68800）重叠会让多行 cfg
   在执行中途被后续 `file_open` 覆盖——B17 把目录缓冲挪到 0x6A000、内核
   缓冲到 0x6B000，互不干扰。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `script_tokenize` | `grub-core/script/tokenizer.c` | 引号/变量/注释语义一致；无转义/here-doc |
| 环境变量 | `grub-core/kern/env.c` | 固定槽位 vs 动态 |
| `script_run_file` | `normal/main.c` 的 `grub_normal_execute` | 逐行执行；无 if/for/函数 |
| `multiboot2` 命令 | `commands/multiboot2.c` | 读文件 + elf_load + 记 entry |
| `boot` 命令 | `commands/boot.c` | mbi + 交接 |
| menuentry 跳过 | `commands/menuentry.c` + `script/parser.y` | B17 只识别块，B18 实现菜单 |

---

## 8. 思考题与练习

1. 给 tokenizer 加"转义"：`\$` 输出字面 `$`、`\"` 输出引号（GRUB 的
   tokenizer 有反斜杠处理）。补对应的单元测试。
2. 实现 `${name:-default}` 形式的默认值展开（GRUB 的 `${var:=default}`）。
3. `menuentry` 块体目前被跳过；如果块体里有多行命令和嵌套 `{`，如何实现
   括号配对的跳过（提示：计数）。
4. 为什么 `multiboot2`（装载）和 `boot`（交接）要分成两个命令？如果合一，
   grub.cfg 还能在装载后、交接前插入其他命令吗？
5. 在主机上给 `script.c` 增加更多单测：超长 token 截断、连续引号、嵌套
   `${a}${b}` 等边界。

---

## 9. 本课小结与下一课预告

**小结**：本课让"配置文本 → 命令流"发生——`script.c` 的 tokenizer（引号、
`$var`/`${var}` 展开、注释、切词，主机可单测）+ loader 的脚本执行器
（错误继续、menuentry 块跳过）+ `multiboot2`/`boot` 命令（B15 装载链的
命令化）。QEMU 启动后 grub.cfg 自动执行并启动测试内核，全程无需输入。

**下一课** [`b18-stable/README.md`](../b18-stable/README.md)：menuentry 块有了
识别，本课给它们真实语义——菜单列表、timeout 倒计时、default 选择、回车
执行选中项，对照 GRUB `menuentry.c` 与 `normal/main.c`。
