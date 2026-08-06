# Lesson 29: CPL3 `int 0x80` 最小 syscall ABI — 精讲文档

> **课号**：29　**主题**：minimal CPL3 `int 0x80` syscall ABI
> **课程主线位置**：阶段五（用户态与 syscall）——在 Lesson 28（首次 CPL3 进入）之后、
> Lesson 30（有界 dispatcher）之前，建立第一条「用户→内核→用户」往返：用户态执行
> `int 0x80`，内核经 DPL3 中断门处理，返回 ticks 后 `iretq` 回到用户。
> **前置课程**：[../lesson-28-stable/README.md](../lesson-28-stable/README.md)
> **后续课程**：[../lesson-30-stable/README.md](../lesson-30-stable/README.md)
> **一句话目标**：给用户态提供唯一一个 syscall `SYS_GETTICKS`（号码 0）：用户把号码装入
> EAX 后 `int 0x80`，内核用 all-GPR 帧保存现场、返回 ticks、`iretq` 回用户并继续执行。

> **Course status: stable snapshot (validated; verified build artifacts included).**
> 本目录为已校验稳定快照。旧 README 说明：继承的 Lesson 28 发布稳定标签可能仍描述
> 修复前基线；本学习树**刻意包含**修正后的帧（`RFLAGS=0x002`）、只读用户代码映射与
> PMM 固定用户页。

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能完整描述一条 syscall 的执行链：用户 stub 的
  `mov eax,0` → `int 0x80` → IDT 向量 `0x80`（DPL3 中断门）→ 换栈到 `rsp0` →
  `syscall_entry` 保存 15 个 GPR → `syscall_report` 填 `RAX=ticks` → 汇编重载 RAX →
  `iretq` 回用户（IF 仍关闭）→ 用户进入 `hlt` 循环。

- **在课程主线中的位置**：上一课证明了「能进 CPL3、异常能经 `rsp0` 回来」，但没有
  受控的返回路径（`ud2` 直接停机）。本课把 `ud2` 换成 `int 0x80`，把「停机」换成
  「保存帧 → 填返回值 → `iretq`」，从而让用户代码能**安全地请求内核服务并继续运行**。
  这是 Lesson 30 多号 dispatcher 与 Lesson 31（`SYS_EXIT`）的最小核心。

- **前置知识清单**：
  1. Lesson 28 的 CPL3 进入：`USER_DS=0x2b`/`USER_CS=0x33`、`iretq` 五元组、TSS `rsp0`；
  2. IDT 中断门（`0x8e`）与 DPL 字段：`set_gate` 的 `ist/type` 与「DPL3 门」= type 改 `0xee`；
  3. 汇编调用约定与 GPR 保存/恢复（15 个寄存器 push/pop 顺序）；
  4. 物理帧分配（`pmm_alloc`）与用户页的只读映射（PTE_PRESENT|PTE_USER）；
  5. `ticks` 全局计数（IRQ0 每 10ms 递增）。

- **本课交付**（可见结果）：
  - `idtinfo` 显示 `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1` 与
    `int 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt`；
  - `cpl3test` 进入用户 syscall stub，屏幕打印
    `TinyOS lesson 29 syscall`、`syscall: SYS_GETTICKS (0)`、`return rax: <ticks>`，
    随后打印 `returning with iretq; user IF remains disabled`，用户态停在 `hlt` 循环。

## 2. 核心概念精讲

### 2.1 `int 0x80` 作为软件陷阱

- 用户态不能直接调用内核函数（特权级不匹配），需要一个**同步陷阱**入口：`int n` 指令
  产生一个软件中断，CPU 按 IDT 的 `n` 号门进入内核。x86 Linux 传统上用 `int 0x80`
  （`arch/x86/entry/entry_64.S` 保留兼容路径）；TinyOS 沿用 `0x80` 作为教学惯例。
- 为什么不用 `syscall`/`sysret`：`syscall` 指令要求设置 MSR（STAR/LSTAR），依赖 `swapgs`
  与 GS 基址管理；`int 0x80` 走与异常完全一致的 IDT + TSS `rsp0` 换栈机制，复用本课程
  已建立的异常基础设施，教学上更平滑。

### 2.2 DPL3 中断门（type `0xee`）

- 普通中断门 type=`0x8e`（Present+DPL0+32 位门）；要让 **CPL3** 代码也能 `int 0x80`，
  门的 DPL 必须是 3。`set_gate` 先把 type 设为 `IDT_GATE_INTERRUPT`，`install_idt` 再
  对向量 `0x80` 单独覆盖 `idt[0x80].type=0xee`（`0xee` = `1110 1110`：P=1、DPL=3、
  type=`1110` 中断门）。
