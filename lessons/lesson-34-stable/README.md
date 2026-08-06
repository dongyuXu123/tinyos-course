# Lesson 34: 有界 process/thread 对象、保存的用户上下文与受控生命周期 — 精讲文档

> **课号**：Lesson 34
> **主题**：有界 process/thread 对象、保存的用户上下文（saved user context）与
> 受控生命周期（READY / RUNNING / EXITED）
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）第三课，
> 进程模型线的起点。
> **前置课程**：[Lesson 33（有界 address-space 对象）](../lesson-33-stable/README.md)
> **后续课程**：[Lesson 35（CPL3-origin IRQ0 抢占）](../lesson-35-stable/README.md)
> **一句话目标**：学完本课，你能说清「一个固定 PID 1 的 process 对象与一个固定
> TID 1 的 user_thread 对象如何持有镜像/地址空间/内核栈/保存的用户返回上下文，
> 使 CPL3 进入变成显式 `READY → RUNNING`、`SYS_EXIT` 变成 `RUNNING → EXITED`，
> 且每次 syscall 都先保存并校验用户上下文」。

---

## 1. 课程定位（Mission）

- **一句话目标**：在 Lesson 33 的 `address_space` 之上新增两个有界对象——
  `struct process`（固定 PID 1，拥有地址空间、镜像代码/栈物理页、入口/栈顶、
  image_bytes 元数据）与 `struct user_thread`（固定 TID 1，拥有内核栈顶元数据、
  指向所属 process 的指针、以及一个完整的 `saved_user_context`）。
  CPL3 进入/退出第一次被建模为显式状态转换，而不再只是「跳进去、停机」。
- **课程主线位置**：第 2 阶段的 `struct thread`（TCB）管的是**内核线程/worker**；
  本课的 `user_thread` 是**用户线程对象**，二者并存但职责分离。这是走向 Linux
  `task_struct` 教学模型（Lesson 37）前，TinyOS 自己最小的「进程/线程」二元模型。
- **前置知识清单**：
  1. `address_space` 对象与 `kernel_address_space`（Lesson 33）；
  2. 内嵌用户镜像与校验（Lesson 32）、syscall ABI 与 all-GPR 帧（29–31）；
  3. `struct syscall_frame` 的 20 qword 布局（Lesson 29/31）；
  4. `runtime_tss.rsp0` 作为用户态进内核的内核栈顶（Lesson 24/28）。
- **本课交付**：两个新命令 `processtest`、`processinfo`；`cpl3test` 的末屏从
  「声明帧合法」升级为「**保存上下文并校验** + process/thread 显式退出」；
  每发一次非 EXIT 系统调用，`saved_user_context` 就更新一次（`saves` 计数可见）。

---

## 2. 核心概念精讲

### 2.1 process 对象：拥有「地址空间 + 镜像 + 入口」的容器

**定义**：

```c
struct process { u64 pid; struct address_space *address_space; u64 code_phys, stack_phys, entry, stack_top; u32 image_bytes; u8 state, context_valid; };
static struct process user_process;
```

**为什么需要**：操作系统里「进程」是资源容器——它持有地址空间、代码、栈、
入口点。TinyOS 教学模型用固定 PID 1 单进程起步：`pid` 是身份（GETPID 返回它）；
`address_space` 是指向 `kernel_address_space` 的指针（本课仍共享同一个对象）；
`code_phys/stack_phys` 是镜像的两页；`entry/stack_top` 是合法 RIP/RSP 边界；
`image_bytes` 记录镜像字节数元数据（本课为 7，即入口处第一条 7 字节调用段，
配合下文上下文校验的精确匹配）。
**工作机制**：`kernel_main64_binary` 启动期一次性初始化：

```c
user_process.pid=FIXED_PID; user_process.address_space=&kernel_address_space;
user_process.code_phys=user_code_phys; user_process.stack_phys=user_stack_phys;
user_process.entry=USER_CODE_VA; user_process.stack_top=USER_STACK_TOP;
user_process.image_bytes=7; user_process.state=PROCESS_READY; user_process.context_valid=0;
```

`state=PROCESS_READY` 表示「进程已注册、等待首次进入」。

### 2.2 user_thread 对象与保存的用户上下文

**定义**：

