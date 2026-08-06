# Lesson 44: Linux 风格文件描述符表、file/inode/dentry 引用与偏移模型 — 精讲文档

> **课号**：Lesson 44（可执行课）
> **主题**：Linux 风格文件描述符表（fd table）、`struct file`/`inode`/`dentry`
> 引用与偏移模型
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」第二课。前课（43）
> 完成页/页缓存/回收元数据；本课在页模型之上引入 VFS 对象四件套——
> `fd_model → file_model → inode_model` 的引用链，以及 `dentry_model`
> 的名字→inode 映射。
> **前置课程**：[`lesson-43-stable/README.md`](../lesson-43-stable/README.md)
> **后续课程**：[`lesson-45-stable/README.md`](../lesson-45-stable/README.md)
> （ramfs/initramfs 与有界 VFS 路径查找）
> **一句话目标**：能讲清 Linux 为什么把「打开的文件」拆成 fd、file、inode
> 三层、为什么 offset/flags 属于 file 而不属于 inode、关闭最后一个引用时
> refcount 怎么级联递减，并在 TinyOS 里复刻全部**引用计数与偏移记账**——
> 无磁盘 I/O、无任意指针访问。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux 打开一个文件后内核里躺着什么：
进程的 `fd_table` 里一个整数指向 `struct file`（持有 offset、flags、
refs），`struct file` 指向 `struct inode`（持有 ino/size/mode/refs），
`dentry` 把路径名映射到 inode。TinyOS 用 4 个固定容量表
（`FD_MAX=4`/`FILE_MAX=3`/`INODE_MAX=3`/`DENTRY_MAX=3`）与
`fd_open_model`/`fd_close_model`/`fd_read_model` 实现「打开→读偏移→关闭」
的引用计数与偏移记账。

- **在课程主线中的位置**：第 6 阶段从 43 的「页」推进到 44 的「文件对象」。
  44 建立的 inode/file 引用链是 45（ramfs 树与路径查找）的对象骨架、
  46（管道 fd 端点）和 48（timerfd 可读性）都会复用的「fd 是表的索引、
  对象持有状态」思想。
- **前置知识清单**：
  1. lesson-43 的 `struct page_model` 与 refs 引用计数直觉；
  2. lesson-37/38 的 task/thread 元数据表 + 线性表 slot 管理模式
     （`valid` 位 + 固定容量）；
  3. Linux VFS 词汇：`open`/`read`/`close` 与
     `include/linux/fs.h` 的 `struct file`/`struct inode`/`struct dentry`
     基本分工；
  4. C 复合字面量初始化（`(struct file_model){...}`）与八进制字面量
     `0100644` 的文件权限直觉。
- **本课交付**：`fdinfo`/`fdtest` 两条命令；`inode_model`/`dentry_model`/
  `file_model`/`fd_model` 四个结构；`vfs_init`/`fd_open_model`/
  `fd_close_model`/`fd_read_model` 四个函数；四个全局计数
  （opens/closes/reads/seeks）。

---

## 2. 核心概念精讲

### 2.1 概念一：fd 表——进程视角的“整数句柄”

**定义**：文件描述符表是每个进程持有的一张索引表：整数 fd 是下标，
表项 `fd_model { u64 file_index; u8 valid; }` 指向内核的 file 对象。
**为什么需要**：用户程序不想持有笨重的内核对象指针，只想要一个小整数；
内核想在 `close(fd)` 时立刻知道该释放谁。**工作机制**：`fd_open_model`
找第一个 `!valid` 的 fd 槽绑定一个新 file 对象并返回槽号；`fd_close_model`
把 `valid` 清 0 并沿引用链递减。本课 fd 表是单进程固定 4 槽——
Linux 中每个进程的 `files_struct`（`fs/file.c`）可扩容，概念一致。

### 2.2 概念二：struct file——每次打开的“活状态”

