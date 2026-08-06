# Lesson 157: 资源限制与回收 — 精讲文档

> **课号**：Lesson 157（可执行课，checkpoint 快照）
> **主题**：资源限制与回收——把内核里「进程生命周期结束时资源如何清点、如何有序释放、
> 如何防重复释放」的继承机制（`resource_ledger`、`reclaim_one`、PMM 页分配/释放、
> `user_program_reclaim`）串成一张「资源台账」图，并追加确定性校验的 checkpoint 模型
> `lesson_150_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162），位于
> Lesson 156（cgroup 设备策略）之后、Lesson 158（capability 权限检查）之前。
> **前置课程**：[`lesson-156-stable/README.md`](../lesson-156-stable/README.md)
> **后续课程**：[`lesson-158-stable/README.md`](../lesson-158-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 的「资源限制与回收」全貌——资源台账
> `resource_ledger` 记了什么、`teardown` 按什么顺序释放、页回收 `reclaim_one` 判定
> 什么页可回收、`l157test` 校验了什么，以及旧 README 中命令名的勘误。

---

## 1. 课程定位（Mission）

**一句话目标**：读透内核里继承下来的「资源台账 → 有序释放 → 防双重释放」与
「匿名页回收」两套资源管理模型，理解本课新增的确定性 checkpoint 模型
`lesson_150_model` 及其 `l157test` 断言，并会用 `teardowntest`、`resourceinfo`、
`reclaimtest`、`anoninfo`、`l149test`、`l157test` 等命令复现它们。

- **在课程主线中的位置**：与 Lesson 158–162 同属「资源/安全主题的检查点课」，相邻课
  `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 156 仅 4 处改动：`l156test`→
  `l149test` 改名、新增 `struct lesson_150_model` 与 `l157test`、exec64/about/banner
  文案）。资源管理机制代码全部继承自早期课程，检查点课的作用是把「已经实现的机制」
  用断言固化，主题由 banner/about 文案标识。
- **前置知识清单**：
  1. PMM 位图分配器（`pmm_alloc`/`pmm_free_page`、`page_state`、`pmm_reserved`）——
     Lesson 08–09 的物理内存地基，所有回收动作最终都回到 `unmark` 位图清零；
  2. VMA 元数据模型（`vma_table`、`vma_lookup`、`vma_init`）与 `fault_insert`
     登记的 `fault_pages` 匿名页表——`reclaim_one` 的回收对象正是这张表；
  3. 进程/线程生命周期（`PROCESS_READY/RUNNING/EXITED`、`USER_THREAD_RECLAIMED`、
     `user_program_reclaim`）——资源台账的「持有者」是谁；
  4. 本主题课的形态认知：`struct lesson_K_model` + `lXXtest` 是「主题宣告 + checkpoint
     增量」的固定模式（Lesson 133–156 的检查点课都是同一模式）。
- **本课交付**：理解 `resource_ledger` 的六类资源引用计数与「zombie 保留 → 一次
  teardown → 二次调用拒绝」的有序释放协议；理解 `reclaim_one` 的「live +
  reclaimable + refs==1」三重回收条件；命令 `l149test`（改名）与 `l157test`（新增）
  两个 checkpoint 测试；`about`/banner 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：资源台账（resource ledger）

**直觉**：一个进程活着时手里握着地址空间、文件描述符、管道、信号、定时器、延迟
工作等一堆内核资源。它退出后这些资源不能丢、也不能被释放两次。TinyOS 用一个极小的
结构体 `resource_ledger` 把这堆「资源引用数」登记在案：

```c
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
```

**准确定义**：资源台账 = 一个把「进程持有的各类内核对象引用计数」集中管理的元数据
记录。`address_space` 计数地址空间引用，`fd_refs/pipe_refs/signal_refs/timer_refs/
deferred_refs` 分别计数文件、管道、信号、定时器、延迟工作，`releases` 统计已释放的
资源项数，`double_releases` 统计「重复释放」次数。

**为什么需要**：真实内核里进程退出时 `exit()` 必须逐个释放 `mm_struct`、`files_struct`、
`fs_struct`、信号处理等，顺序错了或重复释放都会造成引用计数错乱。教学模型把
「退出时的资源清理」抽象成一个可判定的协议（zombie → teardown → 拒绝第二次），
让「资源回收」这件事能被一条命令验证。