```c
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result; u8 valid; };
struct user_thread { u64 tid, kernel_stack_top, kernel_stack_bytes, context_address, transitions; u8 state; struct process *process; struct saved_user_context context; };
static struct user_thread user_thread;
```

**为什么需要**：用户线程对象的职责是「记住用户执行到哪、内核栈用哪段、上下文
放哪」。`context.frame` 是最近一次 syscall 时保存的完整 20-qword 帧；
`saves` 是保存次数；`last_syscall/last_result` 记账；`valid` 标志上下文是否有效。
`kernel_stack_top/kernel_stack_bytes` 从 `runtime_tss.rsp0` 取（rsp0 就是用户态
进内核时 CPU 换栈的目标栈顶）。

**工作机制**（启动期初始化 + rsp0 后校准）：

```c
user_thread.tid=FIXED_PID; user_thread.process=&user_process;
user_thread.kernel_stack_top=runtime_tss.rsp0; ...
user_thread.state=USER_THREAD_READY; user_thread.context.valid=0;
...
runtime_gdt_tss_init(); user_thread.kernel_stack_top=runtime_tss.rsp0;
user_thread.kernel_stack_bytes=PAGE_SIZE;   /* rsp0 栈确定后回填真实值 */
```

先临时初始化，等 `runtime_gdt_tss_init()` 计算出 rsp0 栈顶后**回填**——顺序
敏感点，体现了「对象元数据依赖硬件初始化结果」。

### 2.3 生命周期状态机：READY → RUNNING → EXITED

**定义**：

```c
enum process_state { PROCESS_EMPTY, PROCESS_READY, PROCESS_RUNNING, PROCESS_EXITED };
enum user_thread_state { USER_THREAD_EMPTY, USER_THREAD_READY, USER_THREAD_RUNNING, USER_THREAD_EXITED };
```

**为什么需要**：没有状态机的「进用户态/停机」无法回答「现在进程处于什么阶段」。
状态机让每次转换可审计（`transitions` 计数）、可拒绝（状态不对时返回失败）。

**工作机制**——进入转换（`cpl3test` 时调用）：

```c
static TEXT64 int user_process_enter(struct long_mode_handoff *h)
{
    if(!h || user_process.state!=PROCESS_READY || user_thread.state!=USER_THREAD_READY) return 0;
    user_process.state=PROCESS_RUNNING; user_thread.state=USER_THREAD_RUNNING;
    user_thread.transitions++; return 1;
}
```

退出转换（`SYS_EXIT` 时调用）：

```c
static TEXT64 int user_process_exit(void)
{
    if(user_process.state!=PROCESS_RUNNING || user_thread.state!=USER_THREAD_RUNNING ||
       !user_context_valid(&user_thread.context)) return 0;
    user_process.state=PROCESS_EXITED; user_thread.state=USER_THREAD_EXITED;
    user_thread.transitions++; return 1;
}
```

`transitions++` 精确记录一次进入/一次退出；`EXITED` 是终止态（本课回收推迟到
Lesson 36）。

### 2.4 保存的用户上下文与有效性校验

**定义**：

```c
static TEXT64 void user_context_save(struct syscall_frame *f, u64 result)
{
    if(!f || user_thread.process!=&user_process || user_process.state!=PROCESS_RUNNING) return;
    user_thread.context.frame=*f;
    user_thread.context.last_syscall=f->rax;
    user_thread.context.last_result=result;
    user_thread.context.saves++;
    user_thread.context.valid=1;
    user_thread.context_address=(u64)(unsigned long)&user_thread.context.frame;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);
}
static TEXT64 int user_context_valid(struct saved_user_context *c)
{
    return c && c->valid && c->frame.cs==USER_CS && c->frame.ss==USER_DS &&
        c->frame.rip==USER_CODE_VA && c->frame.rsp==USER_STACK_TOP;
}
```

**为什么需要**：`syscall_report` 在**每个** syscall（含 EXIT）时保存现场，从而
「用户返回帧」从 CPU 的临时栈变成线程对象的持久状态。`user_context_valid` 要求
帧的选择子、RIP、RSP 与预设边界**精确匹配**——本课用户帧的 RIP 恒等于入口
（因为 IF=0、无抢占，RIP 不会前进到镜像中间）。

