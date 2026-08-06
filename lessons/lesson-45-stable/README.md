# Lesson 45: Linux 风格 ramfs/initramfs 与最小 VFS 路径查找 — 精讲文档

> **课号**：Lesson 45（可执行课）
> **主题**：Linux 风格 ramfs/initramfs 与最小 VFS 路径查找
> （bounded VFS path lookup）
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」第三课。前课（44）
> 建立 fd/file/inode/dentry 对象链；本课把 lesson-44 的静态 dentry
> 「名字→inode」映射升级为一块**内存后援的 ramfs 节点树**（`/`、`/etc`、
> `/etc/motd`、`/bin`、`/bin/sh`）与按整串路径判定命中/未命中的
> 最小路径查找模型。
> **前置课程**：[`lesson-44-stable/README.md`](../lesson-44-stable/README.md)
> **后续课程**：[`lesson-46-stable/README.md`](../lesson-46-stable/README.md)
> （管道、阻塞 I/O 与 poll/wait 机制）
> **一句话目标**：能讲清 Linux 的 ramfs 为什么是「内存里的一棵 inode 树」、
> initramfs 早期根文件系统如何接管启动、`fs/namei.c` 怎么把
> `/etc/motd` 拆成「/ → etc → motd」逐级解析，并在 TinyOS 里复刻
> 整串路径的命中/未命中判定——**不解引用用户指针、不做块设备 I/O**。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——ramfs 是把内存当磁盘的简单文件系统
（文件内容就在 `struct page` 里，无真实落盘）；initramfs 是引导早期
挂在根上的临时内存根文件系统；路径查找是「从 dentry 根逐级沿 parent
链往下走」的过程。TinyOS 用 `ramfs_nodes[RAMFS_MAX=6]` 固定节点数组
预置一棵 5 节点树，`ramfs_lookup` 对已知绝对路径返回 dentry 下标、
对缺失/相对/穿越普通文件的路径返回 -1，`pathtest` 用 7 个用例验证。

- **在课程主线中的位置**：44 给 VFS 打了「对象」地基，45 给 VFS 补上
  「名字空间」。从 45 之后，fd 就有了可被 `open("/etc/motd")` 解析出的
  对象语义；46 的管道/48 的 timerfd 都是「特殊文件」——它们的节点同样
  由路径查找入口进入，只是读写行为不同。
- **前置知识清单**：
  1. lesson-44 的 `inode_table`/`dentry_table` 与 `vfs_init` 预置流程；
  2. Linux 路径语义：绝对路径以 `/` 开头、`.` 指当前目录、不能穿越
     普通文件（`ENOTDIR`）；
  3. 小端 ASCII 的内存表示：`"etc"` 三个字节 `0x65 0x74 0x63` 存进 u64
     是 `0x657463`；
  4. `eq64` 字符串比较函数（TinyOS 自带的定长比较，不用 libc `strcmp`）。
- **本课交付**：`ramfsinfo`/`pathtest` 两条命令；`ramfs_node` 结构与
  `ramfs_init`/`ramfs_lookup` 两个函数；三个查找计数器
  （lookups/hits/misses）；`vfs_init` 尾部挂接 `ramfs_init()`。

---

## 2. 核心概念精讲

### 2.1 概念一：ramfs——把内存当磁盘的文件系统

**定义**：ramfs 是一种把页缓存当作全部存储的文件系统：文件的数据页
`struct page` 既是「缓存」也是「磁盘」本身，写文件就是在改内存，
删文件就是释放页，没有第二层落盘。**为什么需要**：教学上它是理解
「文件 = inode + 页」的最小模型；真实 Linux 里 ramfs 极轻量，
`fs/ramfs/inode.c` 只有几百行。**TinyOS 对应**：`ramfs_nodes` 数组的每个
`ramfs_node { name_hash, parent, inode, type, valid }` 就是一棵
「名字→父节点→inode 索引」的最小 ramfs 树——`type` 区分目录与普通文件，
`inode` 指向 lesson-44 的 `inode_table`。

### 2.2 概念二：initramfs——引导期的临时根

