# Lesson 97: 目录读取与固定缓冲区 — 精讲文档

> **课号**：Lesson 97 ｜ **主题**：目录读取与固定缓冲区（directory reading and fixed buffers）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 90 原型的检查点
> **前置课程**：[`../lesson-96-stable/README.md`](../lesson-96-stable/README.md)（文件偏移与引用计数）
> **后续课程**：[`../lesson-98-stable/README.md`](../lesson-98-stable/README.md)（字符设备注册）
> **一句话目标**：以 ramfs 目录树为对象精讲「目录读取」（路径→dentry 解析、目录表列出）与「固定缓冲区」（定长表、有界命令/令牌缓冲）两大主题，并验证 `l97test` 检查点与继承的 VFS/设备回归。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l96test` 恢复为历史命名 `l89test`（挂在 `lesson_89_state` 上）、新增 `lesson_90_model` 状态与 `l97test` 检查点、更新 `about`/开机横幅为本课主题。目录读取与固定缓冲区机制由累积代码承载：`ramfs_nodes[]` 目录树与 `ramfs_lookup` 路径解析来自 VFS 阶段（Lesson 88–96），固定容量数组与 `token64`/`cmd[32]` 有界缓冲贯穿全内核。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l97test`（旧 README 写的 `l90test` 是 Lesson 90 的命令，此处按源码勘误）；另保留历史检查点 `l82test`–`l89test`，及 `pathtest`/`ramfsinfo`/`shellrun` 等路径与文件回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：解释内核如何把 `"/etc/motd"` 这样的路径解析到目录树中的某个 dentry（对照 Linux `fs/namei.c`）；读懂 ramfs 目录表（`ramfs_nodes[]`）的 parent/inode/type 布局并说明 `ramfsinfo` 为何等价于「目录列出」；说出一组固定缓冲区（`cmd[32]`、`word[24]`、`RAMFS_MAX=6`、`INODE_MAX=3` 等）各自的上界检查在哪一行；运行 `l97test`/`pathtest`/`ramfsinfo`/`shellrun` 验证。

**在课程主线中的位置**：Lesson 88 搭 VFS 层次、Lesson 89 讲超级块注册、Lesson 90–96 依次深入 inode/dentry/路径/偏移等机制，本课进入「检查点课」序列（Lesson 91 起每隔一段落一个检查点）：源码 diff 极小，任务是把继承机制中与主题相关的部分（目录读取、固定缓冲区）系统化复述，并用 `l97test` 把该主题的可验证状态固化。下一课（Lesson 98）主题转向字符设备注册。

**前置知识清单**（学本课前必须掌握）：
1. ramfs 目录树与路径解析：`ramfs_init` 的 5 节点布局、`ramfs_lookup` 的命中/未命中统计（Lesson 52/88–93）。
2. VFS 四层模型：inode/dentry/file/fd 表及 `fd_open_model`/`fd_close_model` 的引用链（Lesson 88–96）。
3. `token64`/`noargs64`/`eq64` 的字符串与命令解析约定（Lesson 5–7）。
4. 检查点模型 `struct lesson_YY_model` 的四 `u32` 计数 + 四 `u8` 布尔位范式（Lesson 69–96）。
5. 命令循环 `kernel_main64_binary` 的 `cmd[32]` 有界输入与 `exec64` 分发（Lesson 5/97）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 97: 目录读取与固定缓冲区`；
- 新命令 `l97test` 输出 `l97test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `pathtest`/`ramfsinfo` 展示目录树路径解析与目录列出，`shellrun` 展示「目录读取 → 打开 → 关闭」的完整调用链。

---

## 2. 核心概念精讲

### 2.1 目录读取：目录树、路径解析与目录列出

**直觉**：把文件系统想成一棵「目录 → 子目录/文件」的树。用户给的 `"/etc/motd"` 是**从根往下走的路径**；内核要「读取目录」才能知道每一层目录里有什么子项。Linux 把这件事拆成两个能力：

1. **路径解析（lookup）**：从根 dentry 出发，逐个组件查目录，直到最后一个组件——返回目标 dentry（`fs/namei.c` 的 `link_path_walk`/`walk_component`）。
2. **目录读取（readdir）**：给定一个目录的 file 与 buffer，把目录项（name、d_type、ino）逐个填进用户缓冲区（`fs/readdir.c` 的 `iterate_dir`，`fs/libfs.c` 的 `dcache_readdir`）。

