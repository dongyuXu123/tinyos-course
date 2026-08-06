# Lesson 149: mount namespace 隔离 — 精讲文档

> **课号**：Lesson 149 ｜ **主题**：mount namespace 隔离（mount namespace isolation）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课是「namespace 三连」的第二课（进程 148 → **mount 149** → network 150）
> **前置课程**：[`../lesson-148-stable/README.md`](../lesson-148-stable/README.md)（进程 namespace）
> **后续课程**：[`../lesson-150-stable/README.md`](../lesson-150-stable/README.md)（network namespace）
> **一句话目标**：讲清「mount namespace 为什么能让每个容器看到不同的文件系统树」——挂载点按世界隔离、`mount` 传播选项与 `CLONE_NEWNS` 的共享/复制语义，对照 Linux `fs/namespace.c`、`include/linux/mnt_namespace.h`，并把教学内核继承的 `ramfs_nodes`/`vfs_init`/`dentry_table` 文件系统设施按这一主题系统化复述，运行 `l149test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（mount namespace 对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l148test` 恢复为历史命名 `l141test`（挂 `lesson_141_state`），新增 `lesson_142_model`/`lesson_142_state` 与 `l149test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l149test`（旧 README 所写 `l142test` 按源码勘误，源码中不存在 `l142test` 命令）；另保留历史检查点 `l100test`–`l141test`，以及 `ramfsinfo`/`pathtest`/`fdinfo`/`fdtest`/`shellrun`/`execpath`/`initinfo`/`shelltest` 等文件系统相关回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「挂载点按世界隔离」的直觉解释 mount namespace（容器内 `mount`/`umount` 不影响宿主机，因为每个容器有独立的挂载表副本）；说出 Linux 中 mount namespace 由 `struct mnt_namespace` 承载、挂载传播用 `fs/namespace.c` 的 `propagate_mnt`/`CLONE_NEWNS` 语义；在教学内核中沿 `ramfs_nodes` → `ramfs_lookup` → `pathtest` → `l149test` 观察文件系统路径解析与检查点状态；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 143–156 是 bounded networking / namespace / cgroup / security 收敛检查点阶段；本课是 **namespace 三连的第二课**（进程 148 → **mount 149** → network 150）。上一课建立了「进程号世界隔离」概念，本课回答「文件系统树怎么隔离」：mount namespace。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中与「文件系统/挂载」相关的设施（`ramfs_nodes`/`ramfs_lookup`、`vfs_init` 的 inode/dentry/file/fd 四表、`fd_open_model`/`fd_close_model`）按 mount namespace 主题系统化复述。下一课（Lesson 150）转向 network namespace。

**前置知识清单**（学本课前必须掌握）：
1. 进程 namespace 概念：nsproxy 挂载、PID 按世界隔离（Lesson 148）。
2. VFS 四表：`struct inode_model`（`ino,size,mode,refs`）、`struct dentry_model`（`name_hash,inode_index,refs`）、`struct file_model`、`struct fd_model` 与 `vfs_init`（Lesson 44s）。
3. ramfs/initramfs：`struct ramfs_node`（`name_hash,parent,inode,type,valid`）、`RAMFS_DIR`/`RAMFS_FILE`、`ramfs_lookup`（Lesson 44s/45s）。
4. fd 生命周期：`fd_open_model`/`fd_close_model`/`fd_read_model`、`fdinfo`/`fdtest`（Lesson 44s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–148）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 149: mount namespace 隔离`；
- 新命令 `l149test` 输出 `l149test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `ramfsinfo`/`pathtest`/`fdinfo`/`shellrun` 继续展示文件系统树与路径解析元数据。

---

## 2. 核心概念精讲

### 2.1 mount namespace：每个容器看到不同的文件系统树

**直觉**：容器里 `mount /dev/sda1 /mnt`，宿主机的 `/mnt` 毫无变化；容器里 `ls /proc` 看到的是容器自己的视图。因为「哪些设备挂在哪里」这份**挂载表**被按世界隔离了——这就是 **mount namespace**。

**准确定义**：mount namespace（`CLONE_NEWNS`，最古老的 namespace）为进程提供独立的**挂载点视图**。Linux 中每个进程通过 `nsproxy->mnt_ns` 指向 `struct mnt_namespace`（`include/linux/mnt_namespace.h`），它持有该命名空间的挂载树根。`fork` 时若带 `CLONE_NEWNS`，`copy_mnt_ns()` 复制一份挂载表（初始是共享，挂载操作后分裂为副本）；否则父子共享同一挂载表。

