# Lesson 95: 文件打开与 file_operations — 精讲文档

> **课号**：Lesson 95 ｜ **主题**：文件打开与 file_operations（file opening and file_operations）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 88 原型的检查点
> **前置课程**：[`../lesson-94-stable/README.md`](../lesson-94-stable/README.md)（文件权限与访问检查）
> **后续课程**：[`../lesson-96-stable/README.md`](../lesson-96-stable/README.md)（文件偏移与引用计数）
> **一句话目标**：精讲 `open()` 从 inode 到 file 到 fd 的打开过程与 Linux `struct file_operations` 的「方法表」设计，验证教学内核 `fd_open_model`/`fd_close_model`/`fd_read_model` 对 file 对象的打开、关闭、读取三种操作。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第八课。`kernel64.c` 相对上一课仅做一处增量——把 `l94test` 恢复为 `l87test`，新增 `lesson_88_model` 状态与 `l95test` 检查点测试，并更新 `about`/开机横幅为本课主题。**如实说明**：本课是「检查点课：机制继承 + 本课检查点增量」——内核**没有**显式的 `struct file_operations` 表；file 对象的行为被硬编码在 `fd_read_model`/`fd_close_model` 等函数里，相当于「每个文件类型只有一份隐式 fops」。管道则是「另一种文件类型有另一套操作」的对照：`pipe_try_read`/`pipe_try_write`。本课以「打开 → 操作 → 释放」的 file 生命周期为线索精讲。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l88test`，但实际源码中本课检查点命令是 `l95test`（`l88test` 到 Lesson 96 才出现）。本文以源码为准：本课检查点命令为 `l95test`，历史回归命令 `l87test`（恢复自上一课 `l94test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：描述 Linux `open()` 打开一个文件后建立的三元组（`struct file` + `struct fd` + 关联 `struct inode`/dentry）；说出 `struct file_operations` 里最常见的五个方法（`llseek/read/write/read_iter/release`）与 VFS 通过 `file->f_op` 分派的机制；在教学内核中沿 `fd_open_model`（建 file+fd）→ `fd_read_model`（读）→ `fd_close_model`（释放）走一遍；运行 `l95test`/`fdtest`/`fdinfo`/`shellrun` 验证。

**在课程主线中的位置**：Lesson 94 讲了打开**之前**的守卫（权限检查）；本课讲打开**本身**——`open()` 成功后得到什么、这个对象支持哪些操作。Linux 用 `struct file` 描述「一个打开的实例」，用 `struct file_operations` 提供「这类文件能做什么」。下一课（Lesson 96）将深入 file 的**位置与引用**（`f_pos`、`f_count`），完成 file 对象模型的收官。

**前置知识清单**（学本课前必须掌握）：
1. inode/dentry 模型与四层 VFS 表（Lesson 88/91）。
2. `fd_open_model`/`fd_close_model` 的引用链（inode `refs++`/级联递减，Lesson 90）。
3. 管道对象：`pipe_try_read`/`pipe_try_write` 与 `pipe_model`（Lesson 44/48）。
4. shell 生命周期：`shell_exec_path` 的 open→记账→close 配对（Lesson 46/83）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 95: 文件打开与 file_operations`；
- 新命令 `l95test` 输出 `l95test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `fdtest`/`fdinfo` 展示打开两个文件、读、关闭两个文件的完整 file 生命周期与 `opens/closes/reads` 计数。

---

## 2. 核心概念精讲

### 2.1 struct file：打开实例的三要素

**直觉**：同一个文件（inode）可以被打开很多次，每次打开是一个**独立的实例**——它有自己当前读到哪里（`f_pos`）、以什么方式打开（`f_flags`）、被谁引用（`f_count`）。Linux 用 `struct file` 表示这个实例。

**教学模型的 `struct file_model`**：
```c
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
```
| 字段 | 对应 Linux `struct file` | 含义 |
|---|---|---|
| `inode_index` | `f_inode`/`f_path.dentry` | 指向打开的文件本体 |
| `offset` | `f_pos` | 当前读写位置（Lesson 96 展开） |
| `flags` | `f_flags` | 打开方式（`O_RDONLY` 等） |
| `refs` | `f_count` | 引用计数（Lesson 96 展开） |

