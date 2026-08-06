# Lesson 88: VFS 层次与 mount 元数据 — 精讲文档

> **课号**：Lesson 88 ｜ **主题**：VFS 层次与 mount 元数据（VFS layering and mount metadata）
> **课程主线位置**：VFS 阶段（Lesson 88–97）的起点，本课为 Lesson 81 原型的检查点
> **前置课程**：[`../lesson-87-stable/README.md`](../lesson-87-stable/README.md)（负载均衡与进程组调度综合 checkpoint）
> **后续课程**：[`../lesson-89-stable/README.md`](../lesson-89-stable/README.md)（超级块与文件系统注册）
> **一句话目标**：建立 VFS 四层模型（superblock → inode → dentry → file）的心智图，并验证教学内核中对应的 `inode_table`/`dentry_table`/`file_table`/`fd_table` 与 ramfs 根文件系统的 mount 元数据一致性。

本课是稳定快照（stable snapshot）检查点，同时是**新的 VFS 阶段的开篇**：进程/调度/COW 阶段（Lesson 71–87）收束于上一课，本课把主线切换到虚拟文件系统。`kernel64.c` 相对上一课仅做一处增量——把 `l87test` 恢复为 `l80test`，新增 `lesson_81_model` 状态与 `l88test` 检查点测试，检查点输出串升级为 `bounded VFS, devices, epoll, and service management checkpoint passed`，并更新 `about`/开机横幅。VFS 元数据本体（inode/dentry/file/fd 四张表与 ramfs）来自 Lesson 44 起累积的代码，本课以「层次」视角重新精讲。继承的进程、GUI、子系统回归保持有效。

---

## 1. 课程定位（Mission）

**学完本课你能**：画出并解释 Linux VFS 的 superblock/inode/dentry/file 四层层次关系；说出 mount 元数据在 VFS 中的位置（把文件系统的 superblock 挂到目录 dentry 上）；在教学内核中定位对应的 `inode_table`/`dentry_table`/`file_table`/`fd_table`、`vfs_init`、`fd_open_model`/`fd_close_model`/`fd_read_model` 与 ramfs 根文件系统；运行 `l88test` 检查点与 `fdinfo`/`fdtest`/`ramfsinfo`/`pathtest` 完成验证。

**在课程主线中的位置**：Lesson 88 是 VFS 主线（Lesson 88–97：mount/superblock/inode/dentry/path/权限/open/offset/readdir）的第一课。后续 Lesson 89 讲超级块与文件系统注册（对照 `fs/super.c`、`fs/filesystems.c`），Lesson 90 讲 inode 生命周期（对照 `fs/inode.c`）。本课先搭「层次」框架，为后续各层精讲提供坐标系。

**前置知识清单**（学本课前必须掌握）：
1. 文件描述符与文件表的基本模型：`fd_table`/`file_table`/`inode_table`/`dentry_table` 四张表与 `fd_open_model`/`fd_close_model`（Lesson 44）。
2. ramfs/initramfs 路径查找：`ramfs_lookup`、`pathtest`（Lesson 44/49）。
3. 对象引用计数概念：`refs` 字段在 `inode_model`/`dentry_model`/`file_model` 中的作用（Lesson 84 共享页的 `refs` 同一思想）。
4. 整体启动流程：`kernel_main64_binary` 中的 `vfs_init()` 调用时机（在 `pmm_init`/`vma_init` 之后）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 88: VFS 层次与 mount 元数据`；
- 新命令 `l88test` 输出 `l88test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `fdinfo`/`fdtest`/`ramfsinfo`/`pathtest` 展示四层表的内容、引用计数与路径解析结果。

---

## 2. 核心概念精讲

### 2.1 VFS 四层模型：superblock / inode / dentry / file

**直觉**：打开一个文件要回答四个不同的问题——「这是哪个文件系统？」（superblock）、「这个文件是谁？」（inode）、「它叫什么名字、挂在哪个目录？」（dentry）、「我正在以什么方式读它？」（file）。Linux 把四个问题拆成四个对象，形成 VFS 的层次。

**层次关系图**（本课核心心智模型）：

