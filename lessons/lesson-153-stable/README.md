# Lesson 153: cgroup 层级 — 精讲文档

> **课号**：Lesson 153 ｜ **主题**：cgroup 层级（cgroup hierarchy）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课为 **cgroup 系列四连**（层级 153 → CPU 统计 154 → 内存限制 155 → 设备策略 156）的第一课
> **前置课程**：[`../lesson-152-stable/README.md`](../lesson-152-stable/README.md)（user namespace）
> **后续课程**：[`../lesson-154-stable/README.md`](../lesson-154-stable/README.md)（cgroup CPU 统计）
> **一句话目标**：讲清 cgroup 为什么是「层级树」而不是平铺列表——父 cgroup 的限制如何叠加到子 cgroup，对照 Linux `kernel/cgroup/cgroup.c`、`kernel/cgroup/cgroup-internal.h` 与 v1/v2 层级模型，并把教学内核中继承的**层级式设施**（进程收养树 `adoption_model`、ramfs 目录树 `ramfs_node.parent`、进程树 `task_struct.parent_pid`）按 cgroup 层级主题系统化复述，运行 `l153test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（cgroup 层级）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l152test` 恢复为历史命名 `l145test`（挂 `lesson_145_state`），新增 `lesson_146_model`/`lesson_146_state` 与 `l153test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l153test`（旧 README 所写 `l146test` 按源码勘误）；另保留历史检查点 `l100test`–`l145test`，以及 `adoptioninfo`/`reparenttest`/`initinfo`/`shelltest`/`taskvalidate`/`ramfsinfo`/`pathtest` 等层级与树形结构回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「一棵限制不断收窄的树」解释 cgroup 层级（`/sys/fs/cgroup` 下的嵌套目录）；说出 Linux v1（多层级并存、每控制器挂一棵树）与 v2（统一一棵树、`cgroup.controllers` 按需启用）的差异（`kernel/cgroup/cgroup.c`）；在教学内核中沿 `adoption_model`（孤儿进程被 init 收养的树维护）→ `reparenttest` → `ramfsinfo`/`pathtest` 观察层级式结构的父-子链；运行 `l153test` 验证。

**在课程主线中的位置**：Lesson 151–152 讲完 namespace（PID/user 隔离），本课转入 cgroup——**与 namespace 并列的第二大容器原语**：namespace 负责「看到什么」，cgroup 负责「能用多少」。作为 cgroup 系列第一课，先把「层级」这个结构地基讲清，后三课（CPU 统计/内存限制/设备策略）再分别讲各控制器。作为检查点课，源码 diff 极小，任务是把继承机制中带「父-子层级」语义的设施（进程树、收养模型、ramfs 目录树）按 cgroup 层级主题系统化复述。下一课（Lesson 154）讲 cgroup CPU 统计。

**前置知识清单**（学本课前必须掌握）：
1. 进程树：`task_struct{pid,tid,parent_pid}`、`task_table` 与 `task_table_validate` 的父子断言（Lesson 37/151）。
2. 孤儿收养模型：`adoption_model`、`adoption_exit_parent`/`adoption_reparent`/`adoption_wait_owner`（Lesson 66s）。
3. ramfs 目录树：`struct ramfs_node{name_hash,parent,inode,type,valid}` 与 `ramfs_lookup`/`pathtest`（Lesson 88s/95s）。
4. init 模型：`init_model_start`（`init_model.pid=FIXED_PID`）、`initinfo`（Lesson 60s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–152）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 153: cgroup 层级`；
- 新命令 `l153test` 输出 `l153test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `adoptioninfo`/`reparenttest`/`initinfo`/`ramfsinfo` 继续展示层级式结构的父子链。

---

## 2. 核心概念精讲

### 2.1 cgroup 层级：一棵限制不断收窄的树

**直觉**：把进程放进 cgroup，等于把它分到「一个资源小组」里。小组还可以再套小组：`/sys/fs/cgroup/` 下 `mkdir` 出一个子目录，就生成了子 cgroup。父组的限制是**天花板**，子组只能在更小的范围里再设限制——这就是 **cgroup 层级（hierarchy）**。

```
cgroup_root（根，无限制）
└── /docker        cpu.limit=4核, mem.limit=8G    ← 父组限制
    └── /docker/0   cpu.limit=2核, mem.limit=4G    ← 子组不能超过父组
```

