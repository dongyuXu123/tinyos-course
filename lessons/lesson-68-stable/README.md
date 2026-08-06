# Lesson 68: 进程组与 session 元数据 — 精讲文档

> **课号**：Lesson 68
> **主题**：进程组（process group）与 session 元数据
> **课程主线位置**：第 5 阶段「进程组/session/调度/COW（68–87）」的起始课
> **前置课程**：[Lesson 67（图形桌面综合验收与 GUI 结课）](../lesson-67-stable/README.md)
> **后续课程**：[Lesson 69（session 首领与控制终端所有权）](../lesson-69-stable/README.md)
> **本课一句话目标**：在 GUI 主线结课后回到文本内核主线，用固定元数据模型引入进程组（pgid/首领/成员）与 session（sid/前台/受控终端）概念，并用 `pginfo`/`pgtest` 做确定性校验。

> **Course status: stable snapshot (validated; verified build artifacts included).**

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清进程组、组首领、session、前台进程组、控制终端这几个概念之间的关系，并说清为什么 `pgtest` 断言「pgid==leader、session==pgid、member_count==2、foreground&&controlled」就足以证明「进程组/session 元数据模型」正确。
- **主线位置**：这是第 5 阶段（68–87）的第一课。GUI 主线已在 Lesson 67 结课；本课回到文本内核主线，从进程组/session 这类 job control 的基础元数据开始，后续课程依次扩展 session 首领与控制终端（69）、前台进程组切换与停止组保护（70）、调度/COW 元数据 checkpoint（71–72）等。注意：**本课是「固定元数据 + 确定性验证」教学模型**——不执行任意用户代码，只用固定的小型结构体和确定性断言讲解概念，这与阶段 1–3 的「真实可执行内核」教学方式不同，也与阶段 4 的 GUI 实时交互不同。
- **前置知识清单**：
  1. `task_struct`/`task_table` 的固定 PID/TID/父进程元数据（Lesson 37）；
  2. `fork_model`/`wait_model` 中 parent/child 与 zombie/reap 状态（Lesson 39/57–59）；
  3. `session_job`/`jobtest` 中 init/shell 两个 job 的 argv/fd/pipe 资源账本（Lesson 60）；
  4. 文本 shell 的 `exec64` 命令分发与 `*info`/`*test` 输出约定（贯穿阶段 3）；
  5. 对 GUI 无依赖：本课把 Lesson 67 的鼠标/桌面/Terminal 栈回退为简单 framebuffer 模型，`desktest`/`shellgui` 等仅作回归保留。
- **本课交付（可见结果）**：
  - 新命令 `pginfo`：打印 `pginfo: pgid/leader/session/members: 0000000000000064/0000000000000064/0000000000000064/0000000000000002`；
  - 新命令 `pgtest`：断言进程组元数据一致性，输出 `pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed`；
  - 回归：`help`/`about`（`Lesson 68: 进程组与 session 元数据`）、GUI 回归命令（`guiinfo`/`desktest`/`shellgui` 等）与全部文本诊断命令继续可用。

---

## 2. 核心概念精讲

### 2.1 进程组（process group）

- **定义**：一组共享同一个 PGID（process group ID）的进程的集合；PGID 取值为**组首领**（leader）的 PID。进程组是 job control 的基本单位：一次 shell 管道命令（如 `ls | grep foo`）通常构成一个进程组。
- **为什么需要（动机）**：需要把「一组进程」作为一个整体来发信号（`kill -SIGINT -pgid`）或切换前台/后台。如果只有单进程没有组，`Ctrl-C` 只能杀前台进程本身而杀不掉它的子进程。
- **工作机制**：`struct process_group_model { u32 pgid,leader,session,member_count; u8 foreground,controlled; }` 用固定字段记录组的身份（pgid=100）、首领（leader=100，即「首领的 pid 等于组号」）、所属 session（100）、成员数（2）以及两个布尔属性。
- **示意图**：

