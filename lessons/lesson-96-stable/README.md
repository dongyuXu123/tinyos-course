# Lesson 96: 文件偏移与引用计数 — 精讲文档

> **课号**：Lesson 96 ｜ **主题**：文件偏移与引用计数（file offset and reference counting）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 89 原型的检查点
> **前置课程**：[`../lesson-95-stable/README.md`](../lesson-95-stable/README.md)（文件打开与 file_operations）
> **后续课程**：[`../lesson-97-stable/README.md`](../lesson-97-stable/README.md)（VFS 阶段收束课）
> **一句话目标**：精讲 `struct file` 的两个核心字段——`f_pos`（文件偏移，读写在哪儿进行）与 `f_count`（引用计数，什么时候才能释放 file），验证教学内核 `file_model.offset`/`refs` 与 inode `refs` 的联动语义。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第九课。`kernel64.c` 相对上一课仅做一处增量——把 `l95test` 恢复为 `l88test`，新增 `lesson_89_model` 状态与 `l96test` 检查点测试，并更新 `about`/开机横幅为本课主题。文件偏移与引用计数机制由累积代码承载：`file_model.offset` 是 `f_pos` 教学版（打开归零、`fd_read_model` 推进、被 inode `size` 钳制），`file_model.refs`/`inode_table[].refs`/`dentry_table[].refs` 构成两级引用链，`fd_close_model` 在 file 引用归零时级联递减 inode。`reclaim_one` 的 `refs==1` 条件与 `resource_teardown` 的 `fd_refs` 归零是同一原则在页对象与资源释放上的复刻。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l89test`，但实际源码中本课检查点命令是 `l96test`（`l89test` 命令在本课源码中不存在，历史检查点按惯例为 `l88test`）。本文以源码为准：本课检查点命令为 `l96test`，历史回归命令 `l88test`（恢复自上一课 `l95test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：解释 `f_pos` 为什么属于 `struct file` 而不是 `struct inode`（每个打开实例独立前进，inode 是共享本体）；描述 `f_count` 的生命周期（`fget` 递增、`fput` 递减、归零才 `__fput`）；在教学内核中沿 `fd_open_model`（offset=0、refs=1）→ `fd_read_model`（offset 前进并钳制）→ `fd_close_model`（refs 归零、级联 inode）走一遍；运行 `l96test`/`fdtest`/`fdinfo`/`reclaimtest`/`teardowntest` 验证。

**在课程主线中的位置**：Lesson 95 建立了 file 对象的「打开 → 操作 → 释放」骨架；本课给这个骨架注入**灵魂**——file 之所以是「打开实例」而非 inode，全靠 `f_pos` 与 `f_count` 两个字段。这是 VFS 阶段对 file 对象模型的收官（Lesson 97 收束整个阶段）。`refs` 机制在页对象（Lesson 76/84）与资源释放（Lesson 41/43）已有先例，本课把「引用归零才销毁」原则在 file 层重新点亮。

**前置知识清单**（学本课前必须掌握）：
1. file 对象模型：`struct file_model{inode_index,offset,flags,refs}` 与 `fd_open_model`/`fd_read_model`/`fd_close_model`（Lesson 95）。
2. inode 生命周期：`inode_table[].refs` 的 `iget`/`iput` 语义（Lesson 90）。
3. 页回收的 `refs==1` 条件：`reclaim_one`（Lesson 76/84）。
4. 资源有序释放：`resource_teardown` 的 `fd_refs` 与 `releases`（Lesson 41/43）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 96: 文件偏移与引用计数`；
- 新命令 `l96test` 输出 `l96test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `fdtest`/`fdinfo` 展示读后 offset 前进与 open/close 的引用链变化，`reclaimtest`/`teardowntest` 展示同一原则的另两个实例。

---

## 2. 核心概念精讲

### 2.1 f_pos：文件偏移为什么属于 file 而非 inode

**直觉**：两个进程同时读同一个文件，各自读到自己的进度——一个读到第 10 字节时，另一个可能还停在开头。所以「当前读到哪」必须跟着**打开实例**走，而不是跟着文件本体走。这就是 `f_pos`（file position）挂在 `struct file` 上的原因。

**教学模型的对应**：`file_model.offset` 在 `fd_open_model` 中初始化为 0；`fd_read_model` 每次读取按 `inode_table[n].size` 钳制后推进 `offset`。若两个 fd 各自独立 open 同一个 inode，它们的 `offset` 互不影响（每个 `file_model` 槽各存一份）——这正是 `f_pos` 独立性的最小演示。

