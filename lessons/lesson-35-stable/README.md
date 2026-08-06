# Lesson 35: CPL3-origin IRQ0——单用户线程 RIP/CS/RFLAGS/RSP/SS 保存恢复与有界 PIT 抢占 — 精讲文档

> **课号**：Lesson 35
> **主题**：CPL3-origin IRQ0：单用户线程 RIP/CS/RFLAGS/RSP/SS 保存恢复与有界
> PIT 抢占
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）第四课，
> 「用户线程被定时器抢占」这一能力的第一次出现。
> **前置课程**：[Lesson 34（有界 process/thread 对象与受控生命周期）](../lesson-34-stable/README.md)
> **后续课程**：[Lesson 36（有界多用户程序运行时与退出回收）](../lesson-36-stable/README.md)
> **一句话目标**：学完本课，你能说清「IRQ0 如何按 `CS==USER_CS` 识别一次
> CPL3-origin 的中断帧，把完整的特权返回帧（RIP/CS/RFLAGS/RSP/SS + 全部 GPR）
> 复制进线程上下文并校验，再原样恢复，从而对单个用户线程完成有界 PIT 抢占」。

---

## 1. 课程定位（Mission）

- **一句话目标**：让 PIT（100 Hz）第一次具备「抢占用户态」的完整路径：IRQ0
  入口识别 CPL3 来源帧 → `user_irq0_save_restore` 把 20 个 qword 完整保存进
  `saved_user_context` → 范围校验 → 原样恢复 → 唯一 `iretq` 返回同一用户线程。
  不做用户 IRQ 回调、不切换用户地址空间、不引入第二用户线程。
- **课程主线位置**：第 2 阶段的 IRQ0 抢占（Lesson 18）抢的是**内核线程**（CPL0
  帧，只有 rip/cs/rflags 3 项）；本课把抢占对象扩展为**用户线程**（CPL3 帧，
  CPU 多压 rsp/ss 两项）。这是「用户态调度」的最小原型，也是 Lesson 36
  多程序、Lesson 37+ 调度归属模型的前置。
- **前置知识清单**：
  1. `struct irq0_frame`（旧 18 qword）与 `irq0_entry`/`irq0_schedule` 的唯一
     `iretq` 返回路径（Lesson 18/23）；
  2. process/thread 对象与 `saved_user_context`（Lesson 34）；
  3. CPU 中断帧的 CPL 相关差异：特权级变化时多压 `SS:RSP`（Intel SDM Vol.3
     §6.12）；
  4. 用户帧 IF=0 策略与 `enter_user_c` 的 RFLAGS 构造（Lesson 28–34）。
- **本课交付**：`processinfo` 新增 `PIT user preempt/resume` 计数；新命令
  `userpitest`；`irq0_frame` 扩到 20 qword 且带偏移断言；banner/about 标注
  Lesson 35 与 CPL3 IRQ0 边界。

---

## 2. 核心概念精讲

### 2.1 CPL3-origin 中断帧：CPU 多压 `SS:RSP`，帧从 18 变 20

**定义**：中断发生时 CPU 会压入返回帧。若目标 CPL 与当前不同（0→3 或 3→0），
帧含 `SS:RSP`；若 CPL 相同，则不含。因此 IRQ0 的用户来源帧比内核来源帧多 2 个
qword：

```c
/* IRQ0 pushes GPRs; CPL3 delivery adds the CPU's rsp/ss pair. */
struct irq0_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct irq0_frame)==20*sizeof(u64),"IRQ0 frame");
_Static_assert(__builtin_offsetof(struct irq0_frame,rsp)==18*sizeof(u64),"IRQ0 rsp offset");
_Static_assert(__builtin_offsetof(struct irq0_frame,ss)==19*sizeof(u64),"IRQ0 ss offset");
```

**为什么需要**：`iretq` 恢复用户态必须知道返回去哪个 RSP、用哪个 SS；这两个值
只存在于 CPL3 来源的帧里。若仍用 18 qword 结构体，读 `rsp/ss` 会读到内核栈的
垃圾数据。`_Static_assert` 在编译期锁死 `rsp` 在偏移 144、`ss` 在 152。
**工作机制**：`irq0_entry` 汇编压 15 个 GPR 后把 `%rsp` 交给
`irq0_schedule`；`irq0_schedule` 用 `f->cs==USER_CS` 判别来源——CPL0 帧的
`cs=0x08`（KERNEL_CS），永不误入用户路径。

### 2.2 IRQ0 分发：按 `CS` 分叉的唯一 iretq 路径