**准确定义**：cgroup 是「一组任务（进程/线程）」的资源限制容器；cgroup 之间按**树形**组织成层级，父 cgroup 包含所有子 cgroup 的任务。任务被加入某个 cgroup 后，其资源使用受「该 cgroup 及全部祖先」的限制约束——**层级决定了限制的叠加**。

### 2.2 为什么需要 cgroup 层级（动机）

1. **限制叠加**：容器是一个 cgroup 树，树里的每个子系统（CPU/内存/IO）再分子树——不这样做，`/docker/0` 无法在 `/docker` 的总配额内再细分。
2. **继承语义**：子 cgroup 默认继承父 cgroup 的参数，创建新容器只需在父组下 mkdir + echo 写参数，改动局部化。
3. **统计聚合**：`cpuacct.usage` 等统计值沿层级向上汇总，父组的统计 = 所有子组之和（为下一课 CPU 统计铺路）。

### 2.3 Linux 中 cgroup 层级的两种模型

- **v1（传统多层级）**：每个控制器（controller）独立挂载一棵树，`/sys/fs/cgroup/cpu/`、`/sys/fs/cgroup/memory/` 各是一棵树；一个任务可同时属于多棵树。优点灵活，缺点是控制器间无法联合约束，`cgroup` 对象数量随树数量膨胀。
- **v2（统一层级）**：`kernel/cgroup/cgroup.c` 保证**全局只有一棵树**；控制器在 `cgroup.controllers`（可用）/`cgroup.subtree_control`（启用）两个文件中声明，启用某控制器即写入子树控制；禁止在同一层级重复挂载、禁止同一控制器在多个层级启用。任务写 `cgroup.procs` 进组，`cgroup.events` 上报 pop/frozen 事件。
- **数据结构**：`kernel/cgroup/cgroup-internal.h` 的 `struct cgroup`（`children`/`parent`/`subtree_control`/`self`）、`struct css_set`（任务 → cgroup 的绑定集）、`kernel/cgroup/cgroup-v1.c` 的 v1 专用 `cgroup_root`。
- **教学简化**：教学内核没有 `struct cgroup`/`css_set`，但「父-子层级」结构在进程树、收养模型、ramfs 目录树三处完整存在——三者合起来就是「层级」概念的三个教学缩影。

### 2.4 教学内核中与「层级」有关的既有设施

本课主题机制（cgroup 层级）**未在源码中实现**，但「父子层级」素材完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| 进程树 | `task_struct{pid,tid,parent_pid}`、`task_table`、`task_table_validate` 的 `parent_pid>=pid` 断言 | 进程组织成树（对照 cgroup 任务树） |
| 收养模型 | `struct adoption_model{init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; orphaned,adopted,wait_owner}` | 孤儿进程重新挂到 init 下——树的「重挂载」维护（对照 cgroup 的移动任务） |
| ramfs 目录树 | `struct ramfs_node{name_hash,parent,inode,type,valid}` | 目录/文件的父-子链（对照 cgroup 的目录树挂载点） |
| init 根 | `init_model_start`：`init_model.pid=FIXED_PID`、`ready=1` | 树根：所有孤儿最终归于 init（对照 cgroup_root） |
| shell 协调 | `shelltest` 更新 `init_model.commands/files/processes/pipes/signals` | 各子系统向根汇总计数（对照层级统计聚合） |
| 模块模型 | `struct module_model{name_hash,init_calls,exit_calls; loaded,initialized}` | 扁平结构（无层级）——反例：教学内核并非所有结构都是树 |