```text
  进程组 (pgid=100, leader pid=100)
     ├── 成员 A (pid=100, 组首领)
     └── 成员 B (pid=101)
  成员数 member_count = 2
```

### 2.2 session 与会话成员

- **定义**：session 是多个进程组的集合，由 **session 首领**（session leader）创建；session ID（sid）取值为**该首领的 PID**。终端登录会话、守护进程 `setsid()` 都围绕 session 组织。
- **为什么需要**：session 把「一次登录的整个活动」圈起来——终端关闭时，session 内所有进程组都能收到 `SIGHUP`；`Ctrl-C`/`Ctrl-Z` 只发给当前 session 的**前台进程组**。
- **工作机制**：教学模型中 `session==pgid==100` 表示「session 首领就住在 pgid=100 这个组里」（进程组首领先成为 session 首领），`controlled=1` 表示该 session 受一个控制终端管理。
- **关键不变量**：`pgtest` 断言 `process_group.session==process_group.pgid`——即 session ID 与组首领 PID 一致，这是 POSIX 对「组长进程调用 setsid 成为 session 首领」的教学化表示。

### 2.3 前台进程组（foreground process group）与控制终端（controlling terminal）

- **定义**：每个 session 在任意时刻只有一个**前台进程组**，其他都是后台组；前台组是唯一能读控制终端输入的组。控制终端（`/dev/tty`）归属 session，由 session 首领建立。
- **为什么需要**：终端输入（键盘）同一时刻只能交给一组进程，否则多个进程同时抢读会产生歧义；`Ctrl-C`/`Ctrl-Z` 信号也按「发给前台组」来定向。
- **工作机制**：模型里 `foreground=1` 表示 pgid=100 当前是前台组，`controlled=1` 表示它受控制终端约束。`pgtest` 要求 `foreground && controlled` 同时成立——即「前台 + 有控制终端」才是合法组合。
- **教学边界**：模型不模拟真实 TTY 设备、不派发真实信号，只记录「是否前台/是否受控」两个布尔位；真实设备与信号派发是课程主线后面的内容（阶段 6 的设备课）。

### 2.4 固定元数据 + 确定性验证教学模型（本课最重要的方法论）

- **定义**：内核对象用一个**固定大小、固定初值**的结构体表示；「功能」就是结构体字段的确定性状态转换；「验证」就是断言一组已知初值下的不变量。
- **为什么需要**：真实 Linux 的 `task_struct` 有几百个字段、进程组通过 `signal_struct`/`pid` 命名空间实现，教学内核无法也不应复刻；固定元数据让学习者聚焦**概念与不变量**，不被工程细节淹没。
- **工作机制**：`pginfo()` 每次执行都**重新把结构体赋成同一组初值** `{100,100,100,2,1,1}` 再打印；`pgtest()` 同样先复位再断言 `pgid==leader && session==pgid && member_count==2 && foreground && controlled`。由于初值固定、断言固定，输出完全确定，可进 CI。
- **对比**：这不是「假装实现了功能」，而是把教学目标定为「能写出并解释不变量」。后续 Lesson 69–72 都沿用同一模型，逐步加字段加断言。

---

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 67） |
|---|---|---|
| `boot.S` | Multiboot2 header、32 位启动、进入 long mode | 微小变化：删除未使用的 `MB2_HEADER_TAG_GRAPHICS` 定义（graphics tag 在 67 已移入 grub.cfg） |
| `kernel.c` | 32 位阶段：解析 MBI、framebuffer tag、建页表 | 回退：去掉 Bochs VBE/PCI BAR fallback 与 RGB 位域，恢复「GRUB framebuffer tag 直用」的简单模型（与 Lesson 66 一致） |
| `kernel64.c` | 64 位内核：文本 shell + 全部回归命令 + 进程组模型 | 关键增量：新增 `struct process_group_model`、`pginfo()`、`pgtest()`、`exec64` 两个新分支；**回退**鼠标/IRQ12/桌面图标/Xfce 桌面/图形 Terminal 等 GUI 栈；`about`/启动横幅改为 Lesson 68；主循环恢复纯文本 shell |
| `kernel64.ld` | 64 位 continuation 链接脚本 | 未变化 |
| `linker.ld` | 外层 32 位 ELF 段布局 | 未变化 |
| `Makefile` | 构建/检查/运行 | 变化：`check` 改为 grep `'进程组与 session 元数据'`、`'pgtest'`、`'Lesson 68'` |
| `grub.cfg` | GRUB menuentry | 回退：改为普通文本 menuentry（标题为 lesson 52 样式），不再设置 `gfxmode/gfxpayload` |