**定义**：`file_model { u64 inode_index, offset, flags, refs; u8 valid; }`
记录一次具体打开行为的动态状态：读到哪里（offset）、以什么模式开
（flags）、被多少个 fd 引用（refs）。**为什么需要**：同一个 inode 可以被
两次独立打开，各有各的偏移——偏移必须属于 file 而不属于 inode，否则两个
描述符读同一个文件会互相干扰（Linux 中 `struct file->f_pos` 正是此理）。
`fd_read_model` 只推进 `file_table[f].offset`，`inode_table` 完全不变。

### 2.3 概念三：inode——磁盘元数据的“常驻副本”

**定义**：`inode_model { u64 ino, size, mode, refs; u8 valid; }` 是文件
身份的载体：唯一编号 `ino`、长度 `size`、权限位 `mode`（本课
`0100644`，八进制，即 rw-r--r--）与引用计数。**为什么需要**：多个 file 可
指向同一个 inode（如两次打开同一文件），元数据只存一份。本课 `vfs_init`
预置 3 个 inode（size 分别为 0x100/0x101/0x102），`fd_open_model` 打开时
`inode_table[inode].refs++`——对应 Linux `iget`/`i_count`。

### 2.4 概念四：dentry——名字到 inode 的桥梁

**定义**：`dentry_model { u64 name_hash, inode_index, refs; u8 valid; }`
把路径名（本课用 `name_hash=0x100+i` 代替字符串）映射到 inode 下标。
**为什么需要**：`open("/etc/motd")` 必须先把名字解析成 inode，dentry 缓存
就是这个解析结果；下一次同名查找直接命中。**本课范围**：dentry 由
`vfs_init` 预置 3 个、`refs` 初始化后**不再变动**——fd 的 open/close/read
路径根本不经过 dentry；它是一张供 lesson-45 路径查找使用的静态
「名字→inode」映射表。

### 2.5 概念五：引用级联（refcount cascade）

**定义**：对象链上的引用计数沿「fd → file → inode」方向逐级持有：
fd 槽引用 file，file 引用 inode。**为什么需要**：关闭 fd 不能立刻销毁
inode——可能还有另一个 file 指着它。**工作机制**（`fd_close_model`）：
关 fd → 该 fd 唯一拥有的 file 的 `refs--`；若 file.refs 归 0（不再被任何
fd 引用）才置 `valid=0`，**并顺势** `inode.refs--`；inode.refs 归 0 后
（本课未实现）才真正释放 inode。这正是 Linux 引用计数
`fput → __fput → iput` 的级联教学版。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-43） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：四个 VFS 结构 + `vfs_init`/`fd_open_model`/`fd_close_model`/`fd_read_model`/`fdinfo`/`fdtest` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `fs/file.c`、`fs/dcache.c`、`include/linux/fs.h`；kernel64.c 含 `fdinfo`、`fdtest` |
| `grub.cfg` | 装载 | **未更新**：menuentry 标题仍是 lesson-43（见 §3.7 源码事实） |

### 3.2 结构 / 宏 / 全局变量精讲

```c
/* Lesson 44: fixed VFS/file-descriptor teaching metadata. No disk I/O. */
#define FD_MAX 4U
#define FILE_MAX 3U
#define INODE_MAX 3U
#define DENTRY_MAX 3U
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
static struct inode_model inode_table[INODE_MAX];
static struct dentry_model dentry_table[DENTRY_MAX];
static struct file_model file_table[FILE_MAX];
static struct fd_model fd_table[FD_MAX];
static u64 fd_opens,fd_closes,fd_reads,fd_seek_ops;
```

逐行注释：
- 头注释声明整课纪律：**固定 VFS/fd 教学元数据、无磁盘 I/O**；
- 四个容量宏体现「有界」：fd 最多 4、file/inode/dentry 各最多 3。注意
  `FD_MAX(4) > FILE_MAX(3)`——允许同一 file 被多个 fd 引用（dup 语义的
  基础），或注释掉的可能性留白；