- 若门 DPL=0 而 CPL3 发起 `int 0x80`，CPU 产生 #GP——这是常见调试点。

### 2.3 all-GPR 帧（`struct syscall_frame`）

- 进入 syscall 时 CPU 已把 `SS/RSP/RFLAGS/CS/RIP` 压到 `rsp0` 栈；`syscall_entry`
  再按固定顺序压入 15 个 GPR（先 `rax`、后 `rbx/rcx/rdx/rbp/rsi/rdi/r8..r15`，
  `r15` 最后压、处于最低地址）。`struct syscall_frame` 布局与之精确对应：

```
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
```

- 为什么保存全部 GPR：内核处理 syscall 时（本课是 C 函数 `syscall_report`）会改动寄存器，
  返回用户前必须完整恢复；同时 `rax` 槽（offset 112）被用作返回值载体——
  处理器把结果写回 `f->rax`，汇编再 `movq 112(%rdi),%rax` 装入真实 RAX。

### 2.4 只读用户代码映射与修正帧

- 上一课用户代码页带 Writable；本课把用户代码 PT 项改为 `PTE_PRESENT|PTE_USER`
  （**去掉了 Writable**），符合「代码段不可写」的安全直觉，也阻止用户自改指令。
  用户**栈**页仍 `PTE_PRESENT_WRITABLE|PTE_USER`。
- 修正帧：`enter_user_c` 的 RFLAGS 从 `0x202` 改为 `0x002`（只置保留位 bit1，IF 仍为 0）。
  两者 IF 都关闭，但 `0x002` 是更「干净」的初始值；本树以此为修正后的基线。

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|------|------|------------------------|
| `boot.S` | 32 位引导 | **未变化** |
| `kernel.c` | 32 位引导期页表与用户 stub | **主要增量**：用户 stub 改为 `mov eax,0; int 0x80; jmp -2`；用户代码页改只读（去 Writable） |
| `kernel64.c` | 64 位续体 | **主要增量**：`syscall_frame`、`syscall_entry` 汇编、IDT 向量 0x80 DPL3 门、`syscall_report`、`idtinfo` 文案、RFLAGS 修正 |
| `kernel64.ld` | 64 位链接脚本 | **未变化** |
| `linker.ld` | 32 位 ELF | **未变化** |
| `Makefile` | 构建 | **未变化** |
| `grub.cfg` | GRUB 菜单 | **微小变化**：menuentry 文案 |

### 3.2 kernel.c：用户 syscall stub

```c
{ volatile u8 *code=(volatile u8 *)(unsigned long)(u32)long_mode_handoff.user_code_phys;
  volatile u64 *pt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_CODE_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  volatile u64 *st=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_STACK_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  code[0]=0xb8; code[1]=0; code[2]=0; code[3]=0; code[4]=0;   /* mov eax, 0（SYS_GETTICKS） */
  code[5]=0xcd; code[6]=0x80;                                  /* int 0x80 */
  code[7]=0xeb; code[8]=0xfe;                                  /* jmp -2：回到 int 0x80 */
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_code_phys|PTE_PRESENT|PTE_USER;  /* 只读！ */
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
```

- 逐字节说明：`0xb8` 是 `mov eax, imm32`，后跟 4 字节小端立即数 `0`；`0xcd 0x80` 是
  `int 0x80`；`0xeb 0xfe` 是 `jmp rel8=-2`，即死循环反复调用 syscall。
- 用户代码 PT 项 `PTE_PRESENT|PTE_USER`（无 Writable）→ 本课「只读用户代码映射」修正；
  栈页保留 Writable。
- 行为：第一次 `int 0x80` 进入内核打印报告并返回；回用户后 `jmp -2` 再次执行
  `int 0x80`（屏幕会不断刷新 syscall 报告）——所以 `cpl3test` 是一次性观察流程。

### 3.3 kernel64.c：syscall 帧与 IDT DPL3 门

```c
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
extern void ... syscall_entry(void) ...
static TEXT64 u64 runtime_syscall_address(void){u64 v;__asm__ volatile("leaq syscall_entry(%%rip),%0":"=r"(v));return v;}
```

- `runtime_syscall_address` 用 RIP 相对 `leaq` 拿到汇编入口的实际运行地址（PIE 环境必需），
  与既有的 `runtime_bp_address`/`runtime_ud_address` 等模式一致。

`install_idt` 的 syscall 门：

