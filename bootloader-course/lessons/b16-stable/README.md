# Lesson B16: 命令注册表与极简 grub> 命令行 — 精讲文档

> **课号**：Lesson B16（Mini-GRUB 从零写 GRUB 课程第 16 课，阶段四第 1 课）
> **主题**：命令注册表（名字 → 函数指针）、`set` 环境变量、极简交互命令行
> **课程位置**：阶段四「配置与交互」第 1 课
> **前置课程**：[`b15-stable/README.md`](../b15-stable/README.md)（El Torito 光盘引导）
> **后续课程**：[`b17-stable/README.md`](../b17-stable/README.md)（grub.cfg 解析执行）
> **一句话目标**：loader 有一个命令表驱动的交互界面：输入 `help`、`set`、
> `ls` 等命令即可执行——为解析 grub.cfg 建立命令执行引擎。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 在启动后进入 `grub>` 式提示符，命令由
注册表分发执行（`grub_command_find` 语义），环境变量用 `set` 读写。

- **在课程中的位置**：研读支线 0.2 指出 GRUB 的 `grub.cfg` 不是 key-value
  文件，而是**脚本**——所有命令（`set`/`menuentry`/`multiboot2`/`boot`）都是
  运行时注册进命令表的。B16 先搭命令引擎与交互界面，B17 让脚本驱动它。
- **前置知识清单**：
  1. B14：文件读取（`ls` 命令用）；B15：CD 启动；
  2. 键盘输入：PS/2 端口轮询（对照 TinyOS Lesson 03 思路）；
  3. 研读支线 0.2（命令注册、查找、执行）。
- **本课交付**：`build/b16.img`（CD）；QEMU 启动后进入提示符，可输入
  `help`、`set timeout=0`、`set`（回显）、`ls`、`echo`、`halt`、`boot`
  （占位）等命令；未知命令报 `B16 error: command not found`。

---

## 2. 核心概念精讲

### 2.1 概念一：命令注册表

GRUB 的命令系统（`include/grub/command.h`）是一张**链表**：每个命令是
`{name, 函数指针, 帮助文本, next}`，`grub_command_register` 头插注册，
`grub_command_find` 按名字查表，`grub_command_execute` 拆词后分发。本课
复刻这套语义：

```c
struct cmd {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
    struct cmd *next;
};

int cmd_register(struct cmd *c) { c->next = cmd_list; cmd_list = c; return 0; }

static struct cmd *cmd_find(const char *name)
{
    struct cmd *c;
    for (c = cmd_list; c; c = c->next)
        if (str_eq(c->name, name))
            return c;
    return 0;
}

int cmd_execute(int argc, char **argv)
{
    struct cmd *c = cmd_find(argv[0]);
    if (!c) { vga_puts("B16 error: command not found: "); ...; return -1; }
    return c->fn(argc, argv);
}
```

`help` 命令遍历这张表打印名字与帮助——**命令表的自文档化**。这也是
B17 脚本执行引擎的基础：grub.cfg 的每一行就是一个 `cmd_execute` 调用。

### 2.2 概念二：环境变量（kern/env.c）

GRUB 的环境变量是 name→value 的链表，`set name=value` 写入、`set name`
回显、`set` 列出全部。本课用**固定槽位数组**（无 malloc）：

```c
#define ENV_MAX 16
struct env { char name[16]; char value[48]; struct env *next; };
static struct env env_slots[ENV_MAX];   /* 无堆分配的教学简化 */
```

`env_set` 先按名字查找（更新已存在的变量），否则占用一个新槽位并头插。
`set` 命令解析 `name=value`（找第一个 `=`），B17 的 `$var` 展开将基于
`env_get`。

### 2.3 概念三：PS/2 键盘轮询

loader 在保护模式下用**端口 I/O 直接轮询 PS/2 控制器**（不需要 BIOS）：
- 端口 0x64 = 状态寄存器（bit0 = 输出缓冲满）；
- 端口 0x60 = 数据寄存器（读扫描码）。

```c
static u8 kbd_read(void)
{
    while (!(inb(KBD_STATUS) & 0x01u))   /* 等输出缓冲非空 */
        ;
    return inb(KBD_DATA);
}
```

set-1 扫描码需要翻译成字符（US 布局映射表）：字母/数字/符号按下码，
**释放码**（bit7 置位）跳过，Shift（0x2A/0x36）跟踪按下/释放，
扩展键前缀 0xE0 吞掉后续字节。`kbd_getline` 提供行缓冲（回显 + 退格）。
QEMU 的 `sendkey` 向 PS/2 控制器注入按键事件，自动化验证直接可用。

### 2.4 概念四：命令行循环

```text
for (;;) {
    打印 "grub> ";
    kbd_getline(line);              # 键盘读一行（回显）
    split_args(line, argv);         # 按空格拆词（写入 '\0' 分隔）
    if (argc == 0) continue;
    cmd_execute(argc, argv);        # 首词查表 + 分发
}
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B15 的增量 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导 + BIOS 回调 + `mb2_boot` | 消息文本变化 |
| `loader.c` | 键盘 + 命令表 + 环境变量 + 提示符循环 | 重写（交互层） |
| `Makefile` | CD 镜像（无内核，`ls` 列 BOOT.BIN） | 精简 |
| `build/b16.img` | 交互式 CD | 新增 |

### 3.2 键盘翻译（set-1 扫描码）

```c
static char scancode_to_char(u8 sc)
{
    static const char map[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '-', '=', '\b', 0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        ...
    };
    static const char shift_map[] = { ... };   /* 数字行符号 + 大写 */
    if (sc < sizeof(map))
        return kbd_shift ? shift_map[sc] : map[sc];
    return 0;
}
```

### 3.3 `set` 命令

```c
static int cmd_set_fn(int argc, char **argv)
{
    if (argc == 1) {              /* set：列出全部 name=value */
        for (e = env_head; e; e = e->next) { vga_puts(e->name); vga_puts("="); ... }
        return 0;
    }
    /* 找第一个 '=' 区分 "name=value" 与 "name" */
    ...
}
```

### 3.4 命令注册

```c
static struct cmd cmd_help = { "help", cmd_help_fn, "list available commands", 0 };
static struct cmd cmd_set  = { "set",  cmd_set_fn,  "get/set environment variables", 0 };
...（echo / ls / halt / boot 同理）