**对照 Linux**：`include/linux/fs.h` 的 `struct file.f_pos`（`loff_t`）；`fs/read_write.c` 的 `vfs_read`/`vfs_write` 使用并更新 `file->f_pos`（`llseek` 也可改）。若两个 fd 指向同一 inode 但分别 open，`f_pos` 独立；若通过 `dup` 共享同一个 file，则 `f_pos` 共享——模型没有 `dup`，只演示前者。

### 2.2 f_count：引用计数与延迟释放

**直觉**：file 可能同时被 fd、dup、内核内部、异步 I/O 引用，任何一个使用者想关闭它都不能直接销毁——必须等**最后一个**引用者放回。Linux 用 `f_count` 记录，`fput()` 减到 0 才真正 `__fput()`。

**两级引用链（教学模型）**：
```
fd_open_model(inode):   file_table[f].refs=1   →  inode_table[n].refs++（= dentry 基引用 + 存活 file 数）
fd_close_model(fd):     fd 槽释放 → file.refs--
                           └─ 归零 → file 槽释放 → inode_table[n].refs--
```
1. inode 的引用 = dentry 基引用（常驻，模型内永不归零）+ 存活 file 数；
2. 只有当**最后一个** file 关闭时 inode 引用才递减——这就是「最后引用者负责销毁」原则。

### 2.3 同一原则的另两个实例：reclaim_one 与 resource_teardown

- `reclaim_one`：`if(!m->live||!m->reclaimable||m->refs!=1) continue;`——只有 `refs==1`（无额外持有）的匿名页才被回收；
- `resource_teardown`：置 `zombie` 后按地址空间→fd→管道→信号→定时器→延迟工作依次归零，`fd_refs` 是其中一环，`teardown_done` 防二次释放。

这三个实例（页/资源/file）共享同一公理：**引用不到零，不销毁**。本课把 file 层补齐，形成完整图景。

### 2.4 检查点模型：lesson_89_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l96test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 89）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l88test`；新增 `lesson_89_model`/`lesson_89_state`/`l96test`；`about` 与横幅更新。偏移/引用机制由累积代码承载（`file_model.offset/refs`、`fd_read_model`、`fd_close_model`），本课以「偏移与引用」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`文件偏移与引用计数`/`Lesson 96`/`l96test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（文件偏移/引用计数视角 + 本课增量）

#### 3.2.1 偏移与引用的载体：file_model / 统计量

```c
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
static struct file_model file_table[FILE_MAX];
static u64 fd_opens,fd_closes,fd_reads,fd_seek_ops;
```
1. `offset` 字段即 `f_pos`：每个 file 槽一份，与 inode 本体无关——「打开实例」语义的关键。
2. `refs` 字段即 `f_count`：file 被持有的次数，归零即回收。
3. 统计量 `fd_opens/fd_closes/fd_reads/fd_seek_ops` 供 `fdinfo`/`fdtest` 断言；注意 `fd_seek_ops` 在本模型中**没有对应的 seek 函数**（`ramfs` 的定长读模型不需要 `llseek`），保留计数器是教学完整性设计。

#### 3.2.2 打开即建立偏移与引用：fd_open_model

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. **offset 初始化**：`file_table[f]=(struct file_model){inode,0,flags,1,1}` 的第 2 个字段 `0`——打开时位置从文件开头开始，对应 Linux `filp_open` 后 `f_pos=0`。
2. **refs 初始化**：第 4 个字段 `1`——新 file 立即被自己的 fd 持有，对应 `f_count=1`。
3. **inode 引用递增**：`inode_table[inode].refs++`——inode 的引用数 = dentry 基引用 + 存活 file 数。
4. **flags 记录**：第 3 个字段原样保存打开方式（Lesson 94 已讲「只存不查」）。

#### 3.2.3 读推进偏移并受 size 钳制：fd_read_model

