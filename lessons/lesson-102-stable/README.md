# Lesson 102: 设备生命周期与卸载 — 精讲文档

> **课号**：Lesson 102 ｜ **主题**：设备生命周期与卸载（device lifecycle and unload）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 95 原型的检查点，也是 Lesson 98–101 设备系列的收尾课
> **前置课程**：[`../lesson-101-stable/README.md`](../lesson-101-stable/README.md)（块设备请求队列）
> **后续课程**：[`../lesson-103-stable/README.md`](../lesson-103-stable/README.md)（下一主题课）
> **一句话目标**：把 Lesson 98–101 的注册、节点、打开、队列串成完整的设备生命周期，并聚焦「卸载/释放」这一末端——模块的 `exit_calls`、`resource_teardown` 的有序释放与 double-reap 防护、`fd_close_model` 的引用递减，对照 Linux 的 `cdev_del`/`blkdev_put`/模块卸载路径，验证 `l102test` 检查点。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l101test` 恢复为历史命名 `l94test`（挂在 `lesson_94_state` 上）、新增 `lesson_95_model` 状态与 `l102test` 检查点、更新 `about`/开机横幅为本课主题。设备生命周期与卸载机制由累积代码承载：模块模型自带 `exit_calls`（卸载计数），`resource_ledger`/`resource_teardown` 提供有序释放与 double-reap 防护，`fd_close_model`/`reclaim_one` 提供引用递减与页回收。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l102test`（旧 README 写的 `l95test` 按源码勘误）；另保留历史检查点 `l82test`–`l94test`，以及 `teardowntest`/`resourceinfo`/`waittest`/`reclaimtest` 等释放与回收回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出一个设备从出生到死亡的完整生命周期阶段（probe/注册 → 设备节点 → 打开/使用 → 卸载/释放）以及每阶段 Linux 对应的函数（`cdev_add`/`cdev_del`、`blkdev_get`/`blkdev_put`、模块 `init`/`exit`）；解释「卸载为什么不能乱序」与「为什么不能释放两次」（double-reap guard）；在教学内核中沿 `module_init_model`（`init_calls`）→ `resource_start` → `resource_teardown`（`releases=6`、`teardown_done`）走一遍生命周期末端，并说明 `fd_close_model` 的引用递减如何与卸载配合；运行 `l102test`/`teardowntest`/`waittest` 验证。

**在课程主线中的位置**：Lesson 98–101 分别讲了设备注册、节点/编号、打开/ioctl、请求队列——都是生命周期的「出生到使用」段。本课补齐「死亡段」：对象不能消失即释放，必须按依赖顺序递减引用、回收资源、并防止二次释放。作为设备系列收尾课，源码 diff 极小，任务是把继承机制（模块 exit、资源 teardown、fd 关闭、页回收）按「生命周期末端」主题系统化复述。下一课（Lesson 103）转向新的主题。

**前置知识清单**（学本课前必须掌握）：
1. 设备注册与节点：`cdev_add`/dev_t/设备节点（Lesson 98–99）；打开与 ioctl（Lesson 100）；请求队列（Lesson 101）。
2. 模块模型：`module_init_model` 的 `init_calls`/`exit_calls` 字段与 `module_lookup`（Lesson 89/98）。
3. 引用计数原则：「引用归零才销毁」在 inode/file/页对象上的实现（Lesson 90/96/97）。
4. 资源 ledger：`resource_ledger` 与 `resource_start` 的初始化（Lesson 41–43/90）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位（Lesson 69–101）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 102: 设备生命周期与卸载`；
- 新命令 `l102test` 输出 `l102test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `teardowntest`/`resourceinfo` 展示有序释放与 double-reap 防护，`waittest`/`reclaimtest` 展示进程回收与页释放。

---

## 2. 核心概念精讲

### 2.1 设备生命周期全景：出生、使用、死亡

把 Lesson 98–101 的对象串成一条时间线：

```
出生（probe/注册）          使用                       死亡（释放/卸载）
──────────────────────────────────────────────────────────────────────
驱动 probe()                open() 建 file 元数据        last fput() → __fput
   → cdev_init/cdev_add      read/write/ioctl             → 设备 release() 回调
   → register_chrdev_region  请求下发 request_queue        → cdev_del
   → blk_alloc_queue         页缓存读写                    → unregister_chrdev_region
   → device_add / mknod                                    → blk_cleanup_queue
                                                          → driver_unregister / remove
```