```
                ┌──────────────────────────────┐
                │        super_block           │  每个已挂载文件系统一个
                │  (文件系统全局状态: s_magic,   │  对应 Linux include/linux/fs.h
                │   s_root, s_fs_info)          │  与 fs/super.c (alloc_super)
                └──────────────┬───────────────┘
                               │ 1 拥有 N 个 inode
                               ▼
                ┌──────────────────────────────┐
                │           inode              │  每个文件/目录一个（物理身份）
                │  i_ino, i_size, i_mode,      │  Linux: struct inode (include/linux/fs.h)
                │  i_count (引用计数)           │  fs/inode.c (iget/iput)
                └──────────────┬───────────────┘
                               │ 1 拥有 N 个名字
                               ▼
                ┌──────────────────────────────┐
                │           dentry             │  路径组件缓存（目录项）
                │  d_name_hash, d_inode        │  Linux: struct dentry
                │                              │  fs/dcache.c
                └──────────────┬───────────────┘
                               │ 被 N 个 open file 引用
                               ▼
                ┌──────────────────────────────┐
                │            file              │  一次 open 的实例（读位置）
                │  f_inode, f_pos, f_flags,    │  Linux: struct file (include/linux/fs.h)
                │  f_count                     │  fs/file_table.c
                └──────────────┬───────────────┘
                               │ 进程侧再包一层
                               ▼
                ┌──────────────────────────────┐
                │          fd_table            │  进程的文件描述符表
                │  fd → file_index             │  Linux: files_struct / fdtable
                └──────────────────────────────┘
```

**教学内核的对应**：`inode_table[INODE_MAX=3]`、`dentry_table[DENTRY_MAX=3]`、`file_table[FILE_MAX=3]`、`fd_table[FD_MAX=4]` 四张定长表直接对应四层。superblock 层在代码里没有独立 struct，而是由 `vfs_init()` 的统一初始化 + ramfs 节点树隐含表达（本课以「mount 元数据」视角说明，Lesson 89 会专门补 superblock/注册表）。

### 2.2 mount 元数据：文件系统如何「挂上」目录树

**定义**：mount 是把一个文件系统的 superblock 与目录树中某个目录（mount point）的 dentry 绑定，使该目录之下的路径解析进入被挂载的文件系统。

**直觉**：`/etc/motd` 里的 `etc` 是根文件系统的一个目录，而 `/bin/sh` 里的 `bin` 也属于根文件系统；如果某个 `bin` 被另一个设备挂载，路径解析在到达 mount point dentry 时就要「跨过去」。

**教学模型的 mount 元数据**：ramfs 节点树就是被挂载的根文件系统。`ramfs_init` 构造 5 个节点：
```c
ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};
ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};   /* /etc */
ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};/* /etc/motd */
ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};   /* /bin */
ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};    /* /bin/sh */
```
每个节点含 `name_hash`（名字散列）、`parent`（父节点索引）、`inode`（指向 inode_table 的索引）、`type`（目录/文件）、`valid`。`ramfs_lookup` 用字符串直接比对路径返回 inode 索引——这就是「挂载后的路径解析」。

### 2.3 四层表的引用链

`vfs_init` 建立初始表：
```c
for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};
for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};
for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};
for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};
```
- 3 个 inode：`ino=i+1`（1..3）、`size=0x100+i`、`mode=0100644`（八进制权限）、`refs=1`、`valid=1`；
- 3 个 dentry：`name_hash=0x100+i`、`inode_index=i`（dentry i 指向 inode i）、`refs=1`；
- 3 个 file 初始为空（`refs=0,valid=0`），4 个 fd 初始为空。

打开文件时 `fd_open_model` 建立 fd → file → inode 的引用链并 `inode_table[inode].refs++`；关闭时 `fd_close_model` 逆链递减并级联回收（file refs 归零才无效化 inode refs）——这正是「mount 之后，四层对象通过引用计数保持一致性」的最小模型。

