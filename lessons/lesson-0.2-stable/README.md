# Lesson 0.2: grub.cfg 配置语言与命令分发 — 精讲文档

> **课号**：Lesson 0.2（GRUB 源码研读支线第 2 课，文档观察课，不生成内核）
> **主题**：`/boot/grub/grub.cfg` 的三行配置如何变成菜单，以及 `menuentry` / `multiboot2` /
> `boot` 三条命令如何被 GRUB 解析、注册、分发执行
> **课程主线位置**：第 1 阶段支线；承上启下（0.1 讲清了源码树与产物，本课讲「配置怎么跑起来」）
> **前置课程**：[`lesson-0.1-stable/README.md`](../lesson-0.1-stable/README.md)（源码树地图、
> ISO 产物、只读观察方法）
> **后续课程**：[`lesson-0.3-stable/README.md`](../lesson-0.3-stable/README.md)
> （设备、文件系统与路径解析）
> **一句话目标**：把 TinyOS 那 7 行 `grub.cfg` 逐行翻译成 GRUB 内部的「命令 → 处理器」调用链。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能解释 `set timeout=0`、`set default=0`、`menuentry "..." { ... }`、
`multiboot2 /boot/kernel.elf`、`boot` 每一行分别触发 GRUB 的哪个模块、哪个注册表项。

- **在课程主线中的位置**：0.1 给了「零件清单」，本课给「配置→执行的驱动链」。它是 0.3（路径
  解析）与 0.4（ELF 装载）的入口：`multiboot2` 命令的执行正是这两个主题的交汇点。
- **前置知识清单**：
  1. 0.1 课的源码树五区地图（`grub-core/normal|script|commands|loader` 各干什么）；
  2. 会用 `xorriso` 列出 ISO 文件、用 `grep -R` 在源码里搜符号；
  3. 认得 Lesson 01 的 [`grub.cfg`](../../lessons/lesson-01-stable/grub.cfg) 七行内容。
- **本课交付**：一张「grub.cfg 每一行 → 命令注册点 → 处理器函数」的映射表，以及
  「ISO 上的配置 = 102 字节」的只读证据。

---

## 2. 核心概念精讲

### 2.1 概念一：grub.cfg 是一种「脚本语言」

`grub.cfg` 不是配置键值对文件，而是 GRUB 自己脚本引擎可执行的脚本。它具备三类语法：

| 语法 | 例子 | 含义 |
|---|---|---|
| 环境变量赋值 | `set timeout=0` | 把 `timeout` 变量设为 0，存入环境变量区 |
| 块结构 | `menuentry "TinyOS lesson 1" { ... }` | 定义菜单项（一条带名字的函数体） |
| 普通命令 | `multiboot2 /boot/kernel.elf`、`boot` | 逐条执行的命令 |

脚本引擎位于 `grub-core/script/`（词法/语法/执行三部分），`normal` 模块把配置文件读进来后
交给它逐条执行。**为什么需要脚本而不是写死？** 因为 GRUB 要适配上千种发行版与硬件组合，
脚本让「找内核、选内核、传参数」的逻辑可以在不重新编译 core image 的前提下调整。

### 2.2 概念二：命令注册与分发（关键机制）

GRUB 的所有命令（`set`、`menuentry`、`multiboot2`、`boot`、`ls`…）都是运行时注册进一张
**命令表**的。核心接口在 `include/grub/command.h`：

- 注册：`grub_register_command("名字", 处理器函数, 帮助, 说明)`；
- 查找：`grub_command_find("名字")` 按名字查表；
- 执行：`grub_command_execute("名字", argc, argv)` 调处理器。

`normal` 每次遇到一个命令词，就查这张表；查到了调用对应处理器，查不到就报 `error: command
not found`。**分发 = 字符串查表 → 函数指针调用**，这是理解全课的主线索。

### 2.3 概念三：normal 模块与菜单生命周期

`normal` 是交互核心（本机 ISO 上 `i386-pc/normal.mod` 有 115532 字节，是所有模块里最大的一
档）。它的职责链：

```text
normal 命令 → 读取 (prefix)/grub.cfg → 脚本执行器逐条跑
    ├─ set timeout/default（环境变量）
    ├─ menuentry ...（登记一条菜单）
    └─ 进入菜单循环 → 用户选择/超时 → 执行该菜单项函数体
```

`timeout=0` + `default=0` 的效果：菜单几乎不等用户，立刻选中第 0 项。所以 QEMU 里看不到
菜单直接进内核——这是 Lesson 01 产物能「自动启动」的配置层原因。

### 2.4 概念四：multiboot2 与 boot 的职责分离