**工作机制**：保存先于打印；`user_process.context_valid` 缓存校验结果，
`processinfo` 直接显示 `validated` / `not validated`。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 33） |
|---|---|---|
| `kernel64.c` | 64 位主内核 | **核心增量**：process/thread 状态枚举、`saved_user_context`/`process`/`user_thread` 结构、`user_context_save/valid`、`user_process_enter/exit`、`processinfo`、`process_lifecycle_test`；`enter_user` 先做状态转换；`syscall_report` 每次保存上下文；`kernel_main64_binary` 初始化两对象；`exec64` 新增两命令 |
| `kernel.c` | 32 位引导 + 内嵌镜像 | 未变化 |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位链接脚本 | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 数据结构精讲

```c
enum process_state { PROCESS_EMPTY, PROCESS_READY, PROCESS_RUNNING, PROCESS_EXITED };
enum user_thread_state { USER_THREAD_EMPTY, USER_THREAD_READY, USER_THREAD_RUNNING, USER_THREAD_EXITED };
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result; u8 valid; };
struct process { u64 pid; struct address_space *address_space; u64 code_phys, stack_phys, entry, stack_top; u32 image_bytes; u8 state, context_valid; };
struct user_thread { u64 tid, kernel_stack_top, kernel_stack_bytes, context_address, transitions; u8 state; struct process *process; struct saved_user_context context; };
```

- `context.frame` 内嵌 20×8=160 字节，使线程对象自带上下文而无需外部分配。
- `context_address` 冗余保存帧地址，方便 `processinfo` 显示与调试器比对。
- `process.context_valid` 与 `thread.context.valid` 分居两层：前者是「进程层
  结论」，后者是「线程层标志」，`user_context_save` 用前者缓存校验结果。

### 3.3 syscall_report —— 每次保存 + EXIT 显式退出（关键函数）

```c
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;
clear64(&c);
if((u32)f->rax==SYS_EXIT){
  user_context_save(f,0);
  text64(&c,"TinyOS lesson 34 SYS_EXIT\nuser requested controlled exit\n");
  if(user_process_exit()) text64(&c,"saved user context validated; process/thread exited\n");
  else text64(&c,"controlled exit rejected: invalid lifecycle\n");
  text64(&c,"halting intentionally\n");
  for(;;)__asm__ volatile("cli; hlt");
}
result=syscall_dispatch(f,&c);
user_context_save(f,result);
text64(&c,"TinyOS lesson 34 syscall dispatcher\nsyscall number: ");
hex64(&c,number); ... /* 打印 number/rax/rip/cs/rsp/ss 与 all-GPR 说明 */
}
```

- **签名与职责**：每个 syscall 的收口：保存上下文 → （EXIT 时）退出 → 报告。
- **算法步骤**：① EXIT 路径：`user_context_save(f,0)` 先保存（EXIT 的 result 为 0）
  → 打印课号与 `user requested controlled exit` → `user_process_exit()` 若成功打印
  `saved user context validated; process/thread exited`，否则打印
  `controlled exit rejected: invalid lifecycle` → `halting intentionally` →
  `cli; hlt`；② 非 EXIT 路径：`syscall_dispatch` 得 result → `user_context_save(f,result)`
  → 打印 dispatcher 报告。
- **边界与错误处理**：`user_process_exit` 失败（状态不对或上下文无效）时明确打印
  `controlled exit rejected: invalid lifecycle`——这是本课唯一的「拒绝性」可观察点，
  正常流程不会触发。
- **为什么这样设计**：保存发生在**每一次**系统调用（不是只有 EXIT），这为
  Lesson 35 的 IRQ0 抢占准备了「始终最新的用户现场」。

### 3.4 enter_user —— CPL3 进入的显式状态转换（关键函数）

```c
static TEXT64 void enter_user(struct long_mode_handoff*h){
    if(!user_process_enter(h)){ return; }
    __asm__ volatile("cli; call enter_user_c":::"memory");
}
```

- **签名与职责**：`cpl3test` 调用的入口；先做 `READY → RUNNING` 转换，成功才
  真正进用户态。
- **算法步骤**：① `user_process_enter(h)`：要求 `h` 非空、进程 READY、线程
  READY；② 失败直接 return（命令层可见「没有任何反应」，因为状态机拒绝了
  二次进入）；③ 成功后 `cli; call enter_user_c`。