- `inode_model`：`ino` 唯一身份、`size` 字节长度、`mode` 权限位、`refs`
  引用计数、`valid` 槽位；
- `dentry_model`：`name_hash` 代替名字字符串、`inode_index` 指向 inode
  槽、`refs` 与 `valid`；
- `file_model`：`inode_index`、`offset`（关键！每次打开独立）、`flags`、
  `refs`；
- `fd_model`：极简——只有 `file_index` 与 `valid`，fd 自身无状态，
  一切状态在 file 对象里；
- `fd_opens/fd_closes/fd_reads/fd_seek_ops` 四个记账计数器。其中
  `fd_seek_ops` 在 `fdinfo` 中被打印，但**全课没有 seek 操作函数**，
  从不递增——是留给后续课的占位计数器。

### 3.3 函数精讲：vfs_init —— 预置三张静态表

```c
static TEXT64 void vfs_init(void){u32 i;
for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};
for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};
for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};
for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};
fd_opens=fd_closes=fd_reads=fd_seek_ops=0;}
```

逐行分析：
- **inode 表**（第二行）：`{i+1, 0x100+i, 0100644, 1, 1}` 逐个初始化——
  `ino=i+1`（1/2/3）、`size=0x100+i`（256/257/258 字节）、`mode=0100644`
  （八进制文件权限，rw-r--r--）、`refs=1`（初始被表自身持有）、
  `valid=1`。`0x100=256` 让 `fdtest` 里读 8 字节不越界；
- **dentry 表**（第三行）：`{0x100+i, i, 1, 1}`——`name_hash=0x100+i`
  与 `inode_index=i` 一一对应，形成静态名字→inode 映射；
- **file 表**（第四行）：`{i,0,0,0,0}`——所有 file 初始 `valid=0`、
  `refs=0`，等待打开时被激活；
- **fd 表**（第五行）：全 0（invalid）；末行把四个计数器清零，
  保证每次启动基线一致。`vfs_init` 在 `kernel_main64_binary` 中于
  `reclaim_init()` 之后、`address_space_init` 之前被调用。

### 3.4 函数精讲：fd_open_model —— 双层找槽

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;
if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;
for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;
for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){
file_table[f]=(struct file_model){inode,0,flags,1,1};
fd_table[i]=(struct fd_model){f,1};
inode_table[inode].refs++;fd_opens++;return (int)i;}}
return -1;}
```

逐行分析：
- **入参校验**（第二行）：`inode>=INODE_MAX` 越界或对应 inode 槽
  无效直接返回 `-1`——对应 Linux `open` 在路径解析后对 inode 的
  `igrab` 失败路径；
- **双层找槽**（第三、四行）：先找第一个空闲 fd 槽 `i`，再在该槽内找
  第一个空闲 file 槽 `f`。嵌套结构保证「fd 槽和 file 槽都最小编号」，
  与 `fdtest` 的断言（fd0/file0、fd1/file1）一致；
- **激活两个对象**（第五、六行）：`file_table[f]={inode,0,flags,1,1}`
  （inode_index、offset=0、flags、refs=1、valid=1），
  `fd_table[i]={f,1}` 把 fd 绑到 file 槽 f；
- **记账**（第七行）：`inode_table[inode].refs++`（inode 多了一个 file
  引用）、`fd_opens++`，返回 fd 槽号（`(int)i`，i 是 u32 强转）；
- 若 fd 表满或 file 表满（两层循环都找不到），返回 `-1`——**表满即拒绝**，
  没有扩容，是「有界」的直接体现。

### 3.5 函数精讲：fd_read_model / fd_close_model —— 偏移推进与引用级联

```c
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;
if(fd>=FD_MAX||!fd_table[fd].valid)return 0;
f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;
n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;
size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;
if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
```

`fd_read_model` 逐行分析：
1. **三层校验**（前三行）：fd→file→inode 逐级查有效性与越界，任何一环
   无效返回 0——教学模型对「悬垂 fd/file」零容忍；
2. **剩余量计算**（第五行）：`remaining = offset < size ? size-offset : 0`，
   若偏移已越过文件尾则剩余为 0（不会负读）；
3. **有界推进**（第六行）：`bytes>remaining` 时截断到 `remaining`——
   读操作不能越过 EOF，对应 Linux `rw_verify_area`/`i_size` 检查；
   `offset+=bytes` 只改 file 对象、不动 inode.size，`fd_reads++` 返回 1。

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;
if(fd>=FD_MAX||!fd_table[fd].valid)return 0;
f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;
n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;
fd_table[fd].valid=0;
if(file_table[f].refs){file_table[f].refs--;
if(!file_table[f].refs){file_table[f].valid=0;
if(inode_table[n].refs)inode_table[n].refs--;}}
fd_closes++;return 1;}
```

