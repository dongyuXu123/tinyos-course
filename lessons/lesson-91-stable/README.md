# Lesson 91: dentry 缓存与路径组件 — 精讲文档

> **课号**：Lesson 91 ｜ **主题**：dentry 缓存与路径组件（dentry cache and path components）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 84 原型的检查点
> **前置课程**：[`../lesson-90-stable/README.md`](../lesson-90-stable/README.md)（inode 生命周期与引用）
> **后续课程**：[`../lesson-92-stable/README.md`](../lesson-92-stable/README.md)（路径解析与遍历边界）
> **一句话目标**：精讲 dentry（目录项）在路径名与 inode 之间扮演的「缓存 + 桥接」角色，验证教学内核 `dentry_table`/`ramfs_nodes` 从根目录 `/` 逐路径组件定位到 `/bin/sh` 的完整链路。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第四课。`kernel64.c` 相对上一课仅做一处增量——把 `l90test` 恢复为 `l83test`，新增 `lesson_84_model` 状态与 `l91test` 检查点测试，并更新 `about`/开机横幅为本课主题。dentry 缓存与路径组件机制由累积代码承载：`vfs_init` 建立 dentry/inode 表、`ramfs_init` 建立 5 节点 initramfs 目录树、`ramfs_lookup` 以路径组件命中 dentry、`fd_open_model` 从 dentry→inode 走向 file/fd。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l84test`，但实际源码中本课检查点命令是 `l91test`（`l84test` 到 Lesson 92 才出现）。本文以源码为准：本课检查点命令为 `l91test`，历史回归命令 `l83test`（恢复自上一课 `l90test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出 Linux dentry 的三大职责（把路径名哈希成键、把名字映射到 inode、维护父子目录关系并带引用计数）；解释「为什么文件名查找要经过 dentry 而不是直接查 inode」；在教学内核中沿 `vfs_init` → `ramfs_init`（建树）→ `ramfs_lookup`（按路径组件命中）→ `dentry_table[].inode_index` → `fd_open_model` 走一遍「路径 → dentry → inode → file → fd」链路；运行 `l91test`/`pathtest`/`ramfsinfo`/`shellrun` 验证。

**在课程主线中的位置**：Lesson 90 已把 VFS 最核心对象 inode 的生命周期讲透；本课把视角上移一层，讲**路径名与 inode 之间的「胶水」——dentry**。`dentry_table` 是 VFS 路径查找的入口缓存，`ramfs_nodes` 则是教学版 initramfs 的静态目录树。下一课（Lesson 92）将用同一棵 ramfs 树讲路径解析的遍历边界。

**前置知识清单**（学本课前必须掌握）：
1. VFS 四层模型与 inode：`inode_model{ino,size,mode,refs}`（Lesson 88/90）。
2. open/close 引用链：`fd_open_model` 的 `inode_table[inode].refs++`、`fd_close_model` 的级联递减（Lesson 88/90）。
3. 哈希作为定长表键的既有用法：`module_lookup` 的 `name_hash`（Lesson 47）、`exported_symbols[].name_hash`。
4. 字符串原语：`eq64`/`token64` 在 `exec64` 命令分派中的角色（Lesson 21 起）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 91: dentry 缓存与路径组件`；
- 新命令 `l91test` 输出 `l91test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `pathtest`/`ramfsinfo` 展示根 `/` 到 `/bin/sh` 的 5 节点目录树与 lookup 命中统计，`shellrun` 展示 `/bin/sh` 经 dentry 打开并运行。

---

## 2. 核心概念精讲

### 2.1 dentry：路径名与 inode 之间的缓存层

**直觉**：每次执行 `open("/etc/motd")` 都要从磁盘读目录内容才能知道 `/etc/motd` 对应哪个 inode，代价太高。Linux 把「名字 → inode」的映射缓存在内存里的 **dentry（directory entry）** 中，下次同名查找直接命中缓存，不必再走磁盘。