教学模型用两张面分别模拟：
- **`ramfs_nodes[]` 定长目录树**：`struct ramfs_node { name_hash, parent, inode, type, valid }`。`type` 区分 `RAMFS_DIR`/`RAMFS_FILE`，`parent` 建立树边，`name_hash` 模拟目录项名。这就是「目录读取」的对象。
- **`ramfs_lookup(path)` 路径解析**：把完整路径串映射到节点下标；`ramfsinfo` 遍历打印整棵目录树（`node/parent/inode/dir-or-file`），等价于一次「目录列出」。

### 2.2 固定缓冲区：为什么教学内核处处有界

**直觉**：真实内核面对不可信的用户输入与海量对象，必须假设任何请求都可能超长。没有上界检查的 `strcpy`/`memcpy` 是缓冲区溢出漏洞的根源。教学内核的选择是**全盘定长**：

| 缓冲/表 | 容量 | 上界检查位置 |
|---|---|---|
| 命令输入 `cmd[32]` | 31 有效字符 | `kernel_main64_binary` 中 `else if(n<31)` |
| 命令令牌 `word[24]` | 23 有效字符 | `token64` 中 `if(n+1>=cap)return 0;` |
| ramfs 目录树 `ramfs_nodes[]` | `RAMFS_MAX=6` | 全部 `for` 循环按常量遍历 |
| inode/dentry/file/fd 表 | `INODE_MAX=3`/`DENTRY_MAX=3`/`FILE_MAX=3`/`FD_MAX=4` | `fd_open_model` 等函数内逐项 `valid` 检查 |
| 用户拷贝上限 | `USER_COPY_MAX=256` | `uaccess_validate` 的 `length<=USER_COPY_MAX` |
| 文件读取量 | inode 剩余大小 | `fd_read_model` 的 `if(bytes>remaining)bytes=remaining;` |

**关键不变量**：**任何写入定长缓冲的路径都先经过容量校验**。这在 `token64`（命令解析第一道防线）与 `fd_read_model`（读文件不得超过剩余字节）处表现得最直接——「固定缓冲区」不是约定，而是每个写入点的强制检查。

### 2.3 检查点模型：lesson_90_model 与 l97test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位（`valid/active/ready/accounted`），计数串 `90→93` 标记 Origin 为 Lesson 90。成功串沿用 VFS/设备阶段主题。本课同时把上一课被临时改名的 `l96test` 恢复为历史命名 `l89test`（同一 `lesson_89_state`，计数 `89→92`），体现「检查点课之间的命名整理」：`lXXtest` 的命令名尽量与其 Origin 课号一致。

### 2.4 机制继承 + 检查点增量

本课主题机制（目录读取、固定缓冲区）**不是本课新写的代码**，而是累积代码（来自 Lesson 52 的 ramfs、Lesson 88–96 的 VFS 层次与路径解析、以及自 Lesson 5 起的有界命令解析）。本课的实际增量只有三处：`l96test`→`l89test` 更名、`lesson_90_model`+`l97test`、横幅与 `about`。精讲时以「机制继承 + 检查点增量」的视角，把继承代码按本课主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l96test`→`l89test` 恢复命名；新增 `lesson_90_model`/`lesson_90_state`/`l97test`；`about` 与开机横幅更新。目录读取/固定缓冲区机制由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`目录读取与固定缓冲区`/`l97test`/`Lesson 97`） |
| `grub.cfg` | GRUB menuentry（仍写 "lesson 52" 标题） | 未变化 |

### 3.2 kernel64.c（目录读取/固定缓冲区机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_90_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_90_model lesson_90_state;
static TEXT64 void l97test(u16*c){lesson_90_state=(struct lesson_90_model){90U,91U,92U,93U,1,1,1,1};int ok=lesson_90_state.valid&&lesson_90_state.active&&lesson_90_state.ready&&lesson_90_state.accounted&&lesson_90_state.b==lesson_90_state.a+1U;text64(c,"l97test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 90 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `90→93`（Origin Lesson 90），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l97test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 90 fallback reported`。
3. **恢复的 `l89test`**：本课同时把 `l96test` 更名回 `l89test`（同为 `lesson_89_state`，计数 `89→92`），使检查点命令名与其 Origin 课号对齐；`l82test`–`l88test` 历史检查点全部保留。