**定义**（`irq0_schedule` 的开头）：

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){
    u8 old,next;
    ticks++;
    outb64(PIC1_COMMAND,PIC_EOI);
    if(f&&f->cs==USER_CS){ user_irq0_save_restore(f); return f; }
    /* ……以下仍是 Lesson 18–23 的内核线程调度路径…… */
}
```

**为什么需要**：同一条 `irq0_entry`/`iretq` 管线要同时服务内核线程与用户线程。
`CS` 是唯一可靠判别位：内核线程帧 `cs=0x08`，用户线程帧 `cs=0x33`（USER_CS）。
**工作机制**：用户来源时，保存/恢复函数原地处理帧并**返回原帧 `f`**，`movq
%rax,%rsp` 回到原栈、`iretq` 弹 5 元组回用户态——**不经过**内核调度器
（`next_runnable`/`quantum_left` 都不动），保证「不切换到其他用户地址空间」。
对 CPL0 来源仍走完整调度路径。

### 2.3 保存/恢复：20 qword 双向复制 + 记账

**定义**：

```c
static TEXT64 void user_irq0_save_restore(struct irq0_frame *f)
{
    struct syscall_frame *s;
    if(!f || f->cs!=USER_CS || user_process.state!=PROCESS_RUNNING ||
       user_thread.state!=USER_THREAD_RUNNING) return;
    s=&user_thread.context.frame;
    s->r15=f->r15; s->r14=f->r14; ... s->rax=f->rax; s->rip=f->rip;
    s->cs=f->cs; s->rflags=f->rflags; s->rsp=f->rsp; s->ss=f->ss;
    user_thread.context.valid=1; user_thread.context.saves++;
    user_thread.context.pit_preemptions++;
    user_thread.context_address=(u64)(unsigned long)s;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);
    /* The single user thread is the only legal destination: restore the exact
       validated frame rather than attempting a user-side IRQ or task switch. */
    f->r15=s->r15; ... f->cs=s->cs; f->rflags=s->rflags;
    f->rsp=s->rsp; f->ss=s->ss;
    user_thread.context.pit_resumes++;
}
```

**为什么需要**：用户帧的 20 个字段是「特权返回的完整事实」。保存方向
`f → s` 把现场沉淀到线程对象（持久、可审计）；恢复方向 `s → f` 原样回写，
保证用户线程在抢占后**从被中断的那条指令继续**。
**工作机制**：`pit_preemptions` 记抢占次数、`pit_resumes` 记恢复次数——
两个计数在 `processinfo` 里成对显示；`saves++` 与 Lesson 34 的 syscall 保存共用
计数器，体现「同一上下文对象承载两类保存」。

### 2.4 范围校验：RIP/RSP 从「精确」放宽为「区间」

**定义**：

```c
static TEXT64 int user_context_valid(struct saved_user_context *c)
{
    u64 code_end=user_process.entry+(u64)user_process.image_bytes;
    return c && c->valid && c->frame.cs==USER_CS && c->frame.ss==USER_DS &&
        c->frame.rip>=user_process.entry && c->frame.rip<code_end &&
        c->frame.rsp>=USER_STACK_VA && c->frame.rsp<=user_process.stack_top;
}
```

**为什么需要**：Lesson 34 用精确匹配（`rip==USER_CODE_VA`）成立的前提是「用户
帧从不前进」（IF=0、无抢占）。一旦允许抢占，被中断时的 RIP 可以是镜像里的任何
位置，RSP 也可以是栈内任意值。校验必须改成**区间**：RIP 落在
`[entry, entry+image_bytes)` 内、RSP 落在 `[USER_STACK_VA, stack_top]` 内，
否则上下文无效。**边界**：`code_end` 用 `u64` 加法，`image_bytes=7` 使合法 RIP
范围是入口起的 7 字节（镜像第 0 段），若镜像未来更长只需改元数据。

### 2.5 IF 策略与「有界」的边界

本课用户帧 RFLAGS 仍由 `enter_user_c` 压 `0x002`（IF=0）。因此**演示路径中
IRQ0 不会真正在用户态触发**；`user_irq0_save_restore` 是「一旦 IF 被打开，
抢占路径即正确」的防御性实现。若把 RFLAGS 改成 IF=1（思考题 4 的实验），
IRQ0 就会在用户态触发并走本课的保存/恢复路径，这也解释了为什么本课必须先把
帧结构与校验准备好。**不做**：用户 IRQ 回调、跨地址空间切换、第二用户线程。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 34） |
|---|---|---|
| `kernel64.c` | 64 位主内核 | **核心增量**：`irq0_frame` 扩为 20 qword（含 rsp/ss）+ 两个 offset 断言；`saved_user_context` 增 `pit_preemptions/pit_resumes`；`user_context_valid` 改范围校验；新增 `user_irq0_save_restore`；`irq0_schedule` 按 `CS` 分叉；`processinfo` 增 PIT 计数行；`exec64` 增 `userpitest`；banner/about 文案 |
| `kernel.c` | 32 位引导 + 内嵌镜像 | 未变化 |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位链接脚本 | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 数据结构增量

```c
struct irq0_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result, pit_preemptions, pit_resumes; u8 valid; };
```

- `irq0_frame` 与 `syscall_frame` 布局**完全一致**（都是 20 qword，偏移相同），
  这是 `s=&user_thread.context.frame; s->r15=f->r15; ...` 逐字段复制能成立的基础；
- 三个 `_Static_assert` 锁死：大小 160 字节、rsp 偏移 144、ss 偏移 152——
  任何字段重排都会编译失败。

### 3.3 irq0_schedule —— CPL3 分叉 + 内核调度不变（关键函数）

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;
ticks++;
outb64(PIC1_COMMAND,PIC_EOI);
if(f&&f->cs==USER_CS){ user_irq0_save_restore(f); return f; }
if(idle_running){ idle_frame=f; idle_ticks++; }
else threads[current_thread].frame=(u64)(unsigned long)f;
wake_sleepers(); reap_finished_threads();
if(!idle_running&&quantum_left){ quantum_left--; if(quantum_left) return f; }
old=current_thread; next=next_runnable(); quantum_left=TIME_SLICE_TICKS;
/* ……后段与 Lesson 18–23 完全相同：next==0xff 回 idle、next!=old 切换…… */
}
```

