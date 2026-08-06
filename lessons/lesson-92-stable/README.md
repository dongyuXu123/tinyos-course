# Lesson 92: 路径解析与遍历边界 — 精讲文档

> **课号**：Lesson 92 ｜ **主题**：路径解析与遍历边界（path resolution and traversal bounds）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 85 原型的检查点
> **前置课程**：[`../lesson-91-stable/README.md`](../lesson-91-stable/README.md)（dentry 缓存与路径组件）
> **后续课程**：[`../lesson-93-stable/README.md`](../lesson-93-stable/README.md)（mount namespace 元数据）
> **一句话目标**：精讲路径解析（从根 `/` 出发逐组件定位）的算法与遍历边界（相对路径、缺失组件、对非目录继续下钻为何必须失败），验证 `ramfs_lookup`/`pathtest` 对 7 种路径输入的分流。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第五课。`kernel64.c` 相对上一课仅做一处增量——把 `l91test` 恢复为 `l84test`，新增 `lesson_85_model` 状态与 `l92test` 检查点测试，并更新 `about`/开机横幅为本课主题。路径解析与遍历边界机制由累积代码承载：`ramfs_lookup` 承担「绝对路径 + 精确匹配」的解析器角色，`pathtest` 用 4 个正向与 3 个负向用例覆盖边界，`ramfs_init` 提供被解析的 5 节点目录树。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l85test`，但实际源码中本课检查点命令是 `l92test`（`l85test` 到 Lesson 93 才出现）。本文以源码为准：本课检查点命令为 `l92test`，历史回归命令 `l84test`（恢复自上一课 `l91test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出 Linux 路径解析的三个阶段（挂载点/根确定 → 逐组件 walk → 尾组件查找）；解释 `open("relative")` 为什么被拒绝而 `open("/etc/motd")` 成功；理解「遍历边界」是指路径名空间是**有界集合**——教学内核只认识 5 个路径，其余一律 `-1`；运行 `l92test`/`pathtest`/`ramfsinfo` 验证。

**在课程主线中的位置**：Lesson 91 已讲清楚 dentry「名字 → inode」的缓存角色；本课把镜头拉近到 **ramfs_lookup 的算法本身**：它如何解析一条路径、在哪些边界上必须失败。这是 VFS 阶段从「静态结构（表/树）」走向「动态过程（解析/查找）」的关键一课。下一课（Lesson 93）将视角升到整个命名空间，讲 mount namespace 元数据。

**前置知识清单**（学本课前必须掌握）：
1. dentry 与路径组件：`ramfs_nodes[]` 的 5 节点树与 `name_hash`（Lesson 91）。
2. 定长表与容量边界：`RAMFS_MAX=6`、`DENTRY_MAX=3` 的「固定容量」纪律（Lesson 44 起）。
3. 错误返回约定：`ramfs_lookup` 返回 -1 表示「无此 dentry」，调用方 `shell_exec_path` 据此拒绝执行（Lesson 46/83）。
4. `eq64` 逐字符比较语义与 `token64` 的界内拷贝（Lesson 21 起）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 92: 路径解析与遍历边界`；
- 新命令 `l92test` 输出 `l92test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `pathtest` 用 `/`、`/etc`、`/etc/motd`、`/bin/sh` 正向命中与 `/etc/missing`、`relative`、`/etc/motd/child` 负向用例展示解析与边界。

---

## 2. 核心概念精讲

### 2.1 绝对路径与相对路径：解析的起点

**直觉**：路径有两种起点——`/`（文件系统根）与 `.`（当前目录）。Linux 的 `path_init()` 需要 `fs_struct` 提供根与 cwd；教学内核没有 cwd 概念，因此**只接受绝对路径**。

**教学模型的入口守卫**：
```c
if(!path||path[0]!='/')return -1;
```
1. `!path`：空指针直接拒绝——调用方不能传野指针；
2. `path[0]!='/'`：不以 `/` 开头的路径（如 `relative`）一律返回 -1——模型没有 `cwd`，无从解析相对路径；
3. 这也是本课主题「遍历边界」的第一道边界：**路径空间被显式收缩为绝对路径子集**。