#### 3.2.2 目录树数据结构：ramfs_nodes

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
1. **定长目录表**：`RAMFS_MAX=6` 槽位，`ramfs_count` 记录实际节点数——「固定缓冲区」的第一个实例。
2. **节点字段**：`name_hash`（目录项名的哈希模拟）、`parent`（指向父节点下标，构成树边）、`inode`（指向 `inode_table` 的 inode 下标，连接 VFS 四层）、`type`（`RAMFS_ROOT`/`RAMFS_DIR`/`RAMFS_FILE`）、`valid`。
3. **统计三件套**：`ramfs_lookups/hits/misses` 记录路径解析的命中/未命中——可观察的「目录读取」计数器。

#### 3.2.3 目录树初始化：ramfs_init

```c
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
```
1. **五节点树**：节点 0 根（`name_hash=0`、`parent=0`、`inode=0`、`RAMFS_DIR`）；节点 1 `/etc`（`name_hash=0x657463`=小端 "cte"→"etc"、`parent=0`、`inode=1`）；节点 2 `/etc/motd`（`0x6d6f7464`="motd"、`parent=1`、`inode=2`、`RAMFS_FILE`）；节点 3 `/bin`（`0x6e6962`="bin"、`parent=0`、`inode=1`）；节点 4 `/bin/sh`（`0x6873`="sh"、`parent=3`、`inode=2`、`RAMFS_FILE`）。
2. **树边语义**：`/etc` 与 `/bin` 的 `parent=0`（根的直接子目录）；`/etc/motd` 与 `/bin/sh` 分别挂在 `/etc` 与 `/bin` 之下——`parent` 字段就是「目录读取」要遍历的树结构。
3. **inode 共享**：两个目录节点（1、3）都指向 `inode=1`，两个文件节点都指向 `inode=2`——教学模型用少量 inode 复用来演示「不同路径可指向同一 inode」，对应 Linux 的硬链接概念。

#### 3.2.4 路径解析（目录读取的核心）：ramfs_lookup

```c
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
```
1. **前置校验**：空指针或非 `/` 开头（相对路径）直接 `-1`——对应 Linux 只接受从根开始解析的绝对路径这一约定（`link_path_walk` 的初始 `nd->path` 必须是挂载根或起始目录）。
2. **命中路径**：五个已知绝对路径逐一比较，命中则 `ramfs_hits++` 并返回节点下标（`/`→0、`/etc`→1、`/etc/motd`→2、`/bin`→3、`/bin/sh`→4）。
3. **未命中**：`/etc/missing` 等任何不在表内的路径落入 `ramfs_misses++` 并返回 `-1`。
4. **为什么这样设计**：教学模型用「整串匹配」代替「逐组件解析」，把 `fs/namei.c` 的路径分解省略为黑盒——但保留了**查找的判定语义**（命中/未命中）与**统计可观察性**（hits/misses），供 `pathtest` 做确定性断言。

#### 3.2.5 目录列出：ramfsinfo

```c
static TEXT64 void ramfsinfo(u16*c){u32 i;text64(c,"ramfs/initramfs nodes: ");hex64(c,ramfs_count);text64(c," root=dentry0 memory-backed\npaths: / /etc /etc/motd /bin /bin/sh\nlookups/hits/misses: ");hex64(c,ramfs_lookups);text64(c," ");hex64(c,ramfs_hits);text64(c," ");hex64(c,ramfs_misses);putc64(c,'\n');for(i=0;i<ramfs_count;i++){text64(c,"node ");hex64(c,i);text64(c," parent ");hex64(c,ramfs_nodes[i].parent);text64(c," inode ");hex64(c,ramfs_nodes[i].inode);text64(c," ");text64(c,ramfs_nodes[i].type==RAMFS_DIR?"dir":"file");putc64(c,'\n');}}
```
1. **头部统计**：先打印节点总数与 `lookups/hits/misses` 三计数——等效于 readdir 之后刷新缓存统计。
2. **逐节点列出**：`for(i=0;i<ramfs_count;i++)` 把每个节点的 `node 下标 / parent / inode / 类型(dir|file)` 打印出来——这就是教学版的「目录读取缓冲区」内容（对照 Linux `fs/readdir.c` 向用户缓冲区填充 `struct linux_dirent`）。
3. **有界遍历**：循环上界是常量 `ramfs_count`，天然不会越界——固定缓冲区与遍历一致性在此汇合。

#### 3.2.6 路径解析回归：pathtest

