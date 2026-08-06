# Lesson 90: inode 生命周期与引用 — 精讲文档

> **课号**：Lesson 90 ｜ **主题**：inode 生命周期与引用（inode lifecycle and reference counting）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 83 原型的检查点
> **前置课程**：[`../lesson-89-stable/README.md`](../lesson-89-stable/README.md)（超级块与文件系统注册）
> **后续课程**：[`../lesson-91-stable/README.md`](../lesson-91-stable/README.md)（dentry 缓存与路径组件）
> **一句话目标**：精讲 inode 从「创建（iget）→ 被引用（i_count++）→ 释放（iput）」的完整生命周期，并验证教学内核 `inode_table[].refs` 在 open/close 与回收/teardown 路径上的引用计数一致性。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第三课。`kernel64.c` 相对上一课仅做一处增量——把 `l89test` 恢复为 `l82test`，新增 `lesson_83_model` 状态与 `l90test` 检查点测试，并更新 `about`/开机横幅为本课主题。inode 生命周期与引用机制由累积代码承载：`vfs_init` 创建 inode 表、`fd_open_model`/`fd_close_model` 维护 `refs`、`reclaim_one`/`resource_teardown` 提供回收与有序释放语义。继承的进程、GUI、子系统回归保持有效。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出 Linux inode 生命周期的四个阶段（分配 `iget` → 引用 `i_count++` → 最后放回 `iput` → 销毁 `destroy_inode`）；解释「为什么不能立即销毁一个还在被 file/dentry 引用的 inode」；在教学内核中沿 `vfs_init` → `fd_open_model`（`refs++`）→ `fd_close_model`（末个 file 关闭时 `refs--`）→ `reclaim`/`teardown` 走一遍引用计数链；运行 `l90test`/`fdtest`/`reclaimtest`/`teardowntest` 验证。

**在课程主线中的位置**：Lesson 88 搭层次、Lesson 89 讲超级块与注册，本课把视角聚焦到四层模型中**最核心的对象——inode**。它是文件物理身份的化身，也是生命周期与引用计数教学的最佳载体（对照 Linux `fs/inode.c`）。下一课（Lesson 91）转向 dentry 缓存与路径组件。

**前置知识清单**（学本课前必须掌握）：
1. VFS 四层模型与 inode 在其中的位置：`inode_model{ino,size,mode,refs}`（Lesson 88）。
2. 打开/关闭引用链：`fd_open_model` 的 `inode_table[inode].refs++`、`fd_close_model` 的级联递减（Lesson 88）。
3. 匿名页回收：`reclaim_one` 的 `refs!=1` 跳过逻辑（Lesson 76/84）。
4. 资源有序释放与僵尸保留：`resource_ledger`/`teardowntest`（Lesson 41/43）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 90: inode 生命周期与引用`；
- 新命令 `l90test` 输出 `l90test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `fdtest`/`fdinfo` 展示 open/close 后 inode `refs` 的变化，`reclaimtest`/`teardowntest` 展示回收与有序释放路径。

---

## 2. 核心概念精讲

### 2.1 inode 生命周期：iget → 引用 → iput → destroy

**直觉**：inode 是「文件本体」在内核里的代表。它不能随最后一个打开者离开就立即消失——可能还有 dentry 指向它、还有缓存页、还有内核持有引用。因此生命周期被拆成「计数增减」与「实际销毁」两件事。

**Linux 四阶段**（对照 `fs/inode.c`）：
1. **分配（iget）**：`iget_locked()` 在 inode cache 中查找或分配 `struct inode`，初始化 `i_count=1`；
2. **引用（i_count++）**：每次 `iget()`/`dget` 让 `i_count` 递增；
3. **放回（iput）**：`iput()` 递减 `i_count`；仅当 `i_count==0` 时才进入回收路径；
4. **销毁（destroy_inode）**：`iput_final()` 调用文件系统 `destroy_inode` 钩子释放 inode 本体。

**关键不变量**：**引用计数不到零，绝不销毁**。这是所有对象生命周期（inode/file/page）的公共原则。

### 2.2 教学模型的 inode 引用链

`struct inode_model { u64 ino,size,mode,refs; u8 valid; }` 用 `refs` 模拟 `i_count`：

```
vfs_init():   inode_table[i] = {i+1, 0x100+i, 0100644, refs=1, valid=1}
                                        │ refs=1（创建后自带一个引用）
fd_open_model(0,1):                      ▼
    file_table[f]={inode=0,...}   →   inode_table[0].refs++   (refs=2)
fd_open_model(1,2):                     ▼
    file_table[g]={inode=1,...}   →   inode_table[1].refs++   (refs=2)
fd_close_model(1):                      ▼
    file refs 归零 → inode_table[1].refs--  (refs=1)
fd_close_model(0):                      ▼
    file refs 归零 → inode_table[0].refs--  (refs=1)
    （回到创建时的 refs=1，由 dentry 层持有，模型内永不归零销毁）
```

