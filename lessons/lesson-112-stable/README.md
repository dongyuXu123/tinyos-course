# Lesson 112: VFS/设备/epoll/服务综合验证 — 精讲文档

> **课号**：Lesson 112（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：VFS/设备/epoll/服务综合验证（Integrated VFS, Devices, epoll, and Service
> Validation）
> **课程主线位置**：第 5 阶段「VFS/设备/epoll/服务管理检查点」主线收官站（100–114）。
> 前课 Lesson 111（守护进程生命周期）完成服务生命周期的检查点；本课把 VFS、设备、epoll
> 与服务管理四类元数据放在一个检查点里综合复验；Lesson 113 起主线转入 mutex/spinlock
> 并发主题。
> **前置课程**：[`lesson-111-stable/README.md`](../lesson-111-stable/README.md)
> **后续课程**：[`lesson-113-stable/README.md`](../lesson-113-stable/README.md)
> **一句话目标**：理解 `l112test` 检查点如何用 `lesson_105_model` 对「bounded VFS、
> 设备、epoll 与服务管理」整套元数据做收官综合验证，并回顾这些机制在 kernel64.c 中的
> 实际载体（inode/dentry/file/fd、ramfs、pipe/poll、module、init/session）。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能把「VFS、设备、epoll、服务管理」四类机制在源码中的结构
逐个指出来，并解释为什么 `l112test` 的一条输出串可以代表这四类的综合健康——
因为每个子系统都有自己的 `xxxinfo`/`xxxtest` 命令，检查点只是把「整体自洽」压缩成
一行断言。`l112test` 输出
`bounded VFS, devices, epoll, and service management checkpoint passed` 即通过。

- **在课程主线中的位置**：第 5 阶段检查点主线（约 100–114）的收官站。100–108 已把
  设备打开/ioctl、块设备请求队列、设备生命周期、poll/epoll 边沿/水平触发、epoll
  wait/wake、服务状态机逐个建模；109–111 以服务拓扑/回滚/守护进程为主题复验；
  112 一次性地把四类机制放到同一个检查点模型里。
- **责任边界**：本课**不新增**机制（无新表、新命令逻辑）。新增的只有
  `struct lesson_105_model`/`lesson_105_state`/`l112test()` 与主题串。
- **前置知识清单**：① VFS 四表（`inode_table`/`dentry_table`/`file_table`/`fd_table`）
  与 `ramfs_nodes`；② 设备/模块模型（`module_model`/`symbol_model`，`moduletest`）；
  ③ poll/epoll 就绪语义（`pipe_poll`、`polltest`）；④ 服务管理模型
  （`init_model`/`session_job`/`shell_runtime`）。