**工作机制**（`resource_start` → `resource_teardown`）：
1. `resource_start` 把台账设为 `{1,2,1,1,1,1,0,0,1,0}`——地址空间 1、fd 2、管道 1、
   信号 1、定时器 1、工作 1，`zombie=1`（进程已变僵尸，资源保留）；
2. `resource_teardown` 前置条件：必须是 zombie 且尚未完成 teardown；满足则六类引用
   全部清零、`releases=6`、`teardown_done=1`；
3. 第二次调用 `resource_teardown` 因 `teardown_done` 被拒——这就是「double-reap
   guard」。

### 2.2 概念二：有序回收与双重释放防护

**直觉**：Linux 里 `exit()` 走到 `release_task` 之前，资源是「分阶段」释放的：
先 `exit_mm` 放地址空间，再 `exit_files` 放文件表，最后 `release_task` 回收 task
本体。TinyOS 把「有序」抽象成 `resource_teardown` 一口气把六类引用归零并计 6 次
release，同时用 `teardown_done` 标志保证「只做一次」。

**为什么这样设计**：防重复释放是引用计数系统最核心的不变量——`double_releases`
字段就是为观测这个不变量准备的。这与 Linux 的 `refcount`/`kref` 语义同源：
`refcount_dec_and_test` 在计数归零的那一次执行析构，之后所有 `get` 都不再合法。

**例子**（`teardowntest` 的验证序列）：
```text
resource_start(); resource_ledger.zombie=1;   // 进程变僵尸，资源仍保留
a=resource_teardown();   // 第一次：六类清零，releases=6，teardown_done=1
b=!resource_teardown();  // 第二次：被 teardown_done 拒绝
d=(address_space==0 && releases==6);
// a&&b&&d ⇒ "zombie retention, ordered resource release, and double-reap guard passed"
```

### 2.3 概念三：匿名页回收（page reclaim）

**直觉**：物理内存不够时，内核要把「可回收页」交还给分配器。TinyOS 的 `reclaim_one`
从 `fault_pages`（Lesson 133 `fault_insert` 登记的教学页表）里找一页满足条件的匿名页，
把它从表中摘除并交还 PMM：

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

**回收条件三重检查**：①`m->live` 页在位；②`m->reclaimable` 标记可回收；③
`m->refs==1` 恰好只有一个引用（有额外引用者不能回收）。之后调用 `pmm_free_page`
真正把物理页交还分配器，仅当返回 `"freed"` 才从 `fault_pages` 摘除并统计
`anon_reclaims`。这是 Linux `mm/vmscan.c` 中 `shrink_page_list`「refs==0 才释放」
规则的极简版。

### 2.4 概念四：确定性 checkpoint 模型

**直觉**：检查点课不写新机制，而是用「结构体 + 赋值 + 断言」把本课主题固化为
一行可验证的真假值。Lesson 157 新增：

