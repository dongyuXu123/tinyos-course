# Lesson 93: mount namespace 元数据 — 精讲文档

> **课号**：Lesson 93 ｜ **主题**：mount namespace 元数据（mount namespace metadata）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 86 原型的检查点
> **前置课程**：[`../lesson-92-stable/README.md`](../lesson-92-stable/README.md)（路径解析与遍历边界）
> **后续课程**：[`../lesson-94-stable/README.md`](../lesson-94-stable/README.md)（文件权限与访问检查）
> **一句话目标**：精讲「每个进程看到一个文件系统树视图」的 mount namespace 概念，验证教学内核以「单个全局命名空间 + ramfs 根树 + 注册表式模块/符号」承载 namespace 元数据的建模方式。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第六课。`kernel64.c` 相对上一课仅做一处增量——把 `l92test` 恢复为 `l85test`，新增 `lesson_86_model` 状态与 `l93test` 检查点测试，并更新 `about`/开机横幅为本课主题。**如实说明**：本课是「检查点课：机制继承 + 本课检查点增量」——内核中**没有**独立的 mount 表或 `struct mount`；mount namespace 以元数据形式承载于既有机制之上：`ramfs_init` 的根树 = 命名空间唯一的根文件系统视图，`ramfs_lookup("/")` = 根解析，`module_init_model`/`exported_symbols[]` = 文件系统/模块注册表，`init_model` = init 进程作为命名空间宿主。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l86test`，但实际源码中本课检查点命令是 `l93test`（`l86test` 到 Lesson 94 才出现）。本文以源码为准：本课检查点命令为 `l93test`，历史回归命令 `l85test`（恢复自上一课 `l92test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：定义 mount namespace（进程的 `nsproxy->mnt_ns`，一组 `vfsmount` 组成的文件系统树视图）；说出 Linux 中进程如何获得独立命名空间（`clone(CLONE_NEWNS)` / `unshare`）；在教学内核中定位「单命名空间」的三个载体——ramfs 根树、根解析 `ramfs_lookup("/")`、注册表 `modules[]`/`exported_symbols[]`；如实说明模型为何不做 mount 表与 `CLONE_NEWNS`；运行 `l93test`/`moduletest`/`ramfsinfo`/`pathtest` 验证。

**在课程主线中的位置**：Lesson 88 搭四层 VFS 层次、Lesson 89 讲超级块与注册、Lesson 91/92 讲 dentry 与路径解析。本课把视角放到**最高处**——「进程看到的是哪棵文件系统树」。Linux 答案是 mount namespace（`fs/namespace.c`/`fs/mount.c`）；教学模型把它压缩为「单个全局命名空间」，并用既有注册表机制补足「文件系统登记」语义。下一课（Lesson 94）进入权限层，讲文件权限与访问检查。

**前置知识清单**（学本课前必须掌握）：
1. VFS 层次与 mount 元数据（Lesson 88）：superblock 挂到目录 dentry 上的心智图。
2. 超级块与文件系统注册（Lesson 89）：`modules[]`/`exported_symbols[]` 的注册表语义与 `module_lookup`。
3. ramfs 根树与根解析：`ramfs_init` 5 节点树、`ramfs_lookup("/")` 返回根 dentry 0（Lesson 91/92）。
4. init 进程模型：`init_model`（pid=1）与 `shell_runtime` 的服务宿主角色（Lesson 41/83）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 93: mount namespace 元数据`；
- 新命令 `l93test` 输出 `l93test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `ramfsinfo`/`pathtest` 展示命名空间的根视图，`moduletest`/`moduleinfo` 展示注册表式查找。

---

## 2. 核心概念精讲

### 2.1 mount namespace：进程的文件系统树视图

**直觉**：同一台机器上，容器里看到的 `/` 和宿主机看到的 `/` 是**同一根路径、不同内容**——因为每个进程挂着不同的「文件系统树视图」。这个视图在 Linux 里就是 mount namespace。