> 说明：本课的 GUI 回退是「简化回退」而非「回归错误」——GUI 已结课，文本主线不再需要鼠标/桌面/图形 Terminal，保留 `guiinfo`/`desktest`/`shellgui` 等命令作最小回归（仍走简单 framebuffer 直写路径），同时减少维护面。

### 3.2 kernel64.c 精讲

#### 3.2.1 进程组模型结构体（核心新增）

```c
struct process_group_model { u32 pgid,leader,session,member_count; u8 foreground,controlled; };
static struct process_group_model process_group;
```

- `pgid`：进程组号（Process Group ID），同时是组首领的 PID；
- `leader`：组首领 PID；POSIX 约定「leader 的 PID == pgid」；
- `session`：所属 session 的 sid；组长进程调用 `setsid()` 后 sid == 组首领 PID；
- `member_count`：组成员个数（确定性验证断言为 2）；
- `foreground`：该组是否为 session 的**前台进程组**（1=是）；
- `controlled`：该 session 是否**受控制终端管理**（1=是）；
- 设计动机：把 Linux 中分散在 `task_struct->signal`、`sighand`、`tty` 里的 job-control 信息压缩成一个 6 字段的固定记录，只保留「身份 + 两个布尔位」，是「固定元数据」模型的最小可教学切片。

#### 3.2.2 pginfo（信息命令，关键函数 ≥3 行分析）

```c
static TEXT64 void pginfo(u16*c){
  process_group=(struct process_group_model){100,100,100,2,1,1};
  text64(c,"pginfo: pgid/leader/session/members: ");
  hex64(c,process_group.pgid);text64(c,"/");
  hex64(c,process_group.leader);text64(c,"/");
  hex64(c,process_group.session);text64(c,"/");
  hex64(c,process_group.member_count);putc64(c,'\n');}
```

- **签名与职责**：`void pginfo(u16*c)`，把进程组模型**重置为固定初值** `{100,100,100,2,1,1}` 并打印四个身份字段；
- **输入输出**：输入为空命令（`exec64` 分支保证 `noargs64`），输出到 VGA 文本 shell；每次调用都会覆盖 `process_group`——这是刻意为之，保证「先复位再观察」；
- **算法步骤**：① 复位模型；② 打印前缀 `pginfo: pgid/leader/session/members: `；③ 依次 hex 输出 pgid、leader、session、member_count，用 `/` 分隔；
- **边界与错误处理**：不依赖任何外部状态，无参数解析失败路径；`member_count` 用 u32 承载，足够容纳教学规模的成员数；
- **为什么这样设计**：信息命令与验证命令（`pginfo`/`pgtest`）共用同一复位初值，确保「打印即一致、断言即通过」，是确定性验证模型的标准形态。预期输出（100 的 hex 是 `0000000000000064`、2 是 `0000000000000002`）：

```text
pginfo: pgid/leader/session/members: 0000000000000064/0000000000000064/0000000000000064/0000000000000002
```

#### 3.2.3 pgtest（确定性验证，关键函数 ≥3 行分析）