- **边界与错误处理**：转换失败不打印任何消息——有意为之，让「状态机拒绝」与
  「正常进入」可区分（正常进入会立即看到后续 dispatcher 输出）。
- **为什么这样设计**：`READY → RUNNING` 从此成为用户执行的**前置条件**，
  syscall 保存路径与退出路径都以 `PROCESS_RUNNING` 为前提，三者形成闭环。

### 3.5 processinfo —— 对象与上下文的展示（关键函数）

```c
static TEXT64 void processinfo(u16*c)
{
    text64(c,"process pid/state: ");hex64(c,user_process.pid);text64(c," ");text64(c,process_state_name(user_process.state));
    text64(c,"\naddress-space: ");hex64(c,(u64)(unsigned long)user_process.address_space);
    text64(c,"\nimage code/stack: ");hex64(c,user_process.code_phys);text64(c," ");hex64(c,user_process.stack_phys);
    text64(c,"\nentry/stack-top: ");hex64(c,user_process.entry);text64(c," ");hex64(c,user_process.stack_top);
    text64(c,"\nuser thread tid/state: ");hex64(c,user_thread.tid);text64(c," ");text64(c,user_thread_state_name(user_thread.state));
    text64(c,"\nkstack top/bytes: ");hex64(c,user_thread.kernel_stack_top);text64(c," ");hex64(c,user_thread.kernel_stack_bytes);
    text64(c,"\nsaved context/address/saves: ");text64(c,user_thread.context.valid?"valid":"empty");hex64(c,user_thread.context_address);text64(c," ");hex64(c,user_thread.context.saves);
    text64(c,"\ncontext lifecycle: ");text64(c,user_process.context_valid?"validated":"not validated");text64(c," transitions ");hex64(c,user_thread.transitions);putc64(c,'\n');
}
```

- **签名与职责**：展示 process/thread 全部元数据与上下文状态。
- **算法步骤**：依次打印 pid/state、address-space 指针、code/stack 物理页、
  entry/stack-top、tid/state、kstack top/bytes、saved context（valid/empty、
  地址、saves 计数）、context lifecycle（validated/not validated + transitions）。
- **为什么这样设计**：`saves` 与 `transitions` 是本课两个「会变化」的观测点——
  `processtest` 之后 `saves=0`；跑完任意一次 syscall 后 `saves` 增长；跑完
  `cpl3test` 后进程变 `exited`、`transitions=2`。

### 3.6 process_lifecycle_test —— 初始有界生命周期断言（关键函数）

```c
static TEXT64 int process_lifecycle_test(u16*c)
{
    int ok=user_process.state==PROCESS_READY && user_thread.state==USER_THREAD_READY &&
        user_process.address_space==&kernel_address_space && user_process.code_phys==user_code_phys &&
        user_process.stack_phys==user_stack_phys && user_thread.process==&user_process &&
        user_thread.kernel_stack_top==runtime_tss.rsp0 && !user_thread.context.valid;
    text64(c,"process lifecycle: ");text64(c,ok?"bounded one-user-thread object ready":"BROKEN");putc64(c,'\n');
    return ok;
}
```

- **签名与职责**：验证对象图的 8 项不变量，全中打印
  `bounded one-user-thread object ready`。
- **算法步骤**：① 进程 READY 且线程 READY；② address_space 指向
  `&kernel_address_space`；③ code/stack 物理页与全局一致；④ 线程所属进程是
  `&user_process`；⑤ 内核栈顶等于 `runtime_tss.rsp0`；⑥ 上下文尚未保存
  （`!valid`）。
- **为什么这样设计**：这是「启动即正确」的回归断言——在跑任何用户代码之前
  验证对象图已装配完整。

### 3.7 exec64 / kernel_main64_binary 增量

- `help` 文案在 `clear` 前新增 `processinfo processtest`。
- 新增命令：
  - `processinfo` → `processinfo(c)`；
  - `processtest` → `process_lifecycle_test(c)`。
- `about`：`TinyOS lesson 34: bounded process/thread object with saved user context`
- banner：`TinyOS lesson 34: validated embedded user image and SYS_EXIT`