`fd_close_model` 逐行分析：
1. **三层校验**（前三行）：与 read 相同，防止关闭一个已失效的 fd/file；
2. **释放 fd 槽**（第五行）：`fd_table[fd].valid=0`——fd 不再指向任何
   file；
3. **file 引用递减**（第六行）：`file_table[f].refs--`（仅当 refs 非零，
   防御性检查）；
4. **级联到 inode**（第七、八行）：当 file.refs 降到 0，置
   `file_table[f].valid=0` 并 `inode_table[n].refs--`——注意 inode 的
   引用递减**只在 file 生命周期结束时发生一次**，这正是「引用级联」的
   核心；
5. `fd_closes++` 返回 1。两次独立关闭（fd0、fd1）后 inode0/inode1 的
   refs 都从 2 回到 1（表自身持有），file0/file1 变为 invalid。

### 3.6 函数精讲：fdinfo / fdtest

```c
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");
for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;
u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");
hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");
hex64(c,file_table[f].offset);putc64(c,'\n');}
text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);
text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
static TEXT64 void fdtest(u16*c){int a=fd_open_model(0,1),b=fd_open_model(1,2),
r1=a>=0&&b>=0&&fd_read_model((u32)a,8)&&fd_close_model((u32)b)&&fd_close_model((u32)a);
text64(c,"fdtest: ");text64(c,r1&&fd_opens==2&&fd_closes==2?
"fd/file/inode/dentry refs and offsets passed":"BROKEN");putc64(c,'\n');}
```

- `fdinfo`：先打印表头 `fd/file/inode/dentry tables (bounded)`，再遍历
  fd 表打印每个有效槽的 `fd i file f inode <ino> off <offset>`（通过
  file→inode 两级间接取 ino 与 offset），最后打印四个计数器；
- `fdtest`：一次完整生命周期——① `fd_open_model(0,1)`（flags=1）打开
  inode0 得 fd0；② `fd_open_model(1,2)` 打开 inode1 得 fd1；
  ③ `fd_read_model(0,8)` 从 offset 0 读 8 字节；④ 先关 fd1 再关 fd0。
  断言 `r1 && fd_opens==2 && fd_closes==2`——打开与关闭次数必须各为 2，
  验证引用级联后表状态自洽。

### 3.7 exec64 分支、kernel_main、grub.cfg 与 Makefile

`exec64` 新增两个分支（源码逐字）：

```c
else if(eq64(word,"fdinfo")){if(!noargs64(arg))usage64(c,"fdinfo");else fdinfo(c);}
else if(eq64(word,"fdtest")){if(!noargs64(arg))usage64(c,"fdtest");else fdtest(c);}
```

`kernel_main64_binary` 初始化链新增 `vfs_init()`（源码逐字）：

```c
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;
task_names_keep();active_sched_class=&fair_sched_class;sched_enqueues=sched_dequeues=sched_picks=0;
pmm_init(h);vma_init();reclaim_init();vfs_init();address_space_init(&kernel_address_space,h);
```