Linux 设备模型（`drivers/base/dd.c` 的 `device_add`/`device_del`、`driver_register`/`driver_unregister`）对每个阶段都有配对函数：**每个「注册」都对应一个「注销」，每个「get」都对应一个「put」**。生命周期管理的全部艺术就在于保证这些配对严格发生、按序发生、且只发生一次。

### 2.2 卸载三原则

1. **先依赖、后依赖者**：释放必须从「最不依赖别人」的对象开始——地址空间先于 fd、fd 先于 inode，否则会使用已释放的内存；
2. **引用计数归零才销毁**：`fput`/`iput`/`bdput` 把计数减到 0 才真正释放，否则对象还活着（Lesson 90/96）；
3. **禁止 double-reap**：同一个对象不能被释放两次——需要标志位或状态机把「已释放」固化下来。

### 2.3 教学内核的卸载三件套

- **模块 `exit_calls`**：`struct module_model{name_hash,init_calls,exit_calls,loaded,initialized}`——`init_calls` 对应注册（加载），`exit_calls` 对应卸载（当前为 0，模型只演示 init 路径）；真实内核的模块 `exit` 函数正是 `cdev_del`/`blk_cleanup_queue` 等注销动作的宿主。
- **`resource_teardown`**：有序清空六类资源（地址空间→fd→管道→信号→定时器→延迟工作），`releases=6` 计数，`teardown_done=1` 防二次释放——「先依赖后依赖者」+「double-reap guard」的教学实现。
- **`fd_close_model`**：file 引用递减，末个 file 关闭时递减 inode——「引用归零才销毁」在 fd 层的实现（Lesson 90/96）。

### 2.4 检查点模型：lesson_95_model 与 l102test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `95→98` 标记 Origin 为 Lesson 95。本课同时把上一课新增的 `l101test` 恢复为历史命名 `l94test`（同一 `lesson_94_state`，计数 `94→97`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理，至此 `l82test`–`l94test` 历史检查点全部按 Origin 命名。

### 2.5 机制继承 + 检查点增量