### 2.2 为什么需要 mount namespace（动机）

1. **文件系统隔离**：挂载是全局共享资源，容器 A 挂载/卸载不应影响容器 B——独立挂载表是文件系统隔离的前提。
2. **安全**：隐藏宿主挂载（如 `/proc`、宿主根）避免容器读写敏感路径；`chroot` 不够（仍是同一 mount ns），namespace 才是硬隔离。
3. **可复制的视图**：每个容器从「自己的根」看世界（`/` 可以是各自不同的文件系统），配合 rootfs 实现镜像可移植。

### 2.3 Linux 中 mount namespace 的工作机制

- **数据结构**：`struct mnt_namespace`（`list` 挂载树、`root` 根挂载、`user_ns` 关联）在 `include/linux/mnt_namespace.h`；每个挂载点对应 `struct mount`（`mnt_mountpoint`、`mnt_parent`、`mnt_child` 链表）在 `fs/mount.h`。
- **复制与传播**：`fs/namespace.c` 的 `copy_mnt_ns()`（fork 时按 `CLONE_NEWNS` 决定复制/共享）、`attach_mnt()`（挂载）、`propagate_mnt()`（挂载事件沿传播组扩散）、`umount_tree()`（卸载）。传播选项（`MS_SHARED`/`MS_SLAVE`/`MS_PRIVATE`）决定一个 namespace 的挂载/卸载是否影响同传播组的兄弟。
- **路径解析**：`fs/dcache.c` 的 `path_walk`/`walk_component` 沿 dentry 树解析绝对路径；每次 `open` 都从当前 namespace 的根开始。
- **教学简化**：教学内核没有 `struct mnt_namespace`/`struct mount`，只有一份定长 `ramfs_nodes` 树（5 个节点：`/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh`）与 inode/dentry 四表——这相当于「只有一个根挂载表、无 namespace 副本」的特例，所有进程共享同一文件系统视图。

### 2.4 教学内核中与「mount namespace」有关的既有设施

本课主题机制（mount namespace）**未在源码中实现**，但「文件系统树」这个主题素材在内核里完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| `ramfs_nodes` | `struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };` + `RAMFS_MAX 6U` | 内存文件系统节点树（对照 `struct mount` 挂载树） |
| `ramfs_lookup` | 逐串匹配 `/` `/etc` `/etc/motd` `/bin` `/bin/sh` | 路径解析（对照 `fs/dcache.c` 的 `path_walk`） |
| `RAMFS_DIR`/`RAMFS_FILE` | `#define RAMFS_DIR 1U`、`#define RAMFS_FILE 2U` | 目录/文件类型位（对照 `DT_DIR`/`DT_REG`） |
| `vfs_init` | 初始化 inode/dentry/file/fd 四表 | 挂载树的底层对象表（对照 VFS 超级块/索引节点） |
| `fd_open_model` | 在 file 表中登记、递增 inode `refs` | 打开即引用挂载点（对照 `get_mount`/`mntget`） |
| `pathtest` | 断言 5 条合法路径命中、3 条非法路径失败 | 路径解析正确性的可执行断言 |
| `shell_exec_path` | `ramfs_lookup("/bin/sh")` + `fd_open_model` | 执行程序 = 从挂载树解析路径 + 打开文件 |

### 2.5 检查点模型：lesson_142_model 与 l149test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `142→145` 标记 Origin 为 Lesson 142（`a=142,b=143,c=144,d=145`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「挂载计数连续性」。本课同时把上一课新增的 `l148test` 恢复为历史命名 `l141test`（挂 `lesson_141_state`，计数 `141→144`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-148 曾用 `l148test` 名字挂 141 号模型，本课将其名实重新对齐）。

### 2.6 机制继承 + 检查点增量