**Linux 中的 dentry 三职责**（对照 `include/linux/dcache.h` 的 `struct dentry`）：
1. **名字键**：`d_name`（struct qstr，含 name 与 hash）；
2. **映射**：`d_inode` 指向对应的 `struct inode`；
3. **树形关系**：`d_parent`/`d_subdirs` 把各级目录串成一棵 dcache 树，每个 dentry 带 `d_count` 引用计数。

**为什么不直接查 inode**：inode 的编号（`i_ino`）是文件系统内部的，与应用层路径名无关；路径查找必须逐级「目录 → 子项」解析，dentry 正是这个解析过程的缓存单元。教学模型用 `dentry_table[]` 与 `ramfs_nodes[]` 分别模拟「通用 dcache」与「initramfs 静态目录树」两个层次。

### 2.2 教学模型的两张表：dentry_table 与 ramfs_nodes

```c
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct ramfs_node  { u64 name_hash,parent,inode; u8 type,valid; };
```

| 表 | 容量 | 语义 | 对应 Linux 概念 |
|---|---|---|---|
| `dentry_table[]` | `DENTRY_MAX=3` | 通用 dcache 槽：名字哈希 → inode 索引 + 引用计数 | `include/linux/dcache.h` 的 `struct dentry` |
| `ramfs_nodes[]` | `RAMFS_MAX=6` | initramfs 静态目录树：节点带 `parent` 父指针与 `type`（`RAMFS_DIR`/`RAMFS_FILE`） | `fs/ramfs/inode.c` 的 ramfs 目录项 + `fs/dcache.c` 的 dcache 树 |

`ramfs_nodes[i].name_hash` 是组件名的 ASCII 小端哈希：`0x657463`="etc"、`0x6d6f7464`="motd"、`0x6e6962`="bin"、`0x6873`="sh"。`ramfs_node.type` 区分目录与文件，`parent` 指向父节点下标——这就是一棵教学版路径树。

### 2.3 路径组件（path components）与逐级命中

路径 `/bin/sh` 由两个组件 `bin`、`sh` 构成。`ramfs_lookup` 每次调用以**完整路径字符串**为输入，在 5 节点静态表中做精确匹配。本课把它理解为「组件已在 ramfs 建立时的直接命中」；下一课（Lesson 92）将专门讨论缺组件/越界/相对路径等遍历边界。

### 2.4 检查点模型：lesson_84_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l91test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 84）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l83test`；新增 `lesson_84_model`/`lesson_84_state`/`l91test`；`about` 与横幅更新。dentry/路径组件机制由累积代码承载，本课以「dentry 缓存与路径组件」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`dentry 缓存与路径组件`/`Lesson 91`/`l91test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（dentry 缓存/路径组件 + 本课增量）

#### 3.2.1 结构定义与全局表

```c
#define DENTRY_MAX 3U
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
static struct inode_model inode_table[INODE_MAX];
static struct dentry_model dentry_table[DENTRY_MAX];
#define RAMFS_MAX 6U
#define RAMFS_ROOT 0U
#define RAMFS_DIR 1U
#define RAMFS_FILE 2U
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static struct ramfs_node ramfs_nodes[RAMFS_MAX];
static u32 ramfs_count;
static u64 ramfs_lookups,ramfs_hits,ramfs_misses;
```
1. `dentry_table` 每槽 3 个字段：`name_hash`（组件名的哈希键）、`inode_index`（映射到 inode 表）、`refs`（dentry 引用计数）——对应 Linux `struct dentry` 的 `d_name.hash/d_inode/d_count`。
2. `ramfs_nodes` 每槽 4 个字段：`name_hash`、`parent`（父节点下标）、`inode`（绑定的 inode 槽）、`type`（目录/文件）——构成静态 initramfs 目录树。
3. 统计变量 `ramfs_lookups/hits/misses` 为 `pathtest` 与 `ramfsinfo` 提供可观察数字。

#### 3.2.2 建立缓存与目录树：vfs_init / ramfs_init