对照 Linux：`fs/namei.c` 的 `path_init()` 会依据 `nd->root`/`nd->path` 决定从根还是从 cwd 开始；教学模型砍掉 cwd，只保留根。

### 2.2 逐组件解析与整体匹配

**直觉**：Linux 的路径解析是「逐组件」的——`/etc/motd` 先到 `etc`，检查它是目录，再进它找 `motd`。教学模型的 `ramfs_lookup` 把整串当作一个键做精确匹配，等价于「组件查找的压缩结果」：5 个已知路径的组件序列被预先穷举成 5 个字符串。

```
输入           解析结果（组件序列）            返回
"/"           [根]                           0
"/etc"        [/]->[etc]                     1
"/etc/motd"   [/]->[etc]->[motd]             2
"/bin"        [/]->[bin]                     3
"/bin/sh"     [/]->[bin]->[sh]               4
"/etc/missing"[/]->[etc]->[missing]      miss → -1
"relative"    （无前导 /，非法起点）          -1
"/etc/motd/child"（motd 是文件，不可下钻）   -1
```

**为什么这样设计**：在 5 节点的静态 initramfs 里，真正的逐组件 walk 退化为查表；但**边界条件必须保留**——`ramfs_lookup` 仍需在「组件缺失」与「对非目录下钻」两种情况下返回 -1，这正是本课要精讲的遍历边界。

### 2.3 遍历边界（traversal bounds）：路径空间是有界的

「遍历边界」指解析器必须回答的一组「越界问题」：
1. **起点边界**：非绝对路径（`relative`）→ 拒绝；
2. **存在性边界**：组件不存在（`/etc/missing`）→ 拒绝，对应 Linux `ENOENT`；
3. **类型边界**：对文件继续下钻（`/etc/motd/child`，motd 是 `RAMFS_FILE`）→ 拒绝，对应 Linux `ENOTDIR`；
4. **容量边界**：路径表只有 5 条（`RAMFS_MAX=6` 实际用 5），查询超出集合一律 miss；
5. **深度边界**：不递归、不支持 `..`、不支持符号链接——模型刻意不做这些，教学上先建立「解析+边界」的最小骨架。

### 2.4 检查点模型：lesson_85_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l92test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 85）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l84test`；新增 `lesson_85_model`/`lesson_85_state`/`l92test`；`about` 与横幅更新。路径解析/遍历边界机制由累积代码承载，本课以「解析与边界」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`路径解析与遍历边界`/`Lesson 92`/`l92test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（路径解析/遍历边界 + 本课增量）

#### 3.2.1 被解析的对象：ramfs_init 的 5 节点目录树

```c
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```
1. 5 个节点构成解析的「已知集合」：`/`(0)、`/etc`(1)、`/etc/motd`(2)、`/bin`(3)、`/bin/sh`(4)（节点含义逐条已在 Lesson 91 精讲）。
2. 对解析而言关键的是 `type` 字段：`etc`/`bin` 是 `RAMFS_DIR`（可继续下钻），`motd`/`sh` 是 `RAMFS_FILE`（叶子，不可下钻）——类型信息正是「类型边界」判定的依据（虽然教学 `ramfs_lookup` 用整串匹配而非逐节点检查类型，但类型仍记录在树里供 `ramfsinfo` 展示）。
3. `ramfs_count=5` 同时定义了「容量边界」：任何第 6 个路径都不存在。