本课主题机制（mount namespace 隔离）**不是本课新写的代码**：ramfs 节点树、VFS 四表、路径解析全部来自文件系统阶段（Lesson 44s–45s）。本课实际增量只有三处：`l148test`→`l141test` 更名、`lesson_142_model`+`l149test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「挂载树 + 路径解析」主题重新组织，并如实说明：**mount namespace 对象（`struct mnt_namespace`/`struct mount` 等价物）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l148test`→`l141test` 恢复命名；新增 `lesson_142_model`/`lesson_142_state`/`l149test`；`about` 与开机横幅更新。mount namespace 主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`mount namespace 隔离`/`l149test`/`Lesson 149`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（文件系统树机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l149test(u16*c){lesson_142_state=(struct lesson_142_model){142U,143U,144U,145U,1,1,1,1};int ok=lesson_142_state.valid&&lesson_142_state.active&&lesson_142_state.ready&&lesson_142_state.accounted&&lesson_142_state.b==lesson_142_state.a+1U;text64(c,"l149test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 142 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `142→145`（Origin Lesson 142），四布尔位全置 1，`b==a+1U` 校验计数连续——「挂载树按顺序演进」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_142_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `143==142+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 142 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l149test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 142 fallback reported`。
4. **恢复的 `l141test`**：本课同时把上一课的 `l148test` 更名回 `l141test`（同为 `lesson_141_state`，计数 `141→144`），使检查点命令名与 Origin 对齐；`l100test`–`l140test` 历史检查点全部保留。

#### 3.2.2 挂载树：ramfs_nodes / ramfs_init（mnt_namespace 的教学形态）

```c
#define RAMFS_MAX 6U
#define RAMFS_ROOT 0U
#define RAMFS_DIR 1U
#define RAMFS_FILE 2U
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static struct ramfs_node ramfs_nodes[RAMFS_MAX];
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```

1. **节点字段（≥3 行）**：`name_hash` 存目录名哈希（`0x657463`=`etc`、`0x6d6f7464`=`motd`、`0x6e6962`=`bin`、`0x6873`=`sh`），`parent` 指向父节点，`inode` 指向 `inode_table` 下标，`type` 用 `RAMFS_DIR/RAMFS_FILE` 区分目录与文件——这是「挂载树 + 索引节点」的浓缩。
2. **树拓扑**：`nodes[0]=/`（根、parent=0）、`nodes[1]=/etc`（parent=0）、`nodes[2]=/etc/motd`（parent=1）、`nodes[3]=/bin`（parent=0）、`nodes[4]=/bin/sh`（parent=3）——一棵 5 节点目录树，对应「单 mount namespace 的文件系统视图」。
3. **固定容量**：`RAMFS_MAX 6U` 上限，`ramfs_init` 固化 5 个节点，容量不变式保证 freestanding 与确定性。

#### 3.2.3 路径解析：ramfs_lookup / pathtest（path_walk 的教学原型）

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
```

1. **入口守卫（≥3 行）**：`!path` 或 `path[0]!='/'` 直接返回 -1——路径必须绝对路径，且空指针视为失败（对照 Linux `path_init` 的 `AT_FDCWD`/绝对路径判定）。
2. **逐串匹配**：对 `/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh` 逐一精确 `eq64` 匹配，命中即 `hits++` 并返回节点下标；未命中 `misses++` 返回 -1——教学内核用「字符串表匹配」替代真实 dentry 树的逐段 `walk_component`。
3. **统计**：`lookups/hits/misses` 三计数器由 `ramfsinfo` 打印，路径解析行为的可观测性。
4. **观察命令**：`pathtest` 断言 5 条合法路径命中（`/`=0、`/etc`=1、`/etc/motd`=2、`/bin/sh`=4）、3 条非法路径失败（`/etc/missing`、`relative`、`/etc/motd/child`），输出 `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。

#### 3.2.4 VFS 底层对象表：vfs_init / inode / dentry（超级块/挂载点的教学形态）

```c
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```

1. **四表分层（≥3 行）**：`inode_table`（文件元数据：`ino,size,mode 0100644,refs`）、`dentry_table`（目录项：`name_hash,inode_index,refs`）、`file_table`（打开文件：`inode_index,offset,flags,refs`）、`fd_table`（进程 fd 号 → file 下标）——完整复刻 Linux VFS 的「inode/dentry/file/fd」四层抽象。
2. **refs 引用链**：inode 和 dentry 各带 `refs`，`fd_open_model` 打开时递增 inode `refs`、`fd_close_model` 关闭时递减——对照挂载点的 `mntget/mntput` 引用计数。
3. **执行路径**：`shell_exec_path` 先 `ramfs_lookup("/bin/sh")`（解析路径）再 `fd_open_model`（打开文件）——「从挂载树解析 + 打开」正是 exec 的文件系统前置步骤，`shellrun` 输出 `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。

#### 3.2.5 观察命令：ramfsinfo / fdinfo

- `ramfsinfo` 输出 `ramfs/initramfs nodes: ... paths: / /etc /etc/motd /bin /bin/sh` 与每个 `node i parent p inode n dir/file`；
- `fdinfo` 打印 `fd/file/inode/dentry tables (bounded)` 与 `opens/closes/reads/seeks` 计数。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 149: mount namespace 隔离\n`；检查点分支：
```c
else if(eq64(word,"l141test")){if(!noargs64(arg))usage64(c,"l141test");else l141test(c);}else if(eq64(word,"l149test")){if(!noargs64(arg))usage64(c,"l149test");else l149test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 149: mount namespace 隔离\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `mount namespace 隔离`、`Lesson 149`，kernel64.c 含 `l149test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init / reclaim_init / vfs_init（初始化 inode/dentry/file/fd + ramfs 树）
 ├─ 横幅 "Lesson 149: mount namespace 隔离"
 └─ 主循环：命令 → exec64
     ├─ l149test / l141test → 阶段检查点（lesson_142_state / lesson_141_state）
     ├─ pathtest → 路径解析断言（ramfs_lookup 命中/未命中）
     ├─ ramfsinfo → 挂载树节点与 lookups/hits/misses
     ├─ fdinfo / fdtest → VFS 四表与引用计数
     ├─ shellrun / execpath → 从挂载树解析 /bin/sh 并打开
     └─ about → "Lesson 149: mount namespace 隔离"
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，`vfs_init` 建立 inode/dentry/file/fd 四表与 ramfs 树，打印横幅 `Lesson 149: mount namespace 隔离`。
2. **`l149test`** → `l149test(c)` → 初始化 `lesson_142_state` → 五条件断言 → `l149test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l141test`**（恢复名） → `l141test(c)` → `lesson_141_state` 断言 → `l141test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`pathtest`** → 8 次 `ramfs_lookup` → 5 命中 3 未命中 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
5. **`ramfsinfo`** → `ramfs/initramfs nodes: 5 root=dentry0 memory-backed` + 每节点行。
6. **`shellrun`** → `ramfs_lookup("/bin/sh")` → `fd_open_model` → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
7. **`about`** → `Lesson 149: mount namespace 隔离`。

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
Multiboot2 and Lesson 149 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 149: mount namespace 隔离` 横幅 |
| `l149test` | `l149test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l141test` | `l141test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `ramfsinfo` | `ramfs/initramfs nodes: 5 root=dentry0 memory-backed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 149: mount namespace 隔离` |

判定成功：`l149test` 输出 passed、无 fallback，`pathtest` 输出 passed、无 `BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l149test` 输出 `Lesson 142 fallback reported` | `lesson_142_state` 初始化/断言失败（stale 镜像） | `grep -n "l149test" kernel64.c`；确认初始化串 `{142U,143U,144U,145U,1,1,1,1}` 与 `b==a+1U` |
| `pathtest` 输出 `BROKEN` | `ramfs_lookup` 命中/未命中计数与预期不符 | 对照 `ramfs_lookup` 的 8 个分支：`/`=0、`/etc`=1、`/etc/motd`=2、`/bin`=3、`/bin/sh`=4，其余 -1 |
| `ramfsinfo` 显示 nodes 数非 5 | `ramfs_init` 未执行或节点被破坏 | 确认 `vfs_init` 调用了 `ramfs_init`；`ramfs_nodes[0..4]` 的 `type/parent/inode` 字段 |
| `fdtest` 的 opens/closes 非 2 | `fd_open_model`/`fd_close_model` 引用计数错误 | 对照 `fd_open_model` 的空槽查找与 inode `refs++`；`fd_close_model` 的 `refs--` 与表清除 |
| `shellrun` 显示 BROKEN | `/bin/sh` 路径解析失败或 fd 打开失败 | 先 `pathtest` 确认 `ramfs_lookup("/bin/sh")==4`；再确认 `fd_open_model(4,0)>=0` |
| `l149test` 与 `l141test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l149test` 只操作 `lesson_142_state`、`l141test` 只操作 `lesson_141_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 149' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `mount namespace 隔离` 与 `Lesson 149` |
| 输入 `l149test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l149test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `ramfs_nodes[5]` 目录树 | `fs/namespace.c` 的 `struct mount` 挂载树；`include/linux/mnt_namespace.h` 的 `struct mnt_namespace` | 模型只有一棵静态树、无 `struct mount`/挂载传播 |
| `ramfs_lookup` 逐串匹配 | `fs/dcache.c` 的 `path_walk`/`walk_component` | 模型无 dentry 哈希缓存、无逐段路径游标，只有整串 `eq64` |
| `RAMFS_DIR/RAMFS_FILE` | `include/uapi/linux/dirent.h` 的 `DT_DIR`/`DT_REG` | 模型只有两种类型位，无符号链接/设备文件 |
| `vfs_init` 四表（inode/dentry/file/fd） | `fs/inode.c`/`fs/dcache.c`/`fs/file_table.c`/`fs/file.c` | 模型每表固定容量（3/3/3/4），无分配器与 LRU |
| `fd_open_model` 递增 inode `refs` | `fs/namespace.c` 的 `mntget()`/`mntput()`；`fs/file.c` `alloc_fd` | 模型只做元数据引用计数，不真实占用挂载 |
| `shell_exec_path` 解析 `/bin/sh` 后打开 | `fs/exec.c` `do_open_execat()`：`getname`+`open_exec` | 模型不装载可执行文件，只校验路径与 fd |
| mount namespace 隔离（主题概念） | `fs/namespace.c` `copy_mnt_ns()`/`clone_mnt`；`kernel/nsproxy.c` | 教学内核**没有** `mnt_namespace`，所有进程共享一份挂载表 |
| `l149test` 断言 | 无直接对应（LTP `mount_namespace`/container 测试） | 模型把 mount namespace 主题的可验证状态固化进内核 |

**权威来源**：Linux `fs/namespace.c`、`include/linux/mnt_namespace.h`、`fs/dcache.c`、`include/linux/nsproxy.h` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct mnt_namespace`/`struct mount` 或 `copy_mnt_ns` 的等价实现——mount namespace 隔离是「主题宣告」，教学内核停留在「单挂载表、所有进程共享文件系统视图」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么「chroot 不够、mount namespace 才行」？挂载表复制与共享（`CLONE_NEWNS`）对容器隔离分别意味着什么？
2. **源码定位**：在 `kernel64.c` 中找出所有「解析路径 / 打开文件」的代码位置（提示：`ramfs_lookup`、`fd_open_model`、`shell_exec_path`），并说明每个 `/bin/sh` 解析的返回值。
3. **动手实验**：在 `ramfs_nodes` 中新增一个节点 `/etc/hostname`（`parent=1`、`RAMFS_FILE`），并在 `ramfs_lookup` 增加匹配分支，运行 `pathtest` 验证命中；随后把 `pathtest` 的断言也更新。
4. **动手实验**：给 `ramfs_lookup` 增加「按 parent 逐级下钻」的解析（从 `/` 出发按 `parent` 字段走树），对照现在「整串匹配」实现，比较两者的复杂度与确定性。
5. **Linux 对照**：阅读 `fs/namespace.c` 的 `copy_mnt_ns`，说明 `CLONE_NEWNS` 时为什么要复制挂载树、不复制时为什么父子共享；对照教学模型「单一共享挂载表」的简化。

---

## 9. 本课小结与下一课预告

1. mount namespace 让每个容器看到独立的文件系统树：`CLONE_NEWNS` + `struct mnt_namespace`（`fs/namespace.c`、`include/linux/mnt_namespace.h`）。
2. 挂载传播（`MS_SHARED/SLAVE/PRIVATE`）决定挂载事件是否跨 namespace 扩散，路径解析沿 dentry 树完成。
3. 教学内核没有 mount namespace 对象，但文件系统树素材完整：`ramfs_nodes` 节点树、`ramfs_lookup` 路径解析、`vfs_init` 四表、`fd_open_model` 引用计数。
4. `pathtest` 的 5 命中/3 未命中断言是「挂载树路径解析正确性」的教学对应。
5. 检查点增量：`l148test`→`l141test` 更名、新增 `lesson_142_model`+`l149test`、横幅与 `about` 更新为 `Lesson 149: mount namespace 隔离`。
6. 下一课（Lesson 150）主题为 **network namespace**（对照 `net/core/net_namespace.c`、`include/net/net_namespace.h`）：namespace 三连的最后一块拼图，教学内核将以 `pipe_poll`/等待队列的「连接/就绪」设施承接该主题。