**源码事实（必须知悉）**：
- 开机横幅**仍是 lesson-43 文案**（源码逐字）：
  `TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
  / `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`
  ——本课的横幅/`about`/`grub.cfg` menuentry 三处标题**都没有**改成
  lesson-44，是源码里未同步的文案，QEMU 窗口菜单会显示
  `TinyOS lesson 43: page cache, anonymous pages, and reclaim model`；
- `help` 文案与 lesson-42/43 相同，未列出 `fdinfo`/`fdtest`（命令仍可用）；
- Makefile `check` 目标新增 grep（README 三路径 + kernel64.c 两符号）：

```make
@grep -q 'fs/file.c' README.md
@grep -q 'fs/dcache.c' README.md
@grep -q 'include/linux/fs.h' README.md
@grep -q 'fdinfo' kernel64.c
@grep -q 'fdtest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 44 checks passed.'
```

### 3.8 主控制流

```text
kernel_main64_binary
  ├─ pmm_init → vma_init → reclaim_init → vfs_init → address_space_init
  ├─ 横幅（源码仍为 lesson-43 文案，见 §3.7 源码事实）
  └─ 键盘循环 → exec64：
        fdinfo / fdtest / anoninfo / reclaimtest / ptrinfo / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "fdtest"
  ├─ fd_open_model(0,1)   → fd 槽0 空、file 槽0 空 →
  │    file0={inode=0,off=0,flags=1,refs=1}  fd0={file=0}
  │    inode0.refs 1→2  fd_opens=1  返回 fd 0
  ├─ fd_open_model(1,2)   → fd1/file1 同上，inode1.refs 1→2
  │    fd_opens=2  返回 fd 1
  ├─ fd_read_model(0,8)   → inode0.size=256，remaining=256
  │    file0.offset 0→8  fd_reads=1
  ├─ fd_close_model(1)    → fd1 失效；file1.refs 1→0 → file1 失效；
  │    inode1.refs 2→1   fd_closes=1
  └─ fd_close_model(0)    → fd0 失效；file0.refs 1→0 → file0 失效；
       inode0.refs 2→1   fd_closes=2
  → "fdtest: fd/file/inode/dentry refs and offsets passed"

输入 "fdinfo"（fdtest 之后）
  → "fd/file/inode/dentry tables (bounded)"
  → "opens/closes/reads/seeks: 0000000000000002 0000000000000002 0000000000000001 0000000000000000"
```

（fd 表已全部关闭，无有效 fd 行。若想看到 `fd i file f inode n off ...`
明细，可在 `fdtest` 之前运行 `fdinfo`——此时表为空；目前没有命令在
`fd_open_model` 之后停住，明细行的演示留待思考题 3。）

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

`make check` 输出：`Multiboot2 and lesson 44 checks passed.`（要求 README 含
`fs/file.c`、`fs/dcache.c`、`include/linux/fs.h`，kernel64.c 含 `fdinfo` 与
`fdtest`，缺一即失败；旧 README 里的 `fs/open.c`/`fs/inode.c` 引用在 §7
中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
注意它显示的是 lesson-43 文案——源码未同步，见 §3.7）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
fdtest
```

预期：`fdtest: fd/file/inode/dentry refs and offsets passed`

```bash
fdinfo
```

预期（一次 `fdtest` 之后）：

```text
fd/file/inode/dentry tables (bounded)
opens/closes/reads/seeks: 0000000000000002 0000000000000002 0000000000000001 0000000000000000
```