#### 3.2.2 解析器核心：ramfs_lookup

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
```

按解析阶段逐行拆解：
1. **第一阶段——起点校验（对应 Linux `path_init`）**：`!path||path[0]!='/'` 拒绝空指针与相对路径。这是第一道遍历边界。
2. **第二阶段——根解析（对应 Linux 处理 `/` 与 `.`）**：`eq64(path,"/")||eq64(path,".")` 都返回节点 0。`/` 是根；`.` 在无 cwd 的教学模型里也回退到根——注意这里与「绝对路径守卫」并不矛盾：`.` 是**特判**，而 `relative`（其他不以 `/` 开头的串）在第一步就被拦下。
3. **第三阶段——逐组件结果查表（对应 Linux `link_path_walk` + `walk_component`）**：对 4 条已知路径做整串精确匹配，命中返回节点下标。
4. **第四阶段——遍历边界收尾（对应 Linux `ENOENT`/`ENOTDIR`）**：未命中走 `ramfs_misses++; return -1;`。`/etc/missing`（缺失组件）与 `/etc/motd/child`（对文件下钻）在这里被**同一出口**拒绝——教学模型不做错误码细分，统一 `-1`；Linux 则区分 `-ENOENT` 与 `-ENOTDIR`。

**为什么整串匹配而不是逐组件 walk**：逐组件 walk 需要「每级组件在当前 dentry 子目录里查哈希」，至少再写一个 `hash_component`+`subdir_scan`；在固定 5 节点的教学模型里，把组件序列穷举为整串查找表，能让解析的**入口/根/边界语义**更突出，代价是失去通用性——这正是「教学模型简化」的明确取舍。

#### 3.2.3 边界用例集合：pathtest

```c
static TEXT64 void pathtest(u16*c){int a=ramfs_lookup("/"),b=ramfs_lookup("/etc"),d=ramfs_lookup("/etc/motd"),e=ramfs_lookup("/bin/sh"),f=ramfs_lookup("/etc/missing"),g=ramfs_lookup("relative"),h=ramfs_lookup("/etc/motd/child");text64(c,"pathtest: ");text64(c,a==0&&b==1&&d==2&&e==4&&f<0&&g<0&&h<0?"ramfs/initramfs root-to-dentry path lookup passed":"BROKEN");putc64(c,'\n');}
```
1. **正向 4 用例**：根 `/`→0；一级目录 `/etc`→1；二级叶子 `/etc/motd`→2；二级叶子 `/bin/sh`→4。覆盖「根 / 目录 / 叶子」三形态。
2. **负向 3 用例（本课重点——遍历边界）**：
   - `/etc/missing` → 组件缺失（`ENOENT` 语义）；
   - `relative` → 非绝对路径起点（`path_init` 拒绝语义）；
   - `/etc/motd/child` → `motd` 是 `RAMFS_FILE`，对文件下钻（`ENOTDIR` 语义）。
3. **断言**：全部通过才输出 `pathtest: ramfs/initramfs root-to-dentry path lookup passed`；任一 `-1` 期望落空即 `BROKEN`。

#### 3.2.4 解析结果的消费方：shell_exec_path

```c
static TEXT64 int shell_exec_path(const char *path,u32 argc,u32 envc){int inode=ramfs_lookup(path);int fd;u64 image_hash=0x5348454c4c494d47ULL;if(inode<0||argc>4U||envc>4U)return 0;fd=fd_open_model((u32)inode,0);if(fd<0)return 0;shell_runtime.commands++;shell_runtime.execs++;shell_runtime.argv_words+=argc;shell_runtime.env_words+=envc;shell_runtime.pipe_links++;shell_runtime.signal_links++;shell_runtime.timer_links++;shell_runtime.deferred_links++;if(image_hash!=0x5348454c4c494d47ULL)return 0;fd_close_model((u32)fd);shell_runtime.exits++;return 1;}
```
1. `ramfs_lookup(path)` 是解析入口；`inode<0` 直接返回 0——**解析失败即执行失败**，边界被第一道防线拦截。
2. 成功后 `fd_open_model` 建 file/fd，记账六类子系统链接，`fd_close_model` 释放，`exits++`。
3. 由此把「解析边界」与「生命周期」（Lesson 90）串成完整命令执行路径：`shellrun` 调用它解析 `/bin/sh`。

#### 3.2.5 本课新增检查点函数

```c
struct lesson_85_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_85_model lesson_85_state;
static TEXT64 void l92test(u16*c){lesson_85_state=(struct lesson_85_model){85U,86U,87U,88U,1,1,1,1};int ok=lesson_85_state.valid&&lesson_85_state.active&&lesson_85_state.ready&&lesson_85_state.accounted&&lesson_85_state.b==lesson_85_state.a+1U;text64(c,"l92test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 85 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 85→88（Origin 编号 Lesson 85），四布尔位全置 1。
2. **成功串**：`l92test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 85 fallback reported`。
3. **恢复的 `l84test`**：本课同时恢复 `l84test`（`lesson_84_state`，84→87，由上一课 `l91test` 更名而来），历史检查点可独立运行。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 92: 路径解析与遍历边界\n`；检查点分支：
```c
else if(eq64(word,"l84test")){if(!noargs64(arg))usage64(c,"l84test");else l84test(c);}else if(eq64(word,"l92test")){if(!noargs64(arg))usage64(c,"l92test");else l92test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 92: 路径解析与遍历边界\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `路径解析与遍历边界` 与 `Lesson 92`、kernel64.c 含 `l92test`，最后打印 `Multiboot2 and Lesson 92 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma/reclaim → vfs_init() → ramfs_init()（5 节点树）
 ├─ 横幅 "Lesson 92: 路径解析与遍历边界"
 └─ 主循环：命令 → exec64
     ├─ l92test → 阶段检查点
     ├─ pathtest → ramfs_lookup 七次（4 命中 / 3 边界拒绝）
     ├─ ramfsinfo → 打印被解析的目录树与命中统计
     └─ shellrun → shell_exec_path：ramfs_lookup("/bin/sh") 成功才打开 fd
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`ramfs_init()` 建 5 节点目录树，打印横幅 `Lesson 92: 路径解析与遍历边界`。
2. **`l92test`** → `l92test(c)` → 断言 `lesson_85_state` → `l92test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`pathtest`** → 7 次 `ramfs_lookup`：`/`→0、`/etc`→1、`/etc/motd`→2、`/bin/sh`→4，`/etc/missing`、`relative`、`/etc/motd/child` 全部 -1 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
4. **`ramfsinfo`** → 打印 `ramfs/initramfs nodes: 5`、`paths: / /etc /etc/motd /bin /bin/sh`、lookups/hits/misses 计数（运行 `pathtest` 后 hits=4、misses=3）。
5. **`shellrun`** → `shell_exec_path("/bin/sh",2,1)`：解析命中节点 4 → open → close → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
6. **`about`** → `Lesson 92: 路径解析与遍历边界`。

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
Multiboot2 and Lesson 92 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 92: 路径解析与遍历边界` 横幅 |
| `l92test` | `l92test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l84test` | `l84test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `ramfsinfo` | 首行 `ramfs/initramfs nodes: 5`，随后 5 行 `node N parent P inode I dir|file` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 92: 路径解析与遍历边界` |

判定成功：`l92test`/`pathtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l92test` 输出 `Lesson 85 fallback reported` | `lesson_85_state` 初始化/断言失败（stale 镜像） | `grep -n "l92test" kernel64.c`；确认初始化串 `{85U,86U,87U,88U,1,1,1,1}` |
| `pathtest` 输出 `BROKEN` | `ramfs_lookup` 某个边界分支返回错误值 | 对照 5 个匹配分支；重点检查 `/etc/missing`、`relative`（被 `path[0]!='/'` 拦截）、`/etc/motd/child` 都返回 -1 |
| `ramfs_lookup("relative")` 返回 0 | 入口守卫被绕过 | 确认 `if(!path||path[0]!='/')return -1;` 在 `ramfs_lookups++` 之前 |
| `ramfsinfo` hits/misses 与预期不符 | 在 `ramfs_lookup` 中提早 `return` 跳过计数 | 对照各分支 `ramfs_hits++` 与末尾 `ramfs_misses++` 的位置 |
| `shellrun` 输出 `BROKEN` | `/bin/sh` 未命中或 fd 生命周期异常 | 先运行 `pathtest` 确认解析；检查 `shell_exec_path` 的 `inode<0` 防线与 open/close 配对 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 92' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `路径解析与遍历边界` 与 `Lesson 92` |
| 误敲 `l85test` 无响应 | `l85test` 在本课源码中不存在（Lesson 93 才引入） | 本课检查点命令是 `l92test`；旧 README 的 `l85test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `ramfs_lookup` 的 `path[0]!='/'` 起点守卫 | `fs/namei.c`：`path_init()`（依据 `nd->root`/`nd->path` 决定根或 cwd 起点）；`fs_struct`（`fs/fs_struct.c`） | 模型无 cwd/`fs_struct`，只支持根起点，且不区分 `-ENOENT`/`-ENOTDIR` |
| `eq64(path,"/")||eq64(path,".")` 根特判 | `fs/namei.c`：`path_init()` 对 `/` 与 `..`/`.` 的特殊处理 | 模型把 `.` 直接映射回根，无 `..` 上跳 |
| 整串精确匹配 | `fs/namei.c`：`link_path_walk()`（逐组件）+ `walk_component()`（每级 dentry hash 查找） | 模型无组件拆分循环、无每级 `d_lookup`、无哈希桶 |
| `/etc/missing` → -1 | `fs/namei.c`：组件未找到返回 `-ENOENT` | 模型不细分 errno，统一 -1 |
| `/etc/motd/child` → -1 | `fs/namei.c`：`walk_component` 发现当前是普通文件返回 `-ENOTDIR` | 模型不检查 `ramfs_nodes[].type`，靠整串 miss 表达 |
| `ramfs_lookup` 计数 hits/misses | `fs/dcache.c`：dcache 命中/未命中统计（`d_lookup` 结果） | 模型无真实缓存竞争窗口，统计仅为教学展示 |
| `shell_exec_path` 解析失败即拒绝执行 | `fs/exec.c`：`do_execve` → `getname_kernel`/`open_exec`，失败返回 errno | 模型用 `inode<0` 一条防线，无权限/格式细分 |
| `l92test` 断言 | 无直接对应（LTP `fs` 测试套件） | 模型把解析边界验证固化进内核 |

**权威来源**：Linux `fs/namei.c`、`fs/dcache.c`、`fs/fs_struct.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么教学模型拒绝 `relative` 相对路径，而 Linux 能解析它？结合 `fs_struct` 的 `root`/`pwd` 说明差在哪。
2. **源码定位**：在 `kernel64.c` 中找出 `ramfs_lookup` 的入口守卫与五个分支，为每种输入标注「对应 Linux 的哪个阶段（path_init / link_path_walk / 组件查找 / 错误返回）」。
3. **动手实验**：给 `ramfs_lookup` 增加对 `/bin/sh/args` 的解析（把 `/bin/sh` 之后的部分当作 argv 一并拒绝），运行 `pathtest` 观察新增边界是否生效。
4. **动手实验**：去掉 `ramfs_lookup` 末尾的 `ramfs_misses++`，运行 `pathtest` 后 `ramfsinfo`，观察 misses 计数停滞而 hits 不变。
5. **Linux 对照**：阅读 `fs/namei.c` 的 `link_path_walk`，对比教学模型的整串匹配，指出 Linux 在每级组件上做了什么模型里完全没有的事（如 `..`、挂载穿越、符号链接解析）。

---

## 9. 本课小结与下一课预告

1. 本课把 VFS 阶段从「静态结构」推进到「动态解析」：`ramfs_lookup` 是教学内核的路径解析器。
2. 解析分四步：起点校验（绝对路径）→ 根特判 → 整串查表 → 边界拒绝，对照 Linux `path_init` → `link_path_walk` 的骨架。
3. 「遍历边界」是路径空间的五个约束：起点、存在性、类型、容量、深度；`pathtest` 的 3 个负向用例覆盖了前三个。
4. 教学模型把逐组件 walk 压缩成整串匹配，代价是丢失 `-ENOENT`/`-ENOTDIR` 的细分与 `..`/符号链接能力——这是有意的教学简化。
5. 解析失败统一返回 -1，被 `shell_exec_path` 的第一道防线拦截，保证「解析不过就不执行」。
6. `l92test` 沿用 VFS/设备阶段检查点家族，`l84test` 历史检查点保留。
7. 下一课（[`../lesson-93-stable/README.md`](../lesson-93-stable/README.md)，Lesson 93）将视野从单条路径提升到整个命名空间，讲 **mount namespace 元数据**（对照 `fs/mount.c`、`fs/namespace.c`）。