`multiboot2 /boot/kernel.elf` 只做「装载登记」：打开文件、校验 header、把段读进内存、记录
入口点与参数，把「启动动作」交给 GRUB 的 loader 框架（`grub-core/kern/loader.c`）。
`boot` 命令才真正触发交接：调用 `grub_loader_boot()`，执行 loader 注册的 boot 函数，
跳到内核入口。**装载与启动是两次调用**，这对应 TinyOS 视角里「GRUB 把 EAX/EBX 准备好再跳转」。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（normal / script / commands / loader）

```bash
cd "$GRUB_SRC"
grep -R "multiboot2" grub-core/loader include/grub | head -30
grep -R "grub_register_command" grub-core/commands grub-core/loader | head -30
grep -R "grub_command_find" grub-core | head -10
grep -R "grub_normal_execute" grub-core/normal | head -10
```

**预期输出解读**：第一条应命中 `loader/multiboot.c`（`multiboot2` 命令名与处理器在这里注册，
GRUB 2.14 中 multiboot/multiboot2 已合并；旧版 2.02 分开在 `loader/multiboot2.c`）与
`include/grub/multiboot.h`。第二条展示命令注册的"全家福"；第三条是命令分发核心；第四条
`grub_normal_execute` 是 normal 读取配置的入口（`grub-core/normal/main.c`）。发行版路径以
grep 结果为准。

### 3.2 观察一：ISO 上的 grub.cfg 与源码版本一致

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --
wc -c lessons/lesson-01-stable/grub.cfg
```

实测输出：ISO 上 `/boot/grub/grub.cfg` 为 **102 字节**，与源码文件 `wc -c` 相同——证明
`Makefile` 的 `cp grub.cfg $(ISO_ROOT)/boot/grub/` 是逐字节复制，没有生成改写。**解读**：
`grub-mkrescue` 不会改配置内容，这为后续所有「配置驱动实验」提供了可对比基线。

### 3.3 观察二：7 行 grub.cfg 逐行精讲

源码全文（`lessons/lesson-01-stable/grub.cfg`）：

```text
set timeout=0
set default=0

menuentry "TinyOS lesson 1" {
    multiboot2 /boot/kernel.elf
    boot
}
```

逐行机制：

| 行 | 命令 | 注册点（以 grep 为准） | 作用与分发结果 |
|---|---|---|---|
| 1 | `set timeout=0` | `grub-core/kern/env.c` 附近的环境变量系统 | 把 `timeout` 置 0，菜单零等待 |
| 2 | `set default=0` | 同上 | 默认选中第 0 个菜单项 |
| 4 | `menuentry "TinyOS lesson 1" {` | `grub-core/commands/menuentry.c` | 登记一条菜单项，花括号内是它的函数体 |
| 5 | `multiboot2 /boot/kernel.elf` | `grub-core/loader/multiboot.c` | 装载登记：打开文件、校验 header、载入段（0.4 课细讲） |
| 6 | `boot` | `grub-core/commands/boot.c` | 调用 `grub_loader_boot()`，真正交接 |
| 7 | `}` | — | 菜单项函数体结束 |

### 3.4 观察三：命令注册的证据

```bash
cd "$GRUB_SRC"
grep -R '"multiboot2"\|grub_register_command.*multiboot' grub-core/loader
grep -R '"boot"' grub-core/commands/boot.c | head -5
grep -R 'grub_loader_set' grub-core/loader grub-core/kern | head -10
```

**预期输出解读**：`multiboot` 的处理器注册了命令名与 boot 函数；`boot.c` 里的 `boot` 命令
调用 `grub_loader_boot()`；`grub_loader_set` 把装载器登记进 loader 框架。三条命令共同构成
2.4 节「装载与启动分离」的源码证据。

### 3.5 观察四：ISO 上的命令模块对应

```bash
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E 'normal|multiboot2|multiboot'
```

实测（节选，2026-08-06）：

```text
/boot/grub/i386-pc/normal.mod     115532
/boot/grub/i386-pc/multiboot2.mod  15972
/boot/grub/i386-pc/multiboot.mod   14908
```

**解读**：`normal.mod` 是菜单与脚本引擎的模块载体；`multiboot2.mod` 就是 3.3 节里
`multiboot2` 命令的宿主。若 ISO 缺 `multiboot2.mod`，命令表里就查不到这个名字——这就是 0.9 课
「缺失模块」故障的直接成因。

---

## 4. 数据流与运行逻辑

把「配置」到「启动」串成一条链：

```text
normal 启动 → 读 /boot/grub/grub.cfg（102 字节）
  → set timeout=0 / set default=0（环境变量）
  → menuentry "TinyOS lesson 1" { ... }（登记菜单项）
  → 菜单循环：timeout=0 → 自动选中 default=0
  → 执行函数体：multiboot2 /boot/kernel.elf
       ├─ loader/multiboot.c：打开文件、校验 Multiboot2 header、按 PT_LOAD 装段
       └─ grub_loader_set 登记（入口点、MBI 生成器、boot 函数）
  → boot
       └─ grub_loader_boot()：生成 MBI、设置 EAX/EBX、跳转 _start