继承回归：`anoninfo`/`reclaimtest`/`pfmodel`/`ptrinfo` 行为与 lesson-43
一致；真实 `#PF` 命令 `pftest`/`isttest`/`stackguardtest` 保持致命停机。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 44 checks passed.`；`fdtest` 断言
通过（fd_opens=2、fd_closes=2）；`fdinfo` 显示 opens/closes/reads/seeks =
2/2/1/0；`anoninfo`/`reclaimtest` 与 lesson-43 一致。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `fs/file.c`/`fs/dcache.c`/`include/linux/fs.h`，或 kernel64.c 缺 `fdinfo`/`fdtest` | 对照 Makefile check 的 grep 列表 |
| `fdtest` 输出 `BROKEN` | 打开/读/关闭链中断言不成立 | 逐项核对：`fd_open_model` 返回 0/1；`fd_read_model` 推进 offset；两次 `fd_close_model` 后 `fd_opens==2 && fd_closes==2` |
| `fdinfo` 的 seeks 计数永远是 0 | 全课没有实现 seek 操作，`fd_seek_ops` 只被打印不递增（源码事实） | 属预期；后续课若加 `fd_seek_model` 才会递增 |
| `fdinfo` 无 `fd ...` 明细行 | `fdtest` 已把两个 fd 全部关闭 | 在 `fdtest` 之前/之间运行 `fdinfo`，或自行修改代码在 open 后打印 |
| 横幅/菜单显示 lesson-43 | 本课横幅/`about`/`grub.cfg` 三处文案未更新为 lesson-44（源码事实） | 对照 lesson-44 kernel64.c 与 grub.cfg 实际字符串 |
| 担心「真读了磁盘」 | 设计保证元数据化 | `fd_read_model` 只动 `file_table[f].offset`，无任何内存/磁盘访问指令 |
| `fd_open_model` 返回 -1 | inode 越界/无效，或 fd/file 表满 | 检查入参 `inode<INODE_MAX`；`FD_MAX=4`/`FILE_MAX=3` 是有界容量 |
| inode.refs 未按预期递减 | 引用级联只在 file.refs 归 0 时触发 | 对照 `fd_close_model`：`if(!file_table[f].refs)` 分支内的 `inode_table[n].refs--` |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `fs/file.c`、`fs/open.c`、`fs/inode.c`、`fs/dcache.c`、
`include/linux/fs.h`**（延续 lesson-43 的 `mm/filemap.c`/`mm/vmscan.c`
对照线）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `fd_model` 表 + 空槽扫描 | `fs/file.c` 的 `fdtable`/`alloc_fd`（位图 + `FD_SET`），每进程 `files_struct` | 教学模型固定 4 槽线性扫描；Linux 用位图找最小空闲 fd 并自动扩容 |
| `file_model{inode_index,offset,flags,refs}` | `include/linux/fs.h` 的 `struct file`（`f_path`/`f_pos`/`f_flags`/`f_count`） | 教学模型把 `f_pos` 记作 `offset`，`f_count` 记作 `refs`，省略锁与 RCU |
| `fd_open_model` 返回 fd 整数 | `fs/open.c` 的 `do_sys_open` → `fd_install`（`__fd_install` 写入 fdtable） | 教学模型把 open 拆成「找 inode 槽→建 file→绑 fd」，不解析路径（路径留给 lesson-45） |
| `fd_read_model` 的 offset 推进与 EOF 截断 | `fs/read_write.c` 的 `vfs_read` → `new_sync_read`（`file_pos` 推进 + `i_size_read` 上限） | 教学模型无 `copy_to_user`、无页缓存读取，只记账 |
| `fd_close_model` 的 file.refs→inode.refs 级联 | `fs/file.c` 的 `__fput` → `iput`；`include/linux/fs.h` 的 `fput`/`ihold`/`iput` | 教学模型把两级 refcount 递减显式写出，Linux 还有 `f_count` 保护与 `fsnotify` |
| `inode_model{ino,size,mode,refs}` | `fs/inode.c` 的 `struct inode`（`i_ino`/`i_size`/`i_mode`/`i_count`）+ `iget`/`iput` | 教学模型无 inode 缓存树（radix tree）、无磁盘元数据读写 |
| `dentry_model{name_hash,inode_index}` | `fs/dcache.c` 的 `struct dentry`（`d_name`/`d_inode`/`d_parent`），dentry 缓存哈希表 | **教学模型只有静态映射、无解析逻辑**；dentry 的实际遍历在 lesson-45 才登场 |
| `0100644` mode | `include/uapi/linux/stat.h` 的 `S_IFREG|S_IRUSR|...` 权限位 | 八进制字面量即 Linux 权限位直觉 |
| 固定 `FD_MAX=4`/`FILE_MAX=3` | `fs/file.c` 的 `NR_OPEN_DEFAULT` 与动态扩容 | 有界是教学刻意简化 |

**权威来源**：POSIX 文件描述符语义（open 返回最小可用 fd）、
Linux VFS 对象模型文档（`Documentation/filesystems/vfs.rst`）。

**教学模型简化了什么**：
1. 无路径解析：`fd_open_model` 直接给 inode 下标，dentry 未参与；
2. 无磁盘 I/O：open/read/close 全是内存表记账；
3. 无锁/RCU/引用保护竞态：单 CPU 串行语义；
4. 无 `dup`/`fork` 继承 fd 表、无 O_CLOEXEC、无 `struct cred` 权限检查；
5. `fd_seek_ops` 是占位计数器，seek 未实现。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `offset` 放在 `file_model` 而不放在
   `inode_model`？如果两次打开同一 inode 共享一个 offset 会出什么问题？
2. **源码定位**：`fd_close_model` 的 inode 引用递减为什么被包在
   `if(!file_table[f].refs)` 里？如果两个 fd 指向同一个 file，只关一个，
   inode.refs 应该变吗？
3. **动手实验**：给 `fdtest` 在两次 `fd_open_model` 之后插入一次
   `fdinfo(c)` 调用（或在 `fd_read_model` 前），重建后观察
   `fd 0 file 0 inode 1 off 0` 等明细行的实际格式，然后还原。
4. **Linux 对照**：打开 `fs/file.c` 的 `alloc_fd` 与 `__fd_install`，
   列出它为本课 `fd_open_model` 省略掉的至少 4 项工作
   （提示：位图、扩容、CLOEXEC、fd 表锁）。
5. **设计思考**：`FD_MAX=4 > FILE_MAX=3`，若要实现 `dup(fd0)` 让 fd3 与
   fd0 共享 file0，`fd_dup_model` 应该写什么？（提示：只新建 fd 槽、
   file.refs++、不新建 file。）

---

## 9. 本课小结与下一课预告

**小结**：本课用 `fd_model → file_model → inode_model` 三层表复刻了
Linux 打开文件的完整对象链：fd 是进程手里的整数句柄，file 持有每次打开
独立的 offset/flags 与 refs，inode 持有文件的 ino/size/mode/refs，
dentry 提供静态名字→inode 映射；`fd_open_model` 双层找槽、`fd_read_model`
有界推进偏移、`fd_close_model` 逐级递减引用并在 file.refs 归零时才触碰
inode.refs。`fdtest` 用「开两个→读一个→关两个」验证引用与偏移记账，
`fdinfo` 打印表头与四个计数器。本课无磁盘 I/O、无路径解析；横幅/`about`/
grub.cfg 三处标题仍是 lesson-43 文案（源码未同步），`fd_seek_ops` 为占位
计数器——这些是如实记录的本课源码事实。Makefile `check` 强制 README 与
源码包含相应符号。

**下一课预告**：lesson-45 将把本课的 dentry「静态名字→inode 映射」升级为
**真正可遍历的 ramfs/initramfs 树与绝对路径查找**（对照 `fs/ramfs/`、
`fs/namei.c`）：`/etc/motd`、`/bin/sh` 这类字符串怎么从根逐级解析到 inode，
以及路径穿越普通文件、缺失路径、相对路径分别返回什么。本课学到的 inode
表将成为 ramfs 元数据数组的直接骨架。
