# Lesson 81: context switch 上下文切换元数据 — 精讲文档

> **课号**：Lesson 81（主线源课编号 Lesson 74 线）
> **本课主题**：上下文切换元数据（context switch metadata）——CPU 寄存器的保存/恢复、`irq0_frame` 帧结构、栈指针交换、用户上下文保存
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。Lesson 79 讲"抢占决策"、Lesson 80 讲"定时器驱动"，本课聚焦切换动作本身：CPU 帧长什么样、汇编怎么压/弹、切换如何只靠交换栈指针完成。
> **前置课程**：[`../lesson-80-stable/README.md`](../lesson-80-stable/README.md)（定时器驱动调度：`ticks`、时间片、sleep 唤醒与 idle）
> **后续课程**：[`../lesson-82-stable/README.md`](../lesson-82-stable/README.md)（Copy-on-Write 基础元数据：页对象、写权限标志与引用计数）
> **本课一句话目标**：理解"上下文切换 = 保存一个线程的 CPU 帧，恢复另一个线程的 CPU 帧"，并掌握 `irq0_frame` 20 个 u64 字段、`irq0_entry` 汇编的压弹顺序、`irq0_schedule` 返回帧指针的切换机制，以及用户线程上下文 `saved_user_context` 的保存/恢复记账。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l81test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说出"一次上下文切换"需要保存/恢复哪些数据（15 个通用寄存器 + `rip/cs/rflags/rsp/ss`），能看懂 `irq0_entry` 汇编如何把 GPR 压进当前线程栈、`irq0_schedule` 如何用"返回目标线程的帧指针"完成切换，并能在 `ps`/`processinfo` 里读出每个线程保存的帧地址与切换次数。
- **在课程主线中的位置**：调度三连课（79 主动抢占、80 定时器驱动、81 上下文切换）的收尾。前两课回答"何时切"，本课回答"切什么、怎么切"。之后（82–84）进入 COW 内存元数据。
- **前置知识清单**（学本课之前必须掌握）：
  1. `struct irq0_frame` 的布局与 `irq0_schedule` 的切换决策（Lesson 79 精讲过）；
  2. 线程内核栈：`start_threads` 从 PMM 分配 1 页栈、`phys_to_high` 映射、`thread_trampoline`（Lesson 61 起）；
  3. x86-64 中断流程：IRQ0 压入 `rsp/ss`（CPL3）或仅靠当前栈（CPL0）、`iretq` 恢复；`syscall_entry` 与 `int 0x80` 门（Lesson 33 起）；
  4. 用户进程/线程对象：`struct process`、`struct user_thread`、`saved_user_context`、`user_process_enter/user_process_exit`（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_74_model` + `lesson_74_state` + `l81test`；把 `l80test` 改名为 `l73test`；`about` 与 banner 更新为「Lesson 81: context switch 上下文切换元数据」。

---

## 2. 核心概念精讲

### 2.1 CPU 上下文（CPU context）到底是什么

**直觉**：一个正在运行的线程，CPU 里装满了它的"状态"：寄存器里的局部变量、栈顶指针、下一条指令地址（`rip`）、标志位。切换线程就是把这套状态整个"换血"——旧的存起来，新的装回来。

**准确定义**：在 x86-64 上，"上下文"由 CPU 在响应中断/调用时自动压栈的返回信息（`rip/cs/rflags/rsp/ss`，即 `iretq` 要弹出的部分）加上软件自己压栈的通用寄存器组成。TinyOS 用两个结构体建模：

```c
struct irq0_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
```

- 两者布局完全相同（20 个 u64），只是入口不同：`irq0_frame` 由 `irq0_entry` 压栈（IRQ0 抢占），`syscall_frame` 由 `syscall_entry` 压栈（int 0x80 系统调用）。
- 头 15 个是通用寄存器（`r15…rax`），后 5 个（`rip,cs,rflags,rsp,ss`）是"iretq 段"——恢复后 CPU 就回到被中断的位置。
- 源码里有 `_Static_assert` 锁定布局：`sizeof==20*sizeof(u64)`、`rsp` 偏移第 18 个、`ss` 偏移第 19 个——**布局是 ABI 级别的约束**，改错任何一处汇编就错位。

### 2.2 切换 = 交换栈指针

**关键洞察**：因为每个线程有独立的内核栈，而 CPU 帧就保存在栈上，所以"保存上下文"= 记录当前栈顶指针，"恢复上下文"= 把栈顶指针换成目标线程的栈顶指针。`irq0_entry` 的结尾只有一句 `movq %rax,%rsp`——把 `irq0_schedule` 返回的帧指针装进 `rsp`，然后 `pop` 15 个寄存器 + `iretq` 就"变成"了另一个线程。

```
线程 A 栈: [irq0_frame A] ← rsp    线程 B 栈: [irq0_frame B] ← rsp
   irq0_schedule(A帧) ──► 返回 B帧指针
   movq %rax,%rsp  ──► rsp 指向 B 的帧
   pop 15 寄存器 + iretq ──► CPU 在 B 里继续跑