**定义**：initramfs 是 Linux 引导早期由 bootloader 交给内核的一个 cpio
归档，内核在挂载真正根文件系统前先把它解包成内存里的根
（`init/initramfs.c` 的 `populate_rootfs`）。**为什么需要**：真正的根在
磁盘驱动尚未就绪时读不到，先用内存根启动最小环境（比如加载驱动）。
**TinyOS 对应**：本课没有解析真实 cpio 归档，但 `ramfsinfo` 的头行
`ramfs/initramfs nodes: ...` 把这块内存树命名为 initramfs 风格的
启动期根——`root=dentry0 memory-backed` 明确「根是内存后援的」。

### 2.3 概念三：路径查找（path lookup）——从根逐级往下走

**定义**：Linux `fs/namei.c` 的 `path_lookupat` 把绝对路径按 `/` 切成
分量，从 `d_parent` 根 dentry 出发逐级 `walk_component`，每级用
`d_lookup` 在孩子里按名找下一个 dentry。**为什么需要**：一棵树上找文件，
只能从根沿着名字逐级走。**TinyOS 对应**：`ramfs_lookup` 不做逐分量
解析，而是对**整串已知路径**直接命中——这是对 namei 的「判定结果」
简化：`/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh` 各返回其 dentry 下标
（0/1/2/3/4），其余路径全部 miss。树结构（parent 字段）仍在 `ramfsinfo`
里可观察，但查找接口是整串匹配。

### 2.4 概念四：三类必败路径——缺失/相对/穿越普通文件

**为什么需要**：路径查找的正确性不只靠「命中已知路径」，还要正确拒绝
非法输入。本课 `pathtest` 覆盖三类必败：
1. **缺失路径** `/etc/missing`：树里没有这个孩子 → miss；
2. **相对路径** `relative`：不以 `/` 开头 → 直接拒绝（本课模型只认
   绝对路径，等价于「没有当前工作目录」）；
3. **穿越普通文件** `/etc/motd/child`：motd 是普通文件，不能当目录继续
   下钻 → 对应 Linux `ENOTDIR`。
三者都返回 -1，`pathtest` 用 `f<0&&g<0&&h<0` 断言。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-44） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：`ramfs_node` 结构 + `ramfs_init`/`ramfs_lookup`/`ramfsinfo`/`pathtest`；`vfs_init` 尾部调用 `ramfs_init()` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `ramfs`、`initramfs`、`path lookup`；kernel64.c 含 `ramfsinfo`、`pathtest` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 `TinyOS lesson 45: ramfs, initramfs, and VFS path lookup` |

### 3.2 结构 / 宏 / 全局变量精讲

```c
#define RAMFS_MAX 6U
#define RAMFS_ROOT 0U
#define RAMFS_DIR 1U
#define RAMFS_FILE 2U
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static struct ramfs_node ramfs_nodes[RAMFS_MAX];
static u32 ramfs_count;
static u64 ramfs_lookups,ramfs_hits,ramfs_misses;
```

逐行注释：
- `RAMFS_MAX=6`：节点数组容量；本课只用前 5 个（`ramfs_count=5`），
  留 1 槽余量体现「有界」；
- `RAMFS_ROOT=0`/`RAMFS_DIR=1`/`RAMFS_FILE=2`：节点类型枚举——根是
  「既是目录又是根」的特殊身份（节点 0 的 type 用 `RAMFS_DIR`），
  目录与普通文件用 type 区分；
- `struct ramfs_node`：`name_hash`（名字的小端 ASCII 值）、`parent`
  （父节点下标，根指向自己 0）、`inode`（指向 lesson-44 `inode_table`
  的索引）、`type`、`valid`；
- 全局量：`ramfs_nodes[6]` 静态数组、`ramfs_count` 活节点数、
  三个查找计数器 lookups/hits/misses。

### 3.3 函数精讲：ramfs_init —— 用复合字面量种一棵 5 节点树

```c
static TEXT64 void ramfs_init(void){ramfs_count=5;
ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};
ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};
ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};
ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};
ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};
ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```

逐行分析：
1. **根节点**（第二行）：`{0,0,0,RAMFS_DIR,1}`——`name_hash=0`（根没有
   名字）、`parent=0`（根的父亲是自己）、`inode=0`、目录、有效。对应
   Linux 的 `/`（`&rootfs->d_inode` 的 sb 根 inode）；
2. **/etc**（第三行）：`{0x657463,0,1,RAMFS_DIR,1}`——名字哈希
   `0x657463` 是 `"etc"` 三字节 `0x65('e') 0x74('t') 0x63('c')` 的小端
   拼接；`parent=0` 挂在根下；`inode=1`；