```c
static TEXT64 void pgtest(u16*c){
  process_group=(struct process_group_model){100,100,100,2,1,1};
  int ok=process_group.pgid==process_group.leader&&
          process_group.session==process_group.pgid&&
          process_group.member_count==2&&
          process_group.foreground&&process_group.controlled;
  text64(c,"pgtest: ");
  text64(c,ok?"bounded process-group leader, session, foreground, and controlling-terminal metadata passed":"process-group fallback reported");
  putc64(c,'\n');}
```

- **签名与职责**：`void pgtest(u16*c)`，复位模型后验证四条 job-control 不变量；
- **不变量清单**（逐条对照 POSIX 概念）：
  1. `pgid==leader`：组号等于组首领 PID（组必须有个「带头」的成员）；
  2. `session==pgid`：sid 等于组首领 PID（组首领先成为 session 首领，session 才能建立）；
  3. `member_count==2`：模型固定两成员（首领先、再加一个）——成员数可观测且确定；
  4. `foreground && controlled`：组处于前台且受控制终端约束——这是「能读终端输入、能被终端信号定向」的最小条件；
- **边界与错误处理**：若任一不变量不成立则输出 fallback 文案 `process-group fallback reported`（不 panic、不停机，符合确定性模型「宁可报 fallback 也不崩溃」的约定）；
- **为什么这样设计**：四条断言覆盖了进程组「身份、归属、规模、状态」四个维度，是最小的完备集；后续 Lesson 69/70 会把 `session`/`foreground` 变成可动态转换的状态机，本课先锁死初值让模型立住。预期输出（逐字抄录自源码）：

```text
pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed
```

#### 3.2.4 exec64 新分支与横幅变化

`exec64` 在 `shellgui` 分支后新增：

```c
else if(eq64(word,"pginfo")){if(!noargs64(arg))usage64(c,"pginfo");else pginfo(c);}
else if(eq64(word,"pgtest")){if(!noargs64(arg))usage64(c,"pgtest");else pgtest(c);}
```

- 与既有命令统一形态：`noargs64` 拒绝参数，`usage64` 打印用法，否则调用处理函数；
- `about` 输出改为 `Lesson 68: 进程组与 session 元数据\n`；启动横幅改为：

```text
Lesson 68: 进程组与 session 元数据
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

- `help` 命令清单恢复为文本主线样式（无 iconinfo/icontest/desktopinfo 前缀，`pginfo`/`pgtest` 未列入清单——与 lesson-66 相同的「help 不全」约定，可通过 `about` 与本文档确认）。

#### 3.2.5 主循环与 GUI 回退

```c
threads[0].id=0;threads[0].state=THREAD_RUNNING;quantum_left=TIME_SLICE_TICKS;framebuffer_init(h);
char cmd[32];u8 ch;__asm__ volatile("cli":::"memory");
/* ...初始化序列不变... */
clear64(&c);(void)exec_validate();
text64(&c,"Lesson 68: 进程组与 session 元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
prompt64(&c);__asm__ volatile("sti":::"memory");
for(;;){
  if(!kbd_dequeue(&ch)){__asm__ volatile("sti; hlt":::"memory");continue;}
  if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}
  else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}
  else if(n<31){cmd[n++]=(char)ch;putc64(&c,(char)ch);}}