```c
struct lesson_150_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l157test` 把 `lesson_150_state` 整体赋为 `{150U,151U,152U,153U,1,1,1,1}`
（a=150, b=151, c=152, d=153，四个状态位全 1），然后断言 `valid && active && ready &&
accounted && b==a+1`。由于赋值是字面量，断言恒真，输出恒为 `bounded networking,
namespaces, cgroups, and security checkpoint passed`。「fallback」分支只是保证任何
情况下都有输出，不会真正触发。**这是教学模型：不执行任何网络/资源管理代码，只校验
元数据自洽**。注意模型名是 `lesson_150_model` 而非 `lesson_157_model`——编号 157−7=
150，「回锚」到更早的 Lesson 150 检查点模型，与 Lesson 133 用 `lesson_126_model`
是同一惯例。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 156） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1030 行）：PMM/异常/中断/调度/进程/VFS/GUI/资源回收/checkpoint 模型 | `l156test`→`l149test` 改名；新增 `struct lesson_150_model`、`l157test`；exec64 增加 `l149test`/`l157test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `资源限制与回收`、`l157test`、`Lesson 157` | 仅 grep 文案（Lesson 156→157） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：资源回收机制精讲（继承代码）

> 说明：本节的 `resource_ledger`、`reclaim_one` 等函数都是早期课程继承下来的
> **既有机制**，本课没有修改它们；把它们精讲是因为它们正是本课主题
> 「资源限制与回收」的机制载体。

#### 3.2.1 资源台账与 teardown 协议

```c
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
static struct resource_ledger resource_ledger;
static TEXT64 void resource_start(void){resource_ledger=(struct resource_ledger){1,2,1,1,1,1,0,0,1,0};}
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
```

逐行注释：
- `resource_ledger` 字段：六类资源引用计数（address_space/fd/pipe/signal/timer/
  deferred）+ 两个审计计数器（releases 已释放项数、double_releases 重复释放次数）+
  两个状态位（zombie 是否僵尸、teardown_done 是否已拆解）。
- `resource_start`：整体字面量赋值，`zombie=1, teardown_done=0`——模拟「进程已
  退出但资源仍被保留」的僵尸态初始条件。
- `resource_teardown` 算法：①入口防御 `!zombie || teardown_done` 直接返回 0；
  ②六类引用全部清零；③`releases=6` 记录释放了 6 项；④`teardown_done=1` 封口。
- 边界与错误处理：第二次调用、或非 zombie 调用都会失败——这是「有序释放只执行
  一次」和「双重释放防护」两个不变量。
- 设计动机：Linux `kernel/exit.c` 的 `do_exit` → `exit_mm`/`exit_files`/
  `exit_fs`/`exit_sighand` 逐层释放资源，TinyOS 用一张台账 + 一个状态位做等价抽象。

```c
static TEXT64 void resourceinfo(u16*c){text64(c,"resources as/fd/pipe/signal/timer/work/releases: ");hex64(c,resource_ledger.address_space);hex64(c,"/");hex64(c,resource_ledger.fd_refs);hex64(c,"/");hex64(c,resource_ledger.pipe_refs);hex64(c,"/");hex64(c,resource_ledger.signal_refs);hex64(c,"/");hex64(c,resource_ledger.timer_refs);hex64(c,"/");hex64(c,resource_ledger.deferred_refs);hex64(c,"/");hex64(c,resource_ledger.releases);putc64(c,'\n');}
static TEXT64 void teardowntest(u16*c){resource_start();resource_ledger.zombie=1;int a=resource_teardown(),b=!resource_teardown(),d=resource_ledger.address_space==0&&resource_ledger.releases==6;text64(c,"teardowntest: ");text64(c,a&&b&&d?"zombie retention, ordered resource release, and double-reap guard passed":"BROKEN");putc64(c,'\n');}
```

- `resourceinfo`：把台账六类引用 + releases 逐一以 `0/1` 十六进制打印，是观察
  「teardown 前后台账变化」的只读窗口。
- `teardowntest` 算法：①`resource_start()` 复位台账；②手动置 `zombie=1`；
  ③`a=resource_teardown()`（应成功）；④`b=!resource_teardown()`（第二次应被拒）；
  ⑤`d=address_space==0 && releases==6`（确认清零与计数）；⑥三者全真输出
  `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`。
- 关键设计：`double_releases` 字段虽未在断言中使用，却是给学习者准备的「观察位」——
  若后续课程实现真正意义上的重复释放计数，`teardowntest` 的断言可扩展。

#### 3.2.2 物理页回收（reclaim_one 与 PMM 归还）

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

- 签名与职责：遍历最多 `VMA_MAX_PAGES(4)` 张 `fault_pages` 教学页，回收第一张满足
  条件的匿名页，返回是否回收成功。
- 算法步骤：①`reclaim_scans++` 计扫描次数；②三重资格检查
  `!live || !reclaimable || refs!=1` 任一成立即跳过（`reclaim_skips++`）；
  ③调用 `pmm_free_page` 把物理页归还位图，仅当返回 `"freed"`（页确实处于 allocated
  态且未被映射/未被线程栈占用）才继续；④`m->live=0`、`fault_page_count--`、
  `anon_pages--`、`anon_reclaims++`，返回 1。
- 边界与错误处理：PMM 归还失败（页被映射、是线程栈、或状态不是 allocated）会被
  `page_state`/`vm_frame_owned`/`thread_stack_owned` 拒绝并返回非 `"freed"`，此时
  该页不摘除、计入 `reclaim_skips`。
- 为什么这样设计：`refs==1 && reclaimable && live` 正是「唯一引用 + 可回收 + 在位」
  的可回收判定，与 Linux `mm/vmscan.c` 中「页引用计数归零才可回收」一致。

```c
static TEXT64 void anoninfo(u16*c){text64(c,"anon pages/cache live/reclaims: ");hex64(c,anon_pages);hex64(c,"/");hex64(c,page_cache_count);hex64(c,"/");hex64(c,anon_reclaims);text64(c," cache hit/miss: ");hex64(c,cache_hits);hex64(c,"/");hex64(c,cache_misses);text64(c," reclaim scans/skips: ");hex64(c,reclaim_scans);text64(c,"/");hex64(c,reclaim_skips);putc64(c,'\n');}
static TEXT64 void reclaimtest(u16*c){int a=fault_insert(VMA_DATA_START,1),b=page_cache_get(1,1),d=page_cache_get(1,0),e=reclaim_one();text64(c,"reclaimtest: ");text64(c,a&&b&&d&&e&&anon_pages==0&&page_cache_count==1?"anonymous reclaim and page-cache hit model passed":"BROKEN");text64(c,"\npage cache is metadata-only; no disk I/O or swap executed\n");}
```

- `anoninfo`：打印 `anon_pages`（当前匿名页数）、`page_cache_count`（页缓存条数）、
  `anon_reclaims`（累计回收数）以及 cache hit/miss、reclaim scans/skips。
- `reclaimtest` 算法：①`fault_insert` 插入一张匿名页；②两次 `page_cache_get(1,…)`
  构造「第一次未命中建缓存、第二次命中」；③`reclaim_one` 回收刚插入的页；
  ④断言 `a&&b&&d&&e&&anon_pages==0&&page_cache_count==1`——插的页被回收、缓存条
  保持 1，输出 `reclaimtest: anonymous reclaim and page-cache hit model passed`，
  第二行 `page cache is metadata-only; no disk I/O or swap executed` 点明纯元数据。

#### 3.2.3 进程资源回收（user_program_reclaim 与 PMM 分配/释放）

```c
static TEXT64 void user_program_reclaim(u32 i){if(i>=MAX_USER_PROGRAMS||user_processes[i].state!=PROCESS_EXITED)return;user_threads[i].state=USER_THREAD_RECLAIMED;user_processes[i].state=PROCESS_EMPTY;user_processes[i].context_valid=0;user_reclaims++;}
```

- 职责：用户程序退出后回收其进程/线程对象——前置条件是该槽位状态必须是
  `PROCESS_EXITED`；满足后线程置 `USER_THREAD_RECLAIMED`、进程置 `PROCESS_EMPTY`、
  清 `context_valid`、`user_reclaims++`。
- 边界：`i>=MAX_USER_PROGRAMS(2)` 或非 exited 状态直接返回——与 `resource_teardown`
  的「zombie 才允许释放」是同一不变量在不同对象上的体现。
- 与 PPM 的关系：物理页真正回归分配器靠 `pmm_free_page`（Lesson 10 继承），
  `page_state` 返回 `"freed"` 才允许 `unmark`；`user_program_reclaim` 回收的是
  **进程对象**（元数据），两者共同构成「进程资源回收」的完整链条。

#### 3.2.4 本课新增 checkpoint：lesson_150_model 与 l157test

```c
struct lesson_150_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_150_model lesson_150_state;
static TEXT64 void l157test(u16*c){lesson_150_state=(struct lesson_150_model){150U,151U,152U,153U,1,1,1,1};int ok=lesson_150_state.valid&&lesson_150_state.active&&lesson_150_state.ready&&lesson_150_state.accounted&&lesson_150_state.b==lesson_150_state.a+1U;text64(c,"l157test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 150 fallback reported");putc64(c,'\n');}
```

- `struct lesson_150_model`：4 个 u32（a/b/c/d 连续编号）+ 4 个状态位
  （valid/active/ready/accounted）。字段 `a` 用 `150U` 起头，正好等于本课编号
  157 − 7，是「回锚」到 Lesson 150 检查点模型的记号（与 Lesson 133 的
  `lesson_126_model` 同惯例）。
- `l157test` 算法：①整体赋值字面量 `{150,151,152,153,1,1,1,1}`；②`ok` 五连断言
  （valid、active、ready、accounted、b==a+1）；③按 ok 输出成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 或失败串
  `Lesson 150 fallback reported`。
- 为什么：检查点课把「继承机制仍然自洽」压缩成一个恒真断言，任何相邻课改坏模型
  字段时输出会翻转为 fallback，起回归探针作用。注意它**不执行任何资源/网络代码**，
  消息里的 "networking, namespaces, cgroups, and security" 描述的是整个内核继承机制
  的覆盖面而非本函数行为。

#### 3.2.5 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l149test")){if(!noargs64(arg))usage64(c,"l149test");else l149test(c);}else if(eq64(word,"l157test")){if(!noargs64(arg))usage64(c,"l157test");else l157test(c);}
```