```

后续 0.3（`/boot/kernel.elf` 怎么被打开）与 0.4（ELF 段怎么装）正是这条链上两个特写镜头。

---

## 5. 观察与验证

### 5.1 依赖

`xorriso`、`binutils`；源码阅读需自备 `$GRUB_SRC`。

### 5.2 复现命令清单

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --   # 102 字节
wc -c lessons/lesson-01-stable/grub.cfg                         # 102
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E 'normal|multiboot'
cd "$GRUB_SRC" && grep -R '"multiboot2"' grub-core/loader       # 命令注册点
```

### 5.3 实测记录（2026-08-06，全部只读）

ISO 上 `grub.cfg` 102 字节与源码一致；`normal.mod` 115532 / `multiboot2.mod` 15972 /
`multiboot.mod` 14908 字节；`grep -R '"multiboot2"'` 命中 `loader/multiboot.c`。

### 5.4 安全边界（本课红线）

只读 `kernel.iso` 与源码；不执行 `grub-mkrescue`/`grub-mkimage` 等写文件命令；改配置实验
必须复制到个人学习版进行，不覆盖 stable 目录。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 菜单一闪而过直接进内核 | `timeout=0` 生效 | 把 `timeout` 改大在个人副本里验证 |
| 报 `error: command not found: multiboot2` | 缺 `multiboot2.mod` 或命令名拼错 | 检查 ISO 的 `i386-pc/` 是否有该模块；对照 3.3 表 |
| 停在 `grub>` 提示符 | `grub.cfg` 没被找到（0.9 课详讲） | 检查 ISO 内路径是否 `/boot/grub/grub.cfg` |
| 菜单项不出现 | `menuentry` 语法错误（括号不配对） | 逐行对照 3.3 的 7 行模板 |
| `boot` 后黑屏/重启 | 装载登记了但 header 或段有问题 | 先跑 `grub-file`，再看 0.4/0.5 课的检查表 |
| 想让另一课内核出现在菜单 | 复制修改 `menuentry` 块 | 只改个人副本的 `grub.cfg` |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/init/main.c` 的命令行解析：GRUB 的 `set` 环境变量与 Linux 内核 cmdline
  「键=值」解析都是一种受控的启动参数机制，但一个是脚本、一个是 C 结构解析；
- `linux-v6.12/kernel/reboot.c` 的 `kernel_restart` 与 GRUB `boot` 命令的「最后一步触发」
  在工程语义上相似：都是把控制权从当前执行者交给下一阶段。

**边界提醒**：GRUB 的命令表机制是 GRUB 自己的设计，Linux 无对应物；对照只用于工程直觉。

---

## 8. 思考题与练习

1. 概念理解：为什么 `multiboot2` 只「装载登记」、要等 `boot` 才交接？如果去掉 `boot` 行，
   预计 QEMU 里会发生什么？（提示：`menuentry` 函数体执行完菜单会怎样。）
2. 源码定位：在 `$GRUB_SRC` 中找 `grub_register_command` 对 `set`、`menuentry`、`boot`、
   `multiboot2` 的注册处，各在哪个文件？
3. 动手实验：在个人副本里把 `timeout=0` 改成 `timeout=5` 重新构建 ISO，观察菜单是否出现
   并说明 `default=0` 的作用。
4. Linux 对照：比较 GRUB 环境变量 `timeout` 与 Linux cmdline 参数 `console=` 的解析方式
   有何异同。
5. 综合：画出「`boot` 行 → `grub_loader_boot()`」之前的完整调用链，标出 0.3 与 0.4 课
   会补充的环节。

---

## 9. 本课小结与下一课预告

**小结**：`grub.cfg` 是 GRUB 脚本引擎执行的脚本；`normal` 模块读取它，`menuentry` 登记菜单，
`set` 写环境变量；一切命令靠「字符串查命令表 → 函数指针」分发执行；`multiboot2` 只装载登记，
`boot` 才真正交接。ISO 上的 102 字节配置与源码一致，是可复现的观察基线。

**下一课预告**：进入 [`lesson-0.3-stable`](../lesson-0.3-stable/README.md)，追问
`multiboot2 /boot/kernel.elf` 里的路径是如何被 GRUB 解析的：设备（`(cd0)`）、ISO9660 文件系统、
`grub_file_open` 三层的查找流程。