`struct fd_model { u64 file_index; u8 valid; }` 对应进程的 fd 表槽（Linux `struct files_struct` 的 `fdtable`）。

### 2.2 file_operations：文件类型的方法表

**直觉**：普通文件能 `read`，管道也能 `read`，但两者的 `read` 实现完全不同。Linux 用一张**函数指针表**（`struct file_operations`）把「这类文件支持哪些操作」集中声明，VFS 只调用 `file->f_op->read(...)` 而不管背后是 ext4、pipe 还是 socket。

**Linux `struct file_operations`**（`include/linux/fs.h`）常见成员：`llseek`、`read`、`write`、`read_iter`、`write_iter`、`iterate_shared`、`mmap`、`open`、`release`、`fsync`。每个文件类型提供一份：普通文件用 `ext4_file_operations`，管道用 `pipefifo_fops`，目录用 `ext4_dir_inode_operations` 等。

**教学模型的对应**：模型没有 fops 表，file 的三种操作被硬编码为三个函数：
- `fd_open_model`（建 file/fd）≈ `filp_open` + 普通文件的 `open`；
- `fd_read_model`（推进 offset）≈ 普通文件的 `read_iter`；
- `fd_close_model`（递减 refs 并回收）≈ 普通文件的 `release`/`fput`。

这相当于「每个文件类型只有一份隐式 fops」，而管道的 `pipe_try_read`/`pipe_try_write` 就是「另一类文件的不同方法」的对照样例——两者正好演示了 fops 的存在意义：**同样叫 read，行为不同**。

### 2.3 打开路径回顾：dentry → inode → file → fd

```
open("/bin/sh")
  ramfs_lookup("/bin/sh") → dentry 节点 4 → inode 2
  fd_open_model(2, flags)  → 分配 file 槽 {inode=2, offset=0, flags, refs=1}
                             → 分配 fd 槽 {file_index=f}
                             → inode_table[2].refs++
```

这条链在 Lesson 91/92/90 分别讲过 dentry、解析、引用，本课聚焦中间的**两跳**：`fd_open_model` 如何在 `fd_table`/`file_table` 两个定长表里分配槽位——这正是「打开」这个动作在模型里的全部内涵。

### 2.4 检查点模型：lesson_88_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l95test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 88）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l87test`；新增 `lesson_88_model`/`lesson_88_state`/`l95test`；`about` 与横幅更新。file 打开/操作机制由累积代码承载（`file_table`/`fd_table`/`fd_open_model` 等），本课以「打开与 file_operations」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`文件打开与 file_operations`/`Lesson 95`/`l95test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（文件打开/file_operations 视角 + 本课增量）

#### 3.2.1 file 对象与打开路径：fd_open_model

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. **入参即打开请求**：`inode` 是已解析的 inode 下标，`flags` 是打开方式——对应 Linux `filp_open(path, flags, mode)` 的后两个参数。
2. **两层分配**：先在外层循环找空 fd 槽，再在内层循环找空 file 槽——`FILE_MAX=3`/`FD_MAX=4` 的定长容量就是「同时最多打开 3 个 file、4 个 fd」的边界（对应 Linux fdtable 的动态扩容被省略）。
3. **file 初始化**：`{inode, offset=0, flags, refs=1, valid=1}`——offset 清零、refs 置 1（新建即被 fd 持有）。
4. **fd 绑定与 inode 引用**：`fd_table[i]={f,1}` 把 fd 指向 file；`inode_table[inode].refs++` 使 inode 多一个持有者（Lesson 90 的链）。
5. **记账**：`fd_opens++`，供 `fdinfo`/`fdtest` 观察。
6. **失败语义**：inode 无效返回 -1；表满返回 -1——对应 Linux `-EBADF`/`-EMFILE` 的简化。

#### 3.2.2 file 的读操作：fd_read_model

