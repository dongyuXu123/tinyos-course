# Lesson 36: 有界多用户程序运行时与退出回收 — 精讲文档

> **课号**：Lesson 36
> **主题**：有界多用户程序运行时（bounded multi-user-program runtime）与退出回收
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）第五课，
> 也是「有界用户程序运行时」小节的收尾课。
> **前置课程**：[Lesson 35（CPL3-origin IRQ0 保存恢复）](../lesson-35-stable/README.md)
> **后续课程**：[Lesson 37（Linux 风格 task_struct 教学模型总览）](../lesson-37-stable/README.md)
> **一句话目标**：学完本课，你能说清「两个固定用户程序描述符（PID 1/2）如何各自
> 拥有 process/address-space/user-thread 对象、代码页、栈页与保存的 CPL3 上下文
> 元数据，`SYS_EXIT` 如何走到 `EXITED`、`processtest` 如何把 `EXITED` 回收为
> `EMPTY`（`USER_THREAD_RECLAIMED`），以及 PMM 如何拒绝释放两对内嵌帧」。

---

## 1. 课程定位（Mission）

- **一句话目标**：把 Lesson 34–35 的「单进程、单线程」推广为**长度固定为 2 的
  对象数组**：`user_address_spaces[2]`、`user_processes[2]`、`user_threads[2]`，
  并补上生命周期最后一环——`EXITED → EMPTY` 的有界回收（`user_program_reclaim`），
  用 `user_exit_count`/`user_reclaims` 记账。`cpl3test`/`userpitest` 仍只进入
  「活动」的 1 号程序，第二个程序是完整注册的元数据对象。
- **课程主线位置**：32–36 是「用户程序运行时」的完整闭环：镜像（32）→
  地址空间（33）→ 进程/线程对象（34）→ 用户态抢占（35）→ 多程序与回收（36）。
  从 Lesson 37 起进入 Linux 风格模型（task_struct/waitqueue/fork/exec），本课的
  「有界数组 + 状态机 + 回收」正是那个模型的 TinyOS 前身。
- **前置知识清单**：
  1. `struct process`/`struct user_thread`/`saved_user_context` 与生命周期状态机
     （Lesson 34）；
  2. CPL3-origin IRQ0 保存恢复与范围校验（Lesson 35）；
  3. 内嵌镜像复制与 `USER_CODE_VA`/`USER_STACK_VA` 映射（Lesson 32）；
  4. `pmm_reserved`/`pmm_fixed` 对用户代码/栈帧的预留（Lesson 14/29）。
- **本课交付**：`processtest` 文案升级为 `two bounded process/address-space/
  thread objects ready`；`processinfo` 新增 `second pid/state/as/thread` 与
  `programs bounded`、`exits/reclaims` 两行计数；第二个代码/栈镜像出现在
  `USER2_CODE_VA`(0x00500000)/`USER2_STACK_VA`(0x00900000)。

---

## 2. 核心概念精讲

### 2.1 有界多程序运行时：长度为 2 的对象数组

**定义**：

```c
#define SECOND_PID 2ULL
#define USER2_CODE_VA 0x00500000ULL
#define USER2_STACK_VA 0x00900000ULL
#define MAX_USER_PROGRAMS 2U

static struct address_space user_address_spaces[MAX_USER_PROGRAMS];
static struct process user_processes[MAX_USER_PROGRAMS];
static struct user_thread user_threads[MAX_USER_PROGRAMS];
#define user_process user_processes[0]
#define user_thread user_threads[0]
```

**为什么需要**：真实系统有任意多的进程；教学模型用**编译期定长的数组**表达
「多」且保持「有界」——没有动态分配、没有 fork、没有任意镜像加载。`#define
user_process user_processes[0]` 让 Lesson 34/35 的所有既有代码（`user_context_
save`、`user_process_enter/exit`、`user_irq0_save_restore`、`processinfo`）零改动
继续编译，把「单」平滑升级为「多」。
**工作机制**：`kernel_main64_binary` 启动期复制 `kernel_address_space` 到两个
槽（`user_address_spaces[0]=kernel_address_space; [1]=...`），并为 2 号程序填
`pid=SECOND_PID`、`code_phys=user2_code_phys`、`entry=USER2_CODE_VA` 等字段。

