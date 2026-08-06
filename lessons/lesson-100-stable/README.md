# Lesson 100: 设备打开与 ioctl 元数据 — 精讲文档

> **课号**：Lesson 100 ｜ **主题**：设备打开与 ioctl 元数据（device open and ioctl metadata）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 93 原型的检查点
> **前置课程**：[`../lesson-99-stable/README.md`](../lesson-99-stable/README.md)（设备节点与 major/minor）
> **后续课程**：[`../lesson-101-stable/README.md`](../lesson-101-stable/README.md)（块设备请求队列）
> **一句话目标**：讲清设备/文件「打开」在打开瞬间建立了哪些元数据（f_mode/f_flags/f_pos），以及 ioctl 作为「控制面」与读写「数据面」分离的接口模型，并把教学内核的 `fd_open_model(flags)`、`file_model.offset`、`fdinfo`/`fdtest` 与定时器/时钟这类「ioctl 式控制对象」映射到 Linux `fs/open.c` 与 `fs/ioctl.c` 上，验证 `l100test` 检查点。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l99test` 恢复为历史命名 `l92test`（挂在 `lesson_92_state` 上）、新增 `lesson_93_model` 状态与 `l100test` 检查点、更新 `about`/开机横幅为本课主题。设备打开与 ioctl 元数据机制由累积代码承载：打开元数据由 `fd_open_model`（flags 参数、file/offset）与 `fdinfo`/`fdtest` 表达，ioctl 式控制对象由定时器/时钟模型（`timer_arm`/`timer_read`/`timer_cancel`）与 syscall 元数据表表达。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l100test`（旧 README 写的 `l93test` 按源码勘误）；另保留历史检查点 `l82test`–`l92test`，以及 `fdtest`/`fdinfo`/`timertest`/`clocktest`/`syscallinfo` 等打开与控制面回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出 `open()` 返回 fd 之前内核做了什么（路径解析 → 分配 fd/file → 初始化 `f_mode`/`f_flags`/`f_pos`，对照 `fs/open.c`）；区分 ioctl 的「控制面」与 read/write 的「数据面」，说出 `do_vfs_ioctl` 会先拦截哪些通用命令（对照 `fs/ioctl.c`）；在教学内核中沿 `fd_open_model(inode, flags)` → `fdinfo` 观察打开元数据，并沿 `timer_arm`/`timer_read`/`timer_cancel` 走一遍「ioctl 式」控制链；运行 `l100test`/`fdtest`/`timertest` 验证。

**在课程主线中的位置**：Lesson 98–99 讲设备注册与设备节点，本课把视角落到「设备被打开之后」——打开即建立元数据，之后读/写/控制都围绕这份元数据展开。作为检查点课，源码 diff 极小，任务是把继承机制中与「打开元数据 + 控制接口」相关的部分（fd/file 表、syscall 表、定时器/时钟控制对象）按主题系统化复述。下一课（Lesson 101）转向块设备请求队列。

**前置知识清单**（学本课前必须掌握）：
1. 打开链：`ramfs_lookup` → `fd_open_model(inode, flags)` → `fd_close_model` 与 fd/file/inode 三层表（Lesson 88–97）。
2. fd 元数据字段：`struct file_model{inode_index,offset,flags,refs,valid}`、`struct fd_model{file_index,valid}`（Lesson 88–96）。
3. 定时器/时钟模型：`timer_arm`/`timer_read`/`timer_cancel`/`clock_update`（Lesson 40s）。
4. syscall 元数据表：`syscallinfo` 的 `0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS`（Lesson 30s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位（Lesson 69–99）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 100: 设备打开与 ioctl 元数据`；
- 新命令 `l100test` 输出 `l100test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `fdtest`/`fdinfo` 展示打开元数据，`timertest`/`clocktest`/`syscallinfo` 展示 ioctl 式控制对象与系统调用元数据。

---

## 2. 核心概念精讲