```c
static TEXT64 void pathtest(u16*c){int a=ramfs_lookup("/"),b=ramfs_lookup("/etc"),d=ramfs_lookup("/etc/motd"),e=ramfs_lookup("/bin/sh"),f=ramfs_lookup("/etc/missing"),g=ramfs_lookup("relative"),h=ramfs_lookup("/etc/motd/child");text64(c,"pathtest: ");text64(c,a==0&&b==1&&d==2&&e==4&&f<0&&g<0&&h<0?"ramfs/initramfs root-to-dentry path lookup passed":"BROKEN");putc64(c,'\n');}
```
1. **正例断言**：`/`→0、`/etc`→1、`/etc/motd`→2、`/bin/sh`→4，四个绝对路径全部命中且下标正确。
2. **反例断言**：`/etc/missing`（存在父目录但文件不存在）、`relative`（非绝对路径）、`/etc/motd/child`（在文件节点下再挂子路径）三例必须返回 `-1`。
3. **成功串**：`pathtest: ramfs/initramfs root-to-dentry path lookup passed`。

#### 3.2.7 固定缓冲区的两处关键防线：token64 与 fd_read_model

命令解析的令牌缓冲（`exec64` 里 `char word[24]` 的上界检查源头）：
```c
static TEXT64 const char *token64(const char*s,char *word,u32 cap){u32 n=0;while(space64(*s))s++;while(*s&&!space64(*s)){if(n+1>=cap)return 0;word[n++]=*s++;}word[n]=0;while(space64(*s))s++;return s;}
```
1. **容量前置校验**：写 `word[n]` 之前先检查 `n+1>=cap`，留一位给 `\0`——保证 23 个有效字符即满的 `word[24]` 永不溢出。
2. **返回 0 的语义**：令牌超长时 `token64` 返回 0，`exec64` 立即走 `command too long\n` 分支并 `prompt64`——超长命令被安全拒绝而非截断误执行。
3. **与输入缓冲的呼应**：`kernel_main64_binary` 主循环里 `else if(n<31){cmd[n++]=(char)ch;...}` 是同一原则在命令输入端的副本：两处（输入 31 上限、令牌 23 上限）把「命令超长」挡在解析与执行之外。

文件读取的有界性（继承自 Lesson 90–96，与主题直接相关）：
```c
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
```
1. **三层悬垂校验**：fd→file→inode 逐层查 `valid`，任何一层失效即拒绝——与 `fd_close_model` 的三层校验对称。
2. **剩余字节截断**：`remaining` 取 `size-offset`（读到文件尾为止），`if(bytes>remaining)bytes=remaining`——请求量超过剩余字节时被**夹到合法范围内**，这是「固定缓冲区」在数据读取端的强制边界。
3. **偏移推进**：`file_table[f].offset+=bytes` 并 `fd_reads++`，使重复读永远不会越过文件末尾——与 Lesson 96 的文件偏移主题衔接。

#### 3.2.8 目录读取到文件打开的完整链：shell_exec_path