### 2.2 第二个用户镜像：两对代码/栈页与两份 PTE

**定义**（`kernel.c`）：

```c
long_mode_handoff.user_code_phys=bootstrap_alloc_page();
long_mode_handoff.user_stack_phys=bootstrap_alloc_page();
long_mode_handoff.user2_code_phys=bootstrap_alloc_page();
long_mode_handoff.user2_stack_phys=bootstrap_alloc_page();
...
{ volatile u8 *code=...user_code_phys; volatile u8 *code2=...user2_code_phys;
  ...
  u32 k; for(k=0;k<user_image.image_bytes;k++){ code[k]=user_image_code[k]; code2[k]=user_image_code[k]; }
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_code_phys|PTE_PRESENT|PTE_USER;
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER;
  pt2[((USER_CODE_VA+0x100000)/PAGE_SIZE)%PAGE_ENTRIES]=...user2_code_phys|PTE_PRESENT|PTE_USER;
  st2[((USER_STACK_VA+0x100000)/PAGE_SIZE)%PAGE_ENTRIES]=...user2_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
```

**为什么需要**：两个程序各要一份镜像页。第二份镜像与第一份**逐字节相同**
（同一 `user_image_code[]`），但落在**不同的虚拟地址**（`USER2_CODE_VA =
USER_CODE_VA+0x100000`，即 4 MiB → 5 MiB），因此需要第二个代码/栈 PTE 槽
（`pt2/st2` 指向 `pt[(5MiB/2MiB)]` 等）。
**工作机制**：同一个 `for` 循环把镜像同时拷进两个代码页——「一份镜像，两处
驻留」；4 个物理页都在 `pmm_reserved` 里登记，PMM 永远不会把它们当空闲帧发出去。

### 2.3 退出回收：EXITED → EMPTY（USER_THREAD_RECLAIMED）

**定义**：

```c
enum user_thread_state { USER_THREAD_EMPTY, USER_THREAD_READY, USER_THREAD_RUNNING,
                         USER_THREAD_EXITED, USER_THREAD_RECLAIMED };

static TEXT64 void user_program_reclaim(u32 i){
    if(i>=MAX_USER_PROGRAMS||user_processes[i].state!=PROCESS_EXITED) return;
    user_threads[i].state=USER_THREAD_RECLAIMED;
    user_processes[i].state=PROCESS_EMPTY;
    user_processes[i].context_valid=0;
    user_reclaims++;
}
```

**为什么需要**：Lesson 34 的 `EXITED` 是死路——对象永远停在终止态。本课补上
「回收」边：把已退出的进程对象复位为 `EMPTY`（可被未来重新初始化），用户线程
进入 `RECLAIMED`（区别于 `EMPTY`，记录「它曾存在过并已被回收」）。
**工作机制**：`user_program_reclaim` 只对 `PROCESS_EXITED` 生效（状态前置），
`user_reclaims++` 记账；两个槽在 `process_lifecycle_test` 开头都被调用，使
`processtest` 同时充当「退出→回收」的回归断言。`user_exit_count` 由
`user_process_exit` 递增，两个计数配对展示回收闭环。

### 2.4 多对象一致性断言：user_programs_ready

**定义**：

```c
static TEXT64 int user_programs_ready(void){
    u32 i;
    for(i=0;i<MAX_USER_PROGRAMS;i++)
        if(user_processes[i].state!=PROCESS_READY || user_threads[i].state!=USER_THREAD_READY ||
           !user_processes[i].address_space || user_threads[i].process!=&user_processes[i]) return 0;
    return 1;
}
```

**为什么需要**：单对象断言（Lesson 34 的 `process_lifecycle_test`）扩展为对
**每个槽**的一致性检查：状态都是 READY、地址空间指针非空、线程归属正确。
**工作机制**：`process_lifecycle_test` 先回收两个槽，再调用 `user_programs_ready`
并叠加 1 号程序的旧断言（address_space 指向 `&user_address_spaces[0]` 等），
全部通过才打印 `two bounded process/address-space/thread objects ready`。

