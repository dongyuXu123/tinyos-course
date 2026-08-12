# Lesson B22: 故障调试与 rescue 模式 — 精讲文档

> **课号**：Lesson B22（Mini-GRUB 从零写 GRUB 课程第 22 课，可执行课）
> **主题**：错误分类、错误消息、rescue 模式（极简命令集）
> **课程位置**：阶段六「故障与验收」第 1 课
> **前置课程**：[`b21-stable/README.md`](../b21-stable/README.md)（type-8 → GUI）；
> 研读支线 0.9（故障分类）
> **后续课程**：[`b23-stable/README.md`](../b23-stable/README.md)（端到端综合验收）
> **一句话目标**：loader 面对坏 ELF、坏 header、缺文件、缺模块时，报出可诊断的
> 错误并进入可交互的 rescue 提示符，而不是死机或跳飞。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能向自己的 loader 注入各类故障，观察它报出
`B22 error: ...` 并降级到 `rescue>` 提示符，再用极简命令（`set`/`ls`/
`insmod`/`normal`/`halt`）恢复现场。

- **在课程中的位置**：研读支线 0.9 区分了 `grub>`（正常，模块/配置部分缺失）
  与 `grub rescue>`（core 不完整，只剩内建命令）。B22 复刻这套分级降级：
  **正常脚本执行 ↔ rescue 提示符**。这是"从零写 GRUB"的收尾能力——引导器
  自身也会出错，错误路径与正常路径同等重要。
- **责任边界**：本课**不实现** `grub_errno` 全套错误码枚举与错误栈
  （`grub_error_push/pop`），用**字符串错误表**表达"最后错误"；rescue 命令集
  固定内建子集（不做模块化降级链）。错误处理代码本身也要经受审查——报错路径
  引入的越界/死循环同样是故障。
- **前置知识清单**：
  1. B16：命令注册表（rescue 复用同一命令引擎，只注册子集）；
  2. B17：脚本执行（`script_run_file`，cfg 缺失是 rescue 的触发条件）；
  3. B06/B07：ELF/header 校验（坏文件错误来源）；
  4. B14：`file_open`（缺文件错误）。
- **本课交付**：`build/b22.img`（错误路径演示 + 正常装载 boot）与
  `build/b22-rescue.img`（无 grub.cfg → 直接进 rescue）；FAULT 注入变体
  （`bad.elf`/`badheader.elf`）只在临时副本构造；`make check` + QEMU 两级验证。

---

## 2. 核心概念精讲

### 2.1 概念一：错误对象 —— GRUB 的 grub_errno 思想

定义：整个 GRUB 只有一个"最后错误"全局状态：`grub_errno`（错误码枚举）+ 
`grub_errmsg`（格式化后的消息缓冲区）。

```c
/* grub-core/kern/err.c:27 */
grub_err_t grub_errno;

/* err.c:41 */
grub_error (grub_err_t n, const char *file, const char *function,
            const int line, const char *fmt, ...)
{
  grub_errno = n;
  m = grub_snprintf (grub_errmsg, sizeof (grub_errmsg),
                     "%s:%s:%d:", file, function, line);
  ...
  grub_vsnprintf (grub_errmsg + m, sizeof (grub_errmsg) - m, _(fmt), ap);
  return n;
}
```

为什么需要：GRUB 的调用栈很深（命令 → 文件系统 → 磁盘驱动），每一层都可能
失败。逐层向上传播"是否成功"不够——**错误原因必须跟随调用栈一起冒泡**，最
终由 UI 层统一打印。GRUB 用一个全局 `grub_errno` 做到这点：任何函数失败时
调用 `grub_error(...)` 覆盖最后一次错误，上层在需要时 `grub_print_error()`
打印。

B22 的简化对应（`loader.c:381-402`）：

```c
static const char *last_error = 0;

static __attribute__((noinline)) void err_set(const char *msg)
{
    last_error = msg;
}

static __attribute__((noinline)) void err_print(void)
{
    log_puts("B22 error: ");
    log_puts(last_error ? last_error : "unknown error");
    log_puts("\n");
}
```

- `err_set` = `grub_error` 的"设置错误"动作（省略了文件/函数/行号与格式化）；
- `err_print` = `grub_print_error` 的"打印 error: <msg>"动作；
- `err_get`（`loader.c:399`）= `grub_errno` 的读取动作，供 `set error` 查看。