3. **/etc/motd**（第四行）：`{0x6d6f7464,1,2,RAMFS_FILE,1}`——哈希
   `0x6d6f7464` = `"motd"`；`parent=1`（父亲是节点 1，即 etc）；
   `inode=2`、普通文件；
4. **/bin**（第五行）：`{0x6e6962,0,1,RAMFS_DIR,1}`——`"bin"` 的
   小端哈希；挂根下。**注意**：`inode=1` 与 /etc 相同——本课 inode 索引
   是「内容身份」的教学简化，两个目录共享 inode 1（详见 §7）；
5. **/bin/sh**（第六行）：`{0x6873,3,2,RAMFS_FILE,1}`——`"sh"` 哈希、
   `parent=3`（bin）、`inode=2`（与 motd 共享 inode 2 的又一处简化）、
   普通文件；
6. 末行把三个查找计数器归零，保证可重复断言。

### 3.4 函数精讲：ramfs_lookup —— 整串路径的命中/未命中

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;
ramfs_lookups++;
if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}
if(eq64(path,"/etc")){ramfs_hits++;return 1;}
if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}
if(eq64(path,"/bin")){ramfs_hits++;return 3;}
if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}
ramfs_misses++;return -1;}
```

逐行分析：
1. **入口拒绝**（第一行）：`!path`（空指针）或 `path[0]!='/'`（相对路径）
   直接 `return -1`——**注意这发生在 `ramfs_lookups++` 之前**，所以
   相对路径不计入 lookups 统计；
2. **根与当前目录**（第三行）：`/` 与 `.` 都返回 0（根 dentry）——
   `.` 是 POSIX 规定的「当前目录」别名，本课模型把它映射到根；
3. **四个已知路径**（第四至七行）：`/etc`→1、`/etc/motd`→2、`/bin`→3、
   `/bin/sh`→4，每个命中都 `ramfs_hits++`。返回值即
   `ramfs_nodes[]` 的下标——`/bin/sh` 返回 4 正是节点 4；
4. **未命中记账**（第八行）：已知表之外的路径 `ramfs_misses++` 并
   返回 -1——包括缺失路径与「穿越普通文件」路径，二者共用 miss 计数；
5. 教学简化说明：Linux namei 会逐分量拆解并沿 parent 链走树，
   本模型直接比较整串（`eq64` 定长比较），但**返回的 dentry 下标、
   命中/未命中判定**与 namei 的最终结果一致。

### 3.5 函数精讲：ramfsinfo / pathtest

```c
static TEXT64 void ramfsinfo(u16*c){u32 i;
text64(c,"ramfs/initramfs nodes: ");hex64(c,ramfs_count);
text64(c," root=dentry0 memory-backed\npaths: / /etc /etc/motd /bin /bin/sh\nlookups/hits/misses: ");
hex64(c,ramfs_lookups);text64(c," ");hex64(c,ramfs_hits);text64(c," ");hex64(c,ramfs_misses);
putc64(c,'\n');
for(i=0;i<ramfs_count;i++){text64(c,"node ");hex64(c,i);text64(c," parent ");
hex64(c,ramfs_nodes[i].parent);text64(c," inode ");hex64(c,ramfs_nodes[i].inode);
text64(c," ");text64(c,ramfs_nodes[i].type==RAMFS_DIR?"dir":"file");putc64(c,'\n');}}
static TEXT64 void pathtest(u16*c){int a=ramfs_lookup("/"),b=ramfs_lookup("/etc"),
d=ramfs_lookup("/etc/motd"),e=ramfs_lookup("/bin/sh"),f=ramfs_lookup("/etc/missing"),
g=ramfs_lookup("relative"),h=ramfs_lookup("/etc/motd/child");
text64(c,"pathtest: ");text64(c,a==0&&b==1&&d==2&&e==4&&f<0&&g<0&&h<0?
"ramfs/initramfs root-to-dentry path lookup passed":"BROKEN");putc64(c,'\n');}
```

- `ramfsinfo`：先打印节点数与内存后援声明（`root=dentry0 memory-backed`）、
  已知路径列表与三计数器，再逐节点打印 `node <i> parent <p> inode <n>
  <dir|file>`——把整棵树的 parent 链展现在屏幕上，可直观验证
  `/etc/motd`（parent 1）挂在 `/etc`（parent 0）下；
- `pathtest`：7 个断言——4 个命中（`/`→0、`/etc`→1、`/etc/motd`→2、
  `/bin/sh`→4）+ 3 个必败（`/etc/missing` 缺失、`relative` 非绝对、
  `/etc/motd/child` 穿越普通文件），全部通过才输出 passed 串；
- 计数推导：一次 `pathtest` 后 `lookups=6`（`"relative"` 在计数前就被
  拒绝）、`hits=4`、`misses=2`。

### 3.6 vfs_init 挂接、exec64 分支、kernel_main、grub.cfg 与 Makefile

`vfs_init` 尾部新增 `ramfs_init()`（源码逐字，仅列尾部）：

```c
static TEXT64 void vfs_init(void){u32 i; ... fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();}
```

——lesson-44 的 VFS 表初始化完毕后再种 ramfs 树，`kernel_main64_binary`
无需新增调用。`exec64` 新增两个分支（源码逐字）：

```c
else if(eq64(word,"ramfsinfo")){if(!noargs64(arg))usage64(c,"ramfsinfo");else ramfsinfo(c);}
else if(eq64(word,"pathtest")){if(!noargs64(arg))usage64(c,"pathtest");else pathtest(c);}
```

**源码事实（必须知悉）**：
- 开机横幅与 `about` **仍是 lesson-43 文案**（源码逐字）：
  `TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
  / `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`
  ——本课横幅/`about` 两处未同步，但 **grub.cfg 已更新**为
  `TinyOS lesson 45: ramfs, initramfs, and VFS path lookup`；