### 3.8 构建管线与主控制流

构建与 Lesson 33 完全一致（无新增构建步骤）。

```
kernel_main64_binary
  ├─ pmm_init / address_space_init
  ├─ user_process.* 初始化（READY）
  ├─ user_thread.* 初始化（READY）→ runtime_gdt_tss_init 后回填 rsp0/栈字节数
  ├─ ... 既有初始化 ...
  └─ shell: processtest → processinfo → cpl3test
        ├─ enter_user → user_process_enter（READY→RUNNING）
        ├─ 镜像 5 次 int 0x80：每次 syscall_report 先 user_context_save
        └─ SYS_EXIT → user_process_exit（RUNNING→EXITED）→ 报告 → 停机
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 装配 `user_process`（READY）与
   `user_thread`（READY）；`runtime_gdt_tss_init` 之后把真实 `rsp0` 与
   `PAGE_SIZE` 回填进线程对象。
2. **processtest**：`process_lifecycle_test` 断言 8 项不变量，通过打印
   `bounded one-user-thread object ready`。
3. **processinfo**：显示 `process pid/state: 0000000000000001 ready`、
   `saved context/address/saves: empty ... 0000000000000000`、
   `context lifecycle: not validated transitions 0000000000000000`。
4. **cpl3test**：`enter_user` → `READY→RUNNING`（`transitions=1`）→ 镜像执行
   syscall 0、1、2、99：每次 `user_context_save` 保存帧、`saves` 递增、
   `context_valid` 置位并校验通过（RIP 恰为入口、RSP 恰为栈顶）。
5. **SYS_EXIT**：先 `user_context_save(f,0)`，再 `user_process_exit` 成功 →
   `RUNNING→EXITED`（`transitions=2`）→ 打印三段报告 → `cli; hlt`。
6. EXIT 停机后无法再输入命令；若要观察 `exited`/`validated`/`transitions=2`/
   `saves` 累计 5 的终态，需要把 EXIT 分支的停机改成返回 shell 再跑
   `processinfo`（这是思考题 4 的实验方向）。

---

## 5. 构建、运行与验证

**依赖**：与 Lesson 33 相同。

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
   process lifecycle: bounded one-user-thread object ready
   ```

2. `processinfo`，关键行（逐字摘自 `processinfo`）：
   `process pid/state: 0000000000000001 ready`、
   `context lifecycle: not validated transitions 0000000000000000`。