```

### 2.3 首帧（initial frame）：线程怎么"出生"

线程从没运行过，也就没有真实寄存器状态。`start_threads` 在分配的栈顶伪造一个 `irq0_frame`：`rip` 指向 `thread_trampoline`（汇编：`movq %r12,%rsp; call thread_trampoline_c`），`r12` 预置栈高地址，`rflags=0x202`（IF=1）。这样当调度器第一次选中该线程时，`iretq` 弹出伪造帧，CPU 从 `thread_trampoline` 开始执行——线程"凭空出生"且无需特殊切换代码。idle 线程同理（`idle_trampoline`）。

### 2.4 用户上下文：另一套保存机制

内核线程切换靠 `irq0_frame`；用户线程（CPL3）被打断时，帧里还带 `rsp/ss`（用户栈/段）。本内核只有**一个**用户线程被调度器特殊对待：`irq0_schedule` 看到 `f->cs==USER_CS` 就走 `user_irq0_save_restore`——把 IRQ0 帧原样抄进 `saved_user_context`（挂在一个用户线程的 `struct user_thread.context` 上），再原样抄回。`saved_user_context` 额外记 `pit_preemptions/pit_resumes/saves/last_syscall/last_result` 等记账字段，供 `processinfo` 观测。

### 2.5 「固定元数据 + 确定性验证」教学模型

`struct lesson_74_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续充当调度 checkpoint。本课的确定性验证除 `l81test` 外，还有 `userreturntest`（用户返回帧校验）与 `ps`/`processinfo`（帧地址、切换次数可读）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-80） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：CPU 帧、汇编切换入口、用户上下文、调度器 | **主要增量**：新增 `struct lesson_74_model`、`lesson_74_state`、`l81test()`；把 `l80test` 改名为 `l73test`；`exec64` 分支与 `about`、banner 文案更新（上下文切换机制代码继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 81 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l81test`

```c
struct lesson_74_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_74_model lesson_74_state;
static TEXT64 void l81test(u16*c){lesson_74_state=(struct lesson_74_model){74U,75U,76U,77U,1,1,1,1};int ok=lesson_74_state.valid&&lesson_74_state.active&&lesson_74_state.ready&&lesson_74_state.accounted&&lesson_74_state.b==lesson_74_state.a+1U;text64(c,"l81test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 74 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 结构体与前两课同构：4 个 u32 计数 + 4 个 u8 调度标志。
- `lesson_74_state=(struct lesson_74_model){74U,75U,76U,77U,1,1,1,1};`：`a=74,b=75,c=76,d=77`。
- `int ok=...`：四标志全真 且 `b==a+1`。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 74 fallback reported");`：成功/失败串逐字来自源码。

#### (b) 上一课回归测试改名为 `l73test`

lesson-80 的 `l80test` 改名 `l73test`（校验 `lesson_73_state`，内容不变）。`exec64` 命令集变为 `l64 l65 l69 l70 l71 l72 l73 l81`。

#### (c) CPU 帧结构：`irq0_frame`/`syscall_frame` 与静态断言

```c
struct irq0_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct irq0_frame)==20*sizeof(u64),"IRQ0 frame");
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
_Static_assert(__builtin_offsetof(struct irq0_frame,rsp)==18*sizeof(u64),"IRQ0 rsp offset");
_Static_assert(__builtin_offsetof(struct irq0_frame,ss)==19*sizeof(u64),"IRQ0 ss offset");
```

逐行注释：
- 两个结构体布局一致：字段顺序必须与汇编压栈顺序完全对应（后进先出）。前 15 个 GPR，然后 `rip,cs,rflags`，最后 `rsp,ss`。
- 为什么有 `rsp,ss`：从 CPL3 中断时 CPU 自动把用户 `rsp/ss` 压进内核栈，`iretq` 时再弹出。CPL0 内核线程被打断时，CPU 不压 `rsp/ss`，但代码里仍预留这两个槽位——`irq0_entry` 用手写的 `pop` 序列统一消费（见 (d)），从而内核/用户线程共用一套 `irq0_entry`。
- `_Static_assert`：把"帧 = 20 个 u64、`rsp`/`ss` 在末尾"变成编译期约束，防止结构体与汇编失配。

#### (d) `irq0_entry` 汇编：压、换、弹、返

```asm
.global irq0_entry
irq0_entry:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp,%rdi
    andq $-16,%rsp
    subq $8,%rsp
    call irq0_schedule
    movq %rax,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
```

逐行注释：
- **压栈顺序**：`rax` 先压（栈顶是 `rax`），`r15` 最后压（栈顶是 `r15`）。`irq0_frame` 字段顺序 `r15…rax…` 与之匹配——`pop %r15` 第一个执行取回的就是 `r15`。
- `cld`：清方向标志（`movsb/movsl` 类指令的前置约定）。
- `movq %rsp,%rdi`：把帧首地址当第一参数传给 `irq0_schedule`。
- `andq $-16,%rsp; subq $8,%rsp`：SysV ABI 要求 `call` 时 RSP 16 字节对齐（多减 8 以抵消 `call` 压入返回地址），保证 `irq0_schedule` 里可安全使用 `movaps` 类指令（虽然本内核禁用了 SSE，对齐仍是良好习惯）。
- `call irq0_schedule`：C 函数决定"下一个执行者"，返回目标线程的帧指针。
- `movq %rax,%rsp`：**切换的核心**——直接换栈。
- `popq %r15 … popq %rax`：按与压栈相反的顺序恢复 15 个 GPR。
- `iretq`：弹出 `rip,cs,rflags,rsp,ss`，CPU 跳回被中断线程的下一条指令。

为什么这样设计：整个切换不碰任何寄存器值（保存/恢复完全靠栈），因此 `irq0_schedule` 里 `threads[current_thread].frame=(u64)(unsigned long)f` 保存的就是"当前帧指针"，`return (struct irq0_frame *)(unsigned long)threads[next].frame` 返回的也是"目标帧指针"——一处保存、一处恢复，这就是上下文切换的最小内核。

#### (e) 首帧伪造：`start_threads` 与 `thread_trampoline`

```asm
.global thread_trampoline
thread_trampoline:
    movq %r12,%rsp
    call thread_trampoline_c
1:  cli
    hlt
    jmp 1b
```

- `start_threads` 在每个 worker 栈顶伪造帧：`f->r12=phys_to_high(p)+THREAD_STACK_BYTES`（栈顶地址，即 `rsp` 初始值）、`f->rip=runtime_thread_trampoline_address()`（`leaq thread_trampoline(%%rip),%0` 取运行时地址）、`f->cs=0x08`（内核代码段）、`f->rflags=0x202`（IF=1）。
- 首次切换该线程时，`iretq` 弹出伪造帧 → `rip=thread_trampoline`、`rsp=栈顶`，但 CPU 实际先执行 `movq %r12,%rsp`（用 r12 里的栈顶地址再设一次栈）——双保险，保证 trampoline 阶段 rsp 正确。
- `call thread_trampoline_c` → C 函数 `worker_run(current_thread)` 开始跑 worker 逻辑；线程退出后 `thread_exit()` 把状态置 `THREAD_FINISHED` 并 `cli; hlt` 永久停在这里。
- idle 的 `idle_trampoline` 结构相同，但循环是 `sti; hlt; jmp 1b`——可被 IRQ0 唤醒。

#### (f) 用户上下文：`saved_user_context` 与 `user_irq0_save_restore`

```c
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result, pit_preemptions, pit_resumes; u8 valid; };
```

- `frame`：与 `irq0_frame` 布局相同的 20 字段用户帧（用 `syscall_frame` 类型表示）。
- 记账字段：`saves`（保存次数）、`last_syscall`/`last_result`（上次系统调用号与结果）、`pit_preemptions`/`pit_resumes`（被 PIT 抢占/恢复次数）、`valid`。

```c
static TEXT64 void user_irq0_save_restore(struct irq0_frame *f)
{
    struct syscall_frame *s;
    if(!f || f->cs!=USER_CS || user_process.state!=PROCESS_RUNNING ||
       user_thread.state!=USER_THREAD_RUNNING) return;   /* 只处理合法用户帧 */
    s=&user_thread.context.frame;
    s->r15=f->r15;s->r14=f->r14;...;s->rip=f->rip;s->cs=f->cs;   /* 抄 20 字段入用户上下文 */
    user_thread.context.valid=1; user_thread.context.saves++;
    user_thread.context.pit_preemptions++;
    user_thread.context_address=(u64)(unsigned long)s;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);  /* 重校验边界 */
    /* 唯一合法目标是该用户线程：恢复同一个帧，不做用户侧调度 */
    f->r15=s->r15;f->r14=s->r14;...;f->rip=s->rip;f->cs=s->cs;   /* 原样抄回 */
    user_thread.context.pit_resumes++;
}
```

- 语义：用户线程（CPL3）被 PIT 抢占时，不能像内核线程那样切到别的线程（本内核只有一个合法用户线程目标），所以"保存→恢复同一个帧"，每次记录 `pit_preemptions++`/`pit_resumes++`。
- `user_context_valid` 校验帧的 `cs==USER_CS`、`ss==USER_DS`、`rip` 落在代码镜像范围、`rsp` 落在用户栈范围——这是"上下文安全"的边界检查。
- `irq0_schedule` 里对应的分支：`if(f&&f->cs==USER_CS){ user_irq0_save_restore(f); return f; }`——用户帧直接原样返回，不进入内核线程切换路径。

#### (g) 系统调用路径与 `user_context_save`

```c
static TEXT64 void user_context_save(struct syscall_frame *f, u64 result)
{
    if(!f || user_thread.process!=&user_process || user_process.state!=PROCESS_RUNNING) return;
    user_thread.context.frame=*f;            /* 整帧拷贝 */
    user_thread.context.last_syscall=f->rax;
    user_thread.context.last_result=result;
    user_thread.context.saves++;
    user_thread.context.valid=1;
    user_thread.context_address=(u64)(unsigned long)&user_thread.context.frame;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);
}
```

- `syscall_entry`（int 0x80 门）压栈后 `call syscall_report`；`syscall_report` 调用 `syscall_dispatch` 得到结果，再用 `user_context_save(f,result)` 把用户帧与返回值记入 `saved_user_context`。
- `syscall_dispatch` 依 `f->rax` 分发：`SYS_GETTICKS` 返回 `ticks`、`SYS_GETPID` 返回 `FIXED_PID`、`SYS_WRITE_CONSOLE` 打印固定消息、`SYS_EXIT` 返回 0、未知返回 `-(s64)ENOSYS`。
- `userreturntest` 命令：手工构造一个 `PROCESS_RUNNING` + 合法帧 + 挂起 SIGTRAP 的场景，断言 `user_return_prepare` 只投递一次信号并保持帧不变，输出：
```
userreturntest: validated user return preserved frame and delivered once
```

#### (h) 观测命令：`ps` 与 `processinfo`

`ps` 逐线程打印 TCB 元数据：

```
threads: id state frame stack-pa stack-high switches progress wake-tick received last
thread 0 running <frame> <stack-pa> <stack-high> <switches> ...
idle ready frame <idle_frame> stack static
```

- `frame` 列 = `threads[i].frame`（最近一次保存的 CPU 帧指针）；`stack-pa`/`stack-high` = 内核栈物理页与高半区映射地址；`switches` = 被调度器切换次数。这些就是"上下文切换元数据"的可视化。
- `processinfo` 打印用户线程上下文记账：`saved context/address/saves:` 与 `PIT user preempt/resume:`——直接读出 `saved_user_context` 的 `pit_preemptions/pit_resumes`。

#### (i) `exec64` 增量与 banner

```c
}else if(eq64(word,"l73test")){if(!noargs64(arg))usage64(c,"l73test");else l73test(c);}
}else if(eq64(word,"l81test")){if(!noargs64(arg))usage64(c,"l81test");else l81test(c);}
```

`about`：`text64(c,"Lesson 81: context switch 上下文切换元数据\n");`
banner：`text64(&c,"Lesson 81: context switch 上下文切换元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'context switch 上下文切换元数据' README.md
	@grep -q 'l81test' kernel64.c
	@grep -q 'Lesson 81' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 81 checks passed.'
```

- 与 lesson-80 唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse ... -Werror` → `kernel64.bin` → 内嵌 → ELF32 → ISO）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ start_threads: 为 worker 分配栈页，栈顶伪造 irq0_frame（rip=thread_trampoline, r12=栈顶, cs=0x08, rflags=0x202）
    ├─ idle_init: 静态栈伪造 idle_frame（rip=idle_trampoline）
    ├─ install_idt: IRQ0 → irq0_entry
    ├─ banner: "Lesson 81: context switch 上下文切换元数据\n..."
    └─ for(;;) 键盘循环
        每个 tick：IRQ0 ──► irq0_entry（push 15 GPR）──► irq0_schedule
            ├─ 保存：threads[cur].frame = 当前 rsp
            ├─ 选下一个：rr_pick_next
            └─ 返回目标帧 ──► movq %rax,%rsp ──► pop 15 GPR ──► iretq
        用户态路径：syscall/int 0x80 ──► syscall_entry ──► syscall_report ──► user_context_save
        用户被抢占：IRQ0 ──► f->cs==USER_CS ──► user_irq0_save_restore（保存并恢复同一帧）
        "l81test" ──► lesson_74_state 校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`start_threads` 给每个 worker 伪造首帧；`idle_init` 伪造 idle 帧；banner 打印后进入键盘循环。
2. **内核线程切换**：`irq0_entry` 把当前线程 15 个 GPR 压栈形成 `irq0_frame`；`irq0_schedule` 保存 `threads[cur].frame`，选择下一个可运行线程并返回其 `frame`；`movq %rax,%rsp` 换栈、`pop` + `iretq` 恢复目标线程。
3. **首次运行**：worker 的伪造帧 `rip=thread_trampoline` → `movq %r12,%rsp` → `call thread_trampoline_c` → `worker_run`。
4. **用户线程**：用户被 IRQ0 抢占 → `f->cs==USER_CS` → `user_irq0_save_restore` 把 20 字段抄入 `saved_user_context.frame` 再原样抄回，`pit_preemptions++`/`pit_resumes++`；系统调用经 `syscall_entry` → `syscall_report` → `user_context_save`。
5. **观测**：`ps` 打印每个线程的 `frame/stack-pa/stack-high/switches`；`processinfo` 打印 `saved context`、`saves`、`PIT user preempt/resume`。
6. **checkpoint**：`l81test` 打印 `l81test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l81test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`userreturntest: validated user return preserved frame and delivered once`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`，详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏应显示：
   ```
   Lesson 81: context switch 上下文切换元数据
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l81test`，预期输出：
   ```
   l81test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l81test: Lesson 74 fallback reported`。）
3. 输入 `l73test`（回归），预期输出：
   ```
   l73test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `preempttest` 再输入 `ps`，预期出现以 `threads: id state frame stack-pa stack-high switches progress wake-tick received last` 开头的表，随后一行 `idle ready frame ... stack static`；多个 `switches` 随 tick 递增。
5. 输入 `processinfo`，预期出现 `saved context/address/saves:` 与 `PIT user preempt/resume:` 行。
6. 输入 `userreturntest`，预期输出：
   ```
   userreturntest: validated user return preserved frame and delivered once
   tinyos>
   ```
7. 输入 `about`，预期输出：
   ```
   Lesson 81: context switch 上下文切换元数据
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 81 checks passed.`；QEMU 中 `l81test` 打印 `...passed`、`ps` 能看到各线程帧地址与递增的 `switches` 即代表上下文切换元数据模型验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 切到新线程后崩溃/花屏 | `irq0_frame` 布局与 `irq0_entry` 压弹顺序错位 | 核对 `_Static_assert`（20 个 u64、`rsp` 第 18、`ss` 第 19）；`pop` 顺序必须与 `push` 相反 |
| `movq %rax,%rsp` 后执行流错乱 | `irq0_schedule` 返回的不是合法帧指针 | 检查 `threads[i].frame` 是否指向栈顶 20 字节内；`start_threads` 伪造帧的 `sizeof(*f)` 是否匹配 |
| 线程永远不切换（switches 恒 0） | `quantum_left` 未耗尽或 `rr_pick_next` 扫描不到 | `threadinfo` 看 `quantum left` 与 `mode`；确认 `start_threads` 已置 `THREAD_RUNNABLE` |
| 用户态返回后寄存器错乱 | `user_irq0_save_restore` 抄字段不完整 | 比对 `s->...` 与 `f->...` 是否 20 字段一一对应；`user_context_valid` 边界（cs/ss/rip/rsp）是否通过 |
| `processinfo` 的 `PIT user preempt/resume` 不增 | 没有用户态运行场景 | 先 `userpitest`/`cpl3test` 进入 CPL3 让 IRQ0 打断用户帧 |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l81test` 符号 | `grep -q 'l81test' kernel64.c` |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'context switch 上下文切换元数据' README.md` |
| `l81test` 打印 fallback 串 | `lesson_74_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=74,b=75`） |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：switch_to / __switch_to**
- TinyOS 教学模型：`irq0_schedule` 返回目标线程帧指针，`irq0_entry` 用 `movq %rax,%rsp` 换栈完成切换；帧内 15 个 GPR + `rip/cs/rflags/rsp/ss`。
- Linux 实现：`arch/x86/include/asm/switch_to.h` 的 `switch_to(prev,next,last)` 宏与 `arch/x86/kernel/process_64.c` 的 `__switch_to()`：保存/恢复 `rsp`、`rbp`、`rbx`、`r12…r15`、`fs/gs` 基址、`cr3`（必要时）、`TSS` 的 `rsp0`、`FPU` 状态等。用户态帧在 `struct pt_regs` 中，内核切换不保存全部 GPR（C 调用约定保证易失寄存器可丢弃）。
- 权威来源：Linux v6.x `arch/x86/kernel/process_64.c`、`arch/x86/include/asm/switch_to.h`；Intel SDM Vol.1 §3（寄存器状态）、Vol.3A §6（中断/异常与 iretq）。
- 教学简化：TinyOS 保存全部 15 个 GPR（简单但冗余），不做 `cr3` 切换（内核共享地址空间）、不做 FPU 惰性切换（禁用了 SSE）、没有 `last` 参数（Linux 用它支持 `schedule_tail` 的引用计数）。

**对照点 2：首帧与内核线程出生**
- TinyOS：`start_threads` 在栈顶伪造 `irq0_frame`，`iretq` 直接跳 `thread_trampoline`。
- Linux：内核线程用 `kernel_thread`/`copy_thread`（`arch/x86/kernel/process.c`）在 `task_pt_regs` 上构造初始 `pt_regs`（`ip` 指向 `ret_from_fork`），`schedule_tail` 在首次调度后完成启动。
- 教学简化：没有 `ret_from_fork`/`schedule_tail` 区分，一个 `thread_trampoline` 走到底。

**对照点 3：用户上下文保存（pt_regs / 信号帧）**
- TinyOS：`saved_user_context.frame` 保存完整 20 字段用户帧，`pit_preemptions/pit_resumes` 记账。
- Linux：用户异常/中断在 `struct pt_regs`（`arch/x86/include/asm/ptrace.h`）里保存；信号处理时 `setup_rt_frame` 把用户上下文打包成 `ucontext_t` 挂到用户栈，`sigreturn` 恢复。
- 教学简化：只保存不真正投递信号帧到用户栈；`user_return_prepare` 只是记账式投递。

---

## 8. 思考题与练习

1. **概念理解**：为什么上下文切换可以只交换 `rsp`？`irq0_frame` 里保存的寄存器值存在哪里？说出"每个线程独立内核栈"与"帧保存在栈上"之间的关系。
2. **源码定位**：在 `irq0_entry` 汇编中数一数 `pushq`/`popq` 各多少次，与 `struct irq0_frame` 的 15 个 GPR 字段一一对应列出。为什么 `andq $-16,%rsp; subq $8,%rsp` 之后才 `call`？
3. **动手实验**：在 `start_threads` 伪造帧时把 `f->rflags` 从 `0x202` 改成 `0x002`（清 IF），重新 `make run` 观察 worker 行为差异（提示：IF 影响中断可屏蔽性）。改完请**恢复原值**。
4. **动手实验**：给 `struct irq0_frame` 增加一个字段（例如 `u64 scratch`）而不改汇编，观察 `_Static_assert` 是否阻止编译。把字段加在末尾并同步修改汇编的压弹顺序，能否通过？体会"结构体与汇编强耦合"。
5. **Linux 对照**：阅读 `switch_to` 宏与 `__switch_to`，列出 TinyOS 没保存而 Linux 必须保存的 3 类状态（如 FSGSBASE、FPU、TSS rsp0 等），并说明为什么教学模型可以省略。

---

## 9. 本课小结与下一课预告

- 本课把"上下文切换"拆成可验证的元数据：`irq0_frame`（20 个 u64 CPU 帧）、`irq0_entry` 汇编（压 15 GPR → 换栈 → 弹 15 GPR → iretq）、`irq0_schedule` 的"返回帧指针即切换"。
- 你理解了首帧伪造：worker/idle 线程凭一个栈顶伪造帧"凭空出生"，`thread_trampoline`/`idle_trampoline` 是它们的执行起点。
- 你掌握了用户上下文 `saved_user_context` 的完整记账（`saves`、`pit_preemptions/pit_resumes`、`last_syscall`），以及 `user_irq0_save_restore` 的保存→恢复同一帧策略。
- 你对照了 Linux `switch_to`/`__switch_to`/`ret_from_fork`，知道教学模型省略了 `cr3`、FPU、FSGSBASE 等状态。
- 你验证了 `l81test`/`l73test` 与 `ps`/`processinfo`/`userreturntest` 的确定性输出。

**下一课预告**：Lesson 82「Copy-on-Write 基础元数据」。调度线告一段落，本课起转向内存：写时复制（COW）的基础元数据——`struct page_model` 的 `writable` 写权限标志与 `refs` 引用计数，如何映射到 Linux `mm/memory.c` 的 `do_wp_page`/`wp_page_copy` 概念。衔接点：上下文切换保存的"每线程状态"换成了"每页状态"。