### 2.5 检查点模型：lesson_146_model 与 l153test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `146→149` 标记 Origin 为 Lesson 146（`a=146,b=147,c=148,d=149`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「层级连续性」。本课同时把上一课新增的 `l152test` 恢复为历史命名 `l145test`（挂 `lesson_145_state`，计数 `145→148`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（cgroup 层级树）**不是本课新写的代码**：进程树与收养模型来自进程阶段，ramfs 目录树来自 VFS 阶段。本课实际增量只有三处：`l152test`→`l145test` 更名、`lesson_146_model`+`l153test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「层级结构」主题重新组织，并如实说明：**cgroup 层级对象（`struct cgroup` 式结构）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l152test`→`l145test` 恢复命名；新增 `lesson_146_model`/`lesson_146_state`/`l153test`；`about` 与开机横幅更新。cgroup 层级主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`cgroup 层级`/`l153test`/`Lesson 153`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（层级结构机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_146_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_146_model lesson_146_state;
static TEXT64 void l153test(u16*c){lesson_146_state=(struct lesson_146_model){146U,147U,148U,149U,1,1,1,1};int ok=lesson_146_state.valid&&lesson_146_state.active&&lesson_146_state.ready&&lesson_146_state.accounted&&lesson_146_state.b==lesson_146_state.a+1U;text64(c,"l153test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 146 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `146→149`（Origin Lesson 146），四布尔位全置 1，`b==a+1U` 校验计数连续。
2. **逻辑分析（≥3 行）**：结构体字面量一次性写入 `lesson_146_state`，`ok` 由四布尔位 + `b==a+1U` 合取；字面量全 1 使断言恒真，成功串必输出；`Lesson 146 fallback reported` 是防御性兜底，仅在模型计数被破坏时命中。
3. **输出串（逐字抄录）**：成功 `l153test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 146 fallback reported`。
4. **恢复的 `l145test`**：本课把上一课 `l152test` 更名回 `l145test`（同为 `lesson_145_state`，计数 `145→148`）；`l100test`–`l144test` 历史检查点全部保留。

#### 3.2.2 层级结构一：进程树（parent_pid）

```c
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
#define TASK_TABLE_CAP 4U
static struct task_struct task_table[TASK_TABLE_CAP];
```

1. **父-子链**：`parent_pid` 字段把任务组织成一棵以内核哨兵（`task_table[0]`，pid=0）为根的树——每个任务只有一个父，形成严格树形。
2. **层级不变量**：`task_table_validate` 断言 `parent_pid>=t->pid` 判失败——父进程号必须小于子进程号，保证祖先链单调向上，不会成环。
3. **与 cgroup 对照**：cgroup 的任务树与进程树是**两个维度**的层级——进程树由 `fork` 建立，cgroup 树由 `mkdir` 建立；本课以进程树作为「层级」的入门样板。

#### 3.2.3 层级结构二：孤儿收养模型（树的动态维护）

```c
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
static TEXT64 void adoption_start(void){adoption_model=(struct adoption_model){1,FIXED_PID,SECOND_PID,FIXED_PID,0,0,0,0,0};}
```

1. **初始拓扑**：`init_pid=1`（收养者，树根）、`original_parent=FIXED_PID`（原始父）、`child_pid=SECOND_PID`（被收养子）、`current_parent=FIXED_PID`——一棵「init → 子进程」的两层树。
2. **孤儿化**：`adoption_exit_parent` 在父退出时置 `orphaned=1`、`wait_owner=init_pid`，触发重挂——「父死了，孩子挂到 init」的树维护（对照 PID namespace 的 init 收养语义与 cgroup 任务迁移）。
3. **重挂**：`adoption_reparent` 把 `current_parent` 改为 `init_pid`、置 `adopted=1`、`adoptions++`——层级树中节点换父的完整一步。

```c
static TEXT64 int adoption_reparent(void){if(!adoption_model.orphaned||adoption_model.adopted)return 0;adoption_model.current_parent=adoption_model.init_pid;adoption_model.adopted=1;adoption_model.adoptions++;return 1;}
```

1. **前置守卫**：`!orphaned||adopted` 直接返回 0——只有「确实孤儿且尚未被收养」时才允许重挂，防止重复收养。
2. **状态更新**：`current_parent=init_pid` 完成换父，`adopted=1` 防止再次执行，`adoptions++` 记录一次收养。
3. **教学意义**：这就是「把节点从树的一处移动到另一处」的元数据模拟——cgroup 把任务写进另一个 cgroup 的 `cgroup.procs` 同样是换父，但真实内核要遍历子树、刷新 `css_set`。

#### 3.2.4 层级结构三：ramfs 目录树与 init 根

```c
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static TEXT64 void init_model_start(void){init_model=(struct init_model){FIXED_PID,1,0,0,0,0,0,1};shell_runtime_start();}
```

1. **目录树的 parent 链**：`ramfs_node.parent` 指向父节点索引，`ramfs_lookup("/bin/sh")` 沿路径逐级下降——树形路径解析（对照 cgroup 伪文件系统把层级暴露为目录树）。
2. **init 即树根**：`init_model.pid=FIXED_PID`（1）、`ready=1`——进程树的「收养根」，与 cgroup_root 的「根 cgroup」角色对应。
3. **统计汇总**：`shelltest` 更新 `init_model.commands/files/processes/pipes/signals` 五个计数——各子系统向根汇总，正是 cgroup 层级「父组统计 = 子组之和」的朴素版本。

#### 3.2.5 exec64 增量与开机横幅

- `about` 输出 `Lesson 153: cgroup 层级\n`；检查点分支：
```c
else if(eq64(word,"l145test")){if(!noargs64(arg))usage64(c,"l145test");else l145test(c);}else if(eq64(word,"l153test")){if(!noargs64(arg))usage64(c,"l153test");else l153test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 153: cgroup 层级\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `objcopy` → `boot.S` 嵌入 → `grub-mkrescue`）。`make check` 断言 README 含 `cgroup 层级`、`Lesson 153`，kernel64.c 含 `l153test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ adoption_start()（init 收养树初始化）/ init_model_start() / vfs_init()（ramfs 树）
 ├─ 横幅 "Lesson 153: cgroup 层级"
 └─ 主循环：命令 → exec64
     ├─ l153test / l145test → 阶段检查点（lesson_146_state / lesson_145_state）
     ├─ adoptioninfo → 收养树六元组（init/original/child/current/adoptions/owner）
     ├─ reparenttest → 孤儿化 + 重挂 + wait owner 全流程断言
     ├─ taskvalidate → 进程树 PID/TID 唯一性与父子断言
     ├─ initinfo / shelltest → init 根与子系统统计汇总
     └─ ramfsinfo / pathtest → ramfs 目录树路径解析
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`adoption_start()` 建两层收养树，`init_model_start()` 把 init 置为 PID 1 根，`vfs_init()` 建 ramfs 目录树，打印横幅 `Lesson 153: cgroup 层级`。
2. **`l153test`** → `l153test(c)` → 初始化 `lesson_146_state` → 五条件断言 → `l153test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`reparenttest`** → `adoption_start` → `adoption_exit_parent`（孤儿化）→ `adoption_wait_owner` → 断言 `orphaned&&adopted&&adoptions==1` → `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`。
4. **`adoptioninfo`** → `adoption init/original/child/current/adoptions/owner: 1/1/2/1/1/1`（依状态而变）——收养树拓扑一览。
5. **`ramfsinfo`/`pathtest`** → ramfs 目录树的 parent 链与路径解析。
6. **`about`** → `Lesson 153: cgroup 层级`。

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
Multiboot2 and Lesson 153 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 153: cgroup 层级` 横幅 |
| `l153test` | `l153test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l145test` | `l145test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `reparenttest` | `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed` |
| `adoptioninfo` | `adoption init/original/child/current/adoptions/owner: ...` 六元组行 |
| `taskvalidate` | `task validation: passed (bounded table, unique PID/TID, valid parent/state)` |
| `about` | `Lesson 153: cgroup 层级` |

判定成功：`l153test`/`reparenttest` 输出 passed、无 fallback/`BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l153test` 输出 `Lesson 146 fallback reported` | `lesson_146_state` 初始化/断言失败（stale 镜像） | `grep -n "l153test" kernel64.c`；确认初始化串 `{146U,147U,148U,149U,1,1,1,1}` 与 `b==a+1U` |
| `reparenttest` 输出 `BROKEN` | `adoption_exit_parent`/`adoption_reparent`/`adoption_wait_owner` 状态机异常 | 先跑 `adoptioninfo` 看 orphaned/adopted/adoptions；对照 `adoption_reparent` 的 `!orphaned||adopted` 守卫 |
| `adoptioninfo` 的 current_parent 不是 1 | 未执行 `adoption_exit_parent`/`adoption_reparent` | 先跑 `reparenttest` 再跑 `adoptioninfo`；`current_parent=init_pid`（1） |
| `taskvalidate` 输出 `BROKEN` | `task_table` 的父进程号不小于子进程号或 PID 重复 | 对照 `task_table_validate` 的 `parent_pid>=pid` 判负与 O(n²) 唯一性检查 |
| `initinfo` 的 pid 不是 1 | `init_model_start` 未执行或 `init_model` 被覆盖 | `init_model.pid=FIXED_PID`；确认开机时调用了 `init_model_start()` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 153' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `cgroup 层级` 与 `Lesson 153` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `task_struct.parent_pid` 进程树 | `kernel/fork.c` 的 `fork()` 建立父子链；`kernel/exit.c` 的 `forget_original_parent` | 模型只有定长 `task_table[4]`，无 `real_parent`/`parent` 指针与 `sibling` 链表 |
| `adoption_model` 孤儿收养 | `kernel/exit.c` 的 `forget_original_parent`/`find_new_reaper`（孤儿挂到 init 或最近活祖先） | 模型固定收养者为 init（PID 1），无「同祖先优先」选择逻辑 |
| `adoption_reparent` 换父 | `kernel/cgroup/cgroup.c` 的 `cgroup_attach_task`/`css_set` 迁移；`__cgroup_procs_write` | 模型只改 3 个字段，无遍历子树、无 `css_set` 引用计数 |
| `ramfs_node.parent` 目录树 | `fs/ramfs/`；cgroup 的伪文件系统由 `kernel/cgroup/cgroup.c` 挂载为目录树 | 模型无 dcache、无挂载点与 dentry 缓存 |
| `init_model.pid=FIXED_PID`（树根） | `kernel/cgroup/cgroup.c` 的根 cgroup（`cgrp_dfl_root`）；init（PID 1）是收养根 | 模型把「树根」收敛为单个 init 对象 |
| v1 多层级 / v2 统一层级 | `kernel/cgroup/cgroup.c`（v2：`cgroup.controllers`/`subtree_control`）；`kernel/cgroup/cgroup-v1.c` | 模型没有控制器与挂载点，只有一个概念上的根树 |
| `shelltest` 的统计汇总 | `kernel/cgroup/cgroup.c` 的 `cgroup.events`/`cpuacct.usage` 沿层级聚合 | 模型只累加 5 个计数器，无逐层刷新 |

**权威来源**：Linux `kernel/cgroup/cgroup.c`、`kernel/cgroup/cgroup-internal.h`、`kernel/cgroup/cgroup-v1.c`、`kernel/exit.c` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct cgroup`、`css_set` 或 `cgroup.procs` 的等价实现——cgroup 层级是「主题宣告」，教学内核用进程树/收养树/ramfs 树三个既有结构承载「层级」概念。

---

## 8. 思考题与练习

1. **概念理解**：为什么 cgroup 用「树」而不是平铺列表？父组限制对子组如何生效（叠加还是覆盖）？
2. **源码定位**：在 `kernel64.c` 中找出全部带 `parent`/`parent_pid` 字段的结构（提示：`task_struct`、`ramfs_node`、`adoption_model`），说明每处「父」的含义。
3. **动手实验**：修改 `adoption_reparent`，让它把孤儿挂到 `SECOND_PID`（而非 init），运行 `reparenttest` 观察断言变化，说明为什么 `current_parent==FIXED_PID` 断言会失败。
4. **动手实验**：给 `adoption_model` 增加 `reparents` 字段并计数，仿照 `adoptioninfo` 增加打印，重新构建运行。
5. **Linux 对照**：阅读 `kernel/exit.c` 的 `find_new_reaper`，说明它为什么优先选「最近的活祖先」而不是无条件挂 init；对比教学模型「无条件挂 init」的简化。

---

## 9. 本课小结与下一课预告

1. cgroup 层级是把进程组织成资源限制树：父组的限制是子组的天花板，统计沿树向上汇总。
2. Linux 有 v1（多层级）与 v2（统一层级）两种模型，v2 在 `kernel/cgroup/cgroup.c` 中用 `cgroup.controllers`/`cgroup.subtree_control` 控制控制器挂载。
3. 教学内核没有 cgroup 对象，但「层级」素材完整：进程树（`parent_pid`）、收养树（`adoption_model`）、ramfs 目录树（`ramfs_node.parent`）、init 根（`init_model`）。
4. `adoption_reparent` 展示了「树节点换父」的元数据模拟，`reparenttest`/`adoptioninfo` 负责验证与观察。
5. 检查点增量：`l152test`→`l145test` 更名、新增 `lesson_146_model`+`l153test`、横幅与 `about` 更新为 `Lesson 153: cgroup 层级`。
6. 下一课（Lesson 154）主题为 **cgroup CPU 统计**（对照 `kernel/sched/core.c` 与 cgroup cpu 控制器）：层级树之上挂第一个控制器——CPU 使用量的计量与汇总，教学内核将以调度统计（`ticks`/`quantum_left`/`idle_worker_ticks`）承接该主题。