- 本课把上一课的 `l156test` 分支改名 `l149test`（其模型 `lesson_149_state` 不动，
  仍是 `{149,150,151,152}`），并新增 `l157test` 分支。
- **勘误**：旧 README 写的 `Commands: l150test` 与源码不符——源码中**不存在**
  `l150test` 命令（`grep -c l150test` 为 0），可用的 checkpoint 命令是 `l149test`
  与 `l157test`。
- about 文案 `else text64(c,"Lesson 157: 资源限制与回收\n");` 与开机横幅
  `text64(&c,"Lesson 157: 资源限制与回收\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。
- 主题相关命令（继承，未改动）：`teardowntest`/`resourceinfo`（资源台账）、
  `reclaimtest`/`anoninfo`（页回收）、`palloc`/`pfree`/`pageinfo`/`meminfo`（PMM）。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq` 取 stub 地址依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`资源限制与回收`、`l157test`、`Lesson 157`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       module_init_model() → init_model_start() → wait_model_start()
       → adoption_start() → resource_start()（资源台账初始化为僵尸保留态）
       → pmm_init() → vma_init() → reclaim_init() → vfs_init()
       → 进程/线程元数据装配 → framebuffer_init
       → stack_guards_init / runtime_gdt_tss_init / idle_init / install_idt
       → pit_init()+pic_init() → 横幅 "Lesson 157: 资源限制与回收\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  exec64 分支 → teardowntest:resource_start→zombie→teardown×2→三重断言
             → reclaimtest:fault_insert→page_cache_get×2→reclaim_one→断言
             → l149test / l157test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 157: 资源限制与回收\n")` → 屏幕打印 `Lesson 157: 资源限制与回收`。