### 2.5 PMM 所有权：两对帧全部 fixed

**定义**（`kernel64.c` 的 `pmm_reserved` 增量）：

```c
|| overlap(p,e,h->user_code_phys,h->user_code_phys+PAGE_SIZE)
|| overlap(p,e,h->user_stack_phys,h->user_stack_phys+PAGE_SIZE)
|| overlap(p,e,h->user2_code_phys,h->user2_code_phys+PAGE_SIZE)   /* 本课新增 */
|| overlap(p,e,h->user2_stack_phys,h->user2_stack_phys+PAGE_SIZE) /* 本课新增 */
```

**为什么需要**：`pmm_init` 用 `pmm_reserved` 把所有重叠帧 `mark+fix`；新增两个
物理页后必须同步登记，否则 `palloc` 可能把第二程序的代码页发给别的用户。
`pmm_init` 同时读取 `user2_code_phys/user2_stack_phys` 到全局。`page_state` 因此
显示四页都是 `fixed/reserved`，`pfree` 返回 `fixed/reserved` 拒绝释放。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 35） |
|---|---|---|
| `kernel.c` | 32 位引导 + 镜像/加载器 | **核心增量**：handoff 增 `user2_code_phys/user2_stack_phys`；分配并复制第二份镜像；映射第二份代码/栈 PTE |
| `kernel64.c` | 64 位主内核 | **核心增量**：`SECOND_PID`/`USER2_CODE_VA`/`USER2_STACK_VA`/`MAX_USER_PROGRAMS`；三个对象数组 + `user_process`/`user_thread` 宏；`user_exit_count`/`user_reclaims`；`user_program_reclaim`/`user_programs_ready`；`pmm_reserved`/`pmm_init` 覆盖第二对帧；`processinfo`/`processtest`/banner/syscallinfo 文案 |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位链接脚本 | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 对象数组与宏

```c
#define SECOND_PID 2ULL
#define USER2_CODE_VA 0x00500000ULL
#define USER2_STACK_VA 0x00900000ULL
#define MAX_USER_PROGRAMS 2U

static struct address_space user_address_spaces[MAX_USER_PROGRAMS];
static struct process user_processes[MAX_USER_PROGRAMS];
static struct user_thread user_threads[MAX_USER_PROGRAMS];
#define user_process user_processes[0]
#define user_thread user_threads[0]
static u64 user_reclaims, user_exit_count;
```

- `user_processes[1]` 的 `pid=SECOND_PID`；`user_threads[1]` 的 `tid=SECOND_PID`；
- `MAX_USER_PROGRAMS=2U` 出现在 `processinfo` 的 `programs bounded:` 输出中，
  是「有界」的可观测常数；
- `user_exit_count` 在 `user_process_exit` 里 `++`，`user_reclaims` 在
  `user_program_reclaim` 里 `++`，配对反映「退出数 vs 回收数」。

### 3.3 kernel_main64_binary —— 双对象装配（关键函数）

```c
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){
    u16 c=0,n=0;
    pmm_init(h);
    address_space_init(&kernel_address_space,h);
    user_address_spaces[0]=kernel_address_space; user_address_spaces[1]=kernel_address_space;
    user_process.pid=FIXED_PID; user_process.address_space=&user_address_spaces[0];
    user_process.code_phys=user_code_phys; user_process.stack_phys=user_stack_phys;
    user_process.entry=USER_CODE_VA; user_process.stack_top=USER_STACK_TOP;
    user_process.image_bytes=7; user_process.state=PROCESS_READY; user_process.context_valid=0;
    user_thread.tid=FIXED_PID; user_thread.process=&user_process;
    user_thread.kernel_stack_top=runtime_tss.rsp0; user_thread.kernel_stack_bytes=0;
    user_thread.context_address=0; user_thread.transitions=0;
    user_thread.state=USER_THREAD_READY; user_thread.context.valid=0;
    user_address_spaces[1]=kernel_address_space;
    user_processes[1].pid=SECOND_PID; user_processes[1].state=PROCESS_READY;
    user_processes[1].context_valid=0; user_threads[1].state=USER_THREAD_READY;
    user_threads[1].context.valid=0;
    user_processes[1].address_space=&user_address_spaces[1];
    user_processes[1].code_phys=user2_code_phys; user_processes[1].stack_phys=user2_stack_phys;
    user_processes[1].entry=USER2_CODE_VA; user_processes[1].stack_top=USER2_STACK_VA+PAGE_SIZE;
    user_threads[1].tid=SECOND_PID; user_threads[1].process=&user_processes[1];
    ...
}
```

