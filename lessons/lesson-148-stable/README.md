# Lesson 148: 进程 namespace — 精讲文档

> **课号**：Lesson 148 ｜ **主题**：进程 namespace（process namespace）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课开启「namespace 三连」（**进程 148** → mount 149 → network 150），网络段（140–147）已在上一课收尾
> **前置课程**：[`../lesson-147-stable/README.md`](../lesson-147-stable/README.md)（网络错误与超时）
> **后续课程**：[`../lesson-149-stable/README.md`](../lesson-149-stable/README.md)（mount namespace 隔离）
> **一句话目标**：讲清「进程 namespace」为什么能把每个容器看成独立的进程世界、Linux 里它由哪些数据结构支撑（`kernel/nsproxy.c`、`include/linux/nsproxy.h`、`kernel/pid_namespace.c`），并把教学内核中继承的 `task_table`/`task_struct`/`FIXED_PID`/`SECOND_PID` 进程号设施按这一主题系统化复述，运行 `l148test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（进程 namespace 对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l147test` 恢复为历史命名 `l140test`（挂 `lesson_140_state`），新增 `lesson_141_model`/`lesson_141_state` 与 `l148test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l148test`（旧 README 所写 `l141test` 按源码勘误，源码中不存在 `l141test` 命令）；另保留历史检查点 `l100test`–`l140test`，以及 `tasklist`/`taskvalidate`/`forkinfo`/`forktest`/`ps`/`processinfo`/`processtest` 等进程号相关回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「进程号按视角隔离」的直觉解释进程 namespace（同一进程在不同 namespace 里看到不同的进程号、容器内第一个进程 PID 是 1）；说出 Linux 中 namespace 由 `nsproxy` 挂到进程上、PID namespace 用 `struct pid_namespace` 与多级 PID 分配实现（`kernel/nsproxy.c`、`include/linux/nsproxy.h`、`kernel/pid_namespace.c`）；在教学内核中沿 `task_table` → `taskvalidate` → `forkinfo` → `l148test` 观察进程号元数据与检查点状态；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 143–156 是 bounded networking / namespace / cgroup / security 收敛检查点阶段；本课是 **namespace 三连的第一课**（进程 148 → mount 149 → network 150），网络段（145–147 状态机/poll/错误超时）已在上一课收尾。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中与「进程号」相关的设施（`task_table`/`task_struct` 的 pid/tid/parent_pid、`fork_model` 的子进程号分配、`FIXED_PID`/`SECOND_PID` 常量）按进程 namespace 主题系统化复述。下一课（Lesson 149）转向 mount namespace 隔离。

**前置知识清单**（学本课前必须掌握）：
1. 进程对象：`struct process`、`struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; }` 与 `task_table`（Lesson 30s/40s）。
2. 进程号常量：`FIXED_PID 1ULL`、`SECOND_PID 2ULL`、`SYS_GETPID 1U`（Lesson 30s）。
3. fork/clone 元数据模型：`fork_model_run`（`child_pid=SECOND_PID+1`）、`forkinfo`/`forktest`（Lesson 39/59s）。
4. 任务状态机：`task_transition`、`task_table_validate` 与 `taskvalidate`（Lesson 37/66s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–147）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 148: 进程 namespace`；
- 新命令 `l148test` 输出 `l148test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `tasklist`/`taskvalidate`/`forkinfo`/`ps` 继续展示进程号元数据与任务表状态。

---

## 2. 核心概念精讲

### 2.1 进程 namespace：每个「世界」的进程号都从 1 开始

**直觉**：容器里敲 `ps`，第一个进程的 PID 是 1；宿主机的 `ps` 里它却是 12345。同一份代码、同一个进程，站在两个「世界」里看到了不同的进程号。这种「进程号按视角不同而不同」的机制就是 **进程 namespace**（PID namespace）。