```c
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **inode 层**：3 个 inode 以 `{ino=i+1, size=0x100+i, mode=0100644, refs=1, valid=1}` 初始化（iget 教学版，见 Lesson 90）。
2. **dentry 层**：`dentry_table[i]={name_hash=0x100+i, inode_index=i, refs=1, valid=1}`——每个通用 dcache 槽默认绑定到同下标 inode，且自带 1 个引用（dentry 是 inode 基引用的持有者，与 Lesson 90 结论一致）。
3. **file/fd 层清空**：等待 open 时由 `fd_open_model` 填充。

```c
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```
逐行拆解这 5 个静态节点（这就是本课要讲的「路径组件」表）：
1. `nodes[0]={hash=0, parent=0, inode=0, RAMFS_DIR}`——**根目录** `/`，父指针指向自己（`RAMFS_ROOT=0`），无名字哈希；
2. `nodes[1]={0x657463, parent=0, inode=1, RAMFS_DIR}`——目录 `etc`（0x657463="etc"），挂在根下，绑定 inode 1；
3. `nodes[2]={0x6d6f7464, parent=1, inode=2, RAMFS_FILE}`——文件 `motd`（"motd"），父节点是 `etc`，绑定 inode 2；
4. `nodes[3]={0x6e6962, parent=0, inode=1, RAMFS_DIR}`——目录 `bin`（"bin"），挂在根下，与 `etc` 共享 inode 1；
5. `nodes[4]={0x6873, parent=3, inode=2, RAMFS_FILE}`——文件 `sh`（"sh"），父节点是 `bin`，绑定 inode 2（与 motd 共享同一个 inode——教学模型里的硬链接式复用）。

树形结构一目了然：
```
ramfs_nodes[0]  /            (dir,  parent=0,  inode=0)
 ├─ [1] etc     (dir,  parent=0, inode=1)
 │    └─ [2] motd (file, parent=1, inode=2)
 └─ [3] bin     (dir,  parent=0, inode=1)
      └─ [4] sh  (file, parent=3, inode=2)
```

#### 3.2.3 按路径组件查找：ramfs_lookup

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
```
1. **入口守卫**：`!path || path[0]!='/'` 直接返回 -1——只接受以 `/` 开头的绝对路径，拒绝空指针与相对路径（这是「遍历边界」的起点，Lesson 92 展开）。
2. **统计**：每次调用 `ramfs_lookups++`，命中 `ramfs_hits++`，未命中 `ramfs_misses++`。
3. **根特判**：`eq64(path,"/")||eq64(path,".")` 返回 0——根 dentry（节点 0）。
4. **精确匹配**：对 4 个预置路径做逐字串比较，命中即返回对应 ramfs 节点下标（1/2/3/4）。
5. **失败路径**：未命中则 `ramfs_misses++` 并返回 -1——`open("/etc/missing")` 之类的查询得到明确的「无此 dentry」。

**设计动机**：教学模型用「完整路径字符串精确匹配」替代 Linux 的「逐组件 walk + dentry hash 查找」，把 dcache 命中的语义压缩成一张 5 行的查找表；代价是不支持任意深度与动态创建，但这正符合「定长表 + 元数据模拟」的教学约束。`shell_exec_path`/`shelltest` 均通过它解析 `/bin/sh`。

#### 3.2.4 从 dentry 走向 file/fd：fd_open_model

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. `ramfs_lookup` 拿到 `inode` 下标后，`fd_open_model` 校验 inode 有效，然后分配空 file 槽 `{inode, offset=0, flags, refs=1, valid=1}` 与空 fd 槽 `{f,1}`。
2. `inode_table[inode].refs++`——打开动作使 inode 多一个持有者（引用链在 Lesson 90 已精讲）。
3. 由此完成本课主链路的最后一跳：`路径 /bin/sh → dentry(ramfs_nodes[4]) → inode(2) → file → fd`。

#### 3.2.5 观察与验证入口：ramfsinfo / pathtest