- **签名与职责**：启动装配两个 process/thread/address-space 对象。
- **算法步骤**：① `pmm_init` 收下两对物理页；② 初始化 `kernel_address_space`；
  ③ 复制到 `user_address_spaces[0/1]`（两个地址空间对象初始共享同一页表，后续
  各自演进）；④ 1 号进程按 Lesson 34 字段装配（READY）；⑤ 2 号进程装配：
  `pid=2`、`code_phys=user2_code_phys`、`entry=USER2_CODE_VA`、
  `stack_top=USER2_STACK_VA+PAGE_SIZE`（0x00901000）、`tid=2`、归属
  `&user_processes[1]`；⑥ 后续既有初始化（GDT/TSS 后回填 rsp0、IDT、PIT）。
- **边界与错误处理**：槽 1 的 `context.valid=0` 表示尚未保存任何上下文；
  `image_bytes` 只有槽 0 显式填 7，槽 1 靠 READY 状态约束（活动入口仍是
  `user_process`，即程序 0）。
- **为什么这样设计**：两个对象完整注册但**只有程序 0 可被 `cpl3test`/`userpitest`
  进入**（`user_irq0_save_restore` 与 `user_context_valid` 仍引用 `user_process`）——
  「元数据全部就位，活动路径保持单线程」是有界教学模型的明确取舍。

### 3.4 退出与回收：user_process_exit / user_program_reclaim / user_programs_ready

```c
static TEXT64 int user_process_exit(void){
    if(user_process.state!=PROCESS_RUNNING || user_thread.state!=USER_THREAD_RUNNING ||
       !user_context_valid(&user_thread.context)) return 0;
    user_process.state=PROCESS_EXITED; user_thread.state=USER_THREAD_EXITED;
    user_thread.transitions++; user_exit_count++; return 1;   /* 本课新增 user_exit_count++ */
}
static TEXT64 void user_program_reclaim(u32 i){
    if(i>=MAX_USER_PROGRAMS||user_processes[i].state!=PROCESS_EXITED) return;
    user_threads[i].state=USER_THREAD_RECLAIMED;
    user_processes[i].state=PROCESS_EMPTY;
    user_processes[i].context_valid=0;
    user_reclaims++;
}
static TEXT64 int user_programs_ready(void){
    u32 i;
    for(i=0;i<MAX_USER_PROGRAMS;i++)
        if(user_processes[i].state!=PROCESS_READY||user_threads[i].state!=USER_THREAD_READY||
           !user_processes[i].address_space||user_threads[i].process!=&user_processes[i]) return 0;
    return 1;
}
```

- **user_process_exit**：与 Lesson 34 逻辑一致，仅新增 `user_exit_count++`
  作为全局退出计数。
- **user_program_reclaim**：前置 `state==PROCESS_EXITED`；把线程复位为
  `USER_THREAD_RECLAIMED`、进程复位为 `PROCESS_EMPTY`、清 `context_valid`；
  `user_reclaims++`。槽越界（`i>=MAX_USER_PROGRAMS`）直接 return。
- **user_programs_ready**：两槽都须 READY、address_space 非空、线程归属正确；
  这是「回收之后对象可复用」的判定前提。

### 3.5 process_lifecycle_test / processinfo —— 双对象展示（关键函数）

```c
static TEXT64 int process_lifecycle_test(u16*c)
{
    user_program_reclaim(0); user_program_reclaim(1);
    int ok=user_programs_ready() && user_process.state==PROCESS_READY &&
        user_thread.state==USER_THREAD_READY &&
        user_process.address_space==&user_address_spaces[0] &&
        user_process.code_phys==user_code_phys && user_process.stack_phys==user_stack_phys &&
        user_thread.process==&user_process &&
        user_thread.kernel_stack_top==runtime_tss.rsp0 && !user_thread.context.valid;
    text64(c,"process lifecycle: ");
    text64(c,ok?"two bounded process/address-space/thread objects ready":"BROKEN");
    putc64(c,'\n');
    return ok;
}
```