**Linux 结构**（对照 `include/linux/nsproxy.h`、`fs/namespace.c`、`fs/mount.c`）：
1. `task_struct->nsproxy->mnt_ns`：每个进程指向一个 `struct mnt_namespace`；
2. `struct mnt_namespace` 拥有一棵由 `struct mount`（封装 `struct vfsmount`）组成的树，树根是根挂载点；
3. 新进程默认**继承**父进程命名空间；用 `clone(CLONE_NEWNS)` 或 `unshare(CLONE_NEWNS)` 才获得副本，之后在本命名空间内的 `mount/umount` 互不可见；
4. 挂载点元数据：`mount` 关联 `mnt_mountpoint`（挂到哪个 dentry）与 `mnt_root`（文件系统根 dentry）、`mnt_sb`（superblock）。

### 2.2 教学模型：单个全局命名空间

教学内核**只有一个**命名空间——所有进程（init、shell、用户进程）共享同一棵 ramfs 根树。这与 Linux 引导早期、任何进程尚未 `CLONE_NEWNS` 时的状态一致（一切从 init 命名空间继承）。

模型的 namespace 元数据由三处既有代码承载：

| 教学元素 | 代码位置 | 对应 Linux namespace 概念 |
|---|---|---|
| 命名空间根树 | `ramfs_init` 的 5 节点树（`/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh`） | 根挂载点的 `mnt_root` 之下可见的整棵树 |
| 根解析 | `ramfs_lookup("/")` 返回节点 0 | `path_init` 从根 dentry 出发 |
| 文件系统登记 | `module_init_model` 的 `modules[]`/`exported_symbols[]` | `fs/filesystems.c` 的 `register_filesystem()` 注册表 |
| 命名空间宿主 | `init_model`（pid=1）/`shell_runtime` | init 命名空间由 pid 1 拥有/继承 |

**为什么不做 mount 表**：真正的 mount namespace 需要 mount/umount 树操作、传播事件、`mnt_id` 分配与引用管理——教学模型用「定长静态树 + 单命名空间」先建立「每个进程通过命名空间看到根视图」的语义，再指出简化方向。这是与 Lesson 88（mount 元数据）的衔接点：Lesson 88 讲 superblock 挂到 dentry 的「挂载元数据」，本课讲「这些挂载组成的**进程可见视图**」。

### 2.3 注册表：文件系统如何进入命名空间

Linux 中每个文件系统类型（`struct file_system_type`）先经 `register_filesystem()` 登记到全局链表，`mount` 时按名字找到它的 `mount` 回调。教学模型的 `modules[]`/`exported_symbols[]` 是注册表的简化：`module_init_model` 登记 core 与 vfs 两个模块并导出 `pmm`/`vfs` 符号，`module_lookup(name_hash)` 按哈希查导出符号。本课把这条「登记 → 查找」链与「命名空间里能挂载什么」绑定讲解。

### 2.4 检查点模型：lesson_86_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l93test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 86）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l85test`；新增 `lesson_86_model`/`lesson_86_state`/`l93test`；`about` 与横幅更新。mount namespace 元数据由既有机制承载，本课以「命名空间」视角精讲，无新增 mount 结构 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`mount namespace 元数据`/`Lesson 93`/`l93test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（mount namespace 元数据视角 + 本课增量）

#### 3.2.1 命名空间根树：ramfs_init

```c
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```
以命名空间视角重读这 5 个节点：
1. **节点 0 = 根挂载点的 `mnt_root`**：`{hash=0, parent=0, inode=0, RAMFS_DIR}`——父指针指向自己的根 dentry，正是「命名空间树根」的元数据形态；所有进程共享这棵根树。
2. **目录节点承载「挂载点」潜力**：`etc`(1)/`bin`(3) 是目录；若模型支持 mount，它们就是可挂载点（Linux 中 `mnt_mountpoint` 指向这类目录 dentry）。教学模型只把它们的 `parent` 关系建好。
3. **叶子节点是文件**：`motd`(2)/`sh`(4)，是命名空间里「能打开/执行」的最终对象；`shell_exec_path` 经 `/bin/sh` 命中节点 4。