`__attribute__((noinline))` 是必修课：`-Os` 下这三个函数会被内联吞掉符号，
`make check` 的 `objdump -t` 断言和教学讲解都拿不到符号。

### 2.2 概念二：错误分类 —— 五个错误码对应五类故障

GRUB 的错误码是枚举（`include/grub/err.h`）：

```c
/* include/grub/err.h:32-51（节选） */
GRUB_ERR_BAD_MODULE,      /* 模块文件不是合法 ELF */
GRUB_ERR_BAD_FILE_TYPE,   /* 文件类型不对 */
GRUB_ERR_FILE_NOT_FOUND,  /* 文件不存在 */
GRUB_ERR_UNKNOWN_COMMAND, /* 命令表里没有 */
GRUB_ERR_BAD_OS,          /* OS 镜像无法装载 */
```

B22 的 `cmd_multiboot2_fn`（`loader.c:886-935`）把装载失败按阶段分类，每条
错误消息**唯一可辨识**：

| 阶段 | 判定 | 错误消息 |
|---|---|---|
| 打开文件 | `file_open < 0` | `file not found` |
| 读入缓冲 | `file_read < 0` | `read failed` |
| ELF 校验 | `elf_load < 0`（magic/machine） | `invalid ELF header` |
| mb2 header 校验 | `mb2_header_check < 0`（checksum/对齐） | `invalid multiboot2 header` |
| 命令表查找 | `cmd_find` 未命中 | `command not found` |
| 脚本装载配置 | `script_run_file < 0` | `config file not found` |

关键判据（对照 GRUB 行为）：**坏文件绝不会被装载执行**——`invalid ELF header`
与 `invalid multiboot2 header` 都发生在 `loaded_entry`/`loaded` 赋值之前。

### 2.3 概念三：两级 UI 与 rescue 降级

GRUB 的正常/救援两级界面（`normal/main.c` + `kern/main.c`）：

```c
/* kern/main.c:369 — 配置缺失时进 rescue */
grub_rescue_run ();

/* normal/main.c:319 — "Enter normal mode from rescue mode." */
grub_cmd_normal (struct grub_command *cmd, int argc, char *argv[])
```

- `grub>` 正常模式：模块 + 配置齐全，命令表是完整的；
- `grub rescue>` 救援模式：core 不完整，只剩**内建命令子集**（`set`/`ls`/
  `insmod`/`normal`/…），且 `normal` 命令会**重新猜测并读取 grub.cfg**——
  一旦成功就回到正常模式。

B22 的对应（`loader.c:1030-1052`）：

```c
static void rescue_shell(void)
{
    in_rescue = 1;
    log_puts("B22 rescue: core incomplete, limited commands only\n");
    log_puts("B22 rescue: available: set ls insmod normal halt\n");
    for (;;) {
        int argc;
        log_puts("rescue> ");
        kbd_getline(line, LINE_MAX);
        argc = script_tokenize(line, argv_list, ARGV_MAX);
        if (argc == 0)
            continue;
        if (name_eq(argv_list[0], "set") || name_eq(argv_list[0], "ls") ||
            name_eq(argv_list[0], "insmod") || name_eq(argv_list[0], "normal") ||
            name_eq(argv_list[0], "halt")) {
            cmd_execute(argc, argv_list);
        } else {
            err_set("command not found in rescue");
            err_print();
        }
    }
}
```

要点：

1. **同一个命令引擎，不同注册面**：`loader_main` 注册完整命令表
   （`loader.c:1078-1085`），但 `rescue_shell` 只放行 5 个内建命令——其余一律
   `command not found in rescue`。这复刻了 GRUB "rescue 只有内建命令"的模型；
2. **`normal` 重试 grub.cfg**：`cmd_normal_fn` 再次调用 `script_run_file`；
   成功 → 脚本执行（可含 `boot` 跳内核）；失败 → 打印 `config file not found`
   并**回到 rescue 循环**（串口实测可见 `rescue>` 再次出现）；
3. **`set error` 取证**：`cmd_set_fn` 支持特殊参数 `set error`，通过
   `err_get()` 显示最近一次错误——"报错后现场可查"。

### 2.4 概念四：故障注入与 ISO9660 8.3 截断（验证中发现的真实 bug）

故障注入**只改临时副本**（红线）。Makefile 从 `kernel.elf` 派生两个坏变体：

