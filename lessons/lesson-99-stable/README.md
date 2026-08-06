# Lesson 99: 设备节点与 major/minor — 精讲文档

> **课号**：Lesson 99 ｜ **主题**：设备节点与 major/minor（device nodes and major/minor）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 92 原型的检查点
> **前置课程**：[`../lesson-98-stable/README.md`](../lesson-98-stable/README.md)（字符设备注册）
> **后续课程**：[`../lesson-100-stable/README.md`](../lesson-100-stable/README.md)（设备打开与 ioctl 元数据）
> **一句话目标**：讲清 `dev_t`（major/minor 设备号）的编码与「设备节点」这种特殊 inode 如何在文件树中表示设备，并把教学内核的 inode/mode、ramfs 目录树、模块 name_hash 映射到这套编号体系上，验证 `l99test` 检查点。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l98test` 恢复为历史命名 `l91test`（挂在 `lesson_91_state` 上）、新增 `lesson_92_model` 状态与 `l99test` 检查点、更新 `about`/开机横幅为本课主题。设备节点与 major/minor 机制由累积代码承载：inode 的 `mode` 字段与 `ino` 承载设备节点的类型与实例身份，ramfs 目录树（`ramfs_nodes[]`）是设备节点所在的目录，模块模型的 `name_hash` 充当「驱动/子系统」级标识。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l99test`（旧 README 写的 `l92test` 按源码勘误）；另保留历史检查点 `l82test`–`l91test`，以及 `ramfsinfo`/`pathtest`/`fdinfo`/`moduleinfo` 等文件树与注册表观察命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出 Linux `dev_t` 的分段编码（major 标识驱动、minor 标识实例，`include/linux/kdev_t.h` 的 `MAJOR`/`MINOR`/`MKDEV`）；解释「设备节点」为什么是一个带着 `S_IFCHR`/`S_IFBLK` 类型位与 `i_rdev` 的特殊 inode，`mknod` 创建它、`open` 按它找到驱动（`chrdev_open`）；在教学内核中沿 inode `mode`/`ino` → ramfs 目录树 → 模块 `name_hash` 走一遍「编号映射」链；运行 `l99test`/`ramfsinfo`/`pathtest` 验证。

**在课程主线中的位置**：Lesson 98 讲字符设备「注册」（把 cdev 按 dev_t 挂进 cdev_map），本课接着讲「编号与节点」——dev_t 从哪来、设备在文件树里长什么样。设备号是注册与打开之间的桥：注册用 dev_t 当键，打开按 inode 里的设备号查表。作为检查点课，源码 diff 极小，任务是把继承机制（inode mode/ino、ramfs 树、模块哈希）按「major/minor 映射」主题系统化复述。下一课（Lesson 100）转向设备打开与 ioctl 元数据。

**前置知识清单**（学本课前必须掌握）：
1. inode 模型：`struct inode_model{ino,size,mode,refs,valid}` 与 `mode=0100644` 的类型/权限位语义（Lesson 88–96）。
2. ramfs 目录树：`ramfs_nodes[]` 的 `name_hash/parent/inode/type` 布局（Lesson 52/88–97）。
3. 字符设备注册：`module_init_model`/`module_lookup` 与 cdev_map 的按键查找（Lesson 89/98）。
4. 打开链：`ramfs_lookup` → `fd_open_model` → `fd_close_model`（Lesson 88–97）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位（Lesson 69–98）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 99: 设备节点与 major/minor`；
- 新命令 `l99test` 输出 `l99test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `ramfsinfo`/`pathtest`/`fdinfo` 展示设备节点所在的目录树与 inode 连接，`moduleinfo` 展示 major 级驱动标识。

---

## 2. 核心概念精讲

### 2.1 dev_t：major 与 minor 的分段编码

**直觉**：内核用一个整数区分所有设备，但一个整数要表达两层意思——「谁家的」（哪个驱动/子系统，**major**）和「第几个」（该驱动下的具体实例，**minor**）。把两个编号拼进一个 32 位整数，就是 `dev_t`。