2. **`teardowntest`** → `teardowntest(c)`：`resource_start()` 复位台账 → 置
   `zombie=1` → `resource_teardown()` 清空六类引用并把 `releases` 置 6 → 第二次调用
   被 `teardown_done` 拒绝 → 三重断言通过 → 输出 `teardowntest: zombie retention,
   ordered resource release, and double-reap guard passed`。
3. **`l157test`** → `l157test(c)` 对 `lesson_150_state` 赋值并五连断言 → 输出
   `l157test: bounded networking, namespaces, cgroups, and security checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-157-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `资源限制与回收`、`l157test`、`Lesson 157` 与 kernel64.c 中的 `l157test`，
  全部命中输出 `Multiboot2 and Lesson 157 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 157: 资源限制与回收`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 157: 资源限制与回收`
  2. `l157test` → `l157test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l149test` → `l149test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `teardowntest` → `teardowntest: zombie retention, ordered resource release,
     and double-reap guard passed`
  5. `resourceinfo` → `resources as/fd/pipe/signal/timer/work/releases: 1/2/1/1/1/1/0`
     （`teardowntest` 之后再查则为 `0/0/0/0/0/0/6`）
  6. `reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit model passed`，
     下一行 `page cache is metadata-only; no disk I/O or swap executed`
  7. `anoninfo` → `anon pages/cache live/reclaims: 0/1/1` 之后
     ` cache hit/miss: 1/1` 与 ` reclaim scans/skips: ...`
  8. `meminfo` → 首行 `PMM: 4 KiB physical frames in 16 MiB mapped window`，
     末行 `invariant tracked = free + used: yes`
- **如何判断成功**：上述命令逐一打印预期串即成功；`make check` 三条 grep 全命中即
  通过。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l157test` 输出 `Lesson 150 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l157test` 的赋值 `{150U,151U,152U,153U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l156test`/`l150test` 报 `unknown command` | 命令已改名或本就不存在 | 源码中可用命令是 `l149test` 与 `l157test`；旧 README 的 `l150test` 是笔误 |