```make
$(BUILD)/bad.elf: $(BUILD)/kernel.elf
	cp $(BUILD)/kernel.elf $@
	python3 -c "d=bytearray(open('$@','rb').read()); d[0]=0xff; ..."

$(BUILD)/badheader.elf: $(BUILD)/kernel.elf
	cp $(BUILD)/kernel.elf $@
	# mb2 header 在 ELF 文件偏移 0x1000（.multiboot 段）：
	# checksum @ 0x1000+12；改成 0 使 magic+arch+len+checksum != 0
	python3 -c "d=bytearray(open('$@','rb').read()); d[0x100c:0x1010]=b'\x00\x00\x00\x00'; ..."
```

首次验证时 `badheader.elf` 报了 `file not found` 而非 `invalid multiboot2
header`。离线解析 ISO 目录记录找到根因：

```
== BOOT (extent=21 size=2048) ==
  nlen=9  name=b'bad.elf;1'          ← 8.3 内，正常
  nlen=14 name=b'badheade.elf;1'     ← "badheader" 9 字符超 8.3 → 截成 "badheade"
  nlen=12 name=b'kernel.elf;1'       ← 8.3 内，正常
```

xorriso 默认按 **ISO9660 Level 1**（8.3 大写字幕）写主树：basename 超过 8
字符被截断，`badheader.elf` 变 `badheade.elf`，loader 找 `badheader.elf`
自然失败。此前 B13-B21 的所有文件名（`kernel.elf`/`grub.cfg`/`BOOT.BIN`/
`hexdump.mod`）都在 8.3 内，所以一直没暴露。

真 GRUB 用 Rock Ridge / 宽松名保留真名（`grub-core/fs/iso9660.c:2`、
`grub-core/fs/iso9660.c:805-870`：有 RR 条目用 RR 名，否则回退主树名并剥
`;` 版本号、转小写）。我们的 loader 只读主树，因此修复用：

```make
	xorriso -as mkisofs -quiet -V B22TEST -b BOOT.BIN -no-emul-boot \
	  -boot-load-size 4 -boot-info-table -iso-level 3 -relaxed-filenames \
	  -o $@ $(BUILD)/cdroot
```

- `-iso-level 3`：文件名上限 8.3 → 31 字符（长度约束在 conformance level）；
- `-relaxed-filenames`：允许主树小写（字符集约束）。

修复后目录记录恢复 `nlen=15 name=b'badheader.elf;1'`，QEMU 实测
`B22 error: invalid multiboot2 header` 出现。

---

## 3. 对照 GRUB 源码

| B22 实现 | GRUB 2.14 源码 |
|---|---|
| `err_set`/`err_print`/`err_get` | `grub-core/kern/err.c:27`（`grub_errno`）、`:41`（`grub_error`）、`:111`（`grub_print_error`） |
| 错误码五类 | `include/grub/err.h:32-51`（`GRUB_ERR_BAD_MODULE` … `GRUB_ERR_BAD_OS`） |
| `rescue_shell` 内建子集 | `grub-core/kern/main.c:369`（`grub_rescue_run`）；`normal/main.c:556`（rescue 注册 `normal`） |
| `cmd_normal` 重试 grub.cfg | `grub-core/normal/main.c:319`（`grub_cmd_normal`，猜测配置文件名） |
| ISO 8.3 截断与 Rock Ridge | `grub-core/fs/iso9660.c:2,805-870`（RR 名优先，主树名回退 + 剥 `;` + 转小写） |
| 错误消息前缀 `error:` | `grub_print_error` 输出 `"error: %s.\n"` |

---

## 4. 实现解读

### 4.1 装载失败分类（`cmd_multiboot2_fn`）

```c
if (file_open(argv[1], &f) < 0)        { err_set("file not found"); ... }
if (f.size > KERNEL_MAX)               { err_set("kernel too big"); ... }
if (file_read(&f, kbuf, f.size) < 0)   { err_set("read failed");   ... }
if (elf_load(kbuf, f.size) < 0)        { err_set("invalid ELF header"); ... }
if (mb2_header_check(kbuf, f.size, &hdr_off) < 0)
                                       { err_set("invalid multiboot2 header"); ... }
loaded_entry = eh->e_entry;
loaded = 1;                            /* 只有全通过才置位 */
```

错误路径全部 `err_print()` + `return -1`，脚本引擎（B17）继续执行下一行——
这正是 `grub.cfg` 里"坏文件报错后脚本仍继续"的行为来源。