```c
set_gate(&idt[0x80],runtime_syscall_address(),0);
idt[0x80].type=0xee;   /* DPL3 中断门：让 CPL3 也能 int 0x80 */
```

- `set_gate` 先把 type 置为 `0x8e`（DPL0），随后单独把 `0x80` 门的 type 改成 `0xee`
  （DPL=3）。`ist=0`（syscall 用普通 `rsp0` 栈，不走 IST1——IST1 只留给 #PF）。

#### 函数：`syscall_report`

```c
TEXT64 void syscall_report(struct syscall_frame*f){
    u16 c=0;clear64(&c);
    text64(&c,"TinyOS lesson 29 syscall\nsyscall: SYS_GETTICKS (0)\nreturn rax: ");
    f->rax=ticks;                     /* 返回值写回帧的 rax 槽 */
    hex64(&c,f->rax);
    text64(&c,"\nuser rip: ");hex64(&c,f->rip);
    text64(&c,"\nuser cs: ");hex64(&c,f->cs);
    text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);
    text64(&c,"\nuser ss: ");hex64(&c,f->ss);
    text64(&c,"\nreturning with iretq; user IF remains disabled\n");
}
```

- 签名与职责：接收 all-GPR 帧指针，打印 syscall 报告并把 `ticks` 写回 `f->rax`。
- 输入输出：输入 CPU 帧 + 15 GPR；输出 VGA 报告与 `f->rax=ticks`。
- 算法步骤：(1) 清屏；(2) 打印头部与 syscall 名；(3) `f->rax=ticks`；(4) 打印返回值与
  用户现场（rip/cs/rsp/ss）；(5) 打印返回提示。
- 为什么打印用户 `rsp`/`ss`：它们是 CPU 压入的用户栈与用户段，证明这次调用确实来自
  CPL3，且 `iretq` 能精确回到用户现场。
- 边界与错误处理：本课只有 `SYS_GETTICKS` 一个号码，不做号码校验（Lesson 30 才引入
  dispatcher 与 `-ENOSYS`）。

#### 汇编：`syscall_entry`

```asm
.global syscall_entry
syscall_entry:
    pushq %rax          /* rax 最先压（帧最深处，offset 112） */
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
    pushq %r15          /* r15 最后压，位于最低地址（offset 0） */
    cld
    movq %rsp,%rdi      /* 帧基址 → 第一个参数 */
    call syscall_report /* C 处理器：打印 + f->rax=ticks */
    movq 112(%rdi),%rax /* 从帧的 rax 槽重载返回值 */
    popq %r15           /* 逆序恢复 15 个 GPR（跳过 rax，由 addq 处理） */
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
    addq $8,%rsp        /* 丢弃 rax 槽（返回值已从帧载入 RAX） */
    iretq               /* 弹出 RIP/CS/RFLAGS/RSP/SS，回到用户 */
```

- 逐段说明：
  1. **保存**：`rax` 在帧内 offset 112；压入顺序决定 `struct syscall_frame` 字段顺序，
     两者必须严格一致（`_Static_assert` 只保证 20×8 字节，字段顺序靠人核对）。
  2. **调用 C**：`movq %rsp,%rdi` 传帧基址；`cld` 保证方向标志（与 IRQ0/IRQ1 入口一致）。
  3. **返回值**：`syscall_report` 已把 `ticks` 写进 `f->rax`，`movq 112(%rdi),%rax`
     把它装入真实 RAX——这是「返回值在帧中传递」的关键一步。
  4. **恢复**：逆序 pop 14 个 GPR；`rax` 槽用 `addq $8,%rsp` 跳过（其内容已进入 RAX）。
  5. **返回**：`iretq` 恢复用户 RIP/CS/RFLAGS/RSP/SS，`CS=0x33` 触发降权回 CPL3。

### 3.4 `enter_user_c` 的修正帧与命令文案

```asm
enter_user_c:
    pushq $0x2b
    pushq $0x00801000
    pushq $0x002          /* 修正：RFLAGS=0x002（bit1 置位，IF=0） */
    pushq $0x33
    pushq $0x00400000
    iretq
```

- 与 Lesson 28 相比唯一变化是 RFLAGS `0x202→0x002`（都关 IF，`0x002` 更干净）。
- `cpl3test` 分支打印 `entering CPL3 syscall stub with IF=0`；`idtinfo` 增加
  `int 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt` 行，
  `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1`。

### 3.5 构建管线与主控制流

- `Makefile` 未变化；banner（源码逐字）：

```
TinyOS lesson 29: CPL3 int 0x80 syscall ABI
All-GPR syscall frame; scheduler and user IRQs remain disabled
```

