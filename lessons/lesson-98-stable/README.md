# Lesson 98: 字符设备注册 — 精讲文档

> **课号**：Lesson 98 ｜ **主题**：字符设备注册（character device registration）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 91 原型的检查点
> **前置课程**：[`../lesson-97-stable/README.md`](../lesson-97-stable/README.md)（目录读取与固定缓冲区）
> **后续课程**：[`../lesson-99-stable/README.md`](../lesson-99-stable/README.md)（设备节点与 major/minor）
> **一句话目标**：讲清「字符设备为什么必须注册、注册什么、注册后怎么被 open 找到」，并把它映射到教学内核的模块注册模型（`module_init_model`/`exported_symbols`）与字符设备式对象（管道 `pipe_model`）上，验证 `l98test` 检查点。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l97test` 恢复为历史命名 `l90test`（挂在 `lesson_90_state` 上）、新增 `lesson_91_model` 状态与 `l98test` 检查点、更新 `about`/开机横幅为本课主题。字符设备注册机制由累积代码承载：内核子系统「注册 + 符号导出查找」由模块模型（`module_init_model`/`module_lookup`）表达，字符设备式对象（FIFO 管道）由 `pipe_model` 表达，设备在 VFS 中的接入点由 ramfs/vfs 层承接。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l98test`（旧 README 写的 `l91test` 按源码勘误）；另保留历史检查点 `l82test`–`l90test`，以及 `moduletest`/`moduleinfo`/`pipetest`/`pipeinfo`/`polltest` 等注册与设备回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：区分字符设备与块设备并说出字符设备注册的动机；复述 Linux `cdev` 注册三步（`alloc_chrdev_region` → `cdev_init` → `cdev_add`）与 `chrdev_open` 的查找路径（对照 `fs/char_dev.c`、`include/linux/cdev.h`）；在教学内核里沿 `module_init_model` → `module_lookup` 走一遍「注册 + 按名哈希查找」链，并解释 `pipe_model` 为什么是「字符设备式对象」；运行 `l98test`/`moduletest`/`pipetest` 验证。

**在课程主线中的位置**：Lesson 89 讲超级块与文件系统注册，Lesson 90–97 依次深入 inode/dentry/路径/偏移/目录读取，本课把「注册」这个动作聚焦到**字符设备**上——设备是 VFS 之下、需要被 open 按设备号找到的一类对象。作为检查点课，源码 diff 极小，任务是把继承机制中与「注册」相关的部分（模块注册、符号导出、管道对象）按主题系统化复述。下一课（Lesson 99）转向设备节点与 major/minor 编号。

**前置知识清单**（学本课前必须掌握）：
1. 文件系统注册模型：`module_init_model` 的 core/vfs 两个模块与 `exported_symbols` 符号表（Lesson 50s/89）。
2. VFS 四层模型与 open 路径：`ramfs_lookup` → `fd_open_model` → `fd_close_model`（Lesson 88–97）。
3. 管道（FIFO）模型：`pipe_model` 的环形缓冲、阻塞读/写、poll 就绪（Lesson 27–30）。
4. 检查点模型 `struct lesson_YY_model` 的四 `u32` 计数 + 四 `u8` 布尔位范式（Lesson 69–97）。
5. `exec64` 的命令分发与 `noargs64`/`eq64` 约定（Lesson 5–7）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 98: 字符设备注册`；
- 新命令 `l98test` 输出 `l98test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `moduletest`/`moduleinfo` 展示「注册 + 符号导出查找」，`pipetest`/`pipeinfo` 展示字符设备式 FIFO 的行为。

---

## 2. 核心概念精讲

### 2.1 字符设备：直觉、定义与为什么必须注册

**直觉**：字符设备（键盘、串口、终端、FIFO）像「水管」——数据是一个字符一个字符流过，没有随机访问、没有块的概念。块设备（硬盘、U 盘）像「书架」——按固定大小的块读写，可以跳到任意位置。区别不在于硬件，而在于**访问模型**：字符设备 `read`/`write` 处理字节流，块设备 `read`/`write` 处理块（Linux 中通常先走 page cache）。