- **算法步骤**：① 对两槽执行 `user_program_reclaim`（若曾 EXITED 则回收）；
  ② `user_programs_ready()` 校验两槽一致；③ 叠加 1 号程序旧断言（address_space
  指向 `&user_address_spaces[0]`、rsp0、context 未保存）；④ 打印
  `two bounded process/address-space/thread objects ready` 或 `BROKEN`。
- **为什么这样设计**：`processtest` 从「初始就绪断言」升级为「**初始就绪 + 退出后
  回收**」的循环回归——跑完 `cpl3test` 后再次 `processtest` 会把 EXITED 回收成
  EMPTY 并重新判定可复用。

`processinfo` 新增两行（逐字摘自 `processinfo`）：

```c
text64(c,"\nsecond pid/state/as/thread: ");hex64(c,user_processes[1].pid);text64(c," ");
text64(c,process_state_name(user_processes[1].state));text64(c," ");
hex64(c,(u64)(unsigned long)user_processes[1].address_space);text64(c," ");
hex64(c,user_threads[1].tid);text64(c," ");
text64(c,user_thread_state_name(user_threads[1].state));
text64(c,"\nprograms bounded: ");hex64(c,MAX_USER_PROGRAMS);
text64(c," exits/reclaims: ");hex64(c,user_exit_count);text64(c," ");hex64(c,user_reclaims);
```

`second pid/state/as/thread` 展示 2 号对象；`programs bounded` 展示有界上限；
`exits/reclaims` 展示退出/回收计数。

### 3.6 exec64 / syscall_report 文案增量

- `syscallinfo` 第三行改为（逐字摘自 `exec64`）：
  `EXIT reports and intentionally halts; no user IRQ callback or cross-address-space scheduler`
- `about`：`TinyOS lesson 36: bounded CPL3 PIT preemption with saved user context`
- `syscall_report` 课号串：`TinyOS lesson 36 SYS_EXIT` / `TinyOS lesson 36 syscall dispatcher`
- banner（逐字摘自 `kernel_main64_binary`）：

  ```
  TinyOS lesson 36: validated user image with bounded PIT preemption
  GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
  ```

### 3.7 构建管线

Makefile/链接脚本与 Lesson 35 完全一致，无新增构建步骤。`kernel.c` 的第二份
镜像/映射与 `kernel64.c` 的对象数组都是纯 C 扩展，编译产物结构不变。

### 3.8 主控制流

```
kernel_main64_binary
  ├─ pmm_init（两对物理帧 fixed/reserved）
  ├─ address_space_init(kernel) → user_address_spaces[0]=[1]=kernel_address_space
  ├─ 装配 user_processes[0]/user_threads[0]（PID1，READY）
  ├─ 装配 user_processes[1]/user_threads[1]（PID2，READY，entry=0x00500000）
  ├─ ... 既有初始化 ...
  └─ shell
       ├─ processtest ──▶ reclaim(0/1) + user_programs_ready + 单对象断言
       ├─ processinfo ──▶ 双对象 + programs bounded + exits/reclaims
       └─ cpl3test/userpitest ──▶ enter_user（活动对象=程序0）→ syscall → EXIT
            └─ user_process_exit（RUNNING→EXITED, user_exit_count++）→ 停机
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel.c` 分配 4 页（两程序各 code+stack），把同一份镜像拷进两个
   代码页，映射 `0x400000/0x800000`（程序1）与 `0x500000/0x900000`（程序2）；
   `kernel64.c` 登记两对物理帧为 fixed，装配双对象（均 READY）。
2. **processtest**：先 `reclaim(0); reclaim(1)`（此刻均 READY，无 EXITED，不计数），
   再 `user_programs_ready()` + 程序 0 断言 → 打印
   `two bounded process/address-space/thread objects ready`。