```

- **主循环回退**：Lesson 67 的 `mouse_poll()`/`desktop_dirty`/`mouse_cursor_dirty`/`gui_term_input_dirty` 事件泵全部移除，恢复「键盘出队 → 文本 shell」的简洁形态；空闲用 `sti; hlt`；
- **初始化回退**：删除 `bochs_vbe_init()`、`mouse_hw_init()`、`pic_masks(0xf8,0xef)`、首帧 `xfce_desktop()`，`framebuffer_init(h)` 用 GRUB 直给的 framebuffer 参数初始化简单模型；
- **GUI 子系统移除**：`mouse_model`/`irq12_record`/`desktop_icon_model`/`xfce_desktop`/`gui_term_*` 等全部删除；`framebuffer_model` 回到 Lesson 66 的 8 字段版本（无 backbuffer/scene/RGB 位域），`framebuffer_pixel` 直接写显存；
- **为什么回退而不是保留**：GUI 已结课，文本主线不再有「Terminal 打开时输入进 GUI 行」的双模式路由；删除未用代码减少回归面，也让本课专注进程组概念。保留 `desktest`/`shellgui`/`guiinfo` 等命令用于快速确认 framebuffer 直写路径仍工作。

### 3.3 kernel.c 精讲（回退点）

- `struct mb2_framebuffer_tag` 恢复为不带 RGB 位域的 8 字段版本；`long_mode_handoff` 去掉 `framebuffer_phys_base`/`framebuffer_map_offset`/RGB 六字段；
- `prepare_memory_map()` 的 framebuffer 分支恢复简单校验（`bpp==32 && type_field==1 && 页对齐 && pitch>=width*4 && height && 总字节≤2MiB`），不再有 Bochs PCI fallback；
- 删除 `pci_cfg_read/write`、`bochs_pci_init`、`bochs_lfb`；`setup_long_mode_tables()` 的 framebuffer 映射恢复单页表直映射方式；
- 结论：32 位阶段的改动全部是「还原 66 之前形态」，没有引入任何新功能——本课真正的增量在 64 位侧的进程组模型。

### 3.4 构建管线（Makefile / linker）

- `kernel64.o`：`gcc -m64 -ffreestanding -fpie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Werror`，与 67 相同；
- `kernel64.bin` → `boot.S` `.incbin` 嵌入 → `kernel.elf` → `grub-mkrescue` 生成 iso，链路不变；
- `check` 目标变化：`grub-file --is-x86-multiboot2` + grep README `'进程组与 session 元数据'` + grep kernel64.c `'pgtest'` + grep README `'Lesson 68'`；
- 相对上一课：无新增编译标志；`grub.cfg` 去掉 `gfxmode/gfxpayload`（文本模式即可），`run` 命令仍为 `qemu-system-x86_64 -accel tcg -boot order=d -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown`。

### 3.5 主控制流

```text
GRUB → boot.S(32 位 _start) → kernel.c kernel_main32(解析 MBI/framebuffer tag)
→ setup_long_mode_tables(建页表) → boot.S enter_long_mode(64 位)
→ kernel64.c kernel_main64_binary(framebuffer_init + 全套元数据模型初始化)
→ 横幅 "Lesson 68: 进程组与 session 元数据" → 文本 shell 主循环
→ 输入 pginfo/pgtest → exec64 新分支 → pginfo()/pgtest() 复位并打印/断言
```

---

## 4. 数据流与运行逻辑

**进程组命令的数据流**（命令 → exec64 分支 → 函数 → 输出格式串 → 屏幕）：

1. 用户在 `tinyos>` 输入 `pginfo` 并回车；
2. 主循环 `kbd_dequeue` 取出字符，`\n` 触发 `exec64(&c,h,cmd)`；
3. `exec64` 命中 `eq64(word,"pginfo")` 分支 → `noargs64(arg)` 通过 → 调 `pginfo(c)`；
4. `pginfo` 把 `process_group` 复位为 `{100,100,100,2,1,1}`，逐字段打印 → VGA 输出 `pginfo: pgid/leader/session/members: 0000000000000064/0000000000000064/0000000000000064/0000000000000002`；
5. 输入 `pgtest` → 同样复位 → 断言四条不变量 → 输出 `pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed`。

**GUI 回归命令的数据流**：`desktest`/`shellgui` 仍走 `exec64`，直接调用 `framebuffer_rect`/`canvas_text` 等直写显存原语（无 backbuffer），属于 Lesson 61/66 语义的简化回归，不作为 GUI 结课证据。

---

## 5. 构建、运行与验证

依赖：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`。