**准确定义**：进程 namespace 把进程号（PID）组织成**嵌套的层级树**。每个 namespace 有自己的 PID 分配空间；进程在**自己的 namespace 内**分配到一个「局部 PID」，在**父 namespace 及更上层**各有一个「映射 PID」，到宿主机视角就是全局 PID。`init`（PID 1）在任一 namespace 中都是「根进程」，它负责收养孤儿、终结时整个 namespace 的进程都被终结（对照 Linux：namespace 内 PID 1 退出导致 `SIGKILL` 全体）。

### 2.2 为什么需要进程 namespace（动机）

1. **隔离**：进程号是进程的「名字」，名字可被隔离才谈得上环境隔离——容器内进程看不到宿主进程的 PID，无法凭 PID 猜测/攻击其他容器。
2. **可移植的 PID 1**：服务管理约定「PID 1 是 init，负责收尸」，若容器内第一个进程不是 PID 1，init 语义就错位。进程 namespace 让每个容器都有「自己的 PID 1」。
3. **进程生命周期边界**：namespace 是进程树的边界——`kill(1, SIGKILL)` 只杀本 namespace 的进程，信号路由被 namespace 截断。

### 2.3 Linux 中进程 namespace 的工作机制

- **nsproxy**：进程的 `task_struct.nsproxy`（`include/linux/nsproxy.h` 的 `struct nsproxy`）持有所有 namespace 的指针：`pid_ns_for_children`（PID）、`mnt_ns`（mount）、`net_ns`（network）、`uts_ns`、`ipc_ns`、`cgroup_ns`、`time_ns`——这就是「进程属于哪些世界」的挂载点，`kernel/nsproxy.c` 的 `copy_namespaces()` 在 fork 时复制或共享。
- **PID namespace**：`kernel/pid_namespace.c` 定义 `struct pid_namespace`（`level`、`pidmap`、`proc_mnt`……）；`kernel/pid.c` 的 `alloc_pid()` 在 `level` 个层级上逐层分配 PID，`struct pid` 的 `numbers[]` 数组保存每一层的映射。
- **语义对照**：`getpid()` 返回进程在自己所在 namespace 的局部 PID（`task_active_pid_ns()`）；`/proc/<pid>/status` 的 `NSpid` 列出从当前 namespace 到根的整串映射。
- **教学简化**：教学内核没有 `nsproxy`/`pid_namespace` 对象，只有**一份**定长 `task_table`（`pid/tid/parent_pid` 三字段），进程号空间全局唯一、从不重复分配——这相当于「只有一个根 namespace、无子 namespace」的特例，PID 即全局 PID。

### 2.4 教学内核中与「进程 namespace」有关的既有设施

本课主题机制（进程 namespace）**未在源码中实现**，但「进程号」这个主题素材在内核里完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| `FIXED_PID`/`SECOND_PID` | `#define FIXED_PID 1ULL`、`#define SECOND_PID 2ULL` | 教学内核的「初始进程号」：PID 1/2 固定，对应单 namespace 下的根进程 |
| `task_struct` | `struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; }` | Linux `task_struct` 中 `pid`/`tgid`/`real_parent` 的浓缩 |
| `task_table` | `#define TASK_TABLE_CAP 4U; static struct task_struct task_table[TASK_TABLE_CAP];` | 定长任务表（对照 `pid_hash` 全局表）；`taskvalidate` 断言 PID/TID 唯一 |
| `fork_model` | `fork_model_run`：`child_pid=SECOND_PID+1` | 子进程获得新进程号（对照 `alloc_pid` 的号分配） |
| `SYS_GETPID` | `#define SYS_GETPID 1U`，`syscallinfo` 的 `1=GETPID` | `getpid()` 系统调用的教学模拟 |
| 进程对象 | `struct process { u64 pid; ... }`，`user_process.pid=FIXED_PID` | 用户进程的 PID 归属 |

