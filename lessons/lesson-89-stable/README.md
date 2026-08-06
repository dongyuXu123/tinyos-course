# Lesson 89: 超级块与文件系统注册 — 精讲文档

> **课号**：Lesson 89 ｜ **主题**：超级块与文件系统注册（superblock and filesystem registration）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 82 原型的检查点
> **前置课程**：[`../lesson-88-stable/README.md`](../lesson-88-stable/README.md)（VFS 层次与 mount 元数据）
> **后续课程**：[`../lesson-90-stable/README.md`](../lesson-90-stable/README.md)（inode 生命周期与引用）
> **一句话目标**：在 VFS 四层模型之上，讲清「超级块」承载的文件系统全局状态与「文件系统注册表」的登记机制，并用教学内核的模块/符号表（`modules[]`/`exported_symbols[]`）与 ramfs 根文件系统初始化做对照验证。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第二课。`kernel64.c` 相对上一课仅做一处增量——把 `l88test` 恢复为 `l81test`，新增 `lesson_82_model` 状态与 `l89test` 检查点测试，并更新 `about`/开机横幅为本课主题。超级块与注册的「机制」由既有累积代码承载：`vfs_init`/`ramfs_init` 建立 ramfs 的全局状态，`module_init_model`/`module_lookup` 提供注册表式查找。继承的进程、GUI、子系统回归保持有效。

---

## 1. 课程定位（Mission）

**学完本课你能**：定义超级块（每个已挂载文件系统一个，保存 magic、根 inode、大小等全局状态）；说明文件系统注册表（`fs/filesystems.c` 的 `file_system_type` 全局链表与 `register_filesystem()`）为何把「名字」与「mount 函数」绑定；在教学内核中把 `modules[]`/`exported_symbols[]`、`vfs_init`/`ramfs_init` 映射到这两个概念；运行 `l89test`/`moduletest`/`moduleinfo` 验证。

**在课程主线中的位置**：Lesson 88 搭好 VFS 层次框架；本课把最外层「superblock」与「注册机制」讲透（对照 Linux `fs/super.c`、`fs/filesystems.c`）；下一课（Lesson 90）深入 inode 生命周期（对照 `fs/inode.c`）。三课合起来覆盖 VFS 对象体系的「全局态 → 注册 → 个体生命周期」。

**前置知识清单**（学本课前必须掌握）：
1. VFS 四层模型与四张表：`inode_table`/`dentry_table`/`file_table`/`fd_table`（Lesson 88）。
2. ramfs 根文件系统：`ramfs_init` 的 5 节点树与 `ramfs_lookup` 路径解析（Lesson 44/88）。
3. 模块/符号模型：`struct module_model`/`struct symbol_model`、`module_init_model`（Lesson 47）。
4. 打开路径引用链：`fd_open_model`/`fd_close_model`（Lesson 88）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 89: 超级块与文件系统注册`；
- 新命令 `l89test` 输出 `l89test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `moduleinfo`/`moduletest` 展示注册表式查找与模块初始化状态。

---

## 2. 核心概念精讲

### 2.1 超级块（superblock）：文件系统的「身份证」

**定义**：超级块是**每个已挂载文件系统实例**一份的全局元数据，记录该文件系统的身份与总体状态。Linux 中即 `struct super_block`（`include/linux/fs.h`），由 `fs/super.c` 的 `alloc_super()` 分配、`deactivate_super()` 释放。

**关键字段直觉**：
- `s_magic`：文件系统魔数，快速识别类型；
- `s_root`：根 inode 对应的根 dentry（路径解析起点）；
- `s_fs_info`：指向具体文件系统私有数据（如 ext4 超级块副本）；
- `s_count`：引用计数（一个 superblock 可被多个 mount 共享）。

**教学模型的位置**：TinyOS 没有独立 `struct superblock`，但「ramfs 根文件系统的全局状态」实际由 `vfs_init()` 统一建立的三张表承担：`inode_table`（物理实体）+ `dentry_table`（根/目录项）+ ramfs 节点树（`ramfs_nodes`）。可以把这组状态整体视为「ramfs 的超级块」，其「magic/根」信息由 `ramfs_nodes[0]`（根目录 `RAMFS_DIR`）与 inode 表共同表达。

### 2.2 文件系统注册（filesystem registration）：类型登记簿

**定义**：Linux 维护一张**文件系统类型登记簿**——`file_system_type` 的全局链表（`fs/filesystems.c` 的 `file_systems` 链表），`register_filesystem()`/`unregister_filesystem()` 增删登记项，`get_fs_type(name)` 按名字查找。登记项把「文件系统名字」（如 `"ext4"`、`"ramfs"`）与「mount 函数」（`fs_type->mount`）绑定：mount 时按名字找到类型，调用其 mount 例程创建 superblock。