static void cmd_register_all(void)   /* 顺序无关；help 显示链表序 */
{
    cmd_register(&cmd_help);  cmd_register(&cmd_set);  cmd_register(&cmd_echo);
    cmd_register(&cmd_ls);    cmd_register(&cmd_halt); cmd_register(&cmd_boot);
}
```

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2(保护模式) -> loader_main:
  挂载 (cd0) ISO9660 -> 注册 6 个命令 -> 进入循环:
    "grub> " -> 键盘读行 -> 拆词 -> 查表 -> 执行 -> 回到提示符
```

交互场景（QEMU + sendkey 实测输出）：

```
B16 cmd: Mini-GRUB interactive prompt
B16 cmd: boot drive = e0
B16 cmd: (cd0) mounted
grub> help
B16 help: available commands
  boot - boot the loaded kernel (not yet)
  halt - halt the CPU
  ls - list files on (cd0)
  echo - print its arguments
  set - get/set environment variables
  help - list available commands
grub> set foo=1
foo=1
grub> set
foo=1
grub> badcmd
B16 error: command not found: badcmd
grub>
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make            # 构建 build/b16.img
make check      # 命令表静态断言（cmd_register/cmd_execute/env_set/help/set/boot）
make run        # QEMU 从 CD 启动（图形窗口，可交互输入）
./scripts/validate-course.sh b16 check
./scripts/validate-course.sh b16 qemu   # sendkey 输入 help/set/badcmd + VGA 校验
```

### 5.2 成功判据

1. `make check` 全绿：命令表符号与 help/set/boot 注册实例存在；
2. QEMU 交互：`help` 列出命令、`set foo=1` 回显、`set` 列出 `foo=1`、
   未知命令报 `command not found`；
3. 验证脚本 sendkey 序列后 grep 到 `grub>`、`available commands`、
   `foo=1`、`command not found`。

---

## 6. 调试地图

1. **sendkey 的键名**：空格 = `spc`、回车 = `ret`、等号 = `equal`、减号 =
   `minus`；字母/数字直接按键名。QEMU 的 sendkey 一次一个键名。
2. **PS/2 释放码**：每个按键有按下码与释放码（bit7 置位），不跳过会
   产生重复/垃圾字符；Shift 按下/释放要分开跟踪。
3. **static + -Os 内联**：`env_set` 被内联后 `objdump -t` 找不到符号，
   `make check` 失败——对需要静态断言的核心函数加 `noinline`。
4. **`set` 命令的 `=` 解析**：`name=value` 与 `name` 两种形态靠第一个 `=`
   区分；`env_set` 的槽位上限 16 个，超出报 `env full`。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `cmd_register`/`cmd_find`/`cmd_execute` | `include/grub/command.h` 的 `grub_command_*` | 签名简化为 `(int argc, char **argv)` |
| `env_set`/`env_get` | `grub-core/kern/env.c` | 固定槽位 vs `grub_env` 哈希/链表 |
| `help` 命令 | `grub-core/commands/help.c` | 结构一致 |
| `set` 命令 | `grub-core/commands/set.c` | 结构一致 |
| `ls` 命令 | `grub-core/commands/ls.c` | 只列根目录 |
| 提示符循环 | `grub-core/normal/main.c`（grub_enter_normal_mode） | 无补全/历史/颜色 |

---

## 8. 思考题与练习

1. 给 `echo` 命令加上 `$var` 展开（`echo $foo` 打印 foo 的值）——这正是
   B17 脚本执行要用的最小变量展开。
2. 命令表改成**按名字排序**的查找，或者加"命令不存在时给出相似命令
   提示"（GRUB 的 `did you mean` 风格）。
3. PS/2 键盘的扩展键（方向键，前缀 0xE0）当前被吞掉；试实现方向键 →
   命令行历史（上/下键翻历史）。
4. `ls` 目前只列根目录；扩展到 `ls /BOOT`（复用 B14 的 `file_open` 路径
   查找，找到目录后列其记录）。
5. 为什么 PS/2 端口轮询在保护模式下不需要 BIOS？端口 I/O 指令与 CPU 模式
   的关系是什么？（对照 B05 的实模式回调——INT 13 需要 BIOS，端口读写不需要）

---

## 9. 本课小结与下一课预告

**小结**：本课把 B14 的文件抽象接到一个命令驱动的交互界面——命令注册表
（`grub_command_*` 语义）、`set` 环境变量（固定槽位链表）、PS/2 键盘轮询
与行编辑、`grub>` 提示符循环。`help` 自文档化命令表，未知命令报错。
QEMU 里 `set foo=1` → `set` 回显 `foo=1`，全部判据通过。

**下一课** [`b17-stable/README.md`](../b17-stable/README.md)：命令引擎就绪后，
loader 读取光盘上的 `/boot/grub/grub.cfg` 并逐行执行——tokenizer、语句执行、
变量展开，对照 GRUB `grub-core/script/`。