3. **processinfo**：显示 1 号 `process pid/state: 0000000000000001 ready`、2 号
   `second pid/state/as/thread: 0000000000000002 ready <as-addr> 0000000000000002 ready`、
   `programs bounded: 0000000000000002`、`exits/reclaims: 0000000000000000 0000000000000000`。
4. **cpl3test**：`enter_user` 进入程序 0 → 5 次 `int 0x80` → EXIT →
   `user_process_exit` 成功（`user_exit_count=1`）→ 报告停机。
5. **退出后的回收路径**（机制说明）：EXIT 停机后无法再输入命令；若把 EXIT
   分支改成返回 shell 再跑 `processtest`，`reclaim(0)` 会把槽 0 复位
   （`user_reclaims=1`、线程 `RECLAIMED`、进程 `EMPTY`），`user_programs_ready`
   重新成立并打印 `two bounded process/address-space/thread objects ready`——
   即「退出 → 回收 → 可复用」的闭环判定逻辑在 `process_lifecycle_test` 开头
   每次都会执行。

---

## 5. 构建、运行与验证

**依赖**：与 Lesson 35 相同。

**构建与格式校验**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 通过时输出：

```
Multiboot2 header check passed.
```

**运行**（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**验证步骤**（全新启动后）：

1. `processtest`，预期（逐字摘自 `process_lifecycle_test`）：

   ```
   process lifecycle: two bounded process/address-space/thread objects ready
   ```

2. `processinfo`，应包含（逐字摘自 `processinfo`）：
   `process pid/state: 0000000000000001 ready`、
   `second pid/state/as/thread: 0000000000000002 ready ... 0000000000000002 ready`、
   `programs bounded: 0000000000000002`、
   `exits/reclaims: 0000000000000000 0000000000000000`。

3. `cpl3test`：镜像 0、1、2、99、3 执行；末屏（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 36 SYS_EXIT
   user requested controlled exit
   saved user context validated; process/thread exited
   halting intentionally
   ```

4. `userpitest` 或 `cpl3test` 可互换验证安全 CPL3 IRQ0 帧路径与未变 syscall ABI；
   `syscallinfo` 第三行（逐字摘自 `exec64`）为
   `EXIT reports and intentionally halts; no user IRQ callback or cross-address-space scheduler`。

5. banner 与 `about`（逐字摘自源码）：banner 为
   `TinyOS lesson 36: validated user image with bounded PIT preemption`，
   `about` 为 `TinyOS lesson 36: bounded CPL3 PIT preemption with saved user context`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `processtest` 打印 `BROKEN` | `user_programs_ready()` 中某槽不 READY、address_space 空或线程归属错 | 逐槽核对 `kernel_main64_binary` 的装配字段；先 `processinfo` |
| `processinfo` 的 2 号 pid 显示 0 | `user_processes[1].pid=SECOND_PID` 未执行 | 确认装配代码在 `user_processes[1].state=PROCESS_READY` 前赋值 pid |
| `palloc` 分配到了 `0x5xxxxx` 附近的帧 | 第二对物理帧未在 `pmm_reserved` 登记 | 检查 `pmm_reserved` 是否含 `user2_code_phys`/`user2_stack_phys` |
| `cpl3test` 后 `processtest` 计数为 0/0 | 回收未触发（`reclaim` 要求 `state==PROCESS_EXITED`，而 EXIT 后直接停机，未再回 shell） | 停机后无法再输入；用「EXITED 后再冷启动」或改实验脚本观察计数 |
| 程序 2 的 PTE 未生效（访问 0x500000 #PF） | `pt2`/`st2` 索引算错 | 复算 `(USER2_CODE_VA/(PAGE_ENTRIES*PAGE_SIZE))` 与 `((USER_CODE_VA+0x100000)/PAGE_SIZE)%PAGE_ENTRIES` |
| `exits/reclaims` 显示非 0 | 曾跑过 EXIT（`user_exit_count++`）或回收（`user_reclaims++`） | 这是**预期**，冷启动后归零 |
| 想验证「第二个程序实际运行」 | 本课只注册元数据，活动路径只有程序 0 | 阅读 Lesson 37+ 的调度归属模型，后续课才会切换用户地址空间 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| `user_processes[MAX_USER_PROGRAMS]` 编译期定长数组 | `task_struct` 的 `pid` 命名空间 + 动态 `alloc_pid()` | Linux 进程数是动态的、有 pid 分配器与回收位图；TinyOS 固定 2 |
| `user_threads[].state`（含 RECLAIMED） | `TASK_RUNNING`/`EXIT_DEAD`/`EXIT_ZOMBIE` + `release_task()` | Linux 回收经 `release_task` 减引用、free mm/file；TinyOS 只复位状态字段 |
| `user_program_reclaim` 单函数回收对象 | `exit_mm()` + `put_files_struct()` + `release_task()` | Linux 回收是多子系统协作；TinyOS 是元数据复位 |
| 双地址空间对象共享同一页表（`[0]=[1]=kernel_address_space`） | 每个进程独立 `mm->pgd`（COW 前共享 `active_mm`） | Linux 有 `clone(CLONE_VM)` 共享语义；TinyOS 复制对象但物理页表暂时相同 |
| `pmm_reserved` 对两对帧 `mark+fix` | `struct page` 的 `PG_reserved`/`refcount` | Linux 用 refcount 与 slab；TinyOS 用 bitmap + fixed 位 |

权威来源：Intel SDM Vol.3 §4/§7、Linux `kernel/fork.c`、`kernel/exit.c`、
`include/linux/sched.h`。

---

## 8. 思考题与练习

1. **概念理解**：`#define user_process user_processes[0]` 这种「宏平移」让旧代码
   零改动，代价是什么？（提示：`user_irq0_save_restore` 里所有引用都指程序 0）