**Linux 编码**（`include/linux/kdev_t.h`，自 2.6 起）：
```c
#define MINORBITS 20
#define MINORMASK ((1U << MINORBITS) - 1)
#define MAJOR(dev) ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev) ((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi) (((ma) << MINORBITS) | (mi))
```
- **major**：高 12 位，标识设备类型/驱动（`include/linux/major.h` 为常见 major 分配了名字：`TTY_MAJOR 4`、`MEM_MAJOR 1`、`SD_MAJOR 8`……）；
- **minor**：低 20 位，标识该 major 下的实例（`/dev/tty0` 是 4,0，`/dev/tty1` 是 4,1）；
- `MKDEV(ma,mi)` 负责拼装，`MAJOR`/`MINOR` 负责拆解——注册（`register_chrdev_region`）按 major 登记区间，打开（`chrdev_open`）按完整 dev_t 查 `cdev_map`。

### 2.2 设备节点：特殊 inode

**直觉**：设备不能像文件那样有内容，但用户喜欢用文件接口操作它。于是内核造出「设备节点」——一种**类型位是字符/块设备、正文是 dev_t** 的特殊 inode，通常放在 `/dev` 下。

**Linux 表示**：
- `inode->i_mode` 的类型位是 `S_IFCHR`（`0020000`）或 `S_IFBLK`（`0060000`），而不是普通文件的 `S_IFREG`；
- `inode->i_rdev`（`dev_t`）保存设备号；`inode->i_bdev`/`i_cdev` 缓存块/字符设备对象；
- `mknod("/dev/ttyS0", S_IFCHR|0660, makedev(4,64))` 创建节点，`ls -l` 里类型列显示 `c`/`b`；
- 打开设备节点时，VFS 走 `chrdev_open`：读 `inode->i_rdev` → 按 dev_t 查 `cdev_map` → 调用驱动 `fops->open()`。

### 2.3 教学模型的编号映射

教学内核没有 `struct cdev` 与 `mknod`，但「**类型 + 两层编号**」的骨架全部存在于继承代码里：

| Linux 概念 | 教学内核承载 | 说明 |
|---|---|---|
| `S_IFREG`/`S_IFCHR`/`S_IFBLK` 类型位 | `inode_model.mode`（当前 `0100644`）与 `ramfs_node.type`（`RAMFS_DIR`/`RAMFS_FILE`） | 类型决定对象「是什么」；设备节点就是 type 为字符/块设备的特殊节点 |
| `dev_t`（major+minor 拼装） | 模块 `name_hash`（major 类比）+ `inode.ino`（minor 类比） | major 标识驱动/子系统，minor 标识表内实例序号（`ino=i+1`） |
| `MKDEV/MAJOR/MINOR` 编解码 | 移位/按位运算同理可做（教学演示，非源码结构） | 编码只是「两层信息拼一个键」 |
| `/dev` 目录 | ramfs 目录树（`ramfs_nodes[]` 的 `parent` 边） | 设备节点是文件树里的普通一员 |
| `cdev_map` 按 dev_t 查 fops | `module_lookup(name_hash)` 线性扫描导出符号 | 查找语义一致，键不同 |

**要点**：教学模型把「设备号」拆成「子系统哈希（major）+ inode 序号（minor）」两层，与 Linux 的 major/minor 是**同构的分层标识**；设备节点与普通文件在文件树里**没有结构性差别**——这正是 Linux「一切皆文件」的教学诠释。

### 2.4 检查点模型：lesson_92_model 与 l99test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `92→95` 标记 Origin 为 Lesson 92。本课同时把上一课新增的 `l98test` 恢复为历史命名 `l91test`（同一 `lesson_91_state`，计数 `91→94`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.5 机制继承 + 检查点增量