### 2.1 设备打开：open 的三件事

**直觉**：`open("/dev/xxx", O_RDWR)` 返回一个整数 fd，看起来轻飘飘，内核在背后做了三件事：

1. **定位**：解析路径、走到目标 inode（普通文件或设备节点，Lesson 97/99）；
2. **分配**：从当前进程的 fd 表分配一个空描述符，把它和新建的 `struct file` 绑定；
3. **初始化元数据**：把打开方式写进 file 对象——`f_mode`（读/写/读写）、`f_flags`（`O_NONBLOCK`/`O_APPEND`……）、`f_pos`（从 0 开始）、`f_op`（从 inode 查到 file_operations）。

教学内核的 `fd_open_model(u32 inode,u64 flags)` 正是这三件事的浓缩：校验 inode → 找空 fd/file 槽 → 写入 `{inode, offset=0, flags, refs=1, valid=1}` 并 `inode_table[inode].refs++`。其中 `flags` 参数（`fdtest` 里传 1、2）就是打开方式元数据。

### 2.2 ioctl：控制面与数据面分离

**直觉**：设备不光要传数据，还要「调设置」——改波特率、清缓冲、读状态。把这些塞进 read/write 语义会很别扭（数据和控制混在一起）。于是 Unix 设计了 **ioctl（input/output control）**：同一个 fd，read/write 走**数据面**，ioctl 走**控制面**，控制命令由 `cmd`（一个整数码）标识，第三个参数是命令专用参数（指针或值）。

**Linux 流程**（`fs/ioctl.c`）：`sys_ioctl` → `do_vfs_ioctl`：先拦截**通用命令**（`FIOCLEX`/`FIONCLEX`/`FIONBIO`/`FIOASYNC` 等与文件本身相关的命令），剩下的下发给驱动 `f_op->unlocked_ioctl(file, cmd, arg)`。教学内核没有 `unlocked_ioctl`，但「**控制面与数据面分离、控制对象有元数据**」这一模型完整存在于**定时器对象**上：

```
timer_arm(delay, interval)   → 控制命令 SET（设置 deadline/periodic）
timer_read()                 → 控制命令 GET（读取到期次数，并清零）
timer_cancel()               → 控制命令 RESET（取消，置 canceled）
timer_poll()                 → 状态机推进（tick 到达则置 readable）
```

### 2.3 教学模型的打开元数据

- `fd_open_model(inode, flags)` 的 `flags` → Linux 的 `f_flags`（打开方式）；
- `file_model.offset` → Linux 的 `f_pos`（读写位置，初始 0）；
- `file_model.inode_index` → Linux 的 `f_mapping`/`f_inode`（指向 inode）；
- `fdinfo` 打印 `fd F file F inode I off O` → `ls -l`/`/proc/self/fd` 式的打开表观察；
- `fdtest` 断言 `fd_opens==2 && fd_closes==2` → 打开/关闭计数回归。

### 2.4 教学模型的 ioctl 式元数据

- **定时器对象**（`struct timer_model{deadline_tick,interval_ticks,expirations,arms,reads; armed,readable,periodic,canceled}`）——一个带状态机与统计的「控制对象」，`timerinfo` 打印全部元数据；
- **syscall 元数据表**：`syscallinfo` 输出 `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS`——系统调用面即内核的「ioctl 面」，`-ENOSYS` 是未知命令的返回值（对照 Linux `fs/ioctl.c` 对未知 cmd 返回 `-ENOTTY`）。