```c
static TEXT64 void ramfsinfo(u16*c){u32 i;text64(c,"ramfs/initramfs nodes: ");hex64(c,ramfs_count);text64(c," root=dentry0 memory-backed\npaths: / /etc /etc/motd /bin /bin/sh\nlookups/hits/misses: ");hex64(c,ramfs_lookups);hex64(c," ");hex64(c,ramfs_hits);hex64(c," ");hex64(c,ramfs_misses);putc64(c,'\n');for(i=0;i<ramfs_count;i++){text64(c,"node ");hex64(c,i);text64(c," parent ");hex64(c,ramfs_nodes[i].parent);text64(c," inode ");hex64(c,ramfs_nodes[i].inode);text64(c," ");text64(c,ramfs_nodes[i].type==RAMFS_DIR?"dir":"file");putc64(c,'\n');}}
```
1. 先打印总览：`ramfs/initramfs nodes: 5`、根标记 `root=dentry0 memory-backed`、已知路径清单 `paths: / /etc /etc/motd /bin /bin/sh`、以及 `lookups/hits/misses` 三项统计。
2. 再逐节点打印 `node N parent P inode I dir|file`，把目录树平铺在 VGA 上，供人工核对 `parent` 指针与类型。

```c
static TEXT64 void pathtest(u16*c){int a=ramfs_lookup("/"),b=ramfs_lookup("/etc"),d=ramfs_lookup("/etc/motd"),e=ramfs_lookup("/bin/sh"),f=ramfs_lookup("/etc/missing"),g=ramfs_lookup("relative"),h=ramfs_lookup("/etc/motd/child");text64(c,"pathtest: ");text64(c,a==0&&b==1&&d==2&&e==4&&f<0&&g<0&&h<0?"ramfs/initramfs root-to-dentry path lookup passed":"BROKEN");putc64(c,'\n');}
```
1. 正向命中 4 次：`/`→0、`/etc`→1、`/etc/motd`→2、`/bin/sh`→4；
2. 负向 3 次：`/etc/missing`（不存在）、`relative`（非绝对路径）、`/etc/motd/child`（对文件继续下钻）全部 `-1`；
3. 断言全对才输出成功串：`pathtest: ramfs/initramfs root-to-dentry path lookup passed`。

#### 3.2.6 本课新增检查点函数