**为什么必须注册**：用户程序对设备做 `open("/dev/ttyS0", O_RDWR)` 时，VFS 拿到的是 `dev_t`（major/minor 编号）。内核必须有一个**以设备号为键的查找表**，把 `dev_t` 映射到该设备驱动的 `file_operations`（read/write/ioctl……）。如果没有注册这一步，内核就无从知道「谁负责这个设备号」。Linux 用 `cdev_map`（一个以 dev_t 为键的 `kobj_map`）实现这张表；把 `struct cdev` 插入 `cdev_map` 的动作就叫**注册（cdev_add）**。

```
驱动模块加载                    用户 open("/dev/xxx")
     │                                  │
     ├─ alloc_chrdev_region() ─┐        │
     │   (得到 dev_t=MAJOR,MINOR)       │
     ├─ cdev_init(&cdev, &fops) │        │
     └─ cdev_add(&cdev, dev, 1)──┐      │
         │ 把 {dev_t → cdev} 插入 cdev_map │
         ▼                        ▼      ▼
    cdev_map[dev_t] = &cdev ──► chrdev_open 找到 fops → 调用 fops->open()
```

### 2.2 教学模型的「注册」：module_init_model 与符号导出

教学内核没有 `struct cdev`，但「**注册 = 建条目 + 置初始化标志；查找 = 按键匹配**」这一机制完整存在于**模块模型**中：

- `module_init_model()` 注册两个内核子系统：`modules[0]` 为 `core`（`name_hash=0x636f7265`），`modules[1]` 为 `vfs`（`name_hash=0x766673`），均置 `loaded=1, initialized=1`——对应 Linux 的 `module_init`/子系统注册。
- `exported_symbols[]` 导出 `pmm`（`0x706d6d`）与 `vfs` 符号，`module_lookup(name_hash)` 按哈希精确查找——对应 `cdev_map` 按 dev_t 查找的**同一查找语义**。
- 差异在键的类型：Linux 用 `dev_t`（major/minor 编码）当键，教学模型用 name_hash（子系统名哈希）当键；下一课（Lesson 99）会引入 major/minor 视角。

### 2.3 字符设备式对象：pipe_model

Linux 中 FIFO（named pipe）就是一个字符设备（`fs/pipe.c` 的 `read_pipe`/`write_pipe`，通过 `anon_inode`/`fifo` 文件类型接入 VFS）。教学内核的 `pipe_model` 忠实复刻了字符设备的三个特征：

1. **字节流 + 环形缓冲**：`data[PIPE_CAP]`、`head/tail/used`，读/写各推进游标；
2. **阻塞语义**：空读/满写时 `blocked_readers++`/`blocked_writers++`，有数据/有空间时 `waitq_wake_one` 唤醒（字符设备的 blocking I/O）；
3. **poll 就绪**：`pipe_poll` 按 `POLL_IN`/`POLL_OUT` 报告可读/可写——正是终端/串口这类字符设备的就绪模型。

### 2.4 检查点模型：lesson_91_model 与 l98test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `91→94` 标记 Origin 为 Lesson 91。本课同时把上一课新增的 `l97test` 恢复为历史命名 `l90test`（同一 `lesson_90_state`，计数 `90→93`）——与 Lesson 97 恢复 `l89test` 完全同构，体现检查点课的命名整理规律：**`lXXtest` 命令名始终向其 Origin 课号收敛**。

### 2.5 机制继承 + 检查点增量