```c
static TEXT64 int shell_exec_path(const char *path,u32 argc,u32 envc){int inode=ramfs_lookup(path);int fd;u64 image_hash=0x5348454c4c494d47ULL;if(inode<0||argc>4U||envc>4U)return 0;fd=fd_open_model((u32)inode,0);if(fd<0)return 0;shell_runtime.commands++;shell_runtime.execs++;shell_runtime.argv_words+=argc;shell_runtime.env_words+=envc;shell_runtime.pipe_links++;shell_runtime.signal_links++;shell_runtime.timer_links++;shell_runtime.deferred_links++;if(image_hash!=0x5348454c4c494d47ULL)return 0;fd_close_model((u32)fd);shell_runtime.exits++;return 1;}
```
1. **路径 → inode**：第一步就是 `ramfs_lookup(path)`（目录读取），返回 -1（路径不存在）或 4（`/bin/sh` 的节点下标）。
2. **inode → fd**：`fd_open_model((u32)inode,0)` 把节点指向的 inode 打开成文件描述符，构成「目录读取 → 打开 → 关闭」的完整链路，对应 Linux `filp_open` 内部 `path_openat`→`open_last_lookups`。
3. **有界参数与元数据累计**：`argc>4U||envc>4U` 拒绝超限 argv/env（有界参数的另一处检查），随后把 exec 各环节的 `shell_runtime` 计数累加。
4. **成功串**：`shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。

#### 3.2.9 exec64 增量与开机横幅

- `about` 输出 `Lesson 97: 目录读取与固定缓冲区\n`；检查点分支：
```c
else if(eq64(word,"l89test")){if(!noargs64(arg))usage64(c,"l89test");else l89test(c);}else if(eq64(word,"l97test")){if(!noargs64(arg))usage64(c,"l97test");else l97test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 97: 目录读取与固定缓冲区\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone` 编译 64 位内核、`objcopy` 取 raw 镜像、`grub-mkrescue` 打 ISO。`make check` 断言 README 含 `目录读取与固定缓冲区`、`Lesson 97`，kernel64.c 含 `l97test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma/reclaim → vfs_init() → ramfs_init()（5 节点目录树）
 ├─ 横幅 "Lesson 97: 目录读取与固定缓冲区"
 └─ 主循环：cmd[32] 有界输入 → exec64（word[24] 令牌）
     ├─ l97test / l89test → 阶段检查点（lesson_90_state / lesson_89_state）
     ├─ pathtest → 7 次 ramfs_lookup 断言命中/未命中
     ├─ ramfsinfo → 打印目录树（readdir 等价）
     ├─ shellrun → ramfs_lookup("/bin/sh") → fd_open_model → fd_close_model
     └─ fdinfo/fdtest → fd→file→inode 引用链观察与验证
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 调用 `ramfs_init()` 建 5 节点目录树，打印横幅 `Lesson 97: 目录读取与固定缓冲区`。
2. **`l97test`** → `l97test(c)` → 断言 → `l97test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`pathtest`** → 7 次 `ramfs_lookup` → 4 命中 + 3 未命中全部成立 → `pathtest: ramfs/initramfs root-to-dentry path lookup passed`。
4. **`ramfsinfo`** → 打印 `ramfs/initramfs nodes: 5 ...` 统计行 + 5 行 `node N parent P inode I dir|file` 目录表。
5. **`shellrun`** → `ramfs_lookup("/bin/sh")` → `fd_open_model(4,0)` → 累计 exec 元数据 → `fd_close_model` → `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed`。
6. **`l89test`**（历史检查点） → `l89test: bounded VFS, devices, epoll, and service management checkpoint passed`。
7. **`about`** → `Lesson 97: 目录读取与固定缓冲区`。

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
Multiboot2 and Lesson 97 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 97: 目录读取与固定缓冲区` 横幅 |
| `l97test` | `l97test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l89test` | `l89test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `pathtest` | `pathtest: ramfs/initramfs root-to-dentry path lookup passed` |
| `shellrun` | `shellrun: validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed` |
| `about` | `Lesson 97: 目录读取与固定缓冲区` |
| `ramfsinfo` | 首行 `ramfs/initramfs nodes: 5  root=dentry0 memory-backed` 等统计 + 5 行 `node ...` 目录表 |

判定成功：`l97test`/`pathtest`/`shellrun` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l97test` 输出 `Lesson 90 fallback reported` | `lesson_90_state` 初始化/断言失败（stale 镜像） | `grep -n "l97test" kernel64.c`；确认初始化串 `{90U,91U,92U,93U,1,1,1,1}` 与 `b==a+1U` 断言 |
| `pathtest` 输出 `BROKEN` | `ramfs_lookup` 返回下标与断言不符（例如节点表被改动） | 对照 `ramfs_init` 的 5 节点 parent/inode 布局；`ramfsinfo` 看节点表实际内容 |
| `ramfsinfo` 的 hits/misses 统计异常 | 多次调用 `ramfs_lookup` 未重置统计 | `pathtest` 每次运行应累计 4 hits + 3 misses；重启内核清零 |
| 输入超长命令后显示 `command too long` | `token64` 因 `n+1>=cap` 返回 0 | 对照 `token64` 的容量前置检查；`word[24]` 最多 23 字符 |
| 命令被截断执行（输入 32+ 字符） | 主循环 `n<31` 上限 | 对照 `kernel_main64_binary` 的 `else if(n<31)`；超出部分被静默丢弃 |
| `shellrun` 输出 `BROKEN` | `ramfs_lookup("/bin/sh")` 返回 -1 或 `fd_open_model` 失败 | 先跑 `pathtest`/`ramfsinfo` 确认路径解析正常，再检查 `FD_MAX` 是否被占用 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 97' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `目录读取与固定缓冲区` 与 `Lesson 97` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `ramfs_lookup` 整串匹配绝对路径 | `fs/namei.c`：`link_path_walk()`/`walk_component()`（逐组件路径解析）、`fs/dcache.c` 的 dentry 缓存 | 模型一次整串 `eq64` 完成查找，无逐组件分解、无 `nameidata` 状态机、无权限检查 |
| `ramfs_nodes[]` 定长目录树（parent/type/valid） | `fs/ramfs/` + `fs/libfs.c`：ramfs inode 树与 `simple_dir_operations`（`dcache_readdir`） | 模型 6 槽定长表，无 inode 分配器与目录项哈希链 |
| `ramfsinfo` 逐节点打印 `node/parent/inode/dir|file` | `fs/readdir.c`：`iterate_dir()`/`vfs_readdir()` 向用户缓冲填充目录项；`fs/libfs.c` `dcache_readdir()` | 模型无 `struct linux_dirent`、无 `getdents` syscall、无 per-file dir_context 游标 |
| `pathtest` 的命中/未命中断言 | LTP 文件系统测试 / `fs/namei.c` 的 lookup 返回值（`-ENOENT` 等） | 模型用 `-1` 单一失败码，无错误码分级 |
| `cmd[32]`/`word[24]`/`RAMFS_MAX=6` 固定上界 | `include/linux/limits.h`：`PATH_MAX`(4096)/`NAME_MAX`(255)；`include/linux/fdtable.h`：`struct fdtable` 的 `fd_array[NR_OPEN_DEFAULT]` 内嵌定长数组 | 模型容量按个位数定死，无动态扩展与 `RLIMIT_NOFILE` |
| `fd_read_model` 的剩余字节截断 | `fs/read_write.c`：`vfs_read()` 的 `pos` 边界与 `i_size_read()`；`fs/file_table.c` `f_pos` | 模型无 `copy_to_user` 实际拷贝，仅推进偏移 |
| `shell_exec_path` 的 lookup→open→close 链 | `fs/open.c`：`filp_open()` → `path_openat()` → `open_last_lookups()`；`do_sys_open()` | 模型无 O_CREAT/O_EXCL、无文件系统钩子、无 security 层 |
| `lesson_90_model` 检查点 | 无直接对应（LTP `fs` 测试套件） | 模型把目录读取主题的可验证状态固化进内核 |

**权威来源**：Linux `fs/namei.c`、`fs/readdir.c`、`fs/libfs.c`、`include/linux/limits.h`、`include/linux/fdtable.h` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么 Linux 用 `getdents`（目录读取）与 `stat`/`open` 分开处理目录与文件？教学模型中 `ramfs_lookup` 一次返回节点下标，省略了哪一层抽象？
2. **源码定位**：在 `kernel64.c` 中找出全部带显式上界检查的写入路径（提示：`token64` 的 `n+1>=cap`、主循环 `n<31`、`fd_read_model` 的 `bytes>remaining`），并说明每个检查防止的是哪一类越界。
3. **动手实验**：在 `ramfs_init` 中新增一个节点，例如 `/etc/passwd`（`name_hash=0x706173737764`、`parent=1`、`inode=2`、`RAMFS_FILE`），并在 `ramfs_lookup` 与 `pathtest` 中加入对应分支，重新构建运行 `pathtest` 验证。
4. **动手实验**：把 `exec64` 里 `word[24]` 改成 `word[8]`（需同步 `token64` 调用处的 cap），运行一个长命令，观察 `command too long` 路径与 `ramfs_lookup` 的 hits/misses 统计是否受影响。
5. **Linux 对照**：阅读 `fs/readdir.c` 的 `iterate_dir` 与 `fs/libfs.c` 的 `dcache_readdir`，指出真实 readdir 是如何利用 dcache 遍历目录项的；对比教学模型 `ramfsinfo` 直接遍历定长数组简化了什么。

---

## 9. 本课小结与下一课预告

1. 本课把「目录读取」拆成两个能力：路径解析（`ramfs_lookup`，对照 `fs/namei.c`）与目录列出（`ramfsinfo`，对照 `fs/readdir.c`）。
2. `ramfs_nodes[]` 用 `parent` 字段建树、`type` 区分目录/文件、`name_hash` 模拟目录项名，5 个节点组成 `/ /etc /etc/motd /bin /bin/sh` 的教学文件树。
3. 「固定缓冲区」不是约定而是强制检查：`cmd[32]`/`word[24]` 的输入上界、`RAMFS_MAX=6` 等定长表、`fd_read_model` 的剩余字节截断，共同保证 freestanding 内核不越界。
4. `pathtest` 用 4 正例 + 3 反例对路径解析做确定性回归，`ramfsinfo` 输出整棵目录树供人工核对。
5. 检查点增量：新增 `l97test`（Origin Lesson 90），恢复历史命名 `l89test`，横幅与 `about` 更新。
6. 下一课（Lesson 98）主题转向**字符设备注册**（对照 `fs/char_dev.c`），在 VFS 之上引入设备对象层次。