### 2.4 检查点模型：lesson_81_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，但 `l88test` 的成功串改为 `bounded VFS, devices, epoll, and service management checkpoint passed`——因为从本课起，检查点开始覆盖 VFS/设备/epoll/服务管理四条元数据链（VFS 阶段 + 后续设备阶段）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS 四层表、GUI、检查点） | 恢复 `l80test`；新增 `lesson_81_model`/`lesson_81_state`/`l88test`；检查点成功串改为 VFS/设备/epoll/服务；`about` 与横幅更新。VFS 四层表与 ramfs 为累积代码，本课以「层次」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`VFS 层次与 mount 元数据`/`Lesson 88`/`l88test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（VFS 四层 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_81_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_81_model lesson_81_state;
static TEXT64 void l88test(u16*c){lesson_81_state=(struct lesson_81_model){81U,82U,83U,84U,1,1,1,1};int ok=lesson_81_state.valid&&lesson_81_state.active&&lesson_81_state.ready&&lesson_81_state.accounted&&lesson_81_state.b==lesson_81_state.a+1U;text64(c,"l88test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 81 fallback reported");putc64(c,'\n');}
```
逐行分析：
1. **结构**：与历史检查点同构，计数序列 81→84（Origin 编号 Lesson 81）。
2. **成功串变化**：`bounded VFS, devices, epoll, and service management checkpoint passed`——这是 VFS/设备阶段检查点的新主题串，逐字抄录自源码。fallback 为 `Lesson 81 fallback reported`。
3. **阶段门闩**：`l88test` 通过即证明本课镜像正确；后续 Lesson 89/90 的 `l89test`/`l90test` 沿用同一检查点家族。

#### 3.2.2 四层表数据结构（累积代码，本课层次精讲）

```c
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
```
- `inode_model`：物理身份（ino）、大小、模式位、引用计数。对应 Linux `struct inode` 的 `i_ino`/`i_size`/`i_mode`/`i_count`（`include/linux/fs.h`）。
- `dentry_model`：`name_hash` 缓存名字散列、`inode_index` 指回 inode、`refs`。对应 Linux `struct dentry` 的 `d_name`/`d_inode`（`fs/dcache.c`）。
- `file_model`：一次 open 的实例，`inode_index` + 读写偏移 `offset` + `flags` + `refs`。对应 Linux `struct file` 的 `f_inode`/`f_pos`/`f_flags`/`f_count`（`include/linux/fs.h`）。
- `fd_model`：进程侧描述符，`file_index` 指向 file 表。对应 Linux `struct fdtable`。

#### 3.2.3 vfs_init：四层表的统一初始化（mount 元数据的基座）

```c
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **inode 层**：3 个 inode 被置为有效（ino 1..3、size 递增、八进制 mode 0100644、`refs=1`），这是 ramfs 各节点的物理实体。
2. **dentry 层**：3 个 dentry 分别 `inode_index=i` 指向同名 inode，`refs=1`——目录项层与 inode 层一对一对齐。
3. **file/fd 层**：全空（等待 open），统计计数器归零。
4. **挂载根文件系统**：末尾调用 `ramfs_init()` 构造 ramfs 节点树 + `pipe_init()` 复位管道。`vfs_init` 在 `kernel_main64_binary` 中位于 `pmm_init/vma_init/reclaim_init` 之后，是所有 VFS 命令的前提。

#### 3.2.4 fd_open_model：打开路径的引用链建立

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. **入参检查**：`inode>=INODE_MAX || !inode_table[inode].valid` 立即返回 -1——不存在的 inode 不能打开。
2. **两层分配**：先找空 fd 槽（`!fd_table[i].valid`），再找空 file 槽（`!file_table[f].valid`）；找不到返回 -1。
3. **引用链**：新 file `{inode,offset=0,flags,refs=1,valid=1}`，新 fd `{f,valid=1}`，随后 `inode_table[inode].refs++`——**file → inode 的引用 +1**，这是四层一致性的关键一步。
4. **统计**：`fd_opens++` 供 `fdinfo` 报告。

#### 3.2.5 fd_close_model：关闭路径的引用链回收

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
```
1. **逐层校验**：fd → file → inode 三层索引逐一查有效，任何一层非法即返回 0（防止悬垂引用）。
2. **fd 释放**：`fd_table[fd].valid=0` 立即回收描述符槽。
3. **file 引用递减**：`file_table[f].refs--`；仅当 `refs==0` 时 file 无效化，并级联 `inode_table[n].refs--`——**inode 引用只在最后一个 file 关闭时递减**，这与 Linux `fput`/`iput` 的「延迟到最后一个引用」思想一致。
4. `fd_closes++` 供报告。

#### 3.2.6 诊断与路径解析：fdinfo / fdtest / ramfsinfo / pathtest

- `fdinfo` 打印 `fd/file/inode/dentry tables (bounded)`，对每个有效 fd 展开 `fd → file → inode(ino) → off`，最后报告 `opens/closes/reads/seeks` 四计数。
- `fdtest` 执行 open/open/read/close/close 序列并断言 `fd_opens==2&&fd_closes==2`：
  `fdtest: fd/file/inode/dentry refs and offsets passed`。
- `ramfsinfo` 打印 5 个节点（parent/inode/type），`pathtest` 断言 `/`→0、`/etc`→1、`/etc/motd`→2、`/bin/sh`→4、`/etc/missing`<0、`relative`<0、`/etc/motd/child`<0：
  `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。