### 2.5 检查点模型：lesson_93_model 与 l100test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `93→96` 标记 Origin 为 Lesson 93。本课同时把上一课新增的 `l99test` 恢复为历史命名 `l92test`（同一 `lesson_92_state`，计数 `92→95`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（打开元数据、ioctl 控制面）不是本课新写的代码：fd/file 表与 `fd_open_model` 来自 VFS 阶段，定时器/时钟与 syscall 表来自更早阶段。本课的实际增量只有三处：`l99test`→`l92test` 更名、`lesson_93_model`+`l100test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「打开与控制」主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l99test`→`l92test` 恢复命名；新增 `lesson_93_model`/`lesson_93_state`/`l100test`；`about` 与开机横幅更新。打开元数据/ioctl 控制面由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`设备打开与 ioctl 元数据`/`l100test`/`Lesson 100`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（打开/ioctl 机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_93_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_93_model lesson_93_state;
static TEXT64 void l100test(u16*c){lesson_93_state=(struct lesson_93_model){93U,94U,95U,96U,1,1,1,1};int ok=lesson_93_state.valid&&lesson_93_state.active&&lesson_93_state.ready&&lesson_93_state.accounted&&lesson_93_state.b==lesson_93_state.a+1U;text64(c,"l100test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 93 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `93→96`（Origin Lesson 93），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l100test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 93 fallback reported`。
3. **恢复的 `l92test`**：本课同时把 `l99test` 更名回 `l92test`（同为 `lesson_92_state`），使检查点命令名与 Origin 对齐；`l82test`–`l91test` 历史检查点全部保留。

#### 3.2.2 打开元数据的数据结构：file_model / fd_model

```c
#define FD_MAX 4U
#define FILE_MAX 3U
#define INODE_MAX 3U
#define DENTRY_MAX 3U
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
static u64 fd_opens,fd_closes,fd_reads,fd_seek_ops;
```
1. **file_model 即打开元数据**：`inode_index`（指向 inode，Linux `f_inode`）、`offset`（读写位置，Linux `f_pos`）、`flags`（打开方式，Linux `f_flags`）、`refs`（引用计数）、`valid`。
2. **fd_model 即描述符**：`file_index` 指向 file，`valid` 标记占用——Linux 的 fd 表项（`struct fdtable`）简化版。
3. **统计四件套**：`fd_opens/fd_closes/fd_reads/fd_seek_ops` 是「打开生命周期」的元数据，`fdinfo` 打印、`fdtest` 断言。

#### 3.2.3 打开：fd_open_model（open 三件事的浓缩）

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. **前置校验**：`inode>=INODE_MAX || !inode_table[inode].valid` 拒绝打开不存在/未初始化的 inode——Linux `path_openat` 对路径解析失败的等价前置检查。
2. **双槽分配**：先找空 fd 槽再找空 file 槽，两个都空才成功——Linux `alloc_fd` + `file` 分配的浓缩。
3. **写入打开元数据**：`{inode, offset=0, flags, refs=1, valid=1}`——`offset=0`（f_pos 初始 0）、`flags`（f_flags 即打开方式，`fdtest` 用 1/2 模拟 O_RDONLY/O_WRONLY）、`refs=1`（打开即持有一个引用）。
4. **级联引用**：`inode_table[inode].refs++` 与 `fd_opens++`——打开使 inode 引用 +1（Lesson 90/96 生命周期主题）。

#### 3.2.4 打开元数据的读取与推进：fd_read_model

```c
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
```
1. **三层校验**：fd→file→inode 逐层查 valid——数据面操作必须基于合法的打开元数据。
2. **基于 offset 的读取**：`remaining=size-offset`，`if(bytes>remaining)bytes=remaining` 把请求量夹到剩余范围内——`f_pos` 边界检查（对照 `vfs_read` 的 `i_size_read`）。
3. **推进 f_pos**：`file_table[f].offset+=bytes`——每次 read 推进打开元数据中的读写位置。

#### 3.2.5 打开元数据观察与验证：fdinfo / fdtest

```c
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");hex64(c,file_table[f].offset);putc64(c,'\n');}text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
static TEXT64 void fdtest(u16*c){int a=fd_open_model(0,1),b=fd_open_model(1,2),r1=a>=0&&b>=0&&fd_read_model((u32)a,8)&&fd_close_model((u32)b)&&fd_close_model((u32)a);text64(c,"fdtest: ");text64(c,r1&&fd_opens==2&&fd_closes==2?"fd/file/inode/dentry refs and offsets passed":"BROKEN");putc64(c,'\n');}
```
1. **fdinfo 逐项打印**：对每个有效 fd 打印 `fd F file F inode I off O`——`off` 即打开元数据的 `f_pos`，`inode` 即实例编号；底部打印 opens/closes/reads/seeks 计数。
2. **fdtest 的 flags 语义**：`fd_open_model(0,1)`（flags=1，读方式）与 `fd_open_model(1,2)`（flags=2，写方式）——打开方式元数据作为参数传递。
3. **成功串**：`fdtest: fd/file/inode/dentry refs and offsets passed`。

#### 3.2.6 ioctl 式控制对象：定时器模型

```c
struct timer_model { u64 deadline_tick,interval_ticks,expirations,arms,reads; u8 armed,readable,periodic,canceled; };
```
```c
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
static TEXT64 u64 timer_read(void){u64 n=timer_model.readable?timer_model.expirations:0;timer_model.expirations=0;timer_model.readable=0;timer_model.reads++;return n;}
static TEXT64 void timer_cancel(void){timer_model.armed=0;timer_model.canceled=1;timer_model.readable=0;timer_model.expirations=0;}
```
1. **timer_arm = ioctl SET**：`deadline_tick=ticks+delay`、`interval_ticks=interval`、`periodic=interval!=0`、`armed=1`——一次「设置控制参数」的命令，对应设备 ioctl 的设置类命令。
2. **timer_read = ioctl GET**：读取 `expirations`（到期次数）并清零、置 `readable=0`——「读状态 + 清状态」的一次性 GET 语义（对应 `tcgetattr`/`read`-status 类命令）。
3. **timer_cancel = ioctl RESET**：`armed=0`、`canceled=1`、`expirations=0`——撤销控制参数的 RESET 命令。
4. **状态机推进**：`timer_poll` 在 tick 到达时置 `readable=1`、`expirations++`，周期定时器自动重设 `deadline_tick`——这是驱动内部状态机，ioctl 只是它的对外控制面。

#### 3.2.7 系统调用面即内核的 ioctl 面：syscallinfo

```c
text64(c,"syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS\nWRITE_CONSOLE uses a fixed kernel-owned message and no user pointer\nEXIT reports and intentionally halts; no user IRQ callback or cross-address-space scheduler\n");
```
1. **命令号表**：`0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT`——内核对外暴露的「命令面」，与 ioctl 的 cmd 号表同构；未知命令返回 `-ENOSYS`（Linux 未知 ioctl 返回 `-ENOTTY`）。
2. **元数据说明**：`WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer`——每条命令的约束被固化进元数据说明。
3. **教学意义**：用户程序与内核的每一次交互都是「带命令号的请求」，ioctl 只是其中专门给设备的那一类——`syscallinfo` 把整个命令面作为元数据公开。

#### 3.2.8 exec64 增量与开机横幅

- `about` 输出 `Lesson 100: 设备打开与 ioctl 元数据\n`；检查点分支：
```c
else if(eq64(word,"l92test")){if(!noargs64(arg))usage64(c,"l92test");else l92test(c);}else if(eq64(word,"l100test")){if(!noargs64(arg))usage64(c,"l100test");else l100test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 100: 设备打开与 ioctl 元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `设备打开与 ioctl 元数据`、`Lesson 100`，kernel64.c 含 `l100test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ vfs_init() → fd/file/inode 表清空，ramfs_init
 ├─ 横幅 "Lesson 100: 设备打开与 ioctl 元数据"
 └─ 主循环：命令 → exec64
     ├─ l100test / l92test → 阶段检查点（lesson_93_state / lesson_92_state）
     ├─ fdtest / fdinfo → 打开元数据（flags/offset/引用链）验证与观察
     ├─ timertest / timerinfo → ioctl 式控制对象（arm/read/cancel）
     ├─ clocktest / clockinfo → 时钟元数据
     └─ syscallinfo → 系统调用命令面元数据
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 清空 fd/file/inode 表并建 ramfs，打印横幅 `Lesson 100: 设备打开与 ioctl 元数据`。
2. **`l100test`** → `l100test(c)` → 断言 → `l100test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`fdtest`** → `fd_open_model(0,1)`、`fd_open_model(1,2)` → `fd_read_model(a,8)` → 两次 `fd_close_model` → `fdtest: fd/file/inode/dentry refs and offsets passed`。
4. **`fdinfo`** → 打印每个有效 fd 的 `fd F file F inode I off O` 与 `opens/closes/reads/seeks` 计数——打开元数据一览。
5. **`timertest`** → `timer_arm(2,0)` → `timer_poll` → `timer_read` → 周期定时器 → `timer_cancel` → `timertest: deadline, expiration, periodic, and cancel passed`。
6. **`syscallinfo`** → `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS` 及其说明行。
7. **`l92test`**（历史检查点） → `l92test: bounded VFS, devices, epoll, and service management checkpoint passed`。
8. **`about`** → `Lesson 100: 设备打开与 ioctl 元数据`。

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
Multiboot2 and Lesson 100 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 100: 设备打开与 ioctl 元数据` 横幅 |
| `l100test` | `l100test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l92test` | `l92test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `timertest` | `timertest: deadline, expiration, periodic, and cancel passed` |
| `about` | `Lesson 100: 设备打开与 ioctl 元数据` |
| `syscallinfo` | `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS` 及说明行 |

判定成功：`l100test`/`fdtest`/`timertest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l100test` 输出 `Lesson 93 fallback reported` | `lesson_93_state` 初始化/断言失败（stale 镜像） | `grep -n "l100test" kernel64.c`；确认初始化串 `{93U,94U,95U,96U,1,1,1,1}` 与 `b==a+1U` |
| `fdtest` 输出 `BROKEN` | `fd_opens==2 && fd_closes==2` 断言异常或 `fd_read_model` 失败 | 对照 `fd_open_model`/`fd_close_model`/`fd_read_model`；先跑 `fdinfo` 看计数 |
| `fdinfo` 的 `off` 不是 0 | 读操作推进了 `file_table[f].offset` | 先跑 `fdtest` 再跑 `fdinfo`，读过的 fd 的 `off` 应为 8（`fd_read_model(a,8)`） |
| `timertest` 输出 `BROKEN` | `timer_arm`/`timer_poll`/`timer_read`/`timer_cancel` 状态机异常 | 对照 `timer_poll` 的 `tick_due` 与 `deadline_tick`；`timerinfo` 看 armed/readable/expirations |
| `syscallinfo` 无输出或截断 | 命令执行被超长输入干扰 | 直接输入 `syscallinfo`，检查 `word[24]` 令牌缓冲；对照输出串逐字比对 |
| `l100test` 与 `l92test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l100test` 只操作 `lesson_93_state`、`l92test` 只操作 `lesson_92_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 100' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `设备打开与 ioctl 元数据` 与 `Lesson 100` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `fd_open_model(inode,flags)` 建立 file 元数据 | `fs/open.c`：`do_sys_open()`/`path_openat()`/`do_dentry_open()`（初始化 `f_mode`/`f_flags`/`f_pos`）；`fs/file.c`：`alloc_fd()` | 模型无安全位（`FMODE_*`）与挂载/路径状态机，flags 仅当元数据存 |
| `file_model{inode_index,offset,flags,refs}` | `include/linux/fs.h` 的 `struct file`（`f_inode`/`f_pos`/`f_flags`/`f_count`） | 模型无 `f_op`/`f_mapping`/`f_path`/`f_cred`，无锁 |
| `fdinfo` 打印打开表 | `fs/proc/fd.c`（`/proc/self/fd`）；`fs/fcntl.c` `F_GETFD`/`F_GETFL` | 模型直接遍历定长 `fd_table`，无 proc 抽象 |
| `fd_read_model` 推进 `offset` 并夹到剩余 | `fs/read_write.c`：`vfs_read()`/`new_sync_read()` 与 `i_size_read()` 边界；`lseek` | 模型无 copy_to_user 与读写锁 |
| `timer_arm/read/cancel`（SET/GET/RESET） | `fs/ioctl.c`：`do_vfs_ioctl()` → `f_op->unlocked_ioctl(file, cmd, arg)`；`kernel/time/itimer.c` | 模型无 cmd 编号编码与 arg 指针，命令就是三个函数 |
| `do_vfs_ioctl` 拦截通用命令 | `fs/ioctl.c`：`FIOCLEX`/`FIONCLEX`/`FIONBIO`/`FIOASYNC`/`FS_IOC_FIEMAP` | 模型无通用命令层，全部命令直达对象 |
| `syscalls: 0=GETTICKS ...; unknown=-ENOSYS` | `fs/ioctl.c` 未知 cmd 返回 `-ENOTTY`；`include/uapi/asm-generic/errno.h` `-ENOSYS` | 模型把系统调用面当 ioctl 面展示，无真实 syscall 分发 |
| `l100test` 断言 | 无直接对应（LTP 文件系统测试套件） | 模型把打开/控制主题的可验证状态固化进内核 |

**权威来源**：Linux `fs/open.c`、`fs/ioctl.c`、`fs/read_write.c`、`include/linux/fs.h` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `open` 要把打开方式（flags）作为参数而不是让 read/write 自己决定？`f_pos` 存在 file 里而不是 inode 里，说明什么（同一个文件被两次 open 时各自的 offset 是否独立）？
2. **源码定位**：在 `kernel64.c` 中找出 `fd_open_model` 的全部调用点（提示：`shell_exec_path`、`shelltest`、`fdtest`），并说明每个调用传入的 `flags` 值。
3. **动手实验**：给 `fd_open_model` 增加一个「按 flags 拒绝」的规则：`flags>2` 时返回 -1，并在 `fdtest` 里加一条 `fd_open_model(0,3)<0` 的断言，重新构建运行验证。
4. **动手实验**：仿照 `timer_arm`/`timer_read`/`timer_cancel`，为 `sleep_model` 写三个「ioctl 式」函数（设置 sleep、读取 wake、取消），并添加 `sleepinfo` 打印元数据。
5. **Linux 对照**：阅读 `fs/ioctl.c` 的 `do_vfs_ioctl`，指出哪些命令会被通用层拦截、哪些下发给 `unlocked_ioctl`；对比教学模型「timer_arm 直接改元数据」的简化。

---

## 9. 本课小结与下一课预告

1. 本课把「设备打开」拆成三件事：路径定位、fd/file 分配、打开元数据（f_mode/f_flags/f_pos）初始化。
2. `fd_open_model(inode,flags)` 是这三件事的浓缩：`file_model{inode,offset=0,flags,refs=1}` 承载全部打开元数据，`fdinfo`/`fdtest` 负责观察与断言。
3. ioctl 是「控制面与数据面分离」的接口模型：read/write 传数据，ioctl 传命令（cmd+arg）；教学内核用定时器对象（`timer_arm`/`timer_read`/`timer_cancel`）复现 SET/GET/RESET 控制语义。
4. `syscallinfo` 把整个系统调用面作为元数据公开，未知命令 `-ENOSYS` 对应 Linux ioctl 的 `-ENOTTY`。
5. 检查点增量：新增 `l100test`（Origin Lesson 93），恢复历史命名 `l92test`，横幅与 `about` 更新。
6. 下一课（Lesson 101）主题转向**块设备请求队列**（对照 `drivers/block/` 与 `block/` 层），从字符设备转到块设备的数据路径。