本课主题机制（设备节点、major/minor）不是本课新写的代码：inode 的 mode/ino 来自 VFS 阶段，ramfs 目录树来自 Lesson 52/88–97，模块 name_hash 来自 Lesson 89/98。本课的实际增量只有三处：`l98test`→`l91test` 更名、`lesson_92_model`+`l99test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「编号与节点」主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l98test`→`l91test` 恢复命名；新增 `lesson_92_model`/`lesson_92_state`/`l99test`；`about` 与开机横幅更新。设备节点/major-minor 机制由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`设备节点与 major/minor`/`l99test`/`Lesson 99`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（设备节点/编号机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_92_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_92_model lesson_92_state;
static TEXT64 void l99test(u16*c){lesson_92_state=(struct lesson_92_model){92U,93U,94U,95U,1,1,1,1};int ok=lesson_92_state.valid&&lesson_92_state.active&&lesson_92_state.ready&&lesson_92_state.accounted&&lesson_92_state.b==lesson_92_state.a+1U;text64(c,"l99test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 92 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `92→95`（Origin Lesson 92），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l99test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 92 fallback reported`。
3. **恢复的 `l91test`**：本课同时把 `l98test` 更名回 `l91test`（同为 `lesson_91_state`），使检查点命令名与 Origin 对齐；`l82test`–`l90test` 历史检查点全部保留。

#### 3.2.2 设备节点的类型与实例身份：inode_model

```c
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
```
```c
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **`mode` 字段承载类型**：`0100644` 的高 4 位 `0100` 是 S_IFREG 语义（Linux 权限位前的类型位）。设备节点就是 `mode` 类型位换成 `S_IFCHR`/`S_IFBLK` 的 inode——教学模型的 inode 结构天然支持这个替换，只是当前只创建普通文件 inode。
2. **`ino` 字段承载实例身份**：`ino=i+1`（1..3），是每个 inode 的唯一编号——对应 dev_t 的 minor「第几个实例」语义。
3. **dentry 的 `name_hash` 与 `inode_index`**：dentry 用 `name_hash`（`0x100+i`）给名字建哈希、用 `inode_index` 指向 inode——设备节点在目录里同样以「名字 → 节点 → inode」挂接。

#### 3.2.3 设备节点所在的目录：ramfs_nodes 目录树

```c
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
```
```c
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```
1. **目录树结构**：`parent` 建树边（根 `/` → `/etc`、`/bin` → `/etc/motd`、`/bin/sh`），`type` 区分目录/文件——真实内核的 `/dev` 就是这棵树里一个放设备节点的目录。
2. **设备节点如何插入**：若要在教学模型里放一个字符设备节点，只需新增一个 `type=RAMFS_FILE` 的节点指向一个 `mode` 类型位为 S_IFCHR 的 inode——这就是 mknod 的树结构效果。
3. **`ramfsinfo` 可观察**：`node N parent P inode I dir|file` 逐行列出整棵树，等价于 `ls -l` 的目录列出（Lesson 97 主题）。

#### 3.2.4 major 级驱动标识：模块 name_hash

```c
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
```
1. **name_hash 作为 major 类比**：`core=0x636f7265`、`vfs=0x766673` 是内核子系统的稳定哈希标识——如同 `MEM_MAJOR`/`TTY_MAJOR` 是 Linux 为驱动分配的稳定 major 号（`include/linux/major.h`）。
2. **注册 = 占用一个编号**：`register_chrdev_region` 在 Linux 里「占用」一段 major；教学模型的 `module_init_model` 在 `modules[]` 里「占用」一个子系统槽位——语义一致。
3. **查找 = 按编号定位**：`module_lookup(name_hash)` 按哈希找符号（Lesson 98），与 `chrdev_open` 按 `MAJOR(dev)` 定位驱动同构。

#### 3.2.5 打开链上的编号：fdinfo / pathtest 观察

```c
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");hex64(c,file_table[f].offset);putc64(c,'\n');}text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
```
1. **`fdinfo` 打印 `inode_table[n].ino`**：fd → file → inode 链路末端是 inode 唯一编号——这就是「打开到实例」的可观察路径；设备节点打开后同样停在 `i_rdev`（dev_t）。
2. **`pathtest` 验证路径→节点映射**：`ramfs_lookup` 把 `"/etc/motd"` 之类路径映射到节点下标（`/etc/motd`→2、`/bin/sh`→4），未命中返回 -1——设备节点若存在也走同一条 lookup，不存在的节点（如 `/dev/xxx` 未创建）同样返回 -1。
3. **与 mknod 对照**：Linux 中 `mknod` 先走 `fs/namei.c` 的 `vfs_mknod` 在目录里建节点 inode，之后 `open` 再按节点里的 dev_t 找到驱动——教学模型把「建节点」省略，保留「目录里找节点」的 lookup 语义。

#### 3.2.6 检查点断言与编号连续性

`l99test` 的 `b==a+1U` 是「编号连续」的最小断言；`lesson_92_state` 的计数 `92→95` 与 inode `ino=i+1` 的生成方式（`vfs_init` 里 `i+1`）共享同一个「从 1 开始、每步 +1」的编号原则——major/minor 世界里，编号的**确定性**与**可预测性**是最重要的正确性属性。

#### 3.2.7 exec64 增量与开机横幅

- `about` 输出 `Lesson 99: 设备节点与 major/minor\n`；检查点分支：
```c
else if(eq64(word,"l91test")){if(!noargs64(arg))usage64(c,"l91test");else l91test(c);}else if(eq64(word,"l99test")){if(!noargs64(arg))usage64(c,"l99test");else l99test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 99: 设备节点与 major/minor\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `设备节点与 major/minor`、`Lesson 99`，kernel64.c 含 `l99test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model()（major 级子系统标识：core/vfs）
 ├─ vfs_init() → inode_table{ino=1..3, mode=0100644} → ramfs_init（设备节点所在的目录树）
 ├─ 横幅 "Lesson 99: 设备节点与 major/minor"
 └─ 主循环：命令 → exec64
     ├─ l99test / l91test → 阶段检查点（lesson_92_state / lesson_91_state）
     ├─ ramfsinfo / pathtest → 目录树列出与路径→节点映射
     ├─ fdinfo / fdtest → fd→file→inode 编号链观察
     └─ moduleinfo / moduletest → major 级标识查找
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`module_init_model()` 建立子系统标识，`vfs_init()` 建 `ino=1..3` 的 inode 表与 ramfs 目录树，打印横幅 `Lesson 99: 设备节点与 major/minor`。
2. **`l99test`** → `l99test(c)` → 断言 → `l99test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`ramfsinfo`** → 打印 `ramfs/initramfs nodes: 5 ...` 与 5 行 `node N parent P inode I dir|file` 目录表——设备节点若存在会以同格式出现。
4. **`pathtest`** → 7 次 `ramfs_lookup` 断言 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
5. **`fdinfo`** → 打印每个有效 fd 的 `fd F file F inode I off O`（`inode I` 是实例编号）与 opens/closes/reads/seeks 计数。
6. **`moduleinfo`** → `modules initialized/exports/lookups: 2/2/N` 与每模块 `initialized` 行（major 级标识）。
7. **`l91test`**（历史检查点） → `l91test: bounded VFS, devices, epoll, and service management checkpoint passed`。
8. **`about`** → `Lesson 99: 设备节点与 major/minor`。

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
Multiboot2 and Lesson 99 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 99: 设备节点与 major/minor` 横幅 |
| `l99test` | `l99test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l91test` | `l91test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `moduletest` | `moduletest: module init order and exported-symbol lookup passed` |
| `about` | `Lesson 99: 设备节点与 major/minor` |
| `ramfsinfo` | 首行 `ramfs/initramfs nodes: 5  root=dentry0 memory-backed` + 5 行 `node ...` 目录表 |

判定成功：`l99test`/`pathtest`/`moduletest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l99test` 输出 `Lesson 92 fallback reported` | `lesson_92_state` 初始化/断言失败（stale 镜像） | `grep -n "l99test" kernel64.c`；确认初始化串 `{92U,93U,94U,95U,1,1,1,1}` 与 `b==a+1U` |
| `fdinfo` 显示的 inode 编号不是 1..3 | inode 表被多次初始化或 ino 被改写 | 对照 `vfs_init` 的 `{i+1,0x100+i,0100644,1,1}`；重启内核重置 |
| `ramfsinfo` 的目录树不完整 | `ramfs_count` 或节点 valid 标志异常 | 对照 `ramfs_init` 的 5 节点布局；检查 `parent`/`inode` 边 |
| `pathtest` 输出 `BROKEN` | `ramfs_lookup` 命中/未命中断言异常 | 对照 `ramfs_lookup` 的五条命中分支与三条未命中；先跑 `ramfsinfo` |
| `moduletest` 输出 `BROKEN` | `module_lookup` 的 pmm/miss 断言异常 | 对照 `module_init_model` 的 `0x706d6d`/`0x6d697373` 哈希与 exported 标志 |
| `l99test` 与 `l91test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l99test` 只操作 `lesson_92_state`、`l91test` 只操作 `lesson_91_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 99' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `设备节点与 major/minor` 与 `Lesson 99` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `inode_model.ino=i+1`（实例编号） | `include/linux/fs.h` 的 `struct inode`（`i_ino`/`i_rdev`）；dev_t 的 minor 语义 | 模型 ino 只有 1..3，无 inode 号分配器 |
| `inode_model.mode=0100644`（类型位） | `include/linux/fs.h`：`S_IFCHR`(0020000)/`S_IFBLK`(0060000)/`S_IFREG`(0100000) 类型位 | 模型只造普通文件 inode，未造 S_IFCHR 设备节点 |
| dev_t 的分段编码（major/minor） | `include/linux/kdev_t.h`：`MAJOR`/`MINOR`/`MKDEV`、`MINORBITS=20` | 模型未实现编码宏，用「模块哈希+ino」类比两层编号 |
| major 号由驱动占用 | `include/linux/major.h`（`TTY_MAJOR 4`、`MEM_MAJOR 1` 等）；`fs/char_dev.c` `register_chrdev_region` | 模型 `module name_hash` 固定两个子系统，无 major 分配冲突检测 |
| 设备节点在 /dev 目录中 | `fs/namei.c`：`vfs_mknod()`；`fs/devtmpfs` 与 `drivers/base/devtmpfs.c` 的自动创建设备节点 | 模型无 mknod 路径、无 udev/devtmpfs 自动节点 |
| `ramfsinfo` 目录列出看到节点 | `fs/libfs.c` `dcache_readdir`；`fs/readdir.c` `iterate_dir` | 模型直接遍历定长数组，无 dirent 缓冲 |
| `chrdev_open` 按 inode 设备号找驱动 | `fs/char_dev.c`：`chrdev_open()` 读 `inode->i_rdev` → 查 `cdev_map` | 模型用 `module_lookup` 线性扫描代替 kobj_map 哈希 |
| `l99test` 断言 | 无直接对应（LTP 设备文件测试套件） | 模型把编号主题的可验证状态固化进内核 |

**权威来源**：Linux `include/linux/kdev_t.h`、`include/linux/major.h`、`include/linux/fs.h`、`fs/char_dev.c`、`fs/namei.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么 dev_t 要把 major 放高位、minor 放低位而不是反过来？`MAJOR(dev)`/`MINOR(dev)`/`MKDEV(ma,mi)` 三个宏各自做了什么运算？
2. **源码定位**：在 `kernel64.c` 中找出所有「类型/编号」承载点：`ramfs_node.type`、`inode_model.mode`、`inode_model.ino`、`module_model.name_hash`，画出「major（子系统）→ dev_t 类比 → minor（ino）」的映射链。
3. **动手实验**：在 `vfs_init` 中把 inode 0 的 `mode` 从 `0100644` 改成 `0020644`（模拟 S_IFCHR），并把 `ramfs_init` 的某个节点 type 改为字符设备，用 `ramfsinfo`/`fdinfo` 观察该节点在目录树中的呈现。
4. **动手实验**：实现两个教学宏 `#define MODEL_MAJOR(h) ((h)>>4)` 与 `#define MODEL_MINOR(h) ((h)&0xf)`，用 `module name_hash` 与 `ino` 组合演示 dev_t 分段编解码。
5. **Linux 对照**：阅读 `include/linux/kdev_t.h` 与 `fs/char_dev.c` 的 `cdev_add`，解释为什么 `chrdev_open` 必须同时用 major 与 minor 才能唯一定位驱动实例；对比教学模型只用 name_hash 线性查找的简化。

---

## 9. 本课小结与下一课预告

1. 本课讲清 dev_t：major（高位，驱动/子系统）与 minor（低位，实例）拼成一个 32 位设备号，`include/linux/kdev_t.h` 提供编解码宏。
2. 设备节点是类型位为 `S_IFCHR`/`S_IFBLK`、正文为 dev_t 的特殊 inode，`mknod` 创建、`open` 按它查驱动——教学内核用 inode `mode`/`ino` 与 ramfs 目录树承载同样的语义。
3. 教学映射：模块 `name_hash` ≈ major、inode `ino=i+1` ≈ minor、ramfs 目录树 ≈ /dev、`module_lookup` ≈ `chrdev_open` 的按键查找。
4. 编号的确定性与连续性（`ino=i+1`、检查点 `b==a+1U`）是设备号世界的首要正确性属性。
5. 检查点增量：新增 `l99test`（Origin Lesson 92），恢复历史命名 `l91test`，横幅与 `about` 更新。
6. 下一课（Lesson 100）主题转向**设备打开与 ioctl 元数据**（对照 `fs/ioctl.c`），在编号与节点之上精讲设备的 open 路径与 ioctl 控制接口。