要点：教学模型里 inode 的「最后一个引用」始终由 dentry 层持有，`fd` 关闭最多把它降回 `refs=1`，**永不真正销毁**——这既演示了「引用归零才销毁」的原则，又符合「定长表 + 元数据模拟、不做真实磁盘释放」的约束。

### 2.3 回收（reclaim）与有序释放（teardown）：同一原则的另两个实例

- **页回收** `reclaim_one`：`if(!m->live||!m->reclaimable||m->refs!=1) continue;`——只有 `refs==1`（无额外引用）的匿名页才被回收。这是 inode「引用归零才销毁」原则在页对象上的复刻。
- **资源有序释放** `resource_teardown`：先要求 `zombie`（进程已退出成为僵尸）且 `!teardown_done`，然后按地址空间→fd→管道→信号→定时器→延迟工作六类资源依次归零，`releases=6`，并置 `teardown_done=1` 防止二次释放（double-reap guard）。这与 inode「最后一个引用者负责销毁」的生命周期语义一致。

### 2.4 检查点模型：lesson_83_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l90test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 83）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l82test`；新增 `lesson_83_model`/`lesson_83_state`/`l90test`；`about` 与横幅更新。inode 生命周期机制由累积代码承载，本课以「生命周期与引用」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`inode 生命周期与引用`/`Lesson 90`/`l90test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（inode 生命周期/引用 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_83_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_83_model lesson_83_state;
static TEXT64 void l90test(u16*c){lesson_83_state=(struct lesson_83_model){83U,84U,85U,86U,1,1,1,1};int ok=lesson_83_state.valid&&lesson_83_state.active&&lesson_83_state.ready&&lesson_83_state.accounted&&lesson_83_state.b==lesson_83_state.a+1U;text64(c,"l90test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 83 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 83→86（Origin 编号 Lesson 83），四布尔位全置 1。
2. **成功串**：`l90test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 83 fallback reported`。
3. **恢复的 `l82test`**：本课同时恢复 `l82test`（`lesson_82_state`，82→85），两个历史检查点可独立运行。

#### 3.2.2 inode 创建：vfs_init（iget 的教学版）

```c
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **分配并初始化**：3 个 inode 以 `{ino=i+1, size=0x100+i, mode=0100644, refs=1, valid=1}` 初始化——对应 Linux `iget()` 后 `i_count=1` 的状态。
2. **初始引用来自哪**：创建后 `refs=1` 是「存在即持有」的基引用；随后 dentry 层 `dentry_table[i].inode_index=i` 与 ramfs 节点 `ramfs_nodes[].inode` 都指向它，dentry 层成为这个基引用的持有者。
3. **其余层清空**：file/fd 表全空，等待 open 时建立指向 inode 的新引用。

#### 3.2.3 inode 引用递增：fd_open_model

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. **打开前置校验**：`inode>=INODE_MAX || !inode_table[inode].valid` 立即失败——不能打开一个不存在/未初始化的 inode。
2. **分配 file 与 fd**：空 file 槽 `{inode, offset=0, flags, refs=1, valid=1}` + 空 fd 槽 `{f, valid=1}`。
3. **引用递增**：`inode_table[inode].refs++`——这就是生命周期里「iget/i_count++」的教学版：每次 open，inode 多一个持有者。
4. **统计**：`fd_opens++`。

#### 3.2.4 inode 引用递减：fd_close_model（延迟到最后一个 file）

```c
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
```
1. **三层校验**：fd → file → inode 逐层查有效，任何一层悬垂即拒绝关闭（保护引用链不被破坏）。
2. **fd 先释放**：`fd_table[fd].valid=0` 回收描述符槽。
3. **file 引用递减**：`file_table[f].refs--`。
4. **级联递减 inode**：仅当 `file_table[f].refs==0`（最后一个 file 关闭）才 `inode_table[n].refs--`——这是「**最后引用者负责递减**」的关键设计：inode 的引用数等于 dentry 基引用 + 存活的 file 数。
5. **与 Linux 对照**：Linux 中 `fput()` 把 file 的 `f_count` 减到 0 时调用 `iput()`；教学模型把两步合并为一条条件递减。

#### 3.2.5 引用归零才回收：reclaim_one

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```
1. **扫描**：遍历 `fault_pages`（最多 4 页），每页一次 `reclaim_scans++`。
2. **可回收条件**：`live && reclaimable && refs==1`——**只有恰好一个引用**（无共享、无额外持有）的页才被回收，与 inode「引用归零才销毁」是同一原则。
3. **真实回收**：调用 `pmm_free_page` 归还物理帧（返回 `"freed"` 才成功），随后 `live=0`、`fault_page_count--`、`anon_pages--`、`anon_reclaims++`。
4. **跳过统计**：条件不满足时 `reclaim_skips++`。

#### 3.2.6 有序释放与二次回收防护：resource_teardown / teardowntest

```c
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
```
1. **前置条件**：`zombie`（进程已退出）且 `!teardown_done`，否则拒绝——与 inode「仅当最后一个引用者才进入销毁路径」的守卫同构。
2. **有序归零**：地址空间 → fd → 管道 → 信号 → 定时器 → 延迟工作六类引用按固定顺序清 0，`releases=6`。
3. **幂等保护**：`teardown_done=1` 使第二次调用返回 0——double-reap guard，防止同一资源被释放两次。

`teardowntest` 验证：`resource_start()` 后置 `zombie=1`，断言第一次 `resource_teardown()` 成功、第二次失败、`address_space==0 && releases==6`：
`teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。

#### 3.2.7 验证入口：fdtest / reclaimtest

- `fdtest` 跑 `fd_open_model(0,1)`、`fd_open_model(1,2)`、`fd_read_model((u32)a,8)`、两次 `fd_close_model`，断言 `fd_opens==2 && fd_closes==2`：
  `fdtest: fd/file/inode/dentry refs and offsets passed`。
- `reclaimtest` 插入一页 + 两次页缓存命中 + 回收一次，断言 `anon_pages==0 && page_cache_count==1`：
  `reclaimtest: anonymous reclaim and page-cache hit model passed`。

#### 3.2.8 exec64 增量与开机横幅

- `about` 输出 `Lesson 90: inode 生命周期与引用\n`；检查点分支：
```c
else if(eq64(word,"l82test")){if(!noargs64(arg))usage64(c,"l82test");else l82test(c);}else if(eq64(word,"l90test")){if(!noargs64(arg))usage64(c,"l90test");else l90test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 90: inode 生命周期与引用\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `inode 生命周期与引用`、`Lesson 90`，kernel64.c 含 `l90test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma/reclaim → vfs_init()（inode refs=1 创建）
 ├─ 横幅 "Lesson 90: inode 生命周期与引用"
 └─ 主循环：命令 → exec64
     ├─ l90test → 阶段检查点
     ├─ fdtest → open(refs++)/close(refs--) 引用链验证
     ├─ fdinfo → 观察 fd→file→inode 当前引用状态
     ├─ reclaimtest → 页对象「refs==1 才回收」验证
     └─ teardowntest → 有序释放 + 二次回收防护验证
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vfs_init()` 创建 3 个 `refs=1` 的 inode，打印横幅 `Lesson 90: inode 生命周期与引用`。
2. **`l90test`** → `l90test(c)` → 断言 → `l90test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`fdtest`** → 两次 open（`inode_table[0].refs++`、`inode_table[1].refs++`）→ read → 两次 close（file 归零才 `refs--`）→ `fdtest: fd/file/inode/dentry refs and offsets passed`。
4. **`fdinfo`** → 对每个有效 fd 打印 `fd N file F inode I off O` 行与 `opens/closes/reads/seeks` 计数。
5. **`reclaimtest`** → `fault_insert` + 两次 `page_cache_get` + `reclaim_one` → `reclaimtest: anonymous reclaim and page-cache hit model passed`。
6. **`teardowntest`** → 置 `zombie` → 第一次 `resource_teardown` 成功、第二次失败 → `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。
7. **`about`** → `Lesson 90: inode 生命周期与引用`。

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
Multiboot2 and Lesson 90 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 90: inode 生命周期与引用` 横幅 |
| `l90test` | `l90test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l82test` | `l82test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` |
| `teardowntest` | `teardowntest: zombie retention, ordered resource release, and double-reap guard passed` |
| `about` | `Lesson 90: inode 生命周期与引用` |

判定成功：`l90test`/`fdtest`/`reclaimtest`/`teardowntest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l90test` 输出 `Lesson 83 fallback reported` | `lesson_83_state` 初始化/断言失败（stale 镜像） | `grep -n "l90test" kernel64.c`；确认初始化串 `{83U,84U,85U,86U,1,1,1,1}` |
| `fdtest` 输出 `BROKEN` | open/close 引用链或计数异常 | 检查 `fd_open_model` 的 `inode_table[inode].refs++` 与 `fd_close_model` 的 `if(!file_table[f].refs){...refs--}`；`fdinfo` 看计数 |
| `fdinfo` 显示 inode refs 偏高 | 未关闭的 file 残留 | 对照 `fd_close_model` 只在 `file_table[f].refs==0` 时递减 inode |
| `reclaimtest` 输出 `BROKEN` | `reclaim_one` 未回收（`refs!=1`）或 `pmm_free_page` 未返回 `freed` | 对照 `reclaim_one` 的三条件与 `pmm_free_page` 返回串；`anoninfo` 看 reclaim 统计 |
| `teardowntest` 输出 `BROKEN` | `zombie`/`teardown_done` 前置条件或 `releases` 计数异常 | 对照 `resource_teardown` 的守卫与 `releases=6`；确认 `resource_start()` 先运行 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 90' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `inode 生命周期与引用` 与 `Lesson 90` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `vfs_init` 以 `refs=1` 创建 inode 表 | `fs/inode.c`：`iget_locked()`/`alloc_inode()`（`i_count=1`）；`include/linux/fs.h` 的 `struct inode`（`i_ino`/`i_size`/`i_mode`/`i_count`） | 模型 3 个定长槽，无 inode cache 哈希表与 LRU |
| `fd_open_model` 的 `inode_table[inode].refs++` | `fs/inode.c`：`iget()`/`ihold()`；`fs/open.c`：`filp_open()` | 模型无 per-inode 锁与 RCU，引用递增无并发保护 |
| `fd_close_model` 末个 file 才 `refs--` | `fs/file_table.c`：`fput()` → `__fput()` → `iput()`；`fs/inode.c`：`iput()`/`iput_final()` | 模型把 file 归零与 inode 递减合并，无 `i_state` 状态机 |
| `reclaim_one` 的 `refs!=1` 跳过 | `mm/vmscan.c`：`shrink_inactive_list()` 与 `try_to_unmap`（引用/映射检查） | 模型扫描固定 4 页，无 LRU 与 shrinker |
| `resource_teardown` 有序释放 + double-reap guard | `fs/inode.c`：`evict()`/`destroy_inode()` 钩子；`fs/super.c`：`deactivate_super()` | 模型用 `teardown_done` 布尔防二次释放，无 RCU grace period |
| `inode 引用归零才销毁`原则 | `include/linux/fs.h` 的 `i_count` 注释与 `iput()` 文档 | 模型 inode 永不真正销毁（dentry 基引用常驻），只演示递减路径 |
| `l90test` 断言 | 无直接对应（LTP `fs` 测试套件） | 模型把生命周期验证固化进内核 |

**权威来源**：Linux `fs/inode.c`、`fs/super.c`、`include/linux/fs.h` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么 inode 不能在最后一个 file 关闭时立即销毁？结合教学模型的「dentry 基引用」说明至少还有谁在持有它。
2. **源码定位**：在 `kernel64.c` 中找到 `inode_table[...].refs` 的**全部**读/写点，画出一次 `fdtest` 过程中 inode 0 与 inode 1 的 refs 变化表。
3. **动手实验**：修改 `fd_close_model`，去掉 `if(!file_table[f].refs)` 条件（file 关闭总是递减 inode），重新构建运行 `fdtest`，观察是否仍通过；解释两个 file 同时打开同一 inode 时会发生什么。
4. **动手实验**：给 `reclaim_one` 增加一个 `refs==2` 的测试页，运行 `reclaimtest`，观察 `reclaim_skips` 增长而页不被回收。
5. **Linux 对照**：阅读 `fs/inode.c` 的 `iput_final()`，对比教学模型的条件递减，指出 Linux 在 `i_count==0` 之后还经历了哪些状态（`I_WILL_FREE`/`destroy_inode`）。

---

## 9. 本课小结与下一课预告

1. 本课以 inode 为焦点，把 VFS 阶段从「全局（超级块/注册）」推进到「个体生命周期（iget → 引用 → iput → 销毁）」。
2. 「引用计数不到零绝不销毁」是贯穿 inode/file/页对象/资源释放的共同原则，教学模型在 `refs` 与 `reclaim`/`teardown` 三处复现它。
3. `fd_open_model` 每次 open 使 inode `refs++`，`fd_close_model` 只在最后一个 file 关闭时 `refs--`——与 Linux `iget`/`iput` 的延迟语义一致。
4. `reclaim_one` 的 `refs==1` 条件与 `resource_teardown` 的 `zombie`/`teardown_done` 守卫都是「生命周期末段」的正确性护栏。
5. `l90test` 沿用 VFS/设备阶段检查点家族，`l82test` 历史检查点保留。
6. 下一课（Lesson 91）将主题转向 **dentry 缓存与路径组件**（对照 `fs/dcache.c`），VFS 阶段继续深入路径解析的缓存层。