**为什么需要注册表**：内核要支持多个文件系统并存且互相独立。没有登记簿，mount 就只能写死一个类型；有了它，`mount -t ext4` 与 `mount -t ramfs` 走同一套挂载逻辑、按名分发。

### 2.3 教学模型的注册表映射

教学内核用**模块/符号表**模拟注册机制：

```c
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
struct symbol_model { u64 name_hash,owner; u8 exported,valid; };
static struct module_model modules[MODULE_MAX];      /* MODULE_MAX=3 */
static struct symbol_model exported_symbols[SYMBOL_MAX];  /* SYMBOL_MAX=4 */
```

| 教学模型 | Linux 对照 | 说明 |
|---|---|---|
| `modules[i].loaded`/`initialized` | `file_system_type` 登记项存在 + mount 已完成 | 登记与挂载两个阶段用两个标志表达 |
| `modules[0].name_hash=0x636f7265` | 名字 `"core"`（模块注册表条目） | `0x636f7265` 是 `"core"` 的小端散列 |
| `modules[1].name_hash=0x766673` | 名字 `"vfs"`（VFS 子系统登记项） | `0x766673` 即 `"vfs"` |
| `exported_symbols[]`（owner/exported/valid） | 导出符号表 / `EXPORT_SYMBOL` | `owner` 指明符号属于哪个模块 |
| `module_lookup(name)` | `get_fs_type(name)` / `find_symbol` | 按散列线性扫描 |
| `vfs_init()` + `ramfs_init()` | `mount` 例程创建 superblock | 教学「挂载」就是建表 + 建节点树 |