```bash
cd lessons/lesson-68-stable
make -j"$(nproc)"
make check          # 预期：Multiboot2 and Lesson 68 checks passed.
make run            # QEMU 图形窗口（文本 shell）+ 串口 stdio
```

**验证步骤**（在 `tinyos>` 提示符下）：

| 命令 | 预期输出（逐字抄录自 kernel64.c） |
|---|---|
| `pginfo` | `pginfo: pgid/leader/session/members: 0000000000000064/0000000000000064/0000000000000064/0000000000000002` |
| `pgtest` | `pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed` |
| `about` | `Lesson 68: 进程组与 session 元数据` |
| `help` | 文本主线命令清单（以 `commands: help about pipeinfo pipetest polltest ...` 开头） |
| `guiinfo` | `guiinfo: framebuffer addr/pitch/size/bpp/type: ... ready/mapped: ...`（确认 framebuffer 直写模型仍工作） |
| `desktest` | `desktest: bounded compositor background, taskbar, windows, and cursor ownership passed` |
| `shellgui` | `shellgui: graphical terminal and system status panel linked to init/session metadata passed` |
| 其余回归 | `meminfo`/`taskvalidate`/`forktest`/`waitpidtest`/`jobtest` 等全部通过 |

**如何判断成功**：`make check` 打印 `Multiboot2 and Lesson 68 checks passed.`；QEMU 中 `pginfo`/`pgtest` 输出与上表逐字一致；本课不依赖 GUI 画面作为验收证据（GUI 已在 67 结课），文本输出即证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `pginfo` 输出与预期不一致（如 sid/leader 非 100） | 初值复位缺失或字段序错位 | 对照 `struct process_group_model` 字段顺序与 `{100,100,100,2,1,1}` 初始化列表顺序（pgid,leader,session,member_count,foreground,controlled） |
| `pgtest` 输出 fallback 文案 | 四条不变量中某条不成立（字段被 `pginfo` 以外的代码改动） | 在 `pgtest` 中逐条打印 `pgid==leader`、`session==pgid`、`member_count==2`、`foreground&&controlled` 的布尔值定位 |
| `pginfo`/`pgtest` 显示 unknown command | `exec64` 未加分支，或拼写与命令名不一致 | grep kernel64.c 确认 `eq64(word,"pginfo")`/`eq64(word,"pgtest")` 分支存在且命令名无大小写差异 |
| 输入 `pginfo` 时提示 usage | `noargs64(arg)` 未通过（行尾有空格） | 确认 shell 命令后无尾随空格；`noargs64` 要求命令串完全为空 |
| GUI 命令（`desktest` 等）输出 fallback | framebuffer 未 ready/mapped（GRUB 未给图形 payload） | 先跑 `guiinfo` 看 `ready/mapped`；本课已回退到 GRUB framebuffer 直用，无 Bochs fallback，需确认 grub.cfg/启动环境给了 framebuffer |
| `about`/启动横幅仍显示 Lesson 67 | kernel64.c 横幅字符串未更新 | grep `Lesson 68` 应命中启动横幅与 `about` 分支两处 |
| `make check` 失败（grep 不通过） | README/kernel64.c 关键词缺失 | `check` 目标 grep `'进程组与 session 元数据'`、`'pgtest'`（kernel64.c）、`'Lesson 68'` |
| 反汇编里仍有 `irq12`/`mouse` 符号 | GUI 栈未完全移除或 objcopy 二进制残留 | grep -c `mouse\|irq12\|xfce\|gui_term` kernel64.c 应为 0（inputtest 里的 `input_mouse` 除外） |

---

## 7. 与 Linux 源码对照