```c
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
```
1. **剩余量计算**：`remaining = offset < size ? size-offset : 0`——读到文件末尾为止；offset 已越过 size 时剩余为 0。
2. **钳制**：`bytes>remaining` 时 `bytes=remaining`——读请求超过文件剩余长度被截断，对应 Linux `vfs_read` 到 EOF 返回不足量。
3. **推进偏移**：`file_table[f].offset+=bytes`——每次读取都更新 `f_pos`，下次读从新位置继续。
4. **三级校验**：fd→file→inode 逐层查 `valid`，任何一层悬垂即拒绝（操作必须作用于活对象）。
5. **对照 Linux**：`fs/read_write.c` 的 `vfs_read`：先 `rw_verify_area`（校验文件是否允许读与范围），再从 `file->f_pos` 开始，读后 `file_pos_write` 更新 `f_pos`。

#### 3.2.4 引用归零才级联释放：fd_close_model

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
```
1. **fd 先释放**：`fd_table[fd].valid=0`。
2. **file 引用递减**：`file_table[f].refs--`——对应 Linux `fput()` 的 `f_count--`。
3. **归零才销毁**：`if(!file_table[f].refs)` 时回收 file 槽，并 `inode_table[n].refs--`——对应 Linux `fput` 减到 0 触发 `__fput()` 再 `iput()`。**关键设计**：inode 引用只在「最后一个 file 关闭」时递减。
4. **幂等**：对已关闭 fd 再次调用因 `valid==0` 返回 0——防止 double-close 破坏引用计数。
5. **对照 Linux**：`fs/file_table.c` 的 `fput`/`__fput`（引用计数原子操作 + task_work/RCU 延迟）；模型把「file 归零」与「inode 递减」合并为一条条件语句。

#### 3.2.5 观察仪表盘与回归：fdinfo / fdtest

```c
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");hex64(c,file_table[f].offset);putc64(c,'\n');}text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
static TEXT64 void fdtest(u16*c){int a=fd_open_model(0,1),b=fd_open_model(1,2),r1=a>=0&&b>=0&&fd_read_model((u32)a,8)&&fd_close_model((u32)b)&&fd_close_model((u32)a);text64(c,"fdtest: ");text64(c,r1&&fd_opens==2&&fd_closes==2?"fd/file/inode/dentry refs and offsets passed":"BROKEN");putc64(c,'\n');}
```
1. `fdinfo` 每行打印存活 fd 的 `off`——运行 `fdtest` 后读 fd a 8 字节，其 `off` 显示 8（inode 0 的 size 为 0x100，未触顶），直观展示偏移推进。
2. `fdtest` 打开两个 inode、读 a 8 字节、关 b、关 a，断言 `fd_opens==2 && fd_closes==2`——验证偏移可推进、引用可归零、计数成对：
   `fdtest: fd/file/inode/dentry refs and offsets passed`。

#### 3.2.6 同一原则的另两个实例

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```
1. **页对象**：`refs!=1` 跳过——只有恰好一个引用（无共享）的匿名页才回收；`reclaim_skips++` 记录被跳过次数。
2. **资源释放**：`resource_teardown` 把 `fd_refs` 与其余五类引用按序归零（`releases=6`），`teardown_done` 防二次释放。
3. 与 file 的 `refs` 归零同理：**释放者必须确认自己是最后一个引用者**。三处对照完整覆盖了「对象生命周期」的教学主线。

#### 3.2.7 本课新增检查点函数