- `help` 文案仍未列出 `ramfsinfo`/`pathtest`（命令仍可用）；
- Makefile `check` 目标新增 grep（README 三关键词 + kernel64.c 两符号）：

```make
@grep -q 'ramfs' README.md
@grep -q 'initramfs' README.md
@grep -q 'path lookup' README.md
@grep -q 'ramfsinfo' kernel64.c
@grep -q 'pathtest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 45 checks passed.'
```

（注意：README 必须含字面 `path lookup` 子串，本课各节已多处出现。）

### 3.7 主控制流

```text
kernel_main64_binary
  ├─ pmm_init → vma_init → reclaim_init → vfs_init ──┐
  │      vfs_init 尾部调用 ramfs_init() ◄─────────────┘
  ├─ 横幅（源码仍为 lesson-43 文案，见 §3.6 源码事实）
  └─ 键盘循环 → exec64：
        ramfsinfo / pathtest / fdinfo / fdtest / anoninfo / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "pathtest"
  ├─ ramfs_lookup("/")            → 命中 0
  ├─ ramfs_lookup("/etc")         → 命中 1
  ├─ ramfs_lookup("/etc/motd")    → 命中 2
  ├─ ramfs_lookup("/bin/sh")      → 命中 4
  ├─ ramfs_lookup("/etc/missing") → miss -1
  ├─ ramfs_lookup("relative")     → path[0]!='/' 直接 -1（不计数）
  └─ ramfs_lookup("/etc/motd/child") → miss -1（穿越普通文件）
  → "pathtest: ramfs/initramfs root-to-dentry path lookup passed"

输入 "ramfsinfo"（pathtest 之后）
  → "ramfs/initramfs nodes: 0000000000000005 root=dentry0 memory-backed"
  → "paths: / /etc /etc/motd /bin /bin/sh"
  → "lookups/hits/misses: 0000000000000006 0000000000000004 0000000000000002"
  → "node 0000000000000000 parent 0000000000000000 inode 0000000000000000 dir"
  → "node 0000000000000001 parent 0000000000000000 inode 0000000000000001 dir"
  → "node 0000000000000002 parent 0000000000000001 inode 0000000000000002 file"
  → "node 0000000000000003 parent 0000000000000000 inode 0000000000000001 dir"
  → "node 0000000000000004 parent 0000000000000003 inode 0000000000000002 file"
```

（注意 lookups=6 而非 7：`"relative"` 在 `ramfs_lookups++` 之前被拒绝。
若再跑一次 `pathtest`，lookups/hits/misses 分别累加 6/4/2。）

---

## 5. 构建、运行与验证

### 5.1 依赖