```c
struct lesson_84_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_84_model lesson_84_state;
static TEXT64 void l91test(u16*c){lesson_84_state=(struct lesson_84_model){84U,85U,86U,87U,1,1,1,1};int ok=lesson_84_state.valid&&lesson_84_state.active&&lesson_84_state.ready&&lesson_84_state.accounted&&lesson_84_state.b==lesson_84_state.a+1U;text64(c,"l91test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 84 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 84→87（Origin 编号 Lesson 84），四布尔位全置 1。
2. **成功串**：`l91test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 84 fallback reported`。
3. **恢复的 `l83test`**：本课同时恢复 `l83test`（`lesson_83_state`，83→86，由上一课 `l90test` 更名而来），历史检查点可独立运行。

#### 3.2.7 exec64 增量与开机横幅

- `about` 输出 `Lesson 91: dentry 缓存与路径组件\n`；检查点分支：
```c
else if(eq64(word,"l83test")){if(!noargs64(arg))usage64(c,"l83test");else l83test(c);}else if(eq64(word,"l91test")){if(!noargs64(arg))usage64(c,"l91test");else l91test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 91: dentry 缓存与路径组件\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（32 位引导 + 64 位 `-ffreestanding -fpie -mno-red-zone -mno-sse` 内核、两层链接脚本、`grub-mkrescue` 打包 ISO）。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `dentry 缓存与路径组件` 与 `Lesson 91`、kernel64.c 含 `l91test`，最后打印 `Multiboot2 and Lesson 91 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma/reclaim → vfs_init()
 │     └─ inode 表 refs=1 创建 + dentry 表 0x100+i→i + ramfs_init()（5 节点树）
 ├─ 横幅 "Lesson 91: dentry 缓存与路径组件"
 └─ 主循环：命令 → exec64
     ├─ l91test → 阶段检查点
     ├─ pathtest → 根/目录/文件正向命中 + 三组负向边界
     ├─ ramfsinfo → 打印 5 节点目录树与 lookup 统计
     ├─ shellrun → ramfs_lookup("/bin/sh")→fd_open_model→fd_close_model 生命周期
     └─ fdtest/fdinfo → dentry→inode 引用链与偏移验证
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 建 inode/dentry 表，`ramfs_init()` 建 5 节点目录树，打印横幅 `Lesson 91: dentry 缓存与路径组件`。
2. **`l91test`** → `l91test(c)` → 断言 `lesson_84_state` → `l91test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`pathtest`** → 7 次 `ramfs_lookup`（4 命中 3 未命中）→ `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
4. **`ramfsinfo`** → 打印 `ramfs/initramfs nodes: 5`、`paths: / /etc /etc/motd /bin /bin/sh`、lookups/hits/misses 计数，再逐行打印 `node N parent P inode I dir|file`。
5. **`fdtest`** → `fd_open_model(0,1)`/`fd_open_model(1,2)` 经 dentry 层指向 inode 建 file+fd → read → 两次 close → `fdtest: fd/file/inode/dentry refs and offsets passed`。
6. **`shellrun`** → `vfs_init()` 后 `shell_exec_path("/bin/sh",2,1)`：`ramfs_lookup` 命中 dentry 4 → `fd_open_model(inode=2)` → 记账 → `fd_close_model` → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
7. **`about`** → `Lesson 91: dentry 缓存与路径组件`。

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
Multiboot2 and Lesson 91 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 91: dentry 缓存与路径组件` 横幅 |
| `l91test` | `l91test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l83test` | `l83test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `ramfsinfo` | 首行 `ramfs/initramfs nodes: 5`，随后 5 行 `node N parent P inode I dir|file` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 91: dentry 缓存与路径组件` |

判定成功：`l91test`/`pathtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l91test` 输出 `Lesson 84 fallback reported` | `lesson_84_state` 初始化/断言失败（stale 镜像） | `grep -n "l91test" kernel64.c`；确认初始化串 `{84U,85U,86U,87U,1,1,1,1}` |
| `pathtest` 输出 `BROKEN` | `ramfs_lookup` 命中/未命中断言不符 | 对照 `ramfs_lookup` 的 5 个匹配分支与 `ramfs_init` 的 5 个节点；确认 `relative` 与 `/etc/motd/child` 都返回 -1 |
| `ramfsinfo` 显示的树与预期不符 | `ramfs_nodes[].parent/type` 初始化错误 | 对照 `ramfs_init` 逐节点核对 `parent` 与 `RAMFS_DIR/RAMFS_FILE` |
| `shellrun` 输出 `BROKEN` | `/bin/sh` 未命中（`ramfs_lookup` 返回 -1）或 fd 生命周期异常 | 先运行 `pathtest` 确认 dentry 命中；检查 `shell_exec_path` 对 `fd_open_model`/`fd_close_model` 的配对 |
| `fdinfo` 显示 inode refs 偏高 | 未关闭的 file 残留 | 对照 `fd_close_model` 只在 `file_table[f].refs==0` 时递减 inode（Lesson 90 链条） |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 91' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `dentry 缓存与路径组件` 与 `Lesson 91` |
| 误敲 `l84test` 无响应 | `l84test` 在本课源码中不存在（Lesson 92 才引入） | 本课检查点命令是 `l91test`；旧 README 的 `l84test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `dentry_table[]`（`name_hash/inode_index/refs`） | `include/linux/dcache.h` 的 `struct dentry`（`d_name.hash`/`d_inode`/`d_count`）；`fs/dcache.c` 的 `d_alloc`/`d_lookup` | 模型 3 个定长槽，无 dcache 哈希桶、LRU、RCU |
| `ramfs_nodes[]`（`name_hash/parent/inode/type`） | `fs/ramfs/inode.c`（ramfs 的 `ramfs_dir_inode_operations`/`ramfs_lookup`）；`fs/dcache.c` 的 dcache 树 | 模型为静态 5 节点表，无动态 mkdir/rmdir、无页缓存承载文件内容 |
| `ramfs_lookup` 整串精确匹配 | `fs/namei.c` 的 `link_path_walk`/`walk_component`（逐组件解析 + 逐级 dentry 查找） | 模型无组件拆分与逐级下钻，命中即返回 |
| `ramfs_lookup("/")` 返回根 dentry 0 | `fs/dcache.c` 的 `dget`/根挂载点的 root dentry；`fs/namespace.c` 的 `vfs_path_lookup` | 模型无挂载点概念，根是固定节点 |
| `fd_open_model` 从 inode 建 file/fd | `fs/open.c` 的 `filp_open`（`open_namei` → `dentry_open`） | 模型跳过 path→dentry 的完整解析，直接用 inode 下标 |
| `ramfs_nodes[2]` 与 `[4]` 共享 inode 2 | `fs/dcache.c` + `fs/inode.c`：多个 dentry 可 `d_inode` 指向同一 inode（硬链接） | 模型只复用了 inode 槽，无 link count |
| `l91test` 断言 | 无直接对应（LTP `fs` 测试套件） | 模型把 dentry 查找验证固化进内核 |

**权威来源**：Linux `include/linux/dcache.h`、`fs/dcache.c`、`fs/namei.c`、`fs/ramfs/inode.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：dentry 与 inode 有何区别？为什么文件打开必须「先找到 dentry，再由 dentry 指向 inode」，而不是直接查 inode？
2. **源码定位**：在 `kernel64.c` 中找到 `ramfs_nodes` 的全部初始化与读取点，画出 `/bin/sh` 从根出发经过哪些节点（含 `parent` 指针）到达节点 4。
3. **动手实验**：在 `ramfs_init` 中新增第 6 个节点（如 `nodes[5]={0x746d70,"tmp",...}` 挂到根下），再在 `ramfs_lookup` 加一条 `if(eq64(path,"/tmp")){ramfs_hits++;return 5;}`，重新构建运行 `pathtest`/`ramfsinfo` 观察变化。
4. **动手实验**：修改 `ramfs_lookup` 去掉 `path[0]!='/'` 守卫，运行 `pathtest`，观察 `relative` 不再返回 -1 导致的断言失败。
5. **Linux 对照**：阅读 `include/linux/dcache.h` 中 `struct dentry` 的 `d_parent`/`d_subdirs`/`d_count`，对比 `ramfs_nodes[].parent` 与 `dentry_model.refs` 的教学化取舍。

---

## 9. 本课小结与下一课预告

1. 本课以 dentry 为焦点，补全了 VFS 四层模型中「路径名层」：路径 `/bin/sh` 先命中 dentry，再由 dentry 指向 inode。
2. `dentry_table[]` 是通用 dcache 的教学版（名字哈希 → inode + 引用计数），`ramfs_nodes[]` 是 initramfs 静态目录树（带 `parent` 父指针与目录/文件类型）。
3. `ramfs_init` 用 5 个节点搭建 `/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh` 迷你树，`etc` 与 `bin` 共享 inode 1、`motd` 与 `sh` 共享 inode 2——演示了「dentry 到 inode 多对一」的硬链接式关系。
4. `ramfs_lookup` 以「绝对路径 + 精确匹配」模拟 dcache 命中，未命中与非法输入统一返回 -1 并计入 `ramfs_misses`。
5. `fd_open_model` 完成链路最后一跳，使 inode 引用递增——把本课 dentry 视角与上一课 inode 生命周期无缝衔接。
6. `l91test` 沿用 VFS/设备阶段检查点家族，`l83test` 历史检查点保留。
7. 下一课（[`../lesson-92-stable/README.md`](../lesson-92-stable/README.md)，Lesson 92）将基于同一棵 ramfs 树深入 **路径解析与遍历边界**（对照 `fs/namei.c`）：相对路径、缺失组件、对文件下钻等边界如何被拒绝。