- **进程组与 PID**：Linux 中每个进程组由 `task_struct->signal->pids[PIDTYPE_PGID]` 与 `signal->leader` 描述，`getpgrp()` 返回 `process_group`；TinyOS 用 `process_group.pgid==leader` 一句表达「组号等于首领 PID」。对照文件：`include/linux/sched/signal.h`、`kernel/sys.c`。
- **session 与 session 首领**：Linux 中 `task_struct->signal->pids[PIDTYPE_SID]`、`signal->leader` 标识 session 首领；`setsid()` 要求调用者不是组首领且 `pgid==pid` 才创建新 session。TinyOS 用 `session==pgid` 表示「组长即 session 首领」。对照文件：`kernel/sys.c`（`ksys_setsid`）。
- **前台进程组与控制终端**：Linux 中 `tty_struct->pgrp` 记录当前前台进程组，`tcgetpgrp()/tcsetpgrp()` 读写它；`Ctrl-C` 通过 `tty->pgrp` 定向 `SIGINT`。TinyOS 只保留 `foreground`/`controlled` 两个布尔位。对照文件：`drivers/tty/tty_io.c`、`drivers/tty/tty_jobctrl.c`。
- **教学模型简化了什么**：没有 PID 命名空间、没有 `signal_struct` 引用计数、没有真实 TTY 与信号派发、没有 `wait` 与进程组回收联动；把「一组不变量」从几百个字段中抽取成 6 个字段，目标是概念先行。
- 权威来源：POSIX.1-2017（`setpgid`/`getpgrp`/`setsid`/`tcgetpgrp` 语义）、Linux `kernel/sys.c`、`drivers/tty/tty_jobctrl.c`。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `pgtest` 要求 `session==pgid`？如果改成 `session==100`（固定常量）能通过吗？两者在语义上有什么差别？
2. **源码定位**：`pginfo()` 和 `pgtest()` 都以 `process_group=(struct process_group_model){100,100,100,2,1,1}` 开头，为什么不能省略？省略后 `pgtest` 会怎样？
3. **动手实验**：把 `process_group.member_count` 的断言值从 2 改成 3，`pgtest` 输出会变为什么？再把初值里的 `member_count` 也改成 3，输出又如何？这说明了「初值与断言必须成对」吗？
4. **动手实验**：在 `exec64` 中把 `pginfo`/`pgtest` 分支删掉，重新 make/run，输入 `pginfo` 会得到什么？结合 `help` 清单也未列出这两个命令，讨论「命令注册与帮助清单」是否需要同步维护。
5. **Linux 对照**：在 `kernel/sys.c` 找 `ksys_setsid()`，它要求「调用者 PID 必须等于其进程组 PGID」才能建新 session。TinyOS 的 `session==pgid` 与这条真实规则是什么关系？若把模型扩展成「第二个组加入 session」，还应新增哪些字段？

---

## 9. 本课小结与下一课预告

- 本课是第 5 阶段「进程组/session/调度/COW」的开篇，也是 GUI 结课后回归文本内核主线的第一课；
- 明确了「固定元数据 + 确定性验证」教学模型：结构体定初值、命令打印、断言输出 passed/fallback，输出完全可预测可进 CI；
- 引入进程组四要素：`pgid`、`leader`（组首领）、`session`、`member_count`，以及 `foreground`/`controlled` 两个布尔属性；
- `pginfo` 复位并打印身份字段，`pgtest` 用四条不变量验证「组长即首领、组长即 session 首领、两成员、前台且受控」；
- 本课把 Lesson 67 的鼠标/桌面/图形 Terminal 栈回退为简单 framebuffer 直写模型，保留 `desktest`/`shellgui`/`guiinfo` 作回归，主循环恢复纯文本 shell；
- 构建侧把 `grub.cfg` 回退为文本 menuentry，`check` 目标改为 grep 本课关键词。

下一课 [Lesson 69（session 首领与控制终端所有权）](../lesson-69-stable/README.md) 将把「进程组模型」推进为「session 首领合法性」与「控制终端所有权转移」：验证只有 session 首领能创建会话、只有首领能获取/释放控制终端，并新增 `sessiontest` 等确定性命令。你会看到同一个结构体如何从「静态初值」演进成「状态机」。