- **签名与职责**：IRQ0 的唯一 C 入口，返回「应被 `iretq` 恢复的帧指针」。
- **算法步骤**：① `ticks++` 并 `PIC_EOI` 应答；② `f->cs==USER_CS` 时直接走
  用户保存/恢复并返回原帧——**跳过**内核线程记账与切换；③ 否则进入既有
  CPL0 调度路径（保存当前线程帧、唤醒 sleepers、收尸、时间片判断、轮转、
  idle 切换）。
- **边界与错误处理**：`f&&f->cs==USER_CS` 双条件避免空帧解引用；CPL0 帧
  `cs=0x08` 永不走用户路径；用户路径不调用 `pmm_free_page`，不触碰其他
  地址空间。
- **为什么这样设计**：把「是否抢占用户」的判定收敛到一个 `if`，使既有内核
  调度行为零回归——Lesson 36 将沿用同一分叉模式。

### 3.4 user_irq0_save_restore —— 完整保存与恢复（关键函数）

- **签名与职责**：就地保存/恢复用户 IRQ0 帧；前置条件失败时静默返回。
- **算法步骤**：① 四重前置检查：`f` 非空、`cs==USER_CS`、进程 RUNNING、线程
  RUNNING；② 逐字段 `f → s` 复制（15 GPR + rip/cs/rflags/rsp/ss）；③ 置
  `valid=1`、`saves++`、`pit_preemptions++`，缓存 `context_address`，用
  `user_context_valid` 刷新 `context_valid`；④ 逐字段 `s → f` 原样写回；
  ⑤ `pit_resumes++`。
- **边界与错误处理**：任何前置不满足（如进程已 EXITED）直接 return——保持帧
  原样、不记账；恢复是对称复制，不存在「保存了却恢复错」的路径。
- **为什么这样设计**：复制而非移动，因为 `saved_user_context` 要**长期保留**
  历史现场（`saves` 可增长），而帧是每次抢占的瞬时载体。

### 3.5 processinfo 增量 —— PIT 计数展示

```c
text64(c,"\nPIT user preempt/resume: ");hex64(c,user_thread.context.pit_preemptions);
text64(c," ");hex64(c,user_thread.context.pit_resumes);
```

在既有 `saved context/address/saves` 行之后追加一行，`preempt/resume` 成对出现，
任何一次「保存了但没恢复」都会暴露为计数不匹配。

### 3.6 exec64 / 启动初始化增量

- `help` 文案在 `clear` 前新增 `userpitest`。
- 新命令 `userpitest`（逐字摘自 `exec64`）：

  ```c
  }else if(eq64(word,"userpitest")){
      if(!noargs64(arg))usage64(c,"userpitest");
      else{ text64(c,"entering CPL3 with IF=0; IRQ0 saves/restores one bounded user frame\n"); enter_user(h); }
  }
  ```