3. `cpl3test`：镜像按 0、1、2、99、3 执行；每份 dispatcher 报告前上下文已被
   保存。末屏（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 34 SYS_EXIT
   user requested controlled exit
   saved user context validated; process/thread exited
   halting intentionally
   ```

   ——**必须**出现 `saved user context validated; process/thread exited` 这一行，
   才代表「保存上下文校验通过 + 生命周期转换成功」。画面冻结为成功。

4. 回归：`vmtest`（`vmtest: two-slot dual-alias map/ownership/unmap/free passed`）、
   `vminfo`、`syscallinfo` 确认 Lesson 33 行为不变。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `processtest` 打印 `BROKEN` | 8 项不变量中有一项不符（address_space 指针、物理页、rsp0、state 等） | 逐项对照 `process_lifecycle_test` 条件；先 `processinfo` 核对 |
| 运行 `cpl3test` 后无任何输出 | `enter_user` 里 `user_process_enter` 失败（进程不在 READY） | 确认未先跑过一次 cpl3test（EXITED 后不可再进入）；冷启动重试 |
| EXIT 末屏出现 `controlled exit rejected: invalid lifecycle` | 上下文无效或状态不在 RUNNING | 检查 `user_context_valid` 的 cs/ss/rip/rsp 精确匹配条件 |
| `processinfo` 的 kstack top 显示 0 | `runtime_gdt_tss_init` 后未回填 `kernel_stack_top` | 确认初始化顺序：`runtime_gdt_tss_init(); user_thread.kernel_stack_top=runtime_tss.rsp0;` |
| `saves` 计数与调用次数不符 | 某次 syscall 未进 `user_context_save`（如进程不在 RUNNING） | 检查 `user_context_save` 的前置条件 |
| 第二次 `cpl3test` 无 EXIT 报告 | 进程已 EXITED，`user_process_enter` 拒绝 | 这是**预期行为**：生命周期状态机不许重入 |
| 镜像 RIP 越过入口后仍被判定 valid | 本课校验是精确匹配 `rip==USER_CODE_VA` | Lesson 35 会放宽为范围匹配；本课 IF=0 保证 RIP 不前进 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| `struct process`（pid/address_space/镜像元数据/state） | `task_struct`（`pid`、`mm`、`comm`、`state`） | Linux task 有完整 mm/fs/files/signal 子树；TinyOS 只有地址空间指针与两页 |
| `struct user_thread` + `saved_user_context` | `thread_struct` / `struct pt_regs`（`task_pt_regs`） | Linux 用 `pt_regs` 存 syscall/信号帧，含完整段寄存器与 debug regs；TinyOS 存 20 qword |
| READY/RUNNING/EXITED 状态机 | `TASK_RUNNING`、`TASK_ZOMBIE`/`EXIT_ZOMBIE` | Linux 还有 `TASK_INTERRUPTIBLE`/`D state` 等十几种状态；TinyOS 只保留 4 态 |
| `user_process_enter/exit` 显式转换 | `wake_up_new_task()`、`do_exit()` | Linux 通过调度器与父进程 wait 联动；TinyOS 是命令驱动的单步转换 |
| `user_context_valid` 精确匹配 | 进程切换时 `switch_to` 的 regs 校验 | Linux 不逐字段校验，靠寄存器协议保证；TinyOS 以「可读校验」作教学证据 |

权威来源：Intel SDM Vol.3 §7（TSS/RSP0）、Linux `include/linux/sched.h`、
`arch/x86/include/asm/processor.h`（thread_struct）。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `user_context_save` 在**每次** syscall 都执行，而不仅是
   EXIT？如果只在 EXIT 保存，Lesson 35 的 IRQ0 抢占会缺什么数据？
2. **源码定位**：找出 `user_process_enter` 与 `user_process_exit` 对状态机的
   全部前置条件，画出 `READY → RUNNING → EXITED` 的完整状态图（含拒绝边）。
3. **动手实验**：在 `user_context_valid` 中把 `rsp==USER_STACK_TOP` 改为
   `rsp<=user_process.stack_top`，重跑 `cpl3test` 观察 EXIT 判定是否仍通过，
   解释 RIP 精确匹配为什么在本课可成立（IF=0）。
4. **动手实验**：连续运行两次 `cpl3test`，观察第二次是否打印
   `controlled exit rejected`——若想让它打印，需要改动 `user_process_enter`
   的哪个条件？
5. **Linux 对照**：查阅 `do_exit()` 中 `TASK_ZOMBIE`/`EXIT_ZOMBIE` 的含义，
   说明 TinyOS 的 `PROCESS_EXITED` 缺少 Linux 的哪些后续步骤（回收 mm/file、
   通知父进程等）。

---

## 9. 本课小结与下一课预告

- 新增 `struct process` 与 `struct user_thread` 两个有界对象，把「进用户态」
  从裸跳转升级为显式状态转换：`READY → RUNNING`（enter）、
  `RUNNING → EXITED`（exit）。
- `saved_user_context` 保存完整 syscall 帧 + `saves/last_syscall/last_result`
  记账，每次 syscall 都先保存、再报告；`user_context_valid` 以
  cs/ss/rip/rsp 精确匹配校验帧合法性。
- `processtest`/`processinfo` 让对象图与生命周期可被命令层观测；EXIT 路径
  现在能区分「校验通过正常退出」与「状态/上下文非法拒绝退出」。
- 本课刻意不做：用户态调度、用户态 IRQ——用户帧的 RIP 永远停在入口。

**下一课（Lesson 35）**：IRQ0 开始识别 **CPL3-origin 帧**（按 `CS==USER_CS`
判别）：`irq0_frame` 扩到 20 qword（含 CPU 附加的 `rsp/ss`），
`user_irq0_save_restore` 把完整特权返回帧（RIP/CS/RFLAGS/RSP/SS + 全部 GPR）
保存进线程上下文，再精确恢复原样，同时把 `user_context_valid` 放宽为范围匹配
（RIP 落点随抢占会推进）。IF 策略仍为 0，避免用户态嵌套 IRQ。