- **本课交付**：命令 `l112test`（新增）与 `l104test`（由 111 课 `l111test` 更名）；
  `about`/横幅显示 `Lesson 112: VFS/设备/epoll/服务综合验证`；
  `make check` 输出 `Multiboot2 and Lesson 112 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：综合验证（Integrated Validation）检查点

定义：把多个子系统各自的健康断言合并成**一个**检查点模型，用一个命令输出「整体通过/
回退」。这是检查点主线的收官形态——前面每课验证单个主题，这里验证「整套机制仍然自洽」。

为什么需要：TinyOS 没有真实文件系统、设备、epoll 与进程调度，一切是元数据。元数据
最大的风险是「字段被悄悄改写而不自知」。综合验证把每类子系统的关键不变量（合法、激活、
就绪、记账 + 步进编号）收进一个模型，一旦任何子系统被改坏，`l112test` 立即变 fallback。

工作机制：`l112test` 注入 `{105U,106U,107U,108U,1,1,1,1}` 到 `lesson_105_state`，断言
`valid&&active&&ready&&accounted&&b==a+1U`。四个状态位不针对某个子系统，而是
「整体合法/整体激活/整体就绪/整体记账」——与旧 README 反复强调的
`bounded VFS, devices, epoll, and service management metadata` 措辞一致。

### 2.2 概念二：VFS 元数据（Lesson 44/88 起继承）

定义：`inode_model`/`dentry_model`/`file_model`/`fd_model` 四张表描述「文件对象层级」；
`ramfs_nodes` 描述内存文件系统路径树。

为什么需要：所有「打开文件、读写偏移、引用计数」语义都在这四张表里；`ramfs_lookup`
给出路径→inode 的映射（`/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh`）。

工作机制（源码证据见 §3.3）：`vfs_init()` 初始化四表；`fd_open_model`/`fd_close_model`/
`fd_read_model` 维护 fd↔file↔inode 引用与偏移；`fdtest` 输出
`fd/file/inode/dentry refs and offsets passed`。

### 2.3 概念三：设备/模块元数据（Lesson 100–102 起继承）

定义：`module_model`/`symbol_model` 表示「已加载、已初始化」的设备/模块与其导出符号。

为什么需要：设备打开（100 课）、块设备请求队列（101 课）、设备生命周期与卸载（102 课）
在 TinyOS 里都是模块元数据：模块必须先 `initialized`，其导出符号才能被查找命中。

工作机制（源码证据见 §3.3）：`module_init_model()` 预置 core（0x636f7265）与 vfs
（0x766673）两个已初始化模块与 pmm/vfs 两个导出符号；`module_lookup` 在
`exported_symbols[]` 中按 name_hash 匹配；`moduletest` 断言「已知符号命中、未知符号
未命中、模块已初始化」，输出 `module init order and exported-symbol lookup passed`。

### 2.4 概念四：epoll/poll 就绪语义（Lesson 103–107 起继承）

定义：`pipe_poll(mask)` 根据管道 used/空余量计算 `POLL_IN`/`POLL_OUT` 就绪位。

为什么需要：poll（103 课）、epoll 实例与固定 watch 表（104 课）、边沿/水平触发
（105/106 课）、epoll wait/wake（107 课）的教学核心都是「如何判定就绪、如何通知等待者」；
TinyOS 用 `pipe_model` + 两个 wait queue 模拟最简就绪语义。

工作机制（源码证据见 §3.3）：`pipe_poll(POLL_IN)` 在 `used>0` 时返回就绪；
`pipe_poll(POLL_OUT)` 在 `used<PIPE_CAP` 时返回就绪；`polltest` 走完整的
IN→OUT 转换序列，输出 `POLLIN/POLLOUT readiness transitions passed`。

### 2.5 概念五：`lesson_105_model` 检查点模型

```c
struct lesson_105_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_105_model lesson_105_state;
```

与 104 模型同构，编号前进一位；`l112test` 注入 `{105U,106U,107U,108U,1,1,1,1}` 并断言
`valid&&active&&ready&&accounted&&b==a+1U`。这是本阶段检查点模型序列
（101→102→103→104→105）的终点，编号 105 对应「VFS/设备/epoll/服务」机制源课。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-111） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | 未变化 |
| `kernel.c` | 32 位阶段 | 未变化 |
| `kernel64.c` | 64 位内核主体 | `l111test`→`l104test` 更名；新增 `struct lesson_105_model`/`lesson_105_state`/`l112test`；exec64 增 `l112test` 分支；`about` 与横幅改「Lesson 112: VFS/设备/epoll/服务综合验证」 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 + check | check grep 更新为 `VFS/设备/epoll/服务综合验证`/`l112test`/`Lesson 112` |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 本课增量（源码逐字）

更名命令 `l104test`（即 111 课的 `l111test`）：

```c
static TEXT64 void l104test(u16*c){lesson_104_state=(struct lesson_104_model){104U,105U,106U,107U,1,1,1,1};int ok=lesson_104_state.valid&&lesson_104_state.active&&lesson_104_state.ready&&lesson_104_state.accounted&&lesson_104_state.b==lesson_104_state.a+1U;text64(c,"l104test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 104 fallback reported");putc64(c,'\n');}
```

- 函数名改为 `l104test`（= 模型号 104），逻辑逐字未变。

新增结构与命令：

```c
struct lesson_105_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_105_model lesson_105_state;
static TEXT64 void l112test(u16*c){lesson_105_state=(struct lesson_105_model){105U,106U,107U,108U,1,1,1,1};int ok=lesson_105_state.valid&&lesson_105_state.active&&lesson_105_state.ready&&lesson_105_state.accounted&&lesson_105_state.b==lesson_105_state.a+1U;text64(c,"l112test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 105 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 结构 `lesson_105_model`：4 个 u32 计数 + 4 个 u8 状态位，检查点序列第 5 个；
2. 全局 `lesson_105_state`：默认全零，仅 `l112test` 注入；
3. `l112test` 赋值 `{105U,106U,107U,108U,1,1,1,1}`：`a=105,b=106,c=107,d=108`，四状态位
   全 1——「VFS/设备/epoll/服务」整体激活、就绪、记账；
4. 断言 `ok=`：五条件 AND；
5. 输出：`l112test: ` + 成功串或 `Lesson 105 fallback reported`。

exec64 新增分支（源码逐字）：

```c
else if(eq64(word,"l104test")){if(!noargs64(arg))usage64(c,"l104test");else l104test(c);}else if(eq64(word,"l112test")){if(!noargs64(arg))usage64(c,"l112test");else l112test(c);}
```

主题横幅与 about（源码逐字）：

```c
text64(&c,"Lesson 112: VFS/设备/epoll/服务综合验证\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

```c
text64(c,"Lesson 112: VFS/设备/epoll/服务综合验证\n");
```

### 3.3 继承机制精讲——四类机制的源码载体

VFS 四表与 ramfs（源码逐字）：

```c
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
static struct inode_model inode_table[INODE_MAX];
static struct dentry_model dentry_table[DENTRY_MAX];
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static struct ramfs_node ramfs_nodes[RAMFS_MAX];
```

- 四表关系：`fd_table[i].file_index` → `file_table[f].inode_index` → `inode_table[n]`；
  dentry 记录 name_hash→inode_index 的路径映射；
- `ramfs_lookup` 硬编码 5 个路径（`/`→0、`/etc`→1、`/etc/motd`→2、`/bin`→3、
  `/bin/sh`→4），未知路径返回 -1；
- `fd_open_model` 双层循环找空闲 fd 与空闲 file，成功后 `inode_table[inode].refs++`；
  `fd_close_model` 逆序递减引用，`refs` 归零才释放 file——这就是 VFS 引用计数的教学版。

设备/模块模型（源码逐字）：

```c
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
struct symbol_model { u64 name_hash,owner; u8 exported,valid; };
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
```

- `module_init_model()` 把模块 0（0x636f7265=core）与模块 1（0x766673=vfs）都置
  `loaded=1,initialized=1`，导出符号 `pmm`（0x706d6d，owner 0）与 `vfs`（owner 1）；
- `module_lookup` 只认 `valid && exported && name_hash 匹配` 的符号——未导出或未初始化
  的符号查不到，这就是「设备必须先初始化才能被打开」的语义；
- `moduletest` 断言 `a=module_lookup(0x706d6d)`（pmm 命中）、`b=module_lookup(0x6d697373)`
  （miss 未命中）、`d=两个模块都已初始化`，输出
  `module init order and exported-symbol lookup passed`。

poll 就绪语义（源码逐字）：

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

- `POLL_IN` 就绪 = 管道有数据（`used>0`）；`POLL_OUT` 就绪 = 管道有空间
  （`used<PIPE_CAP`）；
- `polltest` 用 `pipe_init` 后先断言 `pipe_poll(POLL_IN)==0`（空管道不可读）、
  `pipe_poll(POLL_OUT)==POLL_OUT`（空管道可写），写入后再断言 `pipe_poll(POLL_IN)
  ==POLL_IN`，最后填满断言 `pipe_poll(POLL_OUT)==0`——完整的就绪转换序列。

服务管理模型（源码逐字）：

```c
struct init_model { u64 pid,started,commands,files,processes,pipes,signals; u8 ready; };
struct shell_runtime_model { u64 starts,commands,execs,exits,argv_words,env_words,pipe_links,signal_links,timer_links,deferred_links; u8 ready; };
```

- init（pid=1）是服务根，`shell_runtime` 记录 shell 与各子系统的链接计数；
- `initinfo` 输出 `init pid/ready/commands/files/processes/pipes/signals:` 七项；
- `shelltest`/`shellrun`/`jobtest` 分别验证 init/shell 协调、`/bin/sh` 镜像执行、
  两个 session job 的生命周期。

### 3.4 构建管线

与 lesson-111 完全一致（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse* -Werror`
编译，`ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary`，boot.S `.incbin`，
`grub-mkrescue`）。`check` 目标：`grub-file --is-x86-multiboot2` + grep
`VFS/设备/epoll/服务综合验证`/`l112test`/`Lesson 112`，通过打印
`Multiboot2 and Lesson 112 checks passed.`。`run` 用 `-accel tcg -serial stdio` 的 QEMU。

### 3.5 主控制流

```text
GRUB → kernel_main32 → 长模式 → kernel_main64_binary
  ├─ module_init_model(); init_model_start(); wait_model_start();
  │  adoption_start(); resource_start(); pmm_init(h); vma_init();
  │  reclaim_init(); vfs_init(); address_space_init(...)
  ├─ ... framebuffer_init(h); install_idt(h); pit_init(); pic_init()
  ├─ 横幅 "Lesson 112: VFS/设备/epoll/服务综合验证\nGETTICKS, ... bounded reclaim metadata\n"
  └─ 键盘循环 → exec64
        ├─ l104test → 复验 lesson_104 检查点（更名命令）
        ├─ l112test → 复验 lesson_105 检查点（本课新增，综合验证）
        └─ fdtest / pathtest / polltest / moduletest / initinfo / jobtest 等继承命令
```

---

## 4. 数据流与运行逻辑

```text
输入 "l112test" → exec64 → l112test(c)
  → lesson_105_state = {105,106,107,108, 1,1,1,1}
  → ok = valid(1)&&active(1)&&ready(1)&&accounted(1)&&b(106)==a(105)+1
  → "l112test: bounded VFS, devices, epoll, and service management checkpoint passed"

四类机制证据链：
  VFS:   fdtest / pathtest / ramfsinfo / fdinfo
 设备:   moduletest / moduleinfo
  epoll: polltest / pipeinfo
  服务:   initinfo / shelltest / shellrun / jobtest / sessioninfo
```

任何一类机制被改坏，其专属 test 命令与 `l112test` 都会变 BROKEN/fallback——
综合验证的价值就在于此。

---

## 5. 构建、运行与验证

### 5.1 依赖

`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 112 checks passed.`（README 必须含
`VFS/设备/epoll/服务综合验证` 与 `Lesson 112`，kernel64.c 必须含 `l112test`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 112: VFS/设备/epoll/服务综合验证\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l112test
```

预期：

```text
l112test: bounded VFS, devices, epoll, and service management checkpoint passed
```

```bash
about
```

预期：`Lesson 112: VFS/设备/epoll/服务综合验证`

```bash
fdtest
```

预期：`fdtest: fd/file/inode/dentry refs and offsets passed`

```bash
moduletest
```

预期：`moduletest: module init order and exported-symbol lookup passed`

```bash
polltest
```

预期：`polltest: POLLIN/POLLOUT readiness transitions passed`

继承回归：`l104test`、`pathtest`、`shellrun`、`jobtest`、`reparenttest` 行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「Commands: `l105test`」——**命令名以源码为准勘误**：本课源码可执行命令是
`l104test`（更名）与 `l112test`（新增）；`l105test` 是模型编号（lesson_105），不是命令。
`make check` 复验输出 `Multiboot2 and Lesson 112 checks passed.`，`l112test` 显示
passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l112test` 输出 `Lesson 105 fallback reported` | `lesson_105_state` 未注入或断言不成立 | 核对 `l112test` 赋值 `{105U,106U,107U,108U,1,1,1,1}` 与 `ok` 五条件 |
| `fdtest` 输出 `BROKEN` | `vfs_init` 未初始化四表，或 fd_open/fd_close 引用计数错 | 先 `fdinfo`/`ramfsinfo` 查看表状态；确认 `vfs_init()` 在 main 中被调用 |
| `moduletest` 输出 `BROKEN` | `module_init_model` 未把模块置 initialized，或 name_hash 改错 | 先 `moduleinfo`；确认 `0x636f7265`/`0x766673` 与 `0x706d6d` 哈希 |
| `polltest` 输出 `BROKEN` | `pipe_model` 状态未复位（used 非 0） | 确认 `polltest` 先 `pipe_init()`；检查 `pipe_poll` 的 `used` 判定 |
| `make check` 报错 | README 缺 `VFS/设备/epoll/服务综合验证`/`Lesson 112` 或 kernel64.c 缺 `l112test` | 对照 Makefile 三条 grep |
| `about` 显示旧课号 | 主题串未更新 | grep `Lesson 112: VFS/设备/epoll/服务综合验证` kernel64.c |
| help 列表找不到 `l112test` | 已知小瑕疵：help 串未追加检查点命令 | 用 `about`/README 发现命令 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `inode_model`/`dentry_model`/`file_model`/`fd_model` 四表 | `include/linux/fs.h`：`struct inode`/`struct dentry`/`struct file`；`fs/file.c` fdtable | 四层对象层级与 Linux 一致；教学模型只有 3–4 个槽位 |
| `ramfs_lookup` 硬编码路径树 | `fs/ramfs/inode.c`（ramfs）；`fs/dcache.c` 路径解析 | Linux 用 dentry 缓存+inode 哈希；教学模型写死 5 条路径 |
| `module_lookup` 只命中 exported+valid 符号 | `kernel/module.c` `find_symbol()`；`kernel/kallsyms.c` | 导出符号查找语义一致；教学模型固定 2 模块 2 符号 |
| `pipe_poll` 的 IN/OUT 就绪判定 | `fs/pipe.c` `pipe_poll()`（`POLLIN`/`POLLOUT` 按 used 计算） | 判定逻辑一致；教学模型用 u8 used 计数，无真实缓冲 |
| `lesson_105_model` 综合检查点 | LTP/kselftest 的子系统回归套件 | 教学模型用 4 位状态代表整体健康；Linux 用测试框架逐项断言 |

**权威来源**：Linux v6.x（`include/linux/fs.h`、`fs/ramfs/inode.c`、`fs/pipe.c`、
`kernel/module.c`）；POSIX `poll`/`epoll` 语义。

**教学模型简化了什么**：无真实磁盘与缓存（ramfs 路径写死）；无真实 epoll 实例与
watch 表（只有 pipe_poll 就绪位）；无真实模块二进制（只有 2 个哈希名）；综合检查点
用一行断言代替逐项测试。

---

## 8. 思考题与练习

1. **概念理解**：综合验证检查点的 `valid/active/ready/accounted` 四个位分别「代表」哪
   四类子系统？为什么它们要放在同一个模型里断言？
2. **源码定位**：`fd_open_model` 成功后 `inode_table[inode].refs++`，`fd_close_model`
   何时递减 file 的 refs？refs 归零后哪个结构被置 invalid？
3. **动手实验**：把 `module_lookup` 里 `exported_symbols[i].valid` 条件去掉，
   `make run` 后输入 `moduletest`，观察输出变为 `BROKEN`，然后改回（勿提交）。
4. **Linux 对照**：读 `fs/pipe.c` 的 `pipe_poll`，对照本课 `pipe_poll` 的 `POLL_IN`/
   `POLL_OUT` 判定，列出 Linux 版本还考虑了哪些本课省略的条件（如 `POLLHUP`、
   写端引用等）。
5. **设计思考**：本课 `l112test` 是第 5 阶段收官检查点。如果把 4 个子系统各自单独
   验证（像 `fdtest`/`moduletest` 那样）而不再做综合检查点，会损失什么信息？

---

## 9. 本课小结与下一课预告

**小结**：本课以「VFS/设备/epoll/服务综合验证」为主题，是第 5 阶段检查点主线收官站。
源码增量依旧是「更名 + 新模型 + 新命令 + 主题串」：`l111test`→`l104test`、新增
`struct lesson_105_model`/`lesson_105_state`/`l112test`、exec64 增分支、about/横幅更新。
综合验证的机制载体全部继承：VFS 四表与 ramfs（`fdtest`/`pathtest`）、设备/模块模型
（`moduletest`）、poll 就绪语义（`polltest`）、服务管理模型（`initinfo`/`shellrun`/
`jobtest`）。`l112test` 用 `{105U,106U,107U,108U,1,1,1,1}` 把四类机制的整体健康压缩成
一行断言，任何一类被改坏都会让输出变 fallback。

**下一课预告**：[Lesson 113](../lesson-113-stable/README.md) 主题「mutex 与 spinlock
竞争」：主线从 VFS/设备/epoll/服务转入并发。`l113test` 用 `lesson_106_model` 重申
bounded concurrency、SMP、RCU 与 diagnostics 检查点，并侧重讲解继承自 lesson-50 的
`raw_spin_lock_irqsave`/`atomic_exchange_acquire_u32` 等并发原语；`l105test` 由本课
`l112test` 更名收敛。