### 2.5 检查点模型：lesson_141_model 与 l148test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `141→144` 标记 Origin 为 Lesson 141（`a=141,b=142,c=143,d=144`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「进程号连续性」。本课同时把上一课新增的 `l147test` 恢复为历史命名 `l140test`（挂 `lesson_140_state`，计数 `140→143`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-147 曾用 `l147test` 名字挂 140 号模型，本课将其名实重新对齐）。

### 2.6 机制继承 + 检查点增量

本课主题机制（进程 namespace 隔离与 PID 分配）**不是本课新写的代码**：任务表与进程号设施来自进程阶段（Lesson 30s–40s），fork 模型来自 Lesson 39/59s。本课实际增量只有三处：`l147test`→`l140test` 更名、`lesson_141_model`+`l148test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「进程号」主题重新组织，并如实说明：**进程 namespace 对象（`struct pid_namespace` 式结构）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l147test`→`l140test` 恢复命名；新增 `lesson_141_model`/`lesson_141_state`/`l148test`；`about` 与开机横幅更新。进程 namespace 主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`进程 namespace`/`l148test`/`Lesson 148`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（进程号机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l148test(u16*c){lesson_141_state=(struct lesson_141_model){141U,142U,143U,144U,1,1,1,1};int ok=lesson_141_state.valid&&lesson_141_state.active&&lesson_141_state.ready&&lesson_141_state.accounted&&lesson_141_state.b==lesson_141_state.a+1U;text64(c,"l148test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 141 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `141→144`（Origin Lesson 141），四布尔位全置 1，`b==a+1U` 校验计数连续——「进程号世界按顺序演进」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_141_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `142==141+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 141 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l148test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 141 fallback reported`。
4. **恢复的 `l140test`**：本课同时把上一课的 `l147test` 更名回 `l140test`（同为 `lesson_140_state`，计数 `140→143`），使检查点命令名与 Origin 对齐；`l100test`–`l139test` 历史检查点全部保留。

#### 3.2.2 进程号数据结构：task_struct / task_table（PID namespace 的「单世界」形态）

```c
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
#define TASK_TABLE_CAP 4U
static struct task_struct task_table[TASK_TABLE_CAP];
```

1. **进程号三字段**：`pid`（进程号）、`tid`（线程号）、`parent_pid`（父进程号）——Linux `task_struct` 的 `pid`/`tgid`/`real_parent` 浓缩（对照 `include/linux/sched.h`）。
2. **定长表**：`TASK_TABLE_CAP 4U` 固定容量——对照 Linux 全局 `pid_hash`，教学模型只有 4 个槽且永不扩容，保证 freestanding 与容量不变式。
3. **唯一性不变式**：PID/TID 在整个表中唯一、非 0，父子关系满足 `parent_pid<pid`，由 `task_table_validate` 逐条断言——这是「单 namespace 内进程号全局唯一」的教学对应。

```c
static TEXT64 int task_table_validate(void){u32 i,j;if(!task_table[0].valid||task_table[0].pid!=0||task_table[0].tid!=0||task_table[0].parent_pid!=0)return 0;for(i=0;i<TASK_TABLE_CAP;i++){struct task_struct*t=&task_table[i];if(!t->valid||((i!=0)&&(!t->pid||!t->tid))||!task_state_valid(t->state)||!t->kind)return 0;for(j=i+1;j<TASK_TABLE_CAP;j++)if(task_table[j].valid&&i&&j&&(t->pid==task_table[j].pid||t->tid==task_table[j].tid))return 0;if(i&&t->parent_pid>=t->pid)return 0;}return 1;}
```

1. **根槽约定**：`task_table[0]` 是内核哨兵——pid/tid/parent_pid 全 0 且 `valid`，其余槽位必须非 0 PID/TID。
2. **两重不变式**：外层循环校验每个任务的状态合法（`task_state_valid`）且类型非空；内层循环做 O(n²) 唯一性检查，`pid` 与 `tid` 都不允许重复。
3. **父子关系**：`parent_pid>=t->pid` 直接判失败——父进程号必须小于子进程号，保证祖先链单调（对照 PID namespace 的树形嵌套）。这条规则是进程号主题最核心的断言，`taskvalidate` 命令把它打印出来。

#### 3.2.3 进程号初始化与分配：kernel_main64_binary / fork_model_run

```c
user_process.pid=FIXED_PID; user_process.address_space=&kernel_address_space; user_process.code_phys=user_code_phys; user_process.stack_phys=user_stack_phys;
```

1. **PID 1 固定**：`user_process.pid=FIXED_PID`（1）——教学内核的「根进程」，对应进程 namespace 里 PID 1 的 init 语义。
2. **PID 2 固定**：`user_processes[1].pid=SECOND_PID`（2）——第二个用户进程，对应同一 namespace 内按序分配的进程号。
3. **fork 分配新号**：`fork_model_run` 以 `child_pid=SECOND_PID+1` 分配子进程号——教学内核唯一的「PID 分配」点，对照 Linux `kernel/pid.c` 的 `alloc_pid()`。

```c
static TEXT64 int fork_model_run(u16*c,int clone){fork_attempts++;if(fork_model.valid){text64(c,"forktest: bounded child already exists\n");return 0;}fork_model.parent_pid=user_process.pid;fork_model.parent_tid=user_thread.tid;fork_model.child_pid=SECOND_PID+1;fork_model.child_tid=SECOND_PID+1;fork_model.parent_address_space=(u64)(unsigned long)user_process.address_space;fork_model.child_address_space=(u64)(unsigned long)&user_address_spaces[1];fork_model.copied_metadata=RESOURCE_COPIED;fork_model.shared_resources=RESOURCE_SHARED;fork_model.is_clone=(u8)clone;fork_model.valid=1;fork_successes++;if(clone)clone_successes++;text64(c,clone?"cloneinfo: metadata-only clone created\n":"forktest: metadata-only child created\n");return 1;}
```

1. **新号分配（≥3 行）**：`child_pid=SECOND_PID+1`（即 3）在父进程号之上递增，保证「子进程号 > 父进程号」——与 `task_table_validate` 的 `parent_pid<pid` 单调性一致；`parent_tid/child_tid` 同样分配，PID/TID 双序列。
2. **复制/共享边界**：`copied_metadata=RESOURCE_COPIED` 表示用户地址空间元数据被复制，`shared_resources=RESOURCE_SHARED` 表示内核控制台/PIT/PMM 共享——对照 Linux fork 的「COW 页面 + 共享文件表」。
3. **幂等守卫**：`fork_model.valid` 已置位时直接拒绝重复 fork，输出 `forktest: bounded child already exists`——单次分配、不可重复，与 PID namespace 的号回收语义不同（教学模型不回收）。

#### 3.2.4 观察命令：taskvalidate / forkinfo / ps

- `taskvalidate` 输出 `task validation: passed (bounded table, unique PID/TID, valid parent/state)`；
- `forkinfo` 打印 `parent pid/tid`、`child pid/tid/parent`、`address spaces parent/child`、`copied metadata bits/shared resource bits`；
- `ps` 打印 `threads: id state frame stack-pa ...`，`processinfo` 打印 `process pid/state` 与 `user thread tid/state`——进程号的世界观尽收眼底。

#### 3.2.5 exec64 增量与开机横幅

- `about` 输出 `Lesson 148: 进程 namespace\n`；检查点分支：
```c
else if(eq64(word,"l140test")){if(!noargs64(arg))usage64(c,"l140test");else l140test(c);}else if(eq64(word,"l148test")){if(!noargs64(arg))usage64(c,"l148test");else l148test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 148: 进程 namespace\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```
`GETPID`（`SYS_GETPID 1U`）即 `getpid()` 系统调用的教学模拟——用户态取进程号，正是进程 namespace 面向应用的最小接口。

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `进程 namespace`、`Lesson 148`，kernel64.c 含 `l148test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init / reclaim_init / vfs_init / address_space_init
 ├─ user_process.pid=FIXED_PID; user_processes[1].pid=SECOND_PID
 ├─ 横幅 "Lesson 148: 进程 namespace"
 └─ 主循环：命令 → exec64
     ├─ l148test / l140test → 阶段检查点（lesson_141_state / lesson_140_state）
     ├─ taskvalidate → 任务表 PID/TID 唯一性与父子关系断言
     ├─ tasklist / ps / processinfo → 任务与进程号元数据
     ├─ forkinfo / forktest / cloneinfo → 子进程号分配（SECOND_PID+1）
     └─ syscallinfo → 1=GETPID 系统调用面元数据
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，把两个用户进程的 PID 固定为 1/2，打印横幅 `Lesson 148: 进程 namespace`。
2. **`l148test`** → `l148test(c)` → 初始化 `lesson_141_state` → 五条件断言 → `l148test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l140test`**（恢复名） → `l140test(c)` → `lesson_140_state` 断言 → `l140test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`taskvalidate`** → `task_table_validate()` → 根槽/唯一性/父子三组检查 → `task validation: passed (bounded table, unique PID/TID, valid parent/state)`。
5. **`forktest`** → `fork_model_run(c,0)` → `child_pid=SECOND_PID+1` → `forktest: metadata-only child created`。
6. **`about`** → `Lesson 148: 进程 namespace`。

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
Multiboot2 and Lesson 148 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 148: 进程 namespace` 横幅 |
| `l148test` | `l148test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l140test` | `l140test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `taskvalidate` | `task validation: passed (bounded table, unique PID/TID, valid parent/state)` |
| `forktest` | `forktest: metadata-only child created`（首次） |
| `about` | `Lesson 148: 进程 namespace` |

判定成功：`l148test` 输出 passed、无 fallback，`taskvalidate` 输出 passed、无 `BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l148test` 输出 `Lesson 141 fallback reported` | `lesson_141_state` 初始化/断言失败（stale 镜像） | `grep -n "l148test" kernel64.c`；确认初始化串 `{141U,142U,143U,144U,1,1,1,1}` 与 `b==a+1U` |
| `taskvalidate` 输出 `BROKEN` | `task_table` 的 PID/TID 重复、父进程号不小于子进程号、或槽位状态非法 | 对照 `task_table_validate` 的三个循环：根槽检查、O(n²) 唯一性、`parent_pid>=pid` 判负 |
| `forkinfo` 的 child pid 不是 3 | `fork_model_run` 未执行或模型被重置 | 先跑 `forktest` 再跑 `forkinfo`；`fork_model.child_pid=SECOND_PID+1`（即 3） |
| `forktest` 显示 `bounded child already exists` | `fork_model.valid` 仍为真（重复 fork 被幂等守卫拒绝） | 确认模型只允许单次 fork；重启或观察 `forkinfo` 的 `attempts/success` |
| `l148test` 与 `l140test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l148test` 只操作 `lesson_141_state`、`l140test` 只操作 `lesson_140_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 148' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `进程 namespace` 与 `Lesson 148` |
| 输入 `l148test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l148test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `task_struct{pid,tid,parent_pid}` + `task_table[4]` | `include/linux/sched.h` 的 `struct task_struct`；`kernel/pid.c` 的 `pid_hash` | 模型无 `nsproxy`/`pid_ns` 指针、无 PID 位图分配器，PID 全局唯一且固定 1/2 |
| `user_process.pid=FIXED_PID`（1） | `kernel/pid_namespace.c` 的 `init_pid_ns`；`kernel/pid.c` `find_task_by_pid_ns` | 模型只有一个根 namespace，PID 1 即全局 1；无子 namespace 嵌套 |
| `task_table_validate` 的 PID/TID 唯一性 | `kernel/pid.c`：PID 分配循环 + `nr_hashed`；namespace 间允许同名 PID | 模型 O(n²) 查表断言唯一，无哈希、无层级掩码 |
| `parent_pid>=t->pid` 判负 | `kernel/fork.c`：`parent->pid` 与子 PID 属于不同 namespace 层级 | 模型强制父子在同一「表」且父号小，无跨 namespace 父子 |
| `fork_model_run` 的 `child_pid=SECOND_PID+1` | `kernel/pid.c` `alloc_pid()`：逐层分配，每层一个 `pidmap` | 模型只递增一个计数器，无位图、无回收 |
| `nsproxy`（概念对照） | `include/linux/nsproxy.h` 的 `struct nsproxy`；`kernel/nsproxy.c` `copy_namespaces()` | 教学内核**没有** `nsproxy` 结构，进程不携带 namespace 指针集合 |
| `SYS_GETPID`（`1=GETPID`） | `kernel/sys.c` `sys_getpid` → `task_active_pid_ns()` | 模型返回固定 PID 元数据，无「当前 namespace」概念 |
| `l148test` 断言 | 无直接对应（LTP/container 测试套件） | 模型把进程 namespace 主题的可验证状态固化进内核 |

**权威来源**：Linux `kernel/pid_namespace.c`、`kernel/pid.c`、`include/linux/nsproxy.h`、`include/linux/sched.h` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct pid_namespace` 或 `alloc_pid` 的等价实现——进程 namespace 是「主题宣告」，教学内核停留在「单 namespace、进程号全局唯一」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么进程 namespace 让每个容器都有 PID 1？如果容器内第一个进程不是 PID 1，`kill(1, SIGKILL)` 的语义会如何错位？
2. **源码定位**：在 `kernel64.c` 中找出所有写入 `pid` 字段的位置（提示：`kernel_main64_binary`、`fork_model_run`、`task_names_keep`），并说明每处进程号的值。
3. **动手实验**：把 `task_table_validate` 的唯一性检查注释掉，运行 `taskvalidate` 观察输出变化；随后恢复并说明这条检查保护了什么不变式。
4. **动手实验**：仿照「PID 唯一」检查，给 `fork_model_run` 增加断言「`child_pid` 未被占用」，在 `forktest` 里验证，重新构建运行。
5. **Linux 对照**：阅读 `kernel/pid.c` 的 `alloc_pid`，说明它为什么要在 `level` 个层级上各分配一次 PID；对照教学模型「全局唯一 PID」的简化。

---

## 9. 本课小结与下一课预告

1. 进程 namespace 是「进程号按世界隔离」的机制：每个 namespace 内进程号从 1 开始，向根层层映射。
2. Linux 用 `struct nsproxy` 把 PID/mount/network 等 namespace 挂到进程上，PID 分配由 `struct pid_namespace`/`alloc_pid` 逐层完成（`kernel/nsproxy.c`、`include/linux/nsproxy.h`、`kernel/pid_namespace.c`）。
3. 教学内核没有 namespace 对象，但进程号素材完整：`task_struct{pid,tid,parent_pid}`、`task_table`、`FIXED_PID`/`SECOND_PID`、`fork_model` 的 `child_pid=SECOND_PID+1`。
4. `task_table_validate` 的 PID/TID 唯一性与 `parent_pid<pid` 父子单调性是「单 namespace 全局唯一进程号」的教学对应。
5. 检查点增量：`l147test`→`l140test` 更名、新增 `lesson_141_model`+`l148test`、横幅与 `about` 更新为 `Lesson 148: 进程 namespace`。
6. 下一课（Lesson 149）主题为 **mount namespace 隔离**（对照 `fs/namespace.c`、`include/linux/mnt_namespace.h`）：从「进程号世界」转到「挂载点世界」，教学内核将以 `ramfs_nodes`/`vfs_init`/`dentry_table` 文件系统设施承接该主题。