### 4.2 脚本级故障演示（`grub.cfg`）

```
set timeout=0
multiboot2 /boot/bad.elf          ← 报 invalid ELF header
multiboot2 /boot/badheader.elf    ← 报 invalid multiboot2 header
multiboot2 /boot/missing.elf      ← 报 file not found
insmod /boot/missing.mod          ← 报 file not found
echo "B22 script: errors handled, continuing"
multiboot2 /boot/kernel.elf       ← 正常装载
echo "B22 script: kernel loaded, booting"
boot                              ← 跳转
```

### 4.3 rescue 变体

`b22-rescue.img` 的 CD 树**不含 grub.cfg**，`loader_main` 的
`script_run_file("/boot/grub/grub.cfg")` 失败 → `err_print()`（打印
`config file not found`）→ `rescue_shell()`。

---

## 5. 产物与验证

### 5.1 构建产物

- `stage1.S`/`stage2.S`：B15 两段式 El Torito 引导（BOOT.BIN 2048 字节对齐）；
- `loader.c`：命令表 + 错误处理 + rescue；`script.c`：B17 tokenizer；
- `build/b22.img`（错误路径演示）、`build/b22-rescue.img`（无 cfg）；
- `build/bad.elf`、`build/badheader.elf`（FAULT 变体，随 `make` 生成）。

### 5.2 make check 断言

- stage1 = 2048 字节、stage2 < 0x7000、boot.bin 2048 对齐；
- `objdump -t` 断言 `err_set`/`err_print`/`rescue_shell`/`mb2_header_check`
  符号存在（`-Os` 内联防线）；
- 五类错误消息字符串在 `loader.o` 中（`strings`）；
- FAULT 变体内容校验：`bad.elf` 首字节 ≠ `7f`；`badheader.elf` 的
  `magic+arch+len+checksum ≠ 0`；
- `grub-script-check grub.cfg`；xorriso 报告含 `boot-info-table`。

### 5.3 QEMU 验证（`scripts/validate-course.sh b22 qemu`，两步）

第一步，主镜像错误路径（串口 marker）：

```
B22 err: Mini-GRUB fault debugging & rescue
B22 err: boot drive = e0
B22 error: invalid ELF header              ← bad.elf ✓
B22 error: invalid multiboot2 header       ← badheader.elf ✓（8.3 修复后）
B22 error: file not found
/boot/missing.elf                          ← missing.elf ✓
B22 error: file not found
/boot/missing.mod                          ← missing.mod ✓
B22 script: errors handled, continuing
B22 multiboot2: loaded /boot/kernel.elf entry=00100018
B22 boot: jumping to entry=00100018
```

第二步，rescue 变体（无 grub.cfg）：

```
B22 error: config file not found
B22 rescue: core incomplete, limited commands only
B22 rescue: available: set ls insmod normal halt
rescue>
```

`normal` 交互实测：重试 grub.cfg 失败 → 打印 `config file not found` → 回到
`rescue>`（`cmd_normal_fn` 的错误回退路径正确）。

### 5.4 验证工具改进

`scripts/qemu-text-check.sh` 本轮两处健壮性改动（回归 B01-B21 全 PASS）：

1. **串口轮询**：TCG 下 CD 引导慢，`QEMU_SERIAL=1` 时轮询 `serial.log`
   非空（最多 10s）替代固定 `sleep 1`；
2. **VGA 重试**：慢启动内核（B21 L61）的 banner 晚于 loader 串口出现，
   marker 校验重试 dump 最多 6 次（每次 2s），全部命中或超时才停。

---

## 6. 安全边界（沿用课程红线）

- 故障注入（改首字节 / 改 checksum）**只作用于构建目录的派生副本**，绝不
  改写源文件或提交产物；`validate-course.sh` 在临时目录 `cp -a` 后验证；
- 稳定产物 `build/` 按约定以只读权限提交；本轮实验不自动 commit；
- 错误处理代码本身经受审查：`err_print` 对 `last_error == 0` 有兜底
  （"unknown error"），`rescue_shell` 对空行/未知命令都有明确分支。

## 7. 后续课程预告

下一课 [`b23-stable/README.md`](../b23-stable/README.md)：**终课**——端到端
综合验收：source-to-screen 全链路、23 课回归验证、课程地图与扩展方向。