### 2.4 检查点模型：lesson_82_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l89test` 成功串沿用 VFS/设备阶段主题：`bounded VFS, devices, epoll, and service management checkpoint passed`（Origin 编号 Lesson 82）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、模块/注册表、GUI、检查点） | 恢复 `l81test`；新增 `lesson_82_model`/`lesson_82_state`/`l89test`；`about` 与横幅更新。超级块/注册机制由累积代码承载，本课以「超级块与注册」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`超级块与文件系统注册`/`Lesson 89`/`l89test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（超级块/注册机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_82_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_82_model lesson_82_state;
static TEXT64 void l89test(u16*c){lesson_82_state=(struct lesson_82_model){82U,83U,84U,85U,1,1,1,1};int ok=lesson_82_state.valid&&lesson_82_state.active&&lesson_82_state.ready&&lesson_82_state.accounted&&lesson_82_state.b==lesson_82_state.a+1U;text64(c,"l89test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 82 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 82→85（Origin 编号 Lesson 82），四布尔位全置 1。
2. **成功串**：`l89test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 82 fallback reported`。
3. **恢复的 `l81test`**：本课同时恢复 `l81test`（`lesson_81_state`，81→84），两个 VFS 检查点可独立运行。

#### 3.2.2 注册表：module_init_model 建立登记项

```c
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
```

逐行分析：
1. **清表**：先清零全部 `modules`（3 个）与 `exported_symbols`（4 个）槽，保证登记表从确定状态开始。
2. **登记模块 0**：`{name_hash=0x636f7265("core"),init_calls=1,exit_calls=0,loaded=1,initialized=1}`——「core」模块已登记且完成初始化。这对应注册表中已注册的文件系统类型。
3. **登记模块 1**：`{name_hash=0x766673("vfs"),init_calls=1,...,loaded=1,initialized=1}`——「vfs」模块已登记。
4. **导出符号**：`exported_symbols[0]={0x706d6d("pmm"),owner=0,exported=1,valid=1}`（pmm 由 core 导出）、`exported_symbols[1]={0x766673("vfs"),owner=1,exported=1,valid=1}`（vfs 由 vfs 模块导出）——模拟 `EXPORT_SYMBOL` 的登记簿。
5. **统计**：`module_inits=2`、`module_exports=2`、`module_lookups=0`。

#### 3.2.3 注册表查找：module_lookup

```c
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
```
1. **查找计数**：每次调用 `module_lookups++`，供 `moduleinfo` 报告。
2. **线性扫描**：遍历 4 个符号槽，同时要求 `valid && exported && name_hash==name` 才命中——缺任一条件即不返回该符号。
3. **语义对应**：这就是 `get_fs_type(name)`/`find_symbol` 的教学版：登记项必须「已登记（valid）且可导出（exported）」才能被解析。

`moduletest` 用两个名字验证：
```c
static TEXT64 void moduletest(u16*c){int a=module_lookup(0x706d6d),b=module_lookup(0x6d697373),d=modules[0].initialized&&modules[1].initialized;text64(c,"moduletest: ");text64(c,a&&!b&&d?"module init order and exported-symbol lookup passed":"BROKEN");putc64(c,'\n');}
```
- `module_lookup(0x706d6d)`（pmm）命中 → `a=1`；
- `module_lookup(0x6d697373)`（"miss"，未登记）未命中 → `b=0`；
- 两模块 `initialized` 均为真 → `d=1`。
输出：`moduletest: module init order and exported-symbol lookup passed`。

#### 3.2.4 超级块侧：vfs_init + ramfs_init（ramfs 的全局状态）

超级块概念在教学内核中最接近的实现是 `vfs_init` 与 `ramfs_init` 共同建立的「ramfs 已挂载实例的全局状态」：
```c
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **根 inode 与根 dentry**：inode 1..3、dentry 0..2 成对建立，其中 dentry 0 是根目录项（`ramfs_nodes[0]` 即根目录）——对应 superblock 的 `s_root`。
2. **挂载**：末尾 `ramfs_init()` 建 5 节点树 = 「ramfs 类型被 mount 到一个已挂载实例」的过程；`pipe_init()` 复位管道。
3. **与注册表的分工**：`module_init_model`（登记）发生在 `kernel_main64_binary` 顶部；`vfs_init`（挂载/建状态）紧随其后。二者分别对应「register_filesystem」与「mount 例程创建 super_block」。

`ramfs_init` 中 `ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1}` 等 5 个节点共同表达 ramfs 的目录树（见 Lesson 88 精讲），此处不再重复。

#### 3.2.5 exec64 增量与开机横幅

- `about` 输出 `Lesson 89: 超级块与文件系统注册\n`；检查点分支：
```c
else if(eq64(word,"l81test")){if(!noargs64(arg))usage64(c,"l81test");else l81test(c);}else if(eq64(word,"l89test")){if(!noargs64(arg))usage64(c,"l89test");else l89test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 89: 超级块与文件系统注册\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `超级块与文件系统注册`、`Lesson 89`，kernel64.c 含 `l89test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model()：登记 core/vfs 模块与导出符号（注册表就绪）
 ├─ pmm_init/vma_init/reclaim_init → vfs_init()（建 inode/dentry 表 + ramfs 根 = superblock 侧状态）
 ├─ 横幅 "Lesson 89: 超级块与文件系统注册"
 └─ 主循环：命令 → exec64
     ├─ l89test → 阶段检查点
     ├─ moduletest → 注册表查找验证
     ├─ moduleinfo → 登记项与统计
     └─ shellrun/shelltest → /bin/sh 打开（走 VFS + fd 生命周期）
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`module_init_model()` 登记后打印横幅 `Lesson 89: 超级块与文件系统注册`。
2. **`l89test`** → `l89test(c)` → 断言 → `l89test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`moduletest`** → `module_lookup(0x706d6d)`/`module_lookup(0x6d697373)` → `moduletest: module init order and exported-symbol lookup passed`。
4. **`moduleinfo`** → `modules initialized/exports/lookups: 2/2/N` + `module 0 initialized 1`、`module 1 initialized 1` 两行。
5. **`ramfsinfo`** → 打印 5 个 ramfs 节点（目录/文件、parent、inode），展示已挂载 ramfs 的树。
6. **`shellrun`** → `vfs_init()` + `shell_exec_path("/bin/sh",2,1)` → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`——把注册/挂载后的文件系统真正用起来。

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
Multiboot2 and Lesson 89 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 89: 超级块与文件系统注册` 横幅 |
| `l89test` | `l89test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l81test` | `l81test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `moduletest` | `moduletest: module init order and exported-symbol lookup passed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `ramfsinfo` | 5 个节点的 `node N parent P inode I dir/file` 列表 |
| `about` | `Lesson 89: 超级块与文件系统注册` |

判定成功：`l89test`/`moduletest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l89test` 输出 `Lesson 82 fallback reported` | `lesson_82_state` 初始化/断言失败（stale 镜像） | `grep -n "l89test" kernel64.c`；确认初始化串 `{82U,83U,84U,85U,1,1,1,1}` |
| `moduletest` 输出 `BROKEN` | 注册表查找失败或模块未初始化 | 检查 `module_init_model` 是否在 banner 前调用；`moduleinfo` 看 `initialized` 与 `lookups` |
| `module_lookup` 误命中 | `valid/exported` 条件被破坏 | 对照 `module_lookup` 的三条件 `if(exported_symbols[i].valid&&exported_symbols[i].exported&&...name_hash==name)` |
| `moduleinfo` 显示 `initialized 0` | `modules[i].initialized` 未置位（登记失败） | 检查 `modules[0]/[1]` 的初始化串末尾的 `1,1` |
| `shellrun` 输出 `BROKEN` | `ramfs_lookup("/bin/sh")` 或 `fd_open_model` 失败 | 先 `pathtest`/`fdtest` 定位；确认 `vfs_init` 已运行 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 89' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `超级块与文件系统注册` 与 `Lesson 89` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `vfs_init` 建的 inode/dentry 表（ramfs 全局状态） | `fs/super.c`：`alloc_super()`/`sget()`；`include/linux/fs.h` 的 `struct super_block`（`s_magic`/`s_root`/`s_fs_info`/`s_count`） | 模型无独立 super_block 对象，全局状态散落在定长表；无块设备/日志 |
| `modules[]` 登记表（loaded/initialized） | `fs/filesystems.c`：`file_systems` 链表、`register_filesystem()`/`unregister_filesystem()`；`include/linux/fs.h` 的 `struct file_system_type` | 模型固定 3 槽、无链表、无优先级排序 |
| `module_lookup(name_hash)` 线性扫描 | `fs/filesystems.c`：`get_fs_type(name)`（按名字遍历链表） | 模型用散列 + 固定槽，无名字字符串比较 |
| `exported_symbols[]`（owner/exported/valid） | `kernel/module.c` + `include/linux/export.h`：`EXPORT_SYMBOL`/`find_symbol` | 模型 4 个符号槽，无地址绑定与依赖解析 |
| `ramfs_init` 建 5 节点树 = mount 过程 | `fs/ramfs/inode.c`：`ramfs_mount()`/`ramfs_fill_super()`；`fs/super.c`：`mount_single()` | 模型不读盘，节点树是直接内存数据 |
| `modules[0].name_hash=0x636f7265` | `fs/filesystems.c` 中名字如 `"ramfs"`、`"ext4"` 的登记 | 模型名字散列代替字符串常量 |
| `l89test` 断言 | 无直接对应（`tools/testing/selftests/` 的 fs 测试） | 模型把注册/挂载验证固化进内核 |

**权威来源**：Linux `fs/super.c`、`fs/filesystems.c`、`include/linux/fs.h` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么「注册（register）」与「挂载（mount）」是两回事？教学模型里分别对应 `module_init_model` 与 `vfs_init`+`ramfs_init`，请解释。
2. **源码定位**：在 `kernel64.c` 中找到 `module_lookup` 的全部调用点，并解释 `moduletest` 为何断言 `b==0`（未登记符号必须查找失败）。
3. **动手实验**：在 `module_init_model` 中把 `modules[1]` 的 `initialized` 改为 0，重新构建运行 `moduletest`，观察输出变为 `BROKEN`；解释登记与初始化两阶段分离。
4. **动手实验**：在 `exported_symbols` 中新增 `{0x646576,1,1,1}`（"dev" 由 vfs 导出），给 `moduletest` 增加 `module_lookup(0x646576)` 断言，重跑验证。
5. **Linux 对照**：阅读 `fs/super.c` 的 `sget()` 与 `alloc_super()`，对比教学模型的「ramfs 全局状态」，指出 Linux 中 `s_root` 如何在 `mount_single` 中被填充。

---

## 9. 本课小结与下一课预告

1. 本课在 VFS 框架内补上「超级块」与「文件系统注册」两个概念：前者是已挂载文件系统的全局身份，后者是类型登记簿。
2. 教学模型用 `vfs_init`/`ramfs_init` 承载超级块侧状态，用 `modules[]`/`exported_symbols[]` 与 `module_lookup` 承载注册表侧机制。
3. `moduletest` 验证「已登记符号命中、未登记符号不命中、模块初始化顺序正确」三条注册表不变量。
4. `shellrun` 把注册/挂载后的文件系统接入 shell 生命周期，验证 VFS 与 init/session 子系统的联动。
5. `l89test` 继续沿用 VFS/设备阶段检查点家族，`l81test` 历史检查点保留。
6. 下一课（Lesson 90）将主题深入 **inode 生命周期与引用**（对照 `fs/inode.c`），精讲 `iget`/`iput` 与 `inode_model.refs` 的完整生命旅程，VFS 阶段由此从「全局」进入「个体」。