同旧课：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and lesson 45 checks passed.`（要求 README 含
`ramfs`、`initramfs`、`path lookup`，kernel64.c 含 `ramfsinfo` 与
`pathtest`，缺一即失败；旧 README 里的 `fs/ramfs/inode.c`/
`init/initramfs.c`/`fs/namei.c`/`fs/dcache.c` 引用在 §7 中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
显示的是 lesson-43 文案——源码未同步，见 §3.6）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
pathtest
```

预期：`pathtest: ramfs/initramfs root-to-dentry path lookup passed`

```bash
ramfsinfo
```

预期（一次 `pathtest` 之后）：

```text
ramfs/initramfs nodes: 0000000000000005 root=dentry0 memory-backed
paths: / /etc /etc/motd /bin /bin/sh
lookups/hits/misses: 0000000000000006 0000000000000004 0000000000000002
node 0000000000000000 parent 0000000000000000 inode 0000000000000000 dir
node 0000000000000001 parent 0000000000000000 inode 0000000000000001 dir
node 0000000000000002 parent 0000000000000001 inode 0000000000000002 file
node 0000000000000003 parent 0000000000000000 inode 0000000000000001 dir
node 0000000000000004 parent 0000000000000003 inode 0000000000000002 file
```

继承回归：`fdtest`/`fdinfo`（lesson-44 回归）、`anoninfo`/`reclaimtest`
（lesson-43 回归）行为一致；真实 `#PF` 命令 `pftest`/`isttest`/
`stackguardtest` 保持致命停机。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 45 checks passed.`；`pathtest`
7 断言通过；`ramfsinfo` 显示 5 节点树与 lookups/hits/misses=6/4/2；
`fdtest`/`anoninfo` 与旧课一致。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `ramfs`/`initramfs`/`path lookup`，或 kernel64.c 缺 `ramfsinfo`/`pathtest` | 对照 Makefile check 的 grep 列表（注意 `path lookup` 是含空格子串） |
| `pathtest` 输出 `BROKEN` | 7 个断言中一个不成立 | 逐项核对：`/`→0、`/etc`→1、`/etc/motd`→2、`/bin/sh`→4、三必败<0 |
| `ramfsinfo` 的 lookups 是 6 而不是 7 | `"relative"` 不以 `/` 开头，在 `ramfs_lookups++` 之前就被拒绝（源码事实） | 对照 `ramfs_lookup` 首行：`if(!path||path[0]!='/')return -1;` |
| 想看到 hits 为 5 或更多 | 命中只在 4 个已知路径上加 | 已知路径表是 `/ /etc /etc/motd /bin /bin/sh` 五个、其中根+四路径共 5 个可命中入口，但 `pathtest` 只调 4 个命中用例 |
| 节点 1 与 3 共享 inode 1、节点 2 与 4 共享 inode 2 | 教学简化：inode 字段是「内容身份」索引而非唯一文件身份（源码事实） | 对比 `ramfs_init` 的 5 行复合字面量 |
| 横幅/`about` 显示 lesson-43 | 本课横幅/`about` 未同步（源码事实），grub.cfg 已同步 | 对照 lesson-45 kernel64.c 字符串与 grub.cfg menuentry |
| 担心「真解析了 cpio 或读了磁盘」 | 设计保证内存后援 | `ramfs_init` 只写静态数组；`ramfs_lookup` 只用 `eq64` 比较字符串 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `fs/ramfs/inode.c`、`init/initramfs.c`、
`fs/namei.c`、`fs/dcache.c`、`include/linux/fs.h`**（延续 lesson-44 的
VFS 对象对照线）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `ramfs_nodes[]` 内存后援节点树 | `fs/ramfs/inode.c`：ramfs 的 inode 建在内存里、文件数据是页缓存页 | 教学模型只有元数据数组，无 `struct address_space` 数据页 |
| `root=dentry0 memory-backed` | `init/initramfs.c` 的 `populate_rootfs` 把 cpio 解到内存根 | 教学模型不解析 cpio 归档，只保留「内存根」概念与命名 |
| `ramfs_lookup` 整串路径命中/未命中 | `fs/namei.c` 的 `path_lookupat`/`walk_component`（按 `/` 切分量逐级 `d_lookup`） | **教学模型省掉逐分量遍历**，只对已知整串路径判定；返回值等价 namei 的 dentry 下标 |
| `name_hash` 小端 ASCII 值 | `fs/dcache.c` 的 `d_name` + `dentry_hash`（`d_hash` 哈希名字） | 教学模型直接存整值代替哈希函数，`parent` 代替 `d_parent` |
| `pathtest` 的 `/etc/motd/child` 返回 -1 | `fs/namei.c` 在中间分量是普通文件时返回 `-ENOTDIR` | 教学模型对「穿越普通文件」整串 miss，语义一致但无错误码 |
| `pathtest` 的 `relative` 返回 -1 | `path_lookupat` 对相对路径用 `dfd` 当前目录继续解析 | 教学模型无当前目录概念，非绝对路径直接拒绝 |
| `inode` 索引指向 lesson-44 `inode_table` | `struct dentry->d_inode` 指向 `struct inode` | 教学模型共享 inode 索引（/etc 与 /bin 同 inode 1）是「内容身份」简化 |
| 固定 `RAMFS_MAX=6`/5 个节点 | ramfs 树随内容动态增长 | 有界是教学刻意简化 |

**权威来源**：POSIX 路径名语义（绝对路径、`.`、`ENOTDIR`）、
Linux `Documentation/filesystems/ramfs-rootfs-initramfs.txt`。

**教学模型简化了什么**：
1. 无逐分量解析：不切 `/`、不沿 parent 链行走，整串 `eq64` 比较；
2. 无 cpio 解包：initramfs 只有命名，没有真实归档；
3. 无 `ENOENT`/`ENOTDIR` 错误码区分：全部归并为 `-1`；
4. 无 inode 缓存/目录项缓存生命周期：表是静态的；
5. 不执行解析出的文件：`/bin/sh` 只是元数据，不是可运行程序。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `/etc/motd/child` 必须失败？如果它成功了，
   Linux 的哪些安全/正确性语义会被破坏？（提示：`ENOTDIR`。）
2. **源码定位**：`ramfs_lookup` 里 `ramfs_lookups++` 为什么放在
   `path[0]!='/'` 检查**之后**？把递增移到函数第一行会怎样影响
   `ramfsinfo` 的 lookups 计数？
3. **动手实验**：在 `ramfs_init` 里新增一个节点 5
   `{0x656d707479,1,2,RAMFS_FILE,1}`（"empty"）挂在 /etc 下，并在
   `ramfs_lookup` 里加一行 `if(eq64(path,"/etc/empty")){ramfs_hits++;return 5;}`
   和 `ramfs_count=6`，重建后看 `ramfsinfo`/`pathtest` 的变化，然后还原。
4. **Linux 对照**：打开 `fs/namei.c` 的 `walk_component`，找出它为
   `ramfs_lookup` 省略掉的至少 4 项工作
   （提示：`d_lookup`、`follow_managed`、`path_connected`、`inode_permission`）。
5. **设计思考**：本课 inode 索引在 /etc 与 /bin 间共享（都是 1）。
   若要改成「每个节点独立 inode」，需要动 lesson-44 的哪些常量？
   （提示：`INODE_MAX=3` 不够 5 个节点用。）

---

## 9. 本课小结与下一课预告

**小结**：本课用 `ramfs_nodes[6]` 静态数组种了一棵 5 节点内存后援树
（`/`、`/etc`、`/etc/motd`、`/bin`、`/bin/sh`），以
`name_hash/parent/inode/type` 模拟 Linux ramfs 的「目录/文件 + 名字 +
父节点」结构；`ramfs_lookup` 对已知绝对路径返回 dentry 下标并对
缺失/相对/穿越普通文件的路径返回 -1；`pathtest` 用 4 命中 + 3 必败
共 7 个断言验证，`ramfsinfo` 把树的 parent 链与 lookups/hits/misses
展现在屏幕。本课无 cpio 解析、无块设备 I/O；横幅与 `about` 仍是
lesson-43 文案（源码未同步），但 grub.cfg 菜单已更新为 lesson-45。
「路径查找」至此把名字空间补进了 VFS 模型。

**下一课预告**：lesson-46 将在 fd/VFS 之上实现 Linux 风格**管道、阻塞 I/O
与 poll/wait 机制**（对照 `fs/pipe.c`、`fs/select.c`）：固定容量环形缓冲、
空读阻塞/满写阻塞的状态转移、`POLLIN`/`POLLOUT` 就绪判定。届时会复用
本课的「节点类型」思想——管道是另一种「特殊文件」，路径/节点语义继续延伸。