本课主题机制（生命周期末端、有序卸载）不是本课新写的代码：模块 `exit_calls` 来自模块阶段，`resource_teardown` 来自资源管理阶段，`fd_close_model`/`reclaim_one` 来自 VFS/页回收阶段。本课的实际增量只有三处：`l101test`→`l94test` 更名、`lesson_95_model`+`l102test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「生命周期与卸载」主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l101test`→`l94test` 恢复命名；新增 `lesson_95_model`/`lesson_95_state`/`l102test`；`about` 与开机横幅更新。生命周期/卸载机制由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`设备生命周期与卸载`/`l102test`/`Lesson 102`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（生命周期/卸载机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_95_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_95_model lesson_95_state;
static TEXT64 void l102test(u16*c){lesson_95_state=(struct lesson_95_model){95U,96U,97U,98U,1,1,1,1};int ok=lesson_95_state.valid&&lesson_95_state.active&&lesson_95_state.ready&&lesson_95_state.accounted&&lesson_95_state.b==lesson_95_state.a+1U;text64(c,"l102test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 95 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `95→98`（Origin Lesson 95），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l102test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 95 fallback reported`。
3. **恢复的 `l94test`**：本课同时把 `l101test` 更名回 `l94test`（同为 `lesson_94_state`），使检查点命令名与 Origin 对齐；`l82test`–`l93test` 历史检查点全部保留。

#### 3.2.2 模块的卸载字段：exit_calls

```c
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
```
```c
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
```
1. **`init_calls` 与 `exit_calls` 成对出现**：`modules[0]=core` 与 `modules[1]=vfs` 的 `init_calls=1` 表示各注册一次，`exit_calls=0` 表示尚未卸载——`struct module_model` 为「注册/注销配对」预留了对称的字段，这正是 Linux 模块 `init`/`exit` 函数的元数据模型（`kernel/module/main.c` 的 `do_init_module`/`free_module`）。
2. **`loaded`/`initialized` 状态位**：卸载前必须检查这些状态位——已卸载的对象不能再释放（double-reap 防护在模块层的体现）。
3. **与设备注销对照**：真实驱动的 `exit` 函数体就是 `cdev_del(&cdev)`、`unregister_chrdev_region(...)`、`blk_cleanup_queue(...)` 的集合——`exit_calls` 就是这组注销动作的计数器。

#### 3.2.3 卸载的核心：resource_teardown 有序释放

```c
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
static struct resource_ledger resource_ledger;
static TEXT64 void resource_start(void){resource_ledger=(struct resource_ledger){1,2,1,1,1,1,0,0,1,0};}
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
```
1. **前置守卫**：`!zombie || teardown_done` 时拒绝——两个条件分别对应「对象还活着不能销毁」（进程未退出，对照 Linux `device_del` 前必须 `driver_remove` 干净）与「已经销毁过一次不能再来」（double-reap guard）。
2. **有序归零**：地址空间（`address_space`）→ fd 引用（`fd_refs`）→ 管道引用（`pipe_refs`）→ 信号引用（`signal_refs`）→ 定时器引用（`timer_refs`）→ 延迟工作引用（`deferred_refs`）——严格按「先依赖后依赖者」顺序，与 Linux 卸载顺序（先停 I/O、再断引用、最后释放对象）同构。
3. **`releases=6` 计数**：一次 teardown 恰好释放六类资源，`teardowntest` 以此断言「全部释放」。
4. **`teardown_done=1` 固化状态**：第二次调用因守卫直接返回 0——释放动作幂等。

#### 3.2.4 卸载的验证：teardowntest

```c
static TEXT64 void teardowntest(u16*c){resource_start();resource_ledger.zombie=1;int a=resource_teardown(),b=!resource_teardown(),d=resource_ledger.address_space==0&&resource_ledger.releases==6;text64(c,"teardowntest: ");text64(c,a&&b&&d?"zombie retention, ordered resource release, and double-reap guard passed":"BROKEN");putc64(c,'\n');}
```
1. **复位并置僵尸**：`resource_start()` 复位 ledger，`zombie=1` 表示进程已退出、进入可回收状态（对应设备「已无使用者」）。
2. **首次 teardown 成功**：`a=resource_teardown()` 必须返回 1——有序释放执行。
3. **二次 teardown 失败**：`b=!resource_teardown()`——`teardown_done` 守卫使第二次返回 0，double-reap 被拦截。
4. **资源清零断言**：`address_space==0 && releases==6`——六类资源全部释放且计数正确。
5. **成功串**：`teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。

#### 3.2.5 引用递减与释放路径：fd_close_model 与 reclaim_one

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
```
1. **三层悬垂校验**：fd→file→inode 逐层查 valid——释放路径同样要防悬垂（与 `fd_open_model` 对称）。
2. **file 引用递减**：`file_table[f].refs--`；仅当归零（`!refs`）才 `valid=0` 并级联 `inode_table[n].refs--`——「引用归零才销毁」在 fd 层的实现（对应 `fs/file_table.c` 的 `__fput`→`iput`）。
3. **与卸载配合**：`resource_teardown` 的 `fd_refs=0` 是「一次性把 fd 引用清账」的批量版；`fd_close_model` 是「逐个递减」的精细版——同一引用计数原则的两种应用。

页对象释放（块缓存/匿名页的「卸载」）：
```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```
1. **`refs!=1` 跳过**：只有恰好一个引用的页才回收——对象还被共享时绝不能释放。
2. **真实归还**：`pmm_free_page` 返回 `freed` 才置 `live=0`——物理帧归还 PMM（对应 `__free_pages`）。
3. **与 teardown 的关系**：`reclaim_one` 是页级卸载，`resource_teardown` 是资源级卸载，`fd_close_model` 是 fd 级卸载——同一「引用归零才销毁」原则贯穿三个层级。

#### 3.2.6 生命周期末端的进程对象：waittest 的僵尸与 reap

```c
static TEXT64 void waittest(u16*c){wait_model_start();int a=!wait_model_wait(),b=wait_model_exit(42),d=wait_model_wait(),e=wait_model.state==WAIT_ZOMBIE&&wait_model.exit_code==42,f=wait_model_reap(),g=wait_model.state==WAIT_DEAD;text64(c,"waittest: ");text64(c,a&&b&&d&&e&&f&&g?"bounded wait, exit status, zombie selection, and reap passed":"BROKEN");putc64(c,'\n');}
```
1. **僵尸保留**：`wait_model_exit(42)` 后进程进入 `WAIT_ZOMBIE`，退出码 42 被保留——僵尸状态让父进程能读到退出状态（`resource_ledger.zombie` 的同款语义）。
2. **reap 才真正销毁**：`wait_model_reap()` 后状态变 `WAIT_DEAD`——「僵尸保留 → 收割销毁」与设备「资源 ledger → teardown」的生命周期末段同构。
3. **成功串**：`waittest: bounded wait, exit status, zombie selection, and reap passed`。

#### 3.2.7 exec64 增量与开机横幅

- `about` 输出 `Lesson 102: 设备生命周期与卸载\n`；检查点分支：
```c
else if(eq64(word,"l94test")){if(!noargs64(arg))usage64(c,"l94test");else l94test(c);}else if(eq64(word,"l102test")){if(!noargs64(arg))usage64(c,"l102test");else l102test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 102: 设备生命周期与卸载\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `设备生命周期与卸载`、`Lesson 102`，kernel64.c 含 `l102test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model()（init_calls=1；exit_calls=0 待卸载）
 ├─ resource_start()（资源 ledger 初始化，zombie=1）
 ├─ vfs_init() → fd/file/inode 表 + ramfs
 ├─ 横幅 "Lesson 102: 设备生命周期与卸载"
 └─ 主循环：命令 → exec64
     ├─ l102test / l94test → 阶段检查点（lesson_95_state / lesson_94_state）
     ├─ teardowntest / resourceinfo → 有序释放 + double-reap 验证/观察
     ├─ waittest / reaptest → 进程僵尸保留与 reap
     ├─ reclaimtest / anoninfo → 页级卸载与回收统计
     └─ fdtest / fdinfo → fd 引用递减路径回归
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`module_init_model()` 注册 core/vfs（`init_calls=1`），`resource_start()` 初始化 ledger（`zombie=1`），打印横幅 `Lesson 102: 设备生命周期与卸载`。
2. **`l102test`** → `l102test(c)` → 断言 → `l102test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`teardowntest`** → `resource_start()` → 置 `zombie=1` → 首次 `resource_teardown` 成功、二次失败 → `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。
4. **`waittest`** → `wait_model_exit(42)` → 僵尸 → `wait_model_reap` → `waittest: bounded wait, exit status, zombie selection, and reap passed`。
5. **`reclaimtest`** → 插页 + 缓存命中 + `reclaim_one` → `reclaimtest: anonymous reclaim and page-cache hit model passed`。
6. **`fdtest`** → 两次 open + read + 两次 close（file 归零才递减 inode）→ `fdtest: fd/file/inode/dentry refs and offsets passed`。
7. **`l94test`**（历史检查点） → `l94test: bounded VFS, devices, epoll, and service management checkpoint passed`。
8. **`about`** → `Lesson 102: 设备生命周期与卸载`。

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
Multiboot2 and Lesson 102 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 102: 设备生命周期与卸载` 横幅 |
| `l102test` | `l102test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l94test` | `l94test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `teardowntest` | `teardowntest: zombie retention, ordered resource release, and double-reap guard passed` |
| `waittest` | `waittest: bounded wait, exit status, zombie selection, and reap passed` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` |
| `about` | `Lesson 102: 设备生命周期与卸载` |

判定成功：`l102test`/`teardowntest`/`waittest`/`reclaimtest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l102test` 输出 `Lesson 95 fallback reported` | `lesson_95_state` 初始化/断言失败（stale 镜像） | `grep -n "l102test" kernel64.c`；确认初始化串 `{95U,96U,97U,98U,1,1,1,1}` 与 `b==a+1U` |
| `teardowntest` 输出 `BROKEN` | 首次 teardown 返回 0 或二次 teardown 返回 1 | 对照 `resource_teardown` 的守卫 `!zombie||teardown_done`；确认 `resource_start()` 先运行且 `zombie=1` |
| `teardowntest` 的 `releases` 不是 6 | 六类资源未全部归零 | 对照 `resource_teardown` 的六个 `=0` 赋值与 `releases=6`；`resourceinfo` 看各项 |
| `waittest` 输出 `BROKEN` | 僵尸状态/退出码/reap 断言异常 | 对照 `wait_model_exit`/`wait_model_reap` 的状态机；确认 `wait_model_start()` 先运行 |
| `fdtest` 输出 `BROKEN` | 引用递减只在末个 file 关闭时发生 | 对照 `fd_close_model` 的 `if(!file_table[f].refs){...refs--}`；`fdinfo` 看 inode refs |
| `reclaimtest` 输出 `BROKEN` | `reclaim_one` 的 `refs!=1` 跳过或 `pmm_free_page` 未返回 `freed` | 对照 `reclaim_one` 三条件；`anoninfo` 看 reclaim 统计 |
| `l102test` 与 `l94test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l102test` 只操作 `lesson_95_state`、`l94test` 只操作 `lesson_94_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 102' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `设备生命周期与卸载` 与 `Lesson 102` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `module_model.exit_calls`（卸载计数） | `kernel/module/main.c`：`do_init_module()`/`free_module()`（模块 init/exit 生命周期）；`delete_module()` | 模型只注册不卸载（`exit_calls=0`），无模块引用计数与等待队列 |
| `resource_teardown` 有序释放六类资源 | `drivers/base/dd.c`：`device_del()`/`driver_unregister()` 的 remove 顺序；`fs/char_dev.c` `cdev_del()`+`unregister_chrdev_region()`；`block/blk-core.c` `blk_cleanup_queue()` | 模型用六次 `=0` 模拟，无真实对象与 RCU 宽限期 |
| `teardown_done` double-reap guard | 设备模型引用计数（`device_initialize`/`put_device`）与 `kobject` 释放路径 | 模型用布尔标志防二次释放，无 kref 计数与 release 回调 |
| `zombie` 前置守卫 | 进程 `fs/exit.c` 的 `exit_mm`/`exit_files` 有序释放；设备「无使用者才 remove」 | 模型用单布尔表达「可回收」状态 |
| `fd_close_model` 末个 file 递减 inode | `fs/file_table.c`：`__fput()` → `iput()`；`fs/inode.c` `iput_final()` | 模型把 file 归零与 inode 递减合并，无 `i_state` 状态机 |
| `reclaim_one` 的 `refs==1` 才回收 | `mm/vmscan.c`：`shrink_inactive_list()`；`mm/page_alloc.c` `__free_pages()` | 模型扫描固定 4 页，无 LRU 与 shrinker |
| `waittest` 僵尸保留与 reap | `kernel/exit.c`：`exit_notify()`（僵尸化）、`wait()` 收割；`wait_task_zombie()` | 模型单进程状态机，无进程组与线程组语义 |
| `l102test` 断言 | 无直接对应（LTP / blktests 卸载测试） | 模型把生命周期末端主题的可验证状态固化进内核 |

**权威来源**：Linux `kernel/module/main.c`、`fs/char_dev.c`、`fs/block_dev.c`、`block/blk-core.c`、`drivers/base/dd.c`、`fs/file_table.c`、`kernel/exit.c` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么卸载要遵循「先依赖、后依赖者」的顺序？如果先释放 inode 再释放 fd 会发生什么？
2. **源码定位**：在 `kernel64.c` 中找出所有「double-reap guard」形态的守卫（提示：`resource_teardown` 的 `teardown_done`、`reclaim_one` 的 `!m->live`、`wait_model_reap` 的状态机），逐一说明它们防止什么。
3. **动手实验**：给 `resource_teardown` 增加一个「未释放的 `deferred_refs` 计数」错误注入（把 `deferred_refs=0` 改成 `deferred_refs=1`），运行 `teardowntest`，观察 `releases==6` 仍成立但资源未清零——解释为什么断言应加上资源清零检查。
4. **动手实验**：修改 `fd_close_model`，让 file 关闭时**总是**递减 inode（去掉 `if(!file_table[f].refs)` 条件），运行 `fdtest` 观察两个 file 打开同一 inode 时引用链是否被破坏。
5. **Linux 对照**：阅读 `fs/char_dev.c` 的 `cdev_del` 与 `block/blk-core.c` 的 `blk_cleanup_queue`，说明真实内核在注销字符/块设备时分别清理了什么；对比教学模型 `resource_teardown` 六类归零的简化。

---

## 9. 本课小结与下一课预告

1. 本课把 Lesson 98–101 串成完整设备生命周期：probe/注册 → 设备节点 → 打开/使用 → 卸载/释放，每一阶段都有配对的注册/注销函数。
2. 卸载三原则：先依赖后依赖者、引用计数归零才销毁、禁止 double-reap——`resource_teardown`、`fd_close_model`、`reclaim_one` 分别是资源/fd/页三个层级的实现。
3. 模块模型自带 `init_calls`/`exit_calls` 对称字段，`exit_calls` 就是设备注销动作（`cdev_del`/`blk_cleanup_queue`）的计数器。
4. `teardowntest` 用「首次成功、二次失败、`releases==6`」三断言把有序释放与 double-reap 防护固化下来；`waittest` 展示了进程对象的僵尸保留与 reap。
5. 检查点增量：新增 `l102test`（Origin Lesson 95），恢复历史命名 `l94test`，横幅与 `about` 更新——至此 `l82test`–`l94test` 全部按 Origin 命名。
6. 下一课（Lesson 103）将开启新主题，从设备系列转向课程主线的下一阶段；本课的「注册/使用/卸载」生命周期方法论将作为后续对象管理（进程、模块、文件系统）的通用范式。