本课主题机制（设备注册、字符设备对象）不是本课新写的代码：模块注册来自早期子系统阶段，管道来自进程/IPC 阶段，VFS 来自 Lesson 88–96。本课的实际增量只有三处：`l97test`→`l90test` 更名、`lesson_91_model`+`l98test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「注册」主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l97test`→`l90test` 恢复命名；新增 `lesson_91_model`/`lesson_91_state`/`l98test`；`about` 与开机横幅更新。设备注册/字符设备机制由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`字符设备注册`/`l98test`/`Lesson 98`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（设备注册机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_91_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_91_model lesson_91_state;
static TEXT64 void l98test(u16*c){lesson_91_state=(struct lesson_91_model){91U,92U,93U,94U,1,1,1,1};int ok=lesson_91_state.valid&&lesson_91_state.active&&lesson_91_state.ready&&lesson_91_state.accounted&&lesson_91_state.b==lesson_91_state.a+1U;text64(c,"l98test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 91 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `91→94`（Origin Lesson 91），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l98test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 91 fallback reported`。
3. **恢复的 `l90test`**：本课同时把 `l97test` 更名回 `l90test`（同为 `lesson_90_state`），使检查点命令名与其 Origin 对齐；`l82test`–`l89test` 历史检查点全部保留。

#### 3.2.2 设备注册的承载：模块模型数据结构

```c
#define MODULE_MAX 3U
#define SYMBOL_MAX 4U
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
struct symbol_model { u64 name_hash,owner; u8 exported,valid; };
static struct module_model modules[MODULE_MAX];
static struct symbol_model exported_symbols[SYMBOL_MAX];
static u64 module_inits,module_exports,module_lookups;
```
1. **注册表与符号表**：`modules[]` 是被注册的内核子系统（最多 3 个），`exported_symbols[]` 是它们导出的符号（最多 4 个）——对应 Linux 的 module 链表与 `EXPORT_SYMBOL` 表。
2. **模块字段**：`name_hash`（模块名哈希，作为注册键）、`init_calls`/`exit_calls`（初始化/退出调用计数）、`loaded`/`initialized`（注册状态机：已加载、已初始化）。
3. **符号字段**：`name_hash`（符号名）、`owner`（属于哪个模块）、`exported`/`valid`（可导出且有效）。
4. **固定容量**：`MODULE_MAX=3`、`SYMBOL_MAX=4` 是「注册表也定长」的体现——与 Lesson 97 的固定缓冲区主题一致。

#### 3.2.3 注册动作：module_init_model

```c
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
```
1. **清表再注册**：先整体清零 `modules[]` 与 `exported_symbols[]`，保证注册的幂等起点。
2. **两个子系统**：`modules[0]=core`（`0x636f7265`，`init_calls=1`、`loaded=1`、`initialized=1`）、`modules[1]=vfs`（`0x766673`，同上）——`init_calls=1` 模拟 `module_init()` 被调用一次。
3. **导出符号**：`exported_symbols[0]=pmm`（`0x706d6d`，owner=0）、`exported_symbols[1]=vfs`（owner=1），`module_exports=2`。
4. **与 Linux 对照**：对应 `register_chrdev_region`（登记一段 dev_t）+ 驱动的 `cdev_add`；教学模型把「注册表条目 = 名字哈希 + 状态位」简化表达。

#### 3.2.4 按键查找：module_lookup（chrdev_open 的教学版）

```c
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
```
1. **遍历匹配**：对 `exported_symbols[]` 逐项检查 `valid && exported && name_hash==name`——与 `cdev_map` 按 dev_t 哈希定位到 cdev 的语义一致，只是教学模型退化为线性扫描。
2. **可观察统计**：每次调用 `module_lookups++`，供 `moduleinfo` 展示查找次数。
3. **正确性护栏**：必须 `valid && exported` 才命中，防止「未注册/未导出的符号被误用」——对应 Linux 中 `chrdev_open` 对未注册 dev_t 返回 `-ENODEV`。

#### 3.2.5 注册表观察与验证：moduleinfo / moduletest

```c
static TEXT64 void moduleinfo(u16*c){u32 i;text64(c,"modules initialized/exports/lookups: ");hex64(c,module_inits);text64(c,"/");hex64(c,module_exports);text64(c,"/");hex64(c,module_lookups);putc64(c,'\n');for(i=0;i<MODULE_MAX;i++)if(modules[i].loaded){text64(c,"module ");hex64(c,i);text64(c," initialized ");hex64(c,modules[i].initialized);putc64(c,'\n');}}
static TEXT64 void moduletest(u16*c){int a=module_lookup(0x706d6d),b=module_lookup(0x6d697373),d=modules[0].initialized&&modules[1].initialized;text64(c,"moduletest: ");text64(c,a&&!b&&d?"module init order and exported-symbol lookup passed":"BROKEN");putc64(c,'\n');}
```
1. **moduleinfo**：先打印 `modules initialized/exports/lookups` 三个计数，再遍历 `modules[]` 打印每个已加载模块的初始化状态——等效于查看「注册表」当前内容。
2. **moduletest 断言**：`module_lookup(0x706d6d)`（pmm）必须命中、`module_lookup(0x6d697373)`（"miss"，未导出）必须未命中、两个模块必须都已初始化。
3. **成功串**：`moduletest: module init order and exported-symbol lookup passed`。

#### 3.2.6 字符设备式对象：pipe_model 与阻塞语义

```c
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
```
```c
static TEXT64 int pipe_try_write(u8 value){u8 id;if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}pipe_model.data[pipe_model.head]=value;pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);pipe_model.used++;pipe_model.writes++;if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;return 1;}
static TEXT64 int pipe_try_read(u8*out){u8 id;if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}*out=pipe_model.data[pipe_model.tail];pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);pipe_model.used--;pipe_model.reads++;if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;return 1;}
```
1. **满写**：`used>=PIPE_CAP` 时 `blocked_writers++` 并拒绝——字符设备的 full 阻塞边界（Linux `write_pipe` 的 `pipe_full` 检查）。
2. **空读**：`!used` 时 `blocked_readers++` 并拒绝——字符设备的 empty 阻塞边界（`read_pipe` 的 `pipe_empty`）。
3. **环形推进与唤醒**：写入推进 `head`、读取推进 `tail`（均取模 `PIPE_CAP`），成功一侧唤醒对侧等待者——`waitq_wake_one` 使阻塞读/写变成「可观察的字符设备流」。

#### 3.2.7 字符设备式就绪模型：pipe_poll

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```
1. **按掩码报告**：`POLL_IN`（可读）当且仅当 `used>0`，`POLL_OUT`（可写）当且仅当 `used<PIPE_CAP`。
2. **与终端设备对照**：Linux 的 `tty_poll`/`n_tty` 用同样思路报告可读/可写——字符设备就绪模型的直接教学版。
3. **统计**：`poll_registrations++` 让 poll 调用次数可观察（`pipeinfo` 打印）。

#### 3.2.8 设备在 VFS 中的接入：ramfs_lookup → fd_open_model

设备的注册最终服务于「按路径/按号打开」。教学内核中 `shell_exec_path` 展示了打开链：
```c
static TEXT64 int shell_exec_path(const char *path,u32 argc,u32 envc){int inode=ramfs_lookup(path);int fd;u64 image_hash=0x5348454c4c494d47ULL;if(inode<0||argc>4U||envc>4U)return 0;fd=fd_open_model((u32)inode,0);if(fd<0)return 0;...fd_close_model((u32)fd);shell_runtime.exits++;return 1;}
```
1. **路径 → inode → fd**：`ramfs_lookup`（目录读取，Lesson 97 主题）→ `fd_open_model`（引用链，Lesson 90/96 主题）——字符设备在真实内核里走的是同一条 `open()` 通道，区别只在 `chrdev_open` 按 `dev_t` 查 `cdev_map` 得到 fops。
2. **本课视角**：教学模型把「设备驱动」合并进 VFS 对象，省略了 `cdev` 层；模块模型补充了「注册/查找」语义，两者合起来覆盖了字符设备注册的完整叙事。

#### 3.2.9 exec64 增量与开机横幅

- `about` 输出 `Lesson 98: 字符设备注册\n`；检查点分支：
```c
else if(eq64(word,"l90test")){if(!noargs64(arg))usage64(c,"l90test");else l90test(c);}else if(eq64(word,"l98test")){if(!noargs64(arg))usage64(c,"l98test");else l98test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 98: 字符设备注册\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `字符设备注册`、`Lesson 98`，kernel64.c 含 `l98test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model()（注册 core/vfs 子系统 + 导出 pmm/vfs 符号）
 ├─ init_model_start() → pmm_init → vma_init → reclaim_init → vfs_init → ramfs_init
 ├─ 横幅 "Lesson 98: 字符设备注册"
 └─ 主循环：命令 → exec64
     ├─ l98test / l90test → 阶段检查点（lesson_91_state / lesson_90_state）
     ├─ moduletest / moduleinfo → 注册表「查找/列出」
     ├─ pipetest / pipeinfo → 字符设备式 FIFO 阻塞读/写
     ├─ polltest → POLLIN/POLLOUT 就绪转移
     └─ pathtest / shellrun → VFS 打开链回归
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`module_init_model()` 注册 core/vfs，`vfs_init()` 建 VFS 与 ramfs，打印横幅 `Lesson 98: 字符设备注册`。
2. **`l98test`** → `l98test(c)` → 断言 → `l98test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`moduletest`** → 两次 `module_lookup`（pmm 命中、"miss" 未命中）+ 初始化断言 → `moduletest: module init order and exported-symbol lookup passed`。
4. **`moduleinfo`** → 打印 `modules initialized/exports/lookups: 2/2/N` 与各模块 `initialized` 状态。
5. **`pipetest`** → 空读失败 → 写 `0x41` → 读回验证 → 满写失败 → `pipetest: bounded FIFO empty/full blocking transitions passed`。
6. **`polltest`** → 空管道 `POLL_OUT` 就绪、写满后 `POLL_IN`/`POLL_OUT` 转移 → `polltest: POLLIN/POLLOUT readiness transitions passed`。
7. **`l90test`**（历史检查点） → `l90test: bounded VFS, devices, epoll, and service management checkpoint passed`。
8. **`about`** → `Lesson 98: 字符设备注册`。

---

## 5. 构建、运行与验证

**依赖**：同前几课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 98 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 98: 字符设备注册` 横幅 |
| `l98test` | `l98test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l90test` | `l90test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `moduletest` | `moduletest: module init order and exported-symbol lookup passed` |
| `pipetest` | `pipetest: bounded FIFO empty/full blocking transitions passed` |
| `polltest` | `polltest: POLLIN/POLLOUT readiness transitions passed` |
| `about` | `Lesson 98: 字符设备注册` |
| `moduleinfo` | `modules initialized/exports/lookups: 2/2/N` 与每模块 `initialized` 行 |

判定成功：`l98test`/`moduletest`/`pipetest`/`polltest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l98test` 输出 `Lesson 91 fallback reported` | `lesson_91_state` 初始化/断言失败（stale 镜像） | `grep -n "l98test" kernel64.c`；确认初始化串 `{91U,92U,93U,94U,1,1,1,1}` 与 `b==a+1U` |
| `moduletest` 输出 `BROKEN` | `module_lookup` 命中/未命中断言异常或模块未初始化 | 对照 `module_init_model` 的 `0x706d6d`（pmm）与 `0x6d697373`（miss）哈希；先跑 `moduleinfo` 看注册表 |
| `moduleinfo` 的 lookups 计数异常 | `module_lookup` 调用次数累计 | 每次 `moduletest` 应 +2；重启内核清零 |
| `pipetest` 输出 `BROKEN` | `pipe_model.used` 状态或环形游标异常 | 对照 `pipe_try_read`/`pipe_try_write` 的 `head/tail/used` 推进；`pipeinfo` 看 reads/writes 计数 |
| `polltest` 输出 `BROKEN` | `pipe_poll` 的 `POLL_IN`/`POLL_OUT` 判定与 `used` 不符 | 对照 `pipe_poll` 的两条 `if`；写满（`used=PIPE_CAP`）后 `POLL_OUT` 必须为 0 |
| `l98test` 之后运行 `l90test` 输出 fallback | 两者共享 `lesson_90_state` 但初始化顺序错乱 | 检查 exec64 分支是否各自调用独立函数；`l90test` 与 `l98test` 使用不同 state 结构 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 98' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `字符设备注册` 与 `Lesson 98` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `module_init_model` 注册 core/vfs 模块（loaded/initialized） | `kernel/module/main.c`：`load_module()`/`do_init_module()`；`include/linux/module.h` 的 `struct module` | 模型 3 槽定长，无符号重定位、无依赖解析、无模块卸载 |
| `exported_symbols[]` 与 `module_lookup(name_hash)` | `fs/char_dev.c`：`cdev_add()` 把 `{dev_t→cdev}` 插入 `cdev_map`（`kobj_map`）；`include/linux/cdev.h` 的 `struct cdev` | 模型用 name_hash 线性扫描代替 dev_t 哈希表，无 kobj/refcount |
| 注册三步（建条目、置 initialized、可查找） | `fs/char_dev.c`：`alloc_chrdev_region()`/`register_chrdev_region()` → `cdev_init()` → `cdev_add()` | 模型把三步合并为一次结构体赋值，无 dev_t 分配冲突检测 |
| 按键查找失败返回未命中 | `fs/char_dev.c`：`chrdev_open()` 对未注册 dev_t 返回 `-ENODEV` | 模型返回 0/1，无错误码分级 |
| `pipe_model` 阻塞读/写 + 唤醒 | `fs/pipe.c`：`pipe_read()`/`pipe_write()` 与 `pipe_full`/`pipe_empty`、等待队列 | 模型单管道 `PIPE_CAP=4`，无多个 reader/writer 的锁与 waiter 链表 |
| `pipe_poll` 的 POLLIN/POLLOUT | `fs/pipe.c`：`pipe_poll()`；终端设备 `drivers/tty/` 的 `tty_poll()` | 模型无 `poll_table` 注册与事件回填 |
| ramfs 路径打开链承载设备 | `fs/open.c`：`filp_open()` → `chrdev_open()`（设备文件） | 模型设备对象直接走普通 inode，无 `S_IFCHR` 特判 |
| `l98test` 断言 | 无直接对应（LTP 设备文件测试套件） | 模型把注册主题的可验证状态固化进内核 |

**权威来源**：Linux `fs/char_dev.c`、`include/linux/cdev.h`、`fs/pipe.c`、`kernel/module/main.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么设备驱动的 fops 必须「注册」到内核而不是直接放在全局变量里？`cdev_map` 以什么为键、解决什么查找问题？
2. **源码定位**：在 `kernel64.c` 中找出 `module_init_model` 的所有调用点（提示：开机 `kernel_main64_binary` 第一行），并说明它在 `vfs_init` 之前执行的顺序意义。
3. **动手实验**：在 `module_init_model` 中新增第三个模块（例如 `dev`，`name_hash=0x646576`、`init_calls=1`、`loaded=1`、`initialized=1`），并在 `exported_symbols` 里导出 `char`（`0x63686172`），修改 `moduletest` 断言后重新构建验证。
4. **动手实验**：把 `pipe_try_write` 的满写判定 `used>=PIPE_CAP` 改成 `used>PIPE_CAP`（允许越界一字节），运行 `pipetest`，观察环形缓冲 `head/tail` 是否破坏 `used` 不变量。
5. **Linux 对照**：阅读 `fs/char_dev.c` 的 `cdev_add` 与 `chrdev_open`，指出真实内核在 open 设备文件时如何从 `inode->i_rdev` 得到 dev_t 并在 `cdev_map` 中定位驱动；对比教学模型「路径 → inode → fd」省略了哪一步。

---

## 9. 本课小结与下一课预告

1. 本课把「注册」聚焦到字符设备：字符设备按字节流访问，块设备按块访问，两者差异决定其驱动接口。
2. Linux 字符设备注册三步（`alloc_chrdev_region` → `cdev_init` → `cdev_add`）最终把 `{dev_t → cdev}` 插入 `cdev_map`，open 时 `chrdev_open` 据此找到 fops。
3. 教学内核用模块模型承载「注册 + 按键查找」：`module_init_model` 注册 core/vfs，`module_lookup` 按 name_hash 匹配，`moduleinfo`/`moduletest` 提供观察与验证。
4. `pipe_model` 是字符设备式对象：环形字节流、阻塞读/写、`pipe_poll` 就绪模型，与 `fs/pipe.c`/`tty` 语义一致。
5. 检查点增量：新增 `l98test`（Origin Lesson 91），恢复历史命名 `l90test`，横幅与 `about` 更新。
6. 下一课（Lesson 99）主题转向**设备节点与 major/minor**（对照 `include/linux/major.h` 与 dev_t 编码），把「注册的键」从 name_hash 换成设备号。