- 主控制流：`cpl3test` → `enter_user` → `enter_user_c`（iretq 降权）→ 用户
  `mov eax,0` → `int 0x80` → 门 DPL3 检查通过 → CPU 用 `rsp0` 换栈并压
  SS/RSP/RFLAGS/CS/RIP → `syscall_entry` 压 15 GPR → `syscall_report` →
  RAX 重载 → `iretq` → 用户 `jmp -2` 循环。

## 4. 数据流与运行逻辑

- 输入命令 `cpl3test` → `exec64` 打印 `entering CPL3 syscall stub with IF=0` →
  `enter_user` → `iretq` 降权到 CPL3，RIP=`0x00400000`。
- 用户执行 `mov eax,0; int 0x80` → IDT[0x80]（DPL3 门）→ 特权级升到 0，TSS `rsp0` 换栈
  → `syscall_entry` 保存 15 GPR。
- `syscall_report`：清屏打印 `TinyOS lesson 29 syscall` / `syscall: SYS_GETTICKS (0)` /
  `return rax: <ticks>` / 用户 rip/cs/rsp/ss / `returning with iretq; user IF remains disabled`。
- 汇编把 `f->rax`（=ticks）载入 RAX → pop 14 GPR → `addq $8,%rsp` → `iretq` 回用户
  （IF 仍 0）→ `jmp -2` 再次 `int 0x80`。

## 5. 构建、运行与验证

- **构建命令**（与 Makefile 一致）：

```bash
cd /home/dongyu/.zcode/workspace/default/lessons/lesson-29-stable
make clean && make -j"$(nproc)"
make check
```

- **运行命令**：`make run`（QEMU VGA 图形窗口，勿加 `-display none`）。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. 启动后输入 `idtinfo`：预期首行
     `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1`，末行
     `int 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt`。
  2. 从**全新启动**运行 `cpl3test`：预期先打印 `entering CPL3 syscall stub with IF=0`，
     随后清屏出现
     ```
     TinyOS lesson 29 syscall
     syscall: SYS_GETTICKS (0)
     return rax: <ticks 十六进制>
     user rip: 0000000000400005
     user cs: 0000000000000033
     user rsp: 0000000000800ff8
     user ss: 000000000000002b
     returning with iretq; user IF remains disabled
     ```
     （`user rip` 指向 `int 0x80` 之后的下一条 `jmp`，`user rsp` 在用户栈页内，
     `return rax` 随 `tickinfo` 的 `ticks` 增长而变化。）
  3. 用户态继续 `int 0x80` 循环（屏幕重复刷新 syscall 报告）；用 `tickinfo`/`uptime`
     核对 `return rax` 与内核 `ticks` 一致。
  4. 回归 `lminfo`、`hhinfo`、`meminfo`、`tssinfo`、`stackinfo`、`vminfo`/`vmtest`、
     `bptest`、`preempttest`、`idletest`（与上几课一致）。
- **判断成功**：`idtinfo` 显示 DPL3 门；`cpl3test` 的 syscall 屏 `user cs=0x33`、
  `user ss=0x2b`、`return rax` 非零且与 `tickinfo` 一致；屏幕末尾提示
  `returning with iretq; user IF remains disabled`。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `cpl3test` 后 #GP 而非 syscall | IDT[0x80] 门 DPL 仍为 0（`idt[0x80].type=0xee` 未执行或顺序错） | `idtinfo` 看 `DPL: 0000000000000003`；检查 `install_idt` 中 `set_gate` 后覆盖 type |
| 屏幕无 `TinyOS lesson 29 syscall` | 用户 stub 字节写错（`0xcd 0x80` 缺失） | 检查 `kernel.c` 的 `code[5]=0xcd; code[6]=0x80`；QEMU `-d int` 看是否触发 `int 0x80` |
| `return rax` 恒 0 | `f->rax=ticks` 未生效或 `movq 112(%rdi),%rax` 偏移错 | 核对 `syscall_frame` 中 `rax` 是否在 offset 112；确认 `syscall_report` 写的是 `f->rax` |
| 恢复时寄存器错乱/死机 | pop 顺序与 push 顺序不一致，或 `addq $8,%rsp` 被省略 | 对拍 `syscall_entry` 的 push/pop 列表；`rax` 槽必须跳过而非 pop |
| 用户代码页写入触发 #PF | 用户代码 PTE 已被改为只读（预期行为） | `cpl3test` 验证只读；`pageinfo <user_code_phys>` 确认 `allocated` |
| `cpl3test` 进入后无 `hlt` 停顿表现 | `jmp -2`（`0xeb 0xfe`）字节错，跳到其它地址 | 检查 `code[7]=0xeb; code[8]=0xfe`；确认用户 RIP 起点为 `0x00400000` |
| `tssinfo`/`rsp0` 与 handler rsp 不符 | 上几课 `runtime_tss.rsp0` 配置被改动 | 回归 Lesson 28 的 `tssinfo` 与 `stackinfo` 输出 |