```c
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
```
1. **三级解引用**：fd → file → inode 逐层校验有效，任何一层悬垂即拒绝——这是「操作必须作用于活对象」的护栏，等价于 Linux `fdget(fd)` 拿到 file 后对 `f_op` 的信任边界。
2. **读的语义**：从 `file_table[f].offset` 读到 inode 的 `size` 为止，`bytes` 超过剩余量则截断——对应 Linux `read` 的 `count` 钳制与 EOF。
3. **推进位置**：`file_table[f].offset+=bytes`——file 的位置随读前进（`f_pos` 语义，Lesson 96 展开）。
4. **记账**：`fd_reads++`。教学模型只移动元数据，不拷贝任何字节（真实读由 `vfs_read` 分派到 fops 的 `read_iter`）。

#### 3.2.3 file 的释放操作：fd_close_model

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
```
1. **先释放 fd 槽**：`fd_table[fd].valid=0`。
2. **file 引用递减**：`file_table[f].refs--`；归零时才回收 file 槽（`valid=0`）并级联递减 inode 引用——对应 Linux `fput()`→`__fput()`→`iput()` 的延迟释放链。
3. **幂等边界**：再次 close 同一个 fd 因 `valid==0` 返回 0——防止 double-close。
4. 这整套「open 分配 → read 操作 → close 释放」就是 file 生命周期，也是本课主题「文件打开与 file_operations」在模型中的三个操作点。

#### 3.2.4 观察与验证：fdinfo / fdtest

```c
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");hex64(c,file_table[f].offset);putc64(c,'\n');}text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
static TEXT64 void fdtest(u16*c){int a=fd_open_model(0,1),b=fd_open_model(1,2),r1=a>=0&&b>=0&&fd_read_model((u32)a,8)&&fd_close_model((u32)b)&&fd_close_model((u32)a);text64(c,"fdtest: ");text64(c,r1&&fd_opens==2&&fd_closes==2?"fd/file/inode/dentry refs and offsets passed":"BROKEN");putc64(c,'\n');}
```
1. `fdinfo` 把每个存活 fd 的行展开：`fd N file F inode I off O`，再打印 `opens/closes/reads/seeks` 四项计数——是观察 file 生命周期状态的仪表盘。
2. `fdtest` 打开 inode 0（flags=1）与 inode 1（flags=2），读 fd a 8 字节（offset 前进），再关闭 b、a，断言 `fd_opens==2 && fd_closes==2`：
   `fdtest: fd/file/inode/dentry refs and offsets passed`。
3. 注意 `fdtest` 两个 fd 各绑一个 file（inode 0 与 inode 1），演示「不同 inode 各自独立实例」；共享同一 inode 的双 fd 场景在 Lesson 96 的引用计数里讨论。

#### 3.2.5 「另一类文件的不同方法」：pipe_try_read / pipe_try_write

```c
static TEXT64 int pipe_try_write(u8 value){u8 id;if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}pipe_model.data[pipe_model.head]=value;pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);pipe_model.used++;pipe_model.writes++;if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;return 1;}
static TEXT64 int pipe_try_read(u8*out){u8 id;if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}*out=pipe_model.data[pipe_model.tail];pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);pipe_model.used--;pipe_model.reads++;if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;return 1;}
```
1. 管道的 `read`/`write` 是**环形缓冲**上的 FIFO 存取（`head`/`tail`/`used`），并带阻塞唤醒——与普通 file 的「按 offset 读 inode」完全不同。
2. 这正是 fops 的意义：**同名方法、不同实现**。Linux 里管道注册 `pipefifo_fops`，普通文件注册 `ext4_file_operations`；教学模型虽然没有 fops 表，但 `fd_read_model` 与 `pipe_try_read` 的对立，就是「两种 file 类型两套操作」的最小演示。

#### 3.2.6 本课新增检查点函数

```c
struct lesson_88_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_88_model lesson_88_state;
static TEXT64 void l95test(u16*c){lesson_88_state=(struct lesson_88_model){88U,89U,90U,91U,1,1,1,1};int ok=lesson_88_state.valid&&lesson_88_state.active&&lesson_88_state.ready&&lesson_88_state.accounted&&lesson_88_state.b==lesson_88_state.a+1U;text64(c,"l95test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 88 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 88→91（Origin 编号 Lesson 88），四布尔位全置 1。
2. **成功串**：`l95test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 88 fallback reported`。
3. **恢复的 `l87test`**：本课同时恢复 `l87test`（`lesson_87_state`，87→90，由上一课 `l94test` 更名而来），历史检查点可独立运行。

#### 3.2.7 exec64 增量与开机横幅

- `about` 输出 `Lesson 95: 文件打开与 file_operations\n`；检查点分支：
```c
else if(eq64(word,"l87test")){if(!noargs64(arg))usage64(c,"l87test");else l87test(c);}else if(eq64(word,"l95test")){if(!noargs64(arg))usage64(c,"l95test");else l95test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 95: 文件打开与 file_operations\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `文件打开与 file_operations` 与 `Lesson 95`、kernel64.c 含 `l95test`，最后打印 `Multiboot2 and Lesson 95 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma → vfs_init()（inode/dentry/file/fd 四表）
 ├─ 横幅 "Lesson 95: 文件打开与 file_operations"
 └─ 主循环：命令 → exec64
     ├─ l95test → 阶段检查点
     ├─ fdtest → fd_open_model×2 → fd_read_model → fd_close_model×2
     ├─ fdinfo → 展开存活 fd/file/inode/off 与 opens/closes/reads 计数
     └─ shellrun → shell_exec_path 内 open/close 配对执行 /bin/sh
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 建四表，打印横幅 `Lesson 95: 文件打开与 file_operations`。
2. **`l95test`** → `l95test(c)` → 断言 `lesson_88_state` → `l95test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`fdtest`** → `fd_open_model(0,1)` 得 fd a、`fd_open_model(1,2)` 得 fd b → `fd_read_model(a,8)` → `fd_close_model(b)`、`fd_close_model(a)` → `fdtest: fd/file/inode/dentry refs and offsets passed`。
4. **`fdinfo`** → 对存活 fd 打印 `fd N file F inode I off O`，再打印 `opens/closes/reads/seeks: 2 2 1 0`（紧随 fdtest 时）。
5. **`shellrun`** → `vfs_init()` 后 `shell_exec_path("/bin/sh",2,1)`：open 建 file → 记账 → close 释放 → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
6. **`about`** → `Lesson 95: 文件打开与 file_operations`。

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
Multiboot2 and Lesson 95 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 95: 文件打开与 file_operations` 横幅 |
| `l95test` | `l95test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l87test` | `l87test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `fdinfo` | 首行 `fd/file/inode/dentry tables (bounded)`，存活 fd 行 `fd N file F inode I off O`，末行 `opens/closes/reads/seeks: ...` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 95: 文件打开与 file_operations` |

判定成功：`l95test`/`fdtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l95test` 输出 `Lesson 88 fallback reported` | `lesson_88_state` 初始化/断言失败（stale 镜像） | `grep -n "l95test" kernel64.c`；确认初始化串 `{88U,89U,90U,91U,1,1,1,1}` |
| `fdtest` 输出 `BROKEN` | `fd_open_model` 分配失败或 `fd_opens/closes` 计数不符 | 对照 `fd_open_model` 的两层循环与 `FILE_MAX=3`/`FD_MAX=4` 容量；确认 open 两次、close 两次 |
| `fdinfo` 显示 fd 行缺失 | 该 fd 未打开或已关闭 | 先运行 `fdtest` 再 `fdinfo`；`fd_table[fd].valid` 只在 open 时置 1 |
| `fdinfo` off 与预期不符 | `fd_read_model` 的 offset 推进异常 | 对照 `fd_read_model` 的 `bytes>remaining` 钳制与 `offset+=bytes` |
| `fd_close_model` 未递减 inode refs | file refs 未归零 | 对照 `if(!file_table[f].refs){...inode_table[n].refs--;}` 的条件递减 |
| `shellrun` 输出 `BROKEN` | open/close 配对或记账异常 | 检查 `shell_exec_path` 的 `fd_open_model` 后若 `fd<0` 直接返回、结束前 `fd_close_model` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 95' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `文件打开与 file_operations` 与 `Lesson 95` |
| 误敲 `l88test` 无响应 | `l88test` 在本课源码中不存在（Lesson 96 才引入） | 本课检查点命令是 `l95test`；旧 README 的 `l88test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `struct file_model{inode_index,offset,flags,refs}` | `include/linux/fs.h` 的 `struct file`（`f_inode`/`f_pos`/`f_flags`/`f_count`/`f_op`） | 模型无 `f_op` 指针、无 `f_path`/`f_mapping`、无锁 |
| `struct fd_model{file_index}` | `fs/file.c` 的 `struct files_struct`/`fdtable`（fdtable 数组） | 模型 4 个定长 fd 槽，无 `get_unused_fd` 的动态分配与 `filp_close` 复杂度 |
| `fd_open_model` 分配 file+fd | `fs/open.c` 的 `filp_open()`→`path_openat()`→`alloc_file()`；`fs/file.c` 的 `fd_install()` | 模型跳过 path 解析与安全钩子，直接用 inode 下标 |
| `fd_read_model` 按 offset 读 inode | `fs/read_write.c` 的 `vfs_read()`→`f_op->read_iter()`；普通文件 `ext4_file_operations.read_iter` | 模型无 fops 表、无缓冲页，只移动 offset 元数据 |
| `fd_close_model` 延迟递减引用 | `fs/file_table.c` 的 `fput()`→`__fput()`→`f_op->release()`；`fs/inode.c` 的 `iput()` | 模型无 fput 的 task_work/RCU 延迟，级联直接递减 |
| `pipe_try_read/pipe_try_write` 作为另一套操作 | `fs/pipe.c` 的 `pipefifo_fops`（`pipe_read`/`pipe_write`） | 模型管道无页缓冲与 splice，仅环形 u8 数组 |
| `fdinfo`/`fdtest` 记账 | `/proc/<pid>/fd`（`fs/proc`）与 LTP 打开/关闭测试 | 模型把 file 生命周期断言固化进内核 |
| `l95test` 断言 | 无直接对应 | 模型把打开主题检查点固化 |

**权威来源**：Linux `include/linux/fs.h`、`fs/open.c`、`fs/file.c`、`fs/file_table.c`、`fs/read_write.c`、`fs/pipe.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么同一个 inode 被打开两次需要两个 `struct file`？若教学模型让两个 fd 共享同一个 file 槽，会破坏哪个不变量？
2. **源码定位**：在 `kernel64.c` 中列出 `fd_table` 与 `file_table` 的所有读/写点，画出 `fdtest` 过程中 fd 0/1、file 0/1、inode 0/1 三者的绑定变化。
3. **动手实验**：修改 `fdtest`，先打开同一 inode 两次（`fd_open_model(0,1)` 两次），再依次关闭，观察 `fdinfo` 中 inode refs 的变化；对比 Linux 中两个 fd 指向同一 inode 时的 `f_pos` 独立性。
4. **动手实验**：给 `fd_close_model` 去掉 `if(file_table[f].refs)` 分支，重新构建运行 `fdtest`，观察引用计数破坏后的断言结果。
5. **Linux 对照**：阅读 `include/linux/fs.h` 的 `struct file_operations`，数出其中至少 10 个方法；为教学内核的三个函数（open/read/close）设想一个最小的 `struct file_operations` 教学版应包含哪些成员。

---

## 9. 本课小结与下一课预告

1. 本课讲清了 `open()` 的产出物：`struct file`（打开实例）与 fd 槽，以及两者如何由 `fd_open_model` 一次性建立。
2. `struct file_operations` 是 Linux「文件类型方法表」的核心设计；教学模型以三个硬编码函数承载 open/read/close，是「单份隐式 fops」的简化。
3. 管道是「另一类文件另一套操作」的对照，`pipe_try_read` 与 `fd_read_model` 的行为差异演示了 fops 存在的意义。
4. file 生命周期 = 分配（`fd_open_model`）→ 操作（`fd_read_model` 推进 offset）→ 释放（`fd_close_model` 级联递减），与 inode 生命周期（Lesson 90）嵌套衔接。
5. `fdinfo` 是观察 file 对象状态的仪表盘，`fdtest` 把打开/读/关闭固化为可断言回归。
6. `l95test` 沿用 VFS/设备阶段检查点家族，`l87test` 历史检查点保留。
7. 下一课（[`../lesson-96-stable/README.md`](../lesson-96-stable/README.md)，Lesson 96）将深入 file 的两个核心字段——**文件偏移与引用计数**（`f_pos`/`f_count`，对照 `fs/read_write.c` 的 `vfs_read`/`vfs_write`）。