- `about`：`TinyOS lesson 35: bounded CPL3 PIT preemption with saved user context`
- banner（逐字摘自 `kernel_main64_binary`）：

  ```
  TinyOS lesson 35: validated user image with bounded PIT preemption
  GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
  ```

### 3.7 构建管线

Makefile/链接脚本与 Lesson 34 完全一致，无新增构建步骤。三个 `_Static_assert`
在 `make` 阶段提供帧布局的编译期保证。

### 3.8 主控制流

```
irq0_entry（汇编：压 15 GPR）→ irq0_schedule
  ├─ f->cs==USER_CS ──▶ user_irq0_save_restore（保存→校验→恢复）→ return f
  │                        └─ movq %rax,%rsp → iretq → 回到同一用户线程被中断点
  └─ CPL0 帧 ──▶ 既有内核线程调度路径 → 返回调度帧 → iretq
```

---

## 4. 数据流与运行逻辑

1. **启动**：帧结构扩为 20 qword；`start_threads`/`idle_init` 构造的 CPL0 帧
   （`cs=0x08`）占用前 18 个槽，`rsp/ss` 槽不被 `iretq` 读取。
2. **processtest/processinfo**：验证对象图并显示
   `PIT user preempt/resume: 0000000000000000 0000000000000000`（尚未抢占）。
3. **cpl3test**（IF=0 进入）：5 次 `int 0x80`，每次 `syscall_report` 走
   `user_context_save`；EXIT 走 `user_process_exit` 并停机。由于 IF=0，IRQ0
   不会在用户态触发，`pit_preemptions` 保持 0。
4. **userpitest**：与 cpl3test 同一入口（`enter_user`），banner 文案不同；
   同样 IF=0，最终 EXIT 停机。
5. **观察抢占**（动手实验）：若把 `enter_user_c` 的 RFLAGS 改为 IF=1 重编，
   IRQ0 在用户态触发 → `irq0_schedule` 的 `cs==USER_CS` 分支 → 保存 20 qword、
   `pit_preemptions++`、范围校验 → 原样恢复、`pit_resumes++` → `iretq` 回用户。
  再进 syscall 或 EXIT 时，`processinfo` 可见 `PIT user preempt/resume` 非零。

---

## 5. 构建、运行与验证

**依赖**：与 Lesson 34 相同。

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