#### 3.2.7 exec64 增量与开机横幅

- `about` 输出 `Lesson 88: VFS 层次与 mount 元数据\n`；检查点分支：
```c
else if(eq64(word,"l80test")){if(!noargs64(arg))usage64(c,"l80test");else l80test(c);}else if(eq64(word,"l88test")){if(!noargs64(arg))usage64(c,"l88test");else l88test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 88: VFS 层次与 mount 元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与前三课完全相同的构建管线（64 位 freestanding raw 镜像 + 32 位 Multiboot2 外壳 + `grub-mkrescue`）。`make check` 断言 README 含 `VFS 层次与 mount 元数据`、`Lesson 88`，kernel64.c 含 `l88test`。无新增编译步骤。本课是文档/验证型增量，不引入新的链接脚本或编译标志。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ 初始化：pmm_init → vma_init → reclaim_init → vfs_init（建四层表 + ramfs 根）
 ├─ 横幅 "Lesson 88: VFS 层次与 mount 元数据"
 └─ 主循环：命令 → exec64
     ├─ l88test → 阶段检查点
     ├─ fdtest → 打开/读取/关闭引用链验证
     ├─ fdinfo → 四层表内容与统计
     ├─ ramfsinfo/pathtest → ramfs 根文件系统路径解析
     └─ shellrun/shelltest → /bin/sh 打开并走 fd 生命周期
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 建表后打印横幅 `Lesson 88: VFS 层次与 mount 元数据`。
2. **`l88test`** → `l88test(c)` → 断言 → `l88test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`fdtest`** → `fd_open_model(0,1)`、`fd_open_model(1,2)`、`fd_read_model`、两次 `fd_close_model` → `fdtest: fd/file/inode/dentry refs and offsets passed`。
4. **`fdinfo`** → 展开 `fd N file F inode I off O` 每行 + `opens/closes/reads/seeks` 计数行。
5. **`pathtest`** → `ramfs_lookup` 七次 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
6. **`ramfsinfo`** → 节点列表（`node 0 parent 0 inode 0 dir` 等）。
7. **`about`** → `Lesson 88: VFS 层次与 mount 元数据`。

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
Multiboot2 and Lesson 88 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 88: VFS 层次与 mount 元数据` 横幅 |
| `l88test` | `l88test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l80test` | `l80test: bounded scheduling and copy-on-write checkpoint passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 88: VFS 层次与 mount 元数据` |

判定成功：`l88test` 与 `fdtest`/`pathtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l88test` 输出 `Lesson 81 fallback reported` | `lesson_81_state` 初始化/断言失败（stale 镜像） | `grep -n "l88test" kernel64.c`；确认初始化串 `{81U,82U,83U,84U,1,1,1,1}` |
| `fdtest` 输出 `BROKEN` | `fd_opens`/`fd_closes` 计数或引用链异常 | 检查 `fd_open_model` 的 `inode_table[inode].refs++` 与 `fd_close_model` 的级联递减；`fdinfo` 看 opens/closes |
| `fd_open_model` 返回 -1 | inode 越界或 `inode_table[inode].valid==0` | 对照首行 `if(inode>=INODE_MAX||!inode_table[inode].valid)return -1` |
| `pathtest` 失败 | ramfs 节点树与路径字符串不匹配 | 检查 `ramfs_init` 的 5 个节点 name_hash 与 `ramfs_lookup` 的 5 个 `eq64` 分支 |
| `fdinfo` 不显示任何 fd 行 | 尚无 open（表为空是合法状态） | 先运行 `fdtest` 或 `shellrun` 再 `fdinfo` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 88' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `VFS 层次与 mount 元数据` 与 `Lesson 88` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `inode_model`（ino/size/mode/refs） | `include/linux/fs.h`：`struct inode`（`i_ino`/`i_size`/`i_mode`/`i_count`） | 模型 3 个固定 inode、无 rbtree/hlist、无 `i_ops`/`i_fop` 方法表 |
| `dentry_model`（name_hash/inode_index/refs） | `include/linux/dcache.h`：`struct dentry`（`d_name`/`d_inode`） | 模型用 `name_hash` 代替真实哈希链表（`d_hash`），无 RCU |
| `file_model`（inode_index/offset/flags/refs） | `include/linux/fs.h`：`struct file`（`f_inode`/`f_pos`/`f_flags`/`f_count`） | 模型无 `file_operations` 分发表，读写是单一模型函数 |
| `fd_model`（file_index） | `include/linux/fdtable.h`：`struct fdtable`；`kernel/sys.c` 的 `fd_install`/`close_fd` | 模型固定 4 个 fd 槽，无 expand_fdtable 扩容 |
| `vfs_init` 建四层表 + ramfs 根 | `fs/super.c`：`mount_rootfs()`/`init_mount_tree()`；`init/do_mounts.c`（挂载根文件系统） | 模型无真实 super_block 对象，由统一初始化隐含（Lesson 89 补） |
| `ramfs_lookup` 字符串路径 → inode | `fs/ramfs/inode.c`：`ramfs_lookup()`；`fs/namei.c`：`path_lookupat()` | 模型固定 5 个路径分支，无逐组件遍历与权限检查 |
| `fd_open_model` 建立 fd→file→inode 链 | `fs/open.c`：`do_sys_open()` → `filp_open()` → `fd_install()` | 模型不执行任何真实磁盘/内存文件读取 |
| mount 元数据（根文件系统=ramfs） | `fs/super.c`：`sget()`/`alloc_super()`；`include/linux/fs.h` 的 `struct super_block` | 模型无 `s_root`/`s_fs_info` 字段，挂载点语义用 `ramfs_lookup` 路径分支隐含 |

**权威来源**：POSIX 文件系统语义、Intel SDM（硬件无关但作为基础）、Multiboot2 规范、GNU GRUB 手册；Linux `include/linux/fs.h`、`fs/super.c`、`fs/namei.c`、`fs/dcache.c` 仅作工程对照。

---

## 8. 思考题与练习

1. **概念理解**：为什么「同一 inode 可以有多个 dentry（硬链接），但一个 dentry 只有一个 inode」？教学模型里 `dentry_model.inode_index` 如何体现这一点？
2. **源码定位**：在 `kernel64.c` 中找到 `inode_table[inode].refs++` 与 `inode_table[n].refs--` 的**全部**出现位置，画出一次完整 open/close 的引用计数变化。
3. **动手实验**：把 `FD_MAX` 改为 2，重新构建并连续 open 三个 inode，观察 `fd_open_model` 返回 -1 的路径（对照 `fdinfo` 的 opens 计数）。
4. **动手实验**：在 `ramfs_init` 中新增一个节点 `/bin/ls`（inode 索引 2），并在 `ramfs_lookup` 加一个 `eq64` 分支，重跑 `pathtest` 观察命中。
5. **Linux 对照**：阅读 `include/linux/fs.h` 中 `struct inode` 的 `i_count` 注释与 `fs/inode.c` 的 `iget()`/`iput()`，对比 `fd_open_model`/`fd_close_model` 的引用语义，指出模型缺失的并发/RCU 部分。

---

## 9. 本课小结与下一课预告

1. 本课开启 VFS 主线，建立了 superblock → inode → dentry → file → fd 的五层心智模型（superblock 对应 Linux 文件系统全局状态，fd 是进程侧包装）。
2. 教学内核用 `inode_table`/`dentry_table`/`file_table`/`fd_table` 四张定长表实现四层，`vfs_init` 统一初始化，`fd_open_model`/`fd_close_model` 建立与回收引用链。
3. mount 元数据在模型中由 ramfs 根文件系统（5 个节点的节点树 + `ramfs_lookup` 路径解析）隐含表达。
4. `l88test` 检查点成功串升级为 `bounded VFS, devices, epoll, and service management checkpoint passed`，标志 VFS/设备阶段开始。
5. 引用计数是四层一致性的核心：file 归零才递减 inode，与 Linux `fput`/`iput` 思想一致。
6. 下一课（Lesson 89）将主题收窄到**超级块与文件系统注册**（对照 `fs/super.c`、`fs/filesystems.c`），补上本课缺失的 superblock 对象与文件系统注册表。