2. **源码定位**：找出 `pmm_reserved` 中第二对帧的登记位置，并解释为什么漏掉它
   会导致「用户代码页被 `palloc` 发给别人」。
3. **动手实验**：把 `MAX_USER_PROGRAMS` 改为 3 并给 `user_processes[2]` 装配
   `pid=3`、`address_space=&user_address_spaces[2]`（需先补 `user_address_spaces`
   数组与 32 位侧第三份镜像），运行 `processtest` 观察 `user_programs_ready`
   是否通过——体会「有界」的扩展成本。
4. **动手实验**：改 `user_program_reclaim` 去掉 `state!=PROCESS_EXITED` 前置，
   在未退出的槽上调用 `processtest`，观察 READY 对象被误复位后的
   `user_programs_ready` 结果。
5. **Linux 对照**：阅读 `kernel/exit.c` 的 `do_exit()` 与 `release_task()`，
   列出 Linux 在 `TASK_ZOMBIE → 彻底回收` 之间至少 4 个步骤，并对照说明
   `user_program_reclaim` 只覆盖了其中哪一类（元数据复位）。

---

## 9. 本课小结与下一课预告

- 两个固定用户程序描述符（PID 1/2）各持 process/address-space/user-thread
  对象、代码页、栈页、入口与上下文元数据；对象数组长度固定为 `MAX_USER_PROGRAMS=2`。
- 第二份镜像与第一份逐字节相同但驻留在 `USER2_CODE_VA`/`USER2_STACK_VA`
  （4 MiB 之上 1 MiB 处），PTE 权限与第一份一致（代码只读/USER、栈可写/USER）。
- 生命周期闭环补齐：`READY → RUNNING → EXITED` 之外新增 `user_program_reclaim`
  的 `EXITED → EMPTY`（线程 `RECLAIMED`），`exits/reclaims` 双计数可见。
- `user_programs_ready` 让 `processtest` 成为「初始就绪 + 退出后回收」的循环回归。
- PMM 把两对代码/栈帧全部 `mark+fix`，`pfree` 拒绝释放；syscall ABI 与
  CPL3 IRQ0 帧路径保持 Lesson 35 原样。
- 有界取舍明确：元数据全部就位，但活动入口仍只有程序 0——跨地址空间调度与
  用户态切换留给后续课程。

**下一课（Lesson 37）**：进入 Linux 风格教学模型——用 `task_struct` 教学模型
总览进程状态机、父子关系与调度归属，把本课「有界对象数组 + 显式状态转换」的
思路放进 Linux 任务模型的框架里对照学习。