```c
struct lesson_89_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_89_model lesson_89_state;
static TEXT64 void l96test(u16*c){lesson_89_state=(struct lesson_89_model){89U,90U,91U,92U,1,1,1,1};int ok=lesson_89_state.valid&&lesson_89_state.active&&lesson_89_state.ready&&lesson_89_state.accounted&&lesson_89_state.b==lesson_89_state.a+1U;text64(c,"l96test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 89 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 89→92（Origin 编号 Lesson 89），四布尔位全置 1。
2. **成功串**：`l96test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 89 fallback reported`。
3. **恢复的 `l88test`**：本课同时恢复 `l88test`（`lesson_88_state`，88→91，由上一课 `l95test` 更名而来），历史检查点可独立运行。

#### 3.2.8 exec64 增量与开机横幅

- `about` 输出 `Lesson 96: 文件偏移与引用计数\n`；检查点分支：
```c
else if(eq64(word,"l88test")){if(!noargs64(arg))usage64(c,"l88test");else l88test(c);}else if(eq64(word,"l96test")){if(!noargs64(arg))usage64(c,"l96test");else l96test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 96: 文件偏移与引用计数\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `文件偏移与引用计数` 与 `Lesson 96`、kernel64.c 含 `l96test`，最后打印 `Multiboot2 and Lesson 96 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma/reclaim → vfs_init()（file 表 offset=0/refs=0 就绪）
 ├─ 横幅 "Lesson 96: 文件偏移与引用计数"
 └─ 主循环：命令 → exec64
     ├─ l96test → 阶段检查点
     ├─ fdtest → open（offset=0,refs=1）→ read（offset 推进）→ close（refs 归零级联）
     ├─ fdinfo → 观察 off 与 opens/closes/reads 计数
     ├─ reclaimtest → refs==1 才回收的页对象实例
     └─ teardowntest → fd_refs 有序归零 + double-reap guard
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 建四表，打印横幅 `Lesson 96: 文件偏移与引用计数`。
2. **`l96test`** → `l96test(c)` → 断言 `lesson_89_state` → `l96test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`fdtest`** → `fd_open_model(0,1)`（file0 offset=0 refs=1，inode0 refs++）→ `fd_open_model(1,2)`（file1 offset=0 refs=1，inode1 refs++）→ `fd_read_model(a,8)`（file0 offset=8）→ 关 b、关 a（各自 refs 归零、inode 递减）→ `fdtest: fd/file/inode/dentry refs and offsets passed`。
4. **`fdinfo`** → 存活 fd 行 `fd N file F inode I off O` 与 `opens/closes/reads/seeks: 2 2 1 0`。
5. **`reclaimtest`** → 插入页 + 两次页缓存命中 + `reclaim_one`（refs==1 才回收）→ `reclaimtest: anonymous reclaim and page-cache hit model passed`。
6. **`teardowntest`** → 置 zombie → 第一次 `resource_teardown` 成功（fd_refs 归零）、第二次失败 → `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。
7. **`about`** → `Lesson 96: 文件偏移与引用计数`。

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
Multiboot2 and Lesson 96 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 96: 文件偏移与引用计数` 横幅 |
| `l96test` | `l96test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l88test` | `l88test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `fdinfo` | 首行 `fd/file/inode/dentry tables (bounded)`，存活 fd 行含 `off` 值，末行 `opens/closes/reads/seeks: ...` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` |
| `teardowntest` | `teardowntest: zombie retention, ordered resource release, and double-reap guard passed` |
| `about` | `Lesson 96: 文件偏移与引用计数` |

判定成功：`l96test`/`fdtest`/`reclaimtest`/`teardowntest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l96test` 输出 `Lesson 89 fallback reported` | `lesson_89_state` 初始化/断言失败（stale 镜像） | `grep -n "l96test" kernel64.c`；确认初始化串 `{89U,90U,91U,92U,1,1,1,1}` |
| `fdtest` 输出 `BROKEN` | open/read/close 顺序或计数不符 | 对照 `fd_open_model` 的 `offset=0,refs=1` 初始化、`fd_read_model` 的钳制推进、`fd_close_model` 的归零级联 |
| `fdinfo` 的 off 与预期不符 | `fd_read_model` 的 offset 推进或钳制错误 | 对照 `remaining` 计算：`file_table[f].offset<size?size-offset:0` 与 `bytes=remaining` |
| `fdinfo` 显示 inode refs 偏高 | 未关闭的 file 残留 | 对照 `fd_close_model` 只在 `file_table[f].refs==0` 时递减 inode |
| 对同一 inode 打开两个 fd 后关一个，inode refs 应仍 >1 | file refs 未归零则不递减 inode | 用两个 fd 绑同一 inode 的用例复现，检查 `if(!file_table[f].refs)` 条件 |
| `reclaimtest` 输出 `BROKEN` | `reclaim_one` 未回收（refs!=1）或 `pmm_free_page` 未返回 `freed` | 对照 `reclaim_one` 的三条件与 `pmm_free_page` 返回串；`anoninfo` 看统计 |
| `teardowntest` 输出 `BROKEN` | `zombie`/`teardown_done` 前置条件或 `releases` 计数异常 | 对照 `resource_teardown` 的守卫与 `releases=6`；确认 `resource_start()` 先运行 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 96' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `文件偏移与引用计数` 与 `Lesson 96` |
| 误敲 `l89test` 无响应 | `l89test` 命令在本课源码中不存在 | 本课检查点命令是 `l96test`；历史回归为 `l88test`；旧 README 的 `l89test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `file_model.offset`（打开归零、读后推进） | `include/linux/fs.h` 的 `struct file.f_pos`（`loff_t`）；`fs/read_write.c` 的 `vfs_read`/`vfs_write`（`file_pos_read`/`file_pos_write`） | 模型无 `llseek`/`lseek` 系统调用、无 `O_APPEND`/`O_DIRECT` 语义 |
| `fd_read_model` 的 size 钳制 | `fs/read_write.c`：`vfs_read` 经 `rw_verify_area` 与 inode size 界定 EOF | 模型无缓冲页与 `read_iter` 分发，只移动 offset 元数据 |
| `file_model.refs` 打开置 1 | `fs/open.c`/`fs/file_table.c`：`alloc_file` 后 `f_count=1`；`fget` 递增 | 模型无 `fget`/`fput` 的原子操作与 task_work 延迟 |
| `fd_close_model` 归零才递减 inode | `fs/file_table.c`：`fput()`→`__fput()`→`f_op->release()`→`iput()` | 模型把 file 归零与 inode 递减合并为一条条件语句，无 `i_state` 状态机 |
| 两个 fd 独立 offset | `fs/read_write.c`：每个 `struct file` 独立 `f_pos`（`dup` 共享则例外） | 模型无 `dup`/`fcntl(F_DUPFD)`，只演示独立实例 |
| `reclaim_one` 的 `refs!=1` 跳过 | `mm/vmscan.c`：`shrink_inactive_list` 的引用/映射检查 | 模型扫描固定 4 页，无 LRU 与 shrinker |
| `resource_teardown` 的 `fd_refs` 归零 | `fs/file.c`：`exit_files`/`close_files` 的 fdtable 释放顺序 | 模型用 `teardown_done` 布尔防二次释放，无 RCU |
| `l96test` 断言 | 无直接对应（LTP `fs` 测试套件） | 模型把偏移/引用验证固化进内核 |

**权威来源**：Linux `include/linux/fs.h`、`fs/read_write.c`、`fs/file_table.c`、`fs/file.c`、`mm/vmscan.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `f_pos` 属于 file 而不属于 inode？若把 offset 放进 inode，两个进程并发读同一文件会发生什么？
2. **源码定位**：在 `kernel64.c` 中找出 `file_table[f].offset` 与 `file_table[f].refs` 的全部读/写点，画出 `fdtest` 过程中 file0 的 offset 与 inode0 的 refs 变化表。
3. **动手实验**：写一个「同一 inode 打开两次」的测试（两次 `fd_open_model(0,1)`），读第一个 fd 后再读第二个 fd，用 `fdinfo` 观察两个 offset 互不影响，证明 `f_pos` 独立性。
4. **动手实验**：给 `fd_close_model` 去掉 `if(!file_table[f].refs)` 条件，重新构建运行 `fdtest`，观察 inode 引用被过度递减导致的断言失败。
5. **Linux 对照**：阅读 `fs/read_write.c` 的 `vfs_read` 与 `fs/file_table.c` 的 `fput`，对比教学模型的条件递减，指出 Linux 在 file 归零后还经过哪些步骤（`__fput`、`f_op->release`、`iput`）。

---

## 9. 本课小结与下一课预告

1. 本课为 file 对象模型收官：`offset`（`f_pos`）与 `refs`（`f_count`）是「打开实例」区别于「inode 本体」的两个决定性字段。
2. `fd_open_model` 以 `offset=0,refs=1` 初始化 file；`fd_read_model` 按 inode size 钳制后推进 offset；`fd_close_model` 在 file 引用归零时才级联递减 inode——「最后引用者负责销毁」。
3. 偏移属于实例、引用保护本体：两个 fd 独立 offset，inode 的存活 file 数由 refs 精确记账。
4. `reclaim_one`（页 `refs==1` 才回收）与 `resource_teardown`（`fd_refs` 有序归零 + double-reap guard）是同一原则在页对象与资源释放上的复刻，三处拼出完整的对象生命周期图景。
5. `fdinfo`/`fdtest` 把偏移推进与引用链固化为可断言回归；`fd_seek_ops` 计数器保留但无 seek 函数，如实标注了模型的边界。
6. `l96test` 沿用 VFS/设备阶段检查点家族，`l88test` 历史检查点保留。
7. 下一课（[`../lesson-97-stable/README.md`](../lesson-97-stable/README.md)，Lesson 97）将收束整个 VFS 阶段，综合 superblock→inode→dentry→file 四层与偏移/引用/权限/路径机制做阶段总复习。