2. `cpl3test`：镜像 0、1、2、99、3 执行；末屏（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 35 SYS_EXIT
   user requested controlled exit
   saved user context validated; process/thread exited
   halting intentionally
   ```

3. 冷启动后 `userpitest`，首行（逐字摘自 `exec64`）：

   ```
   entering CPL3 with IF=0; IRQ0 saves/restores one bounded user frame
   ```

4. 冷启动后 `processinfo`，应包含（逐字摘自 `processinfo`）：

   ```
   PIT user preempt/resume: 0000000000000000 0000000000000000
   ```

5. 回归：`syscallinfo` 确认 ABI 与 PIT 边界；`threadinfo` 确认继承的 PIT 速率
   与调度策略（`PIT channel 0: 0000000000000064 Hz`、`IRQ0 schedules: yes`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 编译期断言失败（IRQ0 frame / rsp offset / ss offset） | `irq0_frame` 字段被改或顺序变了 | 核对三个 `_Static_assert` 与结构体字段顺序 |
| IRQ0 在用户态触发后三线性/回环错误 | `rsp/ss` 未保存或恢复（帧仍是 18 qword） | 确认 `struct irq0_frame` 已含 `rsp,ss` 且 `user_irq0_save_restore` 复制了它们 |
| 用户帧被误判为 CPL0（走内核调度） | `f->cs` 不等于 `USER_CS` | 检查 `enter_user_c` 压入的 CS 是否 `0x33`；检查 GDT 用户段 base |
| `processinfo` 的 preempt/resume 不匹配 | 保存后恢复方向少抄字段或提前 return | 对照 `f→s` 与 `s→f` 两段逐字段清单（各 20 项） |
| 上下文被判 `not validated` | RIP/RSP 越过 `user_context_valid` 的范围 | 复算 `code_end=entry+image_bytes`（入口+7）与栈区间 `[USER_STACK_VA, stack_top]` |
| 改 IF=1 后 EXIT 前进程卡死 | 抢占发生在 EXIT 报告之后，进程已 EXITED 时 IRQ0 再次触发 | `user_irq0_save_restore` 的前置要求进程 RUNNING；EXITED 后静默返回 |
| `userpitest`/`cpl3test` 行为完全相同 | 两命令共用 `enter_user`，仅文案不同 | 这是**预期**：差异在过程可见性而非行为 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| 按 `CS==USER_CS` 判别 CPL3 来源帧 | `arch/x86/entry/entry_64.S` 的 `idtentry`/`paranoid_entry` 用 `USER_CS`/`kernel CS` 判别与 `SWAPGS` | Linux 还有 `SWAPGS`、`paranoid`、`pti` 切换等；TinyOS 只看 CS 一位 |
| `user_irq0_save_restore` 20 qword 保存/恢复 | `struct pt_regs` 全量保存 + `iretq` | Linux 会处理 `sysret`/`iret` 双路径与 `fs/gs` 基址；TinyOS 只保存通用帧 |
| 用户上下文范围校验（RIP/RSP 区间） | `arch/x86/mm/fault.c` 的 user-range 检查与 `access_ok()` | Linux 用 `TASK_SIZE`/`access_ok` 与 VMA 权限；TinyOS 用 entry+image_bytes 与栈区间 |
| 单用户线程「有界抢占」，不做切换 | 调度器 `schedule()` 在时钟中断里选择 next task | Linux 每次 tick 都做运行队列选择与切换；TinyOS 抢占只是保存/恢复同一线程 |
| PIT 100 Hz 有界频率 | `kernel/time/tick-sched.c` + `timer_interrupt` | Linux 用高精度时钟与 `CONFIG_HZ`；TinyOS 用 8254 固定分频 |

权威来源：Intel SDM Vol.3 §6.12（中断帧与 CPL 变化）、§7.4（TSS/RSP）、
Linux `arch/x86/entry/entry_64.S`。

---

## 8. 思考题与练习

1. **概念理解**：为什么 CPL3-origin 的 IRQ0 帧比 CPL0-origin 多 `SS:RSP` 两项？
   用 Intel SDM §6.12 的压栈规则解释。
2. **源码定位**：在 `kernel64.c` 中找出三个 `_Static_assert` 锁定的偏移
   （rsp=144、ss=152），并说明它们为什么能发现「字段顺序被改」这类编译期错误。
3. **动手实验**：把 `enter_user_c` 的 RFLAGS 从 `0x002` 改为 `0x202`（IF=1），
   重编运行 `userpitest`，观察 `processinfo` 的 `PIT user preempt/resume` 是否
   变为非零、EXIT 是否仍正常——体会「先备好抢占路径再开 IF」的顺序意义。
4. **动手实验**：把 `user_context_valid` 的 `rsp<=user_process.stack_top` 改成
   `rsp==user_process.stack_top`，重跑 IF=1 实验，观察抢占后上下文是否被判无效。
5. **Linux 对照**：阅读 `arch/x86/entry/entry_64.S` 中时钟中断入口如何用
   `SWAPGS` 与 CS 判别区分内核/用户来源，比较它与 `irq0_schedule` 的 `CS` 判别
   在健壮性上的差距。

---

## 9. 本课小结与下一课预告

- `irq0_frame` 扩为 20 qword，`rsp/ss` 偏移由编译期断言锁定，CPL3 来源帧的
  完整特权返回信息从此可保存。
- `irq0_schedule` 以 `f->cs==USER_CS` 一次分叉：用户帧走
  `user_irq0_save_restore` 保存/校验/恢复并返回原帧，内核帧走既有调度器。
- 保存/恢复是 20 字段的双向对称复制，`pit_preemptions/pit_resumes` 成对记账；
  `user_context_valid` 从精确匹配放宽为区间匹配，适配抢占推进 RIP/RSP。
- 用户 IF 仍为 0，演示路径不会真正触发用户 IRQ0；本课把「一旦开 IF 即正确」
  的路径完整备好。
- 全链路只服务一个用户线程：不做用户 IRQ 回调、不做跨地址空间切换。

**下一课（Lesson 36）**：把单进程模型推广为**有界多用户程序运行时**——两个固定
进程（PID 1/2）、两个 address-space 对象、两个用户线程对象、两对代码/栈物理页，
`processtest` 校验双对象，`SYS_EXIT` 之后支持 `EXITED → EMPTY` 的退出回收计数
（`user_reclaims`），PMM 拒绝释放两对内嵌帧。多程序共存与退出回收闭环完成
本阶段「用户程序运行时」的收尾。