| `teardowntest` 输出 `BROKEN` | `resource_start` 未先执行、或 `zombie` 未置位、或 `releases` 计数不符 | 确认测试函数内顺序：`resource_start()` → `zombie=1` → 第一次 `resource_teardown()` 成功 → 第二次被拒 → `releases==6` |
| `resourceinfo` 值不变（总是 1/2/1/1/1/1） | `resource_teardown` 从未成功执行（前置 `zombie` 为 0） | 先跑 `teardowntest` 再查 `resourceinfo`；检查 `resource_ledger.zombie` |
| `reclaimtest` 输出 `BROKEN` 或 `anon_pages!=0` | `fault_insert` 失败（VMA 未初始化）或 `reclaim_one` 三重条件不满足 | 先 `vmainfo` 确认 VMA 表；检查 `fault_pages[i].reclaimable` 与 `refs` 是否被后续测试改脏 |
| `reclaimtest` 后 `meminfo` 的 free 数未回增 | `pmm_free_page` 返回非 `"freed"`（页被 `vm_frame_owned`/`thread_stack_owned` 占用） | 在 `reclaim_one` 中断点看 `page_state(m->phys)`；确认该页不是已映射帧 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 157: 资源限制与回收`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **资源限制（rlimit）**：TinyOS 的 `resource_ledger` 是「进程持有的资源引用台账」，
   与 Linux 进程的 `struct rlimit rlim[RLIM_NLIMITS]`（`include/uapi/linux/resource.h`，
   如 `RLIMIT_NOFILE`/`RLIMIT_AS`）在「限定并清点进程资源」的意图上对应，但 TinyOS
   记录的是引用计数而非「上限值」——`resource_ledger` 更接近 `task_struct` 里
   `signal_struct` 指向的 `rlim` + `mm/files/fs/sighand` 指针集合的极简投影。
2. **退出时的资源释放顺序**：TinyOS `resource_teardown` 把六类引用一次清零并只执行
   一次；Linux `kernel/exit.c` 的 `do_exit` 顺序调用 `exit_mm`（放地址空间）、
   `exit_files`（放 `files_struct`）、`exit_fs`、`exit_sighand`，最终
   `release_task`→`__put_task_struct` 才释放 task 本体。TinyOS 砍掉了每个子系统的
   真实析构细节，只保留「zombie 保留 → 一次拆解 → 拒绝重放」的顺序骨架。
3. **双重释放防护**：TinyOS 用 `teardown_done` 标志 + `double_releases` 计数器；
   Linux 用 `refcount_t`/`kref`（`include/linux/refcount.h`）的
   `refcount_dec_and_test`——计数到 0 的那次触发析构，之后的操作被
   `refcount_warn_saturate` 拒绝。两者的不变量相同：析构只允许发生一次。
4. **匿名页回收**：TinyOS `reclaim_one` 的三重检查（`live && reclaimable &&
   refs==1`）对应 Linux `mm/vmscan.c` 的 `shrink_page_list` 中
   `page_ref_count(page)==0`（或 `!page_mapped && !page_refcount`）才释放的判定；
   `anon_pages/anon_reclaims` 计数对应 `NR_ANON_MAPPED` 之类的 zone 统计。TinyOS
   省略了 LRU 链表、swap 写回与 kswapd 后台线程。
5. **页缓存命中模型**：TinyOS `page_cache_get` 的「先查表命中（`cache_hits`）、未命中
   则 `pmm_alloc` 建条目」对应 Linux `mm/filemap.c` 的 `page_cache_get`/`find_get_page`
   语义；`reclaimtest` 明确声明 `page cache is metadata-only; no disk I/O or swap
   executed`——真实内核的 cache 回写（`writeback_pages` 字段在 TinyOS 中仅登记计数）
   不在本教学模型内。
6. **孤儿进程收养**：TinyOS 本课的 `adoption_start`/`reparenttest`（继承自前课）把
   「父进程退出后孤儿交给 init」建模为 `current_parent=init_pid`；Linux 对应
   `kernel/exit.c` 的 `forget_original_parent`/`find_new_reaper`。它与
   `resource_ledger` 一样都属于「进程生命周期终结时的资源/归属管理」。

**权威来源**：Linux v6.x `include/uapi/linux/resource.h`（RLIMIT 定义）、
`kernel/exit.c`（do_exit/release_task）、`mm/vmscan.c`（shrink_page_list）、
`include/linux/refcount.h`（refcount_dec_and_test）、Intel SDM Vol.3A（IDT/TSS，
本课未改动但 banner 的 GDT/IST 机制依赖它）。
**教学模型简化了什么**：真实内核的资源限制是「上限强制」（超限返回
`EMFILE`/`ENOMEM`），回收是「内存压力驱动的后台 LRU + swap」；TinyOS 只做
「台账记录 + 一次性拆解 + 单页回收判定」的元数据模拟，不执行任何真实释放语义或
内存压力回收。

---

## 8. 思考题与练习

1. **概念理解**：`resource_teardown` 为什么用 `teardown_done` 而不是靠
   `releases==6` 判断「已拆解」？如果把第二次调用改为「直接清零并再计 6 次」，
   `double_releases` 字段会观察到哪里出错？
2. **源码定位**：在 `kernel64.c` 中找出 `reclaim_one` 的三个回收资格条件对应的字段，
   并说明为什么 `refs!=1` 的页必须跳过；再找出 `user_program_reclaim` 与
   `resource_teardown` 在「前置状态检查」上的共同点。
3. **动手实验**：修改 `l157test` 的赋值，把 `b` 从 `151U` 改成 `150U`（即 `b==a`），
   重新构建运行，观察输出是否翻转为 `Lesson 150 fallback reported`；再改回。
4. **动手实验**：在 `teardowntest` 中把 `resource_ledger.zombie=1` 这一行删掉，预测
   `resource_teardown()` 的返回值与输出串会怎样变化，并解释原因。
5. **Linux 对照**：阅读 `kernel/exit.c` 的 `do_exit` 与 `mm/vmscan.c` 的
   `shrink_page_list`，对比它们与 `resource_teardown`/`reclaim_one` 的分工边界，
   指出 TinyOS 砍掉了哪些真实阶段（如挂到 LRU、唤醒 kswapd、swap 写回）。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是资源/安全主题的检查点课，`kernel64.c` 相对上一课只有 4 处小增量，机制全部
   继承自早期课程，主题由 banner/about 文案标识。
2. 资源回收的核心是 `resource_ledger` 台账：六类资源引用 + `releases` 计数 +
   `zombie/teardown_done` 两个状态位。
3. `resource_teardown` 实现「有序拆解 + 防双重释放」：zombie 保留资源 → 一次性清空
   六类引用 → 二次调用被 `teardown_done` 拒绝。
4. `reclaim_one` 用 `live && reclaimable && refs==1` 三重检查从 `fault_pages` 摘除
   匿名页并归还 PMM，是 Linux `shrink_page_list` 的极简版。
5. 进程对象回收由 `user_program_reclaim` 完成（exited → RECLAIMED/EMPTY），与
   `pmm_free_page` 的物理页归还共同构成完整回收链。
6. 新 checkpoint `l157test` 用字面量赋值 + 五连断言把「继承机制自洽」固化为恒真
   回归探针；模型名 `lesson_150_model` 的 150 = 157−7 是「回锚」记号。
7. 旧 README 的 `Commands: l150test` 已勘误为源码实际的 `l149test` 与 `l157test`。

**下一课**：[`lesson-158-stable/README.md`](../lesson-158-stable/README.md) 主题为
「capability 权限检查」，将站在本课「资源台账」对进程资源持有权的建模之上，讲解
Linux capability 位图（`include/linux/capability.h`）的权限检查语义如何被教学模型
固化为新的 checkpoint 模型（命令 `l158test`）。两课的衔接点是「进程权限/资源的
持有与校验」：本课管「资源的清点与释放」，下节课管「能力的授予与检查」。