## 7. 与 Linux 源码对照

- **TinyOS**：`int 0x80` + DPL3 中断门 + all-GPR 帧 + `iretq`；返回值经 `f->rax` 槽传递；
  用户 `RFLAGS=0x002`（IF=0），用户态中断关闭。
- **Linux 对照**：
  - 传统 `int 0x80` 路径：`arch/x86/entry/entry_64.S` 的 `ia32_syscall`，用
    `pt_regs` 保存全部寄存器后调 `do_syscall_64`，`RAX` 槽存放返回码；
  - 现代 `syscall` 指令路径：`entry_SYSCALL_64` + `swapgs`，用 MSR `STAR/LSTAR`
    （`arch/x86/kernel/cpu/common.c`）；
  - 用户/内核段选择子 `__USER_CS=0x33`/`__USER_DS=0x2b`（`arch/x86/include/asm/segment.h`）。
- **权威来源**：Intel SDM Vol.3 §6.4.4（中断门与 DPL）、§6.7（`int n` 与 #GP 条件）、
  §4.3（页表）；Linux `arch/x86/entry/entry_64.S`。
- **教学模型简化**：单 syscall 无号码分派；不 `swapgs`；用户 IF=0（无用户态中断）；
  无用户返回路径选择器；`jmp -2` 死循环代替真实用户程序。

## 8. 思考题与练习

1. **概念理解**：为什么门 DPL 必须是 3 才能让 CPL3 发 `int 0x80`？如果把
   `idt[0x80].type` 改回 `0x8e`，`cpl3test` 会看到什么异常？
2. **源码定位**：画出 `syscall_frame` 字段与 `syscall_entry` push/pop 顺序的对应表，
   确认 `rax` 槽 offset 112 与 `addq $8,%rsp` 的一致性。
3. **动手实验**：把 `movq 112(%rdi),%rax` 改为 `movq 104(%rdi),%rax`（rbx 槽），运行
   `cpl3test`，观察 `return rax` 变成什么，说明返回值载体为什么必须是 rax 槽。
4. **动手实验**：把用户代码 PTE 改回 `PTE_PRESENT_WRITABLE|PTE_USER`，运行
   `cpl3test`，说明「只读用户代码」在哪些场景能防住自改代码攻击。
5. **Linux 对照**：阅读 `arch/x86/entry/entry_64.S` 的 `entry_SYSCALL_64`，对比
   TinyOS `syscall_entry` 在「保存寄存器 → 调用 C → 恢复 → 返回」四段上的异同，
   并说明 `swapgs` 为什么在本课可省略。

## 9. 本课小结与下一课预告

- 本课建立了第一条 CPL3 syscall 往返：用户 `mov eax,0; int 0x80` → IDT 向量 `0x80`
  的 DPL3 中断门 → TSS `rsp0` 换栈 → `syscall_entry`。
- `struct syscall_frame`（20×8 字节）统一定义 all-GPR 帧布局；`syscall_entry` 先压 15 GPR、
  调 `syscall_report`、再从帧的 `rax` 槽重载返回值、逆序恢复后 `iretq`。
- 用户 stub 改为只读映射（`PTE_PRESENT|PTE_USER`），`enter_user_c` 的 RFLAGS 修正为
  `0x002`；这两个修正是本树对 Lesson 28 基线的明确改进。
- `idtinfo` 展示 DPL3 门配置；`cpl3test` 的屏幕输出 `user cs=0x33`/`user ss=0x2b`/
  `return rax=<ticks>` 构成完整验证。
- 本课只有 `SYS_GETTICKS`，无号码校验、无 dispatcher；调度器与用户 IRQ 保持关闭。
- **下一课**（[../lesson-30-stable/README.md](../lesson-30-stable/README.md)）：把单一
  syscall 扩展为**有界 dispatcher**——`SYS_GETTICKS(0)`/`SYS_GETPID(1)`/
  `SYS_WRITE_CONSOLE(2)` 三号 + 未知号返回 `-ENOSYS`；用户 stub 依序调用
  `0,1,2,99`，`syscall_dispatch` 用 switch 分派并保持 all-GPR 帧与 IF=0。