#### 3.2.2 命名空间根解析：ramfs_lookup 的根分支

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
```
1. `eq64(path,"/")||eq64(path,".")` 返回 0——任何进程解析根都得到同一个根 dentry 节点，这正是「单命名空间、全局共享根」的体现（Linux 中该根来自 `current->nsproxy->mnt_ns` 的根挂载）。
2. 其余 4 个匹配分支是「在根树内逐级可达」的全部路径集合；未命中统一 -1。
3. 从命名空间视角看：路径空间 = 命名空间内容 = 5 个静态节点；这与 Lesson 92 的「遍历边界」视角互补——Lesson 92 讲解析器如何拒绝越界，本课讲「被解析的边界之内」正是这个命名空间。

#### 3.2.3 文件系统登记：module_init_model / module_lookup

```c
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
```
1. `modules[1]` 名字哈希 `0x766673`="vfs"——教学内核把自己登记为 `vfs` 模块（Lesson 89 已详讲注册语义）；`modules[0]`="core"。
2. `module_lookup` 按 `name_hash` 线性扫描导出符号（`pmm`/`vfs`），与 Linux `fs/filesystems.c` 的 `get_fs_type(name)` 扫描注册表、再调用该类型的 mount 回调是同构的「登记 → 查找」。
3. 命名空间视角：**一个命名空间能挂载什么，取决于注册表里有什么**。模型只登记了 core/vfs 两个模块、导出两个符号，因此命名空间里只存在 ramfs 这棵静态树。

#### 3.2.4 命名空间宿主：init_model / shell_runtime

```c
static TEXT64 void init_model_start(void){init_model=(struct init_model){FIXED_PID,1,0,0,0,0,0,1};shell_runtime_start();}
static TEXT64 void shellrun(u16*c){vfs_init();int ok=shell_exec_path("/bin/sh",2,1);text64(c,"shellrun: ");text64(c,ok&&shell_runtime.ready&&shell_runtime.exits==1?"validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed":"BROKEN");putc64(c,'\n');}
```
1. `init_model` 以 `FIXED_PID`(=1) 初始化——pid 1 在 Linux 中正是 init 命名空间的拥有者/继承者；`shell_runtime` 作为其服务宿主。
2. `shellrun` 调用 `vfs_init()` 重建 inode/dentry 表与 ramfs 树，再执行 `/bin/sh`——等价于「在根文件系统上启动 shell」，验证命名空间根视图可被进程正常使用。
3. 教学模型所有进程共享此命名空间（无 `CLONE_NEWNS`），因此不存在「视图分裂」，也无需挂载传播逻辑。

#### 3.2.5 本课新增检查点函数

```c
struct lesson_86_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_86_model lesson_86_state;
static TEXT64 void l93test(u16*c){lesson_86_state=(struct lesson_86_model){86U,87U,88U,89U,1,1,1,1};int ok=lesson_86_state.valid&&lesson_86_state.active&&lesson_86_state.ready&&lesson_86_state.accounted&&lesson_86_state.b==lesson_86_state.a+1U;text64(c,"l93test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 86 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 86→89（Origin 编号 Lesson 86），四布尔位全置 1。
2. **成功串**：`l93test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 86 fallback reported`。
3. **恢复的 `l85test`**：本课同时恢复 `l85test`（`lesson_85_state`，85→88，由上一课 `l92test` 更名而来），历史检查点可独立运行。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 93: mount namespace 元数据\n`；检查点分支：
```c
else if(eq64(word,"l85test")){if(!noargs64(arg))usage64(c,"l85test");else l85test(c);}else if(eq64(word,"l93test")){if(!noargs64(arg))usage64(c,"l93test");else l93test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 93: mount namespace 元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `mount namespace 元数据` 与 `Lesson 93`、kernel64.c 含 `l93test`，最后打印 `Multiboot2 and Lesson 93 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model()（登记 core/vfs）→ pmm/vma/reclaim → vfs_init() → ramfs_init()（根树）
 ├─ init_model_start()（pid=1 命名空间宿主）
 ├─ 横幅 "Lesson 93: mount namespace 元数据"
 └─ 主循环：命令 → exec64
     ├─ l93test → 阶段检查点
     ├─ moduletest/moduleinfo → 注册表查找验证
     ├─ ramfsinfo → 命名空间根树展示
     ├─ pathtest → 根解析与路径集合验证
     └─ shellrun → 在根文件系统上启动 /bin/sh
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`module_init_model()` 登记 core/vfs，`ramfs_init()` 建根树，`init_model_start()` 置 pid=1，打印横幅 `Lesson 93: mount namespace 元数据`。
2. **`l93test`** → `l93test(c)` → 断言 `lesson_86_state` → `l93test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`moduletest`** → `module_lookup(0x706d6d)`("pmm") 命中、`module_lookup(0x6d697373)`("miss") 未命中 → `moduletest: module init order and exported-symbol lookup passed`。
4. **`ramfsinfo`** → 打印 `ramfs/initramfs nodes: 5`、`paths: / /etc /etc/motd /bin /bin/sh` 与逐节点 `parent/inode/type`——即命名空间的完整视图。
5. **`pathtest`** → 根解析 `/`→0 等 7 用例 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
6. **`shellrun`** → 在根树内打开并执行 `/bin/sh` → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
7. **`about`** → `Lesson 93: mount namespace 元数据`。

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
Multiboot2 and Lesson 93 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 93: mount namespace 元数据` 横幅 |
| `l93test` | `l93test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l85test` | `l85test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `moduletest` | `moduletest: module init order and exported-symbol lookup passed` |
| `ramfsinfo` | 首行 `ramfs/initramfs nodes: 5`，随后 5 行 `node N parent P inode I dir|file` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 93: mount namespace 元数据` |

判定成功：`l93test`/`moduletest`/`pathtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l93test` 输出 `Lesson 86 fallback reported` | `lesson_86_state` 初始化/断言失败（stale 镜像） | `grep -n "l93test" kernel64.c`；确认初始化串 `{86U,87U,88U,89U,1,1,1,1}` |
| `moduletest` 输出 `BROKEN` | `module_init_model` 登记状态或 `module_lookup` 哈希不符 | 对照 `modules[]` 的 `0x636f7265`/`0x766673` 与 `exported_symbols[]` 的 `0x706d6d`/`0x766673` |
| `ramfsinfo` 树与预期不符 | `ramfs_init` 节点初始化错误 | 逐节点核对 `parent`/`inode`/`type`（节点含义见 Lesson 91） |
| `pathtest` 输出 `BROKEN` | 根解析/边界分支异常 | 对照 `ramfs_lookup` 的 5 分支与 3 个负向用例（Lesson 92） |
| `shellrun` 输出 `BROKEN` | `/bin/sh` 未命中或 fd 生命周期异常 | 先运行 `pathtest` 确认路径集合；检查 `shell_exec_path` open/close 配对 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 93' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `mount namespace 元数据` 与 `Lesson 93` |
| 误敲 `l86test` 无响应 | `l86test` 在本课源码中不存在（Lesson 94 才引入） | 本课检查点命令是 `l93test`；旧 README 的 `l86test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| 单个全局命名空间（所有进程共享 ramfs 根树） | `include/linux/nsproxy.h` 的 `nsproxy->mnt_ns`；`fs/namespace.c` 的 `struct mnt_namespace` | 模型无 `struct mnt_namespace`、无 `CLONE_NEWNS`/`unshare`，全系统一个视图 |
| `ramfs_nodes[0]` 父指针指向自己的根节点 | `fs/mount.c` 的 `struct mount`/`mnt_root`；根挂载点的 root dentry | 模型无挂载点（`mnt_mountpoint`/`mnt_root`）结构，只用静态节点表达根 |
| `ramfs_lookup("/")` 返回根节点 0 | `fs/namei.c` 的 `path_init`：从 `nd->root`（命名空间根挂载）出发 | 模型无挂载穿越（跨越 `mnt` 边界），路径不经过挂载点 |
| `module_init_model`/`module_lookup` | `fs/filesystems.c` 的 `register_filesystem()`/`get_fs_type()`；`struct file_system_type` | 模型只有 2 模块 2 符号，无类型级 mount 回调 |
| `init_model`(pid=1) 作为命名空间宿主 | `kernel/nsproxy.c`：pid 1 的 init namespace 继承链 | 模型只存 pid，无 `nsproxy` 引用计数与共享 |
| `shellrun` 在根树内执行 `/bin/sh` | `fs/exec.c`：`do_execve` 经当前命名空间解析可执行文件 | 模型无 PATH 搜索、无挂载点解析、无权限检查 |
| `l93test` 断言 | 无直接对应（LTP `fs` 测试套件） | 模型把命名空间元数据验证固化进内核 |

**权威来源**：Linux `include/linux/nsproxy.h`、`fs/namespace.c`、`fs/mount.c`、`fs/filesystems.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：mount namespace 与「挂载」的区别是什么？为什么说两个进程即使看到同样的 `/bin/sh` 路径，也可能位于不同 namespace？
2. **源码定位**：在 `kernel64.c` 中找出承载「命名空间根树」「根解析」「注册表」的三个函数，说明它们各自的表/函数名与容量上限。
3. **动手实验**：给 `ramfs_init` 增加一个名为 `var` 的新目录节点（`RAMFS_DIR`），并在 `ramfs_lookup` 中补 `/var` 匹配，重新构建后运行 `ramfsinfo`/`pathtest`，观察命名空间视图如何扩展。
4. **动手实验**：把 `module_lookup` 的 `exported` 条件去掉（未导出符号也能查中），运行 `moduletest` 观察断言如何失败，体会「注册表决定可见性」的边界语义。
5. **Linux 对照**：阅读 `fs/namespace.c` 的 `copy_mnt_ns`/`clone_mnt`，指出若教学内核要支持 `CLONE_NEWNS`，最少需要为 `ramfs_nodes` 增加哪些字段（如每个进程的根指针）。

---

## 9. 本课小结与下一课预告

1. 本课把 VFS 视角提升到「进程可见的文件系统树」：mount namespace 是 Linux 的答案，教学内核以单个全局命名空间建模。
2. 命名空间元数据由四块既有机制承载：ramfs 根树、`ramfs_lookup` 根解析、模块/符号注册表、init(pid=1) 宿主——如实说明了「机制继承 + 检查点增量」的课程性质。
3. 「注册表决定命名空间里能挂载什么」：模型只登记 core/vfs，因此根视图就是那棵 5 节点静态树。
4. 单命名空间意味着无挂载穿越、无传播、无 `CLONE_NEWNS`——这是教学简化，也是与 Lesson 88「mount 元数据」的区分点。
5. `shellrun` 验证了「在命名空间根文件系统上启动 shell」的完整路径。
6. `l93test` 沿用 VFS/设备阶段检查点家族，`l85test` 历史检查点保留。
7. 下一课（[`../lesson-94-stable/README.md`](../lesson-94-stable/README.md)，Lesson 94）进入权限层，讲 **文件权限与访问检查**（对照 `fs/namei.c` 的 `permission` 与 `generic_permission`）。
